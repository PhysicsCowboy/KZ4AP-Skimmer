"""The jittered development set (task D1 of docs/plans/2026-10-06-development-set-redesign.md)."""

import json
import math
import re
from collections import Counter
from pathlib import Path

import numpy as np
import pytest

from kz4ap_proto import experiments
from kz4ap_synth import suites
from kz4ap_synth.generate import (DEFAULT_NOISE_SIGMA, EDGE_S, generate, labels, plan_intervals,
                                  station_labels)
from kz4ap_synth.jitter import SNR_CELLS, SPEED_CELLS
from kz4ap_synth.morse import keying_intervals
from kz4ap_synth.suites import SUITES, check_recording, dev2_suite, text_100, text_chars


@pytest.fixture(scope="module")
def dev2():
    return dev2_suite(1)


def _scored(recs, group):
    return [s for r in recs if r.group == group for s in r.specs if s.score]


def test_text_is_100_plus_minus_5_characters():
    rng = np.random.default_rng(11)
    lengths = [text_chars(text_100(rng)) for _ in range(1000)]
    assert min(lengths) >= 95 and max(lengths) <= 105
    assert abs(np.mean(lengths) - 100) < 1.0
    assert text_chars("CQ <BT> K") == 6  # spaces counted, a prosign one character


def test_nominal_duration_is_12_over_wpm_per_character():
    assert suites.nominal_duration_s("PARIS ", 12.0) == pytest.approx(6.0)


def test_every_recording_is_valid_named_and_seeded(dev2):
    names = [r.name for r in dev2]
    assert len(names) == len(set(names))
    for r in dev2:
        check_recording(r)
        assert re.match(r"^[A-IS]2-", r.name) and r.name.endswith("-s1")
        assert len(r.specs) <= 2 * suites.DEV2_MAX_STATIONS  # a unit holds at most a signal and its neighbor
    seeds = Counter(r.noise_seed for r in dev2)
    shared = {s for s, n in seeds.items() if n > 1}
    # only A2 and its detector-path copies share noise
    assert shared == {r.noise_seed for r in dev2 if r.group == "A2 sensitivity, detector"}
    old = {r.noise_seed for r in SUITES["full"](3)}
    assert not old & set(seeds)
    assert all(r.oracle for r in dev2 if r.group != "A2 sensitivity, detector")


def test_a2_has_per_cell_signals_in_every_speed_and_snr_cell(dev2):
    specs = _scored(dev2, "A2 sensitivity")
    assert len(specs) == 10 * 14 * 4
    cells = Counter((s.design["speed_wpm"]["cell"], s.design["s500_db"]["cell"]) for s in specs)
    assert set(cells) == {(v, k) for v in SPEED_CELLS.numbers for k in SNR_CELLS.numbers}
    assert set(cells.values()) == {4}
    assert all(s.keying == "machine" and s.fading_hz == 0 for s in specs)
    assert len({s.wpm for s in specs}) == len(specs) and len({s.snr_db for s in specs}) == len(specs)
    small = _scored(dev2_suite(1, per_cell={"A2": 1}, groups=["A2"]), "A2 sensitivity")
    assert len(small) == 140


