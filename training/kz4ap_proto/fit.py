"""The per-branch duration fit (spec 4.5): a mixture over the element classes with log-normal scatter plus
the branch's timing resolution and an outlier class; exponential memory held exactly by recursive
likelihood tables over a grid; the global grid maximum (with the T_P prior), then a local
weighted-least-squares refinement; the quality Q (weighted mean log-likelihood per element, nats).
Densities are in ln(duration)."""

from __future__ import annotations

import copy as _copy
import functools
import math
from collections import deque
from dataclasses import dataclass

import numpy as np

from kz4ap_synth.messages import VE3NEA_CHAR_WEIGHTS, VE3NEA_WORD_LENGTH_PROBS
from kz4ap_synth.morse import CODES

LOG_SQRT_2PI = 0.5 * math.log(2.0 * math.pi)
# Class means are linear in theta = (T, w, Q = qT, G = T_g): dit T + w, dah Q + w, element space T - w,
# character gap 3G - w, word gap 7G - w.
DESIGN = np.array([[1.0, 1.0, 0.0, 0.0],
                   [0.0, 1.0, 1.0, 0.0],
                   [1.0, -1.0, 0.0, 0.0],
                   [0.0, -1.0, 0.0, 3.0],
                   [0.0, -1.0, 0.0, 7.0]])
IS_MARK_CLASS = np.array([True, True, False, False, False])
SPACE_KINDS = ("element", "character", "word")


@functools.lru_cache(maxsize=1)
def class_priors() -> tuple[np.ndarray, np.ndarray]:
    """P(dit), P(dah) among marks and P(element), P(character), P(word) among spaces, derived from VE3NEA's
    tables: per character its dits and dahs, its elements minus one element spaces, a character gap after
    every character but a word's last ((L - 1)/L per character) and a word gap per word (1/L), L the mean
    word length (3.062 characters)."""
    total = sum(VE3NEA_CHAR_WEIGHTS.values())
    dits = sum(w * CODES[c].count(".") for c, w in VE3NEA_CHAR_WEIGHTS.items()) / total
    dahs = sum(w * CODES[c].count("-") for c, w in VE3NEA_CHAR_WEIGHTS.items()) / total
    p = np.array(VE3NEA_WORD_LENGTH_PROBS) / sum(VE3NEA_WORD_LENGTH_PROBS)
    mean_len = float(np.sum(np.arange(len(p)) * p))
    spaces = np.array([dits + dahs - 1.0, (mean_len - 1.0) / mean_len, 1.0 / mean_len])
    return np.array([dits, dahs]) / (dits + dahs), spaces / spaces.sum()


@dataclass(frozen=True)
class Fit:
    t_s: float      # T, the dit, s
    q: float        # dah/dit ratio (the spec's r)
    w_s: float      # key weighting, s
    tg_s: float     # gap timebase T_g, s
    quality: float  # Q: weighted mean log-likelihood per element, nats
    weight: float   # the memory's weight, elements

    def theta(self) -> np.ndarray:
        return np.array([self.t_s, self.w_s, self.q * self.t_s, self.tg_s])


def resolution_var_s2(length_s: float, a: float, rate_hz: float) -> float:
    """sigma_t^2, s^2: an edge through a boxcar of length L is a ramp of slope s/L, so noise of RMS sigma_v
    moves its crossing by L/a; a duration has two edges; sampling adds 1/(12 r^2) per edge (derived, first
    order, high SNR). a is floored at 1."""
    return 2.0 * (length_s / max(a, 1.0)) ** 2 + 2.0 / (12.0 * rate_hz ** 2)


def _log_priors(cfg) -> np.ndarray:
    marks, spaces = class_priors()
    return np.log((1.0 - cfg.outlier_prior) * np.concatenate((marks, spaces)))


def _log_outlier(cfg) -> float:
    lo, hi = cfg.outlier_range_s
    return math.log(cfg.outlier_prior) - math.log(math.log(hi / lo))


