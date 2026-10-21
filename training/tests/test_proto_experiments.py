import json
import re
import subprocess

import pytest

import stand_in_bench
from kz4ap_proto import devset2, experiments
from test_search_stats import V1, V2, _suite


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


# --- search errors: a run's text against the wide-beam reference's
# (docs/plans/2026-10-20-new-decoder-prototype-1.md) ---

def _label(text, wpm=25.0, **more):
    return {"text": text, "freq_offset_hz": 0.0, "wpm": wpm, "snr_db": 5.0, "start_s": 1.0, "end_s": 30.0, "tag": "t",
            "score": True, "transmissions": [{"text": text, "start_s": 1.0, "end_s": 30.0}], **more}


def _search_suite(tmp_path, detector_copy=False):
    """A suite folder with an A2 test case (three scored signals and an unscored one), an I2 test case (three
    signals, the last above Farnsworth factor 8) and a second A2 test case that only the run under test, "b28", has;
    the decoded texts of "b28" and of the reference run "ref". The labels' own texts are not the decoded ones: they
    play no part. detector_copy: also A2's detector-path copy (a recording that is not an oracle one, the same signals
    decoded again through the detector), with one signal that differs between the two runs."""
    i2, a2 = "I2-farnsworth-o1-0-s1", "A2-awgn-c01-0-s1"
    labels = {a2: [_label("L1"), _label("L2"), _label("L3"), _label("L4", score=False)],
              i2: [_label("L1", wpm=18.0, farnsworth_wpm=10.0), _label("L2", wpm=18.0, farnsworth_wpm=5.0),
                   _label("L3", wpm=25.0, farnsworth_wpm=5.0)],
              "A2-awgn-c02-0-s1": [_label("L1")]}
    groups = {a2: "A2 sensitivity", i2: "I2 Farnsworth", "A2-awgn-c02-0-s1": "A2 sensitivity"}
    oracle = {n: True for n in labels}
    if detector_copy:
        labels["A2-detector-c01-0-s1"] = [_label("L1")]
        groups["A2-detector-c01-0-s1"] = "A2 sensitivity, detector"
        oracle["A2-detector-c01-0-s1"] = False
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "t", "recordings": [
        {"name": n, "group": groups[n], "oracle": oracle[n], "wav": f"{n}.wav", "labels": f"{n}.json",
         "station_labels": None} for n in labels]}))
    for n, sigs in labels.items():
        (tmp_path / f"{n}.json").write_text(json.dumps({"sample_rate": 48000, "duration_s": 100.0, "signals": sigs}))
    # (reference's text, the run's text) per signal; the edits (symbols that differ, and a longer or shorter tail); the
    # letters are the reference's characters that are not word spaces
    texts = {a2: [("CQ TEST DE K1ABC", "CQ TEST DE K1ABC"),   # 13 letters, 0 edits
                  ("PARIS PARIS", "PARIS PARIX"),             # 10 letters, 1 edit
                  ("AB", "AB E"),                             # 2 letters, 2 edits: a longer tail (a space and an E)
                  ("X", "YYYYY")],                            # not scored: left out
             i2: [("TEST TEST", "TEST TEST"),                 # 8 letters, 0 edits, factor 3.11
                  ("TEST TEST", "TEXT TEXT"),                 # 8 letters, 2 edits, factor 7.84
                  ("TEST TEST", "XXXX XXXX")],                # 8 letters, 8 edits, factor 11.53: above 8
             "A2-awgn-c02-0-s1": [("Q", "R")]}
    if detector_copy:
        texts["A2-detector-c01-0-s1"] = [("AAAA", "AAAB")]       # 4 letters, 1 edit
    for run, side in (("ref", 0), ("b28", 1)):
        folder = tmp_path / "results" / run
        folder.mkdir(parents=True)
        for n, pairs in texts.items():
            if run == "ref" and n == "A2-awgn-c02-0-s1":
                continue
            signals = [{"index": i, "scored": labels[n][i].get("score", True), "decoded": pair[side],
                        "edits": 99, "symbols": 99} for i, pair in enumerate(pairs)]
            (folder / f"{n}.json").write_text(json.dumps({"decoder": run, "score": {"signals": signals}}))
    return texts


