"""The call-list scoring tool: its forgiving reader and its two reports."""

import json

import pytest

from kz4ap_proto import calls_score
from kz4ap_proto.calls_score import base_call, compare_lists, read_calls_text, score_truth, truth_calls


@pytest.mark.parametrize("word, base", [
    ("K1ABC", "K1ABC"), ("k1abc", "K1ABC"), ("K1ABC/P", "K1ABC"), ("DL/K1ABC", "K1ABC"), ("K1ABC/DL", "K1ABC"), ("S58W", "S58W"),
    ("IK0HBN", "IK0HBN"), ("E51MWA", "E51MWA"), ("3DA0RU", "3DA0RU"), ("9A1A", "9A1A"), ("W1A", "W1A"), ("2E0ABC", "2E0ABC"),
    ("A61AA", "A61AA"), ("TM100TF", "TM100TF"),
    ("5NN", None), ("TEST", None), ("CQ", None), ("599", None), ("Q1ABC", None), ("7030.1", None), ("", None), ("K1", None), ("1234Z", None),
])
def test_base_call(word, base):
    assert base_call(word) == base


def test_kz4ap_skimmer_csv():
    text = ("call,base,freq_hz,freq_khz,first_s,reported_s,last_s,count,points,known,heard,framed\n"
            "K1ABC/P,K1ABC,-1500.0,,1.0,2.0,3.0,4,3.0,0,1,1\nW2XY,W2XY,200.0,,1.0,2.0,3.0,2,2.0,0,1,0\n")
    listed = read_calls_text(text)
    assert listed.adapter == "csv"
    assert listed.calls["K1ABC"].count == 4
    assert listed.calls["K1ABC"].freqs_khz == [-1.5]
    assert listed.calls["W2XY"].count == 2


def test_rbn_csv_takes_dx_not_the_skimmer():
    text = "callsign,de_pfx,de_cont,freq,band,dx,dx_pfx,db,date,speed\nK5TR,K,NA,7025.1,40m,DL1ABC,DL,12,x,25\nK5TR,K,NA,7025.1,40m,DL1ABC,DL,14,x,25\n"
    listed = read_calls_text(text)
    assert set(listed.calls) == {"DL1ABC"}
    assert listed.calls["DL1ABC"].count == 2
    assert listed.calls["DL1ABC"].freqs_khz == [7025.1, 7025.1]


def test_telnet_spots():
    text = "DX de W3LPL-#: 7025.1 K1ABC 12 dB 25 WPM CQ 1234Z\nDX de W3LPL-#:  7031.5  DL1XY/P  CW  9 dB 28 WPM CQ 1235Z\nDX de W3LPL-#: 7025.1 K1ABC 14 dB 25 WPM CQ 1244Z\n"
    listed = read_calls_text(text)
    assert listed.adapter == "spots"
    assert listed.calls["K1ABC"].count == 2
    assert listed.calls["DL1XY"].freqs_khz == [7031.5]


def test_a_spot_is_kept_whatever_the_readers_grammar_makes_of_its_word():
    # Review 5: these went missing with nothing skipped, so the comparison would flatter this decoder. A spot is the spotter's test, not ours.
    words = ["9A3B", "D4C", "E7DX", "C6AGU", "P3F", "T6A", "J6M", "K1ABC/QRP", "LZ1SPACE", "K1ABC"]
    text = "".join(f"DX de ZZ0ZZZ-#: {7000.5 + i}  {w} 14 dB 24 WPM CQ 1538Z\n" for i, w in enumerate(words))
    listed = read_calls_text(text)
    assert listed.adapter == "spots"
    assert sorted(listed.calls) == sorted(["9A3B", "D4C", "E7DX", "C6AGU", "P3F", "T6A", "J6M", "K1ABC", "LZ1SPACE"])
    assert listed.calls["K1ABC"].count == 2  # K1ABC/QRP and K1ABC: one station
    assert listed.calls["D4C"].freqs_khz == [7001.5]
    assert listed.skipped_lines == 0


