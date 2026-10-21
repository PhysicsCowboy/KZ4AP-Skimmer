"""The new decoder's search statistics, reduced to what the measurement runs need (docs/plans/2026-10-20-new-decoder-
prototype-1.md, section 6.1: R0 to R4).

kz4ap-bench --decoder viterbi --oracle --stats FILE writes the statistics of each oracle channel's search
(bench/src/viterbi_json.hpp documents the file; its numbers are exact). Two forms of the one format, "kz4ap-viterbi-
search-stats":

- version 1 (--stats-detail): every newborn line, as columns (45 kB per channel-second: for a small run);
- version 2 (the default of --stats): one summary per channel, which holds what the reductions below need and no
  record of a line except the lines that ever led.

A "line" is a newborn story and its descendants (search.hpp): line 0 is the channel's first story (kind Start, the
no-signal story), a newborn from silence has kind Silence (an over starts), a rescue newborn has kind Rescue (the sender
begins a new story at a letter start). A line "led" if one of its stories was the best after the pruning of some piece
(first_led_s is not null). A line ends in one of four ways: Alive (it has a story at the end of the recording), Merge
(merged into a better story of its state), Beam (more than beam_nats behind the best) or Ceiling (beyond the ceiling's
best stories). A piece is piece_s = 2 ms of the channel (config.piece_s); the live series is kept in windows of
round(0.1 s / piece_s) pieces (50 at the default); times are stream times, s; deficits are in nats, relative to the best
story of the piece.

reduce_channel is the reduction of a version 1 channel into the summary a version 2 channel holds, so it defines what the
bench keeps. The reductions over summaries are one function per run's question:

- R0 live_stories: the mean over the pieces (the windows' means weighted by their pieces) and the largest number of live
  stories; the share of pieces at which the ceiling decided (dropped at least one story); the newborns' fates, as counts
  by kind and by end.
- R1 led_deficits: the largest deficit any newborn (a line of kind Silence or Rescue) reaches before it leads, per group:
  the maximum and the upper quantiles of the distribution over the newborns that led (a line that never led has no
  deficit "before it leads"), and how many exceed the margin of the plan's rule (MARGIN_NATS).
- R2 dropped_by_beam: the lines the beam dropped, how many of them within their first element (before any story of the
  line had left the newborn's first mark) and how many never led.
- R3 removed_by_ceiling: the lines the ceiling removed, with the same split.

run_summary applies them to the statistics files of one run (a results folder of kz4ap_synth.suites run --stats), per
group of the suite and pooled.
"""

from __future__ import annotations

import json
import math
import re
from pathlib import Path

STATS_FORMAT = "kz4ap-viterbi-search-stats"
DETAIL_VERSION = 1
SUMMARY_VERSION = 2
# The legends of the file: the codes of a line's kind and end, in the order of their values (search.hpp), the axes of a
# channel summary's fates, and the columns of a line (NewbornLine's members).
LINE_KINDS = ("Start", "Silence", "Rescue")
LINE_ENDS = ("Alive", "Merge", "Beam", "Ceiling")
FATE_AXES = ("kind", "end", "ended_in_first_element", "led")
LINE_COLUMNS = ("birth_s", "kind", "first_led_s", "largest_deficit_nats", "end", "end_s", "ended_in_first_element")
# s: the window of the live series (search.cpp, kStatsWindowS)
STATS_WINDOW_S = 0.1
# nats: the margin of R1's rule, "if the largest deficit exceeds the 10-nat margin, the margin and the minimum beam are
# raised to it" (the plan, section 6.1; design notes, topic 5, item 4)
MARGIN_NATS = 10.0
# The upper quantiles reported for R1's distribution (heuristic: the tail the rule looks at, beside the maximum)
DEFICIT_QUANTILES = (0.5, 0.9, 0.99, 0.999)
_SUMMARY_KEYS = ("track_id", "label_index", "pieces", "ceiling_pieces", "live_histogram", "live_mean_stories",
                 "live_max_stories", "lines", "fates", "led_lines")


