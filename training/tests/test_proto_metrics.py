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
