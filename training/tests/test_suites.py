import json
import subprocess
from collections import defaultdict

import numpy as np
import pytest

from kz4ap_synth.generate import Sender, SignalSpec, qso_spec, scenario_band
from kz4ap_synth.messages import Over
from kz4ap_synth import suites
from kz4ap_synth.morse import symbols
from kz4ap_synth.suites import (
    SUITES,
    VE3NEA_SNR_DB,
    Recording,
    aggregate,
    check_recording,
    crossing_snr,
    format_markdown,
    over_rows,
    paired_differences,
    qso_regime,
    run_suite,
    view_fits,
    track_splits,
    write_suite,
    write_summary,
)


def test_smoke_suite_is_the_smoke_script_recording():
    (rec,) = SUITES["smoke"](1)
    assert rec.specs == scenario_band(np.random.default_rng(1), 8, 30.0, 192000)
    assert (rec.sample_rate, rec.duration_s, rec.noise_seed, rec.oracle) == (192000, 30.0, 2, False)


def test_full_suite_recordings_are_valid_and_uniquely_named():
    recs = SUITES["full"](1)
    names = [r.name for r in recs]
    assert len(names) == len(set(names)) == 41
    for r in recs:
        check_recording(r)
    assert {r.group for r in recs} == {
        "A sensitivity", "B fading", "C fists", "D speed", "E interference", "F tuning",
        "G ragchew", "H two-station QSO", "H two-station QSO, oracle", "I Farnsworth", "strong", "pauses",
        "tune-up", "first sample", "crowded", "band"}


def test_full_suite_covers_the_scenarios():
    recs = SUITES["full"](1)
    specs = [s for r in recs for s in r.specs]
    assert max(s.snr_db for s in specs if s.score) == 60.0
    assert min(s.snr_db for s in specs) == -10.0
    assert set(VE3NEA_SNR_DB) <= {s.snr_db for s in specs if s.fading_hz > 0}
    assert {s.fading_hz for s in specs} >= {0.1, 0.3, 1.0, 3.0}
    assert {s.keying for s in specs} == {"machine", "computer", "paddle", "bug", "hand"}
    assert {s.fading_shape for s in specs if s.fading_hz > 0} == {"butterworth"}
    assert all(s.edges_centered and s.edge_s == 0.002 for s in specs if s.fading_hz > 0)
    assert not any(s.edges_centered for s in specs if s.fading_hz == 0)
    assert any(not s.score for s in specs)
    assert {s.pause_s for s in specs if s.repeats > 1} == {2.0, 5.0, 10.0, 20.0}
    assert {s.tune_s for s in specs if s.tune_s > 0} == {0.3, 0.6, 1.0, 2.0}
    assert {s.drift_hz_per_s for s in specs if s.drift_hz_per_s} == {0.2, 0.5, 1.0, 2.0}
    assert any(s.start_s == 0.0 for s in specs)
    assert min(s.wpm for s in specs) <= 10.0 and max(s.wpm for s in specs) >= 60.0
    assert any(s.wpm_end is not None for s in specs)
    qsos = [s for s in specs if s.overs]
    assert {s.senders[1].offset_hz for s in qsos} >= {0.0, 10.0, 25.0, 50.0, 100.0, 200.0}
    assert {qso_regime(s.senders[1].offset_hz) for s in qsos} == {"same-track", "ambiguous", "separate-track"}
    assert any(s.senders[0].wpm != s.senders[1].wpm for s in qsos)
    assert all(s.overs[-1].text.endswith("<SK>") for s in qsos)
    assert all(r.station_labels for r in recs if r.group.startswith("H "))


def test_points_hold_enough_characters_for_three_seeds():
    # Target: at least 1000 characters (spaces excluded) per S500 point with --seeds 3, so
    # seed 1 alone must hold a third of that in groups A, B and C.
    chars = defaultdict(int)
    for r in SUITES["full"](1):
        if r.group in ("A sensitivity", "B fading", "C fists"):
            for s in r.specs:
                chars[(r.group, s.tag, s.snr_db)] += len([c for c in symbols(s.text) if c != " "])
    assert min(chars.values()) >= 1000 / 3


def test_slow_fading_points_span_a_hundred_fade_times_over_three_seeds():
    # f_D = 0.1 Hz decorrelates (1/e) in about 4.5 s: 100 fades need 450 station-seconds per point.
    seconds = defaultdict(float)
    for r in SUITES["full"](1):
        for s in r.specs:
            if s.fading_hz == 0.1:
                seconds[(s.tag, s.snr_db)] += r.duration_s
    assert 3 * min(seconds.values()) >= 450.0


def test_more_seeds_make_more_recordings():
    assert len(SUITES["full"](2)) == 2 * len(SUITES["full"](1))


def test_qso_regimes_follow_the_detector_bins():
    assert [qso_regime(df) for df in (0.0, 10.0, 25.0, 46.0)] == ["same-track"] * 4
    assert [qso_regime(df) for df in (50.0, -60.0)] == ["ambiguous"] * 2
    assert [qso_regime(df) for df in (71.0, 100.0, -200.0)] == ["separate-track"] * 3