# --- reading ---

def check_stats(stats, where: str = "statistics") -> None:
    """Raises ValueError unless `stats` is a statistics file of format version 1 or 2 with a configuration and
    channels."""
    if not isinstance(stats, dict) or stats.get("format") != STATS_FORMAT:
        raise ValueError(f"{where}: its format is not {STATS_FORMAT!r}")
    if stats.get("version") not in (DETAIL_VERSION, SUMMARY_VERSION):
        raise ValueError(f"{where}: version {stats.get('version')!r}; this reader reads versions "
                         f"{DETAIL_VERSION} (every line) and {SUMMARY_VERSION} (the summary)")
    for key in ("config", "channels"):
        if key not in stats:
            raise ValueError(f"{where}: {key!r} is missing")
    if not isinstance(stats["config"], dict) or not stats["config"].get("piece_s", 0) > 0:
        raise ValueError(f"{where}: config.piece_s (s) is missing or not positive")


def read_stats(path) -> dict:
    """A statistics file of either version, as the JSON object it holds. Raises FileNotFoundError, or ValueError (not
    JSON, another format or version, no configuration or channels)."""
    text = Path(path).read_text(encoding="utf-8")
    try:
        stats = json.loads(text)
    except json.JSONDecodeError as error:
        raise ValueError(f"{path}: not JSON ({error})") from error
    check_stats(stats, str(path))
    return stats


def window_pieces(piece_s: float) -> int:
    """Pieces in a window of the live series: round(0.1 s / piece_s), half up, at least 1 (search.cpp)."""
    return max(1, math.floor(STATS_WINDOW_S / piece_s + 0.5))


# --- one channel ---

def reduce_channel(channel: dict, piece_s: float) -> dict:
    """The summary of a version 1 channel (the object a version 2 file holds for it), keys:

    track_id, label_index                 as in the file
    pieces, ceiling_pieces                counts, as in the file
    live_histogram                        counts of pieces by live stories after pruning, in bins of 10, as in the file
    live_mean_stories                     stories: the mean number of live stories after pruning over the channel's
                                          pieces = the sum over the 0.1 s windows of (the window's mean x its pieces)
                                          over the pieces; a window holds window_pieces(piece_s) pieces, the last one
                                          the rest (0 for a channel that searched nothing)
    live_max_stories                      stories: the largest live count at any piece, the largest of the windows'
                                          maxima (0 for no pieces)
    lines                                 count: the newborn lines
    fates                                 counts of the lines by [kind][end][ended_in_first_element][led]: kind and end
                                          are the codes of LINE_KINDS and LINE_ENDS, the last two axes 0 (false) or 1
                                          (true); 3 x 4 x 2 x 2 integers
    led_lines                             the lines that ever led (first_led_s is not null), as columns of LINE_COLUMNS
                                          plus "line", the line's id (its position in the version 1 columns)

    Raises ValueError for a channel the windows of whose live series do not cover its pieces, or whose codes lie outside
    the legends."""
    pieces = int(channel["pieces"])
    means, maxima = channel["live_mean_100ms"], channel["live_max_100ms"]
    if len(means) != len(maxima):
        raise ValueError(f"channel {channel['track_id']}: {len(means)} windows of means, {len(maxima)} of maxima")
    window = window_pieces(piece_s)
    windows = len(means)
    last = pieces - window * (windows - 1)
    if (pieces == 0) != (windows == 0) or (windows and not 1 <= last <= window):
        raise ValueError(f"channel {channel['track_id']}: {windows} windows of {window} pieces do not cover its "
                         f"{pieces} pieces")
    total = 0.0
    for w, mean in enumerate(means):
        total += mean * (window if w + 1 < windows else last)
    columns = channel["lines"]
    n = len(columns["kind"])
    if any(len(columns[name]) != n for name in LINE_COLUMNS):
        raise ValueError(f"channel {channel['track_id']}: the columns of lines have different lengths")
    fates = [[[[0, 0], [0, 0]] for _ in LINE_ENDS] for _ in LINE_KINDS]
    led = []
    for i in range(n):
        kind, end = columns["kind"][i], columns["end"][i]
        if not (0 <= kind < len(LINE_KINDS) and 0 <= end < len(LINE_ENDS)):
            raise ValueError(f"channel {channel['track_id']}, line {i}: kind {kind} or end {end} is not in the legend")
        has_led = columns["first_led_s"][i] is not None
        fates[kind][end][int(bool(columns["ended_in_first_element"][i]))][int(has_led)] += 1
        if has_led:
            led.append(i)
    return {"track_id": channel["track_id"], "label_index": channel["label_index"], "pieces": pieces,
            "ceiling_pieces": int(channel["ceiling_pieces"]), "live_histogram": list(channel["live_histogram"]),
            "live_mean_stories": total / pieces if pieces else 0.0,
            "live_max_stories": float(max(maxima)) if windows else 0.0, "lines": n, "fates": fates,
            "led_lines": {"line": led, **{name: [columns[name][i] for i in led] for name in LINE_COLUMNS}}}


