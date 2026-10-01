"""Stage 1 experiments (plan Tasks 13-14): the prototype on the development set with one parameter group
changed, scored like the suite and compared signal by signal with a reference run; the periodicity estimator
evaluated offline on stored branch-1 posteriors; and the synthetic speed-step follow test.

    python -m kz4ap_proto.experiments run --out build/suite/full3 --bench PATH --name exp-ref [--set KEY=VALUE ...] [--keep-p1] [--jobs N]
    python -m kz4ap_proto.experiments compare --out build/suite/full3 --base exp-ref --variant exp-E10-branch
    python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref [--set KEY=VALUE ...] --subsets "2,5,10;1,2,5,10"
    python -m kz4ap_proto.experiments follow [--set KEY=VALUE ...] [--seeds 10]
    python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench PATH --spec build/suite/full3/experiments/E4.json
    python -m kz4ap_proto.experiments calibrate-x-on [--set KEY=VALUE ...] [--events 20]
Start run, batch and calibrate-x-on in the background; each writes experiments/summary-<name>.md when done.
"""

from __future__ import annotations

import argparse
import json
import math
import time
from pathlib import Path

import numpy as np

from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.suites import BOOTSTRAP_RESAMPLES, _interval, _rng_for, _with_interval, aggregate, load_results

from . import metrics, runner
from .bank import boxcar, branch_lengths_s, branch_samples, realized_lengths_s
from .channel import ChannelDecoder
from .params import ProtoConfig
from .power import disable_power_throttling
from .testsignals import lowpass, stream  # shared with the tests; experiments.lowpass / .stream

# Seed 1 of the oracle recordings, without group B's per-row recordings (60% of the channel-seconds); B's
# mixed-style recording stays in.
DEV = (r"^(A-awgn-.*|B-fading-mix|C-fists-.*|D-speed|E-qrm|F-offset|F-drift|G-ragchew|H-qso-oracle|"
       r"I-farnsworth-.*)-s1(\.stations)?$")
# E5's finest grids cost about 15x a run: they use this subset (about 20% of DEV), base included.
DEV_E5 = r"^(A-awgn-25wpm-.*|C-fists-.*|D-speed|I-farnsworth-.*)-s1$"
SUBSETS = {"dev": DEV, "e5": DEV_E5}
EXPERIMENT_RESULTS = Path("experiments") / "results"


def run(out_dir, bench, name: str, values: dict, keep_p1: bool = False, jobs=None, only: str = DEV) -> Path:
    """Decodes and scores the subset, then writes experiments/summary-<name>.md: the settings, the signals
    scored, the pooled CER, the wall time and the decoding CPU (the only file the agent reads afterwards)."""
    out_dir = Path(out_dir)
    started = time.monotonic()
    runner.decode(out_dir, name, values, only, jobs, keep_p1)
    runner.score(out_dir, bench, name, only, results_root=out_dir / EXPERIMENT_RESULTS)
    rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [name])
    scored = [r for r in rows if r["scored"]]
    edits, symbols = sum(r["edits"] for r in scored), sum(r["symbols"] for r in scored)
    path = out_dir / "experiments" / f"summary-{name}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"# {name}\n\nSettings: {json.dumps(values)}; subset: `{only}`.\n\n"
                    f"Signals scored: {len(scored)}; pooled CER {edits / symbols if symbols else float('nan'):.4f}.\n"
                    f"Wall time {(time.monotonic() - started) / 60.0:.1f} min; decoding CPU "
                    f"{1000 * metrics.cpu_per_channel_second(out_dir, name, only):.1f} ms per channel-second.\n",
                    encoding="utf-8")
    return path


