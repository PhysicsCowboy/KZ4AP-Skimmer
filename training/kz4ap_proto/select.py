"""Branch selection (spec 4.6)."""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

from .fit import Fit


@dataclass(frozen=True)
class BranchView:
    index: int                  # 0-based ladder position (branch k = index + 1); longer branches have larger indices
    length_s: float             # realized length N_k / r, s
    fit: Fit | None
    text_logprob: float | None  # mean over the branch's recent characters, nats per character


class Selector:
    def __init__(self, cfg, lengths_s):
        self.cfg = cfg
        self.lengths = np.asarray(lengths_s, float)
        self.current = 0
        self.candidate: tuple[int, bool] | None = None  # (branch, chosen among eligible branches)
        self.count = 0
        self.eligible_since: list[float | None] = [None] * len(self.lengths)

    def eligible(self, view: BranchView) -> bool:
        """The branch's own fitted dit agrees with its length: |ln(L_k / (0.8 T_k))| <= ln 1.1, once the fit's
        memory holds min_fit_weight elements."""
        f = view.fit
        return (f is not None and f.weight >= self.cfg.min_fit_weight and f.t_s > 0
                and abs(math.log(view.length_s / (self.cfg.length_dits * f.t_s))) <= self.cfg.eligibility_tolerance)

    def best(self, views, prior_t_s: float | None) -> tuple[int, bool]:
        """(branch, chosen among eligible branches)."""
        cfg = self.cfg
        eligible = [v for v in views if self.eligible(v)]
        if eligible:
            q_best = max(v.fit.quality for v in eligible)
            tied = [v for v in eligible if v.fit.quality >= q_best - cfg.quality_tie_nats]
            texts = [v.text_logprob if v.text_logprob is not None else -math.inf for v in tied]
            if len(tied) > 1 and math.isfinite(max(texts)):
                tied = [v for v, t in zip(tied, texts) if t >= max(texts) - cfg.text_tie_nats]
            return max(v.index for v in tied), True  # the longer branch (better SNR)
        texts = sorted((v.text_logprob, v.index) for v in views if v.text_logprob is not None)
        if len(texts) >= 2 and texts[-1][0] - texts[-2][0] >= cfg.text_separation_nats:
            return texts[-1][1], False
        if prior_t_s is not None:
            return int(np.argmin(np.abs(np.log(self.lengths / (cfg.length_dits * prior_t_s))))), False
        return 0, False

    def update(self, views, instants: int, t_now: float, prior_t_s: float | None) -> int:
        """One more selection (instants: how many selection instants it stands for). A switch needs the same
        other branch best for switch_persistence instants in a row, all as the best eligible branch or all as
        the fallback pick (spec 4.6: "the best eligible one for M marks in a row")."""
        for v in views:
            if self.eligible(v):
                if self.eligible_since[v.index] is None:
                    self.eligible_since[v.index] = t_now
            else:
                self.eligible_since[v.index] = None
        b, from_eligible = self.best(views, prior_t_s)
        if b == self.current:
            self.candidate, self.count = None, 0
        elif self.candidate == (b, from_eligible):
            self.count += instants
        else:
            self.candidate, self.count = (b, from_eligible), instants
        if self.candidate is not None and self.count >= self.cfg.switch_persistence:
            self.current, self.candidate, self.count = self.candidate[0], None, 0
        return self.current
