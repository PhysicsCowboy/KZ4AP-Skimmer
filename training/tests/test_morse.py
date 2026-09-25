import re
from pathlib import Path

import pytest

from kz4ap_synth.morse import CODES, dit_seconds, keying_intervals, symbols


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
    assert len(CODES) == 62
    source = (Path(__file__).resolve().parents[2] / "engine" / "src" / "morse.cpp").read_text()
    engine = {s.replace('\\"', '"'): p for s, p in re.findall(r'\{"((?:\\"|[^"])+)",\s*"([.-]+)"\}', source)}
    assert engine == CODES


def test_prosign_is_one_symbol_without_character_gaps():
    # <SK> is ...-.- sent as one symbol: only 1-dit gaps between its elements.
    assert flat(keying_intervals("<SK>", 20)) == pytest.approx(
        [0.0, 0.06, 0.12, 0.18, 0.24, 0.30, 0.36, 0.54, 0.60, 0.66, 0.72, 0.90])
    assert keying_intervals("<SK>", 20) != keying_intervals("SK", 20)


def test_characters_sharing_a_prosign_code_are_not_in_the_table():
    for c in "(=+&":
        assert c not in CODES
    assert keying_intervals("E(", 20) == keying_intervals("E", 20)


def test_symbols_keeps_prosign_tokens_whole():
    assert symbols("K1ABC<KN>") == ["K", "1", "A", "B", "C", "<KN>"]
    assert symbols("A<B") == ["A", "<", "B"]
