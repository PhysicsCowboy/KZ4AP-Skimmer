"""Noise estimates for the branches (spec 4.2): branch 1's level by the milestone-2 three-tap guard and
every branch's by the shape of the shared noise spectrum; or, the recorded fallback, a three-tap estimate
per branch. Variances are per real component of v_k, FS^2."""

from __future__ import annotations

import math

import numpy as np

from .bank import power_response

MIN_VAR = 1e-20  # FS^2 (-200 dBFS): keeps x and a finite on noise-free input


def guard_mean(kappa: float) -> float:
    """E[y | y < kappa] for y ~ Exp(1): what the guard's truncation leaves of the mean (0.632 at 1.75)."""
    return 1.0 - kappa * math.exp(-kappa) / (1.0 - math.exp(-kappa))


class ThreeTapNoise:
    """sigma_v,k^2 of each boxcar in branch_n by the milestone-2 guard (docs/signal-processing.md 8b): the tap
    v[n-N] updates sigma^2 only if |v[n-N]|^2/(2 sigma^2) < kappa and |v[n]|^2, |v[n-2N]|^2 are below
    kappa_n 2 sigma^2 (three taps N apart share no inputs); the mean of the accepted taps is divided by
    m(kappa). The first estimate is the 20% quantile of |v|^2 over the warm-up (a provisional one before).
    Updated once per block with the block's starting sigma^2 (tau_n >> one block)."""

    def __init__(self, cfg, rate_hz: float, branch_n):
        self.n = np.asarray(branch_n, dtype=int)
        self.kappa, self.kappa_n = cfg.noise_guard, cfg.neighbor_guard
        self.m = guard_mean(cfg.noise_guard)
        self.alpha = 1.0 - math.exp(-1.0 / (cfg.noise_tau_s * rate_hz))
        self.warmup = max(1, int(round(cfg.noise_warmup_s * rate_hz)))
        self.var = np.full(len(self.n), np.nan)
        self.weight = np.zeros(len(self.n))
        self.started = False

    def update(self, P, n0: int, n1: int) -> None:
        if n1 <= n0:
            return
        if not self.started:
            q = np.quantile(np.asarray(P[:, :n1], float), 0.2, axis=1) / (2.0 * -math.log(0.8))
            self.var = np.maximum(q, MIN_VAR)
            if n1 >= self.warmup:
                self.started = True
                self.weight[:] = 0.1 * self.warmup  # milestone 2: the warm-up counts as this much weight
            return
        k = len(self.n)
        idx = np.broadcast_to(np.arange(n0, n1), (k, n1 - n0))
        lag = self.n[:, None]
        valid = idx >= 2 * lag
        now = np.take_along_axis(P, idx, axis=1).astype(float)
        mid = np.take_along_axis(P, np.maximum(idx - lag, 0), axis=1).astype(float)
        old = np.take_along_axis(P, np.maximum(idx - 2 * lag, 0), axis=1).astype(float)
        two_var = 2.0 * self.var[:, None]
        ok = valid & (mid < self.kappa * two_var) & (now < self.kappa_n * two_var) & (old < self.kappa_n * two_var)
        count = ok.sum(axis=1)
        has = count > 0
        if not has.any():
            return
        mean_mid = (mid * ok).sum(axis=1) / np.maximum(count, 1)
        self.weight += count
        step = np.maximum(1.0 - (1.0 - self.alpha) ** count, count / np.maximum(self.weight, 1.0))
        target = mean_mid / (2.0 * self.m)
        self.var = np.where(has, np.maximum(self.var + step * (target - self.var), MIN_VAR), self.var)


class BranchNoise:
    """The recorded fallback (spec 4.2): each branch's own three-tap estimate."""

    def __init__(self, cfg, rate_hz: float, branch_n):
        self.est = ThreeTapNoise(cfg, rate_hz, branch_n)

    def update(self, u, P, n0: int, n1: int) -> None:
        self.est.update(P, n0, n1)

    def sigma2(self) -> np.ndarray:
        return self.est.var.copy()


