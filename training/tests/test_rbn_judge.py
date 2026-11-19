"""The RBN judge of checkpoints 3 and 4: its classes, its window's edges, its counts of skimmers and its table."""

import csv

from kz4ap_proto import rbn_judge
from kz4ap_proto.rbn_judge import Window, judge, parse_utc, read_rbn, read_run

HEADER = "callsign,de_pfx,de_cont,freq,band,dx,dx_pfx,dx_cont,mode,db,date,speed,tx_mode\n"


def _day(tmp_path, lines):
    p = tmp_path / "day.csv"
    p.write_text(HEADER + "".join(line + "\n" for line in lines), encoding="utf-8")
    return read_rbn(p)


def _spot(skimmer, khz, dx, when, cont="NA", mode="CW"):
    return f"{skimmer},K,{cont},{khz},40m,{dx},K,NA,CQ,10,2017-08-21 {when},25,{mode}"


WINDOW = Window(parse_utc("2017-08-21 18:00:00"), 600.0, 7096.0)   # 18:00:00 to 18:10:00, 7000 to 7192 kHz


def test_classes(tmp_path):
    spots = _day(tmp_path, [
        _spot("W2AXR", 7042.1, "K9BGL", "18:01:00"),          # in the window
        _spot("N2GZ", 7042.1, "K9BGL", "18:01:04"),
        _spot("N2GZ", 7042.1, "K9BGL", "18:02:04"),           # the same skimmer again: counted once
        _spot("K8ND", 3540.1, "W1AW", "18:01:27"),            # in time, out of the span: that day only
        _spot("K8ND", 7030.0, "N4ZZ", "12:00:00"),            # out of time: that day only
        _spot("K8ND", 7030.0, "K1XYZ", "18:03:00", mode="RTTY"),  # not CW: ignored
    ])
    v = judge(["K9BGL", "W1AW", "K9BGM", "N4ZY", "K1XYZ", "DL1ABC"], spots, WINDOW)
    assert (v["K9BGL"].kind, v["K9BGL"].skimmers) == ("rbn_window", 2)
    assert v["W1AW"].kind == "rbn_day"
    assert (v["K9BGM"].kind, v["K9BGM"].one_edit_from) == ("misread_window", ["K9BGL"])
    assert (v["N4ZY"].kind, v["N4ZY"].one_edit_from) == ("misread_day", ["N4ZZ"])
    assert v["K1XYZ"].kind == "unknown"
    assert v["DL1ABC"].kind == "unknown"


def test_window_edges_are_inclusive(tmp_path):
    spots = _day(tmp_path, [_spot("A", 7000.0, "K1AA", "18:00:00"), _spot("B", 7192.0, "K1BB", "18:10:00"),
                            _spot("C", 6999.9, "K1CC", "18:05:00"), _spot("D", 7050.0, "K1DD", "18:10:01")])
    v = judge(["K1AA", "K1BB", "K1CC", "K1DD"], spots, WINDOW)
    assert [v[c].kind for c in ("K1AA", "K1BB", "K1CC", "K1DD")] == ["rbn_window", "rbn_window", "rbn_day", "rbn_day"]
    late = Window(parse_utc("2017-08-21T18:00:00.05Z"), 600.0, 7096.0)   # a start with a fraction leaves 18:00:00 out
    assert judge(["K1AA"], spots, late)["K1AA"].kind == "rbn_day"


def test_a_spotted_word_that_is_no_base_call_is_kept_as_spotted(tmp_path):
    # The RBN's own busts (W6SSEQP, S5S) are matched as spotted; a /P call by its base.
    spots = _day(tmp_path, [_spot("A", 7050.0, "W6SSEQP", "18:01:00"), _spot("A", 7050.0, "K1ABC/P", "18:01:00"),
                            _spot("A", 7050.0, "S5S", "11:00:00")])
    v = judge(["W6SSEQP", "K1ABC", "SD5S"], spots, WINDOW)
    assert v["W6SSEQP"].kind == "rbn_window"
    assert v["K1ABC"].kind == "rbn_window"
    assert (v["SD5S"].kind, v["SD5S"].one_edit_from) == ("misread_day", ["S5S"])


