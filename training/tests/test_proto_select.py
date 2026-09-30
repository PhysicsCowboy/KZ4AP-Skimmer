import math

import numpy as np
import pytest

from kz4ap_proto.bank import realized_lengths_s
from kz4ap_proto.fit import Fit
from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.select import BranchView, Selector

CFG = ProtoConfig()
L = realized_lengths_s(CFG, 1500.0)


def fit_for(k, quality, weight=24.0, scale=1.0):
    t = L[k] / 0.8 * scale  # the dit this branch matches, times scale
    return Fit(t, 3.0, 0.0, t, quality, weight)


def views(fits=None, texts=None):
    fits, texts = fits or {}, texts or {}
    return [BranchView(k, float(L[k]), fits.get(k), texts.get(k)) for k in range(len(L))]


def test_a_branch_is_eligible_when_its_fitted_dit_matches_its_length():
    sel = Selector(CFG, L)
    assert sel.eligible(BranchView(15, L[15], fit_for(15, -1.0), None))
    assert sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, scale=1.09), None))
    assert not sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, scale=1.12), None))  # beyond one ladder step
    assert not sel.eligible(BranchView(15, L[15], fit_for(15, -1.0, weight=3.0), None))   # too little memory
    assert not sel.eligible(BranchView(15, L[15], None, None))


def test_the_best_eligible_quality_wins():
    v = views({14: fit_for(14, -1.0), 15: fit_for(15, -0.5), 20: fit_for(20, 0.0, scale=1.5)})
    assert Selector(CFG, L).best(v, None) == (15, True)  # branch 20 fits better but is not eligible


def test_quality_ties_go_to_the_likelier_text_then_to_the_longer_branch():
    sel = Selector(CFG, L)
    fits = {14: fit_for(14, -0.50), 15: fit_for(15, -0.52)}  # within 0.05 nats per element: a tie
    assert sel.best(views(fits, {14: -2.0, 15: -3.5}), None) == (14, True)
    assert sel.best(views(fits, {14: -2.0, 15: -2.05}), None) == (15, True)  # texts tie too: the longer branch
    assert sel.best(views(fits), None) == (15, True)                          # no text: the longer branch


def test_without_an_eligible_branch_text_then_periodicity_then_the_shortest():
    sel = Selector(CFG, L)
    assert sel.best(views(texts={3: -2.0, 20: -4.0}), None) == (3, False)        # the text clearly separates
    nearest = int(np.argmin(np.abs(np.log(L / (0.8 * 0.048)))))
    assert sel.best(views(texts={3: -2.0, 20: -2.5}), 0.048) == (nearest, False)  # no clear text: nearest 0.8 T_P
    assert sel.best(views(), None) == (0, False)


def test_a_switch_needs_m_instants_in_a_row():
    sel = Selector(CFG, L)  # M = 4
    v15 = views({15: fit_for(15, -0.5)})
    v16 = views({16: fit_for(16, -0.5)})
    assert [sel.update(v15, 1, t, None) for t in (1.0, 2.0, 3.0)] == [0, 0, 0]
    assert sel.update(v16, 1, 4.0, None) == 0         # the run is broken
    assert sel.update(v16, 3, 5.0, None) == 16        # three more instants in one update: four in a row
    assert sel.eligible_since[16] == 4.0 and sel.eligible_since[15] is None


def test_fallback_picks_do_not_complete_an_eligible_run():
    # Spec 4.6: the best *eligible* branch for M instants in a row. Branch 16 as a fallback pick (text only)
    # twice, then eligible twice: no switch yet with M = 4.
    sel = Selector(CFG, L)
    fallback16 = views(texts={16: -2.0, 20: -4.0})
    eligible16 = views({16: fit_for(16, -0.5)})
    assert [sel.update(fallback16, 1, t, None) for t in (1.0, 2.0)] == [0, 0]
    assert [sel.update(eligible16, 1, t, None) for t in (3.0, 4.0)] == [0, 0]
    assert sel.update(eligible16, 2, 5.0, None) == 16
