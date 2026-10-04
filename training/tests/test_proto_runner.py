import json
import subprocess

import numpy as np

from kz4ap_proto import runner


def manifest(tmp_path, recordings):
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": recordings}))


def rec(name, oracle, stations=None):
    return {"name": name, "group": "A sensitivity", "oracle": oracle, "wav": f"{name}.wav", "labels": f"{name}.json",
            "station_labels": stations}


def test_record_runs_the_bench_on_every_oracle_scoring(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True, "a.stations.json"), rec("b", False)])
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "", ""))
    runner.record(tmp_path, tmp_path / "kz4ap-bench")
    oracle = [c for c in calls if "--oracle" in c]
    assert [c[c.index("--record-channels") + 1] for c in oracle] == [
        str(tmp_path / "channels" / "a"), str(tmp_path / "channels" / "a.stations")]
    assert all(c[c.index("--decoder") + 1] == "envelope" for c in oracle)
    # b is not an oracle recording: its detector channels are recorded once (Task 11b)
    assert [c[c.index("--record-channels") + 1] for c in calls if "--oracle" not in c] == [
        str(tmp_path / "channels" / "b.detector")]


def test_decode_writes_one_text_per_label_in_label_order(tmp_path):
    manifest(tmp_path, [rec("a", True)])
    (tmp_path / "a.json").write_text(json.dumps({"signals": [
        {"text": "E", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0},
        {"text": "T", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0}]}))
    record_dir = tmp_path / "channels" / "a"
    record_dir.mkdir(parents=True)
    rng = np.random.default_rng(1)
    channels = []
    for i in (1, 2):
        y = ((rng.standard_normal(3000) + 1j * rng.standard_normal(3000)) * 0.1).astype("<c8")
        y.tofile(record_dir / f"channel-{i}.c64")
        channels.append({"track_id": i, "label_index": i - 1, "label_freq_hz": 0.0, "center_hz": 0.0,
                         "first_sample_index": 0, "samples": 3000, "file": f"channel-{i}.c64"})
    (record_dir / "channels.json").write_text(json.dumps({"sample_rate_hz": 1500.0, "channels": channels}))
    runner.decode(tmp_path, "t-proto", jobs=1, keep_p1=True)
    decoded = json.loads((tmp_path / "proto" / "t-proto" / "a.decoded.json").read_text())
    assert decoded["front_end"] == "t-proto" and len(decoded["texts"]) == 2
    assert [c["label_index"] for c in decoded["channels"]] == [0, 1]
    assert decoded["channels"][0]["channel_s"] == 2.0 and decoded["channels"][0]["rate_hz"] == 1500.0
    assert np.load(tmp_path / "proto" / "t-proto" / "p1" / "a" / "1.npy").shape == (3000,)


def two_channel_recording(tmp_path, samples=(3000, 3000)):
    """Oracle recording "a": two labels, two 2 s noise channels at 1500 samples/s (the second's file holds
    samples[1] samples while the manifest says 3000)."""
    manifest(tmp_path, [rec("a", True)])
    (tmp_path / "a.json").write_text(json.dumps({"signals": [
        {"text": "E", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0},
        {"text": "T", "start_s": 0.5, "end_s": 1.0, "wpm": 25.0, "snr_db": 10.0}]}))
    record_dir = tmp_path / "channels" / "a"
    record_dir.mkdir(parents=True)
    rng = np.random.default_rng(3)
    channels = []
    for i, n in zip((1, 2), samples):
        ((rng.standard_normal(n) + 1j * rng.standard_normal(n)) * 0.1).astype("<c8").tofile(record_dir / f"channel-{i}.c64")
        channels.append({"track_id": i, "label_index": i - 1, "label_freq_hz": 0.0, "center_hz": 0.0,
                         "first_sample_index": 0, "samples": 3000, "file": f"channel-{i}.c64"})
    (record_dir / "channels.json").write_text(json.dumps({"sample_rate_hz": 1500.0, "channels": channels}))


