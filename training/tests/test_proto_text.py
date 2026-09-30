import math

import pytest

from kz4ap_proto.text import INVALID_LOGPROB, TextModel, decode_pattern
from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS

TOTAL = sum(VE3NEA_CHAR_WEIGHTS.values())  # 2688


def test_patterns_decode_as_the_engine_reads_them():
    assert [decode_pattern(p) for p in (".-", "-...-", "...-.-", "..--..")] == ["A", "<BT>", "<SK>", "?"]
    assert decode_pattern("........") == "<HH>" and decode_pattern(".........") == "<HH>"
    assert decode_pattern("--.--.") == "*"


def test_text_log_probability_uses_ve3nea_frequencies():
    m = TextModel()
    assert m.char_logprob("E") == pytest.approx(math.log(321 / TOTAL))
    assert m.char_logprob("<KN>") == pytest.approx(math.log(8 / TOTAL))   # valid but not in his table: his rarest
    assert m.char_logprob("*") == INVALID_LOGPROB == pytest.approx(math.log(1e-6))
    assert m.mean_logprob(["E", " ", "T"]) == pytest.approx((math.log(321 / TOTAL) + math.log(236 / TOTAL)) / 2)
    assert m.mean_logprob([" "]) is None and m.mean_logprob([]) is None