def test_search_errors_count_edits_between_two_runs_per_1000_letters(tmp_path):
    texts = _search_suite(tmp_path)
    bench = stand_in_bench.launcher(tmp_path)
    r = experiments.search_errors(tmp_path, bench, "b28", "ref")
    # per group and pooled: the edits (every one, a word space's too) between the run's text and the reference's text
    # of each scored signal, over the reference's letters (its characters that are not word spaces)
    a2, i2 = r["groups"]["A2 sensitivity"], r["groups"]["I2 Farnsworth"]
    assert (a2["signals"], a2["letters"], a2["errors"], a2["signals_with_errors"]) == (3, 25, 3, 2)
    assert a2["per_1000_letters"] == pytest.approx(120.0)
    assert (i2["signals"], i2["letters"], i2["errors"], i2["signals_with_errors"]) == (3, 24, 10, 2)
    assert r["pooled"] == {"signals": 6, "letters": 49, "errors": 13, "signals_with_errors": 4,
                           "per_1000_letters": pytest.approx(1000 * 13 / 49)}
    assert r["left_out"] is None and r["max_farnsworth"] is None
    assert r["test_cases"] == {"compared": 2, "only_in_run": ["A2-awgn-c02-0-s1"], "only_in_reference": []}
    # the bench was given the reference's text as the label's and the run's text as the decoded one
    work = tmp_path / "experiments" / "search-errors-b28-vs-ref"
    reference = json.loads((work / "A2-awgn-c01-0-s1.reference.json").read_text())["signals"]
    assert [s["text"] for s in reference] == [p[0] for p in texts["A2-awgn-c01-0-s1"]]
    assert all("transmissions" not in s for s in reference)
    assert json.loads((work / "A2-awgn-c01-0-s1.decoded.json").read_text())["texts"] == [
        p[1] for p in texts["A2-awgn-c01-0-s1"]]
    # the direction matters: the letters are the reference's (the run's tail of AB E is not a letter of the reference)
    back = experiments.search_errors(tmp_path, bench, "ref", "b28")
    assert (back["groups"]["A2 sensitivity"]["letters"], back["groups"]["A2 sensitivity"]["errors"]) == (26, 3)
    assert back["test_cases"] == {"compared": 2, "only_in_run": [], "only_in_reference": ["A2-awgn-c02-0-s1"]}
    # the I2 cut: signals above the factor are left out of the rates, on both sides of the comparison, and listed apart
    cut = experiments.search_errors(tmp_path, bench, "b28", "ref", max_farnsworth=8.0)
    assert cut["groups"]["A2 sensitivity"] == a2
    assert (cut["groups"]["I2 Farnsworth"]["signals"], cut["groups"]["I2 Farnsworth"]["letters"],
            cut["groups"]["I2 Farnsworth"]["errors"]) == (2, 16, 2)
    assert (cut["pooled"]["signals"], cut["pooled"]["letters"], cut["pooled"]["errors"]) == (5, 41, 5)
    assert cut["pooled"]["per_1000_letters"] == pytest.approx(1000 * 5 / 41)
    assert cut["left_out"]["groups"] == {"I2 Farnsworth": {"signals": 1, "letters": 8, "errors": 8,
                                                          "signals_with_errors": 1,
                                                          "per_1000_letters": pytest.approx(1000.0)}}
    assert cut["left_out"]["pooled"]["errors"] == 8
    assert [(s["test_case"], s["index"], s["errors"], s["farnsworth_factor"]) for s in cut["left_out"]["signals"]] == [
        ("I2-farnsworth-o1-0-s1", 2, 8, pytest.approx((60 * 25 / 5 - 37.2) / 22.8, abs=1e-12))]  # 25 over 5 WPM: 11.53
    # a selection of test cases, as the analysis's
    only = experiments.search_errors(tmp_path, bench, "b28", "ref", only=r"^I2-")
    assert set(only["groups"]) == {"I2 Farnsworth"} and only["pooled"]["errors"] == 10