def test_decode_resumes_skipping_recordings_decoded_with_the_same_config(tmp_path, capsys):
    two_channel_recording(tmp_path)
    runner.decode(tmp_path, "t-proto", jobs=1)
    path = tmp_path / "proto" / "t-proto" / "a.decoded.json"
    decoded = json.loads(path.read_text())
    path.write_text(json.dumps({**decoded, "marker": 1}))  # a file this run must leave alone
    capsys.readouterr()
    runner.decode(tmp_path, "t-proto", jobs=1)
    assert json.loads(path.read_text())["marker"] == 1
    assert "skipped 1 recordings" in capsys.readouterr().out
    # another config value: decoded again
    runner.decode(tmp_path, "t-proto", values={"correction_reach_s": 19.0}, jobs=1)
    again = json.loads(path.read_text())
    assert "marker" not in again and again["config"]["correction_reach_s"] == 19.0
    # keep_p1 with the posteriors missing: decoded again, and the posteriors written
    runner.decode(tmp_path, "t-proto", values={"correction_reach_s": 19.0}, jobs=1, keep_p1=True)
    assert (tmp_path / "proto" / "t-proto" / "p1" / "a" / "1.npy").exists()


def test_a_failing_channel_is_named_by_its_recording_and_position(tmp_path):
    import pytest
    two_channel_recording(tmp_path, samples=(3000, 2999))  # channel position 1 is one sample short
    with pytest.raises(runner.ChannelFailed, match=r"channels.a, channel position 1 failed: ValueError"):
        runner.decode(tmp_path, "t-proto", jobs=1)
    assert not (tmp_path / "proto" / "t-proto" / "a.decoded.json").exists()  # no incomplete file


def test_oracle_copies_are_recorded_and_scored_by_the_engine_front_ends(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True), {**rec("p", False), "group": "pauses"}, {**rec("h", False), "group": "H two-station QSO"}])
    assert [j["result"] for j in runner.oracle_scorings(tmp_path)] == ["a", "p.oracle"]
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.score_engine_copies(tmp_path, tmp_path / "kz4ap-bench")
    assert [(c[c.index("--decoder") + 1], c[c.index("--json") + 1]) for c in calls] == [
        ("envelope", str(tmp_path / "results" / "baseline" / "p.oracle.json")),
        ("matched", str(tmp_path / "results" / "matched" / "p.oracle.json"))]
    assert all("--oracle" in c and c[1] == str(tmp_path / "p.wav") for c in calls)
    # The bank decoder: --decoder bank into results/bank, asked for by name on the command line.
    calls.clear()
    runner.main(["engine-copies", "--out", str(tmp_path), "--bench", str(tmp_path / "kz4ap-bench"),
                 "--decoder", "bank"])
    assert [(c[c.index("--decoder") + 1], c[c.index("--json") + 1]) for c in calls] == [
        ("bank", str(tmp_path / "results" / "bank" / "p.oracle.json"))]