def test_qso_regimes_follow_the_front_ends_attribution_rule():
    # Matched: the channel distance D_ch = 47 Hz; ambiguous up to D_ch + 2 bins (93.875 Hz),
    # where interpolated frequencies, each up to one bin from its carrier, can read closer than D_ch.
    assert [qso_regime(df, "matched") for df in (0.0, 25.0, 46.9, -46.99)] == ["same-track"] * 4
    assert [qso_regime(df, "matched") for df in (47.0, 50.0, -70.2, 93.8)] == ["ambiguous"] * 4
    assert [qso_regime(df, "matched") for df in (93.9, 100.0, -200.0)] == ["separate-track"] * 3
    # The Envelope path keeps milestone 1's 3-bin rule (and is the default, as in the tags).
    assert [qso_regime(df, "baseline") for df in (46.9, 80.0)] == ["ambiguous", "separate-track"]
    assert qso_regime(80.0) == "separate-track"
    with pytest.raises(ValueError):
        qso_regime(10.0, "neural")


def test_views_are_judged_by_the_regimes_on_the_rows_path():
    assert not view_fits("H two-station QSO", "separate-track, drawn offset", ["separate-track"])
    assert view_fits("H two-station QSO", "separate-track, drawn offset", ["ambiguous"])
    assert not view_fits("H two-station QSO (per station)", "ambiguous, drawn offset", ["same-track"])
    # A row mixing regimes fits both views, like an ambiguous one.
    assert view_fits("H two-station QSO", "ambiguous, drawn offset", ["same-track", "separate-track"])
    assert view_fits("H two-station QSO (per station)", "ambiguous, drawn offset",
                     ["same-track", "separate-track"])