def class_logliks(theta, is_mark, d, var_t, cfg):
    """ll (n, 5): ln(prior x density in ln d) of each observation under each class (-inf for the other kind,
    or a class whose mean is not positive); total (n,): the log-likelihood with the outlier class; var_lin
    (n, 5): each class's variance in duration, s^2."""
    is_mark = np.asarray(is_mark, bool)
    d = np.asarray(d, float)
    var_t = np.asarray(var_t, float)
    mu = DESIGN @ np.asarray(theta, float)
    valid = mu > 0
    safe = np.where(valid, mu, 1.0)
    sigma = np.where(IS_MARK_CLASS, cfg.sigma_ln_mark, cfg.sigma_ln_space)
    s2 = sigma[None, :] ** 2 + var_t[:, None] / safe[None, :] ** 2
    z = np.log(d)[:, None] - np.log(safe)[None, :]
    ll = _log_priors(cfg)[None, :] - 0.5 * z * z / s2 - 0.5 * np.log(s2) - LOG_SQRT_2PI
    ll = np.where((IS_MARK_CLASS[None, :] == is_mark[:, None]) & valid[None, :], ll, -np.inf)
    total = np.logaddexp(np.logaddexp.reduce(ll, axis=1), _log_outlier(cfg))
    return ll, total, s2 * safe[None, :] ** 2


def observations_loglik(fit, obs, cfg) -> float:
    """Mean log-likelihood per element of observations (is_mark, duration s, sigma_t^2 s^2, ...), nats."""
    if fit is None or not obs:
        return -math.inf
    _, total, _ = class_logliks(fit.theta(), [o[0] for o in obs], [o[1] for o in obs], [o[2] for o in obs], cfg)
    return float(np.mean(total))


def classify_mark(fit: Fit, d: float, var_t: float, cfg) -> bool:
    """True if the mark is likelier a dah than a dit under the fit."""
    ll, _, _ = class_logliks(fit.theta(), [True], [d], [var_t], cfg)
    return bool(ll[0, 1] > ll[0, 0])


def classify_space(fit: Fit, d: float, var_t: float, cfg) -> str:
    """The likeliest space class under the fit: "element", "character" or "word"."""
    ll, _, _ = class_logliks(fit.theta(), [False], [d], [var_t], cfg)
    return SPACE_KINDS[int(np.argmax(ll[0, 2:]))]


@functools.lru_cache(maxsize=8)
def _grid(cfg):
    """T (log-spaced), q, w/T, T_g/T and, per class, (ln mean, 1/mean^2, mean > 0) over the grid: marks over
    (T, q, w) (the dit's broadcast over q), spaces over (T, w, T_g) (the element space's over T_g)."""
    t_min, t_max = 1.2 / cfg.max_wpm, 1.2 / cfg.min_wpm
    count = int(math.ceil(math.log(t_max / t_min) / math.log1p(cfg.t_grid_step))) + 1
    t = t_min * (1.0 + cfg.t_grid_step) ** np.arange(count)
    q = np.asarray(cfg.q_grid, float)
    w = np.asarray(cfg.w_grid, float)
    g = np.asarray(cfg.tg_grid, float)
    T = t[:, None, None]
    marks = (T * (1.0 + w[None, None, :]),
             T * (q[None, :, None] + w[None, None, :]))
    spaces = (T * (1.0 - w[None, :, None]),
              T * (3.0 * g[None, None, :] - w[None, :, None]),
              T * (7.0 * g[None, None, :] - w[None, :, None]))

    def prep(mu):
        safe = np.where(mu > 0, mu, 1.0)
        return np.log(safe), 1.0 / safe ** 2, mu > 0

    return t, q, w, g, tuple(prep(m) for m in marks), tuple(prep(m) for m in spaces)