def test_the_search_errors_command_writes_the_table_and_the_per_signal_counts(tmp_path, capsys):
    _search_suite(tmp_path)
    experiments.main(["search-errors", "--out", str(tmp_path), "--run", "b28", "--reference", "ref", "--bench",
                      str(stand_in_bench.launcher(tmp_path)), "--max-farnsworth", "8"])
    md_path = tmp_path / "experiments" / "search-errors-b28-vs-ref.md"
    assert str(md_path) in capsys.readouterr().out
    md = md_path.read_text(encoding="utf-8")
    assert "# Search errors: b28 against ref" in md
    assert "| A2 sensitivity | 3 | 25 | 3 | 2 | 120.00 |" in md and "| I2 Farnsworth | 2 | 16 | 2 | 1 | 125.00 |" in md
    assert "| all | 5 | 41 | 5 | 3 | 121.95 |" in md
    assert "Left out: Farnsworth factor above 8" in md and "| I2 Farnsworth | 1 | 8 | 8 | 1 | 1000.00 |" in md
    assert "A2-awgn-c02-0-s1" in md  # a test case only one run has is named, not silently dropped
    assert str(tmp_path) not in md  # committed beside a results record: no machine's folders
    saved = json.loads(md_path.with_suffix(".json").read_text())
    assert saved["run"] == "b28" and saved["reference"] == "ref" and saved["max_farnsworth"] == 8.0
    assert len(saved["signals"]) == 5 and {s["errors"] for s in saved["signals"]} == {0, 1, 2}
    # the bench is needed
    with pytest.raises(SystemExit):
        experiments.main(["search-errors", "--out", str(tmp_path), "--run", "b28", "--reference", "ref"])


def test_search_errors_name_a_missing_run_and_a_bench_that_fails(tmp_path, monkeypatch):
    _search_suite(tmp_path)
    bench = stand_in_bench.launcher(tmp_path)
    with pytest.raises(ValueError, match="nope"):
        experiments.search_errors(tmp_path, bench, "nope", "ref")
    with pytest.raises(ValueError, match="no test case"):
        experiments.search_errors(tmp_path, bench, "b28", "ref", only=r"^never$")
    monkeypatch.setattr(subprocess, "run", lambda cmd, *a, **k: subprocess.CompletedProcess(cmd, 2, "", "boom"))
    with pytest.raises(RuntimeError, match="boom"):
        experiments.search_errors(tmp_path, bench, "b28", "ref")


def test_the_b9_test_cases_stay_out_of_the_development_set():
    names = ["S-stretch-25wpm-0-s1", "A-awgn-25wpm-1-s1", "H-qso-oracle-s1", "pauses-s1.oracle",
             "S-stretch-25wpm-0-s2", "A-awgn-12wpm-0-s1"]
    assert [n for n in names if re.search(experiments.DEV, n)] == ["A-awgn-25wpm-1-s1", "H-qso-oracle-s1",
                                                                   "A-awgn-12wpm-0-s1"]
    assert [n for n in names if re.search(experiments.STRETCH, n)] == ["S-stretch-25wpm-0-s1", "A-awgn-25wpm-1-s1"]
    assert [n for n in names if re.search(experiments.B9, n)] == names[:4]


# --- search errors: which test cases, and the rounded per-signal table ---

def test_search_errors_compare_the_oracle_test_cases_unless_told_otherwise(tmp_path, capsys):
    _search_suite(tmp_path, detector_copy=True)
    bench = stand_in_bench.launcher(tmp_path)
    r = experiments.search_errors(tmp_path, bench, "b28", "ref")
    # A2's detector-path copies are the same signals and noise decoded again: pooled with the oracle test cases they
    # would count A2's signals twice (the plan's runs are oracle runs, O9)
    assert r["only"] == experiments.DEV2 and "A2 sensitivity, detector" not in r["groups"]
    assert r["test_cases"]["compared"] == 2 and r["pooled"]["signals"] == 6
    both = experiments.search_errors(tmp_path, bench, "b28", "ref", only=devset2.DEV2_ANALYSIS)
    assert both["test_cases"]["compared"] == 3 and both["groups"]["A2 sensitivity, detector"]["errors"] == 1
    assert both["pooled"]["signals"] == 7
    # and the option's help says which it is
    with pytest.raises(SystemExit):
        experiments.main(["search-errors", "--help"])
    help_text = " ".join(capsys.readouterr().out.split())
    assert "oracle" in help_text and "detector" in help_text and "copies" in help_text
    assert "DEV2, seed 1" not in help_text   # the old wording, which understated what the default selected