def test_the_other_groups_have_their_counts_per_cell(dev2):
    def count(group, *keys):
        return Counter(tuple(s.design[k]["cell"] for k in keys) for s in _scored(dev2, group))
    b2 = count("B2 fading", "speed_wpm", "fading_hz", "s500_db")
    assert len(b2) == 10 * 4 * 2 and set(b2.values()) == {2}
    c2 = count("C2 fists", "speed_wpm", "keying", "s500_db")
    assert len(c2) == 10 * 5 * 3 and set(c2.values()) == {2}
    d2 = count("D2 speed changes", "speed_wpm", "direction", "profile")
    assert len(d2) == 36 and set(d2.values()) == {3}
    assert (1, 2, 1) not in d2 and (10, 1, 1) not in d2  # cell 1 cannot go down, cell 10 cannot go up
    e2 = count("E2 interference", "speed_wpm", "offset_hz")
    assert len(e2) == 30 and set(e2.values()) == {3}
    f2 = Counter((s.design["speed_wpm"]["cell"], s.design["drift_excursion_hz"]["cell"] if "drift_excursion_hz"
                  in s.design else 0) for s in _scored(dev2, "F2 tuning"))
    assert len(f2) == 10 * (1 + 3) and set(f2.values()) == {2}  # offsets (cell 0 here) and 3 excursion cells
    for group in ("G2 QSO, same track", "H2 QSO, separate tracks"):
        assert count(group, "speed_wpm") == Counter({(v,): 2 for v in SPEED_CELLS.numbers})
    i2 = count("I2 Farnsworth", "speed_wpm", "farnsworth_wpm", "s500_db", "keying")
    assert len(i2) == 2 * 3 * 3 * 2 and set(i2.values()) == {2}
    s2 = _scored(dev2, "S2 stretch")
    assert len(s2) == 14 * 4


def test_per_cell_scales_every_group():
    recs = dev2_suite(1, per_cell={g: 1 for g in suites.DEV2_PER_CELL})
    assert len(_scored(recs, "B2 fading")) == 80 and len(_scored(recs, "S2 stretch")) == 14
    assert len(_scored(recs, "H2 QSO, separate tracks")) == 10
    with pytest.raises(ValueError):
        dev2_suite(1, per_cell={"Z2": 1})
    with pytest.raises(ValueError):
        dev2_suite(1, per_cell={"A2": 0}, groups=["A2"])


def _design_values(design):
    """(name, entry) of every drawn value with a cell in a label's design."""
    return [(k, v) for k, v in design.items() if isinstance(v, dict) and v.get("cell") is not None and "range" in v]


def test_every_label_value_lies_inside_its_cell(dev2):
    checked = Counter()
    for r in dev2:
        entries = ([] if r.per_station_only else labels(r.specs, r.sample_rate, r.duration_s, r.noise_seed)["signals"])
        if r.station_labels or r.per_station_only:
            entries += station_labels(r.specs, r.sample_rate, r.duration_s, r.noise_seed)["signals"]
        for e in entries:
            if not e.get("score", True):
                assert "design" not in e  # the E2 neighbor: its values are in the wanted signal's label
                continue
            design = e["design"]
            assert design["group"] == r.name[:2]
            values = _design_values(design)
            assert values
            for name, v in values:
                assert v["range"][0] <= v["value"] <= v["range"][1], (r.name, name, v)
                checked[name] += 1
    assert {"speed_wpm", "s500_db", "fading_hz", "offset_hz", "factor", "end_speed_wpm", "neighbor_relative_db",
            "neighbor_speed_wpm", "imbalance_dits", "drift_excursion_hz", "answer_speed_wpm", "answer_offset_hz",
            "answer_relative_db", "farnsworth_wpm"} <= set(checked)


def test_label_values_are_the_signals_values(dev2):
    for r in dev2:
        for s in r.specs:
            if not s.score or r.group == "S2 stretch":
                continue
            d = s.design
            assert s.wpm == d["speed_wpm"]["value"] and s.snr_db == d["s500_db"]["value"]
            if r.group != "I2 Farnsworth":  # I2's speed_wpm is the character speed, in its own cells
                assert d["speed_wpm"]["range"] == list(SPEED_CELLS.bounds(d["speed_wpm"]["cell"]))
            if "fading_hz" in d:
                assert s.fading_hz == d["fading_hz"]["value"] and s.keying == d["keying"]["value"]
                assert s.imbalance_dits == d["imbalance_dits"]["value"]
            if "end_speed_wpm" in d:
                end, factor = d["end_speed_wpm"]["value"], d["factor"]["value"]
                assert s.wpm_end == end and 8.0 <= end <= 80.0 and 1.3 <= factor <= 2.0
                up = d["direction"]["value"] == "up"
                assert end == pytest.approx(s.wpm * factor if up else s.wpm / factor, rel=1e-12)
            if "farnsworth_wpm" in d:
                assert s.farnsworth_wpm == d["farnsworth_wpm"]["value"] < s.wpm
            if s.senders:
                assert s.senders[1].wpm == d["answer_speed_wpm"]["value"]
                assert s.senders[1].offset_hz == d["answer_offset_hz"]["value"]
                assert s.senders[1].relative_db == d["answer_relative_db"]["value"]


