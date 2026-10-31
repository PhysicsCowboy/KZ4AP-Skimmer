"""The reductions of the new decoder's search statistics (kz4ap_proto.search_stats), on a small real file.

The fixtures in data/search-stats/ are real: tiny.v1.stats.json is the statistics file (format version 1, every newborn
line) that kz4ap-bench --oracle --decoder viterbi --stats FILE --stats-detail wrote, at the defaults, for a 1.05 s
recording at 48 kHz of two oracle channels: "TEST" at 40 WPM, S500 +12 dB, 300 Hz above the span's center, and a
station at S500 -60 dB (far below any decodable level: a channel of noise), both starting at 0.1 s. The recording is
kz4ap_synth.generate.generate([SignalSpec("TEST", 300.0, 40.0, 12.0, 0.1), SignalSpec("EE", -900.0, 30.0, -60.0, 0.1)],
48000, 1.05, seed=7) with its labels. tiny.v2.stats.json is the summary (format version 2) that the same bench wrote
from the same recording and settings without --stats-detail. (The data of the version 1 file are those that the bench
before the summary mode wrote, commit ae8f65d; only the legends of the file's "fields" were reworded.)
"""

import json
import math
from pathlib import Path

import pytest

from kz4ap_proto import search_stats

DATA = Path(__file__).parent / "data" / "search-stats"
V1 = DATA / "tiny.v1.stats.json"
# nats: a newborn's starting cost at the fixtures' configuration, the defaults, written out here and not computed by the
# code under test: ln(1/P) + ln(32) with P = 0.05 /s x 2 ms = 1e-4 (9.2103 + 3.4657) for a newborn from silence, which
# pays nothing net for its level, and ln(1/epsilon) + ln(32) + 4.5 with epsilon = 1e-3 (6.9078 + 3.4657 + 4.5) for a
# rescue newborn, which pays up to 4.5 nats (simulated, the design notes) to learn its level against the old story
COSTS = {"Silence": 12.676076274775909, "Rescue": 14.873491181781864}


def v1_file():
    return json.loads(V1.read_text(encoding="utf-8"))


def naive_fates(channel):
    """The counts of the lines of a version 1 channel by (kind, end, ended_in_first_element, led), by a plain loop."""
    columns = channel["lines"]
    counts = {}
    for i in range(len(columns["kind"])):
        key = (columns["kind"][i], columns["end"][i], int(columns["ended_in_first_element"][i]),
               int(columns["first_led_s"][i] is not None))
        counts[key] = counts.get(key, 0) + 1
    return counts


def line_channel(lines, samples=100, ceiling_samples=0, means=None, maxima=None, track_id=1):
    """A version 1 channel from `lines`, a list of (kind, end, first_element, led_at, deficit) tuples: a hand-made
    channel for the cases the real file does not hold."""
    cols = {"birth_s": [], "kind": [], "first_led_s": [], "largest_deficit_nats": [], "end": [], "end_s": [],
            "ended_in_first_element": []}
    for k, (kind, end, first, led_at, deficit) in enumerate(lines):
        cols["birth_s"].append(0.002 * k)
        cols["kind"].append(kind)
        cols["first_led_s"].append(led_at)
        cols["largest_deficit_nats"].append(deficit)
        cols["end"].append(end)
        cols["end_s"].append(None if end == 0 else 0.002 * k + 0.01)
        cols["ended_in_first_element"].append(first)
    windows = math.ceil(samples / 50)
    return {"track_id": track_id, "label_index": track_id - 1, "samples": samples, "ceiling_samples": ceiling_samples,
            "live_histogram": [samples], "live_mean_100ms": [3.0] * windows if means is None else means,
            "live_max_100ms": [5.0] * windows if maxima is None else maxima, "lines": cols}


# --- the real file ---

def test_the_real_file_reads_as_format_version_1():
    stats = search_stats.read_stats(V1)
    assert stats["format"] == "kz4ap-viterbi-search-stats" and stats["version"] == 1
    assert [c["track_id"] for c in stats["channels"]] == [1, 2]
    assert stats["config"]["sample_interval_s"] == 0.002 and stats["config"]["beam_nats"] == 40.0


def test_a_channel_reduces_to_counts_the_live_stories_and_the_lines_that_led():
    stats = v1_file()
    sample_interval_s = stats["config"]["sample_interval_s"]
    for channel in stats["channels"]:
        s = search_stats.reduce_channel(channel, sample_interval_s)
        columns = channel["lines"]
        assert (s["track_id"], s["label_index"]) == (channel["track_id"], channel["label_index"])
        assert (s["samples"], s["ceiling_samples"]) == (523, channel["ceiling_samples"])
        assert s["live_histogram"] == channel["live_histogram"]
        assert s["lines"] == len(columns["kind"]) == 1045
        # the fates: a count of the lines for each (kind, end, ended in its first element, led), 3 x 4 x 2 x 2
        naive = naive_fates(channel)
        assert sum(naive.values()) == s["lines"]
        for kind in range(3):
            for end in range(4):
                for first in range(2):
                    for led in range(2):
                        assert s["fates"][kind][end][first][led] == naive.get((kind, end, first, led), 0)
        # the lines that led: their ids and every column of theirs, nothing of the others
        led = [i for i, t in enumerate(columns["first_led_s"]) if t is not None]
        assert s["led_lines"]["line"] == led
        for name in search_stats.LINE_COLUMNS:
            assert s["led_lines"][name] == [columns[name][i] for i in led]
        # live stories: the largest of the windows' maxima, and the mean over the samples, the windows weighted by their
        # samples (10 windows of 50 samples and a last one of 23)
        assert s["live_max_stories"] == max(channel["live_max_100ms"]) == 1700.0
        means = channel["live_mean_100ms"]
        assert len(means) == 11
        expected = (sum(50 * m for m in means[:10]) + 23 * means[10]) / 523
        assert s["live_mean_stories"] == pytest.approx(expected, rel=1e-12)


