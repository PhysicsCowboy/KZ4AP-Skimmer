import json
import math

import pytest

from kz4ap_proto import metrics


def suite(tmp_path, label, channel, windows=(2.0, 5.0, 10.0)):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "a", "group": "A sensitivity", "oracle": True, "wav": "a.wav", "labels": "a.json",
         "station_labels": None}]}))
    (tmp_path / "a.json").write_text(json.dumps({"signals": [label]}))
    (tmp_path / "proto" / "p").mkdir(parents=True)
    (tmp_path / "proto" / "p" / "a.decoded.json").write_text(json.dumps({
        "front_end": "p", "config": {"periodicity_windows_s": list(windows)}, "texts": [channel["text"]],
        "channels": [{"label_index": 0, "rate_hz": 1500.0, "cpu_s": 1.0, "channel_s": 60.0, **channel}]}))


LABEL = {"text": "CQ", "wpm": 25.0, "snr_db": 10.0, "start_s": 0.0, "end_s": 30.0, "score": True,
         "transmissions": [{"text": "CQ", "start_s": 0.0, "end_s": 30.0}]}


def test_speed_errors_and_lock_ins(tmp_path):
    right, wrong = 0.048, 0.096
    selections = [[t, 15, right] for t in range(4, 10)] + [[t, 25, wrong] for t in range(10, 15)]
    suite(tmp_path, LABEL, {"text": "CQ", "selections": selections, "over_starts": [], "chars": [], "periodicity": []})
    s = metrics.speed_errors(tmp_path, "p")["A sensitivity"]
    assert s["instants"] == 11 and s["error_fraction"] == pytest.approx(5 / 11) and s["lock_ins"] == 1


def test_switches_alternations_over_starts_and_false_characters(tmp_path):
    selections = [[1.0, 0, None], [2.0, 5, 0.03], [3.0, 0, None], [20.0, 7, 0.05]]
    chars = [["E", 31.0, 31.05], ["T", 10.0, 10.1], [" ", 31.1, 31.1]]
    suite(tmp_path, LABEL, {"text": "E T", "selections": selections, "over_starts": [0.5, 12.0], "chars": chars,
                            "periodicity": []})
    sw = metrics.switch_stats(tmp_path, "p")["A sensitivity"]
    assert sw["switches"] == 3 and sw["alternations"] == 1
    assert sw["switches_per_min"] == pytest.approx(3 / 0.5)
    assert metrics.spurious_over_starts(tmp_path, "p")["A sensitivity"]["per_transmission"] == pytest.approx(1.0)
    fc = metrics.false_characters(tmp_path, "p")["A sensitivity"]
    assert fc["characters"] == 1 and fc["per_min"] == pytest.approx(1 / ((60.0 - 30.5) / 60.0))
    assert metrics.cpu_per_channel_second(tmp_path, "p") == pytest.approx(1 / 60)


def test_the_shortest_confident_window_rule_is_scored_against_the_true_dit():
    truth = 0.048
    points = [{"unit": ("a", 0), "tx": 0, "truth": truth, "since_start": float(t),
               "per": [(None, 0.0) if t < 2 else (0.1, 0.02), (0.049, 0.3), (0.048, 0.4)]} for t in range(10)]
    r = metrics.evaluate_rule(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.05)
    assert r["confident"] == 10 and r["precision"] == pytest.approx(1.0) and r["coverage"] == pytest.approx(1.0)
    r = metrics.evaluate_rule(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.01)
    assert r["precision"] == pytest.approx(2 / 10)  # the 2 s window's wrong estimates now count as confident
    threshold, best = metrics.calibrate(points, (2.0, 5.0, 10.0), (2.0, 5.0, 10.0), 0.95)
    assert threshold > 0.02 and best["precision"] >= 0.95
    assert metrics.true_dit_s({**LABEL, "wpm_end": 35.0}) is None
    assert metrics.true_dit_s({**LABEL, "senders": [{"wpm": 20.0}, {"wpm": 25.0}]}) is None
    assert metrics.true_dit_s(LABEL) == pytest.approx(0.048)