def test_summary_marks_group_h_views_by_each_front_ends_regime(tmp_path):
    # 80 Hz: separate-track on the Envelope path (3 bins = 70.3 Hz), ambiguous on the Matched
    # path (below 93.9 Hz). 46.95 Hz: ambiguous on Envelope (above 2 bins = 46.875 Hz),
    # same-track on Matched (below 47 Hz).
    rec = Recording("qso", "H two-station QSO", 8000, 12.0, 5, False, [_two_station(80.0), _two_station(46.95)],
                    True)
    write_suite([rec], tmp_path, "test")
    for fe in ("baseline", "matched"):
        results = tmp_path / "results" / fe
        results.mkdir(parents=True)
        (results / "qso.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0), _fake_signal(1)]}}))
        (results / "qso.stations.json").write_text(json.dumps({"score": {"signals": [
            _fake_signal(i) for i in range(4)]}}))
    write_summary(tmp_path)
    lines = (tmp_path / "summary.md").read_text(encoding="utf-8").splitlines()
    qso_rows = lines[lines.index("## H two-station QSO"):lines.index("## H two-station QSO (per station)")]
    start = lines.index("## H two-station QSO (per station)")
    station_rows = lines[start:next(i for i in range(start + 1, len(lines)) if lines[i].startswith("## "))]
    assert any(line.startswith("| separate-track, offset 80 Hz † | baseline |") for line in qso_rows)
    assert any(line.startswith("| separate-track, offset 80 Hz (on this path: ambiguous) | matched |")
               for line in qso_rows)
    assert any(line.startswith("| ambiguous, offset 46.95 Hz | baseline |") for line in station_rows)
    assert any(line.startswith("| ambiguous, offset 46.95 Hz † (on this path: same-track) | matched |")
               for line in station_rows)
    groups = {(g["group"], g["front_end"], g["tag"]): g for g in json.loads((tmp_path / "summary.json").read_text())[
        "groups"]}
    assert groups[("H two-station QSO", "matched", "separate-track, offset 80 Hz")]["regimes"] == ["ambiguous"]
    assert groups[("H two-station QSO", "baseline", "separate-track, offset 80 Hz")]["regimes"] == [
        "separate-track"]


def test_views_that_do_not_fit_the_regime_are_marked():
    assert not view_fits("H two-station QSO (per station)", "same-track, offset 0 Hz")
    assert view_fits("H two-station QSO (per station)", "separate-track, offset 100 Hz")
    assert not view_fits("H two-station QSO", "separate-track, drawn offset")
    assert not view_fits("H two-station QSO, oracle", "separate-track, offset 200 Hz")
    assert view_fits("H two-station QSO", "ambiguous, offset 50 Hz")
    assert view_fits("A sensitivity", "separate-track")
    # Oracle channels: judged by the channel's passband (-6 dB at 150 Hz, measured), not the detector's bins.
    assert view_fits("H two-station QSO, oracle", "separate-track, offset 100 Hz")
    assert not view_fits("H two-station QSO, oracle (per station)", "separate-track, offset 100 Hz")
    assert view_fits("H two-station QSO, oracle (per station)", "separate-track, offset 200 Hz")
    assert not view_fits("H two-station QSO, oracle (per station)", "same-track, offset 0 Hz")
    assert view_fits("H two-station QSO, oracle", "ambiguous, offset 50 Hz")
    assert not view_fits("H two-station QSO, oracle (per station)", "ambiguous, offset 50 Hz")


def test_check_rejects_a_signal_that_runs_past_the_end():
    rec = Recording("x", "g", 8000, 1.0, 1, True, [SignalSpec("CQ CQ CQ", 100.0, 20.0, 10.0, 0.5)])
    with pytest.raises(ValueError):
        check_recording(rec)


def test_crossing_interpolates_between_bracketing_points():
    points = [(-4.0, 0.9), (-2.0, 0.4), (0.0, 0.08), (2.0, 0.02), (4.0, 0.0)]
    assert crossing_snr(points, 0.10) == pytest.approx(-2.0 + (0.4 - 0.10) / (0.4 - 0.08) * 2.0)


def test_crossing_starts_from_the_highest_failing_point():
    points = [(-2.0, 0.05), (0.0, 0.3), (2.0, 0.02)]
    assert crossing_snr(points, 0.10) == pytest.approx(0.0 + (0.3 - 0.10) / (0.3 - 0.02) * 2.0)


def test_crossing_is_none_when_never_reached_and_lowest_point_when_always_below():
    assert crossing_snr([(0.0, 0.5), (2.0, 0.3)], 0.10) is None
    assert crossing_snr([(0.0, 0.01), (2.0, 0.0)], 0.10) == 0.0


def _row(snr, symbols_, edits, scored=True, front_end="baseline", index=0, freq_error=None):
    return {"front_end": front_end, "recording": "r", "group": "A", "tag": "25 wpm", "index": index,
            "snr_db": snr, "scored": scored, "detected": True, "symbols": symbols_, "edits": edits,
            "chars": symbols_ - 2, "char_edits": edits, "spaces": 2, "space_edits": 0, "first_word_symbols": 2,
            "first_word_edits": 0, "nospace_symbols": symbols_ - 2, "nospace_edits": edits,
            "freq_error_hz": freq_error}


def test_aggregate_pools_symbols_and_skips_unscored_signals():
    agg = aggregate([_row(0.0, 10, 5), _row(2.0, 30, 3, index=1), _row(2.0, 100, 100, scored=False, index=2)])
    v = agg[("baseline", "A", "25 wpm")]
    assert v["signals"] == 2
    assert v["cer"] == pytest.approx(8 / 40)
    assert v["nospace_cer"] == pytest.approx(8 / 36)
    assert v["space_error_rate"] == 0.0
    assert v["cer_by_snr"] == [(0.0, 0.5), (2.0, 0.1)]
    assert v["min_symbols_per_point"] == 10


def test_bootstrap_interval_brackets_the_estimate_and_is_reproducible():
    rows = [_row(float(snr), 100, e, index=i) for i, (snr, e) in
            enumerate((s, e) for s in (0, 2, 4, 6) for e in (40 - 6 * s, 30 - 5 * s, 35 - 5 * s))]
    first = aggregate(rows)[("baseline", "A", "25 wpm")]
    again = aggregate(rows)[("baseline", "A", "25 wpm")]
    low, high = first["cer_interval"]
    assert low < first["cer"] < high
    assert first["cer_interval"] == again["cer_interval"]
    low, high = first["snr_at_cer_interval"]["0.1"]
    assert low <= first["snr_at_cer"]["0.1"] <= high


def test_no_crossing_when_a_point_holds_a_single_signal():
    # band and crowded draw S500 per signal: every point is one signal, so no crossing
    rows = [_row(float(snr), 100, e, index=i) for i, (snr, e) in enumerate(((0, 50), (2, 20), (4, 5), (6, 0)))]
    v = aggregate(rows)[("baseline", "A", "25 wpm")]
    assert v["snr_at_cer"] == {} and v["snr_at_cer_interval"] == {}


def test_no_interval_for_a_single_signal():
    v = aggregate([_row(0.0, 10, 5)])[("baseline", "A", "25 wpm")]
    assert v["cer_interval"] is None


def test_a_crossing_at_the_lowest_point_is_printed_as_an_upper_bound(tmp_path):
    rows = [_row(float(snr), 100, e, index=i) for i, (snr, e) in
            enumerate((s, e) for s in (0, 2, 4) for e in (8, 12))]  # CER 0.10 at every point
    v = aggregate(rows)[("baseline", "A", "25 wpm")]
    assert v["snr_at_cer"]["0.1"] == 0.0 and v["snr_at_cer_upper_bound"]["0.1"] is True
    assert v["snr_at_cer_upper_bound"]["0.05"] is False
    text = format_markdown(aggregate(rows), {})
    row = next(line for line in text.splitlines() if line.startswith("| 25 wpm |"))
    assert "| ≤ 0.0 |" in row and "(0.0–0.0)" not in row


def test_run_reports_a_bench_that_prints_nothing(tmp_path, monkeypatch):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "x", "group": "g", "oracle": False, "wav": "x.wav", "labels": "x.json", "station_labels": None}]}))
    monkeypatch.setattr(subprocess, "run", lambda *a, **k: subprocess.CompletedProcess(a, 0, "", ""))
    with pytest.raises(RuntimeError, match="printed nothing"):
        run_suite(tmp_path, tmp_path / "kz4ap-bench", ["baseline"])