def test_table_and_counts(tmp_path):
    spots = _day(tmp_path, [_spot("W2AXR", 7042.1, "K9BGL", "18:01:00"), _spot("VE6JY", 7030.0, "W0AAA", "18:02:00"),
                            _spot("G4ZFE", 7030.0, "G0AAA", "18:02:00", cont="EU"),
                            *[_spot("X", 7030.0, c, "12:00:00") for c in ("N4AB", "N4AC", "N4AD", "N4AE")]])
    ours = tmp_path / "ours.csv"
    ours.write_text("call,base,freq_hz\nK9BGL/P,K9BGL,1.0\nN4AA,N4AA,2.0\n", encoding="utf-8")
    cws = tmp_path / "spots.txt"
    cws.write_text("12:00:01.5 DX de -#: -54.0 K9BGL 20 dB 25 WPM CQ 0000Z\nnoise\nDX de -#: 3.1 G0AAA 9 dB 22 WPM CQ 0000Z\n",
                   encoding="utf-8")
    runs = {"cw_skimmer": read_run(cws), "ours": read_run(ours)}
    assert runs == {"cw_skimmer": {"K9BGL", "G0AAA"}, "ours": {"K9BGL", "N4AA"}}
    table = rbn_judge.rows("rec", "test", runs, spots, WINDOW, na_window=True)
    out = tmp_path / "calls.csv"
    rbn_judge.write_rows(out, list(runs), table)
    got = list(csv.DictReader(out.open(encoding="utf-8")))
    assert [r["call"] for r in got] == ["G0AAA", "K9BGL", "N4AA", "W0AAA"]   # W0AAA: a North American window call, no one's
    assert got[2]["one_edit_from"] == "N4AB/N4AC/N4AD"                         # the first three only
    assert (got[3]["cw_skimmer"], got[3]["ours"], got[3]["rbn_skimmers_in_window"]) == ("0", "0", "1")
    assert rbn_judge.counts(got, "ours") == {"calls": 2, "confirmed": 1, "unconfirmed": 1, "rbn_window": 1, "rbn_day": 0,
                                             "misread_window": 0, "misread_day": 1, "unknown": 0}
    rbn_judge.write_rows(out, list(runs), table[:1], append=True)
    assert len(list(csv.DictReader(out.open(encoding="utf-8")))) == 5


def test_main(tmp_path, capsys):
    day = tmp_path / "day.csv"
    day.write_text(HEADER + _spot("W2AXR", 7042.1, "K9BGL", "18:01:00") + "\n", encoding="utf-8")
    ours = tmp_path / "ours.csv"
    ours.write_text("call,base\nK9BGL,K9BGL\nK9BGM,K9BGM\n", encoding="utf-8")
    out = tmp_path / "calls.csv"
    assert rbn_judge.main([str(day), "--recording", "r", "--role", "t", "--start", "2017-08-21T18:00:00Z", "--seconds", "600",
                           "--center-khz", "7096", "--run", f"ours={ours}", "--out", str(out)]) == 0
    assert "r ours: calls 2, confirmed 1, unconfirmed 1, rbn_window 1, rbn_day 0, misread_window 1" in capsys.readouterr().out
    assert out.read_text(encoding="utf-8").splitlines()[0] == "recording,role,call,class,rbn_skimmers_in_window,one_edit_from,ours"


def test_known_list_splits_the_unspotted_calls_one_sided(tmp_path):
    # MASTER.SCP's form: a header, comments, a /P entry by its base call. In the list = likely real; not in it = undecided.
    scp = tmp_path / "MASTER.SCP"
    scp.write_text("!!Order,1,1\r\n#\r\n# Super Check Partial\r\nDL1ABC\r\nK9BGM/P\r\nW1AW\r\n", encoding="utf-8")
    known = rbn_judge.read_known(scp)
    assert known == {"DL1ABC", "K9BGM", "W1AW"}
    spots = _day(tmp_path, [_spot("W2AXR", 7042.1, "K9BGL", "18:01:00")])
    runs = {"ours": {"K9BGL", "K9BGM", "DL1ABC", "JA1XYZ"}}
    table = rbn_judge.rows("rec", "test", runs, spots, WINDOW, known=known)
    out = tmp_path / "calls.csv"
    rbn_judge.write_rows(out, list(runs), table, known=True)
    got = list(csv.DictReader(out.open(encoding="utf-8")))
    assert list(got[0]) == ["recording", "role", "call", "class", "rbn_skimmers_in_window", "one_edit_from", "in_known_list", "ours"]
    by_call = {r["call"]: (r["class"], r["in_known_list"]) for r in got}
    assert by_call == {"DL1ABC": ("unknown", "1"), "JA1XYZ": ("unknown", "0"), "K9BGL": ("rbn_window", "0"),
                       "K9BGM": ("misread_window", "1")}   # membership leaves every class as it was
    c = rbn_judge.counts(got, "ours")
    assert (c["unknown"], c["unknown_listed"], c["unknown_unlisted"], c["misread_window"], c["misread_listed"]) == (2, 1, 1, 1, 1)