class SpectrumNoise:
    """The shared noise spectrum (spec 4.2). sigma_v,k^2 = sigma_v,1^2 (W_k . S) / (W_1 . S): branch 1's level
    from the three-tap guard, the ratio from S, an exponential average (tau_n) of Hann-windowed periodograms of
    u over segments of T_seg, smoothed over +/- spectrum_smoothing_hz; W_k[m] is the mean of |H_k(f)|^2 over
    bin m. A sample of u is left out of its segment if any |v_1|^2 that contains it, or lies within
    guard_margin_s of it, reaches kappa_n 2 sigma_v,1^2; a segment enters only if at least min_clean_fraction
    of it is left in. Until the three-tap warm-up is over no segment enters and the shape is white
    (ratio N_1/N_k, exact for white noise by Parseval)."""

    SUBSAMPLES = 16  # points per bin for W_k

    def __init__(self, cfg, rate_hz: float, branch_n, level: str = "three-tap"):
        if level not in ("three-tap", "spectrum"):
            raise ValueError(f"unknown noise level source {level!r}")
        self.level = level
        self.n = np.asarray(branch_n, dtype=int)
        self.mask_bias = None
        if level == "spectrum":
            if len(cfg.mask_bias) != len(self.n):
                raise ValueError(f"mask_bias has {len(cfg.mask_bias)} values for {len(self.n)} branches")
            self.mask_bias = np.asarray(cfg.mask_bias, float)
        self.ref = ThreeTapNoise(cfg, rate_hz, self.n[:1])
        self.m = max(16, int(round(cfg.segment_s * rate_hz)))
        self.window = 0.5 - 0.5 * np.cos(2.0 * np.pi * np.arange(self.m) / self.m)
        centers = np.fft.fftfreq(self.m, d=1.0 / rate_hz)
        offsets = ((np.arange(self.SUBSAMPLES) + 0.5) / self.SUBSAMPLES - 0.5) * rate_hz / self.m
        f = centers[:, None] + offsets[None, :]
        self.bin_weights = np.stack([power_response(f, int(n), rate_hz).mean(axis=1) for n in self.n])  # (K, M)
        self.half = max(0, int(round(cfg.spectrum_smoothing_hz * self.m / rate_hz)))
        self.kappa_n = cfg.neighbor_guard
        self.reach = int(round(cfg.guard_margin_s * rate_hz))
        self.min_clean = cfg.min_clean_fraction
        self.beta = 1.0 - math.exp(-cfg.segment_s / cfg.noise_tau_s)
        self.shape = None       # periodogram average, FS^2 per bin
        self.ratio = None       # (W_k . S) / (W_1 . S), cached
        self.segments = 0              # accepted
        self.segments_offered = 0      # after the warm-up
        self.kept_fraction_sum = 0.0   # fraction of each offered segment left in by the mask
        self.masked_power_sum = 0.0    # mean power per sample of each accepted masked periodogram, FS^2
        self.periodogram_sum = None    # sum of the accepted masked periodograms, FS^2 per bin (calibration)
        self.next_start = 0

    def update(self, u, P, n0: int, n1: int) -> None:
        self.ref.update(P[:1], n0, n1)
        look = int(self.n[0]) - 1 + self.reach  # the flag needs |v_1|^2 this far past a segment
        while self.next_start + self.m + look <= n1:
            s = self.next_start
            self.next_start += self.m
            if not self.ref.started:
                continue
            clean = self._clean(P[0], s)
            self.segments_offered += 1
            self.kept_fraction_sum += float(clean.mean())
            if clean.mean() >= self.min_clean:
                self._accept(np.asarray(u[s:s + self.m]), clean)

    def _clean(self, p1, s: int) -> np.ndarray:
        n1 = int(self.n[0])
        lo = max(0, s - self.reach)
        hi = min(len(p1), s + self.m + n1 - 1 + self.reach)
        flagged = np.asarray(p1[lo:hi], float) >= self.kappa_n * 2.0 * self.ref.var[0]
        c = np.concatenate(([0], np.cumsum(flagged)))
        i = np.arange(s, s + self.m)
        a = np.clip(i - self.reach - lo, 0, len(flagged))
        b = np.clip(i + n1 - 1 + self.reach - lo + 1, 0, len(flagged))
        return (c[b] - c[a]) == 0  # u[i] feeds v_1[i .. i + N_1 - 1]; none of them (+/- the reach) is flagged

    def _accept(self, seg, clean) -> None:
        w = self.window * clean
        norm = float(np.sum(w * w))
        if norm <= 0.0:
            return
        periodogram = np.abs(np.fft.fft(seg * w)) ** 2 / norm  # mean over bins = power per sample, FS^2
        self.segments += 1
        self.masked_power_sum += float(periodogram.mean())
        if self.periodogram_sum is None:
            self.periodogram_sum = periodogram.copy()
        else:
            self.periodogram_sum += periodogram
        if self.shape is None:
            self.shape = periodogram
        else:
            self.shape = self.shape + max(self.beta, 1.0 / self.segments) * (periodogram - self.shape)
        self.ratio = None

    def _smoothed(self, spectrum=None) -> np.ndarray:
        s = self.shape if spectrum is None else spectrum
        if self.half == 0:
            return s
        ext = np.concatenate((s[-self.half:], s, s[:self.half]))
        return np.convolve(ext, np.ones(2 * self.half + 1) / (2 * self.half + 1), mode="valid")

    def masked_branch_power(self) -> np.ndarray:
        """sigma_v,k^2 per real component, FS^2, from the flat mean of every accepted masked periodogram so far,
        smoothed as sigma2() smooths it and with no bias correction: 0.5 (W_k . S) / M. Divided by the true
        sigma_v,k^2 it is the mask's bias per branch, b_mask,k (the calibration of arm (b))."""
        if self.periodogram_sum is None:
            raise ValueError("no segment has entered the spectrum")
        return 0.5 * (self.bin_weights @ self._smoothed(self.periodogram_sum / self.segments)) / self.m

    def sigma2(self) -> np.ndarray:
        level = self.ref.var[0]
        if self.shape is None:
            return level * self.n[0] / self.n
        if self.ratio is None:
            k = self.bin_weights @ self._smoothed()
            self.ratio = k / k[0]
            # arm (b): complex power of v_k is (1/M) sum_m I_m W_k[m]; per real component half of it; through the
            # mask, branch k reads mask_bias[k] of its true noise power (measured per branch, white noise)
            if self.mask_bias is not None:
                self.absolute = 0.5 * k / self.m / self.mask_bias
        return self.absolute.copy() if self.level == "spectrum" else level * self.ratio


def make_noise(cfg, rate_hz: float, branch_n):
    if cfg.noise_method == "spectrum":
        return SpectrumNoise(cfg, rate_hz, branch_n)
    if cfg.noise_method == "spectrum-level":
        return SpectrumNoise(cfg, rate_hz, branch_n, level="spectrum")
    if cfg.noise_method == "branch":
        return BranchNoise(cfg, rate_hz, branch_n)
    raise ValueError(f"unknown noise method {cfg.noise_method!r}")