def test_detector_path_is_recorded_decoded_as_tracks_and_scored_under_the_engine_names(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True), {**rec("h", False, "h.stations.json"), "group": "H two-station QSO"}])
    jobs = runner.detector_jobs(tmp_path)
    assert [(j["result"], [s[1] for s in j["scorings"]]) for j in jobs] == [("h.detector", ["h", "h.stations"])]
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.record(tmp_path, tmp_path / "kz4ap-bench", only="^h")
    (cmd,) = calls
    assert "--oracle" not in cmd and cmd[cmd.index("--decoder") + 1] == "matched"
    assert cmd[cmd.index("--record-channels") + 1] == str(tmp_path / "channels" / "h.detector")
    # a detector recording with one track, decoded as a "tracks" file
    record_dir = tmp_path / "channels" / "h.detector"
    record_dir.mkdir(parents=True)
    y = ((np.random.default_rng(2).standard_normal(3000) + 0j) * 0.1).astype("<c8")
    y.tofile(record_dir / "channel-4.c64")
    (record_dir / "channels.json").write_text(json.dumps({"sample_rate_hz": 1500.0, "channels": [
        {"track_id": 4, "label_index": None, "label_freq_hz": None, "birth_freq_hz": 1011.0, "center_hz": 1000.0,
         "first_sample_index": 1500, "open_s": 1.0, "close_s": 3.0, "samples": 3000, "file": "channel-4.c64",
         "anchors": [[1500, 1011.0]]}]}))
    runner.decode(tmp_path, "t-proto", only="^h", jobs=1)
    decoded = json.loads((tmp_path / "proto" / "t-proto" / "h.detector.decoded.json").read_text())
    assert "texts" not in decoded and [(t["id"], t["freq_hz"], t["last_freq_hz"]) for t in decoded["tracks"]] == [
        (4, 1011.0, 1011.0)]
    calls.clear()
    runner.score(tmp_path, tmp_path / "kz4ap-bench", "t-proto", only="^h")
    assert [(c[c.index("--labels") + 1], c[c.index("--json") + 1]) for c in calls] == [
        (str(tmp_path / "h.json"), str(tmp_path / "results" / "t-proto" / "h.json")),
        (str(tmp_path / "h.stations.json"), str(tmp_path / "results" / "t-proto" / "h.stations.json"))]


def test_detector_measures_sum_the_bench_counts_per_front_end(tmp_path):
    from kz4ap_proto.report import detector_measures
    manifest(tmp_path, [{**rec("band-s1", False), "group": "band"}, rec("a", True)])
    for fe, (scored, detected, false) in (("bank-proto", (20, 19, 2)), ("matched", (20, 18, 3)), ("baseline", (20, 17, 5))):
        (tmp_path / "results" / fe).mkdir(parents=True)
        (tmp_path / "results" / fe / "band-s1.json").write_text(json.dumps(
            {"tracks": [], "score": {"scored": scored, "detected": detected, "false_tracks": false, "signals": []}}))
    m = detector_measures(tmp_path, "bank-proto")["by_front_end"]
    assert m["bank-proto"]["band"] == {"recordings": 1, "scored": 20, "detected": 19, "false_tracks": 2,
                                       "detection_recall": 0.95}
    assert m["baseline"]["band"]["false_tracks"] == 5 and "A sensitivity" not in m["matched"]


def test_score_hands_each_decoded_file_to_the_bench(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True)])
    (tmp_path / "proto" / "t-proto").mkdir(parents=True)
    (tmp_path / "proto" / "t-proto" / "a.decoded.json").write_text("{}")
    calls = []
    monkeypatch.setattr(subprocess, "run",
                        lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0.1\n", ""))
    runner.score(tmp_path, tmp_path / "kz4ap-bench", "t-proto")
    (cmd,) = calls
    assert cmd[cmd.index("--score-decoded") + 1] == str(tmp_path / "proto" / "t-proto" / "a.decoded.json")
    assert cmd[cmd.index("--json") + 1] == str(tmp_path / "results" / "t-proto" / "a.json")


def test_score_says_how_many_scorings_and_labels_had_a_decoded_file(tmp_path, monkeypatch, capsys):
    manifest(tmp_path, [rec("a", True), rec("b", True)])
    for name, count in (("a", 2), ("b", 3)):
        (tmp_path / f"{name}.json").write_text(json.dumps({"signals": [{}] * count}))
    (tmp_path / "proto" / "t-proto").mkdir(parents=True)
    (tmp_path / "proto" / "t-proto" / "b.decoded.json").write_text("{}")
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.score(tmp_path, tmp_path / "kz4ap-bench", "t-proto")
    assert "t-proto: scored 1 of 2 test cases (3 of 5 labels)" in capsys.readouterr().out
