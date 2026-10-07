"""The jittered development set's analysis (docs/plans/2026-10-06-development-set-redesign.md, principle 6; task D2):
a smooth fit of CER over S500 and speed, crossings with bootstrap intervals, the E/N0-per-dit view and its test of
time-base invariance, paired comparisons of a variant with a reference, and detection recall through the detector
path. Figures: kz4ap_proto.figures. Command line: kz4ap_proto.experiments devset2 / figures.

Quantities and units:
- S500: key-down carrier power over the noise power in 500 Hz, dB SNR in 500 Hz (the labels' snr_db).
- E/N0 per dit: key-down energy in one dit, E = P * T with T = 1.2 s / WPM, over the one-sided noise power spectral
  density N0; E/N0 = S500 + 10 log10(500 Hz * T), in dB relative to 1 (derived; stage-1 results record section 5.1.1).
- CER: edits over the reference's symbols (characters, spaces counted), per signal; a ratio.

The model (the plan's, principle 6; this module's choices are marked derived, measured or heuristic):

    CER(L, v) = c_j(v) + (1 - c_j(v)) * logistic(-(L - s0(v)) / w(v))

L the level (S500 or E/N0 per dit, dB), v the speed (WPM), x = ln(v / V_REF_WPM),
s0(v) = a0 + a1 x + a2 x^2 (dB), ln w(v) = b0 + b1 x + b2 x^2 (w in dB), and c_j = logistic(g_j) the floor of
speed cell j(v) (0 < c_j < 1), one per speed cell (owner, 2026-10-06, after the pilot; task D4). It replaced task
D2's single floor, which a speed cell failing at every S500 pulled up for every cell. A speed outside 8-80 WPM takes
the nearest cell's floor. A cell with no signals keeps its floor at the start value: not fitted, reported as None.
V_REF_WPM = sqrt(8 * 80) = 25.30 WPM, the speed range's geometric center: a centering for conditioning only, the
fitted curves do not depend on it (derived). CER above 1 (more edits than reference symbols) is clipped to 1, the
model's ceiling.

A crossing (the level where the fitted CER falls to a threshold t, at a speed) is "no crossing" when the floor of the
speed's cell is at or above t (the curve never reaches t; reason "floor"), or when the crossing lies above the
highest level of that cell's fit signals (the curve does not reach t within the data; reason "above the data"). A
crossing below the data's lowest level is still reported, flagged outside_data.

Fitted by weighted least squares (FIT_METHOD, the default) or by binomial maximum likelihood, both by
Levenberg-Marquardt on the Gauss-Newton / Fisher-scoring normal equations. Why least squares: see FIT_METHOD.
A fit stops "converged" when a step changes the objective by less than 1e-10 relative and every parameter by less
than 1e-6, or "stationary" when no step lowers the objective even at the largest damping (a minimum to working
precision, or a stall: Fit.stop says which). Bootstrap resamples are warm-started from the full fit (heuristic:
standard and fast, but it can understate the spread if the objective has several minima, e.g. the floor trading
against the width).
"""

from __future__ import annotations

import json
import math
import re
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from kz4ap_synth.jitter import SNR_CELLS, SPEED_CELLS
from kz4ap_synth.suites import BOOTSTRAP_RESAMPLES, _interval, _rng_for, _scorings, load_results, oracle_copies

V_REF_WPM = math.sqrt(SPEED_CELLS.edges[0] * SPEED_CELLS.edges[-1])  # 25.30 WPM
DIT_S_WPM = 1.2               # the dit, s, is 1.2 s / WPM (PARIS)
SNR_BANDWIDTH_HZ = 500.0      # S500's noise bandwidth, Hz
CER_LEVELS = (0.10, 0.05)     # the crossings reported (the plan's, principle 6; the old suite's CER_THRESHOLDS)
AXES = {"s500": "S500 (dB SNR in 500 Hz)", "en0": "E/N0 per dit (dB re 1)"}
N_CELLS = len(SPEED_CELLS)
# a: s0's coefficients (dB); b: ln(w / 1 dB)'s; g1 ... g10: the speed cells' floors' logits (one floor per speed cell)
PARAMETERS = ("a0", "a1", "a2", "b0", "b1", "b2") + tuple(f"g{k}" for k in SPEED_CELLS.numbers)
N_SHAPE = 6                   # the shape parameters a0 ... b2 come first, the floors' logits after them
ALL_FREE = (True,) * len(PARAMETERS)
S0_CONSTANT = (True, False, False, True, True, True) + (True,) * N_CELLS  # time-base-invariant hypothesis: a1 = a2 = 0
START_FLOOR = 0.002           # the floors' first starting value (heuristic; _starts)
# The floors are held within [FLOOR_LIMIT, 1 - FLOOR_LIMIT] (their logits within +/-G_LIMIT). Heuristic: a cell with no
# errors at high S500 drives its floor toward 0, where the objective flattens and the logit would drift without end
# (the fit would stop on its iteration limit); 1e-5 is far below anything these data resolve (a few times 10^4
# symbols per speed cell, one error in them is above 1e-5).
FLOOR_LIMIT = 1e-5
G_LIMIT = math.log((1.0 - FLOOR_LIMIT) / FLOOR_LIMIT)
# The default method. Weighted least squares on each signal's CER, weights its reference symbols (measured on
# synthetic data, test_devset2.py and the task D2 report): both methods recover s0 and w to within their tolerances
# on binomial data, but with whole-signal failures (a signal that locks to a wrong speed prints garbage at any S500)
# the binomial likelihood charges such a signal about n ln(1/f) (f the model's CER there) and lets a few of them move
# the floor and the slope, while least squares bounds each signal's cost by its symbols. Over 40 sets with 3% such
# failures the mean bias of the CER-0.10 crossing was 0.01-0.06 dB SNR in 500 Hz per cell for least squares and
# 0.20-0.27 dB for the binomial likelihood; the direction held in every cell in the review's independent check (12
# sets, other seeds), where least squares' bias reached 0.29 dB in cell 10 (the magnitude at the speed extremes is
# not pinned down tightly). Bootstrap failures, 0 of 200 against 1 of 200, are weak evidence and not a reason.
# Re-measured with one floor per speed cell (task D4; each cell's floor then absorbs its own few failures): over 200
# sets of 4 signals per cell, least squares +0.05 to +0.15 dB per cell (RMS 0.38-0.75 dB), the binomial likelihood
# +0.18 to +0.29 dB (RMS 0.50-0.76 dB); at 48 signals per cell (20 sets) +0.00 to +0.11 dB against +0.14 to +0.29 dB.
FIT_METHOD = "wls"
METHODS = ("wls", "binomial")
MIN_SIGNALS_FOR_FIT = 20      # heuristic: about three signals per fitted parameter
FINE_SPEEDS_WPM = tuple(float(v) for v in np.geomspace(SPEED_CELLS.edges[0], SPEED_CELLS.edges[-1], 61))
FIT_GROUPS = ("A2 sensitivity",)
DETECTOR_GROUPS = ("A2 sensitivity, detector",)
# Seed 1 of the jittered set's oracle recordings (experiments.DEV2) and A2's detector-path copies.
DEV2_ANALYSIS = (r"^(A2-awgn|A2-detector|B2-fading|C2-fists|D2-speed|E2-qrm|F2-offset|F2-drift|G2-qso|H2-qso|"
                 r"I2-farnsworth|S2-stretch)-.*-s1$")