def test_aggregate_reports_the_median_frequency_error():
    rows = [_row(0.0, 10, 0, freq_error=1.0), _row(2.0, 10, 0, index=1, freq_error=3.0),
            _row(4.0, 10, 0, index=2)]
    assert aggregate(rows)[("baseline", "A", "25 wpm")]["freq_error_hz_median"] == pytest.approx(2.0)
    assert aggregate([_row(0.0, 10, 0)])[("baseline", "A", "25 wpm")]["freq_error_hz_median"] is None


def test_paired_differences_compare_the_same_signals():
    rows = [_row(0.0, 100, 10, index=i) for i in range(4)]
    rows += [_row(0.0, 100, 5, front_end="matched", index=i) for i in range(4)]
    d = paired_differences(rows)[("A", "25 wpm")]
    assert d["signals"] == 4
    assert d["mean"] == pytest.approx(-0.05)
    assert d["interval"] == pytest.approx((-0.05, -0.05))


def _fake_signal(index, scored=True, **counts):
    base = {"index": index, "snr_db": 10.0, "wpm": 25.0, "scored": scored, "track_id": 1, "symbols": 2,
            "edits": 1, "chars": 2, "char_edits": 1, "spaces": 0, "space_edits": 0, "first_word_symbols": 2,
            "first_word_edits": 1, "nospace_symbols": 2, "nospace_edits": 1, "transmissions": []}
    return {**base, **counts}


def test_write_suite_and_summary_round_trip(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True,
                    [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5, tag="25 wpm")])
    write_suite([rec], tmp_path, "test")
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert manifest["recordings"][0]["oracle"] is True
    assert manifest["recordings"][0]["station_labels"] is None
    assert (tmp_path / "tiny.wav").exists()
    results = tmp_path / "results" / "baseline"
    results.mkdir(parents=True)
    (results / "tiny.json").write_text(json.dumps({
        "channel_seconds": 3.0, "timing": {"cpu_s": 0.03, "decoder_ms_per_channel_s": 1.0},
        "score": {"signals": [_fake_signal(0)]}}))
    write_summary(tmp_path)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "## A sensitivity" in text
    # One signal: no bootstrap interval
    assert "| 25 wpm | baseline | 1 | 1 | 0.500 | 0.500 | 0.000 | 0.500 | 0.500 | 2 |" in text
    assert "| baseline | 3.0 | 10.000 | 1.000 |" in text


def _two_station(offset_hz):
    senders = [Sender("K1ABC", 25.0, "paddle"), Sender("W9XYZ", 30.0, "hand", offset_hz=offset_hz)]
    return qso_spec([Over(0, "CQ K1ABC"), Over(1, "K1ABC <KN>")], senders, 1000.0, 10.0, 0.5,
                    tag=f"{qso_regime(offset_hz)}, offset {offset_hz:g} Hz", turn_s=(0.5, 0.5))


def test_qsos_are_scored_per_over_per_station_and_for_track_splits(tmp_path):
    rec = Recording("qso", "H two-station QSO", 8000, 12.0, 5, False, [_two_station(100.0)], True)
    write_suite([rec], tmp_path, "test")
    entry = json.loads((tmp_path / "manifest.json").read_text())["recordings"][0]
    assert entry["station_labels"] == "qso.stations.json"
    stations = json.loads((tmp_path / "qso.stations.json").read_text())["signals"]
    assert [s["freq_offset_hz"] for s in stations] == [1000.0, 1100.0]
    results = tmp_path / "results" / "baseline"
    results.mkdir(parents=True)
    over_counts = [{"symbols": 8, "edits": 0, "first_word_symbols": 2, "first_word_edits": 0},
                   {"symbols": 7, "edits": 1, "first_word_symbols": 5, "first_word_edits": 1}]
    (results / "qso.json").write_text(json.dumps({
        "tracks": [{"id": 1, "freq_hz": 1001.0, "text": "CQ K1ABC"}, {"id": 2, "freq_hz": 1099.0, "text": "K1AEC"}],
        "score": {"signals": [_fake_signal(0, symbols=19, edits=1, transmissions=over_counts,
                                           tracked_freq_hz=1098.0)]}}))
    (results / "qso.stations.json").write_text(json.dumps({"score": {"signals": [
        _fake_signal(0, symbols=8, edits=0, tracked_freq_hz=1000.5),
        _fake_signal(1, symbols=10, edits=1, tracked_freq_hz=1099.0)]}}))
    rows = over_rows(tmp_path)
    assert [(r["sender"], r["keying"], r["symbols"], r["edits"]) for r in rows] == [
        ("K1ABC", "paddle", 8, 0), ("W9XYZ", "hand", 7, 1)]
    assert track_splits(tmp_path)[("baseline", "separate-track, offset 100 Hz")] == {"qsos": 1, "mean_tracks": 2.0}
    write_summary(tmp_path)
    summary = json.loads((tmp_path / "summary.json").read_text())
    by_group = {g["group"]: g for g in summary["groups"]}
    # The QSO label's error is measured against the last over's sender (W9XYZ at 1100 Hz);
    # each station label's against its own carrier.
    assert by_group["H two-station QSO"]["freq_error_hz_median"] == pytest.approx(2.0)
    assert by_group["H two-station QSO (per station)"]["freq_error_hz_median"] == pytest.approx(0.75)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "| H two-station QSO | hand | baseline | 1 | 0.143 |" in text
    assert "| separate-track, offset 100 Hz | baseline | 1 | 2.00 |" in text