def test_periodicity_points_from_the_decoders_own_records(tmp_path):
    # [t, T_P, confidence, window, [(T, score) per window, shortest first]]; points only inside the transmission
    per = [[t, 0.048, 0.4, 2.0, [[0.048, 0.4], [None, 0.0], [0.096, 0.2]]] for t in (0.25, 10.0, 29.75, 31.0)]
    suite(tmp_path, LABEL, {"text": "CQ", "selections": [], "over_starts": [], "chars": [], "periodicity": per})
    points = metrics.periodicity_points_decoded(tmp_path, "p")
    assert [p["since_start"] for p in points] == [0.25, 10.0, 29.75]
    assert points[0]["per"] == [(0.048, 0.4), (None, 0.0), (0.096, 0.2)]
    assert points[0]["truth"] == pytest.approx(0.048) and points[0]["unit"] == ("a", 0)
    r = metrics.evaluate_rule(points, (0, 1, 2), (0, 1, 2), 0.03)
    assert r["confident"] == 3 and r["precision"] == pytest.approx(1.0)
    assert metrics.periodicity_points_decoded(tmp_path, "p", min_snr_db=20.0) == []


def test_periodicity_by_speed_bins_the_true_dit():
    points = [{"truth": 0.048, "per": [(0.048, 0.04), (0.096, 0.5)]},
              {"truth": 0.048, "per": [(0.030, 0.01), (0.048, 0.5)]},
              {"truth": 0.1, "per": [(None, 0.0), (0.1, 0.2)]}]
    rows = {(r["bin"], r["window"]): r for r in metrics.periodicity_by_speed(points, 0.03)}
    r25 = rows[((22.0, 30.0), 0)]
    assert r25["points"] == 2 and r25["confident"] == pytest.approx(0.5) and r25["precision"] == pytest.approx(1.0)
    rule = rows[((22.0, 30.0), "rule")]
    assert rule["confident"] == pytest.approx(1.0) and rule["precision"] == pytest.approx(1.0)
    assert rows[((5.0, 15.0), 1)]["precision"] == pytest.approx(1.0)
    assert rows[((30.0, 50.0), 0)]["points"] == 0 and rows[((30.0, 50.0), 0)]["confident"] is None


def test_stretch_measures_pair_signals_and_shift_the_crossing():
    # Original CER per S500 step 0.5, 0.3, 0.05, 0.0 at -2, 0, 2, 4 dB: crosses 0.10 at 0 + (0.3 - 0.1) / 0.25 * 2 =
    # 1.6 dB. The stretched copies are one step (2 dB) worse: 0.8, 0.5, 0.3, 0.05 at the same steps, crossing at 3.6 dB
    # of the original's S500, which is 3.6 - 3.1875 dB on the stretched copies' own axis: shift 3.1875 - 2 dB.
    step = 10 * math.log10(25 / 12)
    orig = {-2.0: 0.5, 0.0: 0.3, 2.0: 0.05, 4.0: 0.0}
    worse = {-2.0: 0.8, 0.0: 0.5, 2.0: 0.3, 4.0: 0.05}
    pairs = [{"snr_db": snr, "orig": (round(100 * orig[snr]), 100), "stretched": (round(100 * worse[snr]), 100)}
             for snr in orig for _ in range(3)]
    m = metrics.stretch_measures(pairs, step)
    assert m["crossing_orig_db"] == pytest.approx(1.6) and not m["crossing_orig_upper_bound"]
    assert m["crossing_stretched_db"] == pytest.approx(3.6 - step)
    assert m["shift_db"] == pytest.approx(step - 2.0)
    lo, hi = m["shift_interval"]
    assert lo == pytest.approx(step - 2.0) and hi == pytest.approx(step - 2.0)  # identical pairs within a step
    assert m["by_snr"][0.0]["mean"] == pytest.approx(0.2) and m["by_snr"][0.0]["pairs"] == 3
    assert m["by_snr"][0.0]["orig"] == pytest.approx(0.3) and m["by_snr"][0.0]["stretched"] == pytest.approx(0.5)
    assert m["all"]["mean"] == pytest.approx((0.3 + 0.2 + 0.25 + 0.05) / 4) and m["all"]["pairs"] == 12
    # An invariant decoder: the stretched copies' CER equals the original's step for step, so the shift is the step.
    same = [{**p, "stretched": p["orig"]} for p in pairs]
    m = metrics.stretch_measures(same, step)
    assert m["shift_db"] == pytest.approx(step) and m["all"]["mean"] == 0.0
    # The paired CER is per signal (edits / symbols of each), not pooled: 1/10 - 0/10 and 0/100 - 10/100.
    m = metrics.stretch_measures([{"snr_db": 0.0, "orig": (0, 10), "stretched": (1, 10)},
                                  {"snr_db": 0.0, "orig": (10, 100), "stretched": (0, 100)}], step)
    assert m["all"]["mean"] == pytest.approx(0.0) and m["by_snr"][0.0]["stretched"] == pytest.approx(1 / 110)
    assert m["shift_db"] is None  # fewer than three steps: no crossing


