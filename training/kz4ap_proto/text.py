"""Characters from classified elements, and the text log-probability that breaks selection ties (spec 4.6)."""

from __future__ import annotations

import math

from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS
from kz4ap_synth.morse import CODES

PATTERNS = {pattern: symbol for symbol, pattern in CODES.items()}
INVALID_LOGPROB = math.log(1e-6)  # an element sequence that is no Morse character (heuristic: "very unlikely")


def decode_pattern(pattern: str) -> str:
    """The symbol for a dot/dash pattern: eight or more dits read "<HH>", a pattern with no code "*"
    (as engine/src/morse.cpp and the classical decoder)."""
    if len(pattern) >= 8 and set(pattern) == {"."}:
        return "<HH>"
    return PATTERNS.get(pattern, "*")


class TextModel:
    """Unigram log-probability under VE3NEA's CW character frequencies (messages.VE3NEA_CHAR_WEIGHTS, MIT).
    A valid code missing from his table (prosigns other than <BT>, rarer punctuation) gets his rarest
    character's probability (heuristic); "*" gets 1e-6."""

    def __init__(self):
        total = sum(VE3NEA_CHAR_WEIGHTS.values())
        self.logp = {c: math.log(w / total) for c, w in VE3NEA_CHAR_WEIGHTS.items()}
        self.floor = math.log(min(VE3NEA_CHAR_WEIGHTS.values()) / total)

    def char_logprob(self, symbol: str) -> float:
        if symbol in self.logp:
            return self.logp[symbol]
        return self.floor if symbol in CODES else INVALID_LOGPROB

    def mean_logprob(self, symbols) -> float | None:
        """Mean log-probability per character, nats (word spaces ignored); None without characters."""
        values = [self.char_logprob(s) for s in symbols if s != " "]
        return sum(values) / len(values) if values else None
