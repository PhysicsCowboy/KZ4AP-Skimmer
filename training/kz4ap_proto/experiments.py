"""Experiments on scored results and decoded files (decoded files of oracle test cases are written by
kz4ap-bank-replay): two runs compared signal by signal, the periodicity estimate's own records evaluated, the stretch
test, the new-over checks, the search errors of a run against a wide-beam reference run, and the jittered
development set's analysis, score tables and figures. The stage-1
experiments that ran the Python prototype of the bank decoder (run, batch, periodicity, follow, calibrate-x-on) were
removed with the prototype on 2026-10-07 (git history; engine/tests/data/bank/README.md).

    python -m kz4ap_proto.experiments compare --out build/suite/full3 --base bank-b3b --variant bank-b4g [--subset dev]
    python -m kz4ap_proto.experiments periodicity-decoded --out build/suite/full3 --name bank-b4a [--threshold 0.03]
    python -m kz4ap_proto.experiments stretch --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments new-overs --out build/suite/full3 --name stretch-b4a
    python -m kz4ap_proto.experiments devset2 --out build/suite/dev2 --name NAME --decoder matched --decoder bank-x [--reference matched] [--results DIR ...] [--scores TABLE.csv ...] [--method wls|binomial] [--fit-group "A2 sensitivity" ...] [--resamples 1000] [--max-farnsworth 8] [--leave-out-group "I2 Farnsworth" ...]
    python -m kz4ap_proto.experiments scores --analysis build/suite/dev2/experiments/devset2-NAME.json --decoder matched [--path oracle|detector] --csv TABLE.csv
    python -m kz4ap_proto.experiments figures --analysis build/suite/dev2/experiments/devset2-NAME.json [--dir DIR]
    python -m kz4ap_proto.experiments search-errors --out build/suite/dev2 --run NAME --reference NAME --bench PATH/kz4ap-bench [--only REGEX] [--results DIR ...] [--max-farnsworth 8]
    python -m kz4ap_proto.experiments search-stats --out build/suite/dev2 --run NAME [--only REGEX] [--results DIR ...]
    python -m kz4ap_proto.experiments false-characters --out build/suite/n2 --run NAME [--only REGEX] [--results DIR ...]
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import subprocess
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import (BOOTSTRAP_RESAMPLES, STRETCH_SNR_DB, _interval, _rng_for, _scorings, _with_interval,
                                aggregate, front_end_dirs, load_results, oracle_copies, stretch_source, view_fits)

from . import devset2, metrics, search_stats
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
# The noise-only group N2 (kz4ap_synth.suites.noise2), seed 1: a suite folder of its own (--suite n2), not part of DEV2.
N2 = r"^N2-noise-.*-s1$"
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


# --- search errors (docs/plans/2026-10-20-new-decoder-prototype-1.md, section 6.1, R1-R2) ---

def _bench_edits(bench, labels: dict, reference_texts, run_texts, result: str, run: str, reference: str,
                 work: Path) -> list[dict]:
    """The bench's own edit counts of run_texts against reference_texts, signal by signal (the scorer's alignment,
    kz4ap-bench --score-decoded: no second edit distance is written here). The bench scores decoded text against a
    labels file's text, so the labels file is the test case's with each signal's text replaced by the reference
    run's, and without the transmissions (they split the old text); the decoded file holds the run's texts, matched
    to the labels by order. Both are kept in `work` with the bench's answer, as <result>.reference.json,
    <result>.decoded.json and <result>.scored.json. Returns the answer's per-signal entries: scored, chars (the
    reference text's characters that are not word spaces, a prosign one) and edits (all of them, word spaces too)."""
    work.mkdir(parents=True, exist_ok=True)
    signals = [{k: v for k, v in {**s, "text": t}.items() if k != "transmissions"}
               for s, t in zip(labels["signals"], reference_texts)]
    reference_path, decoded_path, scored_path = (work / f"{result}.{kind}.json" for kind in ("reference", "decoded",
                                                                                              "scored"))
    reference_path.write_text(json.dumps({**labels, "signals": signals}) + "\n", encoding="utf-8")
    decoded_path.write_text(json.dumps({"front_end": run, "recording": result, "texts": list(run_texts)}) + "\n",
                            encoding="utf-8")
    cmd = [str(bench), "--labels", str(reference_path), "--score-decoded", str(decoded_path), "--json",
           str(scored_path)]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"kz4ap-bench failed comparing {run} with {reference} on {result}:\n{done.stderr}")
    return json.loads(scored_path.read_text(encoding="utf-8"))["score"]["signals"]


