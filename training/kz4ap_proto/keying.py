"""Per-branch amplitude, log-likelihood ratio, squelch and keying (spec 4.3), and the unknown-amplitude
test for the first marks of an over (spec 4.7), for all branches at once, one block at a time."""

from __future__ import annotations

import math

import numpy as np

from .detect import envelope_llr, logistic


def hysteresis(down, up, initial) -> np.ndarray:
    """Key state (K, n): down where `down`, up where `up` (up wins), otherwise the state before;
    `initial` (K,) is the state before the first sample."""
    down = np.asarray(down, bool)
    up = np.asarray(up, bool)
    n = down.shape[1]
    event = np.where(up, -1, np.where(down, 1, 0))
    index = np.where(event != 0, np.arange(n)[None, :], -1)
    last = np.maximum.accumulate(index, axis=1)
    value = np.take_along_axis(event, np.maximum(last, 0), axis=1)
    return np.where(last >= 0, value > 0, np.asarray(initial, bool)[:, None])


def edges(key, before, n0: int) -> list[list[tuple[int, bool]]]:
    """Per branch, (sample index, key state after it) at every change of key state."""
    key = np.asarray(key, bool)
    prev = np.concatenate((np.asarray(before, bool)[:, None], key[:, :-1]), axis=1)
    rows, cols = np.nonzero(key != prev)
    out: list[list[tuple[int, bool]]] = [[] for _ in range(key.shape[0])]
    for r, c in zip(rows.tolist(), cols.tolist()):
        out[r].append((n0 + c, bool(key[r, c])))
    return out


def _log_prior_odds(cfg) -> float:
    return math.log(cfg.prior_key_down / (1.0 - cfg.prior_key_down))


