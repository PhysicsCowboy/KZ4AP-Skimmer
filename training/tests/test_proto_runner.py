import json
import subprocess

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


def test_detector_path_is_recorded_and_scored_under_the_engine_names(tmp_path, monkeypatch):
    manifest(tmp_path, [rec("a", True), {**rec("h", False, "h.stations.json"), "group": "H two-station QSO"}])
    jobs = runner.detector_jobs(tmp_path)
    assert [(j["result"], [s[1] for s in j["scorings"]]) for j in jobs] == [("h.detector", ["h", "h.stations"])]
    calls = []
    monkeypatch.setattr(subprocess, "run", lambda cmd, **k: calls.append(cmd) or subprocess.CompletedProcess(cmd, 0, "CER 0\n", ""))
    runner.record(tmp_path, tmp_path / "kz4ap-bench", only="^h")
    (cmd,) = calls
    assert "--oracle" not in cmd and cmd[cmd.index("--decoder") + 1] == "matched"
    assert cmd[cmd.index("--record-channels") + 1] == str(tmp_path / "channels" / "h.detector")
    # a decoded file of the detector channels (the "tracks" form) is scored under each of the recording's scorings
    (tmp_path / "proto" / "t-proto").mkdir(parents=True)
    (tmp_path / "proto" / "t-proto" / "h.detector.decoded.json").write_text(json.dumps(
        {"front_end": "t-proto", "tracks": [{"id": 4, "freq_hz": 1011.0, "last_freq_hz": 1011.0, "text": "E"}]}))
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