def test_the_search_errors_are_also_a_rounded_per_signal_table(tmp_path):
    _search_suite(tmp_path)
    bench = stand_in_bench.launcher(tmp_path)
    args = ["search-errors", "--out", str(tmp_path), "--run", "b28", "--reference", "ref", "--bench", str(bench)]
    experiments.main(args + ["--max-farnsworth", "8"])
    md = tmp_path / "experiments" / "search-errors-b28-vs-ref.md"
    csv_path = md.with_suffix(".csv")
    text = csv_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    assert lines[0] == "run,reference,group,test_case,index,errors,letters,farnsworth_factor,left_out"
    # one row per signal, the ones the cut left out too (left_out 1), by test case and index; the factor at 0.01
    assert lines[1:] == [
        "b28,ref,A2 sensitivity,A2-awgn-c01-0-s1,0,0,13,1.00,0",
        "b28,ref,A2 sensitivity,A2-awgn-c01-0-s1,1,1,10,1.00,0",
        "b28,ref,A2 sensitivity,A2-awgn-c01-0-s1,2,2,2,1.00,0",
        "b28,ref,I2 Farnsworth,I2-farnsworth-o1-0-s1,0,0,8,3.11,0",
        "b28,ref,I2 Farnsworth,I2-farnsworth-o1-0-s1,1,2,8,7.84,0",
        "b28,ref,I2 Farnsworth,I2-farnsworth-o1-0-s1,2,8,8,11.53,1"]
    assert str(tmp_path) not in text   # committed beside a record: no machine's folders
    # the numbers of the table are computed from the committed file: read back, they are the JSON's
    rows = experiments.read_search_error_scores(csv_path)
    assert [r["left_out"] for r in rows] == [False] * 5 + [True] and rows[5]["farnsworth_factor"] == 11.53
    saved = json.loads(md.with_suffix(".json").read_text(encoding="utf-8"))
    kept = [r for r in rows if not r["left_out"]]
    assert experiments._search_stats_by_group(kept) == saved["groups"] and experiments._search_stats(kept) == saved["pooled"]
    assert experiments._search_stats_by_group([r for r in rows if r["left_out"]]) == saved["left_out"]["groups"]
    # without the cut every row is kept
    experiments.main(args)
    assert [line.rsplit(",", 1)[1] for line in csv_path.read_text(encoding="utf-8").splitlines()[1:]] == ["0"] * 6


# --- a run's search statistics, from the command line ---

def test_the_search_stats_command_writes_the_tables_and_prints_the_totals(tmp_path, capsys):
    # the suite and the statistics file are test_search_stats.py's: a real file of two oracle channels, as both forms
    for version, path in ((1, V1), (2, V2)):
        suite = tmp_path / f"v{version}"
        suite.mkdir()
        _suite(suite, path.read_text(encoding="utf-8"))
        experiments.main(["search-stats", "--out", str(suite), "--run", "r1"])
        out = capsys.readouterr().out
        assert "r1: 2 test cases, 4 channels, 4.2 channel-seconds; live stories" in out
        assert str(suite / "experiments" / "search-stats-r1.md") in out
        assert (suite / "experiments" / "search-stats-r1.md").exists()
        saved = json.loads((suite / "experiments" / "search-stats-r1.json").read_text(encoding="utf-8"))
        assert saved["test_cases"] == 2 and saved["versions"] == [version]
        experiments.main(["search-stats", "--out", str(suite), "--run", "r1", "--only", r"^B2-"])
        assert "r1: 1 test cases, 2 channels" in capsys.readouterr().out
    with pytest.raises(SystemExit):      # the run is needed
        experiments.main(["search-stats", "--out", str(tmp_path)])


# --- false characters on the noise-only group ---