def test_the_real_files_two_channels_are_a_station_and_noise():
    # facts of the fixture: the station's channel has two lines that led (the first story, and the newborn from silence
    # at 0.112 s that reads TEST); the noise channel only the first story
    channels = [search_stats.reduce_channel(c, 0.002) for c in v1_file()["channels"]]
    assert channels[0]["led_lines"]["line"] == [0, 89] and channels[1]["led_lines"]["line"] == [0]
    assert channels[0]["led_lines"]["kind"] == [0, 1] and channels[0]["led_lines"]["first_led_s"][1] == 0.12
    assert channels[0]["led_lines"]["largest_deficit_nats"][1] == pytest.approx(17.382956934607172)
    assert (channels[0]["ceiling_samples"], channels[1]["ceiling_samples"]) == (362, 501)


def test_the_group_reductions_of_the_real_file():
    stats = v1_file()
    sample_interval_s = stats["config"]["sample_interval_s"]
    channels = [search_stats.reduce_channel(c, sample_interval_s) for c in stats["channels"]]
    g = search_stats.reduce_group(channels, sample_interval_s, COSTS)
    assert (g["channels"], g["samples"]) == (2, 1046) and g["channel_seconds"] == pytest.approx(2.092)
    # R0: live stories (mean over the samples, maximum), the share of samples where the ceiling decided
    assert g["live_stories"]["max"] == 1700.0
    assert g["live_stories"]["mean"] == pytest.approx(
        (channels[0]["live_mean_stories"] * 523 + channels[1]["live_mean_stories"] * 523) / 1046)
    assert g["ceiling_samples"] == 362 + 501 and g["ceiling_share"] == pytest.approx(863 / 1046)
    # R0: the newborns' fates by kind and by end
    both = [naive_fates(c) for c in stats["channels"]]

    def count(kind=None, end=None, first=None, led=None):
        return sum(n for counts in both for (k, e, f, l), n in counts.items()
                   if kind in (None, k) and end in (None, e) and first in (None, f) and led in (None, l))

    kinds, ends = search_stats.LINE_KINDS, search_stats.LINE_ENDS
    assert g["lines"]["total"] == 2090 == count()
    assert g["lines"]["by_kind"] == {name: count(kind=k) for k, name in enumerate(kinds)}
    assert g["lines"]["by_end"] == {name: count(end=e) for e, name in enumerate(ends)}
    assert g["lines"]["by_kind_and_end"] == {kn: {en: count(kind=k, end=e) for e, en in enumerate(ends)}
                                             for k, kn in enumerate(kinds)}
    assert g["lines"]["led"] == count(led=1) == 3 and g["lines"]["ended_in_first_element"] == count(first=1)
    # R1: the largest deficit a newborn reaches before it leads: the newborns (not the first story) that led, the whole
    # deficit (17.38 nats for the newborn from silence that reads TEST) and the part beyond its starting cost (12.68
    # nats for a newborn from silence), which is what the 10-nat margin is compared with
    d = g["led_deficits_nats"]
    assert d["newborns"] == 1 and d["starting_cost_nats"] == COSTS
    whole, beyond = d["deficit"], d["beyond_starting_cost"]
    assert whole["max"] == pytest.approx(17.382956934607172)
    assert whole["quantiles"] == {"0.5": whole["max"], "0.9": whole["max"], "0.99": whole["max"], "0.999": whole["max"]}
    assert whole["by_kind"]["Silence"] == {"newborns": 1, "max": whole["max"]}
    assert whole["by_kind"]["Rescue"] == {"newborns": 0, "max": None}
    assert beyond["max"] == pytest.approx(17.382956934607172 - COSTS["Silence"])
    assert beyond["max"] == pytest.approx(4.706880659831263)
    assert beyond["quantiles"] == {"0.5": beyond["max"], "0.9": beyond["max"], "0.99": beyond["max"],
                                   "0.999": beyond["max"]}
    assert beyond["by_kind"]["Silence"] == {"newborns": 1, "max": beyond["max"]}
    assert beyond["by_kind"]["Rescue"] == {"newborns": 0, "max": None}
    assert beyond["above_margin"] == {"margin_nats": 10.0, "newborns": 0}    # not 1: 17.38 nats is above 10, 4.71 is not
    # R2: the newborns the beam drops within their first element; R3: those the ceiling removes
    beam = g["dropped_by_beam"]
    assert beam["lines"] == count(end=2) and beam["in_first_element"] == count(end=2, first=1)
    assert beam["never_led"] == count(end=2, led=0) and beam["by_kind"] == {
        name: count(kind=k, end=2) for k, name in enumerate(kinds)}
    ceiling = g["removed_by_ceiling"]
    assert ceiling["lines"] == count(end=3) == 100 + 155 + 394
    assert ceiling["in_first_element"] == count(end=3, first=1) and ceiling["never_led"] == ceiling["lines"]
    assert ceiling["by_kind"] == {"Start": 0, "Silence": 100 + 394, "Rescue": 155}


