"""Keying timing: how long each element and space lasts for a given sender.

"machine" is exact PARIS timing (this project's; it keeps milestone-1
recordings unchanged). "computer", "paddle", "bug" and "hand" are VE3NEA's
DeepCW styles Computer, Paddle, Vibroplex and HandKey: every duration is
T * exp(N(mu, sigma^2)), T = 1.2 s / WPM, with his mu and sigma per element
(DeepCW data_generation.ipynb cell 9, keying_stats.py, commit 2c8fdac, MIT;
docs/research/deepcw-generator-notes.md §1). His one per-operator variation
is a key-on/key-off imbalance delta ~ N(0, (0.1 T)^2), added to every mark
and subtracted from every space (draw_imbalance_dits). His training mix is
hand 0.25, paddle 0.50, computer 0.25 (draw_style); his speed range is
12-48 WPM in the committed training_settings.py but 8-50 WPM in the notebook
cell that writes it, so which trained his published model is unknown.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .morse import CODES, keying_intervals, symbols

MIN_DITS = 0.2  # no element or space is drawn shorter than this, dits
VE3NEA_STYLE_MIX = (("hand", 0.25), ("paddle", 0.50), ("computer", 0.25))
VE3NEA_IMBALANCE_SIGMA_DITS = 0.1           # standard deviation of the per-operator imbalance, dits
VE3NEA_WPM_RANGE = (12.0, 48.0)             # committed training_settings.py; the notebook says 8-50


@dataclass(frozen=True)
class Duration:
    median_dits: float  # median duration, dits (exp(mu))
    sigma: float        # standard deviation of ln(duration); 0 = exact


@dataclass(frozen=True)
class KeyingStyle:
    dit: Duration
    dah: Duration
    element_gap: Duration  # space inside a character
    char_gap: Duration     # space between characters
    word_gap: Duration     # space between words


def _ve3nea(mu, sigma) -> KeyingStyle:
    """A style from VE3NEA's (dot, dash, intra, char, word) mu and sigma columns."""
    return KeyingStyle(*(Duration(math.exp(m), s) for m, s in zip(mu, sigma)))


STYLES = {
    # Exact PARIS timing (this project's default; not a VE3NEA style).
    "machine": KeyingStyle(Duration(1, 0), Duration(3, 0), Duration(1, 0), Duration(3, 0), Duration(7, 0)),
    # VE3NEA's Computer: "all timing is accurate", yet dots and element spaces vary by 5%.
    "computer": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.016, 0.05, 0.016, 0.008)),
    # VE3NEA's Paddle: character and word spaces are the operator's.
    "paddle": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.016, 0.05, 0.2, 0.2)),
    # VE3NEA's Vibroplex (a bug): dashes and spaces are the operator's.
    "bug": _ve3nea((0, 1.10, 0, 1.10, 1.94), (0.05, 0.2, 0.05, 0.2, 0.2)),
    # VE3NEA's HandKey: everything by hand, heavy dashes (median e^1.5 = 4.48 dits), wide spacing.
    "hand": _ve3nea((0, 1.50, 0, 1.50, 2.0), (0.15, 0.3, 0.2, 0.3, 0.2)),
}

SPEED_PROFILES = ("step", "ramp")


def draw_style(rng: np.random.Generator) -> str:
    """A keying style drawn with VE3NEA's training mix (hand 0.25, paddle 0.50, computer 0.25)."""
    names = [name for name, _ in VE3NEA_STYLE_MIX]
    return names[int(rng.choice(len(names), p=[p for _, p in VE3NEA_STYLE_MIX]))]


def draw_imbalance_dits(rng: np.random.Generator) -> float:
    """One operator's key-on/key-off imbalance, dits: N(0, 0.1^2), as VE3NEA draws it."""
    return float(VE3NEA_IMBALANCE_SIGMA_DITS * rng.standard_normal())


def word_wpm(index: int, count: int, wpm: float, wpm_end: float | None, profile: str) -> float:
    """Speed of word `index` of `count`."""
    if profile not in SPEED_PROFILES:
        raise ValueError(f"unknown speed profile {profile!r}")
    if wpm_end is None or count < 2:
        return wpm
    if profile == "step":
        return wpm if index < count // 2 else wpm_end
    return wpm + (wpm_end - wpm) * index / (count - 1)