def test_run_passes_the_front_end_to_the_bench_for_every_front_end(tmp_path, monkeypatch):
    # The bench's default is Matched, so the baseline must be asked for by name.
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "x", "group": "g", "oracle": False, "wav": "x.wav", "labels": "x.json", "station_labels": None}]}))
    calls = []

    def fake_run(cmd, *a, **k):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 0, "CER 0.0\n", "")

    monkeypatch.setattr(subprocess, "run", fake_run)
    run_suite(tmp_path, tmp_path / "kz4ap-bench", ["baseline", "matched"])
    assert [c[c.index("--decoder") + 1] for c in calls] == ["envelope", "matched"]
    assert all("--front-end" not in c for c in calls)


def test_run_scores_the_bank_decoder_into_results_bank(tmp_path, monkeypatch):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": "x", "group": "g", "oracle": True, "wav": "x.wav", "labels": "x.json", "station_labels": None}]}))
    calls = []
    monkeypatch.setattr(subprocess, "run",
                        lambda cmd, *a, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0.0\n", ""))
    run_suite(tmp_path, tmp_path / "kz4ap-bench", ["bank"])
    (cmd,) = calls
    assert cmd[cmd.index("--decoder") + 1] == "bank" and "--oracle" in cmd
    assert cmd[cmd.index("--json") + 1] == str(tmp_path / "results" / "bank" / "x.json")
    assert (tmp_path / "results" / "bank").is_dir()


def test_the_run_command_takes_decoder_and_its_old_name(tmp_path, monkeypatch):
    seen = []
    monkeypatch.setattr(suites, "run_suite", lambda out, bench, fes, only=None: seen.append(fes))
    suites.main(["run", "--out", str(tmp_path), "--bench", "b", "--decoder", "bank", "--front-end", "matched"])
    assert seen == [["bank", "matched"]]
    with pytest.raises(SystemExit):
        suites.main(["run", "--out", str(tmp_path), "--bench", "b", "--decoder", "neural"])


def test_the_bank_decoder_has_the_matched_paths_regimes():
    for df in (10.0, 50.0, 80.0, 93.8, 93.9, 150.0):
        assert qso_regime(df, "bank") == qso_regime(df, "matched")


def _drift_recording(tmp_path, oracle):
    specs = [SignalSpec("CQ CQ DE N8RA K", 1000.0, 25.0, 10.0, 0.5, drift_hz_per_s=d, tag=f"drift {d:g} Hz/s")
             for d in (0.2, 2.0)]
    rec = Recording("drift", "F tuning", 8000, 12.0, 5, oracle, specs)
    write_suite([rec], tmp_path, "test")
    for fe in ("baseline", "matched"):
        results = tmp_path / "results" / fe
        results.mkdir(parents=True)
        (results / "drift.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0), _fake_signal(1)]}}))


def test_matched_rows_beyond_the_oracle_anchor_are_marked_not_meaningful(tmp_path):
    # Oracle channels fix the tracker's anchor at the label; it covers +/-12 Hz. Over the
    # signal's length, a 2 Hz/s drift leaves that range and a 0.2 Hz/s drift does not.
    _drift_recording(tmp_path, oracle=True)
    labels = json.loads((tmp_path / "drift.json").read_text())["signals"]
    spans = [s["drift_hz_per_s"] * (s["end_s"] - s["start_s"]) for s in labels]
    assert spans[0] < 12.0 < spans[1]
    write_summary(tmp_path)
    lines = (tmp_path / "summary.md").read_text(encoding="utf-8").splitlines()
    marked = "| drift 2 Hz/s (not meaningful (oracle anchor)) | matched |"
    assert any(line.startswith(marked) for line in lines)
    assert any(line.startswith("| drift 2 Hz/s | baseline |") for line in lines)
    assert any(line.startswith("| drift 0.2 Hz/s | matched |") for line in lines)
    assert any(line.startswith("| F tuning | drift 2 Hz/s (not meaningful (oracle anchor)) | 1 |") for line in lines)
    assert any(line.startswith("| F tuning | drift 0.2 Hz/s | 1 |") for line in lines)


def test_the_anchor_limit_applies_only_to_oracle_recordings(tmp_path):
    _drift_recording(tmp_path, oracle=False)
    write_summary(tmp_path)
    assert "(not meaningful (oracle anchor))" not in (tmp_path / "summary.md").read_text(encoding="utf-8")


def test_oracle_qso_labels_with_the_answer_beyond_the_anchor_are_marked(tmp_path):
    rec = Recording("qso", "H two-station QSO, oracle", 8000, 12.0, 5, True,
                    [_two_station(10.0), _two_station(25.0)], True)
    write_suite([rec], tmp_path, "test")
    for fe in ("baseline", "matched"):
        results = tmp_path / "results" / fe
        results.mkdir(parents=True)
        (results / "qso.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0), _fake_signal(1)]}}))
        (results / "qso.stations.json").write_text(json.dumps({"score": {"signals": [
            _fake_signal(i) for i in range(4)]}}))
    write_summary(tmp_path)
    lines = (tmp_path / "summary.md").read_text(encoding="utf-8").splitlines()
    marked = [line for line in lines if line.startswith("|") and "(not meaningful (oracle anchor))" in line]
    # The QSO label's channel sits on the caller: the 25 Hz answer is beyond +/-12 Hz of it.
    # Each station label's channel sits on that station, so the per-station view is meaningful.
    assert any(line.startswith("| same-track, offset 25 Hz") and "| matched |" in line for line in marked)
    assert not any("offset 10 Hz" in line for line in marked)
    assert not any("| baseline |" in line for line in marked)
    by_group = {g["group"] + "/" + g["tag"] + "/" + g["front_end"]: g
                for g in json.loads((tmp_path / "summary.json").read_text())["groups"]}
    assert by_group["H two-station QSO, oracle/same-track, offset 25 Hz/matched"]["beyond_oracle_anchor"]
    assert not by_group["H two-station QSO, oracle (per station)/same-track, offset 25 Hz/matched"][
        "beyond_oracle_anchor"]


def test_summary_json_flags_the_oracle_anchor_only_on_matched_rows(tmp_path):
    _drift_recording(tmp_path, oracle=True)
    write_summary(tmp_path)
    groups = {(g["front_end"], g["tag"]): g for g in json.loads((tmp_path / "summary.json").read_text())["groups"]}
    assert groups[("matched", "drift 2 Hz/s")]["beyond_oracle_anchor"] is True
    assert groups[("baseline", "drift 2 Hz/s")]["beyond_oracle_anchor"] is False
    assert groups[("matched", "drift 0.2 Hz/s")]["beyond_oracle_anchor"] is False


def test_bank_rows_of_every_drifting_oracle_label_are_marked(tmp_path):
    # The engine's bank decoder mixes oracle channels at the label's frequency without its drift and has no
    # tracker at all: its rows beyond the anchor are no more meaningful than Matched's, and a row of any drifting
    # oracle label, 0.2 Hz/s included, is not comparable with the prototype, which mixed with the drift
    # (controller's ruling, milestone 2c Task 10). Matched keeps the +/-12 Hz rule.
    _drift_recording(tmp_path, oracle=True)
    results = tmp_path / "results" / "bank"
    results.mkdir(parents=True)
    (results / "drift.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0), _fake_signal(1)]}}))
    write_summary(tmp_path)
    lines = (tmp_path / "summary.md").read_text(encoding="utf-8").splitlines()
    assert any(line.startswith("| drift 2 Hz/s (not meaningful (oracle anchor)) | bank |") for line in lines)
    assert any(line.startswith("| drift 0.2 Hz/s (not meaningful (oracle anchor)) | bank |") for line in lines)
    assert any(line.startswith("| drift 0.2 Hz/s | matched |") for line in lines)
    groups = {(g["front_end"], g["tag"]): g for g in json.loads((tmp_path / "summary.json").read_text())["groups"]}
    assert groups[("bank", "drift 2 Hz/s")]["beyond_oracle_anchor"] is True
    assert groups[("bank", "drift 0.2 Hz/s")]["beyond_oracle_anchor"] is True
    assert groups[("matched", "drift 0.2 Hz/s")]["beyond_oracle_anchor"] is False
    assert groups[("baseline", "drift 2 Hz/s")]["beyond_oracle_anchor"] is False


def test_corrections_are_counted_per_group_once_per_engine_run(tmp_path):
    from kz4ap_synth.suites import correction_stats
    # A detector-path QSO recording with station labels (one engine run, two result files) and an oracle recording.
    qso = Recording("qso", "H two-station QSO", 8000, 12.0, 5, False, [_two_station(100.0)], True)
    ora = Recording("ora", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([qso, ora], tmp_path, "test")
    bank = tmp_path / "results" / "bank"
    bank.mkdir(parents=True)

    def result(reaches, seconds):
        tracks = [{"id": 1, "freq_hz": 0.0, "text": "", "text_immediate": "",
                   "corrections": [{"t_s": 5.0, "reach_s": r, "reason": "switch" if r < 2 else "rekey"}
                                   for r in reaches]}]
        return json.dumps({"decoder": "bank", "channel_seconds": seconds, "tracks": tracks,
                           "score": {"signals": []}})

    (bank / "qso.json").write_text(result([0.5, 1.5, 3.0], 120.0))
    (bank / "qso.stations.json").write_text(result([0.5, 1.5, 3.0], 120.0))  # the same run: not counted again
    (bank / "ora.json").write_text(result([20.0], 60.0))
    (bank / "ora.oracle.json").write_text(result([9.0], 60.0))  # not a copy group: ignored
    base = tmp_path / "results" / "baseline"
    base.mkdir(parents=True)
    (base / "ora.json").write_text(json.dumps({"decoder": "envelope", "channel_seconds": 60.0, "tracks": [],
                                               "score": {"signals": []}}))
    s = correction_stats(tmp_path)
    assert set(s) == {("bank", "H two-station QSO"), ("bank", "A sensitivity")}
    h = s[("bank", "H two-station QSO")]
    assert h["runs"] == 1 and h["corrections"] == 3 and h["channel_minutes"] == 2.0
    assert h["per_channel_minute"] == 1.5
    assert h["by_reason"] == {"rekey": 1, "switch": 2}
    assert h["reach_median_s"] == 1.5 and h["reach_max_s"] == 3.0
    assert h["reach_p99_s"] == pytest.approx(float(np.percentile([0.5, 1.5, 3.0], 99)))
    a = s[("bank", "A sensitivity")]
    assert a["corrections"] == 1 and a["per_channel_minute"] == 1.0 and a["reach_max_s"] == 20.0
    write_summary(tmp_path)
    text = (tmp_path / "summary.md").read_text(encoding="utf-8")
    assert "## Corrections" in text
    assert "| H two-station QSO | bank | 1 | 2.0 | 3 | 1.50 | rekey 1, switch 2 | 1.500 |" in text
    summary = json.loads((tmp_path / "summary.json").read_text())
    assert {(c["front_end"], c["group"]) for c in summary["corrections"]} == set(s)


def test_paired_rows_against_the_bank_carry_its_drift_mark():
    rows = [dict(_row(0.0, 100, 10, front_end="matched", index=0), drifting_oracle=True),
            dict(_row(0.0, 100, 20, front_end="bank", index=0), drifting_oracle=True)]
    assert paired_differences(rows, "matched", "bank")[("A", "25 wpm")]["beyond_oracle_anchor"] is True
    assert paired_differences(rows, "bank", "matched")[("A", "25 wpm")]["beyond_oracle_anchor"] is False


def test_the_bank_drift_mark_needs_an_oracle_recording(tmp_path):
    _drift_recording(tmp_path, oracle=False)
    results = tmp_path / "results" / "bank"
    results.mkdir(parents=True)
    (results / "drift.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0), _fake_signal(1)]}}))
    write_summary(tmp_path)
    assert "(not meaningful (oracle anchor))" not in (tmp_path / "summary.md").read_text(encoding="utf-8")


def test_farnsworth_group_uses_its_overall_speeds():
    recs = [r for r in SUITES["full"](1) if r.group == "I Farnsworth"]
    assert [r.name for r in recs] == ["I-farnsworth-machine-s1", "I-farnsworth-paddle-s1"]
    specs = [s for r in recs for s in r.specs]
    assert {(s.wpm, s.farnsworth_wpm) for s in specs} == {(18.0, 5.0), (18.0, 10.0), (25.0, 13.0), (25.0, 18.0)}
    assert {s.snr_db for s in specs} == {5.0, 10.0, 20.0}
    assert all(r.oracle for r in recs)
    assert {s.keying for s in recs[0].specs} == {"machine"} and {s.keying for s in recs[1].specs} == {"paddle"}


def test_generate_only_rewrites_matching_recordings_and_lists_every_one(tmp_path):
    a = Recording("keep", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    b = Recording("redo", "A sensitivity", 8000, 3.0, 6, True, [SignalSpec("TU", 1000.0, 25.0, 10.0, 0.5)])
    with pytest.raises(FileNotFoundError, match="keep.wav"):
        write_suite([a, b], tmp_path, "test", only="^redo$")
    write_suite([a, b], tmp_path, "test")
    kept = (tmp_path / "keep.wav").stat().st_mtime_ns
    (tmp_path / "redo.wav").unlink()
    write_suite([a, b], tmp_path, "test", only="^redo$")
    assert (tmp_path / "redo.wav").exists()
    assert (tmp_path / "keep.wav").stat().st_mtime_ns == kept
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert [r["name"] for r in manifest["recordings"]] == ["keep", "redo"]


def test_run_only_scores_matching_recordings(tmp_path, monkeypatch):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": n, "group": "g", "oracle": False, "wav": f"{n}.wav", "labels": f"{n}.json", "station_labels": None}
        for n in ("x", "y")]}))
    calls = []

    def fake_run(cmd, *a, **k):
        calls.append(cmd)
        return subprocess.CompletedProcess(cmd, 0, "CER 0.0\n", "")

    monkeypatch.setattr(subprocess, "run", fake_run)
    run_suite(tmp_path, tmp_path / "kz4ap-bench", ["baseline"], only="^y$")
    assert [c[1] for c in calls] == [str(tmp_path / "y.wav")]


def test_paired_differences_compare_any_two_front_ends():
    rows = [_row(0.0, 100, 10, front_end="matched", index=i) for i in range(4)]
    rows += [_row(0.0, 100, 20, front_end="bank-proto", index=i) for i in range(4)]
    d = paired_differences(rows, "matched", "bank-proto")[("A", "25 wpm")]
    assert d["mean"] == pytest.approx(0.10)


def test_detector_only_groups_get_oracle_copies_that_load_as_oracle_rows(tmp_path):
    from kz4ap_synth.suites import ORACLE_COPY_GROUPS, load_results, oracle_copies
    rec = {"name": "pauses-s1", "group": "pauses", "oracle": False, "labels": "pauses-s1.json"}
    assert oracle_copies(rec) == [("pauses-s1.json", "pauses-s1.oracle", "pauses, oracle")]
    assert oracle_copies({**rec, "group": "A sensitivity", "oracle": True}) == []
    assert oracle_copies({**rec, "group": "H two-station QSO"}) == []  # group H has its own oracle copy
    assert set(ORACLE_COPY_GROUPS) == {"pauses", "strong", "tune-up", "first sample", "band", "crowded"}
    spec = Recording("pauses-s1", "pauses", 8000, 3.0, 5, False, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([spec], tmp_path, "test")
    (tmp_path / "results" / "matched").mkdir(parents=True)
    for result in ("pauses-s1", "pauses-s1.oracle"):
        (tmp_path / "results" / "matched" / f"{result}.json").write_text(
            json.dumps({"score": {"signals": [_fake_signal(0)]}}))
    rows, _ = load_results(tmp_path)
    assert sorted((r["recording"], r["group"]) for r in rows) == [("pauses-s1", "pauses"),
                                                                  ("pauses-s1.oracle", "pauses, oracle")]


def test_load_results_reads_the_given_directories_and_front_ends(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([rec], tmp_path, "test")
    for root, fe in ((tmp_path / "results", "baseline"), (tmp_path / "experiments" / "results", "exp-a")):
        (root / fe).mkdir(parents=True)
        (root / fe / "tiny.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0)]}}))
    from kz4ap_synth.suites import load_results
    assert {r["front_end"] for r in load_results(tmp_path)[0]} == {"baseline"}
    rows, _ = load_results(tmp_path, [tmp_path / "results", tmp_path / "experiments" / "results"], ["exp-a"])
    assert {r["front_end"] for r in rows} == {"exp-a"}


def test_track_splits_honor_results_dirs_front_ends_and_only(tmp_path):
    rec = Recording("qso", "H two-station QSO", 8000, 12.0, 5, False, [_two_station(100.0)], True)
    write_suite([rec], tmp_path, "test")
    tracks = {"tracks": [{"id": 1, "freq_hz": 1001.0, "text": "CQ"}, {"id": 2, "freq_hz": 1099.0, "text": "K1"}],
              "score": {"signals": []}}
    for root, fe in ((tmp_path / "results", "baseline"), (tmp_path / "exp", "exp-a")):
        (root / fe).mkdir(parents=True)
        (root / fe / "qso.json").write_text(json.dumps(tracks))
    key = ("exp-a", "separate-track, offset 100 Hz")
    assert key not in track_splits(tmp_path)  # default: out_dir/results only
    assert set(track_splits(tmp_path, [tmp_path / "results", tmp_path / "exp"], ["exp-a"])) == {key}
    assert track_splits(tmp_path, [tmp_path / "exp"], only="^other$") == {}


def test_load_results_rejects_a_front_end_under_two_roots(tmp_path):
    rec = Recording("tiny", "A sensitivity", 8000, 3.0, 5, True, [SignalSpec("CQ", 1000.0, 25.0, 10.0, 0.5)])
    write_suite([rec], tmp_path, "test")
    for root in (tmp_path / "results", tmp_path / "exp"):
        (root / "matched").mkdir(parents=True)
        (root / "matched" / "tiny.json").write_text(json.dumps({"score": {"signals": [_fake_signal(0)]}}))
    from kz4ap_synth.suites import load_results
    with pytest.raises(ValueError, match="matched"):
        load_results(tmp_path, [tmp_path / "results", tmp_path / "exp"])
