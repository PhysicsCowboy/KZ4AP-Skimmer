"""Flat Rayleigh fading (QSB) for synthetic signals.

The gain g(t) is complex Gaussian with E|g|^2 = 1. Its Doppler power spectrum
S(f) has one of two shapes, both parameterized by the frequency spread
f_D = spread_hz, Hz:

- "gaussian": S(f) ~ exp(-f^2 / (2 sigma^2)) with f_D = 2 sigma (the
  Watterson / CCIR 520 HF convention).
- "butterworth": S(f) ~ 1 / (1 + (f / f_c)^4), f_c = 0.625 f_D: the spectrum of
  VE3NEA's DeepCW fading generator (a 2nd-order Butterworth low-pass per
  quadrature, cutoff 1.25 f_D / 2). A Gaussian least-squares fit to it gives
  2 sigma = 1.01 f_D, so f_D means the same spread in both shapes to about 1%;
  the Butterworth has heavier f^-4 tails. (DeepCW data_generation.ipynb
  cells 17, 21, 25, commit 2c8fdac, MIT; docs/research/deepcw-generator-notes.md §2.)

Both are synthesized at FADING_RATE_HZ by shaping white complex Gaussian
noise in the frequency domain, then linearly interpolated.
"""

from __future__ import annotations

import numpy as np

FADING_RATE_HZ = 50.0  # rate the fading process is synthesized at before interpolation, samples/s
BUTTERWORTH_CUTOFF_PER_SPREAD = 0.625  # f_c / f_D of VE3NEA's fading filter
SHAPES = ("gaussian", "butterworth")


def slow_gain(duration_s: float, spread_hz: float, rng: np.random.Generator,
              shape: str = "gaussian") -> np.ndarray:
    """The fading gain at FADING_RATE_HZ, covering duration_s (plus two samples)."""
    if spread_hz <= 0:
        raise ValueError("spread_hz must be positive")
    if shape not in SHAPES:
        raise ValueError(f"unknown fading shape {shape!r}")
    m = int(np.ceil(duration_s * FADING_RATE_HZ)) + 2
    freqs = np.fft.fftfreq(m, 1.0 / FADING_RATE_HZ)
    if shape == "gaussian":
        sigma = spread_hz / 2.0
        response = np.exp(-freqs**2 / (4.0 * sigma**2))  # amplitude response: sqrt of the power spectrum
    else:
        cutoff = BUTTERWORTH_CUTOFF_PER_SPREAD * spread_hz
        response = 1.0 / np.sqrt(1.0 + (freqs / cutoff) ** 4)
    response *= np.sqrt(m / np.sum(response**2))  # expected power of the output is exactly 1
    white = (rng.standard_normal(m) + 1j * rng.standard_normal(m)) / np.sqrt(2.0)
    return np.fft.ifft(white * response, norm="ortho")


def gain_at(slow: np.ndarray, t_s: np.ndarray) -> np.ndarray:
    """The gain at times t_s (s from the start of slow), linearly interpolated."""
    t_slow = np.arange(len(slow)) / FADING_RATE_HZ
    return np.interp(t_s, t_slow, slow.real) + 1j * np.interp(t_s, t_slow, slow.imag)


def rayleigh_gain(n: int, sample_rate: int, spread_hz: float, rng: np.random.Generator,
                  shape: str = "gaussian") -> np.ndarray:
    """n samples of complex fading gain at sample_rate, frequency spread spread_hz (Hz)."""
    return gain_at(slow_gain(n / sample_rate, spread_hz, rng, shape), np.arange(n) / sample_rate)
