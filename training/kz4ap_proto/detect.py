"""ln I0 and the envelope log-likelihood ratio, as engine/src/matched_front_end.cpp computes them."""

from __future__ import annotations

import numpy as np


def log_bessel_i0(z) -> np.ndarray:
    """ln I0(z) for z >= 0 without overflow (Abramowitz & Stegun 9.8.1 for z < 3.75, 9.8.2 above)."""
    z = np.abs(np.asarray(z, dtype=float))
    out = np.empty_like(z)
    small = z < 3.75
    t = (z[small] / 3.75) ** 2
    out[small] = np.log(1.0 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (
        0.0360768 + t * 0.0045813))))))
    zl = z[~small]
    s = 3.75 / zl
    poly = 0.39894228 + s * (0.01328592 + s * (0.00225319 + s * (-0.00157565 + s * (0.00916281 + s * (
        -0.02057706 + s * (0.02635537 + s * (-0.01647633 + s * 0.00392377)))))))
    out[~small] = zl - 0.5 * np.log(zl) + np.log(poly)
    return out


def envelope_llr(x, a) -> np.ndarray:
    """Lambda = -a^2/2 + ln I0(a x), nats: key-down (Rician) over key-up (Rayleigh) for x = |v|/sigma_v
    and a = s/sigma_v (Proakis & Salehi eq. 4.5-21)."""
    a = np.asarray(a, float)
    return -0.5 * a * a + log_bessel_i0(a * np.asarray(x, float))


def logistic(g) -> np.ndarray:
    return 1.0 / (1.0 + np.exp(-np.clip(np.asarray(g, float), -50.0, 50.0)))