def channel_summaries(stats: dict) -> list[dict]:
    """The summaries of a statistics file's channels, whichever its version: a version 1 file's channels reduced
    (reduce_channel), a version 2 file's as the file holds them (checked for their keys)."""
    check_stats(stats)
    if stats["version"] == DETAIL_VERSION:
        return [reduce_channel(c, stats["config"]["piece_s"]) for c in stats["channels"]]
    for c in stats["channels"]:
        missing = [key for key in _SUMMARY_KEYS if key not in c]
        if missing:
            raise ValueError(f"channel {c.get('track_id')}: {', '.join(missing)} missing from a version "
                             f"{SUMMARY_VERSION} channel")
    return list(stats["channels"])


# --- the reductions over channels (a group, a run) ---

def quantile(sorted_values: list, q: float):
    """The q quantile of ascending values by the inverted CDF: the smallest value that has at least the share q of the
    values at or below it. A value of the data, no interpolation (None for no values)."""
    if not sorted_values:
        return None
    return sorted_values[max(1, math.ceil(q * len(sorted_values) * (1.0 - 1e-12))) - 1]


def _pooled_fates(channels) -> list:
    pooled = [[[[0, 0], [0, 0]] for _ in LINE_ENDS] for _ in LINE_KINDS]
    for c in channels:
        for k, by_end in enumerate(c["fates"]):
            for e, by_first in enumerate(by_end):
                for f, by_led in enumerate(by_first):
                    for l, n in enumerate(by_led):
                        pooled[k][e][f][l] += n
    return pooled


def _count(fates, kind=None, end=None, first=None, led=None) -> int:
    """The lines of pooled fates with the given kind, end, first-element and led codes (None: any)."""
    return sum(n for k, by_end in enumerate(fates) if kind in (None, k)
               for e, by_first in enumerate(by_end) if end in (None, e)
               for f, by_led in enumerate(by_first) if first in (None, f)
               for l, n in enumerate(by_led) if led in (None, l))


def live_stories(channels) -> dict:
    """R0: {"mean", "max"}: stories, the mean over all the pieces (each channel's mean weighted by its pieces) and the
    largest live count (None without pieces)."""
    pieces = sum(c["pieces"] for c in channels)
    return {"mean": sum(c["live_mean_stories"] * c["pieces"] for c in channels) / pieces if pieces else None,
            "max": max((c["live_max_stories"] for c in channels), default=None) if pieces else None}


