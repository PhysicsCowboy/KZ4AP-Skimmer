"""Stage 1 experiments (plan Tasks 13-14): the prototype on the development set with one parameter group
changed, scored like the suite and compared signal by signal with a reference run; the periodicity estimator
evaluated offline on stored branch-1 posteriors; and the synthetic speed-step follow test.

    python -m kz4ap_proto.experiments run --out build/suite/full3 --bench PATH --name exp-ref [--set KEY=VALUE ...] [--keep-p1] [--jobs N]
    python -m kz4ap_proto.experiments compare --out build/suite/full3 --base exp-ref --variant exp-E10-branch
    python -m kz4ap_proto.experiments periodicity --out build/suite/full3 --name exp-ref [--set KEY=VALUE ...] --subsets "2,5,10;1,2,5,10"
    python -m kz4ap_proto.experiments periodicity-decoded --out build/suite/full3 --name bank-b4a [--threshold 0.03]
    python -m kz4ap_proto.experiments follow [--set KEY=VALUE ...] [--seeds 10]
    python -m kz4ap_proto.experiments batch --out build/suite/full3 --bench PATH --spec build/suite/full3/experiments/E4.json
    python -m kz4ap_proto.experiments calibrate-x-on [--set KEY=VALUE ...] [--events 20]
    python -m kz4ap_proto.experiments stretch --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments new-overs --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments devset2 --out build/suite/dev2 --name NAME --decoder matched --decoder bank-x [--reference matched] [--results DIR ...] [--method wls|binomial] [--fit-group "A2 sensitivity" ...] [--resamples 1000]
    python -m kz4ap_proto.experiments figures --analysis build/suite/dev2/experiments/devset2-NAME.json [--dir DIR]
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
from kz4ap_synth.suites import (BOOTSTRAP_RESAMPLES, STRETCH_SNR_DB, _interval, _rng_for, _with_interval, aggregate,
                                load_results, stretch_source, view_fits)

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
# The jittered development set (docs/plans/2026-10-06-development-set-redesign.md; suites.dev2_suite): seed 1 of its
# oracle recordings (H2's one labels file holds its per-station labels; there are no .stations scorings); A2's
# detector-path copies (A2-detector-...) are not in it.
DEV2 = (r"^(A2-awgn|B2-fading|C2-fists|D2-speed|E2-qrm|F2-offset|F2-drift|G2-qso|H2-qso|I2-farnsworth|S2-stretch)"
        r"-.*-s1$")
SUBSETS["dev2"] = DEV2
# Plan B, B9 (not in DEV, which stays as it is for comparability): the stretch test's test cases, group A's 25 WPM
# recordings and their stretched copies (seed 1; seeds 2-3 are held out until B12), and the new-over checks' test
# cases, group H's oracle QSO labels and the pauses group's oracle copy.
STRETCH = r"^(A-awgn-25wpm-.*|S-stretch-.*)-s1$"
NEW_OVERS = r"^(H-qso-oracle-s1|pauses-s1\.oracle)$"
B9 = r"^(A-awgn-25wpm-.*-s1|S-stretch-.*-s1|H-qso-oracle-s1|pauses-s1\.oracle)$"
SUBSETS.update(stretch=STRETCH, b9=B9)
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
    lines += ["", "| decoder | group | tag | CER | first-word CER |", "|---|---|---|---|---|"]
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


def periodicity_decoded_table(out_dir, name: str, threshold: float = 0.03, target: float = 0.95) -> Path:
    """Plan B, B4a: the periodicity rule evaluated on the decoder's own records in name's decoded files (whatever its
    windows: the C++ bank's fixed windows in seconds or a window per candidate in dits). Windows are numbered
    shortest first (0, 1, 2). For the rule over all windows and for each window alone: precision, coverage and
    median time to the first confident estimate at `threshold`, and the lowest of 100 quantiles of the scores at
    which precision reaches `target` (scores as recorded, rounded to 4 decimals)."""
    out_dir = Path(out_dir)
    points = metrics.periodicity_points_decoded(out_dir, name, only=DEV)
    n = max((len(p["per"]) for p in points), default=0)
    windows = tuple(range(n))
    lines = [f"# Periodicity, from {name}'s decoded records", "",
             f"{len(points)} update points inside transmissions (groups {', '.join(metrics.PERIODICITY_GROUPS)}; "
             "S500 >= 0 dB; constant-speed labels; the decoder's own recomputations). Correct: within 5% of 1.2 s / "
             "WPM. Intervals: bootstrap 95% over channels. Windows numbered shortest first.", "",
             "| windows | threshold | precision | coverage | median time to confident (s) |", "|---|---|---|---|---|"]
    for subset in [windows] + [(w,) for w in windows]:
        calibrated, r = metrics.calibrate(points, windows, subset, target)
        for label, th, res in (("configured", threshold, metrics.evaluate_rule(points, windows, subset, threshold)),
                               ("calibrated", calibrated, r)):
            if res is None:
                lines.append(f"| {subset} | {label}: none reaches {target} | — | — | — |")
                continue
            lines.append(f"| {subset} | {label} {th:.4g} | {_with_interval(res['precision'], res['precision_interval'], '.3f')} | "
                         f"{_with_interval(res['coverage'], res['coverage_interval'], '.3f')} | "
                         f"{_with_interval(res['median_time_to_confident_s'], None, '.2f')} |")
    lines += ["", f"### By speed (true dit's WPM bin), at threshold {threshold:g}", "",
              "Per window (shortest first) and for the rule over all windows: points, fraction confident, precision of "
              "the confident estimates. Pooled counts, no intervals.", "",
              "| WPM bin | window | points | confident | precision |", "|---|---|---|---|---|"]
    for r in metrics.periodicity_by_speed(points, threshold):
        lo, hi = r["bin"]
        fmt = lambda x: "—" if x is None else f"{x:.3f}"
        lines.append(f"| {lo:g}-{min(hi, 100.0):g} | {r['window']} | {r['points']} | {fmt(r['confident'])} | "
                     f"{fmt(r['precision'])} |")
    path = out_dir / "experiments" / f"periodicity-decoded-{name}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def stretch_pairs(out_dir, name: str) -> list[dict]:
    """The stretch test's pairs from name's scored results (experiments/results/<name>): every scored signal of a
    stretched recording with the same signal of the group-A recording it copies (suites.stretch_source), if both
    were scored. Each pair: {"recording", "index", "snr_db" (the original's), "cer": {"orig", "stretched"},
    "first_word": {...}}, each side (edits, symbols). Raises ValueError if a pair's texts differ or its S500 values
    are not STRETCH_SNR_DB apart."""
    out_dir = Path(out_dir)
    rows, _ = load_results(out_dir, [out_dir / EXPERIMENT_RESULTS], [name])
    by = {(r["recording"], r["index"]): r for r in rows if r["scored"]}
    manifest = {r["name"]: r for r in json.loads((out_dir / "manifest.json").read_text())["recordings"]}
    labels: dict = {}

    def label(rec, i):
        if rec not in labels:
            labels[rec] = json.loads((out_dir / manifest[rec]["labels"]).read_text())["signals"]
        return labels[rec][i]

    pairs = []
    for (rec, i), r in sorted(by.items()):
        src = stretch_source(rec)
        if src is None or (src, i) not in by:
            continue
        o, a, b = by[(src, i)], label(src, i), label(rec, i)
        if a["text"] != b["text"] or abs((a["snr_db"] - b["snr_db"]) - STRETCH_SNR_DB) > 1e-9:
            raise ValueError(f"{rec} signal {i} is not a stretched copy of {src}'s")
        pairs.append({"recording": rec, "index": i, "snr_db": o["snr_db"],
                      "cer": {"orig": (o["edits"], o["symbols"]), "stretched": (r["edits"], r["symbols"])},
                      "first_word": {"orig": (o["first_word_edits"], o["first_word_symbols"]),
                                     "stretched": (r["first_word_edits"], r["first_word_symbols"])}})
    return pairs


def _stretch_lines(title: str, m: dict) -> list[str]:
    def crossing(value, bound):
        return "—" if value is None else (f"≤ {value:.2f}" if bound else f"{value:.2f}")

    lines = [f"### {title}", "",
             "| S₅₀₀ of the original (dB SNR in 500 Hz) | pairs | original | stretched | paired (stretched − original) |",
             "|---|---|---|---|---|"]
    for snr, v in m["by_snr"].items():
        lines.append(f"| {snr:+g} | {v['pairs']} | {v['orig']:.4f} | {v['stretched']:.4f} | "
                     f"{_with_interval(v['mean'], v['interval'], '+.4f')} |")
    if m["all"]:
        v = m["all"]
        lines.append(f"| all | {v['pairs']} | {v['orig']:.4f} | {v['stretched']:.4f} | "
                     f"**{_with_interval(v['mean'], v['interval'], '+.4f')}** |")
    excess = None if m["shift_db"] is None else f"{m['shift_db'] - m['snr_step_db']:+.2f}"
    lines += ["", f"S₅₀₀ at which the CER crosses {m['threshold']:g} (dB SNR in 500 Hz; linear interpolation; "
              "≤: no step fails, the lowest step is an upper bound): original "
              f"{crossing(m['crossing_orig_db'], m['crossing_orig_upper_bound'])}, stretched "
              f"{crossing(m['crossing_stretched_db'], m['crossing_stretched_upper_bound'])} (on its own S₅₀₀ axis); "
              f"shift original − stretched **{_with_interval(m['shift_db'], m['shift_interval'], '+.2f')} dB** of "
              f"S₅₀₀ (time-base invariance: {m['snr_step_db']:+.2f} dB; the shift minus that: "
              f"{excess or '—'} dB).", ""]
    return lines


def stretch_table(out_dir, name: str, threshold: float = 0.10) -> Path:
    """Plan B, B9: the stretch test (stage-2 spec section 4.1) on name's scored results. Writes
    experiments/stretch-<name>.md and .json: per S500 step and pooled, the paired CER and first-word CER (stretched
    − original), and the CER-threshold crossings and their shift (metrics.stretch_measures)."""
    out_dir = Path(out_dir)
    pairs = stretch_pairs(out_dir, name)
    if not pairs:
        raise ValueError(f"{name}: no stretched signal was scored together with its original")
    result = {kind: metrics.stretch_measures([{"snr_db": p["snr_db"], **p[kind]} for p in pairs], STRETCH_SNR_DB,
                                             threshold, key=("stretch", name, kind))
              for kind in ("cer", "first_word")}
    lines = [f"# Stretch test, {name}", "",
             f"{len(pairs)} pairs: each signal of group A's 25 WPM recordings and its stretched copy (the same text, "
             f"its keying timeline 25/12 = 2.0833 times longer, 12 WPM, at S₅₀₀ {STRETCH_SNR_DB:.4f} dB lower: the "
             "same energy per dit relative to the noise density). Paired: the mean over pairs of the difference of "
             "the two signals' CER; parentheses: bootstrap 95% interval over pairs (for the shift, pairs resampled "
             "within each step). Columns original and stretched: CER pooled over the step's signals.", ""]
    lines += _stretch_lines("CER", result["cer"]) + _stretch_lines("First-word CER", result["first_word"])
    path = out_dir / "experiments" / f"stretch-{name}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    js = {k: {**v, "by_snr": [{"snr_db": s, **x} for s, x in v["by_snr"].items()]} for k, v in result.items()}
    path.with_suffix(".json").write_text(json.dumps({"name": name, "pairs": len(pairs), **js}, indent=2) + "\n")
    return path


def new_over_table(out_dir, name: str, only: str = NEW_OVERS, within_s: float = 2.0, settle_s: float = 0.1) -> Path:
    """Plan B, B9: the new-over checks (stage-2 spec section 4.3) on name's decoded files and scored results. For
    each group (and per tag) of the scored labels in `only` whose view fits (group H's oracle QSO labels only where
    the channel holds the answering station, suites.view_fits): false new overs per transmission and missed
    turnovers per turnover (metrics.new_over_counts), over starts in same-station silences per silence, and the
    first-word CER of the overs by the silence before them (first, after a turnover, after the same station);
    bootstrap 95% intervals over channels (signals). Writes experiments/new-overs-<name>.md."""
    out_dir = Path(out_dir)
    counts: dict = {}
    words: dict = {}
    results = out_dir / EXPERIMENT_RESULTS / name
    scored: dict = {}
    for job, label, ch, _ in metrics.iter_channels(out_dir, name, only):
        if not label.get("score", True) or not view_fits(job["group"], label.get("tag", "")):
            continue
        c = metrics.new_over_counts(label, ch["over_starts"], within_s, settle_s)
        for key in ((job["group"], "all"), (job["group"], label.get("tag", ""))):
            counts.setdefault(key, []).append(c)
        if job["result"] not in scored:
            path = results / f"{job['result']}.json"
            scored[job["result"]] = ({s["index"]: s for s in json.loads(path.read_text())["score"]["signals"]}
                                     if path.exists() else {})
        sig = scored[job["result"]].get(ch["label_index"])
        if sig is None or not sig["scored"]:
            continue
        for o in metrics.first_word_by_over(label, sig):
            unit = words.setdefault((job["group"], o["kind"]), {}).setdefault((job["result"], ch["label_index"]),
                                                                              [0, 0, 0])
            unit[0] += o["edits"]
            unit[1] += o["symbols"]
            unit[2] += 1  # overs

    def rate(units, key):
        num, den = sum(u[0] for u in units), sum(u[1] for u in units)
        value = _with_interval(num / den if den else None, metrics.bootstrap_ratio(units, key), ".4f")
        return f"{value} ({num} / {den})"

    lines = [f"# New-over checks, {name}", "",
             f"Test cases `{only}`; group H's oracle QSO labels only where the channel holds the answering station "
             "(offset below 150 Hz). False new over: an over start later than "
             f"{settle_s:g} s after a transmission's first key-down and before its last key-up, other than one that found a "
             "turnover late. Turnover: a transmission by the other station; found late: the first over start after the "
             f"previous transmission's last key-up comes later than {settle_s:g} s and no later than {within_s:g} s "
             "after its first key-down (delays from that key-down, s: median and maximum); missed: no over start in "
             f"that span up to {within_s:g} s. Same-station silence: a transmission after one "
             "by the same station (the pauses group's repeats). Parentheses: bootstrap 95% interval over channels, "
             "then the counts.", "",
             "| group | tag | channels | false new overs per transmission | missed turnovers per turnover | "
             "turnovers found late per turnover | late delay median, maximum (s) | "
             "over starts per same-station silence |", "|---|---|---|---|---|---|---|---|"]
    for (group, tag), cs in sorted(counts.items(), key=lambda kv: (kv[0][0], kv[0][1] != "all", kv[0][1])):
        false = rate([(c["false_new_overs"], c["transmissions"]) for c in cs], ("false", name, group, tag))
        missed = (rate([(c["missed_turnovers"], c["turnovers"]) for c in cs], ("missed", name, group, tag))
                  if any(c["turnovers"] for c in cs) else "—")
        late = (rate([(c["late_turnovers"], c["turnovers"]) for c in cs], ("late", name, group, tag))
                if any(c["turnovers"] for c in cs) else "—")
        delays = [d for c in cs for d in c["late_delays_s"]]
        delay = f"{np.median(delays):.2f}, {max(delays):.2f}" if delays else "—"
        same = (rate([(c["same_station_new_overs"], c["same_station_gaps"]) for c in cs], ("same", name, group, tag))
                if any(c["same_station_gaps"] for c in cs) else "—")
        lines.append(f"| {group} | {tag} | {len(cs)} | {false} | {missed} | {late} | {delay} | {same} |")
    lines += ["", "First-word CER of the overs, by the silence before them (the bench's per-transmission first-word "
              "counts; an upper bound, as in the suite's summary):", "",
              "| group | over | channels | overs | first-word CER |", "|---|---|---|---|---|"]
    for (group, kind), units in sorted(words.items()):
        u = list(units.values())
        lines.append(f"| {group} | {kind} | {len(u)} | {sum(x[2] for x in u)} | "
                     f"{rate([(x[0], x[1]) for x in u], ('fw', name, group, kind))} |")
    path = out_dir / "experiments" / f"new-overs-{name}.md"
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
    pd = sub.add_parser("periodicity-decoded")
    f = sub.add_parser("follow")
    b = sub.add_parser("batch")
    x = sub.add_parser("calibrate-x-on")
    st = sub.add_parser("stretch")
    no = sub.add_parser("new-overs")
    d2 = sub.add_parser("devset2", help="the jittered development set's analysis (kz4ap_proto.devset2)")
    fg = sub.add_parser("figures", help="the five figures from a devset2 analysis file (needs matplotlib)")
    for s in (r, c, p, b, pd, st, no, d2):
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
    pd.add_argument("--name", required=True)
    pd.add_argument("--threshold", type=float, default=0.03)
    pd.add_argument("--target", type=float, default=0.95)
    for s in (st, no):
        s.add_argument("--name", required=True)
    d2.add_argument("--name", required=True, help="the analysis's name: experiments/devset2-NAME.json and .md")
    d2.add_argument("--decoder", dest="decoders", action="append", required=True,
                    help="a decoder's results folder name (repeatable)")
    d2.add_argument("--reference", default=None, help="the reference of the paired comparisons (one of --decoder)")
    d2.add_argument("--results", dest="results_dirs", action="append", type=Path, default=None,
                    help="a results root (repeatable; default OUT/results and OUT/experiments/results)")
    d2.add_argument("--only", default=None, help="regular expression on result names (default: DEV2 and A2's "
                                                 "detector-path copies, seed 1)")
    d2.add_argument("--fit-group", dest="fit_groups", action="append", default=None,
                    help='a group whose oracle signals the fit uses (repeatable; default "A2 sensitivity")')
    d2.add_argument("--method", choices=("wls", "binomial"), default="wls")
    d2.add_argument("--resamples", type=int, default=BOOTSTRAP_RESAMPLES)
    fg.add_argument("--analysis", type=Path, required=True)
    fg.add_argument("--dir", type=Path, default=None, help="output folder (default: figures-NAME beside the analysis)")
    args = parser.parse_args(argv)
    if args.command == "devset2":
        from . import devset2
        if args.reference is not None and args.reference not in args.decoders:
            parser.error("--reference must be one of the --decoder names")
        path = devset2.write_analysis(args.out, args.decoders, args.name, args.reference, args.results_dirs,
                                      args.only or devset2.DEV2_ANALYSIS, tuple(args.fit_groups or devset2.FIT_GROUPS),
                                      args.method, args.resamples)
        print(f"wrote {path} and {path.with_suffix('.md')}")
    elif args.command == "figures":
        from . import figures
        for written in figures.draw_all(args.analysis, args.dir):
            print(f"wrote {written}")
    elif args.command == "run":
        print(f"wrote {run(args.out, args.bench, args.name, runner.parse_values(args.values), args.keep_p1, args.jobs, SUBSETS[args.subset])}")
    elif args.command == "batch":
        print(f"wrote {batch(args.out, args.bench, args.spec, args.jobs)}")
    elif args.command == "calibrate-x-on":
        cfg = ProtoConfig().with_values(**runner.parse_values(args.values))
        print(json.dumps([round(v, 4) for v in calibrate_x_on(cfg, events=args.events)]))
    elif args.command == "compare":
        print(f"wrote {compare(args.out, args.base, args.variant, SUBSETS[args.subset])}")
    elif args.command == "stretch":
        print(f"wrote {stretch_table(args.out, args.name)}")
    elif args.command == "new-overs":
        print(f"wrote {new_over_table(args.out, args.name)}")
    elif args.command == "periodicity-decoded":
        print(f"wrote {periodicity_decoded_table(args.out, args.name, args.threshold, args.target)}")
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
