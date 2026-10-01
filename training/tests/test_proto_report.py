import json

import pytest

from kz4ap_proto import report
from kz4ap_synth.suites import COUNT_KEYS


def test_verdict_calls_better_or_worse_only_when_the_interval_excludes_zero():
    assert report.verdict((-0.2, -0.1)) == "better"
    assert report.verdict((0.1, 0.2)) == "worse"
    assert report.verdict((-0.1, 0.1)) == "unchanged"
    assert report.verdict((0.0, 0.1)) == "unchanged"  # touching 0 does not exclude it
    assert report.verdict(None) == "no interval"


def test_not_comparable_marks_oracle_drift_and_off_mix_qso_labels_only():
    assert "drift" in report.not_comparable("F tuning", "drift 0.5 Hz/s")
    assert report.not_comparable("F tuning", "offset 2.9 Hz 20 wpm") is None
    assert report.not_comparable("H two-station QSO, oracle", "same-track, offset 0 Hz") is None
    assert "answering station" in report.not_comparable("H two-station QSO, oracle", "separate-track, offset 100 Hz")
    assert report.not_comparable("H two-station QSO", "separate-track, offset 100 Hz") is None  # detector path


def row(fe, group, tag, recording, index, edits, symbols=100):
    return {"front_end": fe, "recording": recording, "group": group, "tag": tag, "index": index, "snr_db": 10.0,
            "scored": True, "detected": True, "freq_error_hz": None, "regime": None, "beyond_oracle_anchor": False,
            **{k: 0 for k in COUNT_KEYS}, "symbols": symbols, "edits": edits}


def test_comparison_counts_leave_out_the_not_comparable_rows(tmp_path):
    rows = []
    for i in range(4):  # group A over two recordings: the prototype 0.20, both references 0.10 on every signal
        rec = f"a{i % 2}"
        rows += [row("p", "A sensitivity", "25 wpm", rec, i, 20), row("matched", "A sensitivity", "25 wpm", rec, i, 10),
                 row("baseline", "A sensitivity", "25 wpm", rec, i, 10)]
        # F drift: the prototype clearly better, but not comparable
        rows += [row(fe, "F tuning", "drift 1 Hz/s", "f", i, e) for fe, e in (("p", 0), ("matched", 30), ("baseline", 30))]
    comp = report.comparison(rows, "p", tmp_path)
    by = {(e["group"], e["tag"]): e for e in comp["regimes"]}
    assert by[("A sensitivity", "25 wpm")]["matched"]["mean"] == pytest.approx(0.10)
    assert by[("A sensitivity", "25 wpm")]["matched"]["verdict"] == "worse"
    assert by[("A sensitivity", "25 wpm")]["recordings"] == 2
    assert by[("F tuning", "drift 1 Hz/s")]["baseline"]["verdict"] == "better"
    assert by[("F tuning", "drift 1 Hz/s")]["not_comparable"]
    for ref in ("matched", "baseline"):
        assert comp["counts"][ref] == {"better": 0, "worse": 1, "unchanged": 0, "no interval": 0}
        assert comp["expected_by_chance"][ref] == pytest.approx(0.05 * 1)


def test_write_report_states_coverage_statistics_and_the_detector_path(tmp_path):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "a", "group": "A sensitivity", "oracle": True, "wav": "a.wav", "labels": "a.json",
         "station_labels": None},
        {"name": "band-s1", "group": "band", "oracle": False, "wav": "band-s1.wav", "labels": "band-s1.json",
         "station_labels": None}]}))
    label = {"text": "CQ", "freq_offset_hz": 1000.0, "start_s": 0.5, "end_s": 2.0, "wpm": 25.0, "snr_db": 10.0}
    (tmp_path / "a.json").write_text(json.dumps({"signals": [{**label, "tag": "25 wpm"}] * 2}))
    (tmp_path / "band-s1.json").write_text(json.dumps({"signals": [{**label, "tag": "band"}]}))

    def signal(index, edits):
        return {"index": index, "snr_db": 10.0, "scored": True, "track_id": 1, "symbols": 10, "edits": edits,
                "freq_offset_hz": 1000.0, "cer": edits / 10, **{k: 0 for k in COUNT_KEYS[2:]}}

    for fe, edits in (("p", 1), ("matched", 2), ("baseline", 3)):
        d = tmp_path / "results" / fe
        d.mkdir(parents=True)
        (d / "a.json").write_text(json.dumps({"score": {"signals": [signal(0, edits), signal(1, edits)]}}))
        (d / "band-s1.json").write_text(json.dumps({"tracks": [], "score": {
            "scored": 1, "detected": 1, "false_tracks": int(fe == "baseline"), "signals": [signal(0, edits)]}}))
    path = report.write_report(tmp_path, "p")
    text = path.read_text(encoding="utf-8")
    assert "Same oracle signals" not in text and "Envelope's detector opens its own tracks" in text
    # available: a, band-s1 (detector path) and band-s1.oracle (band's oracle copy, no result here): 3 scorings, 4 labels
    assert "Coverage: p has results for 2 of 3 scorings (3 of 4 labels)." in text
    assert "within-recording correlation is not modeled" in text and "about 0.05·R" in text
    assert "would read better or worse by chance" in text
    assert "| 25 wpm | 1 | 2 |" in text  # tag, recordings, signals
    assert "Tracks per QSO" not in text  # no group H: the table is left out
    assert "| band | Envelope | 1 | 1 | 1 | 1.000 | 1 |" in text
    saved = json.loads(path.with_suffix(".json").read_text())
    assert saved["coverage"] == {"scorings": 2, "scorings_available": 3, "labels": 3, "labels_available": 4}
    assert saved["comparison"]["counts"]["matched"]["better"] == 1