# --- a newborn's starting cost: derived from the configuration, by the kind of line ---

def test_a_newborns_starting_cost_is_derived_from_the_runs_configuration_by_its_kind():
    config = search_stats.read_stats(V1)["config"]   # the defaults: P = 0.05 /s x 2 ms, epsilon = 1e-3, 32 speeds
    assert (config["over_rate_per_s"], config["sample_interval_s"], config["rescue_epsilon"], config["speed_count"]) == (
        0.05, 0.002, 1e-3, 32)
    costs = search_stats.starting_costs_nats(config)
    assert list(costs) == ["Silence", "Rescue"]
    assert costs == pytest.approx(COSTS, abs=1e-12)
    # the terms: from silence ln(1/P) = 9.2103 nats, as a rescue ln(1/epsilon) = 6.9078 nats, and the prior of the speed
    # a newborn takes, ln(32) = 3.4657 nats (a flat share of the speed grid); and the cost of learning its level: none
    # net for a newborn from silence, the design notes' 4.5 nats (simulated, an upper bound) for a rescue newborn
    assert search_stats.RESCUE_LEVEL_NATS == 4.5
    assert costs["Silence"] == pytest.approx(math.log(1e4) + math.log(32))
    assert costs["Rescue"] == pytest.approx(math.log(1e3) + math.log(32) + 4.5)
    # each term follows its configuration value
    assert search_stats.starting_costs_nats({**config, "over_rate_per_s": 0.5})["Silence"] == pytest.approx(
        math.log(1e3) + math.log(32))
    assert search_stats.starting_costs_nats({**config, "sample_interval_s": 0.004})["Silence"] == pytest.approx(
        math.log(1 / 2e-4) + math.log(32))
    assert search_stats.starting_costs_nats({**config, "rescue_epsilon": 1e-2})["Rescue"] == pytest.approx(
        math.log(100) + math.log(32) + 4.5)
    finer = search_stats.starting_costs_nats({**config, "speed_count": 61})
    assert finer["Silence"] == pytest.approx(math.log(1e4) + math.log(61))
    assert finer["Rescue"] == pytest.approx(math.log(1e3) + math.log(61) + 4.5)
    # the genie fist gives each channel one speed: no speed prior is paid; the level is still learned
    assert search_stats.starting_costs_nats(config, genie_fist=True) == pytest.approx(
        {"Silence": math.log(1e4), "Rescue": math.log(1e3) + 4.5})
    # a configuration that gives no cost is refused, naming what is wrong
    for bad, why in (({"over_rate_per_s": 0.0}, "over_rate_per_s"), ({"over_rate_per_s": 600.0}, "over_rate_per_s"),
                     ({"rescue_epsilon": 1.0}, "rescue_epsilon"), ({"rescue_epsilon": 0.0}, "rescue_epsilon"),
                     ({"speed_count": 0}, "speed_count"), ({"sample_interval_s": -0.002}, "sample_interval_s")):
        with pytest.raises(ValueError, match=why):
            search_stats.starting_costs_nats({**config, **bad})
    for key in ("over_rate_per_s", "rescue_epsilon", "speed_count", "sample_interval_s"):
        without = {k: v for k, v in config.items() if k != key}
        with pytest.raises(ValueError, match=key):
            search_stats.starting_costs_nats(without)


# --- the definitions, on hand-made channels ---

def test_the_window_of_the_live_series_is_a_tenth_of_a_second_in_samples():
    assert search_stats.window_samples(0.002) == 50
    assert search_stats.window_samples(0.004) == 25
    assert search_stats.window_samples(0.003) == 33     # 33.3: rounded
    assert search_stats.window_samples(0.0035) == 29    # 28.57
    assert search_stats.window_samples(0.5) == 1        # never below one sample


def test_the_mean_live_stories_weights_each_window_by_its_samples():
    # 120 samples: windows of 50, 50 and 20 samples
    channel = line_channel([(0, 0, 0, 0.002, 0.0)], samples=120, means=[10.0, 20.0, 40.0], maxima=[12.0, 25.0, 41.0])
    s = search_stats.reduce_channel(channel, 0.002)
    assert s["live_mean_stories"] == pytest.approx((50 * 10.0 + 50 * 20.0 + 20 * 40.0) / 120)
    assert s["live_max_stories"] == 41.0
    # a channel that searched nothing
    empty = search_stats.reduce_channel(line_channel([], samples=0, means=[], maxima=[]), 0.002)
    assert (empty["samples"], empty["lines"], empty["live_mean_stories"], empty["live_max_stories"]) == (0, 0, 0.0, 0.0)
    assert empty["led_lines"]["line"] == [] and empty["fates"][0][0] == [[0, 0], [0, 0]]
    # windows that do not cover the samples are refused, not averaged wrongly
    with pytest.raises(ValueError, match="windows"):
        search_stats.reduce_channel(line_channel([], samples=120, means=[1.0, 1.0], maxima=[1.0, 1.0]), 0.002)
    with pytest.raises(ValueError, match="windows"):
        search_stats.reduce_channel(line_channel([], samples=120, means=[1.0] * 4, maxima=[1.0] * 4), 0.002)