class DurationFit:
    """One branch's fit. Every observation multiplies both tables by lambda = exp(-1/N_mem) and adds its
    log-likelihood to one of them, so the tables hold the exponentially weighted log-likelihood exactly at
    every grid point. The last 4 N_mem observations are kept for the refinement and the quality."""

    def __init__(self, cfg):
        self.cfg = cfg
        self.t, self.q, self.w, self.g, self._marks, self._spaces = _grid(cfg)
        priors = _log_priors(cfg)
        self._mark_priors, self._space_priors = priors[:2], priors[2:]
        self._log_outlier = _log_outlier(cfg)
        self.lam = math.exp(-1.0 / cfg.fit_memory)
        self.mark_table = np.zeros((len(self.t), len(self.q), len(self.w)))
        self.space_table = np.zeros((len(self.t), len(self.w), len(self.g)))
        self.weight = 0.0
        self.history: deque = deque(maxlen=int(math.ceil(4 * cfg.fit_memory)))  # newest first

    def copy(self) -> "DurationFit":
        other = _copy.copy(self)  # shares the read-only grid
        other.mark_table = self.mark_table.copy()
        other.space_table = self.space_table.copy()
        other.history = deque(self.history, maxlen=self.history.maxlen)
        return other

    def add(self, is_mark: bool, duration_s: float, var_t: float) -> None:
        """One more mark or space: its duration, s, and its timing-resolution variance sigma_t^2, s^2."""
        if not duration_s > 0:
            return
        log_d = math.log(duration_s)
        if is_mark:
            classes, priors, sigma = self._marks, self._mark_priors, self.cfg.sigma_ln_mark
        else:
            classes, priors, sigma = self._spaces, self._space_priors, self.cfg.sigma_ln_space
        total = None
        for (log_mu, inv_mu2, valid), lp in zip(classes, priors):
            s2 = sigma * sigma + var_t * inv_mu2
            z = log_d - log_mu
            ll = np.where(valid, lp - 0.5 * z * z / s2 - 0.5 * np.log(s2) - LOG_SQRT_2PI, -np.inf)
            total = ll if total is None else np.logaddexp(total, ll)
        total = np.logaddexp(total, self._log_outlier)
        self.mark_table *= self.lam
        self.space_table *= self.lam
        if is_mark:
            self.mark_table += total
        else:
            self.space_table += total
        self.weight = self.lam * self.weight + 1.0
        self.history.appendleft((bool(is_mark), float(duration_s), float(var_t)))

    def _arrays(self):
        h = self.history
        is_mark = np.fromiter((o[0] for o in h), bool, len(h))
        d = np.fromiter((o[1] for o in h), float, len(h))
        var_t = np.fromiter((o[2] for o in h), float, len(h))
        return is_mark, d, var_t, self.lam ** np.arange(len(h))

    def best(self, prior_t_s: float | None = None, prior_weight: float = 0.0) -> Fit | None:
        """The global maximum over the grid, with -prior_weight (ln T - ln T_P)^2 / (2 sigma_P^2) added when
        T_P is given, refined locally. None before any observation."""
        if not self.history:
            return None
        score = self.mark_table.max(axis=1) + self.space_table.max(axis=2)  # (T, w): best q, best T_g
        if prior_t_s is not None and prior_weight > 0:
            z = (np.log(self.t) - math.log(prior_t_s)) / self.cfg.prior_sigma_ln
            score = score - (prior_weight * 0.5 * z * z)[:, None]
        i, j = np.unravel_index(int(np.argmax(score)), score.shape)
        qi = int(np.argmax(self.mark_table[i, :, j]))
        gi = int(np.argmax(self.space_table[i, j, :]))
        t = float(self.t[i])
        theta = self._refine(np.array([t, self.w[j] * t, self.q[qi] * t, self.g[gi] * t]))
        is_mark, d, var_t, age = self._arrays()
        _, total, _ = class_logliks(theta, is_mark, d, var_t, self.cfg)
        quality = float(np.sum(age * total) / np.sum(age))
        return Fit(float(theta[0]), float(theta[2] / theta[0]), float(theta[1]), float(theta[3]), quality, self.weight)

    def _refine(self, theta: np.ndarray) -> np.ndarray:
        """Weighted least squares in the durations: every class mean is linear in theta, weights are age x
        responsibility / variance, and a weak prior (sd 0.2 T per parameter) toward the current point keeps
        unobserved classes where they are."""
        is_mark, d, var_t, age = self._arrays()
        for _ in range(self.cfg.refine_iterations):
            ll, total, var_lin = class_logliks(theta, is_mark, d, var_t, self.cfg)
            weights = age[:, None] * np.exp(ll - total[:, None]) / var_lin
            m = np.einsum("nc,ci,cj->ij", weights, DESIGN, DESIGN)
            b = np.einsum("nc,ci,n->i", weights, DESIGN, d)
            prior = 1.0 / (0.2 * theta[0]) ** 2
            new = np.linalg.solve(m + prior * np.eye(4), b + prior * theta)
            t = max(float(new[0]), 1e-4)
            theta = np.array([t, np.clip(new[1], -0.6 * t, 1.2 * t), np.clip(new[2], 2.0 * t, 6.0 * t),
                              np.clip(new[3], 0.8 * t, 10.0 * t)])
        return theta