def test_texts_are_100_characters_except_qsos(dev2):
    for r in dev2:
        for s in r.specs:
            if s.score and not s.overs:
                assert 95 <= text_chars(s.text) <= 105


def test_e2_neighbors_and_f2_offsets_sit_where_their_labels_say(dev2):
    for r in dev2:
        if r.group == "E2 interference":
            for wanted, neighbor in zip(r.specs[::2], r.specs[1::2]):
                assert wanted.score and not neighbor.score
                d = wanted.design
                assert neighbor.freq_offset_hz - wanted.freq_offset_hz == pytest.approx(d["offset_hz"]["value"],
                                                                                         abs=1e-9)
                assert neighbor.snr_db - wanted.snr_db == pytest.approx(d["neighbor_relative_db"]["value"], abs=1e-9)
                assert neighbor.wpm == d["neighbor_speed_wpm"]["value"] and neighbor.start_s == wanted.start_s
        if r.name.startswith("F2-drift"):
            for s, plan in zip(r.specs, plan_intervals(r.specs, r.noise_seed)):
                d = s.design
                keying_s = plan.intervals[-1][1] - plan.intervals[0][0]  # first key-down to last key-up
                assert d["drift_excursion_hz"]["range"] in ([0.0, 12.0], [12.0, 25.0], [25.0, 40.0])
                assert 0.0 <= s.drift_hz_per_s * keying_s <= 40.0
                assert s.drift_hz_per_s * keying_s == pytest.approx(d["drift_excursion_hz"]["value"], abs=1e-9)
                assert d["drift_hz_per_s"]["value"] == s.drift_hz_per_s
                assert d["drift_hz_per_s"]["keying_time_s"] == pytest.approx(keying_s, abs=1e-12)
        if r.group == "E2 interference":
            for wanted, neighbor in zip(r.specs[::2], r.specs[1::2]):
                cover = keying_intervals(neighbor.text, neighbor.wpm)[-1][1] / keying_intervals(wanted.text,
                                                                                                wanted.wpm)[-1][1]
                assert wanted.design["neighbor_coverage"]["value"] == pytest.approx(cover, rel=1e-12)
        if r.name.startswith("F2-offset"):
            for s in r.specs:  # an exact bin center plus the drawn offset
                bins = (s.freq_offset_hz - s.design["offset_hz"]["value"]) / suites.BIN_HZ
                assert bins == pytest.approx(round(bins), abs=1e-9)


def test_qso_groups_have_their_views(dev2):
    g2 = [r for r in dev2 if r.group == "G2 QSO, same track"]
    h2 = [r for r in dev2 if r.group == "H2 QSO, separate tracks"]
    assert g2 and h2 and not any(r.station_labels or r.per_station_only for r in g2)
    assert all(r.per_station_only and not r.station_labels for r in h2)  # H2: per-station labels only (ruling)
    assert all(0.0 <= s.senders[1].offset_hz <= 10.0 for r in g2 for s in r.specs)
    assert all(200.0 <= s.senders[1].offset_hz <= 300.0 for r in h2 for s in r.specs)
    assert all(suites.qso_regime(s.senders[1].offset_hz, fe) == "same-track" for r in g2 for s in r.specs
               for fe in ("baseline", "matched"))
    assert all(suites.qso_regime(s.senders[1].offset_hz, fe) == "separate-track" for r in h2 for s in r.specs
               for fe in ("baseline", "matched"))