def test_the_lines_that_led_keep_their_kind_end_and_first_element_and_the_fates_count_everything():
    lines = [(0, 1, 0, 0.002, 0.0),     # the first story: led at once, merged later
             (1, 0, 0, 0.040, 25.0),    # a newborn from silence that led and is alive
             (2, 2, 1, 0.050, 17.0),    # a rescue newborn that led, then was dropped by the beam in its first element
             (1, 2, 1, None, 41.0),     # a newborn the beam dropped in its first element, never led
             (2, 3, 0, None, 9.5),      # a rescue newborn the ceiling removed after its first element, never led
             (1, 3, 1, None, 3.0)]      # a newborn the ceiling removed in its first element
    s = search_stats.reduce_channel(line_channel(lines), 0.002)
    assert s["led_lines"]["line"] == [0, 1, 2]
    assert s["led_lines"]["kind"] == [0, 1, 2] and s["led_lines"]["end"] == [1, 0, 2]
    assert s["led_lines"]["ended_in_first_element"] == [0, 0, 1]
    assert s["led_lines"]["end_s"][1] is None            # alive at the end: no end time
    assert s["led_lines"]["largest_deficit_nats"] == [0.0, 25.0, 17.0]
    assert s["fates"][2][2][1][1] == 1 and s["fates"][1][2][1][0] == 1 and s["fates"][2][3][0][0] == 1
    assert s["fates"][1][3][1][0] == 1 and s["fates"][1][0][0][1] == 1 and s["fates"][0][1][0][1] == 1
    assert sum(n for a in s["fates"] for b in a for c in b for n in c) == s["lines"] == 6
    g = search_stats.reduce_group([s], 0.002, COSTS)
    assert g["dropped_by_beam"] == {"lines": 2, "in_first_element": 2, "never_led": 1,
                                    "by_kind": {"Start": 0, "Silence": 1, "Rescue": 1}}
    assert g["removed_by_ceiling"] == {"lines": 2, "in_first_element": 1, "never_led": 2,
                                       "by_kind": {"Start": 0, "Silence": 1, "Rescue": 1}}
    # newborns that led: the deficits of the silence newborn and the rescue newborn, not the first story's. Both are above
    # 10 nats; beyond its own starting cost the silence newborn's 25.0 nats are 12.32 and the rescue newborn's 17.0 are
    # 2.13, so one of the two is above the 10-nat margin
    d = g["led_deficits_nats"]
    assert d["newborns"] == 2 and d["deficit"]["max"] == 25.0
    assert d["deficit"]["by_kind"] == {"Silence": {"newborns": 1, "max": 25.0}, "Rescue": {"newborns": 1, "max": 17.0}}
    beyond = d["beyond_starting_cost"]
    assert beyond["max"] == pytest.approx(25.0 - COSTS["Silence"])
    assert beyond["max"] == pytest.approx(12.323923725224091)
    assert beyond["by_kind"]["Silence"]["max"] == pytest.approx(25.0 - COSTS["Silence"])
    assert beyond["by_kind"]["Rescue"]["max"] == pytest.approx(17.0 - COSTS["Rescue"])
    assert beyond["by_kind"]["Rescue"]["newborns"] == 1
    assert beyond["above_margin"] == {"margin_nats": 10.0, "newborns": 1}


def test_the_part_beyond_the_starting_cost_is_each_newborns_own_and_may_be_negative():
    # a silence newborn at 20.0 nats (7.32 beyond its 12.68) and a rescue newborn at 21.0 nats (6.13 beyond its 14.87):
    # the larger whole deficit is not the larger part beyond. A newborn that never trailed by as much as its starting
    # cost (a strong signal at once) has a negative part, which is kept, not cut at zero.
    lines = [(1, 0, 0, 0.040, 20.0), (2, 0, 0, 0.050, 21.0), (1, 0, 0, 0.060, 5.0)]
    g = search_stats.reduce_group([search_stats.reduce_channel(line_channel(lines), 0.002)], 0.002, COSTS)
    d = g["led_deficits_nats"]
    assert d["deficit"]["max"] == 21.0
    beyond = d["beyond_starting_cost"]
    assert beyond["max"] == pytest.approx(20.0 - COSTS["Silence"])           # the silence newborn: 7.32 against 6.13
    assert beyond["by_kind"]["Silence"]["max"] == pytest.approx(20.0 - COSTS["Silence"])
    assert beyond["by_kind"]["Rescue"]["max"] == pytest.approx(21.0 - COSTS["Rescue"])
    assert beyond["quantiles"]["0.5"] == pytest.approx(21.0 - COSTS["Rescue"])    # -7.68, 6.13, 7.32
    assert beyond["above_margin"]["newborns"] == 0
    only_low = search_stats.reduce_group(
        [search_stats.reduce_channel(line_channel([(1, 0, 0, 0.040, 5.0)]), 0.002)], 0.002, COSTS)
    low = only_low["led_deficits_nats"]["beyond_starting_cost"]["max"]
    assert low == pytest.approx(5.0 - COSTS["Silence"]) and low < 0


def test_the_margin_is_a_parameter_of_the_comparison_with_the_part_beyond():
    lines = [(1, 0, 0, 0.040, 25.0), (2, 0, 0, 0.050, 21.5)]    # 12.32 and 6.63 nats beyond their starting costs
    channels = [search_stats.reduce_channel(line_channel(lines), 0.002)]
    for margin, count in ((5.0, 2), (6.7, 1), (12.4, 0)):
        d = search_stats.led_deficits(channels, COSTS, margin_nats=margin)
        assert d["beyond_starting_cost"]["above_margin"] == {"margin_nats": margin, "newborns": count}


