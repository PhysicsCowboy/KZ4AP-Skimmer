"""Experiments on scored results and decoded files (decoded files of oracle test cases are written by
kz4ap-bank-replay): two runs compared signal by signal, the periodicity estimate's own records evaluated, the stretch
test, the new-over checks, and the jittered development set's analysis, score tables and figures. The stage-1
experiments that ran the Python prototype of the bank decoder (run, batch, periodicity, follow, calibrate-x-on) were
removed with the prototype on 2026-10-07 (git history; engine/tests/data/bank/README.md).

    python -m kz4ap_proto.experiments compare --out build/suite/full3 --base bank-b3b --variant bank-b4g [--subset dev]
    python -m kz4ap_proto.experiments periodicity-decoded --out build/suite/full3 --name bank-b4a [--threshold 0.03]
    python -m kz4ap_proto.experiments stretch --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments new-overs --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments devset2 --out build/suite/dev2 --name NAME --decoder matched --decoder bank-x [--reference matched] [--results DIR ...] [--scores TABLE.csv ...] [--method wls|binomial] [--fit-group "A2 sensitivity" ...] [--resamples 1000]
    python -m kz4ap_proto.experiments scores --analysis build/suite/dev2/experiments/devset2-NAME.json --decoder matched [--path oracle|detector] --csv TABLE.csv
    python -m kz4ap_proto.experiments figures --analysis build/suite/dev2/experiments/devset2-NAME.json [--dir DIR]
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import (BOOTSTRAP_RESAMPLES, STRETCH_SNR_DB, _interval, _rng_for, _with_interval, aggregate,
                                load_results, stretch_source, view_fits)

from . import metrics
from .power import disable_power_throttling

# Seed 1 of the oracle recordings, without group B's per-row recordings (60% of the channel-seconds); B's
# mixed-style recording stays in.
DEV = (r"^(A-awgn-.*|B-fading-mix|C-fists-.*|D-speed|E-qrm|F-offset|F-drift|G-ragchew|H-qso-oracle|"
       r"I-farnsworth-.*)-s1(\.stations)?$")
SUBSETS = {"dev": DEV}
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
    run's speed errors, switches, over starts, false characters and CPU, all over the subset `only` (so a base decoded
    on more test cases is not summarized over groups the variant never decoded)."""
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


def main(argv=None) -> None:
    disable_power_throttling()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    c = sub.add_parser("compare")
    pd = sub.add_parser("periodicity-decoded")
    st = sub.add_parser("stretch")
    no = sub.add_parser("new-overs")
    d2 = sub.add_parser("devset2", help="the jittered development set's analysis (kz4ap_proto.devset2)")
    fg = sub.add_parser("figures", help="the five figures from a devset2 analysis file (needs matplotlib)")
    sc = sub.add_parser("scores", help="a run's per-signal score table (CSV, rounded) from a devset2 analysis or a suite")
    for s in (c, pd, st, no, d2):
        s.add_argument("--out", type=Path, required=True)
    c.add_argument("--base", required=True)
    c.add_argument("--variant", required=True)
    c.add_argument("--subset", choices=sorted(SUBSETS), default="dev")
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
    d2.add_argument("--scores", dest="scores", action="append", type=Path, default=None,
                    help="a score table (experiments scores; repeatable): the rows come from the tables alone, and "
                         "OUT is only where experiments/ is written")
    d2.add_argument("--method", choices=("wls", "binomial"), default="wls")
    d2.add_argument("--resamples", type=int, default=BOOTSTRAP_RESAMPLES)
    fg.add_argument("--analysis", type=Path, required=True)
    sc_from = sc.add_mutually_exclusive_group(required=True)
    sc_from.add_argument("--analysis", type=Path, help="a devset2 analysis JSON: its signals")
    sc_from.add_argument("--out", type=Path, help="a suite folder: its scored files (as devset2 loads them)")
    sc.add_argument("--decoder", required=True, help="the run: a decoder's results folder name")
    sc.add_argument("--path", choices=("oracle", "detector"), default=None, help="only this path's signals")
    sc.add_argument("--results", dest="results_dirs", action="append", type=Path, default=None,
                    help="with --out: a results root (repeatable)")
    sc.add_argument("--only", default=None, help="with --out: regular expression on result names (default as devset2)")
    sc.add_argument("--csv", type=Path, required=True, help="the table to write")
    fg.add_argument("--dir", type=Path, default=None, help="output folder (default: figures-NAME beside the analysis)")
    args = parser.parse_args(argv)
    if args.command == "devset2":
        from . import devset2
        if args.reference is not None and args.reference not in args.decoders:
            parser.error("--reference must be one of the --decoder names")
        path = devset2.write_analysis(args.out, args.decoders, args.name, args.reference, args.results_dirs,
                                      args.only or devset2.DEV2_ANALYSIS, tuple(args.fit_groups or devset2.FIT_GROUPS),
                                      args.method, args.resamples, args.scores)
        print(f"wrote {path} and {path.with_suffix('.md')}")
    elif args.command == "scores":
        from . import devset2
        if args.analysis is not None:
            rows = json.loads(args.analysis.read_text(encoding="utf-8"))["signals"]
        else:
            rows = devset2.load_rows(args.out, [args.decoder], args.results_dirs, args.only or devset2.DEV2_ANALYSIS)
        rows = [r for r in rows if r["decoder"] == args.decoder and (args.path is None or r["path"] == args.path)]
        if not rows:
            parser.error(f"no signals of {args.decoder}" + (f" on the {args.path} path" if args.path else ""))
        print(f"wrote {devset2.write_scores(rows, args.csv)} ({len(rows)} signals)")
    elif args.command == "figures":
        from . import figures
        for written in figures.draw_all(args.analysis, args.dir):
            print(f"wrote {written}")
    elif args.command == "compare":
        print(f"wrote {compare(args.out, args.base, args.variant, SUBSETS[args.subset])}")
    elif args.command == "stretch":
        print(f"wrote {stretch_table(args.out, args.name)}")
    elif args.command == "new-overs":
        print(f"wrote {new_over_table(args.out, args.name)}")
    elif args.command == "periodicity-decoded":
        print(f"wrote {periodicity_decoded_table(args.out, args.name, args.threshold, args.target)}")


if __name__ == "__main__":
    main()