def batch(out_dir, bench, spec_path, jobs=None) -> Path:
    """Runs the variants a JSON file lists, one after another, and compares each with the base:
    {"name": NAME, "base": BASE, "subset": "dev" | "e5", "runs": [{"name": ..., "set": {...}}, ...],
    "follow": [{"set": {...}}, ...]} ("follow": optional speed-step follow tests, 10 seeds each). Writes
    experiments/summary-<NAME>.md listing each run's pooled CER, its paired CER against the base and the files."""
    out_dir = Path(out_dir)
    spec = json.loads(Path(spec_path).read_text())
    only = SUBSETS[spec.get("subset", "dev")]
    lines = [f"# Batch {spec['name']} (base {spec['base']}, subset {spec.get('subset', 'dev')})", ""]
    for item in spec["runs"]:
        summary = run(out_dir, bench, item["name"], item.get("set", {}), jobs=jobs, only=only)
        report = compare(out_dir, spec["base"], item["name"], only) if item["name"] != spec["base"] else None
        rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [spec["base"], item["name"]])
        pooled = pooled_paired(rows, spec["base"], item["name"]).get("all", {})
        lines.append(f"- {item['name']} {json.dumps(item.get('set', {}))}: paired CER against the base "
                     f"{_with_interval(pooled.get('mean'), pooled.get('interval'), '+.4f')}; {summary.name}"
                     + (f"; {report.name}" if report else ""))
    for item in spec.get("follow", []):
        counts = follow_marks(ProtoConfig().with_values(**item.get("set", {})), range(1, 11))
        found = [c for c in counts if c is not None]
        lines.append(f"- follow {json.dumps(item.get('set', {}))}: marks per seed {counts}; median "
                     f"{np.median(found) if found else None}; never followed in {counts.count(None)} of 10")
    path = out_dir / "experiments" / f"summary-{spec['name']}.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def calibrate_x_on(cfg, rate_hz: float = 1500.0, events: int = 20, seed: int = 1, chunk_s: float = 2000.0) -> list:
    """Per branch, the x_on at which channel-shaped noise alone keys the unknown-amplitude test down
    cfg.false_marks_per_s times per second (review I4: the nominal formula keys 7-10x more). Each excursion of x
    above x_off gives exactly one key-down if its maximum exceeds x_on (release needs x < x_off, which ends the
    excursion), so x_on is the (R_fa x duration)-th largest excursion maximum: exact for the simulated noise. The
    noise lasts events / R_fa seconds (about `events` key-downs); sigma_v,k^2 is exact (derived from the filters)."""
    lengths = branch_samples(branch_lengths_s(cfg), rate_hz)
    h = lowpass(rate_hz)
    x_off = math.sqrt(-2.0 * math.log(cfg.release_probability))
    total_s = events / cfg.false_marks_per_s
    skip = len(h) + int(lengths[-1])
    rng = np.random.default_rng(seed)
    maxima: list = [[] for _ in lengths]
    used_s = 0.0
    while used_s < total_s:
        n = int(min(chunk_s, total_s - used_s + 1.0) * rate_hz) + skip
        u = np.convolve((rng.standard_normal(n) + 1j * rng.standard_normal(n)) / math.sqrt(2.0), h, mode="same")
        for k, nk in enumerate(lengths):
            sigma2 = 0.5 * float(np.sum(np.convolve(h, np.ones(int(nk)) / nk) ** 2))
            x = np.abs(boxcar(u, int(nk)))[skip:] / math.sqrt(sigma2)
            ids = np.cumsum(x < x_off)
            m = np.full(int(ids[-1]) + 1, -np.inf)
            np.maximum.at(m, ids, x)
            maxima[k].append(m)
        used_s += (n - skip) / rate_hz
    out = []
    for m in maxima:
        m = np.sort(np.concatenate(m))[::-1]
        rank = max(1, int(round(cfg.false_marks_per_s * used_s)))
        out.append(float(m[min(rank, len(m) - 1)]))
    return out


def pooled_paired(rows, a: str, b: str, edits: str = "edits", symbols: str = "symbols") -> dict:
    """b minus a, signal by signal (edits / symbols of each signal), pooled by group and over all ("all"):
    mean and bootstrap 95% interval over signals."""
    by: dict = {}
    for r in rows:
        if r["scored"] and r[symbols] > 0:
            by.setdefault(r["front_end"], {})[(r["group"], r["recording"], r["index"])] = r[edits] / r[symbols]
    base, var = by.get(a, {}), by.get(b, {})
    diffs: dict = {}
    for key in sorted(set(base) & set(var)):
        d = var[key] - base[key]
        diffs.setdefault(key[0], []).append(d)
        diffs.setdefault("all", []).append(d)
    out = {}
    for group, d in diffs.items():
        v = np.array(d)
        picks = _rng_for((group, a, b, edits)).integers(len(v), size=(BOOTSTRAP_RESAMPLES, len(v)))
        out[group] = {"signals": len(v), "mean": float(v.mean()),
                      "interval": _interval([float(v[p].mean()) for p in picks]) if len(v) >= 2 else None}
    return out


def _table(title, stats, columns):
    lines = [f"### {title}", "", "| group | " + " | ".join(c for c, _ in columns) + " |",
             "|---|" + "---|" * len(columns)]
    for group, v in sorted(stats.items()):
        lines.append(f"| {group} | " + " | ".join(fmt(v) for _, fmt in columns) + " |")
    return lines + [""]