# Heuristic: an interval over test cases from fewer is not shown (A2 puts each speed cell's signals in 2 test cases).
MIN_TEST_CASES_FOR_INTERVAL = 5
# Groups whose signals copy another group's (S2: A2's speed-cell-5 signals stretched by 2), left out of the pooled
# paired sets with the detector path (paired()).
NOT_POOLED_GROUPS = ("S2 stretch",)
UNITS_PER_CHARACTER = 10.0    # the genie bound's units per character, inter-character space included (heuristic)
Z_95 = 1.959963984540054      # the standard normal's 97.5% point
CHI2_2DOF_95 = -2.0 * math.log(0.05)  # 5.991: chi-square with 2 degrees of freedom has survival exp(-T / 2) (derived)


def en0_db(s500_db, wpm):
    """E/N0 per dit, dB re 1: S500 + 10 log10(500 Hz * 1.2 s / WPM) (derived)."""
    return np.asarray(s500_db, float) + 10.0 * np.log10(SNR_BANDWIDTH_HZ * DIT_S_WPM / np.asarray(wpm, float))


def s500_from_en0_db(en0, wpm):
    """The inverse of en0_db: S500 (dB SNR in 500 Hz) from E/N0 per dit (dB re 1)."""
    return np.asarray(en0, float) - 10.0 * np.log10(SNR_BANDWIDTH_HZ * DIT_S_WPM / np.asarray(wpm, float))


def _expit(z):
    z = np.asarray(z, float)
    out = np.empty_like(z)
    pos = z >= 0
    out[pos] = 1.0 / (1.0 + np.exp(-z[pos]))
    e = np.exp(z[~pos])
    out[~pos] = e / (1.0 + e)
    return out


def _log_expit(z):
    return -np.logaddexp(0.0, -np.asarray(z, float))


def _logit(p: float) -> float:
    return math.log(p / (1.0 - p))


def _design(wpm) -> np.ndarray:
    x = np.log(np.asarray(wpm, float) / V_REF_WPM)
    return np.stack([np.ones_like(x), x, x * x], axis=-1)


def cell_index(wpm) -> np.ndarray:
    """The speed cell of each speed as an index 0 ... N_CELLS - 1 (the cell number minus 1). An edge belongs to the
    cell above it and the top edge to the top cell, as in SPEED_CELLS.cell_of; a speed outside 8-80 WPM gets the
    nearest cell."""
    return np.searchsorted(np.asarray(SPEED_CELLS.edges[1:-1], float), np.asarray(wpm, float), side="right")


@dataclass(frozen=True)
class Fit:
    """A fitted model. params: PARAMETERS' values (a in dB, b for ln(w / 1 dB), g1 ... g10 the speed cells' floors'
    logits); axis "s500" or "en0" (the level the curve is a function of). fitted_cells: the speed cells whose floor
    was fitted (a cell without signals keeps its start value, and its floor is reported as None)."""
    params: tuple
    axis: str = "s500"
    method: str = FIT_METHOD
    objective: float = float("nan")   # wls: sum of symbols x (CER - model)^2; binomial: deviance (nats x 2)
    converged: bool = True
    iterations: int = 0
    signals: int = 0
    free: tuple = ALL_FREE
    stop: str = "converged"           # "converged", "stationary" (no step lowers the objective) or "iterations"
    fitted_cells: tuple = tuple(SPEED_CELLS.numbers)

    @property
    def floors(self) -> tuple:
        """Each speed cell's floor c_j (cells 1 ... 10), a CER."""
        return tuple(float(c) for c in _expit(np.asarray(self.params[N_SHAPE:], float)))

    def floor_at(self, wpm):
        """The floor of the speed cell holding each speed."""
        return np.asarray(self.floors)[cell_index(wpm)]

    def s0_db(self, wpm):
        return _design(wpm) @ np.asarray(self.params[0:3])

    def w_db(self, wpm):
        return np.exp(_design(wpm) @ np.asarray(self.params[3:6]))

    def cer(self, level_db, wpm):
        """The model's CER at level_db (on the fit's axis) and wpm."""
        level_db, wpm = np.broadcast_arrays(np.asarray(level_db, float), np.asarray(wpm, float))
        z = -(level_db - self.s0_db(wpm)) / self.w_db(wpm)
        c = self.floor_at(wpm)
        return c + (1.0 - c) * _expit(np.atleast_1d(z)).reshape(np.shape(z))

    def crossing_db(self, threshold: float, wpm):
        """The level (dB, on the fit's axis) at which the model's CER falls to threshold at wpm: s0 - w logit(q),
        q = (threshold - c_j) / (1 - c_j), c_j the floor of wpm's speed cell (derived); NaN where that floor is at or
        above the threshold (the curve never reaches it)."""
        wpm = np.asarray(wpm, float)
        c = self.floor_at(wpm)
        reach = c < threshold
        q = np.where(reach, (threshold - c) / (1.0 - c), 0.5)
        return np.where(reach, self.s0_db(wpm) - self.w_db(wpm) * np.log(q / (1.0 - q)), np.nan)

    def to_json(self) -> dict:
        floors = {str(k): (c if k in self.fitted_cells else None) for k, c in zip(SPEED_CELLS.numbers, self.floors)}
        return {"params": dict(zip(PARAMETERS, self.params)), "axis": self.axis, "method": self.method,
                "floors": floors, "objective": self.objective, "converged": self.converged,
                "iterations": self.iterations, "signals": self.signals, "stop": self.stop,
                "free": [p for p, f in zip(PARAMETERS, self.free) if f], "fitted_cells": list(self.fitted_cells)}

    @staticmethod
    def from_json(d: dict) -> "Fit":
        """Reads to_json's output; also task D2's, whose single floor "g" becomes every cell's floor."""
        params = dict(d["params"])
        free = list(d["free"])
        if "g" in params:   # task D2: one floor for all speed cells
            g = params.pop("g")
            params.update({f"g{k}": g for k in SPEED_CELLS.numbers})
            if "g" in free:
                free += [f"g{k}" for k in SPEED_CELLS.numbers]
        return Fit(tuple(params[p] for p in PARAMETERS), d["axis"], d["method"], d["objective"],
                   d["converged"], d["iterations"], d["signals"], tuple(p in free for p in PARAMETERS),
                   d.get("stop", "converged"), tuple(d.get("fitted_cells", SPEED_CELLS.numbers)))