def test_s2_scales_its_edges(dev2):
    # Ruling 62: S2's edges are D17's scaled by its factor 2: 8.4698 ms, placement "start"
    recs = {r.name: r for r in dev2}
    for r in (r for r in dev2 if r.group == "S2 stretch"):
        a = recs[suites.stretch2_source(r.name)]
        for s, sa in zip(r.specs, a.specs):
            assert s.edge_s == 2.0 * EDGE_S == 2.0 * sa.edge_s
            assert s.edge_s == pytest.approx(8.4698e-3, abs=0.0001e-3)
            assert s.edge_placement == "start"


def test_dev2_and_n2_take_d17s_edges_set_by_their_builders(dev2):
    # D17 (owner, 2026-10-23) and Ruling 70: DEV2 (DEV2e) and N2 (N2e) take D17's edges, except B2, which keeps
    # VE3NEA's centered 2 ms (Ruling 62); S2 scales D17's by 2. Every signal, including E2's neighbors and G2/H2's QSOs.
    groups = set()
    for r in list(dev2) + suites.n2_suite(1):
        groups.add(r.group)
        for s in r.specs:
            if r.group == "B2 fading":
                assert (s.edge_placement, s.edge_s) == ("centered", 0.002)
            elif r.group == "S2 stretch":
                assert (s.edge_placement, s.edge_s) == ("start", 2.0 * EDGE_S)
            else:
                assert (s.edge_placement, s.edge_s) == ("start", EDGE_S)
    assert {"A2 sensitivity", "B2 fading", "S2 stretch", "E2 interference", "N2 noise only"} <= groups
    assert any(not s.score for r in dev2 if r.group == "E2 interference" for s in r.specs)  # the neighbors too


_BEFORE_D17 = Path(__file__).with_name("data") / "dev2_labels_before_d17.json"


def test_regeneration_changes_only_the_envelope(dev2):
    # The fixture holds the labels of one recording of each DEV2 group (seed 1; the first signal, the first two for
    # E2) as the generator wrote them before D17's edges (commit cbc4ecd). Regenerated, the texts, the transmissions'
    # start and end times, the speeds and the noise seeds are identical: only the keying edges differ, and the
    # label schema (edges_centered becomes edge_placement, B2's included).
    before = json.loads(_BEFORE_D17.read_text(encoding="utf-8"))
    recs = {r.name: r for r in dev2}
    assert sorted(before) == sorted(n for n in before if n in recs) and len(before) == 10
    assert {n.split("-")[0] for n in before} == set(suites.DEV2_BUILDERS)
    for name, old in before.items():
        rec = recs[name]
        assert rec.noise_seed == old["noise_seed"] and rec.per_station_only == old["per_station"]
        make = station_labels if rec.per_station_only else labels
        new = json.loads(json.dumps(make(rec.specs, rec.sample_rate, rec.duration_s, rec.noise_seed)))  # as written
        assert {k: v for k, v in new.items() if k != "signals"} == {k: v for k, v in old["labels"].items()
                                                                     if k != "signals"}
        assert len(old["labels"]["signals"]) >= 1
        for was, now in zip(old["labels"]["signals"], new["signals"]):
            if rec.per_station_only:  # a station's entry (H2's only labels) never named its edges, before or after
                assert not {"edge_s", "edges_centered", "edge_placement"} & (set(was) | set(now))
                assert now == was
                continue
            assert "edges_centered" in was and "edge_placement" not in was
            assert "edge_placement" in now and "edges_centered" not in now
            rest_was = {k: v for k, v in was.items() if k not in ("edge_s", "edges_centered")}
            rest_now = {k: v for k, v in now.items() if k not in ("edge_s", "edge_placement")}
            assert rest_now == rest_was  # texts, transmissions, speeds and everything else identical
            if name.startswith("B2-"):
                assert was["edges_centered"] is True and (was["edge_s"], now["edge_s"]) == (0.002, 0.002)
                assert now["edge_placement"] == "centered"
            else:
                assert was["edges_centered"] is False
                assert now["edge_placement"] == "start"
                assert was["edge_s"] == (0.01 if name.startswith("S2-") else 0.005)
                assert now["edge_s"] == (2.0 * EDGE_S if name.startswith("S2-") else EDGE_S)