def compare(out_dir, base: str, variant: str, only: str = DEV) -> Path:
    """Writes experiments/compare-<variant>-vs-<base>.md: pooled and paired CER and first-word CER, and each
    run's speed errors, switches, over starts, false characters and CPU, all over the subset `only` (a batch passes
    its own, so a base decoded on all of DEV is not summarized over groups the variant never decoded)."""
    out_dir = Path(out_dir)
    rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [base, variant])
    agg = aggregate(rows)
    cer = pooled_paired(rows, base, variant)
    fw = pooled_paired(rows, base, variant, "first_word_edits", "first_word_symbols")
    lines = [f"# {variant} against {base} (subset `{only}`)", "",
             "Paired: variant minus base, signal by signal; bootstrap 95% intervals over signals.", "",
             "| group | signals | paired CER | paired first-word CER |", "|---|---|---|---|"]
    for group in sorted(cer, key=lambda g: (g != "all", g)):
        f = fw.get(group, {})
        lines.append(f"| {group} | {cer[group]['signals']} | {_with_interval(cer[group]['mean'], cer[group]['interval'], '+.4f')} | "
                     f"{_with_interval(f.get('mean'), f.get('interval'), '+.4f')} |")
    lines += ["", "| front end | group | tag | CER | first-word CER |", "|---|---|---|---|---|"]
    for (fe, group, tag), v in sorted(agg.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
        lines.append(f"| {fe} | {group} | {tag} | {_with_interval(v['cer'], v['cer_interval'], '.4f')} | "
                     f"{v['first_word_cer']:.4f} |")
    lines.append("")
    for name in (base, variant):
        lines += [f"## {name}", ""]
        lines += _table("Selected speed off by more than x1.5 (S500 >= 6 dB)", metrics.speed_errors(out_dir, name, only),
                        [("fraction", lambda v: _with_interval(v["error_fraction"], v["interval"], ".4f")),
                         ("lock-ins / transmissions", lambda v: f"{v['lock_ins']} / {v['transmissions']}")])
        lines += _table("Switching", metrics.switch_stats(out_dir, name, only),
                        [("switches per min", lambda v: _with_interval(v["switches_per_min"], None, ".3f")),
                         ("alternations per min", lambda v: _with_interval(v["alternations_per_min"], None, ".3f"))])
        lines += _table("Over starts inside transmissions", metrics.spurious_over_starts(out_dir, name, only),
                        [("per transmission", lambda v: _with_interval(v["per_transmission"], None, ".4f"))])
        lines += _table("False characters outside transmissions", metrics.false_characters(out_dir, name, only),
                        [("per min", lambda v: _with_interval(v["per_min"], None, ".4f"))])
        lines += [f"Decoding CPU: {1000 * metrics.cpu_per_channel_second(out_dir, name, only):.2f} ms per channel-second.", ""]
    path = out_dir / "experiments" / f"compare-{variant}-vs-{base}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def periodicity_table(out_dir, name: str, values: dict, subsets, target: float = 0.95, jobs=None) -> Path:
    """Offline evaluation of values' periodicity estimator on name's stored posteriors: for each window subset,
    the calibrated threshold (lowest reaching `target` precision) with precision, coverage and median time to
    the first confident estimate, and the same at the configured threshold."""
    out_dir = Path(out_dir)
    cfg = ProtoConfig().with_values(**values)
    windows = sorted(cfg.periodicity_windows_s)
    points = metrics.periodicity_points(out_dir, name, cfg, only=DEV, jobs=jobs)
    configured = {"comb": cfg.comb_confidence_min, "edge": cfg.edge_confidence_min,
                  "spectrum": cfg.spectrum_confidence_min}[cfg.periodicity_method]
    tag = "-".join(f"{k}={v}" for k, v in sorted(values.items())).replace(" ", "")
    lines = [f"# Periodicity, offline, on {name}'s posteriors ({tag})", "",
             f"{len(points)} update points inside transmissions (groups {', '.join(metrics.PERIODICITY_GROUPS)}; "
             "S500 >= 0 dB; constant-speed labels). Correct: within 5% of 1.2 s / WPM. Intervals: bootstrap 95% over "
             "channels.", "",
             "| windows (s) | threshold | precision | coverage | median time to confident (s) |", "|---|---|---|---|---|"]
    for subset in subsets:
        threshold, r = metrics.calibrate(points, windows, subset, target)
        for label, th, res in (("calibrated", threshold, r),
                               ("configured", configured, metrics.evaluate_rule(points, windows, subset, configured))):
            if res is None:
                lines.append(f"| {subset} | {label}: none reaches {target} | — | — | — |")
                continue
            lines.append(f"| {subset} | {label} {th:.4g} | {_with_interval(res['precision'], res['precision_interval'], '.3f')} | "
                         f"{_with_interval(res['coverage'], res['coverage_interval'], '.3f')} | "
                         f"{_with_interval(res['median_time_to_confident_s'], None, '.2f')} |")
    path = out_dir / "experiments" / f"periodicity-{name}-{tag}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def follow_marks(cfg, seeds) -> list:
    """Spec 4.6's speed jump: 15 -> 30 WPM at the fifth word, S500 = 20 dB. For each seed, the marks sent from
    the step until a branch matched to 30 WPM (within the eligibility tolerance of 0.8 x 40 ms) is selected;
    None if never."""
    text = "CQ CQ CQ DE K1ABC K1ABC K1ABC K1ABC"
    iv = timed_intervals(text, 15.0, wpm_end=30.0, profile="step")
    start = 1.0
    step_s = start + iv[28][0]  # CQ CQ CQ DE has 28 marks
    lengths = realized_lengths_s(cfg, 1500.0)
    counts = []
    for seed in seeds:
        r = ChannelDecoder(cfg, 1500.0).run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, seed))
        followed = next((t for t, k, _ in r.selections if t > step_s and abs(
            math.log(lengths[k] / (cfg.length_dits * 1.2 / 30.0))) <= cfg.eligibility_tolerance), None)
        counts.append(None if followed is None else sum(1 for a, _ in iv if step_s <= start + a <= followed))
    return counts