def _search_stats(records) -> dict:
    """Signals, reference letters, search errors, signals with at least one, and errors per 1 000 letters, of the
    per-signal records (None where there are no letters)."""
    letters, errors = sum(r["letters"] for r in records), sum(r["errors"] for r in records)
    return {"signals": len(records), "letters": letters, "errors": errors,
            "signals_with_errors": sum(1 for r in records if r["errors"] > 0),
            "per_1000_letters": 1000.0 * errors / letters if letters else None}


def _search_stats_by_group(records) -> dict:
    groups: dict = {}
    for r in records:
        groups.setdefault(r["group"], []).append(r)
    return {g: _search_stats(rs) for g, rs in sorted(groups.items())}


def search_errors(out_dir, bench, run: str, reference: str, only: str | None = None, results_dirs=None,
                  max_farnsworth: float | None = None) -> dict:
    """The search errors of the run `run` against the wide-beam reference run `reference` (both results folder names).

    A search error is an edit, in the bench scorer's alignment, between the text `run` decoded for a signal and the
    text `reference` decoded for the same signal: the reference run, with a much wider beam and a higher ceiling, is
    taken as the search's truth, whatever the labels say. Errors are counted per 1 000 letters of the reference's text
    (its characters that are not word spaces, a prosign one: the scorer's `chars`; the plan's "roughly 1 050 000
    letters" for DEV2 is that count, 1 086 215 measured, not the 1 429 627 symbols with word spaces), per group and
    pooled over the groups; every edit counts as an error, a word space's too. Only signals the labels score count, as
    in every CER. The edits come from the bench (_bench_edits), which therefore must be given.

    only: a regular expression on the test cases' result names (default DEV2: seed 1 of the oracle test cases. A2's
    detector-path copies, which devset2.DEV2_ANALYSIS adds, are the same signals and noise decoded again through the
    detector: pooled with the oracle test cases they would count A2's signals twice, and the plan's runs are oracle
    runs, O9); the test cases both runs have are compared, the others are listed. max_farnsworth: the I2 cut
    (devset2.farnsworth_cut): signals above that Farnsworth factor are left out of the groups and the pooled figure,
    for both runs alike, and counted apart under "left_out".

    Returns {"run", "reference", "only", "max_farnsworth", "test_cases": {"compared": n, "only_in_run": [...],
    "only_in_reference": [...]}, "groups": {group: stats}, "pooled": stats, "signals": [per-signal records],
    "left_out": None or {"groups", "pooled", "signals"}}; stats are _search_stats'; a per-signal record is {"group",
    "test_case", "index", "errors", "letters", "farnsworth_factor"}."""
    out_dir = Path(out_dir)
    only = DEV2 if only is None else only
    roots = [Path(d) for d in (results_dirs if results_dirs is not None else devset2.default_results_dirs(out_dir))]
    folders = {}
    for name in (run, reference):
        found = front_end_dirs(out_dir, roots, [name])
        if not found:
            raise ValueError(f"no results folder named {name!r} in {[str(r) for r in roots]}")
        folders[name] = found[0]
    manifest = json.loads((out_dir / "manifest.json").read_text())
    work = out_dir / "experiments" / f"search-errors-{run}-vs-{reference}"
    records, compared, only_run, only_reference = [], [], [], []
    for rec in manifest["recordings"]:
        for label_file, result, group in _scorings(rec) + oracle_copies(rec):
            if not re.search(only, result):
                continue
            mine, theirs = folders[run] / f"{result}.json", folders[reference] / f"{result}.json"
            if mine.exists() != theirs.exists():
                (only_run if mine.exists() else only_reference).append(result)
                continue
            if not mine.exists():
                continue
            run_signals = json.loads(mine.read_text(encoding="utf-8"))["score"]["signals"]
            reference_signals = json.loads(theirs.read_text(encoding="utf-8"))["score"]["signals"]
            labels = json.loads((out_dir / label_file).read_text(encoding="utf-8"))
            if not len(run_signals) == len(reference_signals) == len(labels["signals"]):
                raise ValueError(f"{result}: {run} scored {len(run_signals)} signals, {reference} "
                                 f"{len(reference_signals)}, the labels have {len(labels['signals'])}")
            answer = _bench_edits(bench, labels, [s["decoded"] for s in reference_signals],
                                  [s["decoded"] for s in run_signals], result, run, reference, work)
            for i, (label, counts) in enumerate(zip(labels["signals"], answer)):
                if counts["scored"]:
                    records.append({"group": group, "test_case": result, "index": i, "errors": int(counts["edits"]),
                                    "letters": int(counts["chars"]),
                                    "farnsworth_factor": devset2.farnsworth_of(label)[1]})
            compared.append(result)
    if not compared:
        raise ValueError(f"no test case matching {only!r} has results in both {run} and {reference}")
    kept, left_out = records, None
    if max_farnsworth is not None:
        kept, cut = devset2.farnsworth_cut(records, max_farnsworth)
        left_out = {"groups": _search_stats_by_group(cut), "pooled": _search_stats(cut), "signals": cut}
    return {"run": run, "reference": reference, "only": only, "max_farnsworth": max_farnsworth,
            "test_cases": {"compared": len(compared), "only_in_run": only_run, "only_in_reference": only_reference},
            "groups": _search_stats_by_group(kept), "pooled": _search_stats(kept), "signals": kept,
            "left_out": left_out}