def led_deficits(channels, margin_nats: float = MARGIN_NATS) -> dict:
    """R1: the largest deficit, nats, any newborn (a line of kind Silence or Rescue) reached before it led, over the
    newborns that led: their number, the maximum, the DEFICIT_QUANTILES, how many exceed margin_nats, and the same count
    and maximum for each kind."""
    values: dict = {kind: [] for kind in range(1, len(LINE_KINDS))}
    for c in channels:
        led = c["led_lines"]
        for kind, deficit in zip(led["kind"], led["largest_deficit_nats"]):
            if kind != 0:
                values[kind].append(deficit)
    everything = sorted(v for vs in values.values() for v in vs)
    by_kind = {LINE_KINDS[k]: {"newborns": len(vs), "max": max(vs, default=None)} for k, vs in values.items()}
    return {"newborns": len(everything), "max": everything[-1] if everything else None,
            "quantiles": {f"{q:g}": quantile(everything, q) for q in DEFICIT_QUANTILES},
            "above_margin": {"margin_nats": margin_nats, "newborns": sum(1 for v in everything if v > margin_nats)},
            "by_kind": by_kind}


def _ended_by(fates, end: int) -> dict:
    """R2 and R3: the lines that ended in `end` (the code of LINE_ENDS): their number, how many within their first
    element, how many never led, and the number by kind."""
    return {"lines": _count(fates, end=end), "in_first_element": _count(fates, end=end, first=1),
            "never_led": _count(fates, end=end, led=0),
            "by_kind": {name: _count(fates, kind=k, end=end) for k, name in enumerate(LINE_KINDS)}}


def reduce_group(channels, piece_s: float) -> dict:
    """All the reductions of the channels (summaries, as reduce_channel gives them) of a group or a run:
    channels, pieces, channel_seconds (s = pieces x piece_s), ceiling_pieces, ceiling_share (of the pieces), live_stories
    (R0), lines (the newborn lines: total, by kind, by end, by kind and end, how many led, how many ended in their first
    element), led_deficits_nats (R1), dropped_by_beam (R2), removed_by_ceiling (R3)."""
    channels = list(channels)
    fates = _pooled_fates(channels)
    pieces = sum(c["pieces"] for c in channels)
    ceiling_pieces = sum(c["ceiling_pieces"] for c in channels)
    return {"channels": len(channels), "pieces": pieces, "channel_seconds": pieces * piece_s,
            "ceiling_pieces": ceiling_pieces, "ceiling_share": ceiling_pieces / pieces if pieces else None,
            "live_stories": live_stories(channels),
            "lines": {"total": _count(fates),
                      "by_kind": {name: _count(fates, kind=k) for k, name in enumerate(LINE_KINDS)},
                      "by_end": {name: _count(fates, end=e) for e, name in enumerate(LINE_ENDS)},
                      "by_kind_and_end": {kn: {en: _count(fates, kind=k, end=e) for e, en in enumerate(LINE_ENDS)}
                                          for k, kn in enumerate(LINE_KINDS)},
                      "led": _count(fates, led=1), "ended_in_first_element": _count(fates, first=1)},
            "led_deficits_nats": led_deficits(channels),
            "dropped_by_beam": _ended_by(fates, LINE_ENDS.index("Beam")),
            "removed_by_ceiling": _ended_by(fates, LINE_ENDS.index("Ceiling"))}


# --- a run's statistics files ---

def _default_roots(out_dir) -> list[Path]:
    """The results roots of a suite folder: out/results and out/experiments/results, those that exist."""
    return [p for p in (Path(out_dir) / "results", Path(out_dir) / "experiments" / "results") if p.exists()]


def _channel_row(group: str, test_case: str, c: dict) -> dict:
    """One channel's numbers for joining with the run's per-signal results (see run_summary)."""
    beam, ceiling = LINE_ENDS.index("Beam"), LINE_ENDS.index("Ceiling")
    deficits = [d for kind, d in zip(c["led_lines"]["kind"], c["led_lines"]["largest_deficit_nats"]) if kind != 0]
    return {"group": group, "test_case": test_case, "index": c["label_index"], "pieces": c["pieces"],
            "ceiling_pieces": c["ceiling_pieces"], "live_mean_stories": c["live_mean_stories"],
            "live_max_stories": c["live_max_stories"],
            "dropped_by_beam_in_first_element": sum(c["fates"][k][beam][1][l] for k in range(len(LINE_KINDS))
                                                    for l in range(2)),
            "removed_by_ceiling": sum(c["fates"][k][ceiling][f][l] for k in range(len(LINE_KINDS)) for f in range(2)
                                      for l in range(2)),
            "led_deficit_max_nats": max(deficits, default=None)}