@dataclass
class _Data:
    level: np.ndarray    # dB on the axis
    wpm: np.ndarray
    k: np.ndarray        # edits, clipped to n
    n: np.ndarray        # reference symbols
    X: np.ndarray = field(init=False)
    cell: np.ndarray = field(init=False)   # each signal's speed cell index, 0 ... N_CELLS - 1

    def __post_init__(self) -> None:
        self.X = _design(self.wpm)
        self.cell = cell_index(self.wpm)

    @property
    def y(self) -> np.ndarray:
        return self.k / self.n

    def take(self, idx) -> "_Data":
        return _Data(self.level[idx], self.wpm[idx], self.k[idx], self.n[idx])


def _data(level_db, wpm, edits, symbols) -> _Data:
    level, wpm = np.asarray(level_db, float), np.asarray(wpm, float)
    edits, symbols = np.asarray(edits, float), np.asarray(symbols, float)
    keep = symbols > 0
    return _Data(level[keep], wpm[keep], np.minimum(edits[keep], symbols[keep]), symbols[keep])


def _evaluate(theta, d: _Data, method: str, jacobian: bool = True):
    """(objective, f, J, W): the model f, its Jacobian J, and the weights W of the normal equations
    (J' W J) delta = J' W (y - f): least squares W = n; binomial W = n / (f (1 - f)) (Fisher scoring)."""
    s0 = d.X @ theta[0:3]
    lw = d.X @ theta[3:6]
    w = np.exp(lw)
    z = -(d.level - s0) / w
    g = np.asarray(theta[N_SHAPE:], float)[d.cell]
    c = _expit(g)                     # each signal's floor: its speed cell's
    sig = _expit(z)
    f = c + (1.0 - c) * sig
    one_minus_f = (1.0 - c) * _expit(-z)
    y = d.y
    if method == "wls":
        obj = float(np.sum(d.n * (y - f) ** 2))
        W = d.n
    elif method == "binomial":
        log_1mc = _log_expit(-g)
        log_f = np.logaddexp(_log_expit(g), log_1mc + _log_expit(z))
        log_1mf = log_1mc + _log_expit(-z)
        with np.errstate(divide="ignore", invalid="ignore"):
            sat = (np.where(d.k > 0, d.k * np.log(np.where(d.k > 0, y, 1.0)), 0.0)
                   + np.where(d.n - d.k > 0, (d.n - d.k) * np.log(np.where(d.n > d.k, 1.0 - y, 1.0)), 0.0))
        obj = float(2.0 * np.sum(sat - d.k * log_f - (d.n - d.k) * log_1mf))
        W = d.n / np.maximum(f * one_minus_f, 1e-300)
    else:
        raise ValueError(f"unknown method {method!r}")
    if not jacobian:
        return obj, f, None, W
    ds = (1.0 - c) * sig * (1.0 - sig)
    J = np.zeros((len(f), len(PARAMETERS)))
    J[:, 0:3] = (ds / w)[:, None] * d.X
    J[:, 3:6] = (ds * -z)[:, None] * d.X
    J[np.arange(len(f)), N_SHAPE + d.cell] = c * (1.0 - c) * (1.0 - sig)
    return obj, f, J, W


