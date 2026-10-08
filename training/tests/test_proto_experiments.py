import re

import pytest

from kz4ap_proto import experiments


def row(fe, index, edits, symbols=100, group="A sensitivity"):
    return {"front_end": fe, "group": group, "tag": "t", "recording": "r", "index": index, "scored": True,
            "edits": edits, "symbols": symbols, "first_word_edits": 0, "first_word_symbols": 0}


def test_the_development_set_is_seed_one_without_the_per_row_fading_recordings():
    names = ["A-awgn-12wpm-0-s1", "B-fading-mix-s1", "B-fading-paddle-24wpm-0.1Hz-s1", "C-fists-bug-s1",
             "D-speed-s1", "H-qso-oracle-s1.stations", "I-farnsworth-machine-s1", "A-awgn-12wpm-0-s2", "strong-s1"]
    assert [n for n in names if re.search(experiments.DEV, n)] == [
        "A-awgn-12wpm-0-s1", "B-fading-mix-s1", "C-fists-bug-s1", "D-speed-s1", "H-qso-oracle-s1.stations",
        "I-farnsworth-machine-s1"]


def test_pooled_paired_differences_by_group_and_overall():
    rows = [row("ref", i, 10) for i in range(4)] + [row("var", i, 5) for i in range(4)]
    rows += [row("ref", 9, 10, group="D speed"), row("var", 9, 30, group="D speed")]
    d = experiments.pooled_paired(rows, "ref", "var")
    assert d["A sensitivity"]["mean"] == pytest.approx(-0.05) and d["A sensitivity"]["signals"] == 4
    assert d["all"]["mean"] == pytest.approx((4 * -0.05 + 0.2) / 5)
    assert d["D speed"]["interval"] is None  # one signal: no interval


def test_compare_takes_its_statistics_over_its_subset(tmp_path, monkeypatch):
    # Review M9: a comparison over a subset (here STRETCH) must not summarize the base's statistics over all of DEV.
    seen = []

    def stat(out_dir, name, only=None, *args, **kwargs):
        seen.append(only)
        return {}

    monkeypatch.setattr(experiments, "load_results", lambda *a, **k: ([], []))
    for f in ("speed_errors", "switch_stats", "spurious_over_starts", "false_characters"):
        monkeypatch.setattr(experiments.metrics, f, stat)
    monkeypatch.setattr(experiments.metrics, "cpu_per_channel_second", lambda o, n, only=None: seen.append(only) or 0.0)
    experiments.compare(tmp_path, "base", "variant", only=experiments.STRETCH)
    assert seen and all(o == experiments.STRETCH for o in seen)


def _b9_suite(tmp_path):
    """A hand-made suite folder: group A's 25 WPM recording and its stretched copy (3 S500 steps, one signal each),
    group H's oracle QSO recording and the pauses group's oracle copy, with decoded files and scored results of run
    "v"."""
    import json
    step = 10 * __import__("math").log10(25 / 12)
    recs = [("A-awgn-25wpm-0-s1", "A sensitivity", True), ("S-stretch-25wpm-0-s1", "S stretch", True),
            ("H-qso-oracle-s1", "H two-station QSO, oracle", True), ("pauses-s1", "pauses", False),
            ("S-stretch-25wpm-0-s2", "S stretch", True)]
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": n, "group": g, "oracle": o, "wav": f"{n}.wav", "labels": f"{n}.json", "station_labels": None}
        for n, g, o in recs]}))
    snrs = (0.0, 2.0, 4.0)

    def signal(text, snr, **more):
        return {"text": text, "snr_db": snr, "start_s": 1.0, "end_s": 20.0, "tag": "t", "score": True,
                "transmissions": [{"text": text, "start_s": 1.0, "end_s": 20.0}], **more}

    labels = {"A-awgn-25wpm-0-s1": [signal(f"T{i}", s) for i, s in enumerate(snrs)],
              "S-stretch-25wpm-0-s1": [signal(f"T{i}", s - step) for i, s in enumerate(snrs)],
              "S-stretch-25wpm-0-s2": [signal("X", 0.0)],
              "H-qso-oracle-s1": [{**__import__("copy").deepcopy(QSO_LABEL), "tag": f"same-track, offset {df} Hz"}
                                  for df in (0, 200)],
              "pauses-s1": [signal("CQ", 15.0, tag="pause 2 s", transmissions=[
                  {"text": "CQ", "start_s": 1.0, "end_s": 5.0}, {"text": "CQ", "start_s": 7.0, "end_s": 11.0}])]}
    for n, sigs in labels.items():
        (tmp_path / f"{n}.json").write_text(json.dumps({"signals": sigs}))
    # CER per step: original 0.3, 0.05, 0.0; stretched 0.5, 0.3, 0.05 (one step worse)
    edits = {"A-awgn-25wpm-0-s1": [30, 5, 0], "S-stretch-25wpm-0-s1": [50, 30, 5]}
    over_starts = {"H-qso-oracle-s1": [[10.6, 12.5], [10.6, 12.5]], "pauses-s1.oracle": [[5.7]]}
    for n, sigs in labels.items():
        result = "pauses-s1.oracle" if n == "pauses-s1" else n
        rows = []
        for i, lab in enumerate(sigs):
            e = edits.get(n, [0] * len(sigs))[i]
            txs = [{"first_word_symbols": 4, "first_word_edits": 1 + k} for k in range(len(lab["transmissions"]))]
            rows.append({"index": i, "snr_db": lab["snr_db"], "scored": True, "track_id": 0, "symbols": 100,
                         "edits": e, "chars": 100, "char_edits": e, "spaces": 0, "space_edits": 0,
                         "first_word_symbols": 10, "first_word_edits": e // 10, "nospace_symbols": 100,
                         "nospace_edits": e, "transmissions": txs})
        res = tmp_path / "experiments" / "results" / "v"
        res.mkdir(parents=True, exist_ok=True)
        (res / f"{result}.json").write_text(json.dumps({"score": {"signals": rows}}))
        dec = tmp_path / "proto" / "v"
        dec.mkdir(parents=True, exist_ok=True)
        starts = over_starts.get(result, [[] for _ in sigs])
        (dec / f"{result}.decoded.json").write_text(json.dumps({"config": {}, "channels": [
            {"label_index": i, "over_starts": starts[i], "text": "", "chars": [], "selections": [], "periodicity": [],
             "cpu_s": 0.0, "channel_s": 30.0} for i in range(len(sigs))]}))


