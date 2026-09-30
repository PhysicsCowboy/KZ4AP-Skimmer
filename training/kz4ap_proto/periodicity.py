"""The periodicity estimator (spec 4.4): the coarse speed T_P from branch 1's keying probability, by one of three
methods (E1 chooses): an autocorrelation comb on the dit-plus-space period (the default), a sign-weighted comb on
the signed edges of p, or the spectrum's nulls; over several windows in parallel."""

from __future__ import annotations

import math

import numpy as np

from .bank import branch_lengths_s, branch_samples, power_response


def t_grid(cfg) -> np.ndarray:
    """Candidate dits, 1% apart, from the fastest to the slowest speed, s."""
    t_min, t_max = 1.2 / cfg.max_wpm, 1.2 / cfg.min_wpm
    return t_min * 1.01 ** np.arange(int(math.ceil(math.log(t_max / t_min) / math.log(1.01))) + 1)


def _normalized_acf(x):
    n = len(x)
    size = 1 << int(math.ceil(math.log2(2 * n)))
    spec = np.fft.rfft(x, size)
    return np.fft.irfft(spec * np.conj(spec), size)[:n] / float(x @ x)


def comb_estimate(p, rate_hz: float, grid, teeth: int, width: float):
    """(T, score). The comb of spec 4.4 (as corrected 2026-09-30) on mean-removed p, applied to the period Pi of a
    dit and its element space: teeth at k Pi (k = 1..teeth: 2T ... 8T), negative teeth halfway between, each the
    mean of the normalized autocorrelation within +/- width Pi of its lag (0.075 Pi = 15% of T); score = mean over
    teeth of tooth - (left + right)/2. Consecutive keying edges T apart have opposite signs, so p's structure
    repeats at 2T: its autocorrelation is low at odd and high at even multiples of T, and a comb with teeth at kT
    peaks at 2T (derived; checked while planning and in review); so the comb runs on Pi = 2T and T = Pi / 2.
    Lags reach (teeth + 1/2 + width) Pi, about 9.2 T; candidates whose reach passes half the window are skipped
    (heuristic). With the defaults that caps the comb at T <= W / 18.3: 109 ms (11 WPM) in a 2 s window, 273 ms in
    5 s (the whole grid), 546 ms in 10 s (derived). A window whose true T is beyond its cap still returns its best
    in-reach candidate: only the confidence threshold keeps that estimate out.
    The autocorrelation is the biased estimate (sum over n - tau products, divided by the lag-0 sum), so each tooth
    is tapered by about (1 - tau / N) and a tooth's contrast shrinks with its lag: about 0.62 at the 8T tooth for
    5 WPM (tau = 1.92 s) in a 5 s window (derived). Keeping the taper is a heuristic (the unbiased estimate is
    noisier at long lags); it lowers long-T scores in short windows relative to short-T ones.
    (None, 0) when p does not vary."""
    x = np.asarray(p, float) - float(np.mean(p))
    n = len(x)
    c0 = float(x @ x)
    if n < 16 or c0 <= 1e-12 * n:
        return None, 0.0
    acf = _normalized_acf(x)
    cs = np.concatenate(([0.0], np.cumsum(acf)))
    period = 2.0 * np.asarray(grid) * rate_hz  # Pi in samples
    half = width * period[:, None]
    k = np.arange(1, teeth + 1)[None, :]

    def band(center):
        lo = np.clip(np.floor(center - half), 0, n - 1).astype(int)
        hi = np.clip(np.ceil(center + half), 0, n - 1).astype(int)
        return (cs[hi + 1] - cs[lo]) / (hi - lo + 1)

    contrast = band(k * period[:, None]) - 0.5 * (band((k - 0.5) * period[:, None]) + band((k + 0.5) * period[:, None]))
    score = np.where((teeth + 0.5 + width) * period <= (n - 1) / 2, contrast.mean(axis=1), -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


def edge_comb_estimate(p, rate_hz: float, grid, teeth: int, width: float):
    """(T, score). The sign-weighted edge comb (E1's third arm): on the signed edges e[n] = p[n] - p[n-1], teeth at
    k T (k = 1..teeth) weighted (-1)^k, because key-down and key-up edges alternate, so edges an odd number of dits
    apart tend to have opposite signs and an even number the same sign; each tooth is the mean of e's normalized
    autocorrelation within +/- width T of its lag; score = mean of the weighted teeth. Review: best on hand and
    bug keying at S500 >= 10 dB, but it fails slow keying at 3 dB (differencing whitens the noise). Lags reach
    (teeth + width) T; candidates whose reach passes half the window are skipped, which with the defaults caps T at
    W / 8.3: 241 ms in a 2 s window, so the whole grid (derived). The autocorrelation is the biased estimate, as in
    comb_estimate. Periodicity passes comb_teeth and 2 x comb_width (a fraction of T) here: the edge comb has no
    parameters of its own, so E3 cannot vary its teeth or width independently of the comb's without adding them.
    (None, 0) when e does not vary."""
    x = np.diff(np.asarray(p, float), prepend=float(p[0]) if len(p) else 0.0)
    n = len(x)
    if n < 16 or float(x @ x) <= 1e-12 * n:
        return None, 0.0
    acf = _normalized_acf(x)
    cs = np.concatenate(([0.0], np.cumsum(acf)))
    t = np.asarray(grid) * rate_hz  # T in samples
    half = width * t[:, None]
    k = np.arange(1, teeth + 1)[None, :]
    center = k * t[:, None]
    lo = np.clip(np.floor(center - half), 0, n - 1).astype(int)
    hi = np.clip(np.ceil(center + half), 0, n - 1).astype(int)
    teeth_mean = (cs[hi + 1] - cs[lo]) / (hi - lo + 1)
    score = np.where((teeth + width) * t <= (n - 1) / 2, np.mean(np.where(k % 2 == 0, 1.0, -1.0) * teeth_mean, axis=1),
                     -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


def spectrum_estimate(p, rate_hz: float, grid, nulls: int, width: float, front_power=None, floor_db: float = -20.0):
    """(T, score in nats). Ideal keying on a T lattice has nulls at f = k/T in its power spectrum whatever the
    marks' positions: a mark of duration jT carries sinc(f jT), zero at every f = k/T wherever it starts
    (derived). That survives Farnsworth (only the gaps are stretched; the marks stay multiples of T) but not key
    weighting (marks T + w, nulls at k/(T + w)) nor hand keying (scattered durations). p itself is NOT piecewise
    constant on the lattice: it is the keying seen through branch 1's boxcar (and the keying's nonlinearity), so
    its spectrum is roughly the keying's times the front end's |H(f)|^2, whose own nulls (at m r / N_1: 107.1 Hz,
    214.3 Hz, ... for N_1 = 14 samples at 1500 samples/s) scored boxcar-filtered noise alone up to about 1.5 nats
    at T = (k/m) N_1/r, and a 20-sample boxcar about 3.5 nats at 13.3 ms (review; measured).
    So, given front_power(f) = the front end's |H(f)|^2 relative to its DC gain, the periodogram is divided by it
    (whitening; derived), and frequencies where it is below floor_db (dB relative to the DC power gain; default
    -20 dB, heuristic) are left out: a null k contributes only if its whole null window is above the floor and at
    least half of its band window is (the band's mean is then over its bins above the floor; heuristic). A
    candidate with no contributing null is skipped. Measured on the unit tests' keying (10 s windows): a stricter
    rule that drops a null (or the whole candidate) when any of its band lies below the floor loses 100 WPM (and,
    whole-candidate, 40 WPM) to 3T, because branch 1's first null (107 Hz) lies inside the k = 1 band of a 12 ms
    dit; with this rule both are within 1%, and boxcar noise (N = 14 and 20) scores 0.53-0.80 nats with no lock.
    score = mean over the contributing k of ln(mean power in [(k - 1/2)/T, (k + 1/2)/T] / mean power within
    +/- width/(2T) of k/T), from a Hann-windowed periodogram zero-padded 4x; candidates whose last band passes
    Nyquist are skipped. Without front_power nothing is whitened or left out (the review's form). The maximum is the
    estimate (a rule preferring the longest T near it picked 3T for paddle keying while planning). (None, 0) when p
    does not vary."""
    x = (np.asarray(p, float) - float(np.mean(p))) * np.hanning(len(p))
    if len(x) < 16 or float(x @ x) <= 1e-12 * len(x):
        return None, 0.0
    size = 4 * (1 << int(math.ceil(math.log2(len(x)))))
    power = np.abs(np.fft.rfft(x, size)) ** 2
    df = rate_hz / size
    m = len(power)
    if front_power is None:
        valid = np.ones(m, bool)
    else:
        h = np.asarray(front_power(np.arange(m) * df), float)
        valid = h >= 10.0 ** (floor_db / 10.0)
        power = np.where(valid, power / np.where(valid, h, 1.0), 0.0)
    cs = np.concatenate(([0.0], np.cumsum(power)))
    cv = np.concatenate(([0], np.cumsum(valid)))

    def band(f_lo, f_hi):
        """(mean power over the bins above the floor, their count, all bins)."""
        lo = np.clip(np.floor(f_lo / df), 0, m - 1).astype(int)
        hi = np.clip(np.ceil(f_hi / df), 0, m - 1).astype(int)
        count = cv[hi + 1] - cv[lo]
        return (cs[hi + 1] - cs[lo]) / np.maximum(count, 1), count, hi - lo + 1

    t = np.asarray(grid)[:, None]
    f = np.arange(1, nulls + 1)[None, :] / t
    null, null_count, null_bins = band(f - 0.5 * width / t, f + 0.5 * width / t)
    around, around_count, around_bins = band(f - 0.5 / t, f + 0.5 / t)
    use = (null_count == null_bins) & (2 * around_count >= around_bins)
    ratio = np.where(use, np.log(np.maximum(around, 1e-300) / np.maximum(null, 1e-300)), 0.0)
    used = use.sum(axis=1)
    score = np.where(((nulls + 0.5) / t[:, 0] <= rate_hz / 2) & (used > 0),
                     ratio.sum(axis=1) / np.maximum(used, 1), -np.inf)
    i = int(np.argmax(score))
    return (float(grid[i]), float(score[i])) if np.isfinite(score[i]) else (None, 0.0)


class Periodicity:
    """Branch 1's posterior p, averaged down to periodicity_rate_hz, in a buffer as long as the longest
    window; every periodicity_update_s each window (shortest first) is estimated, and the confident estimate
    with the shortest window is T_P. T_P never feeds back onto its own window (spec 4.4)."""

    def __init__(self, cfg, rate_hz: float):
        self.cfg = cfg
        self.factor = max(1, int(round(rate_hz / cfg.periodicity_rate_hz)))
        self.rate = rate_hz / self.factor
        self.windows = sorted(max(16, int(round(w * self.rate))) for w in cfg.periodicity_windows_s)
        self.update_every = max(1, int(round(cfg.periodicity_update_s * rate_hz)))
        self.grid = t_grid(cfg)
        if cfg.periodicity_method == "comb":
            self.threshold = cfg.comb_confidence_min
            self._estimate = lambda x: comb_estimate(x, self.rate, self.grid, cfg.comb_teeth, cfg.comb_width)
        elif cfg.periodicity_method == "edge":
            self.threshold = cfg.edge_confidence_min
            # The edge comb reuses the comb's parameters: comb_teeth teeth, each 2 x comb_width wide (comb_width is a
            # fraction of Pi = 2T, so this is the same +/-15% of T). E3 cannot vary them independently of the comb's
            # without separate parameters.
            self._estimate = lambda x: edge_comb_estimate(x, self.rate, self.grid, cfg.comb_teeth, 2.0 * cfg.comb_width)
        elif cfg.periodicity_method == "spectrum":
            self.threshold = cfg.spectrum_confidence_min
            # The front end ahead of this estimator: branch 1's realized N_1-sample boxcar at the channel rate r,
            # then the factor-sample mean taken before decimating to periodicity_rate_hz. Aliasing is ignored: for
            # N_1 = 14 and factor 2 the product is at most -22.9 dB (mean -31.5 dB) relative to DC over the band
            # that folds, 375-750 Hz (computed); heuristic.
            n1 = int(branch_samples(branch_lengths_s(cfg), rate_hz)[0])
            factor = self.factor
            front = lambda f: power_response(f, n1, rate_hz) * power_response(f, factor, rate_hz)
            self._estimate = lambda x: spectrum_estimate(x, self.rate, self.grid, cfg.spectrum_nulls,
                                                         cfg.spectrum_null_width, front_power=front,
                                                         floor_db=cfg.spectrum_front_floor_db)
        else:
            raise ValueError(f"unknown periodicity method {cfg.periodicity_method!r}")
        self.buffer = np.zeros(0)
        self.carry = np.zeros(0)
        self.pending = 0
        self.last: tuple = (None, 0.0, None)
        self.per_window: list[tuple[float | None, float]] = [(None, 0.0)] * len(self.windows)

    def push(self, p) -> None:
        x = np.concatenate((self.carry, np.asarray(p, float)))
        whole = len(x) // self.factor * self.factor
        if whole:
            averaged = x[:whole].reshape(-1, self.factor).mean(axis=1)
            self.buffer = np.concatenate((self.buffer, averaged))[-self.windows[-1]:]
        self.carry = x[whole:]
        self.pending += len(p)

    def update(self, force: bool = False):
        """(T_P s or None, confidence, window s or None, whether it was recomputed)."""
        if not force and self.pending < self.update_every:
            return (*self.last, False)
        self.pending = 0
        found, best = None, 0.0
        self.per_window = []
        for w in self.windows:
            if len(self.buffer) < w:
                self.per_window.append((None, 0.0))
                continue
            t, score = self._estimate(self.buffer[-w:])
            self.per_window.append((t, score))
            best = max(best, score)
            if found is None and t is not None and score >= self.threshold:
                found = (t, score, w / self.rate)
        self.last = found if found is not None else (None, best, None)
        return (*self.last, True)