def _data_floors(d: _Data) -> np.ndarray:
    """Each speed cell's starting floor from its data: the mean CER of the quarter of its signals at the highest
    levels, clipped to [START_FLOOR, 0.9] (START_FLOOR for a cell without signals). Heuristic: a starting point."""
    out = np.full(N_CELLS, START_FLOOR)
    for j in range(N_CELLS):
        mine = np.flatnonzero(d.cell == j)
        if len(mine):
            top = mine[np.argsort(d.level[mine])[-max(1, len(mine) // 4):]]
            out[j] = min(max(float(np.mean(d.y[top])), START_FLOOR), 0.9)
    return out


def _starts(d: _Data, method: str) -> list[np.ndarray]:
    """Two starting points (heuristic), each the best point of a coarse grid over a0 (the data's level range, 29
    points) and b0 (w = 0.5, 1, 2, 4 dB) with the speed terms 0: one with every floor at START_FLOOR, one with each
    cell's floor from its data (_data_floors). fit_cer runs Levenberg-Marquardt from both and keeps the lower
    objective."""
    out = []
    for floors in (np.full(N_CELLS, START_FLOOR), _data_floors(d)):
        g = np.log(floors / (1.0 - floors))
        best = None
        for a0 in np.linspace(d.level.min(), d.level.max(), 29):
            for w0 in (0.5, 1.0, 2.0, 4.0):
                theta = np.concatenate([[a0, 0.0, 0.0, math.log(w0), 0.0, 0.0], g])
                obj = _evaluate(theta, d, method, jacobian=False)[0]
                if best is None or obj < best[0]:
                    best = (obj, theta)
        out.append(best[1])
    return out


def _fitted_mask(d: _Data, free: tuple) -> tuple:
    """free, with the floors of speed cells that hold no signals held fixed (no data determines them)."""
    present = np.bincount(d.cell, minlength=N_CELLS) > 0
    return tuple(bool(f) and (i < N_SHAPE or bool(present[i - N_SHAPE])) for i, f in enumerate(free))


def _levenberg_marquardt(theta, d: _Data, method: str, free: tuple, max_iter: int = 200, rtol: float = 1e-10):
    mask = np.array(free, bool)
    theta = np.array(theta, float)
    obj, f, J, W = _evaluate(theta, d, method)
    lam = 1e-3
    converged = False
    stop = "iterations"
    it = 0
    for it in range(1, max_iter + 1):
        # active set: a floor's logit at its bound whose descent direction points out of the bounds is held for this
        # iteration (projected Levenberg-Marquardt), so that it does not distort the other parameters' joint step
        full_grad = J.T @ (W * (d.y - f))
        g = theta[N_SHAPE:]
        held = np.zeros(len(theta), bool)
        held[N_SHAPE:] = ((g <= -G_LIMIT) & (full_grad[N_SHAPE:] < 0)) | ((g >= G_LIMIT) & (full_grad[N_SHAPE:] > 0))
        use = mask & ~held
        Jm = J[:, use]
        A = Jm.T @ (W[:, None] * Jm)
        grad = full_grad[use]
        improved = False
        while lam < 1e12:
            M = A + lam * np.diag(np.diag(A)) + 1e-12 * np.eye(len(A))
            try:
                step = np.linalg.solve(M, grad)
            except np.linalg.LinAlgError:
                lam *= 4.0
                continue
            trial = theta.copy()
            trial[use] += step
            trial[N_SHAPE:] = np.clip(trial[N_SHAPE:], -G_LIMIT, G_LIMIT)
            if not np.all(np.isfinite(trial)):
                lam *= 4.0
                continue
            new_obj = _evaluate(trial, d, method, jacobian=False)[0]
            if np.isfinite(new_obj) and new_obj <= obj:
                improved = True
                break
            lam *= 4.0
        if not improved:
            converged, stop = True, "stationary"  # no step lowers the objective: a minimum to working precision, or a stall
            break
        decrease = obj - new_obj
        change = np.max(np.abs(trial - theta))
        theta = trial
        obj, f, J, W = _evaluate(theta, d, method)
        lam = max(lam / 3.0, 1e-12)
        if decrease <= rtol * (1.0 + abs(obj)) and change < 1e-6:
            converged, stop = True, "converged"
            break
    return theta, obj, converged, it, stop


def fit_cer(level_db, wpm, edits, symbols, *, axis: str = "s500", method: str = FIT_METHOD, free: tuple = ALL_FREE,
            start=None) -> Fit:
    """Fits the module's model to signals: level_db (on `axis`, dB), wpm, edits and reference symbols per signal.
    free: which PARAMETERS are fitted (the others held at start's values, or 0 for a1, a2 when no start is given).
    start: initial parameters (a bootstrap warm-starts from the full fit); otherwise a coarse grid (_start)."""
    if method not in METHODS:
        raise ValueError(f"unknown method {method!r}")
    d = _data(level_db, wpm, edits, symbols)
    if len(d.n) < MIN_SIGNALS_FOR_FIT:
        raise ValueError(f"{len(d.n)} signals: a fit needs at least {MIN_SIGNALS_FOR_FIT}")
    if len(free) != len(PARAMETERS):
        raise ValueError(f"free has {len(free)} entries, not {len(PARAMETERS)}")
    mask = _fitted_mask(d, free)
    cells = tuple(k for k in SPEED_CELLS.numbers if np.any(d.cell == k - 1))
    best = None
    for theta in ([np.array(start, float)] if start is not None else _starts(d, method)):
        result = _levenberg_marquardt(theta, d, method, mask)
        if best is None or result[1] < best[1]:
            best = result
    theta, obj, converged, it, stop = best
    return Fit(tuple(float(t) for t in theta), axis, method, obj, converged, it, len(d.n), mask, stop, cells)


def bootstrap_fits(level_db, wpm, edits, symbols, fit: Fit, key, resamples: int = BOOTSTRAP_RESAMPLES) -> list:
    """The fit again on `resamples` resamples of the signals (with replacement, all signals pooled), each warm-started
    from `fit`, seeded by key; a resample whose fit fails or does not converge gives None."""
    d = _data(level_db, wpm, edits, symbols)
    picks = _rng_for(("devset2", key)).integers(len(d.n), size=(resamples, len(d.n)))
    out = []
    for p in picks:
        sub = d.take(p)
        try:
            theta, obj, converged, it, stop = _levenberg_marquardt(np.array(fit.params), sub, fit.method, fit.free)
        except (FloatingPointError, ValueError):
            out.append(None)
            continue
        out.append(Fit(tuple(float(t) for t in theta), fit.axis, fit.method, obj, converged, it, len(sub.n), fit.free,
                       stop, fit.fitted_cells) if converged else None)
    return out


def _pct_interval(values) -> list | None:
    v = _interval([None if x is None or not np.isfinite(x) else float(x) for x in values])
    return None if v is None else [v[0], v[1]]


def top_levels(level_db, wpm) -> np.ndarray:
    """The highest level (dB, on the fit's axis) among each speed cell's signals, per cell index (NaN for a cell
    without signals): where the data end, for the "no crossing" rule."""
    level, cell = np.asarray(level_db, float), cell_index(wpm)
    return np.array([level[cell == j].max() if np.any(cell == j) else np.nan for j in range(N_CELLS)])


def _within_data(values, speeds, top) -> np.ndarray:
    """values with every crossing above its cell's highest level (top, per cell index) set to NaN."""
    values = np.array(values, float)
    if top is not None:
        limit = np.asarray(top, float)[cell_index(speeds)]
        values[~(values <= limit)] = np.nan   # also NaN where the cell has no data (limit NaN)
    return values


def crossings(fit: Fit, boots: list, speeds_wpm, threshold: float, top=None) -> list[dict]:
    """The fit's crossing at threshold at each speed, with its 95% bootstrap interval (None unless 95% of the
    resamples give a crossing). top: the highest level of each speed cell's signals (top_levels); a crossing above
    it is "no crossing" (the curve does not reach the threshold within the data), as is a cell whose floor is at or
    above the threshold, for the point and for each resample alike. Each entry gives the reason ("floor", "above the
    data" or None), the cell's floor, the fitted CER at the cell's highest level, and the share of the resamples
    that give a crossing."""
    speeds = np.asarray(speeds_wpm, float)
    raw = np.atleast_1d(fit.crossing_db(threshold, speeds))
    point = _within_data(raw, speeds, top)
    per = np.array([_within_data(np.atleast_1d(b.crossing_db(threshold, speeds)), speeds, top) if b is not None
                    else np.full(len(point), np.nan) for b in boots]) if boots else np.empty((0, len(point)))
    floors = fit.floor_at(speeds)
    out = []
    for i, v in enumerate(speeds):
        j = int(cell_index(v))
        top_db = None if top is None or not np.isfinite(top[j]) else float(top[j])
        reason = None
        if not np.isfinite(point[i]):
            reason = ("floor" if floors[i] >= threshold else
                      "no data" if top is not None and top_db is None else "above the data")
        out.append({"wpm": float(v), "value": None if reason else float(point[i]),
                    "interval": _pct_interval(per[:, i]) if len(per) else None,
                    "no_crossing": reason, "floor": float(floors[i]),
                    "top_db": top_db,
                    "cer_at_top": None if top_db is None else float(fit.cer(top_db, v)),
                    "resamples_with_crossing": float(np.mean(np.isfinite(per[:, i]))) if len(per) else None})
    return out


# --- the genie-aided bound (stage-1 results record, section 5.1.1; derived, with a heuristic count) ---

def _q_inverse(p: float) -> float:
    """x with Q(x) = 0.5 erfc(x / sqrt 2) = p, by bisection (to 1e-12)."""
    lo, hi = -10.0, 40.0
    for _ in range(200):
        mid = 0.5 * (lo + hi)
        if 0.5 * math.erfc(mid / math.sqrt(2.0)) > p:
            lo = mid
        else:
            hi = mid
    return 0.5 * (lo + hi)


def ideal_en0_db(cer: float, coherent: bool = False) -> float:
    """E/N0 per dit (dB re 1) at which the genie-aided receiver (timing, speed and amplitude known; one on/off
    decision per Morse unit) reaches `cer`, with CER = UNITS_PER_CHARACTER x P_u: noncoherent P_u = 1/2 exp(-E/4N0)
    (high-SNR approximation), coherent P_u = Q(sqrt(E/2N0)). Derived (stage-1 results record 5.1.1); the 10 units per
    character is heuristic (about +/-0.7 dB). CER 0.10: 11.9 dB re 1 noncoherent, 10.3 dB re 1 coherent."""
    pu = cer / UNITS_PER_CHARACTER
    ratio = 2.0 * _q_inverse(pu) ** 2 if coherent else 4.0 * math.log(1.0 / (2.0 * pu))
    return 10.0 * math.log10(ratio)


def ideal_s500_db(cer: float, wpm, coherent: bool = False):
    """The genie-aided bound in S500 (dB SNR in 500 Hz) at wpm."""
    return s500_from_en0_db(ideal_en0_db(cer, coherent), wpm)


# --- rows ---

def _label_files(out_dir: Path) -> dict:
    """result name -> (labels file, through the detector path?) for every scoring in the manifest."""
    manifest = json.loads((Path(out_dir) / "manifest.json").read_text())
    out = {}
    for rec in manifest["recordings"]:
        for label_file, result, _ in _scorings(rec):
            out[result] = (label_file, not rec["oracle"])
        for label_file, result, _ in oracle_copies(rec):
            out[result] = (label_file, False)
    return out


def default_results_dirs(out_dir: Path) -> list[Path]:
    """The engine's decoders' results (out/results) and the experiments' runs (out/experiments/results)."""
    return [p for p in (Path(out_dir) / "results", Path(out_dir) / "experiments" / "results") if p.exists()]


def load_rows(out_dir, decoders, results_dirs=None, only: str | None = DEV2_ANALYSIS) -> list[dict]:
    """One row per scored signal of each decoder (results folder name) on the test cases matching `only`: its
    speed (the label's wpm, WPM; I2's is the character speed), speed cell (SPEED_CELLS.cell_of), S500 (the label's
    snr_db, dB SNR in 500 Hz), S500 cell, E/N0 per dit (dB re 1), edits and reference symbols, whether the detector
    path found it, and whether its sound leaves the oracle anchor's +/-12 Hz."""
    out_dir = Path(out_dir)
    rows, _ = load_results(out_dir, results_dirs or default_results_dirs(out_dir), list(decoders))
    files = _label_files(out_dir)
    cache: dict = {}
    out = []
    for r in rows:
        if not r["scored"] or (only is not None and not re.search(only, r["recording"])):
            continue
        label_file, detector = files[r["recording"]]
        if label_file not in cache:
            cache[label_file] = json.loads((out_dir / label_file).read_text())["signals"]
        label = cache[label_file][r["index"]]
        wpm, s500 = float(label["wpm"]), float(label["snr_db"])
        out.append({"decoder": r["front_end"], "group": r["group"], "test_case": r["recording"], "index": r["index"],
                    "path": "detector" if detector else "oracle", "speed_wpm": wpm,
                    "speed_cell": SPEED_CELLS.cell_of(wpm), "s500_db": s500, "s500_cell": SNR_CELLS.cell_of(s500),
                    "en0_db": float(en0_db(s500, wpm)), "edits": int(r["edits"]), "symbols": int(r["symbols"]),
                    "detected": bool(r["detected"]), "beyond_oracle_anchor": bool(r["beyond_oracle_anchor"])})
    return out


# --- the fits for one decoder ---

def _columns(rows, axis):
    return ([r[f"{axis}_db"] for r in rows], [r["speed_wpm"] for r in rows], [r["edits"] for r in rows],
            [r["symbols"] for r in rows])


def _cell_centers() -> list[float]:
    return [SPEED_CELLS.center(k) for k in SPEED_CELLS.numbers]


def _wald(boots: list, point: Fit, names=("a1", "a2")) -> dict:
    """A Wald test of the named parameters being 0, with the bootstrap's covariance: T = p' S^-1 p, p-value exp(-T/2)
    for 2 degrees of freedom (assumes the bootstrap distribution about normal)."""
    idx = [PARAMETERS.index(n) for n in names]
    sample = np.array([[b.params[i] for i in idx] for b in boots if b is not None])
    p = np.array([point.params[i] for i in idx])
    if len(sample) < 3:
        return {"terms": list(names), "statistic": None, "p_value": None, "resamples_used": len(sample)}
    cov = np.cov(sample, rowvar=False)
    t = float(p @ np.linalg.solve(cov, p))
    return {"terms": list(names), "statistic": t, "p_value": math.exp(-t / 2.0) if len(idx) == 2 else None,
            "resamples_used": len(sample),
            "threshold_95": CHI2_2DOF_95,
            "intervals": {n: _pct_interval(sample[:, j]) for j, n in enumerate(names)}}


def fit_decoder(rows, method: str = FIT_METHOD, resamples: int = BOOTSTRAP_RESAMPLES, key=()) -> dict:
    """For one decoder's fit rows: the fit against S500 with crossings per speed cell center and on FINE_SPEEDS_WPM,
    and the E/N0 view (the full fit against E/N0, its per-cell crossings and spread, the test of invariance, the fit
    with s0 constant)."""
    centers = _cell_centers()
    cells = list(SPEED_CELLS.numbers)
    counts = {k: sum(1 for r in rows if r["speed_cell"] == k) for k in cells}
    levels = [r["s500_db"] for r in rows]
    started = time.process_time()
    out = {"signals": len(rows), "method": method, "resamples": resamples,
           "s500_range_db": [min(levels), max(levels)] if levels else None}
    s_cols = _columns(rows, "s500")
    s_top = top_levels(s_cols[0], s_cols[1])
    s_bottom = -top_levels(-np.asarray(s_cols[0], float), s_cols[1])   # each cell's lowest level (NaN without data)
    s_fit = fit_cer(*s_cols, axis="s500", method=method)
    s_boots = bootstrap_fits(*s_cols, s_fit, (key, "s500", method), resamples)
    out["s500"] = {"fit": s_fit.to_json(), "failed_resamples": sum(b is None for b in s_boots),
                   "param_intervals": {p: _pct_interval([b.params[i] if b else None for b in s_boots])
                                       for i, p in enumerate(PARAMETERS)},
                   "floor_intervals": {str(k): _pct_interval([b.floors[k - 1] if b else None for b in s_boots])
                                       if k in s_fit.fitted_cells else None for k in cells}}
    for t in CER_LEVELS:
        per_cell = crossings(s_fit, s_boots, centers, t, s_top)
        for k, c in zip(cells, per_cell):
            c.update(cell=k, signals=counts[k], en0_db=None if c["value"] is None else float(en0_db(c["value"], c["wpm"])),
                     # below the cell's own lowest level, as "above the data" uses the cell's highest
                     outside_data=c["value"] is not None and bool(c["value"] < s_bottom[k - 1]),
                     ideal_noncoherent_db=float(ideal_s500_db(t, c["wpm"])),
                     ideal_coherent_db=float(ideal_s500_db(t, c["wpm"], coherent=True)))
        out["s500"][f"crossings_{t:.2f}"] = per_cell
        out["s500"][f"curve_{t:.2f}"] = crossings(s_fit, s_boots, FINE_SPEEDS_WPM, t, s_top)
    e_cols = _columns(rows, "en0")
    e_top = top_levels(e_cols[0], e_cols[1])
    e_fit = fit_cer(*e_cols, axis="en0", method=method)
    e_boots = bootstrap_fits(*e_cols, e_fit, (key, "en0", method), resamples)
    e_per_cell = [{**c, "cell": k, "signals": counts[k]} for k, c in
                  zip(cells, crossings(e_fit, e_boots, centers, 0.10, e_top))]
    # the spread over the cells that cross at the full fit (a cell with no crossing has no E/N0 value to compare)
    crossing_cells = [i for i, c in enumerate(e_per_cell) if c["value"] is not None]
    spread_centers = np.array(centers)[crossing_cells]
    e_cross = np.array([e_per_cell[i]["value"] for i in crossing_cells])
    spreads = []
    for b in e_boots:
        v = None if b is None else _within_data(b.crossing_db(0.10, spread_centers), spread_centers, e_top)
        spreads.append(None if v is None or len(v) < 2 or not np.all(np.isfinite(v)) else float(np.ptp(v)))
    restricted = fit_cer(*e_cols, axis="en0", method=method, free=S0_CONSTANT,
                         start=[e_fit.params[0], 0.0, 0.0, *e_fit.params[3:]])
    out["en0"] = {"fit": e_fit.to_json(), "failed_resamples": sum(b is None for b in e_boots),
                  "crossings_0.10": e_per_cell,
                  "spread_cells": [cells[i] for i in crossing_cells],
                  "spread_db": float(np.ptp(e_cross)) if len(e_cross) >= 2 else None,
                  "spread_interval": _pct_interval(spreads),
                  "invariance": _wald(e_boots, e_fit),
                  "w_terms": _wald(e_boots, e_fit, ("b1", "b2")),
                  "s0_constant": {"fit": restricted.to_json(),
                                  "crossings_0.10": crossings(restricted, [], centers, 0.10, e_top),
                                  "objective_increase": restricted.objective - e_fit.objective},
                  "ideal_noncoherent_db": ideal_en0_db(0.10), "ideal_coherent_db": ideal_en0_db(0.10, True)}
    out["cpu_s"] = time.process_time() - started
    return out


def binned_cer(rows) -> list[dict]:
    """Pooled CER (summed edits over summed symbols) per (speed cell, S500 cell), with the cell's mean S500."""
    cells: dict = {}
    for r in rows:
        if r["speed_cell"] is None or r["s500_cell"] is None:
            continue
        cells.setdefault((r["speed_cell"], r["s500_cell"]), []).append(r)
    return [{"speed_cell": v, "s500_cell": k, "signals": len(rs), "mean_s500_db": float(np.mean([r["s500_db"] for r in rs])),
             "cer": sum(r["edits"] for r in rs) / sum(r["symbols"] for r in rs)} for (v, k), rs in sorted(cells.items())]


def wilson(successes: int, n: int) -> list | None:
    """The Wilson score 95% interval of a binomial proportion (derived; used for recall, where several cells are
    all-detected and a bootstrap would give a zero-width interval)."""
    if n == 0:
        return None
    p = successes / n
    z2 = Z_95 ** 2
    center = (p + z2 / (2 * n)) / (1 + z2 / n)
    half = Z_95 * math.sqrt(p * (1 - p) / n + z2 / (4 * n * n)) / (1 + z2 / n)
    return [max(0.0, center - half), min(1.0, center + half)]


def detection_recall(rows) -> list[dict]:
    """Per (speed cell, S500 cell) of detector-path rows: labels, detected, recall and its Wilson 95% interval, the
    mean S500 of the cell's signals."""
    cells: dict = {}
    for r in rows:
        if r["speed_cell"] is None or r["s500_cell"] is None:
            continue
        cells.setdefault((r["speed_cell"], r["s500_cell"]), []).append(r)
    out = []
    for (v, k), rs in sorted(cells.items()):
        hits = sum(r["detected"] for r in rs)
        out.append({"speed_cell": v, "s500_cell": k, "labels": len(rs), "detected": hits, "recall": hits / len(rs),
                    "interval": wilson(hits, len(rs)), "mean_s500_db": float(np.mean([r["s500_db"] for r in rs]))})
    return out


# --- paired comparisons ---

def _paired_stats(diffs: list, cases: list, anchor: int, key) -> dict:
    v = np.array(diffs, float)
    out = {"signals": len(v), "test_cases": len(set(cases)), "mean": float(v.mean()), "beyond_oracle_anchor": anchor,
           "interval": None, "interval_test_cases": None}
    if len(v) >= 2:
        picks = _rng_for(("devset2-paired", key)).integers(len(v), size=(BOOTSTRAP_RESAMPLES, len(v)))
        out["interval"] = _pct_interval([float(v[p].mean()) for p in picks])
    names = sorted(set(cases))
    if len(names) >= MIN_TEST_CASES_FOR_INTERVAL:
        index = {n: i for i, n in enumerate(names)}
        case = np.array([index[c] for c in cases])
        sums = np.bincount(case, weights=v, minlength=len(names))
        counts = np.bincount(case, minlength=len(names)).astype(float)
        picks = _rng_for(("devset2-paired-cases", key)).integers(len(names), size=(BOOTSTRAP_RESAMPLES, len(names)))
        out["interval_test_cases"] = _pct_interval([float(sums[p].sum() / counts[p].sum()) for p in picks])
    return out


def paired(rows, reference: str, variant: str) -> dict:
    """variant minus reference, signal by signal (each signal's CER, edits over symbols, as experiments.compare), on
    the signals both scored: the mean difference with bootstrap 95% intervals over signals (as experiments.compare)
    and over test cases (all of a test case's signals resampled together), pooled ("all"), per group, per speed cell
    over all groups, and per group and speed cell.

    The pooled and per-speed-cell sets hold each signal once: oracle-path rows only, and not the groups of
    NOT_POOLED_GROUPS. A2's detector-path copies are the same signals and noise as A2's oracle recordings, and S2's
    are A2's cell-5 texts and timing stretched (new noise); pooled with them they would count A2 twice and narrow the
    intervals as if the copies were independent (review I1). Both stay groups of their own."""
    by = {}
    for r in rows:
        if r["decoder"] in (reference, variant) and r["symbols"] > 0:
            by.setdefault(r["decoder"], {})[(r["test_case"], r["index"])] = r
    ref, var = by.get(reference, {}), by.get(variant, {})
    sets: dict = {}
    for key in sorted(set(ref) & set(var)):
        a, b = ref[key], var[key]
        d = b["edits"] / b["symbols"] - a["edits"] / a["symbols"]
        pooled = a["path"] == "oracle" and a["group"] not in NOT_POOLED_GROUPS
        names = [("group", a["group"]), ("group-cell", a["group"], a["speed_cell"])]
        names += ["all", ("cell", a["speed_cell"])] if pooled else []
        for name in names:
            s = sets.setdefault(name, ([], [], [0]))
            s[0].append(d)
            s[1].append(key[0])
            s[2][0] += a["beyond_oracle_anchor"]
    out = {"reference": reference, "variant": variant, "all": None, "groups": {}, "speed_cells": {}, "group_cells": []}
    for name, (diffs, cases, anchor) in sets.items():
        stats = _paired_stats(diffs, cases, anchor[0], (reference, variant, name))
        if name == "all":
            out["all"] = stats
        elif name[0] == "group":
            out["groups"][name[1]] = stats
        elif name[0] == "cell":
            out["speed_cells"][str(name[1])] = stats
        else:
            out["group_cells"].append({"group": name[1], "speed_cell": name[2], **stats})
    return out


# --- the whole analysis ---

def git_commit(repo: Path | None = None) -> dict:
    """The repository's HEAD and whether the working tree differs from it (None if git is unavailable)."""
    repo = repo or Path(__file__).resolve().parents[2]
    try:
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True)
        status = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=repo,
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return {"commit": None, "modified": None}
    return {"commit": head.stdout.strip(), "modified": bool(status.stdout.strip())}


def analyze(rows, decoders, reference: str | None = None, fit_groups=FIT_GROUPS, method: str = FIT_METHOD,
            resamples: int = BOOTSTRAP_RESAMPLES, detector_groups=DETECTOR_GROUPS, meta: dict | None = None) -> dict:
    """The analysis as one JSON-ready dict: per decoder its fits (oracle rows of fit_groups), binned CER and detection
    recall (rows of detector_groups); paired comparisons of every other decoder with `reference`; the signals."""
    result = {"meta": {**(meta or {}), "decoders": list(decoders), "reference": reference, "fit_groups": list(fit_groups),
                       "method": method, "resamples": resamples, "cer_levels": list(CER_LEVELS),
                       "speed_cells": [{"cell": k, "range_wpm": list(SPEED_CELLS.bounds(k)),
                                        "center_wpm": SPEED_CELLS.center(k)} for k in SPEED_CELLS.numbers],
                       "s500_cells": [{"cell": k, "range_db": list(SNR_CELLS.bounds(k))} for k in SNR_CELLS.numbers],
                       **git_commit()},
              "decoders": {}, "paired": [], "signals": rows}
    for name in decoders:
        mine = [r for r in rows if r["decoder"] == name]
        fit_rows = [r for r in mine if r["path"] == "oracle" and r["group"] in fit_groups and r["symbols"] > 0]
        entry = {"fit_signals": len(fit_rows), "binned_cer": binned_cer(fit_rows),
                 "recall": detection_recall([r for r in mine if r["path"] == "detector" and r["group"] in detector_groups])}
        if len(fit_rows) >= MIN_SIGNALS_FOR_FIT:
            entry["fit"] = fit_decoder(fit_rows, method, resamples, key=(name, tuple(fit_groups)))
        else:
            entry["fit"] = None
        result["decoders"][name] = entry
    if reference is not None:
        result["paired"] = [paired(rows, reference, v) for v in decoders if v != reference]
    return result


def _fmt(value, interval=None, fmt="+.2f") -> str:
    if value is None:
        return "—"
    return f"{value:{fmt}}" + (f" ({interval[0]:{fmt}} to {interval[1]:{fmt}})" if interval else "")


def _crossing_text(c: dict, unit: str = "dB SNR in 500 Hz") -> str:
    """A crossing with its interval, * if below the data, or "no crossing" with its reason and the fitted CER at the
    cell's highest level; the share of resamples that cross when it is below 95% (no interval then)."""
    share = c.get("resamples_with_crossing")
    note = f"; {100 * share:.0f}% of resamples cross" if share is not None and share < 0.95 else ""
    if c["value"] is None:
        why = {"floor": "floor at or above the threshold", "above the data": "not within the data",
               "no data": "no signals"}.get(c.get("no_crossing"), "")
        at_top = (f"; fitted CER {c['cer_at_top']:.3f} at {c['top_db']:+.1f} {unit}"
                  if c.get("cer_at_top") is not None else "")
        return f"no crossing ({why}{at_top}{note})"
    return _fmt(c["value"], c["interval"]) + (" *" if c.get("outside_data") else "") + (f" ({note[2:]})" if note else "")


def _fit_warnings(fit: dict) -> list[str]:
    """Visible warnings for a main or restricted fit that did not converge (review M3): its results then come from
    the point where the optimizer stopped."""
    out = []
    for label, f in (("S500 fit", fit["s500"]["fit"]), ("E/N0 fit", fit["en0"]["fit"]),
                     ("E/N0 fit with s0 constant", fit["en0"]["s0_constant"]["fit"])):
        if not f["converged"]:
            out.append(f"**Warning: the {label} did not converge** ({f['iterations']} iterations); its values are where "
                       "the optimizer stopped.")
    if fit["en0"]["s0_constant"]["objective_increase"] < 0:
        out.append("**Warning: the fit with s0 constant has a lower objective than the full fit**: one of them is not "
                   "at its minimum.")
    return out + ([""] if out else [])


def report_markdown(a: dict) -> str:
    """The analysis as a Markdown summary (every dB with its reference)."""
    m = a["meta"]
    lines = [f"# Development set analysis: {', '.join(m['decoders'])}", "",
             f"Runs: {', '.join(m['decoders'])} (results roots {', '.join(m.get('results_dirs', []))}, relative to the suite folder); "
             f"test cases `{m.get('only')}`; fit groups {', '.join(m['fit_groups'])} (oracle); method {m['method']}; "
             f"{m['resamples']} bootstrap resamples over signals; code at {m.get('commit')}"
             + (" with uncommitted changes" if m.get("modified") else "") + ".", "",
             "S500 in dB SNR in 500 Hz; E/N0 per dit in dB re 1 (E = key-down energy in one dit of 1.2 s / WPM). "
             "Crossings: the level at which the fitted CER falls to 0.10 or 0.05, at each speed cell's center "
             "(geometric mean of its edges); 95% intervals by bootstrap over signals. * = below the fitted S500 range. "
             "No crossing: the cell's floor is at or above the threshold, or the crossing lies above the cell's "
             "highest level (not within the data).",
             ""]
    for name, entry in a["decoders"].items():
        lines += [f"## {name}", ""]
        fit = entry["fit"]
        if fit is None:
            lines += [f"No fit: {entry['fit_signals']} signals (at least {MIN_SIGNALS_FOR_FIT} needed).", ""]
        else:
            sf = fit["s500"]["fit"]
            p = sf["params"]
            lines += [f"Fit against S500 on {fit['signals']} signals: s0 = {p['a0']:.2f} {p['a1']:+.2f} x "
                      f"{p['a2']:+.2f} x^2 dB SNR in 500 Hz, ln(w / 1 dB) = {p['b0']:.3f} {p['b1']:+.3f} x {p['b2']:+.3f} x^2, "
                      f"x = ln(v / {V_REF_WPM:.2f} WPM); one floor per speed cell (table); "
                      f"stopped {sf.get('stop', 'converged')} after {sf['iterations']} iterations; failed resamples "
                      f"{fit['s500']['failed_resamples']}; CPU {fit['cpu_s']:.1f} s.", ""]
            lines += _fit_warnings(fit)
            lines += [
                      "| speed cell | center (WPM) | signals | floor (CER) | S500 at CER 0.10 (dB SNR in 500 Hz) | "
                      "S500 at CER 0.05 (dB SNR in 500 Hz) | E/N0 at CER 0.10 (dB re 1) | "
                      "ideal bound at 0.10, noncoherent (dB SNR in 500 Hz) |",
                      "|---|---|---|---|---|---|---|---|"]
            floor_iv = fit["s500"].get("floor_intervals", {})
            for c10, c05 in zip(fit["s500"]["crossings_0.10"], fit["s500"]["crossings_0.05"]):
                floor = sf.get("floors", {}).get(str(c10["cell"]))
                lines.append(f"| {c10['cell']} | {c10['wpm']:.2f} | {c10['signals']} | "
                             f"{_fmt(floor, floor_iv.get(str(c10['cell'])), '.4f')} | "
                             f"{_crossing_text(c10)} | {_crossing_text(c05)} | "
                             f"{_fmt(c10['en0_db'], None)} | {c10['ideal_noncoherent_db']:+.2f} |")
            e = fit["en0"]
            inv, wt = e["invariance"], e["w_terms"]
            lines += ["", "### Against E/N0 per dit (time-base invariance)", "",
                      f"Per-cell crossings at CER 0.10 in E/N0 per dit (dB re 1): "
                      + ", ".join(f"{c['cell']}: {_fmt(c['value'], None, '.2f') if c['value'] is not None else 'no crossing'}"
                                  for c in e["crossings_0.10"])
                      + f". Spread (largest minus smallest, over cells {', '.join(str(k) for k in e.get('spread_cells', []))}): "
                      f"{_fmt(e['spread_db'], e['spread_interval'], '.2f')} dB "
                      "of E/N0 per dit (descriptive: the spread of noisy estimates is above 0 even for an invariant decoder).",
                      "",
                      f"Test of invariance (s0's speed terms a1, a2 in the E/N0 fit = 0): a1 "
                      f"{_fmt(e['fit']['params']['a1'], inv.get('intervals', {}).get('a1'))} dB of E/N0 per dit, a2 "
                      f"{_fmt(e['fit']['params']['a2'], inv.get('intervals', {}).get('a2'))} dB of E/N0 per dit (x = ln(v / {V_REF_WPM:.2f} WPM)); Wald statistic with the "
                      f"bootstrap covariance {_fmt(inv['statistic'], None, '.2f')} (95% point {CHI2_2DOF_95:.2f}, 2 degrees "
                      f"of freedom), p = {_fmt(inv['p_value'], None, '.3g')}. The width's speed terms b1, b2: "
                      f"Wald {_fmt(wt['statistic'], None, '.2f')}, p = {_fmt(wt['p_value'], None, '.3g')}. "
                      f"The E/N0 fit stopped {e['fit'].get('stop', 'converged')}; resamples used "
                      f"{inv.get('resamples_used')} of {fit['resamples']} ({e['failed_resamples']} failed).",
                      "",
                      f"Fit with s0 constant in E/N0: crossing at CER 0.10 "
                      + ", ".join(f"{c['wpm']:.1f} WPM {_fmt(c['value'], None, '.2f')}" for c in e["s0_constant"]["crossings_0.10"])
                      + f" dB re 1; objective increase {e['s0_constant']['objective_increase']:.3g}. The genie bound: "
                      f"{e['ideal_noncoherent_db']:.2f} dB re 1 noncoherent, {e['ideal_coherent_db']:.2f} dB re 1 coherent.", ""]
        if entry["recall"]:
            lines += ["### Detection recall through the detector path (Wilson 95% intervals)", "",
                      "Detected over scored labels per speed cell and S500 cell (columns: S500 in dB SNR in 500 Hz).", "",
                      "| speed cell | " + " | ".join(f"S500 {lo:g} to {hi:g}" for lo, hi in
                                                    (SNR_CELLS.bounds(k) for k in SNR_CELLS.numbers)) + " |",
                      "|---|" + "---|" * len(SNR_CELLS)]
            for v in SPEED_CELLS.numbers:
                cells = {c["s500_cell"]: c for c in entry["recall"] if c["speed_cell"] == v}
                lines.append(f"| {v} | " + " | ".join(f"{cells[k]['detected']}/{cells[k]['labels']}" if k in cells else "—"
                                                      for k in SNR_CELLS.numbers) + " |")
            lines.append("")
    for comp in a["paired"]:
        lines += [f"## Paired: {comp['variant']} minus {comp['reference']}", "",
                  "CER difference per signal; mean with 95% bootstrap intervals over signals and over test cases.", "",
                  "Pooled (\"all\") and per speed cell: oracle-path signals only, without "
                  + ", ".join(NOT_POOLED_GROUPS) + " (copies of other groups' signals); those and the detector path "
                  "appear as groups of their own.", "",
                  "| set | signals | test cases | beyond oracle anchor | mean (signals) | mean (test cases) |",
                  "|---|---|---|---|---|---|"]
        rows = [("all (oracle path)", comp["all"])] if comp["all"] else []
        rows += sorted(comp["groups"].items())
        rows += [(f"speed cell {k}", v) for k, v in sorted(comp["speed_cells"].items(), key=lambda kv: int(kv[0])
                                                          if kv[0] != "None" else 99)]
        for label, s in rows:
            lines.append(f"| {label} | {s['signals']} | {s['test_cases']} | {s['beyond_oracle_anchor']} | "
                         f"{_fmt(s['mean'], s['interval'], '+.4f')} | {_fmt(s['mean'], s['interval_test_cases'], '+.4f')} |")
        lines.append("")
    return "\n".join(lines) + "\n"


def _relative(path: Path, root: Path) -> str:
    """path relative to root, with forward slashes; a folder outside root by its name only."""
    try:
        return Path(path).resolve().relative_to(Path(root).resolve()).as_posix()
    except ValueError:
        return Path(path).name


def write_analysis(out_dir, decoders, name: str, reference: str | None = None, results_dirs=None,
                   only: str | None = DEV2_ANALYSIS, fit_groups=FIT_GROUPS, method: str = FIT_METHOD,
                   resamples: int = BOOTSTRAP_RESAMPLES) -> Path:
    """Loads the rows, analyzes them and writes experiments/devset2-<name>.json (everything, the figures' input) and
    .md (the summary). Returns the JSON's path."""
    out_dir = Path(out_dir)
    dirs = [Path(d) for d in (results_dirs or default_results_dirs(out_dir))]
    rows = load_rows(out_dir, decoders, dirs, only)
    # paths relative to the suite folder: the summary and the captions are committed beside the results record and
    # must not name a machine's folders
    a = analyze(rows, decoders, reference, fit_groups, method, resamples,
                meta={"name": name, "results_dirs": [_relative(d, out_dir) for d in dirs], "only": only})
    path = out_dir / "experiments" / f"devset2-{name}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(a) + "\n", encoding="utf-8")
    path.with_suffix(".md").write_text(report_markdown(a), encoding="utf-8")
    return path