class BankKeyer:
    """Amplitude, LLR keying with hysteresis and squelch per branch; the unknown-amplitude test while a
    branch is at the start of an over (`unknown`)."""

    def __init__(self, cfg, rate_hz: float, lengths_s):
        lengths_s = np.asarray(lengths_s, float)
        self.alpha = 1.0 - math.exp(-1.0 / (cfg.amplitude_tau_s * rate_hz))
        self.log_prior = _log_prior_odds(cfg)
        self.h = cfg.hysteresis_nats
        # Squelch: in noise alone a-hat^2 is a mean over about tau_a / L_k independent samples, so its spread
        # grows as sqrt(L_k) and a_min as L_k^(1/4) (milestone 2's principle with each branch's own sigma_v).
        self.a_min = cfg.squelch_a * (lengths_s / cfg.squelch_ref_s) ** cfg.squelch_exponent
        # Unknown amplitude: about 1/L_k independent envelope samples per second in noise alone, each above x
        # with probability exp(-x^2/2) (Rayleigh): x_on,k = sqrt(-2 ln(R_fa L_k)), nominally.
        # (heuristic: the envelope's upcrossings make it 7-10x more; E9 calibrates x_on_values by measurement)
        if cfg.x_on_values:
            if len(cfg.x_on_values) != len(lengths_s):
                raise ValueError("x_on_values needs one threshold per branch")
            self.x_on = np.asarray(cfg.x_on_values, float)
        else:
            self.x_on = np.sqrt(-2.0 * np.log(np.minimum(0.5, cfg.false_marks_per_s * lengths_s)))
        self.x_off = math.sqrt(-2.0 * math.log(cfg.release_probability))
        self.rekey_weight = cfg.rekey_after_s * rate_hz  # samples of keyed time (W_min)
        k = len(lengths_s)
        self.amp2 = np.zeros(k)          # s_k^2, FS^2
        self.weight = np.zeros(k)        # behind it, samples: keyed samples while unknown, p-weight after
        self.prev_amp2 = np.full(k, np.nan)
        self.unknown = np.ones(k, bool)  # the stream's start is an over's start
        self.keyed: list[list[np.ndarray]] = [[] for _ in range(k)]  # |v|^2 of keyed samples while unknown
        self.keyed_cap = max(1, int(round(4 * self.rekey_weight)))    # at most this many kept, samples
        self.key = np.zeros(k, bool)

    def step(self, P, sigma2):
        """P: (K, n) |v_k|^2, FS^2; sigma2: (K,) sigma_v,k^2, FS^2. Returns the key state (K, n), the posterior
        p (K, n; 0 where squelched), the key state before the block (K,) and a_k (K,) at the block's start."""
        P = np.asarray(P, float)
        sigma2 = np.asarray(sigma2, float)
        a = np.sqrt(self.amp2 / sigma2)
        x = np.sqrt(P / sigma2[:, None])
        g = envelope_llr(x, a[:, None]) + self.log_prior
        p = logistic(g)
        signal = (a >= self.a_min)[:, None]
        unknown = self.unknown[:, None]
        down = np.where(unknown, x > self.x_on[:, None], signal & (g > self.h))
        up = np.where(unknown, x < self.x_off, (~signal) | (g < -self.h))
        before = self.key.copy()
        key = hysteresis(down, up, before)
        self.key = key[:, -1].copy() if key.shape[1] else before
        self._update_amplitude(P, p, sigma2, key)
        return key, p * signal, before, a

    def _update_amplitude(self, P, p, sigma2, key) -> None:
        """Online EM for the Rician component (milestone 2): mean square 2 sigma^2 + s^2, p-weighted, one step
        per block: the block's weighted mean pulls s^2 by 1 - (1 - alpha)^(sum p), or by sum p / W early on.
        While a branch's amplitude is unknown (review I5) only its keyed samples count: W is their number, so
        W_min is keyed time, and s^2 is seeded as their 0.9 quantile of |v|^2 minus 2 sigma^2 (milestone 2 seeds
        from the 0.9 quantile too; a mean would be pulled down by the boxcar's ramps, which the test keys)."""
        known = ~self.unknown
        wp = np.where(known, p.sum(axis=1), 0.0)
        has = wp > 0
        mean_p = (p * P).sum(axis=1) / np.maximum(wp, 1e-300)
        self.weight += wp
        step = np.minimum(1.0, np.maximum(1.0 - (1.0 - self.alpha) ** wp, wp / np.maximum(self.weight, 1e-300)))
        target = mean_p - 2.0 * sigma2
        self.amp2 = np.where(has, np.maximum(0.0, self.amp2 + step * (target - self.amp2)), self.amp2)
        for k in np.nonzero(self.unknown & key.any(axis=1))[0]:
            self.keyed[k].append(P[k][key[k]])
            samples = np.concatenate(self.keyed[k])[-self.keyed_cap:]
            self.keyed[k] = [samples]
            self.weight[k] += float(key[k].sum())
            self.amp2[k] = max(0.0, float(np.quantile(samples, 0.9)) - 2.0 * sigma2[k])

    def ready_to_rekey(self) -> np.ndarray:
        return self.unknown & (self.weight >= self.rekey_weight)

    def start_over(self, k: int) -> None:
        """A possible new over (spec 4.7): a fresh amplitude and the unknown-amplitude test; an established
        amplitude is kept as the fallback."""
        if not self.unknown[k]:
            self.prev_amp2[k] = self.amp2[k]
        self.amp2[k] = 0.0
        self.weight[k] = 0.0
        self.keyed[k] = []
        self.unknown[k] = True

    def finish_over_start(self, k: int, amp2: float, key_now: bool) -> None:
        """After the re-keying: the winning amplitude, the full LLR (and the p-weighted EM) from now on."""
        self.amp2[k] = amp2
        self.keyed[k] = []
        self.unknown[k] = False
        self.key[k] = key_now


def rekey(P, sigma2: float, amp2: float, cfg, a_min: float) -> np.ndarray:
    """Key state over a stored stretch with the full LLR at fixed sigma^2 and s^2, from key up."""
    a = math.sqrt(max(amp2, 0.0) / sigma2)
    x = np.sqrt(np.asarray(P, float) / sigma2)[None, :]
    g = envelope_llr(x, a) + _log_prior_odds(cfg)
    signal = a >= a_min
    down = np.logical_and(signal, g > cfg.hysteresis_nats)
    up = np.logical_or(not signal, g < -cfg.hysteresis_nats)
    return hysteresis(down, up, np.zeros(1, bool))[0]