def test_the_deficit_quantiles_are_values_of_the_data_by_the_inverted_cdf():
    # 1 000 newborns that led, deficits 0.001 ... 1.000 nats: the q quantile is the smallest value with a share q of the
    # values at or below it (no interpolation), so the 99th percentile is the 990th value
    lines = [(1, 1, 0, 0.5, k / 1000.0) for k in range(1, 1001)]
    g = search_stats.reduce_group([search_stats.reduce_channel(line_channel(lines), 0.002)], 0.002, COSTS)
    d = g["led_deficits_nats"]
    assert d["newborns"] == 1000 and d["deficit"]["max"] == 1.0
    assert d["deficit"]["quantiles"] == {"0.5": 0.5, "0.9": 0.9, "0.99": 0.99, "0.999": 0.999}
    # all of kind Silence: each part beyond is the deficit less 12.68 nats, so the quantiles are the same data points
    assert {q: v + COSTS["Silence"] for q, v in d["beyond_starting_cost"]["quantiles"].items()} == pytest.approx(
        d["deficit"]["quantiles"])
    assert d["beyond_starting_cost"]["above_margin"]["newborns"] == 0
    assert d["deficit"]["by_kind"]["Rescue"] == {"newborns": 0, "max": None}


def test_a_group_pools_channels_by_samples_and_takes_the_largest_maximum():
    a = line_channel([(0, 1, 0, 0.002, 0.0)], samples=100, ceiling_samples=10, means=[10.0, 10.0], maxima=[11.0, 12.0])
    b = line_channel([(0, 0, 0, 0.002, 0.0)], samples=300, ceiling_samples=90, means=[30.0] * 6, maxima=[31.0] * 6,
                     track_id=2)
    g = search_stats.reduce_group([search_stats.reduce_channel(c, 0.002) for c in (a, b)], 0.002, COSTS)
    assert g["live_stories"] == {"mean": pytest.approx((100 * 10.0 + 300 * 30.0) / 400), "max": 31.0}
    assert g["ceiling_share"] == pytest.approx(100 / 400) and g["channel_seconds"] == pytest.approx(0.8)
    none = search_stats.reduce_group([], 0.002, COSTS)
    assert none["channels"] == 0 and none["live_stories"] == {"mean": None, "max": None}
    assert none["ceiling_share"] is None and none["led_deficits_nats"]["newborns"] == 0
    for part in ("deficit", "beyond_starting_cost"):
        assert none["led_deficits_nats"][part]["max"] is None
        assert none["led_deficits_nats"][part]["quantiles"] == {"0.5": None, "0.9": None, "0.99": None, "0.999": None}
    assert none["led_deficits_nats"]["beyond_starting_cost"]["above_margin"] == {"margin_nats": 10.0, "newborns": 0}
    assert none["led_deficits_nats"]["starting_cost_nats"] == COSTS


# --- a run's statistics files ---

def _suite(tmp_path, stats_text, run="r1", genie=None):
    """A suite folder: three oracle test cases of two groups (A2 twice, B2 once) and an A2 detector-path copy, results
    for `run` with a statistics file for the first two (A2 and B2) and a result without one for the third. genie: a
    dict of test case to the genie kinds its result file records (the bench writes "genie" only for a run given one)."""
    recs = [("A2-awgn-c01-0-s1", "A2 sensitivity", True), ("B2-fading-c01-0-s1", "B2 fading", True),
            ("A2-awgn-c02-0-s1", "A2 sensitivity", True), ("A2-detector-c01-0-s1", "A2 sensitivity, detector", False)]
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": n, "group": g, "oracle": o, "wav": f"{n}.wav", "labels": f"{n}.json", "station_labels": None}
        for n, g, o in recs]}))
    folder = tmp_path / "results" / run
    folder.mkdir(parents=True)
    for n, _g, _o in recs[:3]:
        result = {"score": {"signals": []}}
        if genie and n in genie:
            result["genie"] = {"kinds": list(genie[n]), "channels": []}
        (folder / f"{n}.json").write_text(json.dumps(result))
    for n in ("A2-awgn-c01-0-s1", "B2-fading-c01-0-s1"):
        (folder / f"{n}.stats.json").write_text(stats_text, encoding="utf-8")
    return folder


