"""A diffuse tail: delayed, scattered copies of a keyed carrier (optional; off unless a signal sets tail_db).

The owner's 80 m "auroral" clips (W1AW's 35 WPM practice text, 2026-11-19) carry, after every mark's fall, power
that stays about 10 dB under the mark for 4 to 12 ms and is about 15 dB under it 40 ms later, incoherent with
the mark, and whose level does not follow the mark's fades (docs/signal-processing.md A.11, "Diffuse tail").
Modelled here as the standard diffuse-scatter channel: a tapped delay line whose taps are independent complex
Gaussian (Rayleigh) gains with their own Doppler spectrum, under an exponential power-delay profile, added to the
signal's direct path (which keeps its own fading):

    s_tail(t) = A e^(j(2 pi f t + phi)) sum_k sqrt(w_k) g_k(t) env(t - d_k),
    d_k = (k + 1/2) TAIL_TAP_S,  w_k proportional to exp(-d_k / tau),  sum_k w_k = 10^(tail_db / 10),

for k = 0 ... K - 1, K = ceil(TAIL_PROFILE_SPAN * tau / TAIL_TAP_S); A the direct path's key-down amplitude, env
its keying envelope (edges included), E|g_k|^2 = 1. The carrier phase each delay adds, e^(-j 2 pi f d_k), is a
constant phase of a circular Gaussian gain and so is absorbed into g_k. After a long mark falls, the tail's mean
power t later is sum over d_k > t of w_k, about 10^(tail_db / 10) exp(-t / tau) (derived; shorter marks leave less).

The tail is built at TAIL_RATE_HZ as a complex baseband and linearly interpolated to the recording's rate.
"""

from __future__ import annotations

import math

import numpy as np

from .fading import gain_at, slow_gain

TAIL_RATE_HZ = 4000.0      # the baseband tail is built at this rate, samples/s, then linearly interpolated
TAIL_TAP_S = 0.001         # tap spacing of the delay line, s (heuristic: 1/20 of a dit at 60 WPM, tau/15 at the default tau of 15 ms)
TAIL_PROFILE_SPAN = 5.0    # the profile is cut at this many decay times tau: exp(-5) = 0.67% of its power lies beyond (derived)
TAIL_GAIN_RATE_PER_HZ = 10.0  # each tap's gain is synthesized at this many times its Doppler spread, samples/s per Hz


def tap_weights(tail_db: float, decay_s: float) -> tuple[np.ndarray, np.ndarray]:
    """Delays d_k (s) and power weights w_k (linear, summing to 10^(tail_db / 10)) of the delay line. The staircase
    P(t) = sum over d_k > t of w_k follows 10^(tail_db / 10) (e^(-t / tau) - e^(-5)) / (1 - e^(-5)), differing from it by at most
    +/- TAIL_TAP_S / (2 tau) relative to 10^(tail_db / 10) e^(-t / tau) / (1 - e^(-5)) (derived), for tau much longer than TAIL_TAP_S; a tau under it leaves one tap or a few."""
    if not math.isfinite(tail_db):
        raise ValueError(f"tail_db must be a finite number of dB, not {tail_db}")
    if not (math.isfinite(decay_s) and decay_s > 0):
        raise ValueError("tail_decay_s must be positive and finite")
    k = max(1, int(math.ceil(TAIL_PROFILE_SPAN * decay_s / TAIL_TAP_S)))
    delays = (np.arange(k) + 0.5) * TAIL_TAP_S
    w = np.exp(-delays / decay_s)
    return delays, w * (10 ** (tail_db / 10) / w.sum())


def reach_s(decay_s: float) -> float:
    """How far the tail reaches past the last key-up, s (the longest delay)."""
    return tap_weights(0.0, decay_s)[0][-1] + TAIL_TAP_S / 2


def tail_baseband(envelope, n: int, sample_rate: int, tail_db: float, decay_s: float, fading_hz: float,
                  rng: np.random.Generator) -> np.ndarray:
    """The tail's complex baseband u(t) at n samples of sample_rate, the sum in the module's docstring without A and
    the carrier. envelope(m, rate) returns the keying envelope at m samples of rate, on the same time origin."""
    if not (math.isfinite(fading_hz) and fading_hz >= 0):
        raise ValueError("tail_fading_hz must be 0 (a static tail) or positive and finite")
    delays, w = tap_weights(tail_db, decay_s)
    m = int(math.ceil(n / sample_rate * TAIL_RATE_HZ)) + 2
    env = envelope(m, TAIL_RATE_HZ)
    gain_rate = max(50.0, TAIL_GAIN_RATE_PER_HZ * fading_hz)
    t = np.arange(m) / TAIL_RATE_HZ
    u = np.zeros(m, dtype=np.complex128)
    for d, wk in zip(delays, w):
        shift = int(round(d * TAIL_RATE_HZ))
        if shift >= m:
            break
        if fading_hz == 0:  # a static tail: each tap one complex Gaussian draw, E|g|^2 = 1 (as fading_hz 0 means no fading)
            g = (rng.standard_normal() + 1j * rng.standard_normal()) / math.sqrt(2.0)
        else:
            g = gain_at(slow_gain(m / TAIL_RATE_HZ, fading_hz, rng, "gaussian", gain_rate), t[shift:], gain_rate)
        u[shift:] += math.sqrt(wk) * g * env[:m - shift]
    ts = np.arange(n) / sample_rate
    return np.interp(ts, t, u.real) + 1j * np.interp(ts, t, u.imag)
