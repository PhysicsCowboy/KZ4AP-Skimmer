import pytest

from kz4ap_synth.morse import CODES, dit_seconds, keying_intervals


def flat(intervals):
    return [x for interval in intervals for x in interval]


def test_dit_length_follows_paris_standard():
    assert dit_seconds(20) == pytest.approx(0.06)


def test_single_dit():
    assert flat(keying_intervals("E", 20)) == pytest.approx([0.0, 0.06])


def test_dit_dah_with_element_gap():
    assert flat(keying_intervals("A", 20)) == pytest.approx([0.0, 0.06, 0.12, 0.30])


def test_character_gap_is_three_dits():
    assert flat(keying_intervals("EE", 20)) == pytest.approx([0.0, 0.06, 0.24, 0.30])


def test_word_gap_is_seven_dits():
    assert flat(keying_intervals("E E", 20)) == pytest.approx([0.0, 0.06, 0.48, 0.54])


def test_unknown_characters_are_skipped():
    assert keying_intervals("E#", 20) == keying_intervals("E", 20)


def test_table_matches_engine_table():
    assert len(CODES) == 44
    assert CODES["K"] == "-.-"
    assert CODES["("] == "-.--."