QSO_LABEL = {"text": "A B C", "start_s": 1.0, "end_s": 30.0, "score": True, "snr_db": 15.0, "transmissions": [
    {"text": "A", "start_s": 1.0, "end_s": 10.0, "sender_index": 0},
    {"text": "B", "start_s": 11.0, "end_s": 20.0, "sender_index": 1},
    {"text": "C", "start_s": 21.5, "end_s": 30.0, "sender_index": 0}]}


def test_the_stretch_table_pairs_each_stretched_signal_with_its_original(tmp_path):
    import json
    _b9_suite(tmp_path)
    pairs = experiments.stretch_pairs(tmp_path, "v")
    assert [(p["recording"], p["index"]) for p in pairs] == [("S-stretch-25wpm-0-s1", i) for i in range(3)]
    assert pairs[0]["snr_db"] == 0.0 and pairs[0]["cer"] == {"orig": (30, 100), "stretched": (50, 100)}
    path = experiments.stretch_table(tmp_path, "v")
    m = json.loads(path.with_suffix(".json").read_text())
    assert m["pairs"] == 3
    assert m["cer"]["all"]["mean"] == pytest.approx((0.2 + 0.25 + 0.05) / 3)
    step = 10 * __import__("math").log10(25 / 12)
    assert m["cer"]["shift_db"] == pytest.approx(step - 2.0)  # one 2 dB step worse
    assert "shift original − stretched" in path.read_text(encoding="utf-8")
    # A stretched copy whose text differs from its original's is refused
    lab = json.loads((tmp_path / "S-stretch-25wpm-0-s1.json").read_text())
    lab["signals"][1]["text"] = "other"
    (tmp_path / "S-stretch-25wpm-0-s1.json").write_text(json.dumps(lab))
    with pytest.raises(ValueError):
        experiments.stretch_pairs(tmp_path, "v")


def test_the_new_over_table_counts_by_group_and_leaves_out_qsos_the_channel_does_not_hold(tmp_path):
    _b9_suite(tmp_path)
    text = experiments.new_over_table(tmp_path, "v").read_text(encoding="utf-8")
    # H: only the 0 Hz QSO (the 200 Hz one is beyond the oracle channel's -6 dB point): 12.5 s is inside B (1 false
    # of 3 transmissions); turnover B found on time (10.6 s), turnover C missed (nothing in 20 to 23.5 s).
    assert "| H two-station QSO, oracle | all | 1 | 0.3333 (1 / 3) | 0.5000 (1 / 2) | 0.0000 (0 / 2) | — | — |" in text
    # pauses: the over start at 5.7 s is in the same station's silence before its repeat.
    assert "| pauses, oracle | all | 1 | 0.0000 (0 / 2) | — | — | — | 1.0000 (1 / 1) |" in text
    # first-word CER per over: first overs 1/4 each, the H turnovers 2/4 and 3/4, the pauses repeat 2/4
    assert "| H two-station QSO, oracle | turnover | 1 | 2 | 0.6250 (5 / 8) |" in text
    assert "| pauses, oracle | same station | 1 | 1 | 0.5000 (2 / 4) |" in text


def test_the_b9_test_cases_stay_out_of_the_development_set():
    names = ["S-stretch-25wpm-0-s1", "A-awgn-25wpm-1-s1", "H-qso-oracle-s1", "pauses-s1.oracle",
             "S-stretch-25wpm-0-s2", "A-awgn-12wpm-0-s1"]
    assert [n for n in names if re.search(experiments.DEV, n)] == ["A-awgn-25wpm-1-s1", "H-qso-oracle-s1",
                                                                   "A-awgn-12wpm-0-s1"]
    assert [n for n in names if re.search(experiments.STRETCH, n)] == ["S-stretch-25wpm-0-s1", "A-awgn-25wpm-1-s1"]
    assert [n for n in names if re.search(experiments.B9, n)] == names[:4]