def _search_error_rows(groups: dict, pooled: dict) -> list[str]:
    """The table of search errors per group and pooled (the last row, "all")."""
    rate = lambda x: "—" if x["per_1000_letters"] is None else f"{x['per_1000_letters']:.2f}"  # noqa: E731
    lines = ["| group | signals | reference letters | search errors | signals with a search error | "
             "search errors per 1 000 letters |", "|---|---|---|---|---|---|"]
    for group, x in {**groups, "all": pooled}.items():
        lines.append(f"| {group} | {x['signals']} | {x['letters']} | {x['errors']} | {x['signals_with_errors']} | "
                     f"{rate(x)} |")
    return lines


SEARCH_ERROR_COLUMNS = ("run", "reference", "group", "test_case", "index", "errors", "letters", "farnsworth_factor",
                        "left_out")


def write_search_error_scores(result: dict, path) -> Path:
    """The per-signal counts of search_errors' result as a rounded table (CSV, one row per signal, the signals the
    cut left out too, marked left_out 1; by test case and index): the columns of SEARCH_ERROR_COLUMNS, the counts
    integers and the Farnsworth factor (a ratio) rounded to 0.01, as the score tables hold it (CLAUDE.md: the
    committed table is what a record's numbers are computed from; read_search_error_scores reads it back)."""
    rows = [(r, 0) for r in result["signals"]] + [(r, 1) for r in (result["left_out"] or {}).get("signals", [])]
    path = Path(path)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(SEARCH_ERROR_COLUMNS)
        for r, left_out in sorted(rows, key=lambda x: (x[0]["test_case"], x[0]["index"])):
            w.writerow([result["run"], result["reference"], r["group"], r["test_case"], r["index"], r["errors"],
                        r["letters"], f"{devset2._hundredths(r['farnsworth_factor']):.2f}", left_out])
    return path


def read_search_error_scores(path) -> list[dict]:
    """The rows of a table that write_search_error_scores wrote, in file order, as search_errors' per-signal records
    ({"group", "test_case", "index", "errors", "letters", "farnsworth_factor"} with the rounded factor) and the run, the
    reference and whether the cut left the signal out (left_out, a bool)."""
    with Path(path).open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if tuple(reader.fieldnames or ()) != SEARCH_ERROR_COLUMNS:
            raise ValueError(f"{path}: columns {reader.fieldnames}, expected {list(SEARCH_ERROR_COLUMNS)}")
        return [{"run": r["run"], "reference": r["reference"], "group": r["group"], "test_case": r["test_case"],
                 "index": int(r["index"]), "errors": int(r["errors"]), "letters": int(r["letters"]),
                 "farnsworth_factor": float(r["farnsworth_factor"]), "left_out": r["left_out"] == "1"}
                for r in reader]