def test_a_runs_statistics_are_reduced_per_group_and_pooled(tmp_path):
    _suite(tmp_path, V1.read_text(encoding="utf-8"))
    s = search_stats.run_summary(tmp_path, "r1")
    assert (s["run"], s["versions"], s["sample_interval_s"], s["test_cases"]) == ("r1", [1], 0.002, 2)
    assert s["without_statistics"] == ["A2-awgn-c02-0-s1"]    # decoded, but no statistics file; the detector copy has none
    assert list(s["groups"]) == ["A2 sensitivity", "B2 fading"]
    assert s["groups"]["A2 sensitivity"]["channels"] == s["groups"]["B2 fading"]["channels"] == 2
    assert s["all"]["channels"] == 4 and s["all"]["lines"]["total"] == 4 * 1045
    # each group is the reduction of the file's two channels, and all is the four channels
    one = search_stats.reduce_group(search_stats.channel_summaries(v1_file()), 0.002, COSTS)
    assert s["groups"]["A2 sensitivity"] == one == s["groups"]["B2 fading"]
    assert s["all"]["samples"] == 2 * one["samples"] and s["all"]["ceiling_samples"] == 2 * one["ceiling_samples"]
    assert s["all"]["led_deficits_nats"]["newborns"] == 2
    # the starting costs come from the files' configuration (the defaults), once for the run
    assert s["starting_cost_nats"] == pytest.approx(COSTS) and s["genie_fist"] is False
    assert s["all"]["led_deficits_nats"]["starting_cost_nats"] == s["starting_cost_nats"]
    # a row for each channel, to join with the run's per-signal results
    assert len(s["channels"]) == 4
    first = s["channels"][0]
    assert (first["group"], first["test_case"], first["index"], first["samples"]) == (
        "A2 sensitivity", "A2-awgn-c01-0-s1", 0, 523)
    assert first["dropped_by_beam_in_first_element"] > 0 and first["removed_by_ceiling"] == 100 + 155
    assert first["led_deficit_max_nats"] == pytest.approx(17.382956934607172)
    assert first["led_beyond_start_max_nats"] == pytest.approx(17.382956934607172 - COSTS["Silence"])
    assert s["channels"][1]["led_deficit_max_nats"] is None    # the noise channel: only its first story led
    assert s["channels"][1]["led_beyond_start_max_nats"] is None
    # a selection of test cases
    only = search_stats.run_summary(tmp_path, "r1", only=r"^B2-")
    assert list(only["groups"]) == ["B2 fading"] and only["test_cases"] == 1 and only["without_statistics"] == []


def test_a_runs_summary_is_written_as_tables_and_data(tmp_path):
    _suite(tmp_path, V1.read_text(encoding="utf-8"))
    s = search_stats.run_summary(tmp_path, "r1")
    path = search_stats.write_summary(tmp_path, s)
    assert path == tmp_path / "experiments" / "search-stats-r1.md"
    md = path.read_text(encoding="utf-8")
    assert md.startswith("# Search statistics: r1")
    assert "| A2 sensitivity | 2 | 2.1 |" in md and "| all | 4 | 4.2 |" in md       # channels, channel-seconds (s)
    assert "| A2 sensitivity | Silence |" in md
    assert "nats" in md and "Test cases with a result but no statistics file: `A2-awgn-c02-0-s1`" in md
    assert str(tmp_path) not in md                                                   # committed beside records: no folders
    assert json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))["all"]["channels"] == 4
    # R1: two tables, the whole deficit (17.38 nats) and the part beyond the starting cost (4.71 nats), and the 10 nats
    # are compared with the second; the caption says so, with the starting costs of the run
    whole, beyond = md.index("## Largest deficit a newborn reaches before it leads"), md.index("## The part of that deficit")
    assert whole < beyond
    first_table, second_table = md[whole:beyond], md[beyond:md.index("## Lines the beam dropped")]
    assert "| A2 sensitivity | 1 | 17.38 |" in first_table and "above 10 nats" not in first_table
    assert "| A2 sensitivity | 1 | 4.71 |" in second_table and "above 10 nats" in second_table
    assert "beyond its own starting cost" in second_table and "margin of 10 nats" in second_table
    assert "12.68" in second_table and "14.87" in second_table                       # the starting costs, nats
    assert "ln(1/P) + ln(speed count)" in second_table and "ln(1/ε) + ln(speed count) + 4.5" in second_table
    assert "learning its level" in second_table                                       # what the 4.5 nats are