def test_s2_is_a2s_cell_5_stretched_by_exactly_2(dev2):
    recs = {r.name: r for r in dev2}
    stretched = [r for r in dev2 if r.group == "S2 stretch"]
    assert sorted(r.name for r in stretched) == ["S2-stretch-c05-0-s1", "S2-stretch-c05-1-s1"]
    assert suites.STRETCH2_SNR_DB == pytest.approx(3.0103, abs=1e-4)
    for r in stretched:
        a = recs[suites.stretch2_source(r.name)]
        assert r.oracle and r.duration_s == 2.0 * a.duration_s
        plans, plans_a = plan_intervals(r.specs, r.noise_seed), plan_intervals(a.specs, a.noise_seed)
        for i, (s, sa, plan, plan_a) in enumerate(zip(r.specs, a.specs, plans, plans_a)):
            assert (s.text, s.freq_offset_hz, s.start_s) == (sa.text, sa.freq_offset_hz, sa.start_s)
            assert s.wpm == sa.wpm / 2.0 and sa.design["speed_wpm"]["cell"] == 5
            assert sa.snr_db - s.snr_db == pytest.approx(10 * math.log10(2.0), abs=1e-12)
            assert s.edge_s == 2.0 * sa.edge_s  # Ruling 62: S2 scales D17's edges with its time base
            assert s.edge_s == pytest.approx(8.4698e-3, abs=0.0001e-3) and s.edge_placement == "start"
            got, want = np.array(plan.intervals), 2.0 * np.array(plan_a.intervals)
            assert got.shape == want.shape and np.max(np.abs(got - want)) < 1e-9
            d = s.design
            assert (d["source"], d["source_index"], d["stretch_factor"]) == (a.name, i, 2.0)
            assert d["speed_wpm"]["range"] == [x / 2.0 for x in SPEED_CELLS.bounds(5)]
            assert d["speed_wpm"]["source_value"] == sa.wpm and d["s500_db"]["source_value"] == sa.snr_db
            assert d["s500_db"]["cell"] == sa.design["s500_db"]["cell"]
        check_recording(r)


def test_the_suite_is_reproducible_by_seed(dev2):
    again = dev2_suite(1)
    assert [(r.name, r.duration_s, r.noise_seed, r.specs) for r in again] == \
        [(r.name, r.duration_s, r.noise_seed, r.specs) for r in dev2]
    seed2 = dev2_suite(2, groups=["A2"])
    assert {r.name for r in seed2 if r.name.endswith("-s1")} == {r.name for r in dev2 if r.name.startswith("A2-")}
    a1 = [s.wpm for r in dev2 if r.group == "A2 sensitivity" for s in r.specs]
    a2 = [s.wpm for r in seed2 if r.group == "A2 sensitivity" and r.name.endswith("-s2") for s in r.specs]
    assert len(a1) == len(a2) and set(a1).isdisjoint(a2)


def test_a_signals_draws_do_not_depend_on_per_cell():
    small = {(s.design["speed_wpm"]["cell"], s.design["s500_db"]["cell"]): s
             for r in dev2_suite(1, per_cell={"A2": 1}, groups=["A2"]) if r.group == "A2 sensitivity" for s in r.specs}
    big = [s for r in dev2_suite(1, per_cell={"A2": 2}, groups=["A2"]) if r.group == "A2 sensitivity"
           for s in r.specs]
    for key, s in small.items():
        assert any((b.wpm, b.snr_db, b.text) == (s.wpm, s.snr_db, s.text) for b in big)


