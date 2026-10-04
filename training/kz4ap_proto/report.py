"""The prototype against the engine's front ends on the same signals, every regime included (oracle channels, and
the detector path: the Matched path's detector's channels): the full comparison the owner decides on (spec
section 7; owner, 2026-09-30: no acceptance gate), from the suite's results (plan Task 12)."""

from __future__ import annotations

import json
import re
from pathlib import Path

from kz4ap_synth import suites

from . import metrics
from .runner import detector_jobs, oracle_scorings

REFERENCES = ("matched", "baseline")
# The start-up runaways of milestone 2 (docs/signal-processing.md 8b, "Start-up runaway"): recording, labeled Hz.
RUNAWAY_CASES = (("A-awgn-25wpm-1-s1", 6606.5), ("A-awgn-25wpm-0-s2", -2991.9))
# A verdict calls a regime better or worse when its paired bootstrap 95% interval excludes 0 (a convention, heuristic).
# Each regime is one such test, with no multiplicity correction: a regime truly unchanged reads better or worse with
# probability about 1 - 0.95 = 0.05.
FALSE_CALL_RATE = 0.05
STATISTICS_NOTE = (
    "Statistics. CER columns are pooled: summed edits over summed symbols. The paired columns are the mean over "
    "signals of the per-signal CER difference (decoder minus reference, signal by signal on the same labels), so "
    "a paired mean and the difference of two pooled CERs can differ, even in sign. Intervals are bootstrap 95% "
    "intervals with the signal as the unit (1000 resamples of signals within each group and tag); signals of one "
    "recording share its noise and its keying draws, and that within-recording correlation is not modeled, so the "
    "intervals are likely too narrow where a regime has few recordings (the recordings column says how many). "
    "\"better\" / \"worse\" means the paired interval excludes 0 (a convention, heuristic), with no correction for "
    "the number of regimes compared: of R regimes that are truly unchanged, about 0.05·R are expected to read better "
    "or worse by chance.")


def comparable_rows(out_dir, name, results_dirs=None, only=None) -> list[dict]:
    """Rows of the prototype and the references, on the signals the prototype decoded."""
    rows, _ = suites.load_results(Path(out_dir), results_dirs, [name, *REFERENCES])
    if only:
        rows = [r for r in rows if re.search(only, r["recording"])]
    ours = {(r["recording"], r["index"]) for r in rows if r["front_end"] == name}
    return [r for r in rows if (r["recording"], r["index"]) in ours]


def not_comparable(group: str, tag: str) -> str | None:
    """Why a row's comparison with the engine is unfair (plan: Design decisions, "Where the streams are recorded")."""
    if group == "F tuning" and tag.startswith("drift"):
        return "oracle mix follows the labeled drift (favors the prototype)"
    if group == "H two-station QSO, oracle" and suites._tag_offset_hz(tag) != 0.0:
        return "QSO label: the answering station is off the oracle mix (handicaps the prototype)"
    return None


def _first_word(rows, fe, group, tag, lo_db, hi_db):
    sig = [(r["first_word_edits"], r["first_word_symbols"]) for r in rows
           if r["front_end"] == fe and r["group"] == group and r["tag"] == tag and r["scored"]
           and lo_db <= r["snr_db"] <= hi_db and r["first_word_symbols"] > 0]
    e, s = sum(x for x, _ in sig), sum(y for _, y in sig)
    return {"cer": e / s if s else None, "interval": suites.bootstrap_cer(sig, suites._rng_for(("fw", fe, tag))),
            "signals": len(sig)}


def _signal_cer(out_dir, results_dirs, fe, recording, freq_hz):
    for root in results_dirs or [Path(out_dir) / "results"]:
        path = Path(root) / fe / f"{recording}.json"
        if path.exists():
            signals = json.loads(path.read_text())["score"]["signals"]
            return min(signals, key=lambda s: abs(s["freq_offset_hz"] - freq_hz))["cer"]
    return None