def test_cw_skimmer_spots_as_it_really_sends_them_playing_a_wav_with_no_tags():
    # CW Skimmer 2.1: no operator's call on the Settings page, the frequency an offset from the center, kHz, either sign, the clock 0000Z.
    text = (
        "Welcome to the CW Skimmer Telnet cluster port!\n"
        "DX de -#:           -3.8  K6UU           34 dB  31 WPM                0000Z\n"
        "00:09:26.5 DX de -#:           65.9  KB1K           14 dB  25 WPM  CQ            0000Z\n"
        "DX de -#:           -3.8  K6UU           34 dB  31 WPM  CQ            0000Z\n"
    )
    listed = read_calls_text(text)
    assert listed.adapter == "spots"
    assert sorted(listed.calls) == ["K6UU", "KB1K"]
    assert listed.calls["K6UU"].freqs_khz == [-3.8, -3.8]
    assert listed.calls["K6UU"].count == 2
    assert listed.calls["KB1K"].freqs_khz == [65.9]


def test_adif():
    text = "header <EOH>\n<CALL:5>K1ABC <FREQ:5:N>7.025 <EOR>\n<CALL:6>DL1XYZ<EOR>"
    listed = read_calls_text(text)
    assert listed.adapter == "adif"
    assert listed.calls["K1ABC"].freqs_khz == [pytest.approx(7025.0)]
    assert "DL1XYZ" in listed.calls


def test_plain_list_and_unknown_log():
    listed = read_calls_text("# calls\nK1ABC\nw2xy\n\nnothing here\n")
    assert listed.adapter == "list"
    assert set(listed.calls) == {"K1ABC", "W2XY"}
    assert listed.skipped_lines == 1
    log = read_calls_text("12:34:56 7025.10 K1ABC 599\n12:35:10 7031.50 DL1XY\n")
    assert log.adapter == "list"
    assert log.calls["K1ABC"].freqs_khz == [7025.1]


def test_compare_counts_near_pairs_and_portable():
    a = read_calls_text("K1ABC/P\nW2XY\nN5JJ\n")
    b = read_calls_text("K1ABC\nW2XZ\nDL1A\n")
    c = compare_lists(a, b)
    assert c.both == ["K1ABC"]
    assert c.only_a == ["N5JJ", "W2XY"]
    assert c.only_b == ["DL1A", "W2XZ"]
    assert c.near_pairs == [("W2XY", "W2XZ")]


def test_truth_precision_recall_attribution():
    run = read_calls_text("call,freq_hz\nK1ABC,1000.0\nW2XY,5000.0\nW2XZ,2000.0\n")
    truth = {"K1ABC": [1040.0], "W2XY": [2000.0], "DL1A": [300.0]}
    s = score_truth(run, truth, 100.0)
    assert s.found == ["K1ABC", "W2XY"]
    assert s.false_calls == ["W2XZ"]
    assert s.missed == ["DL1A"]
    assert s.precision == pytest.approx(2 / 3)
    assert s.recall == pytest.approx(2 / 3)
    assert s.attributed == 1
    assert s.misattributed == [("W2XY", 5000.0, 2000.0)]
    assert s.misreadings == [("W2XZ", "W2XY")]


def test_truth_from_both_label_forms():
    stations = {"stations": [{"freq_hz": 10.0, "call": "W5L", "bursts": []},
                             {"freq_hz": 20.0, "bursts": [{"text": "K1ABC DE W2XY GM"}]}]}
    assert truth_calls(stations) == {"W5L": [10.0], "W2XY": [20.0]}
    signals = {"signals": [{"text": "CQ TEST DL9W DL9W", "freq_offset_hz": 30.0}]}
    assert truth_calls(signals) == {"DL9W": [30.0]}
    qso = {"signals": [{"text": "x", "freq_offset_hz": 100.0, "senders": [{"call": "K1IT"}, {"call": "K8GMI/P", "freq_offset_hz": 5.0}]}]}
    assert truth_calls(qso) == {"K1IT": [100.0], "K8GMI": [105.0]}


