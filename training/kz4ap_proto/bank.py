"""The filter bank (spec 4.1): fixed boxcars on a geometric ladder of lengths."""

from __future__ import annotations

import numpy as np


def branch_lengths_s(cfg) -> np.ndarray:
    """L_k = length_dits x 1.2 s / max_wpm x step^(k-1), up to the first within one step of the
    min_wpm optimum: 9.6 ms ... 184.3 ms, 32 branches with the defaults (owner)."""
    l_min = cfg.length_dits * 1.2 / cfg.max_wpm
    l_max = cfg.length_dits * 1.2 / cfg.min_wpm
    count = int(np.ceil(np.log(l_max / l_min) / np.log(cfg.ladder_step) - 1e-9))
    return l_min * cfg.ladder_step ** np.arange(count)


def branch_samples(lengths_s, rate_hz: float) -> np.ndarray:
    """N_k = round(L_k r), at least 1: the point of use."""
    return np.maximum(1, np.rint(np.asarray(lengths_s) * rate_hz)).astype(int)


def realized_lengths_s(cfg, rate_hz: float) -> np.ndarray:
    """N_k / r, s: the lengths the branches really have."""
    return branch_samples(branch_lengths_s(cfg), rate_hz) / rate_hz


def boxcar(u, n: int) -> np.ndarray:
    """v[m] = (1/n) sum of u[m-n+1 .. m], zeros before the stream: the engine's causal, unity-gain boxcar."""
    c = np.concatenate(([0], np.cumsum(np.asarray(u, np.complex128))))
    m = np.arange(1, len(u) + 1)
    return (c[m] - c[np.maximum(m - n, 0)]) / n


def power_response(f_hz, n: int, rate_hz: float) -> np.ndarray:
    """|H(f)|^2 of an n-sample boxcar at rate r: (sin(pi f n / r) / (n sin(pi f / r)))^2, 1 at 0 Hz."""
    x = np.pi * np.asarray(f_hz, float) / rate_hz
    den = n * np.sin(x)
    out = np.ones_like(x)
    nz = np.abs(den) > 1e-12
    out[nz] = (np.sin(n * x[nz]) / den[nz]) ** 2
    return out