# What stage 1 leaves to stage 2 (owner and controller, 2026-09-30; spec section 7; backlog).
DETECTOR_GAP = ("Which tracks the detector opens is unchanged by the redesign. Detection recall, false tracks and tracks "
                "per QSO as the bench counts them (tracks that decoded text) depend on the decoder, and stage 1 reports "
                "them for the prototype on the recorded channels of the Matched path's detector, beside Matched and "
                "Envelope. Stage 2 confirms them in C++ behind the live detector, with the frequency tracker in the "
                "loop (stage 1 mixes by the detector's frequency without the tracker's fine-tuning).")
# Detector-path groups: the prototype decodes the Matched path's detector channels; Envelope's detector (bin
# attribution) opens its own tracks, so against Envelope these rows compare per label, not per track.
DETECTOR_PATH_GROUPS = ("H two-station QSO", "H two-station QSO (per station)", "strong", "pauses", "tune-up",
                        "first sample", "crowded", "band")


def detector_measures(out_dir, name, results_dirs=None, only=None) -> dict:
    """The bench's detection measures on the detector path, per front end (the prototype, Matched, Envelope) and
    group: scored labels, detected labels, detection recall, and false tracks (tracks that decoded text and matched
    no label), summed over the non-oracle recordings' main scorings; plus tracks per QSO (group H, suites.track_splits).
    As the bench counts them they depend on the decoder (a track counts only if it decoded text)."""
    out_dir = Path(out_dir)
    manifest = json.loads((out_dir / "manifest.json").read_text())
    fe_dirs = {p.name: p for p in suites.front_end_dirs(out_dir, results_dirs, [name, *REFERENCES])}
    out: dict = {}
    for fe in (name, *REFERENCES):
        for rec in manifest["recordings"]:
            if rec["oracle"] or (only and not re.search(only, rec["name"])) or fe not in fe_dirs:
                continue
            path = fe_dirs[fe] / f"{rec['name']}.json"
            if not path.exists():
                continue
            s = json.loads(path.read_text())["score"]
            g = out.setdefault(fe, {}).setdefault(rec["group"], {"recordings": 0, "scored": 0, "detected": 0,
                                                                  "false_tracks": 0})
            g["recordings"] += 1
            for k in ("scored", "detected", "false_tracks"):
                g[k] += s[k]
    for groups in out.values():
        for g in groups.values():
            g["detection_recall"] = g["detected"] / g["scored"] if g["scored"] else None
    # the same directories and recordings as the detection counts above
    splits = suites.track_splits(out_dir, results_dirs, [name, *REFERENCES], only)
    return {"by_front_end": out,
            "tracks_per_qso": {f"{fe} | {tag}": v for (fe, tag), v in sorted(splits.items())}}


def coverage(out_dir, name, results_dirs=None, only=None) -> dict:
    """How much of the suite the prototype's results cover: scorings (result files) and labels with a result for
    `name`, out of those available (oracle scorings and the detector path's scorings; `only` on result names)."""
    out_dir = Path(out_dir)
    available = [(j["result"], j["labels"]) for j in oracle_scorings(out_dir, only)]
    available += [(result, labels) for j in detector_jobs(out_dir, only) for labels, result, _ in j["scorings"]]
    fe_dir = {p.name: p for p in suites.front_end_dirs(out_dir, results_dirs, [name])}.get(name)
    out = {"scorings": 0, "scorings_available": len(available), "labels": 0, "labels_available": 0}
    for result, labels in available:
        count = len(json.loads((out_dir / labels).read_text())["signals"])
        out["labels_available"] += count
        if fe_dir is not None and (fe_dir / f"{result}.json").exists():
            out["scorings"] += 1
            out["labels"] += count
    return out


def verdict(interval) -> str:
    """"better" or "worse" when the paired 95% interval (prototype minus reference, CER) excludes 0, else
    "unchanged"; "no interval" for fewer than 2 signals."""
    if interval is None:
        return "no interval"
    if interval[1] < 0:
        return "better"
    if interval[0] > 0:
        return "worse"
    return "unchanged"


def _recordings(rows, name) -> dict:
    """(group, tag): the number of recordings (result names) among the prototype's scored rows."""
    found: dict = {}
    for r in rows:
        if r["front_end"] == name and r["scored"]:
            found.setdefault((r["group"], r["tag"]), set()).add(r["recording"])
    return {k: len(v) for k, v in found.items()}