def run_summary(out_dir, run: str, only: str | None = None, results_dirs=None) -> dict:
    """The reductions of the statistics files of the run `run` (a results folder of kz4ap_synth.suites run --stats: a
    <test case>.stats.json for each test case), per group of the suite (the manifest's group, " (per station)" added for
    a recording's per-station scoring) and for all of them. only: a regular expression on the test cases' result names.

    Returns {"run", "only", "versions" (the format versions found), "piece_s" (s), "test_cases" (how many have
    statistics), "without_statistics" (the run's test cases that have a result but no statistics file), "groups":
    {group: reduce_group's result}, "all": reduce_group's result, "channels": one row for each channel: {"group",
    "test_case", "index" (the label's), "pieces", "ceiling_pieces", "live_mean_stories", "live_max_stories",
    "dropped_by_beam_in_first_element", "removed_by_ceiling", "led_deficit_max_nats"}, to join with the run's per-signal
    results}. Raises ValueError for a run with no statistics file, or whose files have different pieces."""
    from kz4ap_synth.suites import _scorings, front_end_dirs
    out_dir = Path(out_dir)
    roots = [Path(d) for d in (results_dirs if results_dirs is not None else _default_roots(out_dir))]
    found = front_end_dirs(out_dir, roots, [run])
    if not found:
        raise ValueError(f"no results folder named {run!r} in {[str(r) for r in roots]}")
    folder = found[0]
    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    by_group: dict = {}
    rows, without, versions, piece_s, test_cases = [], [], set(), None, 0
    for rec in manifest["recordings"]:
        for _labels, result, group in _scorings(rec):
            if only is not None and not re.search(only, result):
                continue
            path = folder / f"{result}.stats.json"
            if not path.exists():
                if (folder / f"{result}.json").exists():
                    without.append(result)
                continue
            stats = read_stats(path)
            this = stats["config"]["piece_s"]
            if piece_s not in (None, this):
                raise ValueError(f"{path}: pieces of {this} s, the other files' are {piece_s} s")
            piece_s = this
            versions.add(stats["version"])
            test_cases += 1
            for c in channel_summaries(stats):
                by_group.setdefault(group, []).append(c)
                rows.append(_channel_row(group, result, c))
    if not test_cases:
        raise ValueError(f"no statistics files (<test case>.stats.json) in {folder}"
                         + (f" for the test cases matching {only!r}" if only else "")
                         + "; the run needs kz4ap_synth.suites run --stats")
    return {"run": run, "only": only, "versions": sorted(versions), "piece_s": piece_s, "test_cases": test_cases,
            "without_statistics": without,
            "groups": {g: reduce_group(cs, piece_s) for g, cs in sorted(by_group.items())},
            "all": reduce_group([c for cs in by_group.values() for c in cs], piece_s), "channels": rows}


def _fmt(x, spec: str) -> str:
    return "—" if x is None else f"{x:{spec}}"