def main(argv=None) -> None:
    disable_power_throttling()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    r = sub.add_parser("run")
    c = sub.add_parser("compare")
    p = sub.add_parser("periodicity")
    f = sub.add_parser("follow")
    b = sub.add_parser("batch")
    x = sub.add_parser("calibrate-x-on")
    for s in (r, c, p, b):
        s.add_argument("--out", type=Path, required=True)
    for s in (r, p, f, x):
        s.add_argument("--set", dest="values", action="append", help="KEY=VALUE: a ProtoConfig value")
    for s in (r, p, b):
        s.add_argument("--jobs", type=int, default=None)
    for s in (r, b):
        s.add_argument("--bench", type=Path, required=True)
    r.add_argument("--name", required=True)
    r.add_argument("--keep-p1", action="store_true")
    r.add_argument("--subset", choices=sorted(SUBSETS), default="dev")
    b.add_argument("--spec", type=Path, required=True)
    x.add_argument("--events", type=int, default=20)
    c.add_argument("--base", required=True)
    c.add_argument("--variant", required=True)
    c.add_argument("--subset", choices=sorted(SUBSETS), default="dev")
    p.add_argument("--name", required=True)
    p.add_argument("--subsets", required=True, help='window subsets, s: "2,5,10;1,2,5,10"')
    p.add_argument("--target", type=float, default=0.95)
    f.add_argument("--seeds", type=int, default=10)
    args = parser.parse_args(argv)
    if args.command == "run":
        print(f"wrote {run(args.out, args.bench, args.name, runner.parse_values(args.values), args.keep_p1, args.jobs, SUBSETS[args.subset])}")
    elif args.command == "batch":
        print(f"wrote {batch(args.out, args.bench, args.spec, args.jobs)}")
    elif args.command == "calibrate-x-on":
        cfg = ProtoConfig().with_values(**runner.parse_values(args.values))
        print(json.dumps([round(v, 4) for v in calibrate_x_on(cfg, events=args.events)]))
    elif args.command == "compare":
        print(f"wrote {compare(args.out, args.base, args.variant, SUBSETS[args.subset])}")
    elif args.command == "periodicity":
        subsets = [tuple(float(x) for x in s.split(",")) for s in args.subsets.split(";")]
        print(f"wrote {periodicity_table(args.out, args.name, runner.parse_values(args.values), subsets, args.target, args.jobs)}")
    else:
        counts = follow_marks(ProtoConfig().with_values(**runner.parse_values(args.values)), range(1, args.seeds + 1))
        found = [c for c in counts if c is not None]
        print(f"marks to follow per seed: {counts}; median {np.median(found) if found else None}, "
              f"max {max(found) if found else None}, never followed in {counts.count(None)} of {len(counts)}")


if __name__ == "__main__":
    main()