def comparison(rows, name, out_dir, results_dirs=None) -> dict:
    """The full comparison, no gate (owner): (a) group A S500 at CER 0.10 and 0.05 at every speed for the prototype,
    Matched and Envelope, with intervals and the differences; (b) R1 (group D step 20->35 WPM), R2 (group A 12 WPM
    first-word CER at S500 6-20 dB) and the start-up runaway cases; (c) every (group, tag): the prototype against
    Matched and against Envelope, paired, called better / worse / unchanged beyond its interval, by how much."""
    agg = suites.aggregate(rows)
    crossings = []
    for tag in ("12 wpm", "25 wpm", "40 wpm"):
        for threshold in ("0.1", "0.05"):
            cell = {fe: (agg[(fe, "A sensitivity", tag)]["snr_at_cer"].get(threshold),
                         agg[(fe, "A sensitivity", tag)]["snr_at_cer_interval"].get(threshold))
                    if (fe, "A sensitivity", tag) in agg else (None, None) for fe in (name, *REFERENCES)}
            ours = cell[name][0]
            crossings.append({"tag": tag, "cer": threshold, **{fe: list(v) for fe, v in cell.items()},
                              **{f"minus_{fe}_db": ours - cell[fe][0] if ours is not None and cell[fe][0] is not None
                                 else None for fe in REFERENCES}})
    r1 = {fe: {k: agg.get((fe, "D speed", "step 20->35"), {}).get(k) for k in ("cer", "cer_interval")}
          for fe in (name, *REFERENCES)}
    r2 = {fe: _first_word(rows, fe, "A sensitivity", "12 wpm", 6.0, 20.0) for fe in (name, *REFERENCES)}
    runaways = [{"recording": rec, "freq_hz": f,
                 **{fe: _signal_cer(out_dir, results_dirs, fe, rec, f) for fe in (name, *REFERENCES)}}
                for rec, f in RUNAWAY_CASES]
    pairs = {ref: suites.paired_differences(rows, ref, name) for ref in REFERENCES}
    recordings = _recordings(rows, name)
    regimes = []
    for group, tag in sorted(set(pairs["matched"]) | set(pairs["baseline"])):
        entry = {"group": group, "tag": tag, "recordings": recordings.get((group, tag), 0),
                 "not_comparable": not_comparable(group, tag),
                 "note": not_comparable(group, tag) or ("detector path: Matched's tracks; Envelope's detector opens "
                                                        "its own" if group in DETECTOR_PATH_GROUPS else None)}
        for ref in REFERENCES:
            v = pairs[ref].get((group, tag))
            entry[ref] = None if v is None else {"signals": v["signals"], "mean": v["mean"], "interval": v["interval"],
                                                 "verdict": verdict(v["interval"])}
        regimes.append(entry)
    counts = {ref: {k: sum(1 for e in regimes if e[ref] and e[ref]["verdict"] == k and not e["not_comparable"])
                    for k in ("better", "worse", "unchanged", "no interval")} for ref in REFERENCES}
    # With no multiplicity correction: the better-or-worse calls expected by chance if every compared regime (one
    # with an interval) were truly unchanged
    chance = {ref: FALSE_CALL_RATE * sum(c[k] for k in ("better", "worse", "unchanged")) for ref, c in counts.items()}
    return {"crossings": crossings, "r1": r1, "r2": r2, "runaways": runaways, "regimes": regimes, "counts": counts,
            "expected_by_chance": chance, "detector_gap": DETECTOR_GAP}


def _fmt(v, interval=None, fmt=".3f"):
    return suites._with_interval(v, interval, fmt)