def test_command_line(tmp_path, capsys):
    run = tmp_path / "run.csv"
    run.write_text("call,freq_hz\nK1ABC,1000.0\nW2XZ,5.0\n")
    labels = tmp_path / "labels.json"
    labels.write_text(json.dumps({"stations": [{"freq_hz": 1000.0, "call": "K1ABC", "bursts": []}]}))
    out = tmp_path / "scores.csv"
    assert calls_score.main(["truth", str(run), str(labels), "--out", str(out)]) == 0
    assert "true calls found 1; false calls 1; precision 0.500; recall 1.000" in capsys.readouterr().out
    assert out.read_text().splitlines()[1] == "K1ABC,true,1"
    other = tmp_path / "other.txt"
    other.write_text("DX de A1-#: 7025.1 K1ABC 12 dB 25 WPM CQ 1234Z\n")
    assert calls_score.main(["compare", str(run), str(other)]) == 0
    assert "in both 1; in run only 1; in other only 0" in capsys.readouterr().out


_RBN = ("callsign,de_pfx,de_cont,freq,band,dx,dx_pfx,dx_cont,mode,db,date,speed,tx_mode\n"
        "N2GZ,K,NA,7026.9,40m,AJ1G,K,NA,CQ,20,2017-08-21 18:00:04,24,CW\n"
        "N2GZ,K,NA,14028.3,20m,N4BP,K,NA,CQ,19,2017-08-21 18:00:34,29,CW\n"
        "W3LPL,K,NA,7031.5,40m,W3UL,K,NA,CQ,11,2017-08-21 18:15:00,23,CW\n"
        "DK8NE,DL,EU,7042.1,40m,K9BGL,K,NA,CQ,11,2017-08-21 18:05:19,28,CW\n"
        "N2GZ,K,NA,7080.0,40m,K1RY,K,NA,CQ,9,2017-08-21 18:05:00,45,RTTY\n"
        "N2GZ,K,NA,7037.1,40m,W3IUU,K,NA,CQ,5,2017-08-21 17:59:59,22,CW\n")


def test_rbn_filters_by_spotter_continent_window_span_and_mode():
    every = read_calls_text(_RBN)
    assert set(every.calls) == {"AJ1G", "N4BP", "W3UL", "K9BGL", "K1RY", "W3IUU"}
    own = read_calls_text(_RBN, calls_score.SpotFilter(spotters={"N2GZ"}, start_utc="2017-08-21T18:00:00.093Z",
                                                        end_utc="2017-08-21 18:15:00", low_khz=7000.0, high_khz=7192.0, tx_mode="CW"))
    assert set(own.calls) == {"AJ1G"}  # not 20 m, not RTTY, not before the window
    assert own.filtered == 5
    na = read_calls_text(_RBN, calls_score.SpotFilter(continent="NA", start_utc="2017-08-21 18:00:00", end_utc="2017-08-21 18:15:01",
                                                       low_khz=7000.0, high_khz=7192.0, tx_mode="cw"))
    assert set(na.calls) == {"AJ1G", "W3UL"}  # the window's end is exclusive; the European skimmer's spot dropped
    with pytest.raises(ValueError):
        read_calls_text("DX de A1-#: 7025.1 K1ABC 12 dB 25 WPM CQ 1234Z\n", calls_score.SpotFilter(continent="NA"))


def test_kz4ap_skimmer_csv_with_absolute_frequency_and_utc():
    text = ("call,base,freq_hz,freq_khz,first_s,reported_s,last_s,count,points,known,heard,framed,first_utc,last_utc\n"
            "AJ1G,AJ1G,-68953.6,7027.046421,8.6,37.5,141.6,11,8.2,0,1,1,2017-08-21T18:01:08.600Z,2017-08-21T18:03:21.600Z\n")
    listed = read_calls_text(text)
    assert listed.calls["AJ1G"].freqs_khz == [7027.046421]
    assert listed.calls["AJ1G"].row["first_utc"] == "2017-08-21T18:01:08.600Z"


def test_command_line_rbn_filters(tmp_path, capsys):
    run = tmp_path / "run.csv"
    run.write_text("call,freq_khz\nAJ1G,7027.0\nK2DSW,7031.3\n")
    rbn = tmp_path / "20170821.csv"
    rbn.write_text(_RBN)
    assert calls_score.main(["compare", str(run), str(rbn), "--spotter", "n2gz", "--from", "2017-08-21 18:00:00",
                             "--to", "2017-08-21 18:31:00", "--khz", "7000", "7192", "--tx-mode", "CW"]) == 0
    out = capsys.readouterr().out
    assert "in both 1; in run only 1; in other only 0" in out
    assert "5 rows outside the filters" in out