def farnsworth_gap_s(char_wpm: float, overall_wpm: float) -> float:
    """Gap timebase T_g, s, for Farnsworth spacing: characters at char_wpm, overall speed
    overall_wpm (PARIS). The ARRL standard (J. Bloom, KE3Z, "A Standard for Morse Timing Using
    the Farnsworth Technique", QEX, April 1990) spreads the added time 60/s - 37.2/c seconds per
    PARIS over its 19 gap units (four character gaps of 3, one word gap of 7): T_g = that / 19.
    At s = c it is the dit, 1.2 s / c."""
    if not 0 < overall_wpm <= char_wpm:
        raise ValueError(f"Farnsworth overall speed {overall_wpm} WPM must be positive and at most "
                         f"the character speed {char_wpm} WPM")
    return (60.0 / overall_wpm - 37.2 / char_wpm) / 19.0


def timed_intervals(text: str, wpm: float, style: str = "machine", rng: np.random.Generator | None = None,
                    wpm_end: float | None = None, profile: str = "step",
                    imbalance_dits: float = 0.0, farnsworth_wpm: float | None = None) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, from 0 s.

    wpm_end: the speed at the end (None = constant); profile "step" switches at
    the middle word, "ramp" changes linearly from word to word. imbalance_dits:
    every mark is longer, and every space shorter, by this many dits (a
    transmitter that keys on and off at different speeds). farnsworth_wpm: the
    overall speed with Farnsworth spacing: character and word gaps are drawn on
    the timebase farnsworth_gap_s(character speed, farnsworth_wpm) instead of the
    dit (elements and element spaces stay at the character speed); None =
    standard spacing. Character and word spaces are drawn directly from their
    own distributions (VE3NEA assembles them from several draws; the medians are
    the same). With machine keying, constant speed, no imbalance and standard
    spacing the result is exactly keying_intervals().
    """
    if style not in STYLES:
        raise ValueError(f"unknown keying style {style!r}")
    if profile not in SPEED_PROFILES:
        raise ValueError(f"unknown speed profile {profile!r}")
    if farnsworth_wpm is not None:
        farnsworth_gap_s(min(wpm, wpm_end if wpm_end is not None else wpm), farnsworth_wpm)  # validates
    if style == "machine" and wpm_end is None and imbalance_dits == 0.0 and farnsworth_wpm is None:
        return keying_intervals(text, wpm)
    k = STYLES[style]
    if rng is None and any(d.sigma > 0 for d in (k.dit, k.dah, k.element_gap, k.char_gap, k.word_gap)):
        raise ValueError(f"keying style {style!r} needs a random generator")

    def length(d: Duration, unit_s: float, extra_units: float) -> float:
        units = d.median_dits if d.sigma == 0 else d.median_dits * math.exp(d.sigma * rng.standard_normal())
        return max(units + extra_units, MIN_DITS) * unit_s

    words = [[CODES[s] for s in symbols(w) if s in CODES] for w in text.upper().split()]
    words = [w for w in words if w]
    out: list[tuple[float, float]] = []
    t = 0.0
    for wi, patterns in enumerate(words):
        char_wpm = word_wpm(wi, len(words), wpm, wpm_end, profile)
        dit_s = 1.2 / char_wpm
        gap_s = dit_s if farnsworth_wpm is None else farnsworth_gap_s(char_wpm, farnsworth_wpm)
        # the same imbalance, s, in units of the gap timebase (exactly imbalance_dits with standard spacing,
        # so existing recordings draw bit-identical durations)
        gap_imbalance = imbalance_dits if farnsworth_wpm is None else imbalance_dits * dit_s / gap_s
        for ci, pattern in enumerate(patterns):
            for ei, element in enumerate(pattern):
                mark = length(k.dit if element == "." else k.dah, dit_s, imbalance_dits)
                out.append((t, t + mark))
                t += mark
                if ei < len(pattern) - 1:
                    t += length(k.element_gap, dit_s, -imbalance_dits)
            if ci < len(patterns) - 1:
                t += length(k.char_gap, gap_s, -gap_imbalance)
        if wi < len(words) - 1:
            t += length(k.word_gap, gap_s, -gap_imbalance)
    return out
