"""Synthetic signals shared by the prototype's tests and its experiments (one definition, so a test and an
experiment never drift apart): the channel filter's shape, and a keyed carrier in white noise at a stated S500."""

from __future__ import annotations

import math

import numpy as np

from kz4ap_synth.generate import keying_envelope


def lowpass(rate_hz: float = 1500.0) -> np.ndarray:
    """A 129-tap Blackman-windowed sinc, -6 dB relative to the passband at +/-150 Hz, unity gain at 0 Hz: the
    channel filter's shape."""
    k = np.arange(129) - 64
    h = np.sinc(2 * 150.0 / rate_hz * k) * np.blackman(129)
    return h / h.sum()


def stream(intervals, start_s, duration_s, s500_db, seed, rate_hz=1500.0):
    """A 1 FS carrier keyed by intervals (5 ms raised-cosine edges) from start_s, in white noise giving S500 =
    s500_db: noise power per complex sample rate_hz / (500 Hz x 10^(S500/10)) FS^2 (3 x 10^(-S500/10) FS^2 at
    1500 samples/s)."""
    n = int(round(duration_s * rate_hz))
    rng = np.random.default_rng(seed)
    env = keying_envelope(intervals, start_s, n, int(rate_hz)) if intervals else np.zeros(n)
    power = rate_hz / 500.0 * 10 ** (-s500_db / 10)
    return env * np.exp(1j * rng.uniform(0, 2 * np.pi)) + (
        rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)