def write_report(out_dir, name, results_dirs=None, only=None, suffix="") -> Path:
    out_dir = Path(out_dir)
    rows = comparable_rows(out_dir, name, results_dirs, only)
    agg = suites.aggregate(rows)
    pairs = {ref: suites.paired_differences(rows, ref, name) for ref in REFERENCES}
    comp = comparison(rows, name, out_dir, results_dirs)
    cover = coverage(out_dir, name, results_dirs, only)
    recordings = _recordings(rows, name)
    lines = [f"# {name} against Matched and Envelope" + (f" ({suffix})" if suffix else ""), "",
             f"The same labeled signals for all three, on the signals {name} has results for. Oracle groups (and the "
             "`, oracle` copies): every decoder decodes one oracle channel per label. Detector-path groups (group H "
             f"through the detector, band, crowded, pauses, strong, tune-up, first sample): {name} decodes the "
             "channels the Matched path's detector opened, so against Matched it is the same tracks, while "
             "Envelope's detector opens its own tracks, so against Envelope it is the same labels only. Paired "
             f"columns: {name} minus the reference (negative favors {name}). S₅₀₀: key-down carrier power over noise "
             "power in 500 Hz, dB. Rows marked * are not comparable (reason given).", "",
             f"Coverage: {name} has results for {cover['scorings']} of {cover['scorings_available']} test cases "
             f"({cover['labels']} of {cover['labels_available']} labels).", "",
             STATISTICS_NOTE, ""]
    for group in sorted({g for _, g, _ in agg}):
        lines += [f"## {group}", "", f"| tag | recordings | signals | {name} CER | Matched CER | Envelope CER | "
                  f"{name} − Matched (paired mean) | {name} − Envelope (paired mean) | "
                  f"first-word CER {name} / Matched / Envelope |", "|---|---|---|---|---|---|---|---|---|"]
        for tag in sorted({t for fe, g, t in agg if g == group}):
            v = {fe: agg.get((fe, group, tag)) for fe in (name, *REFERENCES)}
            if v[name] is None:
                continue
            note = not_comparable(group, tag)
            p = [pairs[ref].get((group, tag), {}) for ref in REFERENCES]
            cells = [_fmt(v[fe]["cer"], v[fe]["cer_interval"]) if v[fe] else "—" for fe in (name, *REFERENCES)]
            fw = " / ".join(f"{v[fe]['first_word_cer']:.3f}" if v[fe] else "—" for fe in (name, *REFERENCES))
            lines.append(f"| {tag}{' *' + note if note else ''} | {recordings.get((group, tag), 0)} | "
                         f"{v[name]['signals']} | {' | '.join(cells)} | "
                         f"{_fmt(p[0].get('mean'), p[0].get('interval'), '+.3f')} | "
                         f"{_fmt(p[1].get('mean'), p[1].get('interval'), '+.3f')} | {fw} |")
        lines.append("")
    names = {name: name, "matched": "Matched", "baseline": "Envelope"}
    lines += ["## Comparison for the owner (no acceptance gate)", "",
              "Group A, S₅₀₀ at CER 0.10 and 0.05 (dB), with intervals; differences in dB of S₅₀₀ (negative: the "
              f"{name} needs less signal):", "",
              f"| tag | CER | {name} | Matched | Envelope | {name} − Matched | {name} − Envelope |", "|---|---|---|---|---|---|---|"]
    for c in comp["crossings"]:
        lines.append(f"| {c['tag']} | {c['cer']} | " + " | ".join(_fmt(c[fe][0], c[fe][1], '.1f') for fe in (name, *REFERENCES))
                     + f" | {_fmt(c['minus_matched_db'], None, '+.1f')} | {_fmt(c['minus_baseline_db'], None, '+.1f')} |")
    r1, r2 = comp["r1"], comp["r2"]
    lines += ["", "Regression R1 (group D, step 20→35 WPM, CER; Matched 0.394 after milestone-2a Task 17, 0.113 at Task 14): " +
              ", ".join(f"{names[fe]} {_fmt(r1[fe]['cer'], r1[fe]['cer_interval'])}" for fe in r1) + ".",
              "Regression R2 (group A 12 WPM, first-word CER at S₅₀₀ 6–20 dB; Matched 0.985 after Task 17): " +
              ", ".join(f"{names[fe]} {_fmt(r2[fe]['cer'], r2[fe]['interval'])} ({r2[fe]['signals']} signals)" for fe in r2) + ".",
              "Start-up runaway cases (CER): " + "; ".join(
                  f"{c['recording']} {c['freq_hz']:+.1f} Hz: " + ", ".join(
                      f"{names[fe]} {_fmt(c[fe])}" for fe in (name, *REFERENCES)) for c in comp["runaways"]) + ".", "",
              f"Every regime: {name} minus the reference, the per-signal mean CER difference with its paired bootstrap "
              "95% interval (signals as the unit); better / worse when the interval excludes 0 (a convention, "
              "heuristic), else unchanged; no correction for the number of regimes. Comparable regimes (not-comparable "
              "rows left out): " + "; ".join(
                  f"against {names[ref]}: " + ", ".join(f"{v} {k}" for k, v in comp["counts"][ref].items())
                  + f" (if all {sum(comp['counts'][ref][k] for k in ('better', 'worse', 'unchanged'))} with an "
                  f"interval were truly unchanged, about {comp['expected_by_chance'][ref]:.1f} would read better or "
                  "worse by chance)"
                  for ref in REFERENCES) + ".", "",
              "| group | tag | recordings | against Matched | against Envelope | note |", "|---|---|---|---|---|---|"]
    for e in comp["regimes"]:
        cells = [f"{_fmt(e[ref]['mean'], e[ref]['interval'], '+.3f')} {e[ref]['verdict']}" if e[ref] else "—"
                 for ref in REFERENCES]
        lines.append(f"| {e['group']} | {e['tag']} | {e['recordings']} | {cells[0]} | {cells[1]} | {e['note'] or ''} |")
    measures = detector_measures(out_dir, name, results_dirs, only)
    lines += ["", "Detection measures on the detector path (the Matched path's detector; as the bench counts them, a "
              "track counts only if it decoded text):", "",
              "| group | decoder | recordings | labels scored | detected | detection recall | false tracks |",
              "|---|---|---|---|---|---|---|"]
    for fe, groups in measures["by_front_end"].items():
        for g, v in sorted(groups.items()):
            lines.append(f"| {g} | {names[fe]} | {v['recordings']} | {v['scored']} | {v['detected']} | "
                         f"{_fmt(v['detection_recall'], None, '.3f')} | {v['false_tracks']} |")
    if measures["tracks_per_qso"]:  # the keys are "<front end> | <tag>", two cells
        lines += ["", "Tracks per QSO (group H through the detector, the same recordings as above):", "",
                  "| decoder | group-H tag | QSOs | tracks per QSO |", "|---|---|---|---|"]
        lines += [f"| {k} | {v['qsos']} | {v['mean_tracks']:.2f} |" for k, v in measures["tracks_per_qso"].items()]
    lines += ["", f"**Stated gap.** {comp['detector_gap']}", ""]
    lines += ["", f"## {name}'s own statistics", ""]
    speed = metrics.speed_errors(out_dir, name, only)
    lines += ["Selected speed off the label's by more than ×1.5 (fraction of selection instants, from 3 s into each "
              "transmission, S₅₀₀ ≥ 6 dB, constant-speed labels) and lock-ins (3 s or longer):", "",
              "| group | instants | fraction | lock-ins / transmissions |", "|---|---|---|---|"]
    lines += [f"| {g} | {v['instants']} | {_fmt(v['error_fraction'], v['interval'])} | {v['lock_ins']} / {v['transmissions']} |"
              for g, v in sorted(speed.items())]
    switches = metrics.switch_stats(out_dir, name, only)
    overs = metrics.spurious_over_starts(out_dir, name, only)
    false = metrics.false_characters(out_dir, name, only)
    lines += ["", "| group | switches per min | alternations per min | over starts inside a transmission, per "
              "transmission | false characters per min outside transmissions |", "|---|---|---|---|---|"]
    for g in sorted(switches):
        lines.append(f"| {g} | {_fmt(switches[g]['switches_per_min'], None, '.2f')} | "
                     f"{_fmt(switches[g]['alternations_per_min'], None, '.2f')} | "
                     f"{_fmt(overs.get(g, {}).get('per_transmission'), None, '.3f')} | "
                     f"{_fmt(false.get(g, {}).get('per_min'), None, '.3f')} |")
    lines += ["", f"Decoding CPU: {1000 * metrics.cpu_per_channel_second(out_dir, name, only):.1f} ms per "
              "channel-second (Python prototype; not comparable with the engine's C++)."]
    path = out_dir / f"report-{name}{('-' + suffix) if suffix else ''}.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    (path.with_suffix(".json")).write_text(json.dumps({"comparison": comp, "coverage": cover, "detector_measures": measures,
                                                       "speed": speed, "switches": switches,
                                                       "over_starts": overs, "false_characters": false},
                                                      indent=2, default=str) + "\n")
    return path
