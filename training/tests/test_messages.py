import re

import numpy as np
import pytest

from kz4ap_synth.messages import (
    VE3NEA_CHAR_WEIGHTS,
    VE3NEA_WORD_LENGTH_PROBS,
    callsign,
    contest_exchange,
    cq_call,
    ragchew,
    random_operator,
    random_text,
)
from kz4ap_synth.morse import CODES, symbols

CALL_PATTERN = re.compile(r"^[A-Z]{1,2}\d[A-Z]{1,3}(/P)?$")


def _qso(seed, chat_rounds=0):
    rng = np.random.default_rng(seed)
    return ragchew(rng, random_operator(rng), random_operator(rng), chat_rounds)


def _words(over):
    return over.text.split()


def test_callsigns_look_real():
    rng = np.random.default_rng(1)
    calls = [callsign(rng) for _ in range(500)]
    assert all(CALL_PATTERN.match(c) for c in calls)
    assert len(set(calls)) > 450
    assert any(c.endswith("/P") for c in calls)


def test_the_same_seed_gives_the_same_text():
    assert _qso(3, chat_rounds=1) == _qso(3, chat_rounds=1)
    assert _qso(3) != _qso(4)
    assert random_text(np.random.default_rng(5), 50) == random_text(np.random.default_rng(5), 50)


def test_every_symbol_can_be_keyed():
    texts = [o.text for o in _qso(6, chat_rounds=2)]
    rng = np.random.default_rng(7)
    texts += [cq_call(rng, "K1ABC"), random_text(rng, 300)]
    texts += [contest_exchange(rng, "K1ABC", "W9XYZ") for _ in range(50)]
    for text in texts:
        for word in text.split():
            assert all(s in CODES for s in symbols(word)), word


def test_overs_alternate_starting_with_the_cq():
    overs = _qso(8, chat_rounds=2)
    assert len(overs) == 8 + 2 * 2
    assert [o.sender for o in overs] == [0, 1] * 6
    assert _words(overs[0])[:2] == ["CQ", "CQ"]
    assert _words(overs[0])[-1] == "K"


def test_prosigns_sit_where_operators_send_them():
    overs = _qso(9, chat_rounds=1)
    for over in overs[1:-2]:
        assert _words(over)[-1] in ("<AR>", "<KN>")
    for over in overs[-2:]:
        assert _words(over)[-1] == "<SK>"
    assert all("<SK>" not in _words(o) for o in overs[:-2])
    for over in overs:
        words = _words(over)
        bt = [i for i, w in enumerate(words) if w == "<BT>"]
        assert all(0 < i < len(words) - 1 for i in bt)            # never first or last
        assert all(b - a > 1 for a, b in zip(bt, bt[1:]))          # never two in a row


def test_a_ragchew_carries_the_usual_content_and_abbreviations():
    rng = np.random.default_rng(10)
    a, b = random_operator(rng), random_operator(rng)
    text = " ".join(o.text for o in ragchew(rng, a, b))
    words = set(text.split())
    for abbreviation in ("FB", "OM", "TNX", "UR", "HR", "ES", "WX", "RIG", "ANT", "PWR", "73", "GL", "RST",
                         "NAME", "QTH"):
        assert abbreviation in words, abbreviation
    assert "HPE CUAGN" in text
    for fact in (a.call, b.call, a.name, b.name, a.qth, b.qth, a.rig, b.rig, a.antenna, b.antenna,
                 f"{a.power_w}W", f"{b.power_w}W"):
        assert fact in text, fact


def test_contest_exchanges_include_cq_reports_and_tu():
    rng = np.random.default_rng(11)
    texts = [contest_exchange(rng, "K1ABC", "W9XYZ") for _ in range(200)]
    assert any(t.startswith("CQ TEST K1ABC") for t in texts)
    assert any(t.startswith("W9XYZ 5NN ") for t in texts)
    assert "TU K1ABC" in texts


def test_random_text_follows_ve3nea_statistics():
    words = random_text(np.random.default_rng(12), 20000).split()
    p = np.array(VE3NEA_WORD_LENGTH_PROBS) / sum(VE3NEA_WORD_LENGTH_PROBS)
    lengths = np.array([len(symbols(w)) for w in words])
    assert np.mean(lengths) == pytest.approx(np.sum(np.arange(len(p)) * p), abs=0.05)  # 3.06 characters
    chars = [s for w in words for s in symbols(w)]
    total = sum(VE3NEA_CHAR_WEIGHTS.values())
    for c in ("E", "T", "N", "<BT>", "/"):
        assert chars.count(c) / len(chars) == pytest.approx(VE3NEA_CHAR_WEIGHTS[c] / total, abs=0.004), c
    assert min(lengths) >= 1 and max(lengths) <= 16