def search_error_table(out_dir, result: dict) -> Path:
    """Writes experiments/search-errors-<run>-vs-<reference>.md (the table), .json (search_errors' result, with the
    per-signal counts) and .csv (the per-signal counts, rounded: write_search_error_scores) in out_dir; returns the
    .md's path."""
    run, reference, cases = result["run"], result["reference"], result["test_cases"]
    names = lambda k: ", ".join(f"`{t}`" for t in cases[k]) or "none"  # noqa: E731
    lines = [f"# Search errors: {run} against {reference}", "",
             f"A search error is an edit, in the bench scorer's alignment, between the text {run} decodes for a signal "
             f"and the text {reference} decodes for it: {reference}, with a much wider beam and a higher ceiling, is "
             f"taken as the search's truth. Rates are search errors per 1 000 letters of {reference}'s text (its "
             "characters that are not word spaces, a prosign one; every edit counts, a word space's too). Signals the "
             "labels leave out of the score are not counted. "
             f"Test cases matching `{result['only']}`: {cases['compared']} compared; only in {run}: "
             f"{names('only_in_run')}; only in {reference}: {names('only_in_reference')}.", ""]
    lines += _search_error_rows(result["groups"], result["pooled"]) + [""]
    left_out = result["left_out"]
    if left_out is not None:
        limit = f"{result['max_farnsworth']:g}"
        lines += [f"## Left out: Farnsworth factor above {limit}", "",
                  f"Signals with a Farnsworth factor (the gap timebase over the dit, a ratio) above {limit} are left "
                  "out of the table above, for both runs alike, and counted here (design spec section 5.2, step 3).",
                  ""]
        lines += _search_error_rows(left_out["groups"], left_out["pooled"]) + [""]
    path = Path(out_dir) / "experiments" / f"search-errors-{run}-vs-{reference}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    path.with_suffix(".json").write_text(json.dumps(result) + "\n", encoding="utf-8")
    write_search_error_scores(result, path.with_suffix(".csv"))
    return path


# --- false characters on the noise-only group (docs/plans/2026-10-20-new-decoder-prototype-1.md, R4 and R5) ---

def decoded_symbols(text: str) -> list[str]:
    """The symbols of a decoded text as the scorer counts them (kz4ap::morse::symbols): a prosign token such as <AR>
    is one symbol, from a "<" to the next ">" anywhere after it, else the "<" is a symbol of its own; every other
    character, a word space too, is one."""
    out, i = [], 0
    while i < len(text):
        if text[i] == "<" and ">" in text[i:]:
            close = text.index(">", i)
            out.append(text[i:close + 1])
            i = close + 1
        else:
            out.append(text[i])
            i += 1
    return out


