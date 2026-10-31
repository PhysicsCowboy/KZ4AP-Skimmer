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
begins a new story at a letter start). A line "led" if one of its stories was the best after the pruning of some sample
(first_led_s is not null). A line ends in one of four ways: Alive (it has a story at the end of the recording), Merge
(merged into a better story of its state), Beam (more than beam_nats behind the best) or Ceiling (beyond the ceiling's
best stories). A sample is sample_interval_s = 2 ms of the channel (config.sample_interval_s); the live series is kept in windows of
round(0.1 s / sample_interval_s) samples (50 at the default); times are stream times, s; deficits are in nats, relative to the best
story of the sample.

reduce_channel is the reduction of a version 1 channel into the summary a version 2 channel holds, so it defines what the
bench keeps. The reductions over summaries are one function per run's question:

- R0 live_stories: the mean over the samples (the windows' means weighted by their samples) and the largest number of live
  stories; the share of samples at which the ceiling decided (dropped at least one story); the newborns' fates, as counts
  by kind and by end.
- R1 led_deficits: the largest deficit any newborn (a line of kind Silence or Rescue) reaches before it leads, per group:
  the maximum and the upper quantiles of the distribution over the newborns that led (a line that never led has no
  deficit "before it leads"), reported twice: the whole deficit, which includes the newborn's own starting cost (it is
  born that far behind the story it was born from), and the part beyond that starting cost. The design notes' rule is
  that the beam is never narrower than the largest starting cost of any newborn plus a margin of 10 nats (topic 5,
  item 4), so the margin (MARGIN_NATS) is compared with the part beyond the starting cost, and the count of newborns
  above it is of that part. The starting cost is derived from the run's configuration (starting_costs_nats): the cost of
  the birth and the prior of the speed, and for a rescue newborn the notes' simulated 4.5 nats to learn its level.
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
# nats: the margin of R1's rule, "the beam ... never narrower than the largest starting cost of any newborn plus a margin
# of 10 nats", raised to the largest part of a newborn's deficit beyond its starting cost if that is larger (the design
# notes, topic 5, item 4; the plan, section 6.1). Derived there from the swings of a story's score (5.6 nats for a story
# that has just ended an element against one still in it, 4.1 nats for the letter charged at a letter's start), rounded up
MARGIN_NATS = 10.0
# nats: what a rescue newborn pays to learn its level against the old story, which keeps what it has learned: "up to 4.5
# nats (simulated)", so a rescue newborn's starting cost is the largest. Source: the design notes, topic 5, item 4 ("The
# rule"), docs/design/2026-10-09-new-decoder-design-notes.md, which takes it from the level's simulation in topic 3 ("What
# it does"): 2.9 to 4.5 nats over the first letter, at S500 -2 to +20 dB, the old story's level known to 5-20%. Class:
# simulated (the upper end of that range, so not a measurement and not a configuration value); a newborn from silence
# pays nothing net, because it learns its level from marks that bring more evidence than the level costs.
RESCUE_LEVEL_NATS = 4.5
# The upper quantiles reported for R1's distribution (heuristic: the tail the rule looks at, beside the maximum)
DEFICIT_QUANTILES = (0.5, 0.9, 0.99, 0.999)
_SUMMARY_KEYS = ("track_id", "label_index", "samples", "ceiling_samples", "live_histogram", "live_mean_stories",
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
    if not isinstance(stats["config"], dict) or not stats["config"].get("sample_interval_s", 0) > 0:
        raise ValueError(f"{where}: config.sample_interval_s (s) is missing or not positive")


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


def window_samples(sample_interval_s: float) -> int:
    """Samples in a window of the live series: round(0.1 s / sample_interval_s), half up, at least 1 (search.cpp)."""
    return max(1, math.floor(STATS_WINDOW_S / sample_interval_s + 0.5))


# --- one channel ---

def reduce_channel(channel: dict, sample_interval_s: float) -> dict:
    """The summary of a version 1 channel (the object a version 2 file holds for it), keys:

    track_id, label_index                 as in the file
    samples, ceiling_samples                counts, as in the file
    live_histogram                        counts of samples by live stories after pruning, in bins of 10, as in the file
    live_mean_stories                     stories: the mean number of live stories after pruning over the channel's
                                          samples = the sum over the 0.1 s windows of (the window's mean x its samples)
                                          over the samples; a window holds window_samples(sample_interval_s) samples, the last one
                                          the rest (0 for a channel that searched nothing)
    live_max_stories                      stories: the largest live count at any sample, the largest of the windows'
                                          maxima (0 for no samples)
    lines                                 count: the newborn lines
    fates                                 counts of the lines by [kind][end][ended_in_first_element][led]: kind and end
                                          are the codes of LINE_KINDS and LINE_ENDS, the last two axes 0 (false) or 1
                                          (true); 3 x 4 x 2 x 2 integers
    led_lines                             the lines that ever led (first_led_s is not null), as columns of LINE_COLUMNS
                                          plus "line", the line's id (its position in the version 1 columns)

    Raises ValueError for a channel the windows of whose live series do not cover its samples, or whose codes lie outside
    the legends."""
    samples = int(channel["samples"])
    means, maxima = channel["live_mean_100ms"], channel["live_max_100ms"]
    if len(means) != len(maxima):
        raise ValueError(f"channel {channel['track_id']}: {len(means)} windows of means, {len(maxima)} of maxima")
    window = window_samples(sample_interval_s)
    windows = len(means)
    last = samples - window * (windows - 1)
    if (samples == 0) != (windows == 0) or (windows and not 1 <= last <= window):
        raise ValueError(f"channel {channel['track_id']}: {windows} windows of {window} samples do not cover its "
                         f"{samples} samples")
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
    return {"track_id": channel["track_id"], "label_index": channel["label_index"], "samples": samples,
            "ceiling_samples": int(channel["ceiling_samples"]), "live_histogram": list(channel["live_histogram"]),
            "live_mean_stories": total / samples if samples else 0.0,
            "live_max_stories": float(max(maxima)) if windows else 0.0, "lines": n, "fates": fates,
            "led_lines": {"line": led, **{name: [columns[name][i] for i in led] for name in LINE_COLUMNS}}}


def _is_count(value) -> bool:
    """Whether value is a count: a whole number that is not negative (a JSON true or false is not one)."""
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def check_summary_channel(c: dict) -> None:
    """Raises ValueError unless a version 2 channel agrees with itself, as the bench's writer guarantees (a file edited or
    assembled by hand is the case this guards, as the bench's reader does): every member is there; the histogram counts
    each sample in one bin; the fates are 3 x 4 x 2 x 2 counts that add up to the lines; the lines that led are columns of
    equal length with ids that ascend below the number of lines, codes inside the legends and a time they first led; and
    counted by kind, end and ended_in_first_element they are the fates' led counts."""
    where = f"channel {c.get('track_id')}"
    missing = [key for key in _SUMMARY_KEYS if key not in c]
    if missing:
        raise ValueError(f"{where}: {', '.join(missing)} missing from a version {SUMMARY_VERSION} channel")
    for name in ("samples", "ceiling_samples", "lines"):
        if not _is_count(c[name]):
            raise ValueError(f"{where}: {name} must be a count (a whole number, not negative)")
    histogram = c["live_histogram"]
    if not isinstance(histogram, list) or not all(_is_count(n) for n in histogram):
        raise ValueError(f"{where}: live_histogram must be a list of counts")
    if sum(histogram) != c["samples"]:
        raise ValueError(f"{where}: live_histogram counts {sum(histogram)} samples, not the {c['samples']} searched")
    fates = c["fates"]
    if not (isinstance(fates, list) and len(fates) == len(LINE_KINDS)
            and all(isinstance(by_end, list) and len(by_end) == len(LINE_ENDS)
                    and all(isinstance(by_first, list) and len(by_first) == 2
                            and all(isinstance(by_led, list) and len(by_led) == 2 and all(_is_count(n) for n in by_led)
                                    for by_led in by_first) for by_first in by_end) for by_end in fates)):
        raise ValueError(f"{where}: fates must be {len(LINE_KINDS)} x {len(LINE_ENDS)} x 2 x 2 counts "
                         f"(kind, end, ended_in_first_element, led)")
    total = sum(n for by_end in fates for by_first in by_end for by_led in by_first for n in by_led)
    if total != c["lines"]:
        raise ValueError(f"{where}: the fates add up to {total} lines, not {c['lines']}")
    led = c["led_lines"]
    columns = ("line", *LINE_COLUMNS)
    absent = [name for name in columns if not isinstance(led, dict) or name not in led]
    if absent:
        raise ValueError(f"{where}: led_lines lacks {', '.join(absent)}")
    n = len(led["line"])
    if any(len(led[name]) != n for name in columns):
        raise ValueError(f"{where}: the columns of led_lines have different lengths")
    tally: dict = {}
    previous = -1
    for i in range(n):
        line = led["line"][i]
        if not _is_count(line) or line <= previous or line >= c["lines"]:
            raise ValueError(f"{where}, led_lines: line {line!r} must be above the one before and below the "
                             f"{c['lines']} lines")
        previous = line
        kind, end, first = led["kind"][i], led["end"][i], led["ended_in_first_element"][i]
        if not (_is_count(kind) and kind < len(LINE_KINDS)):
            raise ValueError(f"{where}, led_lines: kind {kind!r} is not in the legend {LINE_KINDS}")
        if not (_is_count(end) and end < len(LINE_ENDS)):
            raise ValueError(f"{where}, led_lines: end {end!r} is not in the legend {LINE_ENDS}")
        if led["first_led_s"][i] is None:
            raise ValueError(f"{where}, led_lines: first_led_s is null, but only lines that led are kept here")
        if first not in (True, False):
            raise ValueError(f"{where}, led_lines: ended_in_first_element must be true or false")
        tally[(kind, end, int(first))] = tally.get((kind, end, int(first)), 0) + 1
    for k, kind in enumerate(LINE_KINDS):
        for e, end in enumerate(LINE_ENDS):
            for f in range(2):
                if tally.get((k, e, f), 0) != fates[k][e][f][1]:
                    raise ValueError(f"{where}: led_lines holds {tally.get((k, e, f), 0)} line(s) of kind {kind} that "
                                     f"ended {end} {'within' if f else 'after'} their first element, but "
                                     f"fates[{k}][{e}][{f}][1] counts {fates[k][e][f][1]}")


def channel_summaries(stats: dict) -> list[dict]:
    """The summaries of a statistics file's channels, whichever its version: a version 1 file's channels reduced
    (reduce_channel), a version 2 file's as the file holds them (checked: check_summary_channel)."""
    check_stats(stats)
    if stats["version"] == DETAIL_VERSION:
        return [reduce_channel(c, stats["config"]["sample_interval_s"]) for c in stats["channels"]]
    for c in stats["channels"]:
        check_summary_channel(c)
    return list(stats["channels"])


def starting_costs_nats(config: dict, genie_fist: bool = False) -> dict:
    """nats: what a newborn starts behind the story it was born from, by the kind of its line, derived from the run's
    configuration (a statistics file's "config"). The design notes' rule for the beam (topic 5, item 4) is "never
    narrower than the largest starting cost of any newborn plus a margin of 10 nats", and a newborn's starting cost is
    there the cost of its birth, plus the prior of its speed, plus the cost of learning its level from its first marks:

        Silence   ln(1/P) + ln(speed_count)                              P = over_rate_per_s x sample_interval_s: the probability
                                                                         that an over starts in one sample of silence
                                                                         (9.210 nats at the defaults); it learns its
                                                                         level from marks that bring more evidence than
                                                                         the level costs: nothing net
        Rescue    ln(1/epsilon) + ln(speed_count) + RESCUE_LEVEL_NATS    epsilon = rescue_epsilon: the probability per
                                                                         letter start that the sender begins a new
                                                                         story (6.908 nats at the defaults); it pays its
                                                                         level against the old story: 4.5 nats

    ln(speed_count) is the prior of the speed a newborn takes, flat over the grid (3.466 nats at 32 speeds); the genie
    fist gives every channel one speed (genie_fist: the base configuration does not show it, the run's result file
    does), so a newborn pays none; the genie does not give the level, so a rescue newborn still pays RESCUE_LEVEL_NATS.
    Derived from the configuration's values, with RESCUE_LEVEL_NATS simulated in the design notes: the line's record in
    the file holds no starting cost, and the engine's statistics do not keep one. It leaves out the cost of the
    newborn's first letter (ln 62 = 4.127 nats), which the notes put in the 10 nats of the margin, so it stays in the
    part of a deficit beyond the starting cost. Raises ValueError for a configuration without these values or that
    gives a probability outside 0 to 1."""
    values = {}
    for key in ("sample_interval_s", "over_rate_per_s", "rescue_epsilon", "speed_count"):
        if key not in config:
            raise ValueError(f"config: {key} is missing (it is needed for a newborn's starting cost)")
        values[key] = config[key]
    if not values["sample_interval_s"] > 0:
        raise ValueError(f"config.sample_interval_s must be positive (s), not {values['sample_interval_s']!r}")
    if not values["over_rate_per_s"] > 0:
        raise ValueError(f"config.over_rate_per_s must be positive (1/s), not {values['over_rate_per_s']!r}")
    over_start = values["over_rate_per_s"] * values["sample_interval_s"]
    if not over_start < 1:
        raise ValueError(f"config.over_rate_per_s x sample_interval_s = {over_start!r} must be a probability below 1")
    if not 0 < values["rescue_epsilon"] < 1:
        raise ValueError(f"config.rescue_epsilon must be a probability above 0 and below 1, not "
                         f"{values['rescue_epsilon']!r}")
    if not values["speed_count"] >= 1:
        raise ValueError(f"config.speed_count must be at least 1, not {values['speed_count']!r}")
    speed_prior = math.log(1 if genie_fist else values["speed_count"])
    return {"Silence": -math.log(over_start) + speed_prior,
            "Rescue": -math.log(values["rescue_epsilon"]) + speed_prior + RESCUE_LEVEL_NATS}


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
    """R0: {"mean", "max"}: stories, the mean over all the samples (each channel's mean weighted by its samples) and the
    largest live count (None without samples)."""
    samples = sum(c["samples"] for c in channels)
    return {"mean": sum(c["live_mean_stories"] * c["samples"] for c in channels) / samples if samples else None,
            "max": max((c["live_max_stories"] for c in channels), default=None) if samples else None}


def _distribution(values_by_kind: dict) -> dict:
    """{"max", "quantiles" (the DEFICIT_QUANTILES), "by_kind" ({kind name: {"newborns", "max"}})} of values, nats, by the
    code of the kind of line they belong to (None for no values)."""
    everything = sorted(v for vs in values_by_kind.values() for v in vs)
    return {"max": everything[-1] if everything else None,
            "quantiles": {f"{q:g}": quantile(everything, q) for q in DEFICIT_QUANTILES},
            "by_kind": {LINE_KINDS[k]: {"newborns": len(vs), "max": max(vs, default=None)}
                        for k, vs in values_by_kind.items()}}


def led_deficits(channels, costs_nats: dict, margin_nats: float = MARGIN_NATS) -> dict:
    """R1: the largest deficit, nats, any newborn (a line of kind Silence or Rescue) reached before it led, over the
    newborns that led, in two forms:

        "deficit"               the whole deficit: it includes the newborn's own starting cost
        "beyond_starting_cost"  the deficit less the newborn's own starting cost (costs_nats[its kind's name], from
                                starting_costs_nats), so a newborn is compared with its own kind's cost; negative where
                                it never trailed by as much as its starting cost (kept, not cut at zero)

    each as {"max", "quantiles" (the DEFICIT_QUANTILES), "by_kind": {kind name: {"newborns", "max"}}}; the second also
    "above_margin": {"margin_nats", "newborns"}, how many newborns have more than margin_nats beyond their starting cost
    (the design notes' rule is a margin beyond the starting cost, topic 5, item 4). The result also has "newborns", their
    number, and "starting_cost_nats", the costs used."""
    whole: dict = {kind: [] for kind in range(1, len(LINE_KINDS))}
    beyond: dict = {kind: [] for kind in range(1, len(LINE_KINDS))}
    for c in channels:
        led = c["led_lines"]
        for kind, deficit in zip(led["kind"], led["largest_deficit_nats"]):
            if kind != 0:
                whole[kind].append(deficit)
                beyond[kind].append(deficit - costs_nats[LINE_KINDS[kind]])
    return {"newborns": sum(len(vs) for vs in whole.values()), "starting_cost_nats": dict(costs_nats),
            "deficit": _distribution(whole),
            "beyond_starting_cost": {**_distribution(beyond),
                                     "above_margin": {"margin_nats": margin_nats,
                                                      "newborns": sum(1 for vs in beyond.values() for v in vs
                                                                      if v > margin_nats)}}}


def _ended_by(fates, end: int) -> dict:
    """R2 and R3: the lines that ended in `end` (the code of LINE_ENDS): their number, how many within their first
    element, how many never led, and the number by kind."""
    return {"lines": _count(fates, end=end), "in_first_element": _count(fates, end=end, first=1),
            "never_led": _count(fates, end=end, led=0),
            "by_kind": {name: _count(fates, kind=k, end=end) for k, name in enumerate(LINE_KINDS)}}


def reduce_group(channels, sample_interval_s: float, costs_nats: dict) -> dict:
    """All the reductions of the channels (summaries, as reduce_channel gives them) of a group or a run:
    channels, samples, channel_seconds (s = samples x sample_interval_s), ceiling_samples, ceiling_share (of the samples), live_stories
    (R0), lines (the newborn lines: total, by kind, by end, by kind and end, how many led, how many ended in their first
    element), led_deficits_nats (R1, led_deficits: costs_nats are the newborns' starting costs by kind name,
    starting_costs_nats), dropped_by_beam (R2), removed_by_ceiling (R3)."""
    channels = list(channels)
    fates = _pooled_fates(channels)
    samples = sum(c["samples"] for c in channels)
    ceiling_samples = sum(c["ceiling_samples"] for c in channels)
    return {"channels": len(channels), "samples": samples, "channel_seconds": samples * sample_interval_s,
            "ceiling_samples": ceiling_samples, "ceiling_share": ceiling_samples / samples if samples else None,
            "live_stories": live_stories(channels),
            "lines": {"total": _count(fates),
                      "by_kind": {name: _count(fates, kind=k) for k, name in enumerate(LINE_KINDS)},
                      "by_end": {name: _count(fates, end=e) for e, name in enumerate(LINE_ENDS)},
                      "by_kind_and_end": {kn: {en: _count(fates, kind=k, end=e) for e, en in enumerate(LINE_ENDS)}
                                          for k, kn in enumerate(LINE_KINDS)},
                      "led": _count(fates, led=1), "ended_in_first_element": _count(fates, first=1)},
            "led_deficits_nats": led_deficits(channels, costs_nats),
            "dropped_by_beam": _ended_by(fates, LINE_ENDS.index("Beam")),
            "removed_by_ceiling": _ended_by(fates, LINE_ENDS.index("Ceiling"))}


# --- a run's statistics files ---

def _default_roots(out_dir) -> list[Path]:
    """The results roots of a suite folder: out/results and out/experiments/results, those that exist."""
    return [p for p in (Path(out_dir) / "results", Path(out_dir) / "experiments" / "results") if p.exists()]


def _channel_row(group: str, test_case: str, c: dict, costs_nats: dict) -> dict:
    """One channel's numbers for joining with the run's per-signal results (see run_summary)."""
    beam, ceiling = LINE_ENDS.index("Beam"), LINE_ENDS.index("Ceiling")
    newborns = [(d, d - costs_nats[LINE_KINDS[kind]])
                for kind, d in zip(c["led_lines"]["kind"], c["led_lines"]["largest_deficit_nats"]) if kind != 0]
    return {"group": group, "test_case": test_case, "index": c["label_index"], "samples": c["samples"],
            "ceiling_samples": c["ceiling_samples"], "live_mean_stories": c["live_mean_stories"],
            "live_max_stories": c["live_max_stories"],
            "dropped_by_beam_in_first_element": sum(c["fates"][k][beam][1][l] for k in range(len(LINE_KINDS))
                                                    for l in range(2)),
            "removed_by_ceiling": sum(c["fates"][k][ceiling][f][l] for k in range(len(LINE_KINDS)) for f in range(2)
                                      for l in range(2)),
            "led_deficit_max_nats": max((d for d, _ in newborns), default=None),
            "led_beyond_start_max_nats": max((b for _, b in newborns), default=None)}


def _genie_kinds(path) -> list:
    """The genie kinds a test case's bench run was given ("fist", "frequency", "fading"), from the "genie" member of its
    result file (the bench writes it only for a run given a genie); none if the file is not there or has no such
    member. Raises ValueError for a result file that is not JSON."""
    path = Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return []
    try:
        result = json.loads(text)
    except json.JSONDecodeError as error:
        raise ValueError(f"{path}: not JSON ({error})") from error
    genie = result.get("genie") if isinstance(result, dict) else None
    return list(genie.get("kinds", [])) if isinstance(genie, dict) else []


def run_summary(out_dir, run: str, only: str | None = None, results_dirs=None) -> dict:
    """The reductions of the statistics files of the run `run` (a results folder of kz4ap_synth.suites run --stats: a
    <test case>.stats.json for each test case), per group of the suite (the manifest's group, " (per station)" added for
    a recording's per-station scoring) and for all of them. only: a regular expression on the test cases' result names.

    Returns {"run", "only", "versions" (the format versions found), "sample_interval_s" (s), "starting_cost_nats" (a newborn's
    starting cost by kind name, derived once for the run: starting_costs_nats from the files' configuration, with the
    genie fist of the test cases' result files), "genie_fist" (whether the run gave the genie fist), "test_cases" (how
    many have statistics), "without_statistics" (the run's test cases that have a result but no statistics file),
    "groups": {group: reduce_group's result}, "all": reduce_group's result, "channels": one row for each channel:
    {"group", "test_case", "index" (the label's), "samples", "ceiling_samples", "live_mean_stories", "live_max_stories",
    "dropped_by_beam_in_first_element", "removed_by_ceiling", "led_deficit_max_nats", "led_beyond_start_max_nats" (the
    largest part of a led newborn's deficit beyond its starting cost, nats)}, to join with the run's per-signal
    results}. Raises ValueError for a run with no statistics file, or whose files have different samples or different
    starting costs (another over rate, rescue probability, speed count or genie fist: not one run)."""
    from kz4ap_synth.suites import _scorings, front_end_dirs
    out_dir = Path(out_dir)
    roots = [Path(d) for d in (results_dirs if results_dirs is not None else _default_roots(out_dir))]
    found = front_end_dirs(out_dir, roots, [run])
    if not found:
        raise ValueError(f"no results folder named {run!r} in {[str(r) for r in roots]}")
    folder = found[0]
    manifest = json.loads((out_dir / "manifest.json").read_text(encoding="utf-8"))
    by_group: dict = {}
    rows, without, versions, sample_interval_s, test_cases = [], [], set(), None, 0
    costs, genie_fist = None, False
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
            this = stats["config"]["sample_interval_s"]
            if sample_interval_s not in (None, this):
                raise ValueError(f"{path}: samples of {this} s, the other files' are {sample_interval_s} s")
            sample_interval_s = this
            # a newborn's starting cost comes from the configuration, and the genie fist from the test case's result
            # file (the statistics file's configuration is the run's base one, without the genie): one value for a run
            fist = "fist" in _genie_kinds(folder / f"{result}.json")
            these = starting_costs_nats(stats["config"], fist)
            if costs is None:
                costs, genie_fist = these, fist
            elif these != costs:
                raise ValueError(f"{path}: the starting costs of its newborns ({_shown(these)} nats) differ from the "
                                 f"other files' ({_shown(costs)} nats): the files come from runs with another over "
                                 "rate, rescue probability, speed count or genie fist, which are not one run")
            versions.add(stats["version"])
            test_cases += 1
            for c in channel_summaries(stats):
                by_group.setdefault(group, []).append(c)
                rows.append(_channel_row(group, result, c, costs))
    if not test_cases:
        raise ValueError(f"no statistics files (<test case>.stats.json) in {folder}"
                         + (f" for the test cases matching {only!r}" if only else "")
                         + "; the run needs kz4ap_synth.suites run --stats")
    return {"run": run, "only": only, "versions": sorted(versions), "sample_interval_s": sample_interval_s,
            "starting_cost_nats": costs, "genie_fist": genie_fist, "test_cases": test_cases,
            "without_statistics": without,
            "groups": {g: reduce_group(cs, sample_interval_s, costs) for g, cs in sorted(by_group.items())},
            "all": reduce_group([c for cs in by_group.values() for c in cs], sample_interval_s, costs), "channels": rows}


def _shown(costs_nats: dict) -> str:
    return ", ".join(f"{kind} {cost:.3f}" for kind, cost in costs_nats.items())


def _fmt(x, spec: str) -> str:
    return "—" if x is None else f"{x:{spec}}"


def r1_line(summary: dict) -> str:
    """R1 for the whole run in one line (the terminal's): the newborns that led, the largest deficit, the largest part of
    a deficit beyond the newborn's own starting cost, and how many newborns are above the margin in that part."""
    d = summary["all"]["led_deficits_nats"]
    if not d["newborns"]:
        return "R1: no newborn led"
    beyond, costs = d["beyond_starting_cost"], d["starting_cost_nats"]
    return (f"R1: {d['newborns']} newborns led; the largest deficit {d['deficit']['max']:.2f} nats; the largest part "
            f"beyond a newborn's own starting cost ({costs['Silence']:.2f} nats from silence, {costs['Rescue']:.2f} "
            f"nats as a rescue) {beyond['max']:.2f} nats; {beyond['above_margin']['newborns']} newborns above the "
            f"{beyond['above_margin']['margin_nats']:g}-nat margin")


def format_markdown(summary: dict) -> str:
    """The run's summary as Markdown: per group and for all, the live stories, the share of samples at which the ceiling
    decided, the newborn lines by kind and fate, the largest deficit before leading, and the lines the beam and the
    ceiling dropped (units in the headings)."""
    rows = list(summary["groups"].items()) + [("all", summary["all"])]
    margin = summary["all"]["led_deficits_nats"]["beyond_starting_cost"]["above_margin"]["margin_nats"]
    costs = summary["all"]["led_deficits_nats"]["starting_cost_nats"]
    lines = [f"# Search statistics: {summary['run']}", "",
             f"The new decoder's search statistics of {summary['test_cases']} test cases (statistics file format "
             f"version{'s' if len(summary['versions']) > 1 else ''} {', '.join(map(str, summary['versions']))}; a "
             f"sample is {1000.0 * summary['sample_interval_s']:g} ms of a channel). Live stories: the stories alive after the "
             "pruning of a sample, the mean over all the samples and the largest number at any sample. The ceiling "
             "decides at a sample when it drops at least one story. A newborn line is a newborn story and its "
             "descendants; kind Start is the channel's first story, Silence a newborn from silence (an over starts), "
             "Rescue a rescue newborn. A line leads if one of its stories is the best after the pruning of some "
             "sample. Deficits are in nats relative to the best story of the sample.", ""]
    if summary["without_statistics"]:
        lines += ["Test cases with a result but no statistics file: "
                  + ", ".join(f"`{t}`" for t in summary["without_statistics"]) + ".", ""]
    lines += ["| group | channels | channel-seconds (s) | live stories, mean | live stories, maximum | "
              "samples where the ceiling decided (share) |", "|---|---|---|---|---|---|"]
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
    lines += ["", "## Largest deficit a newborn reaches before it leads (nats)", "",
              "Over the newborn lines (Silence and Rescue) that led. A newborn's deficit is how far its best story is "
              "behind the best story of the sample, at most, from its birth until it first leads; it includes the "
              "newborn's own starting cost, because a newborn is born that far behind the story it was born from. "
              "Quantiles are values of the data (the smallest value with at least that share of the newborns at or "
              "below it).", "",
              "| group | newborns that led | maximum | 99.9th percentile | 99th percentile | 90th percentile | median |",
              "|---|---|---|---|---|---|---|"]
    for name, g in rows:
        d = g["led_deficits_nats"]
        q = d["deficit"]["quantiles"]
        lines.append(f"| {name} | {d['newborns']} | {_fmt(d['deficit']['max'], '.2f')} | {_fmt(q['0.999'], '.2f')} | "
                     f"{_fmt(q['0.99'], '.2f')} | {_fmt(q['0.9'], '.2f')} | {_fmt(q['0.5'], '.2f')} |")
    lines += ["", f"## The part of that deficit beyond the newborn's starting cost (nats; margin {margin:g} nats)", "",
              "The decoder's beam is never narrower than the largest starting cost of any newborn plus a margin of "
              f"{margin:g} nats (design notes, topic 5, item 4), so the margin is compared with the part of a "
              "newborn's deficit beyond its own starting cost, not with the whole deficit. The starting cost is "
              f"derived from the run's configuration: ln(1/P) + ln(speed count) = {costs['Silence']:.2f} nats for a "
              "newborn from silence (P = over_rate_per_s x sample_interval_s, the probability that an over starts in a sample; "
              "it learns its level from marks that bring more evidence than the level costs, so that adds nothing) "
              f"and ln(1/ε) + ln(speed count) + {RESCUE_LEVEL_NATS:g} = {costs['Rescue']:.2f} nats for a rescue "
              "newborn (ε = rescue_epsilon, the probability per letter start that the sender begins a new story; "
              f"{RESCUE_LEVEL_NATS:g} nats is the design notes' simulated bound on the cost of learning its level "
              "against the old story), where ln(speed count) is the prior of the speed a newborn takes (none for a "
              "genie fist, which gives a channel one speed). It leaves out the cost of the newborn's first letter "
              "(ln 62 = 4.13 nats in the first prototype), which the margin includes. A part below zero: the "
              "newborn never trailed by as much as its starting cost.", "",
              "| group | newborns that led | maximum | 99.9th percentile | 99th percentile | 90th percentile | median | "
              f"above {margin:g} nats | maximum, Silence | maximum, Rescue |", "|---|---|---|---|---|---|---|---|---|---|"]
    for name, g in rows:
        d = g["led_deficits_nats"]
        beyond = d["beyond_starting_cost"]
        q = beyond["quantiles"]
        lines.append(f"| {name} | {d['newborns']} | {_fmt(beyond['max'], '.2f')} | {_fmt(q['0.999'], '.2f')} | "
                     f"{_fmt(q['0.99'], '.2f')} | {_fmt(q['0.9'], '.2f')} | {_fmt(q['0.5'], '.2f')} | "
                     f"{beyond['above_margin']['newborns']} | {_fmt(beyond['by_kind']['Silence']['max'], '.2f')} | "
                     f"{_fmt(beyond['by_kind']['Rescue']['max'], '.2f')} |")
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