def test_dev2_selects_seed_1_of_the_new_oracle_recordings(dev2):
    names = [r.name for r in dev2]
    picked = {n for n in names if re.search(experiments.DEV2, n)}
    assert picked == {r.name for r in dev2 if r.group != "A2 sensitivity, detector"}
    assert not re.search(experiments.DEV2, "H2-qso-c01-0-s1.stations")  # H2's only labels are per station
    assert not any(re.search(experiments.DEV2, n.replace("-s1", "-s2")) for n in names)
    assert not any(re.search(experiments.DEV2, r.name) for r in SUITES["full"](1))
    assert not any(re.search(experiments.DEV, n) for n in names)
    assert experiments.SUBSETS["dev2"] == experiments.DEV2 and experiments.SUBSETS["dev"] == experiments.DEV


def test_old_labels_carry_no_design():
    for rec in SUITES["smoke"](1) + SUITES["full"](1):
        assert not rec.per_station_only
        entries = labels(rec.specs, rec.sample_rate, rec.duration_s, rec.noise_seed)["signals"]
        if rec.station_labels:
            entries += station_labels(rec.specs, rec.sample_rate, rec.duration_s, rec.noise_seed)["signals"]
        assert all("design" not in e for e in entries), rec.name


def test_generate_writes_dev2_with_per_cell_and_groups(tmp_path):
    suites.main(["generate", "--suite", "dev2", "--out", str(tmp_path), "--per-cell", "F2=1", "--groups", "F2"])
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert manifest["suite"] == "dev2"
    assert len(manifest["recordings"]) == 20
    lab = json.loads((tmp_path / "F2-drift-c10-0-s1.json").read_text())
    assert len(lab["signals"]) == 3 and all(e["design"]["speed_wpm"]["cell"] == 10 for e in lab["signals"])
    assert sorted(e["design"]["drift_excursion_hz"]["range"] for e in lab["signals"]) == [[0.0, 12.0], [12.0, 25.0],
                                                                                         [25.0, 40.0]]
    assert "per_station" not in manifest["recordings"][0]


def test_h2_writes_and_scores_only_per_station_labels(tmp_path):
    suites.main(["generate", "--suite", "dev2", "--out", str(tmp_path), "--per-cell", "H2=1", "--groups", "H2"])
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert len(manifest["recordings"]) == 10
    for rec in manifest["recordings"]:
        assert rec["station_labels"] is None and rec["per_station"] is True
        assert suites._scorings(rec) == [(rec["labels"], rec["name"], rec["group"])]
        lab = json.loads((tmp_path / rec["labels"]).read_text())["signals"]
        assert len(lab) == 2 and [e["qso_index"] for e in lab] == [0, 0]  # one entry per station, no QSO entry
        assert abs(lab[1]["freq_offset_hz"] - lab[0]["freq_offset_hz"]) >= 200.0
    assert not list(tmp_path.glob("*.stations.json"))


def test_per_cell_and_groups_are_dev2_only(tmp_path):
    with pytest.raises(SystemExit):
        suites.main(["generate", "--suite", "smoke", "--out", str(tmp_path), "--groups", "A2"])
    with pytest.raises(SystemExit):
        suites.main(["generate", "--suite", "n2", "--out", str(tmp_path), "--per-cell", "A2=1"])


# --- N2, the noise-only group (docs/plans/2026-10-20-new-decoder-prototype-1.md, D8) ---