def false_characters(out_dir, run: str, only: str | None = None, results_dirs=None) -> dict:
    """The characters that run `run` decoded on the noise-only group N2, where every one is false (the stations are far
    below any decodable level; they only open the channels): the decoded symbols that are not word spaces (a prosign
    one) of the channels the labels score, per channel-hour. The plan's R4 and R5 count them.

    A channel-hour is 3 600 s of one channel. A test case's channel-seconds are its result's `channel_seconds`
    (the engine's, over all its channels), taken in proportion to the channels counted when some are not scored (every
    channel of a recording has its duration). only: a regular expression on the test cases' result names (default N2:
    seed 1). Returns {"run", "only", "test_cases", "channels", "channel_seconds" (s), "channel_hours" (h), "characters",
    "per_channel_hour" (None without channel-seconds), "channels_with_characters", "by_test_case": [{"test_case",
    "channels", "channel_seconds", "characters", "channels_with_characters"}]}. Raises ValueError for a run without such
    results, or whose result has no channel_seconds (results scored from decoded files carry none)."""
    out_dir = Path(out_dir)
    only = N2 if only is None else only
    roots = [Path(d) for d in (results_dirs if results_dirs is not None else devset2.default_results_dirs(out_dir))]
    found = front_end_dirs(out_dir, roots, [run])
    if not found:
        raise ValueError(f"no results folder named {run!r} in {[str(r) for r in roots]}")
    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    by_test_case = []
    for rec in manifest["recordings"]:
        for _labels, result, _group in _scorings(rec):
            path = found[0] / f"{result}.json"
            if not re.search(only, result) or not path.exists():
                continue
            j = json.loads(path.read_text(encoding="utf-8"))
            signals = j["score"]["signals"]
            scored = [s for s in signals if s["scored"]]
            channel_seconds = float(j.get("channel_seconds", 0.0))
            if channel_seconds <= 0.0:
                raise ValueError(f"{path.name}: channel_seconds is {channel_seconds:g}, not a run of the engine "
                                 "(results scored from decoded files carry none)")
            counts = [sum(1 for x in decoded_symbols(s["decoded"]) if x != " ") for s in scored]
            by_test_case.append({"test_case": result, "channels": len(scored),
                                 "channel_seconds": channel_seconds * len(scored) / len(signals),
                                 "characters": sum(counts), "channels_with_characters": sum(1 for n in counts if n)})
    if not by_test_case:
        raise ValueError(f"no test case matching {only!r} has results in {run}")
    seconds = sum(t["channel_seconds"] for t in by_test_case)
    characters = sum(t["characters"] for t in by_test_case)
    return {"run": run, "only": only, "test_cases": len(by_test_case), "channels": sum(t["channels"] for t in by_test_case),
            "channel_seconds": seconds, "channel_hours": seconds / 3600.0, "characters": characters,
            "per_channel_hour": characters / (seconds / 3600.0) if seconds > 0 else None,
            "channels_with_characters": sum(t["channels_with_characters"] for t in by_test_case),
            "by_test_case": by_test_case}