def format_markdown(summary: dict) -> str:
    """The run's summary as Markdown: per group and for all, the live stories, the share of pieces at which the ceiling
    decided, the newborn lines by kind and fate, the largest deficit before leading, and the lines the beam and the
    ceiling dropped (units in the headings)."""
    rows = list(summary["groups"].items()) + [("all", summary["all"])]
    margin = summary["all"]["led_deficits_nats"]["above_margin"]["margin_nats"]
    lines = [f"# Search statistics: {summary['run']}", "",
             f"The new decoder's search statistics of {summary['test_cases']} test cases (statistics file format "
             f"version{'s' if len(summary['versions']) > 1 else ''} {', '.join(map(str, summary['versions']))}; a "
             f"piece is {1000.0 * summary['piece_s']:g} ms of a channel). Live stories: the stories alive after the "
             "pruning of a piece, the mean over all the pieces and the largest number at any piece. The ceiling "
             "decides at a piece when it drops at least one story. A newborn line is a newborn story and its "
             "descendants; kind Start is the channel's first story, Silence a newborn from silence (an over starts), "
             "Rescue a rescue newborn. A line leads if one of its stories is the best after the pruning of some "
             "piece. Deficits are in nats relative to the best story of the piece.", ""]
    if summary["without_statistics"]:
        lines += ["Test cases with a result but no statistics file: "
                  + ", ".join(f"`{t}`" for t in summary["without_statistics"]) + ".", ""]
    lines += ["| group | channels | channel-seconds (s) | live stories, mean | live stories, maximum | "
              "pieces where the ceiling decided (share) |", "|---|---|---|---|---|---|"]
    for name, g in rows:
        lines.append(f"| {name} | {g['channels']} | {g['channel_seconds']:.1f} | {_fmt(g['live_stories']['mean'], '.1f')} | "
                     f"{_fmt(g['live_stories']['max'], '.0f')} | {_fmt(g['ceiling_share'], '.4f')} |")
    lines += ["", "## Newborn lines by kind and fate (counts)", "",
              "| group | kind | " + " | ".join(e.lower() for e in LINE_ENDS) + " | all |", "|---|---|" + "---|" * 5]
    for name, g in rows:
        for kind in LINE_KINDS:
            by_end = g["lines"]["by_kind_and_end"][kind]
            lines.append(f"| {name} | {kind} | " + " | ".join(str(by_end[e]) for e in LINE_ENDS)
                         + f" | {g['lines']['by_kind'][kind]} |")
    lines += ["", f"## Largest deficit a newborn reaches before it leads (nats; margin {margin:g} nats)", "",
              "Over the newborn lines (Silence and Rescue) that led; quantiles are values of the data (the smallest "
              "value with at least that share of the newborns at or below it).", "",
              "| group | newborns that led | maximum | 99.9th percentile | 99th percentile | 90th percentile | median | "
              f"above {margin:g} nats |", "|---|---|---|---|---|---|---|---|"]
    for name, g in rows:
        d = g["led_deficits_nats"]
        q = d["quantiles"]
        lines.append(f"| {name} | {d['newborns']} | {_fmt(d['max'], '.2f')} | {_fmt(q['0.999'], '.2f')} | "
                     f"{_fmt(q['0.99'], '.2f')} | {_fmt(q['0.9'], '.2f')} | {_fmt(q['0.5'], '.2f')} | "
                     f"{d['above_margin']['newborns']} |")
    lines += ["", "## Lines the beam dropped and the ceiling removed (counts)", "",
              "| group | dropped by the beam | within their first element | never led | removed by the ceiling | "
              "within their first element | never led |", "|---|---|---|---|---|---|---|"]
    for name, g in rows:
        b, c = g["dropped_by_beam"], g["removed_by_ceiling"]
        lines.append(f"| {name} | {b['lines']} | {b['in_first_element']} | {b['never_led']} | {c['lines']} | "
                     f"{c['in_first_element']} | {c['never_led']} |")
    return "\n".join(lines) + "\n"


def write_summary(out_dir, summary: dict) -> Path:
    """Writes experiments/search-stats-<run>.md (the tables) and .json (the whole summary, with the channels' rows) in
    out_dir; returns the .md's path."""
    path = Path(out_dir) / "experiments" / f"search-stats-{summary['run']}.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(format_markdown(summary), encoding="utf-8")
    path.with_suffix(".json").write_text(json.dumps(summary) + "\n", encoding="utf-8")
    return path