def test_the_symbols_of_a_decoded_text_are_counted_as_the_scorer_counts_them():
    assert experiments.decoded_symbols("CQ DE") == ["C", "Q", " ", "D", "E"]
    assert experiments.decoded_symbols("<AR> E <SK>") == ["<AR>", " ", "E", " ", "<SK>"]    # a prosign is one symbol
    assert experiments.decoded_symbols("<E") == ["<", "E"]                                   # no closing bracket
    assert experiments.decoded_symbols("<A B> C") == ["<A B>", " ", "C"]                     # the first one after it
    assert experiments.decoded_symbols("") == []


def _noise_run(tmp_path):
    """Two N2 test cases of run "r1" (and a seed-2 one), whose channels decoded the texts below; the second has an
    unscored channel."""
    decoded = {"N2-noise-0-s1": (1800.0, [("", True), ("E T", True), ("<AR> E <SK>", True), ("<E", True)]),
               "N2-noise-1-s1": (1800.0, [("E", True), ("", True), ("ET E", True), ("EEEEE", False)]),
               "N2-noise-0-s2": (1800.0, [("E", True)])}
    (tmp_path / "manifest.json").write_text(json.dumps({"suite": "n2", "recordings": [
        {"name": n, "group": "N2 noise only", "oracle": True, "wav": f"{n}.wav", "labels": f"{n}.json",
         "station_labels": None} for n in decoded]}))
    folder = tmp_path / "results" / "r1"
    folder.mkdir(parents=True)
    for n, (channel_seconds, signals) in decoded.items():
        (folder / f"{n}.json").write_text(json.dumps({"channel_seconds": channel_seconds, "score": {"signals": [
            {"index": i, "scored": scored, "decoded": text} for i, (text, scored) in enumerate(signals)]}}))
    return folder


def test_false_characters_per_channel_hour_on_the_noise_group(tmp_path):
    _noise_run(tmp_path)
    r = experiments.false_characters(tmp_path, "r1")
    # seed 1's two test cases: the decoded symbols that are not word spaces, of the channels the labels score
    # (7 + 4 = 11), over their channel-hours: 1800 s of 4 channels, and 3 of the 4 channels' share of 1800 s (1350 s)
    assert (r["test_cases"], r["channels"], r["characters"], r["channels_with_characters"]) == (2, 7, 11, 5)
    assert r["channel_seconds"] == pytest.approx(3150.0) and r["channel_hours"] == pytest.approx(0.875)
    assert r["per_channel_hour"] == pytest.approx(11 / 0.875)
    assert [(t["test_case"], t["channels"], t["characters"]) for t in r["by_test_case"]] == [
        ("N2-noise-0-s1", 4, 7), ("N2-noise-1-s1", 3, 4)]
    both = experiments.false_characters(tmp_path, "r1", only=r"^N2-")
    assert both["test_cases"] == 3 and both["characters"] == 12
    # the table
    path = experiments.false_character_table(tmp_path, r)
    md = path.read_text(encoding="utf-8")
    assert path == tmp_path / "experiments" / "false-characters-r1.md" and "12.57" in md and "N2-noise-1-s1" in md
    assert json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))["characters"] == 11
    # from the command line
    experiments.main(["false-characters", "--out", str(tmp_path), "--run", "r1"])


def test_false_characters_need_a_run_of_the_engine_and_test_cases(tmp_path):
    folder = _noise_run(tmp_path)
    with pytest.raises(ValueError, match="no results folder named 'nope'"):
        experiments.false_characters(tmp_path, "nope")
    with pytest.raises(ValueError, match="no test case"):
        experiments.false_characters(tmp_path, "r1", only=r"^never$")
    # results scored from decoded files (--score-decoded) carry no channel-seconds: no rate
    bad = json.loads((folder / "N2-noise-0-s1.json").read_text(encoding="utf-8"))
    bad["channel_seconds"] = 0.0
    (folder / "N2-noise-0-s1.json").write_text(json.dumps(bad), encoding="utf-8")
    with pytest.raises(ValueError, match="channel_seconds"):
        experiments.false_characters(tmp_path, "r1")