def false_character_table(out_dir, result: dict) -> Path:
    """Writes experiments/false-characters-<run>.md (the table) and .json (false_characters' result) in out_dir;
    returns the .md's path."""
    rate = lambda x: "—" if x["per_channel_hour"] is None else f"{x['per_channel_hour']:.2f}"  # noqa: E731
    lines = [f"# False characters on the noise-only group: {result['run']}", "",
             "The characters a run decoded on the noise-only group N2, where every one is false: the decoded symbols "
             "that are not word spaces (a prosign is one), of the channels the labels score, per channel-hour "
             f"(3 600 s of one channel). Test cases matching `{result['only']}`.", "",
             "| test case | channels | channel-seconds (s) | false characters | channels with a false character | "
             "false characters per channel-hour |", "|---|---|---|---|---|---|"]
    for t in result["by_test_case"]:
        per_hour = None if t["channel_seconds"] <= 0 else t["characters"] / (t["channel_seconds"] / 3600.0)
        lines.append(f"| {t['test_case']} | {t['channels']} | {t['channel_seconds']:.1f} | {t['characters']} | "
                     f"{t['channels_with_characters']} | {rate({'per_channel_hour': per_hour})} |")
    lines.append(f"| all | {result['channels']} | {result['channel_seconds']:.1f} | {result['characters']} | "
                 f"{result['channels_with_characters']} | {rate(result)} |")
    path = Path(out_dir) / "experiments" / f"false-characters-{result['run']}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    path.with_suffix(".json").write_text(json.dumps(result) + "\n", encoding="utf-8")
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
    se = sub.add_parser("search-errors", help="a run's search errors against a wide-beam reference run: the edits "
                                              "between their texts per 1 000 letters, per group and pooled")
    ss = sub.add_parser("search-stats", help="a run's search statistics (kz4ap_synth.suites run --stats), reduced per "
                                             "group: live stories, the ceiling, the newborn lines' fates, the deficit "
                                             "before leading")
    fc = sub.add_parser("false-characters", help="the characters a run decoded on the noise-only group N2, per "
                                                 "channel-hour")
    for s in (c, pd, st, no, d2, se, ss, fc):
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
    d2.add_argument("--max-farnsworth", type=float, default=None, metavar="F",
                    help="the I2 cut: leave the signals whose Farnsworth factor (a ratio) is above F out of every fit "
                         "and comparison, for every decoder alike, and list them separately (the plan: 8)")
    d2.add_argument("--leave-out-group", dest="leave_out_groups", action="append", default=None, metavar="GROUP",
                    help='leave a whole group out of every fit and comparison, for every decoder alike, and list its '
                         'signals separately (repeatable; the new decoder has no Farnsworth spacing, D14, so R4 '
                         'passes "I2 Farnsworth"); a group with no signals is an error')
    se.add_argument("--run", required=True, help="the run under test: its results folder name")
    se.add_argument("--reference", required=True, help="the wide-beam reference run: its results folder name")
    se.add_argument("--bench", type=Path, required=True,
                    help="kz4ap-bench: its --score-decoded gives the edits, in the scorer's alignment")
    se.add_argument("--only", default=None,
                    help="regular expression on result names (default experiments.DEV2: seed 1 of the oracle test "
                         "cases; A2's detector-path copies, the same signals decoded again, are left out so that A2 "
                         "is not counted twice; the plan's runs pass the same expression)")
    se.add_argument("--results", dest="results_dirs", action="append", type=Path, default=None,
                    help="a results root (repeatable; default OUT/results and OUT/experiments/results)")
    se.add_argument("--max-farnsworth", type=float, default=None, metavar="F",
                    help="the I2 cut: leave the signals whose Farnsworth factor (a ratio) is above F out, for both "
                         "runs alike, and count them separately")
    for s, what in ((ss, "search statistics"), (fc, "results")):
        s.add_argument("--run", required=True, help=f"the run whose {what} are reduced: its results folder name")
        s.add_argument("--results", dest="results_dirs", action="append", type=Path, default=None,
                       help="a results root (repeatable; default OUT/results and OUT/experiments/results)")
    ss.add_argument("--only", default=None, help="regular expression on result names (default: every test case "
                                                 "that has a statistics file)")
    fc.add_argument("--only", default=None, help="regular expression on result names (default experiments.N2: the "
                                                 "noise-only group, seed 1)")
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
        if args.reference is not None and args.reference not in args.decoders:
            parser.error("--reference must be one of the --decoder names")
        path = devset2.write_analysis(args.out, args.decoders, args.name, args.reference, args.results_dirs,
                                      args.only or devset2.DEV2_ANALYSIS, tuple(args.fit_groups or devset2.FIT_GROUPS),
                                      args.method, args.resamples, args.scores, args.max_farnsworth,
                                      tuple(args.leave_out_groups or ()))
        print(f"wrote {path} and {path.with_suffix('.md')}")
    elif args.command == "search-errors":
        result = search_errors(args.out, args.bench, args.run, args.reference, args.only, args.results_dirs,
                               args.max_farnsworth)
        path = search_error_table(args.out, result)
        pooled = result["pooled"]
        print(f"{args.run} against {args.reference}: {pooled['errors']} search errors in {pooled['letters']} letters, "
              + ("no letters" if pooled["per_1000_letters"] is None else
                 f"{pooled['per_1000_letters']:.2f} per 1 000 letters"))
        print(f"wrote {path}, {path.with_suffix('.json')} and {path.with_suffix('.csv')}")
    elif args.command == "search-stats":
        summary = search_stats.run_summary(args.out, args.run, args.only, args.results_dirs)
        everything = summary["all"]
        print(f"{args.run}: {summary['test_cases']} test cases, {everything['channels']} channels, "
              f"{everything['channel_seconds']:.1f} channel-seconds; live stories "
              f"{search_stats._fmt(everything['live_stories']['mean'], '.1f')} on average, "
              f"{search_stats._fmt(everything['live_stories']['max'], '.0f')} at most")
        print(search_stats.r1_line(summary))
        print(f"wrote {search_stats.write_summary(args.out, summary)}")
    elif args.command == "false-characters":
        result = false_characters(args.out, args.run, args.only, args.results_dirs)
        print(f"{args.run}: {result['characters']} false characters in {result['channel_hours']:.3f} channel-hours, "
              + ("no rate" if result["per_channel_hour"] is None else f"{result['per_channel_hour']:.2f} per channel-hour"))
        print(f"wrote {false_character_table(args.out, result)}")
    elif args.command == "scores":
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