def test_main_with_a_known_list(tmp_path, capsys):
    day = tmp_path / "day.csv"
    day.write_text(HEADER + _spot("W2AXR", 7042.1, "K9BGL", "18:01:00") + "\n", encoding="utf-8")
    ours = tmp_path / "ours.csv"
    ours.write_text("call,base\nK9BGL,K9BGL\nDL1ABC,DL1ABC\n", encoding="utf-8")
    scp = tmp_path / "MASTER.SCP"
    scp.write_text("DL1ABC\n", encoding="utf-8")
    out = tmp_path / "calls.csv"
    assert rbn_judge.main([str(day), "--recording", "r", "--start", "2017-08-21T18:00:00Z", "--seconds", "600", "--center-khz", "7096",
                           "--run", f"ours={ours}", "--known", str(scp), "--out", str(out)]) == 0
    assert "unknown 1, unknown_listed 1, unknown_unlisted 0, misread_listed 0" in capsys.readouterr().out
    assert out.read_text(encoding="utf-8").splitlines()[0].endswith("one_edit_from,in_known_list,ours")


def test_us_licenses_two_sided_for_us_calls_only(tmp_path):
    # HD.dat: field 5 the call, field 6 the status (A active); a call with an expired and an active license is active.
    hd = tmp_path / "HD.dat"
    hd.write_text("HD|1|x||K9BGL|A|HA|\nHD|2|x||W1AW|E|HA|\nHD|3|x||N4AA|C|HA|\nHD|4|x||N4AA|A|HA|\nEN|5|x||K1XYZ|A|\n",
                  encoding="latin-1")
    us = rbn_judge.read_us_licenses(hd)
    assert us == {"K9BGL": "active", "W1AW": "expired", "N4AA": "active"}   # only HD records count
    assert [rbn_judge.us_license(c, us) for c in ("K9BGL", "W1AW", "K1XYZ", "DL1ABC", "5NN5NN", "AM1ABC")] == \
        ["active", "expired", "never", "", "", ""]                              # AM is Spain's, not the US form
    spots = _day(tmp_path, [_spot("W2AXR", 7042.1, "K9BGL", "18:01:00")])
    runs = {"ours": {"K9BGL", "K1XYZ", "DL1ABC", "N4AA"}}
    table = rbn_judge.rows("rec", "test", runs, spots, WINDOW, us=us)
    out = tmp_path / "calls.csv"
    rbn_judge.write_rows(out, list(runs), table, us=True)
    got = list(csv.DictReader(out.open(encoding="utf-8")))
    assert list(got[0])[-2:] == ["us_license", "ours"]
    assert {r["call"]: (r["class"], r["us_license"]) for r in got} == {
        "DL1ABC": ("unknown", ""), "K1XYZ": ("unknown", "never"), "K9BGL": ("rbn_window", "active"), "N4AA": ("unknown", "active")}
    c = rbn_judge.counts(got, "ours")
    assert (c["us_never"], c["unknown_us_active"], c["unknown"]) == (1, 1, 3)


def test_main_with_both_lists(tmp_path, capsys):
    day = tmp_path / "day.csv"
    day.write_text(HEADER + _spot("W2AXR", 7042.1, "K9BGL", "18:01:00") + "\n", encoding="utf-8")
    ours = tmp_path / "ours.csv"
    ours.write_text("call,base\nK9BGL,K9BGL\nK1XYZ,K1XYZ\n", encoding="utf-8")
    scp = tmp_path / "MASTER.SCP"
    scp.write_text("K1XYZ\n", encoding="utf-8")
    hd = tmp_path / "HD.dat"
    hd.write_text("HD|1|x||K9BGL|A|HA|\n", encoding="latin-1")
    out = tmp_path / "calls.csv"
    assert rbn_judge.main([str(day), "--recording", "r", "--start", "2017-08-21T18:00:00Z", "--seconds", "600", "--center-khz", "7096",
                           "--run", f"ours={ours}", "--known", str(scp), "--us-licenses", str(hd), "--out", str(out)]) == 0
    assert "misread_listed 0, us_never 1, unknown_us_active 0" in capsys.readouterr().out
    assert out.read_text(encoding="utf-8").splitlines()[0].endswith("one_edit_from,in_known_list,us_license,ours")