def test_stretch_crossings_mark_upper_bounds_and_drop_empty_pairs():
    step = 3.0
    pairs = [{"snr_db": snr, "orig": (0, 100), "stretched": (0, 100)} for snr in (0.0, 2.0, 4.0) for _ in range(2)]
    pairs.append({"snr_db": 4.0, "orig": (0, 0), "stretched": (5, 10)})  # no symbols: left out
    m = metrics.stretch_measures(pairs, step)
    assert m["crossing_orig_db"] == 0.0 and m["crossing_orig_upper_bound"]
    assert m["crossing_stretched_db"] == -3.0 and m["crossing_stretched_upper_bound"]
    assert m["all"]["pairs"] == 6


QSO = {"text": "A B C D", "start_s": 1.0, "end_s": 40.0, "score": True, "transmissions": [
    {"text": "A", "start_s": 1.0, "end_s": 10.0, "sender_index": 0},
    {"text": "B", "start_s": 11.0, "end_s": 20.0, "sender_index": 1},
    {"text": "C", "start_s": 21.5, "end_s": 30.0, "sender_index": 0},
    {"text": "D", "start_s": 31.0, "end_s": 40.0, "sender_index": 0}]}


def test_new_over_counts_false_overs_missed_turnovers_and_same_station_silences():
    # 10.6: the turnover to B; 12.5: inside B, false, and it is within 2 s of B's start; 21.55: 50 ms after C's first
    # key-down (within the 0.1 s settle: C's own start, not false); 35.0: inside D, false; nothing in the silence
    # before D (same station).
    c = metrics.new_over_counts(QSO, [10.6, 12.5, 21.55, 35.0])
    assert c == {"transmissions": 4, "false_new_overs": 2, "turnovers": 2, "missed_turnovers": 0,
                 "same_station_gaps": 1, "same_station_new_overs": 0}
    # Nothing after B's end until 23.6 s, more than 2 s after C's first key-down: C's turnover is missed.
    c = metrics.new_over_counts(QSO, [10.6, 23.6, 30.5])
    assert (c["missed_turnovers"], c["false_new_overs"], c["same_station_new_overs"]) == (1, 1, 1)
    # A label without senders (the pauses group's repeats): every silence is the same station's.
    rep = {"start_s": 0.0, "end_s": 30.0, "transmissions": [{"start_s": 0.0, "end_s": 10.0},
                                                             {"start_s": 20.0, "end_s": 30.0}]}
    c = metrics.new_over_counts(rep, [10.6, 15.0, 25.0])
    assert (c["turnovers"], c["same_station_gaps"], c["same_station_new_overs"], c["false_new_overs"]) == (0, 1, 1, 1)


def test_first_word_by_over_names_the_silence_before_each_over():
    sig = {"transmissions": [{"first_word_symbols": 2, "first_word_edits": k} for k in range(4)]}
    got = metrics.first_word_by_over(QSO, sig)
    assert [g["kind"] for g in got] == ["first", "turnover", "turnover", "same station"]
    assert [(g["edits"], g["symbols"]) for g in got] == [(0, 2), (1, 2), (2, 2), (3, 2)]