def test_n2_noise_group(dev2):
    """100 channels x 360 s (10 channel-hours) of white noise as 4 recordings of 25 channels at DEV2's 48 kHz, each
    channel opened by a labeled station at S500 -60 dB (far below any decodable level), reproducible by seed, and a
    suite of its own: it is not part of DEV2."""
    recs = suites.noise2(1)
    assert [r.name for r in recs] == [f"N2-noise-{k}-s1" for k in range(4)]
    assert SUITES["n2"](1) == recs and suites.noise2(1) == recs  # reproducible
    for r in recs:
        check_recording(r)
        assert (r.sample_rate, r.duration_s, r.oracle, r.group) == (48000, 360.0, True, "N2 noise only")
        assert len(r.specs) == 25 and not r.station_labels and not r.per_station_only
        freqs = sorted(s.freq_offset_hz for s in r.specs)
        # slots 1200 Hz apart, each moved by at most half a bin (11.7 Hz) and rounded to 0.1 Hz
        assert min(np.diff(freqs)) > suites.DEV2_SLOT_HZ - suites.BIN_HZ - 0.1
        assert abs(freqs[0]) < 0.45 * r.sample_rate and abs(freqs[-1]) < 0.45 * r.sample_rate
    specs = [s for r in recs for s in r.specs]
    assert sum(r.duration_s * len(r.specs) for r in recs) == 36000.0  # channel-seconds: 10 channel-hours
    assert {s.snr_db for s in specs} == {-60.0} and all(s.score and s.keying == "machine" for s in specs)
    assert all(95 <= text_chars(s.text) <= 105 for s in specs)
    # ten stations in each of DEV2's speed cells, spread over the recordings (no cell is tied to a recording)
    assert Counter(s.design["speed_wpm"]["cell"] for s in specs) == Counter({c: 10 for c in SPEED_CELLS.numbers})
    for r in recs:
        assert set(Counter(s.design["speed_wpm"]["cell"] for s in r.specs).values()) == {2, 3}
    for s in specs:
        lo, hi = SPEED_CELLS.bounds(s.design["speed_wpm"]["cell"])
        assert lo <= s.wpm <= hi and s.wpm == s.design["speed_wpm"]["value"]
        assert s.design["group"] == "N2" and s.design["s500_db"]["value"] == -60.0
    # the noise is DEV2's: sigma 0.02 FS per complex sample, and a station at -60 dB adds nothing to it
    assert DEFAULT_NOISE_SIGMA == 0.02
    iq = generate(recs[0].specs, recs[0].sample_rate, 2.0, seed=recs[0].noise_seed)
    assert np.sqrt(np.mean(np.abs(iq) ** 2)) == pytest.approx(0.02, rel=0.01)
    # the next group code after DEV2's (S2's noise uses 11): noise seeds 10^6 x seed + 10^4 x 12 + the recording's
    # number
    assert [r.noise_seed for r in recs] == [suites.DEV2_SEED_BASE + 12 * suites.DEV2_GROUP_SEED + k for k in range(4)]
    assert not {r.noise_seed for r in recs} & ({r.noise_seed for r in dev2} | {r.noise_seed for r in SUITES["full"](3)})
    other = suites.noise2(2)
    assert [r.name for r in other] == [f"N2-noise-{k}-s2" for k in range(4)]
    assert {s.text for r in other for s in r.specs}.isdisjoint({s.text for s in specs})
    # not in DEV2: its default groups, its analysis's test cases
    assert not any(r.group == "N2 noise only" for r in dev2)
    assert not any(re.search(experiments.DEV2, r.name) or re.search(experiments.DEV, r.name) for r in recs)
    assert "N2" not in suites.DEV2_BUILDERS and "N2" not in suites.DEV2_PER_CELL
    # its labels carry the drawn values like DEV2's, and the one fixed value
    entry = labels(recs[0].specs, recs[0].sample_rate, recs[0].duration_s, recs[0].noise_seed)["signals"][0]
    assert entry["design"]["speed_wpm"]["unit"] == "WPM" and entry["design"]["s500_db"]["value"] == -60.0
    assert entry["end_s"] < 360.0


def test_the_n2_suite_is_generated_by_name(tmp_path, monkeypatch):
    seen = []
    monkeypatch.setattr(suites, "write_suite", lambda recs, out, name, only=None: seen.append((name, recs, out)))
    suites.main(["generate", "--suite", "n2", "--out", str(tmp_path)])
    ((name, recs, out),) = seen
    assert name == "n2" and out == tmp_path and [r.name for r in recs] == [f"N2-noise-{k}-s1" for k in range(4)]
    seen.clear()
    suites.main(["generate", "--suite", "n2", "--seeds", "2", "--out", str(tmp_path)])
    assert len(seen[0][1]) == 8