def test_a_run_with_nothing_to_reduce_or_mixed_samples_is_refused(tmp_path):
    folder = _suite(tmp_path, V1.read_text(encoding="utf-8"))
    with pytest.raises(ValueError, match="no results folder named 'nope'"):
        search_stats.run_summary(tmp_path, "nope")
    with pytest.raises(ValueError, match="no statistics files"):
        search_stats.run_summary(tmp_path, "r1", only=r"^never$")
    # a file with another sample (s) cannot be pooled with the first
    other = v1_file()
    other["config"]["sample_interval_s"] = 0.004
    for ch in other["channels"]:     # the same windows of 0.1 s hold 25 samples: 523 samples would need 21 windows
        ch["samples"], ch["live_mean_100ms"], ch["live_max_100ms"] = 60, [1.0, 1.0, 1.0], [1.0, 1.0, 1.0]
    (folder / "B2-fading-c01-0-s1.stats.json").write_text(json.dumps(other), encoding="utf-8")
    with pytest.raises(ValueError, match="0.004 s"):
        search_stats.run_summary(tmp_path, "r1")
    empty = tmp_path / "empty"
    (empty / "results" / "r1").mkdir(parents=True)
    (empty / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": []}))
    with pytest.raises(ValueError, match="no statistics files"):
        search_stats.run_summary(empty, "r1")


def test_the_starting_costs_follow_the_genie_the_run_was_given(tmp_path):
    # a genie fist gives each channel's decoder one speed (the base configuration in the statistics file does not show
    # it; the result file's "genie" does), so no speed prior of ln(32) is paid
    both = {"A2-awgn-c01-0-s1": ["fading", "fist"], "B2-fading-c01-0-s1": ["fist"]}
    _suite(tmp_path, V1.read_text(encoding="utf-8"), genie=both)
    s = search_stats.run_summary(tmp_path, "r1")
    assert s["genie_fist"] is True
    assert s["starting_cost_nats"] == pytest.approx({"Silence": math.log(1e4), "Rescue": math.log(1e3) + 4.5})
    assert s["all"]["led_deficits_nats"]["starting_cost_nats"] == s["starting_cost_nats"]
    assert s["all"]["led_deficits_nats"]["beyond_starting_cost"]["max"] == pytest.approx(
        17.382956934607172 - math.log(1e4))
    # genie kinds without the fist leave the speed prior
    other = tmp_path / "other"
    other.mkdir()
    _suite(other, V1.read_text(encoding="utf-8"), genie={"A2-awgn-c01-0-s1": ["fading"], "B2-fading-c01-0-s1": ["fading"]})
    plain = search_stats.run_summary(other, "r1")
    assert plain["genie_fist"] is False and plain["starting_cost_nats"] == pytest.approx(COSTS)
    # a run given the fist for some test cases only is not one run: its starting costs differ, and it is refused
    mixed = tmp_path / "mixed"
    mixed.mkdir()
    _suite(mixed, V1.read_text(encoding="utf-8"), genie={"A2-awgn-c01-0-s1": ["fist"]})
    with pytest.raises(ValueError, match=r"B2-fading-c01-0-s1.*starting costs"):
        search_stats.run_summary(mixed, "r1")


def test_a_run_whose_files_have_different_starting_costs_is_refused(tmp_path):
    folder = _suite(tmp_path, V1.read_text(encoding="utf-8"))
    other = v1_file()
    other["config"]["over_rate_per_s"] = 0.5      # a run made with another over rate is another run
    (folder / "B2-fading-c01-0-s1.stats.json").write_text(json.dumps(other), encoding="utf-8")
    with pytest.raises(ValueError, match=r"B2-fading-c01-0-s1.*starting costs"):
        search_stats.run_summary(tmp_path, "r1")


# --- the two versions give the same numbers ---

V2 = DATA / "tiny.v2.stats.json"


def assert_same(a, b, where="value"):
    """a equals b, the floats to 1e-12 relative (the mean of the live stories is a sum of products, whose last digit
    the order of the summation can change); everything else exactly."""
    if isinstance(a, dict):
        assert isinstance(b, dict) and a.keys() == b.keys(), where
        for key in a:
            assert_same(a[key], b[key], f"{where}.{key}")
    elif isinstance(a, list):
        assert isinstance(b, list) and len(a) == len(b), where
        for i, (x, y) in enumerate(zip(a, b)):
            assert_same(x, y, f"{where}[{i}]")
    elif isinstance(a, float) or isinstance(b, float):
        assert a == pytest.approx(b, rel=1e-12, abs=0.0), where
    else:
        assert a == b, where


def test_the_committed_summary_is_the_reduction_of_the_committed_full_file():
    # the two files are one run's statistics in the two forms (the bench wrote them from the same recording)
    full, summary = search_stats.read_stats(V1), search_stats.read_stats(V2)
    assert summary["version"] == 2 and full["version"] == 1
    for key in ("format", "recording", "config", "line_kinds", "line_ends"):
        assert summary[key] == full[key], key
    assert summary["line_kinds"] == list(search_stats.LINE_KINDS)
    assert summary["line_ends"] == list(search_stats.LINE_ENDS)
    assert summary["fate_axes"] == list(search_stats.FATE_AXES)
    # the legend names every member of a summary channel and of a line that led, and a unit first
    fields = summary["fields"]["ChannelSummary"]
    assert set(fields) == {"samples", "ceiling_samples", "live_histogram", "live_mean_stories", "live_max_stories",
                           "lines", "fates", "led_lines"}
    assert set(fields) | {"track_id", "label_index"} == set(summary["channels"][0])
    assert set(summary["fields"]["NewbornLine"]) == {"line", *search_stats.LINE_COLUMNS}
    # the reduction of each full channel is the summary's channel
    sample_interval_s = full["config"]["sample_interval_s"]
    assert len(full["channels"]) == len(summary["channels"]) == 2
    for mine, theirs in zip((search_stats.reduce_channel(c, sample_interval_s) for c in full["channels"]),
                            summary["channels"]):
        assert_same(mine, theirs)
    # what the summary does not keep: the 0.1 s series, and the lines that never led
    assert "live_mean_100ms" not in summary["channels"][0] and "lines" in summary["channels"][0]
    assert not isinstance(summary["channels"][0]["lines"], dict)
    assert V2.stat().st_size < V1.stat().st_size / 10


def test_either_version_gives_the_same_group_and_run_reductions(tmp_path):
    full, summary = search_stats.read_stats(V1), search_stats.read_stats(V2)
    sample_interval_s = full["config"]["sample_interval_s"]
    assert_same(search_stats.reduce_group(search_stats.channel_summaries(full), sample_interval_s, COSTS),
                search_stats.reduce_group(search_stats.channel_summaries(summary), sample_interval_s, COSTS))
    runs = []
    for version, path in ((1, V1), (2, V2)):
        suite = tmp_path / f"v{version}"
        suite.mkdir()
        _suite(suite, path.read_text(encoding="utf-8"))
        runs.append(search_stats.run_summary(suite, "r1"))
    assert [r["versions"] for r in runs] == [[1], [2]]
    for r in runs:
        r.pop("versions")
    assert_same(runs[0], runs[1])
    # and the tables written from either are the same text
    tables = []
    for version, run in enumerate(runs, start=1):
        run["versions"] = [version]
        tables.append(search_stats.format_markdown(run).replace(f"version {version};", "version N;"))
    assert tables[0] == tables[1]


def test_a_version_2_channel_that_lacks_a_member_is_refused():
    summary = search_stats.read_stats(V2)
    assert len(search_stats.channel_summaries(summary)) == 2
    del summary["channels"][1]["fates"]
    with pytest.raises(ValueError, match="fates"):
        search_stats.channel_summaries(summary)


def test_a_version_2_channel_that_contradicts_itself_is_refused():
    # The reader checks what the bench's writer guarantees (a file edited or assembled by hand is the case): the
    # histogram adds up to the samples, the fates to the lines, and the lines that led, by kind, end and first element, are
    # the fates' led counts. The real file's first channel: 523 samples, 1 045 lines, two of which led (line 0, the first
    # story, kind Start, merged; line 89, kind Silence, alive).
    base = search_stats.read_stats(V2)
    assert len(search_stats.channel_summaries(base)) == 2

    def refusal(edit):
        summary = json.loads(json.dumps(base))
        edit(summary["channels"][0])
        with pytest.raises(ValueError) as refused:
            search_stats.channel_summaries(summary)
        return str(refused.value)

    def bump_histogram(c):
        c["live_histogram"][0] += 1

    def bump_samples(c):
        c["samples"] += 1

    assert "live_histogram" in refusal(bump_histogram) and "channel 1" in refusal(bump_histogram)
    assert "live_histogram" in refusal(bump_samples)
    assert "fates" in refusal(lambda c: c.update(lines=c["lines"] + 1))
    assert "fates" in refusal(lambda c: c["fates"][2][3][1].__setitem__(1, c["fates"][2][3][1][1] + 1))
    assert "fates" in refusal(lambda c: c["fates"].pop())                       # 2 kinds, not 3
    assert "fates" in refusal(lambda c: c["fates"][0].pop())                    # 3 ends, not 4
    assert "fates" in refusal(lambda c: c["fates"][1][1].append([0, 0]))        # 3 values of ended_in_first_element
    assert "fates" in refusal(lambda c: c["fates"][1][1][0].__setitem__(0, -1))
    # the lines that led against the fates' led counts
    assert "fates" in refusal(lambda c: c["led_lines"]["kind"].__setitem__(0, 1))
    assert "fates" in refusal(lambda c: c["led_lines"]["kind"].__setitem__(1, 2))
    assert "fates" in refusal(lambda c: c["led_lines"].__setitem__("end", [0, 1]))   # the two ends swapped
    assert "fates" in refusal(lambda c: c["led_lines"]["ended_in_first_element"].__setitem__(1, True))
    # the columns of the lines that led
    assert "led_lines" in refusal(lambda c: c["led_lines"]["largest_deficit_nats"].pop())
    assert "led_lines" in refusal(lambda c: c["led_lines"].__setitem__("line", [0, 5000]))   # not below the 1 045 lines
    assert "led_lines" in refusal(lambda c: c["led_lines"].__setitem__("line", [89, 0]))     # not ascending
    assert "first_led_s" in refusal(lambda c: c["led_lines"]["first_led_s"].__setitem__(1, None))
    assert "kind" in refusal(lambda c: c["led_lines"]["kind"].__setitem__(1, 7))
    assert "end" in refusal(lambda c: c["led_lines"]["end"].__setitem__(1, 4))
    assert "led_lines" in refusal(lambda c: c.update(led_lines={k: v for k, v in c["led_lines"].items() if k != "end_s"}))


def test_what_the_reduction_makes_passes_the_checks_of_a_summary():
    for channel in v1_file()["channels"]:
        search_stats.check_summary_channel(search_stats.reduce_channel(channel, 0.002))
    lines = [(0, 1, 0, 0.002, 0.0), (1, 0, 0, 0.040, 25.0), (2, 2, 1, 0.050, 17.0), (1, 2, 1, None, 41.0),
             (2, 3, 0, None, 9.5)]
    search_stats.check_summary_channel(search_stats.reduce_channel(line_channel(lines), 0.002))
    search_stats.check_summary_channel(search_stats.reduce_channel(line_channel([], samples=0, means=[], maxima=[]), 0.002))


# --- the reader ---

def test_the_reader_accepts_the_two_versions_and_refuses_anything_else(tmp_path):
    stats = v1_file()
    ok = tmp_path / "ok.json"
    ok.write_text(json.dumps(stats), encoding="utf-8")
    assert search_stats.read_stats(ok)["version"] == 1
    for edit, why in ((lambda s: s.update(version=3), "version"), (lambda s: s.update(version=0), "version"),
                      (lambda s: s.update(format="other"), "format"), (lambda s: s.pop("channels"), "channels"),
                      (lambda s: s.pop("config"), "config")):
        bad = json.loads(json.dumps(stats))
        edit(bad)
        path = tmp_path / "bad.json"
        path.write_text(json.dumps(bad), encoding="utf-8")
        with pytest.raises(ValueError, match=why):
            search_stats.read_stats(path)
    path.write_text("not json", encoding="utf-8")
    with pytest.raises(ValueError, match="JSON"):
        search_stats.read_stats(path)
    with pytest.raises(FileNotFoundError):
        search_stats.read_stats(tmp_path / "missing.json")
