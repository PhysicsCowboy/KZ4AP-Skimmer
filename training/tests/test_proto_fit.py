import numpy as np
import pytest

from kz4ap_proto.fit import (DurationFit, Fit, class_priors, classify_mark, classify_space, observations_loglik,
                             resolution_var_s2)
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.keying import farnsworth_gap_s, timed_intervals
from kz4ap_synth.messages import random_text
from kz4ap_synth.morse import keying_intervals

CFG = ProtoConfig()


def durations(intervals):
    """(is_mark, duration s) for every mark and the space before it, in order."""
    out = []
    for i, (a, b) in enumerate(intervals):
        if i:
            out.append((False, a - intervals[i - 1][1]))
        out.append((True, b - a))
    return out


def fitted(obs, prior=(None, 0.0), var_t=1e-8):
    fit = DurationFit(CFG)
    for is_mark, d in obs:
        fit.add(is_mark, d, var_t)
    return fit.best(*prior)


def text(seed=1, words=30):
    return random_text(np.random.default_rng(seed), words)


def test_class_priors_follow_ve3nea_statistics():
    marks, spaces = class_priors()
    assert marks.tolist() == pytest.approx([0.5716, 0.4284], abs=1e-4)
    assert spaces.tolist() == pytest.approx([0.6467, 0.2379, 0.1154], abs=1e-4)


def test_resolution_variance_is_two_edges_of_l_over_a_plus_sampling():
    assert resolution_var_s2(0.04, 10.0, 1500.0) == pytest.approx(2 * 0.004 ** 2 + 2 / (12 * 1500.0 ** 2))
    assert resolution_var_s2(0.04, 0.1, 1500.0) == resolution_var_s2(0.04, 1.0, 1500.0)  # a floored at 1


def test_fits_machine_keying():
    f = fitted(durations(keying_intervals(text(), 25.0)))
    assert f.t_s == pytest.approx(0.048, rel=0.02)
    assert f.q == pytest.approx(3.0, abs=0.1)
    assert abs(f.w_s) < 0.05 * 0.048
    assert f.tg_s == pytest.approx(0.048, rel=0.05)


def test_fits_farnsworth_spacing():
    f = fitted(durations(timed_intervals(text(), 18.0, farnsworth_wpm=10.0)))
    assert f.t_s == pytest.approx(1.2 / 18.0, rel=0.02)
    assert f.tg_s == pytest.approx(farnsworth_gap_s(18.0, 10.0), rel=0.05)  # 207 ms, 3.11 T


def test_fits_key_weighting():
    f = fitted(durations(timed_intervals(text(), 25.0, "machine", None, imbalance_dits=0.2)))
    assert f.w_s == pytest.approx(0.2 * 0.048, abs=0.002)
    assert f.t_s == pytest.approx(0.048, rel=0.03)


def test_fits_heavy_dahs():
    # HandKey: dah median e^1.5 = 4.48 dits with sigma_ln 0.3 (its mean is 4.69 dits).
    rng = np.random.default_rng(2)
    f = fitted(durations(timed_intervals(text(words=60), 24.0, "hand", rng)))
    assert 4.0 <= f.q <= 5.2
    assert f.t_s == pytest.approx(0.05, rel=0.06)


def test_slow_first_dits_are_not_read_as_dahs():
    # Regression R2 (milestone-2a results 3.8): a 12 WPM station's first marks are dits. The element spaces
    # between them (T - w) and the derived class priors put the global maximum at T = 100 ms, not at the
    # dahs reading T = 33 ms (checked while planning: 99.9 ms).
    assert fitted(durations(keying_intervals("HI", 12.0))).t_s == pytest.approx(0.1, rel=0.1)


def test_the_fit_follows_a_speed_step_within_its_memory():
    # Regression R1's trigger was a mark between the old clusters; the fit has no "all alike" branch.
    before = durations(keying_intervals(text(3), 20.0))
    after = durations(keying_intervals(text(4, 40), 35.0))
    assert fitted(before + after[:72]).t_s == pytest.approx(1.2 / 35.0, rel=0.05)


def test_a_tune_up_carrier_is_an_outlier():
    obs = durations(keying_intervals(text(), 25.0))
    obs = obs[:40] + [(False, 0.5), (True, 2.0), (False, 0.5)] + obs[40:]
    assert fitted(obs).t_s == pytest.approx(0.048, rel=0.02)


def test_the_periodicity_prior_decides_an_ambiguous_start():
    two_marks = [(True, 0.1), (True, 0.1)]  # dits of 12 WPM, or dahs of 36 WPM
    assert fitted(two_marks, prior=(0.1, 1.0)).t_s == pytest.approx(0.1, rel=0.1)
    assert fitted(two_marks, prior=(1.2 / 36, 1.0)).t_s == pytest.approx(1.2 / 36, rel=0.1)


def test_quality_is_higher_for_morse_than_for_random_durations():
    morse = durations(keying_intervals(text(), 25.0))
    rng = np.random.default_rng(5)
    random = [(m, float(d)) for (m, _), d in zip(morse, np.exp(rng.uniform(np.log(0.01), np.log(1.0), len(morse))))]
    assert fitted(morse).quality > fitted(random).quality + 1.0


def test_classification_follows_the_fit():
    f = fitted(durations(keying_intervals(text(), 25.0)))
    assert not classify_mark(f, 0.048, 1e-8, CFG) and classify_mark(f, 0.144, 1e-8, CFG)
    assert [classify_space(f, d, 1e-8, CFG) for d in (0.048, 0.144, 0.336)] == ["element", "character", "word"]
    obs = [(True, 0.048, 1e-8), (False, 0.144, 1e-8)]
    assert observations_loglik(f, obs, CFG) > observations_loglik(Fit(0.1, 3.0, 0.0, 0.1, 0.0, 1.0), obs, CFG)


def model_durations(seed, words, wpm=25.0):
    """Durations drawn from the fit's own model: machine-keyed medians (T, 3T, T, 3T, 7T) with log-normal
    scatter sigma_ln (0.15 marks, 0.25 spaces), so the model's medians are the truth."""
    rng = np.random.default_rng(seed)
    iv = keying_intervals(random_text(rng, words), wpm)
    out = []
    for i, (a, b) in enumerate(iv):
        if i:
            out.append((False, (a - iv[i - 1][1]) * np.exp(CFG.sigma_ln_space * rng.standard_normal())))
        out.append((True, (b - a) * np.exp(CFG.sigma_ln_mark * rng.standard_normal())))
    return out


def test_the_fit_is_unbiased_on_model_matched_durations():
    # Review fix round 1: a least-squares refinement on raw durations estimates means, not the model's medians
    # (T biased by about +2.2%, w by -0.0105 T, from exp(sigma_ln^2 / 2) = 1.011 marks, 1.032 spaces).
    # N_mem = 1000 and about 3400 observations per seed give an effective count (sum lambda^k)^2 /
    # sum lambda^2k = 1870; T and w come mostly from dits (T + w, 0.5 x 0.5716 of them) and element spaces
    # (T - w, 0.5 x 0.6467), so sd(ln T) = sd(w / T) = 0.5 sqrt(0.15^2 / 535 + 0.25^2 / 605) = 0.0060 per
    # seed, 0.0030 for the mean of 4 seeds (derived, first order). Bounds: 1% for T (3.3 sd) and 0.009 T
    # (3 sd) for w.
    cfg = CFG.with_values(fit_memory=1000.0)
    t_err, w_rel = [], []
    for seed in range(1, 5):
        fit = DurationFit(cfg)
        for is_mark, d in model_durations(seed, 200):
            fit.add(is_mark, d, 1e-8)
        f = fit.best()
        t_err.append(f.t_s / 0.048 - 1.0)
        w_rel.append(f.w_s / 0.048)
    assert abs(np.mean(t_err)) < 0.01
    assert abs(np.mean(w_rel)) < 0.009


def test_refinement_never_lowers_the_weighted_loglik():
    cases = [(durations(keying_intervals(text(), 25.0)), (None, 0.0)),
             (durations(timed_intervals(text(words=60), 24.0, "hand", np.random.default_rng(2))), (None, 0.0)),
             (durations(keying_intervals("HI", 12.0)), (None, 0.0)),
             ([(True, 0.1), (True, 0.1)], (1.2 / 36, 1.0)),
             (durations(keying_intervals(text(3), 20.0)) + durations(keying_intervals(text(4, 40), 35.0))[:36],
              (0.04, 1.0))]
    rng = np.random.default_rng(5)
    cases += [([(m, float(d)) for (m, _), d in zip(cases[0][0], np.exp(rng.uniform(np.log(0.01), np.log(1.0),
                                                                                    len(cases[0][0]))))], (None, 0.0))]
    cases += [(model_durations(seed, 30), (None, 0.0)) for seed in range(1, 4)]
    for obs, prior in cases:
        fit = DurationFit(CFG)
        for is_mark, d in obs:
            fit.add(is_mark, d, 1e-8)
        best = fit.best(*prior)
        assert fit.weighted_loglik(best.theta(), *prior) >= fit.weighted_loglik(fit.grid_theta(*prior), *prior) - 1e-9


def test_durations_must_be_positive_and_are_clamped_to_the_outlier_range():
    f = Fit(0.048, 3.0, 0.0, 0.048, 0.0, 1.0)
    with pytest.raises(ValueError):
        observations_loglik(f, [(True, 0.0, 1e-8)], CFG)
    with pytest.raises(ValueError):
        classify_space(f, -0.1, 1e-8, CFG)
    assert observations_loglik(f, [(False, 50.0, 1e-8)], CFG) == observations_loglik(f, [(False, 10.0, 1e-8)], CFG)
    fit = DurationFit(CFG)
    fit.add(True, 0.0, 1e-8)  # ignored
    assert not fit.history


def test_copy_is_independent_and_best_needs_an_observation():
    fit = DurationFit(CFG)
    assert fit.best() is None
    fit.add(True, 0.048, 1e-8)
    other = fit.copy()
    other.add(False, 0.048, 1e-8)
    assert len(fit.history) == 1 and len(other.history) == 2
    assert fit.weight == pytest.approx(1.0) and other.weight == pytest.approx(1.0 + np.exp(-1 / 24))


def test_fast_paths_are_bit_identical_to_the_plain_formulas():
    # Task 11p: the fit's speed-ups (in-place grid arithmetic, logaddexp by parts, history arrays, shared scores in
    # best()) must give exactly the plain formulas' doubles, also after the history buffers wrap (> 2 x maxlen adds).
    import math

    import kz4ap_proto.fit as fm

    def plain_grid_loglik(fit, is_mark, d, var_t):  # Task 7's DurationFit.add, verbatim
        log_d = math.log(min(max(d, CFG.outlier_range_s[0]), CFG.outlier_range_s[1]))
        if is_mark:
            classes, priors, sigma = fit._marks, fit._mark_priors, CFG.sigma_ln_mark
        else:
            classes, priors, sigma = fit._spaces, fit._space_priors, CFG.sigma_ln_space
        total = None
        for (log_mu, inv_mu2, valid), lp in zip(classes, priors):
            s2 = sigma * sigma + var_t * inv_mu2
            z = log_d - log_mu
            ll = np.where(valid, lp - 0.5 * z * z / s2 - 0.5 * np.log(s2) - fm.LOG_SQRT_2PI, -np.inf)
            total = ll if total is None else np.logaddexp(total, ll)
        return np.logaddexp(total, fit._log_outlier)

    def bits(a):
        return np.asarray(a, float).view(np.int64)

    rng = np.random.default_rng(11)
    fit = DurationFit(CFG)
    marks = np.zeros_like(fit.mark_table)
    spaces = np.zeros_like(fit.space_table)
    n = 2 * fit.history.maxlen + 30
    for i in range(n):
        is_mark = i % 2 == 0
        d = 0.05 * float(rng.choice([1, 3] if is_mark else [1, 3, 7])) * math.exp(0.2 * rng.standard_normal())
        var_t = resolution_var_s2(float(rng.choice([0.0093, 0.04, 0.184])), float(rng.uniform(0.5, 50.0)), 1500.0)
        expected = plain_grid_loglik(fit, is_mark, d, var_t)
        assert np.array_equal(bits(fm._grid_loglik(fit, is_mark, d, var_t)), bits(expected))
        fit.add(is_mark, d, var_t)
        marks, spaces = marks * fit.lam, spaces * fit.lam
        if is_mark:
            marks = marks + expected
        else:
            spaces = spaces + expected
    assert np.array_equal(bits(fit.mark_table), bits(marks)) and np.array_equal(bits(fit.space_table), bits(spaces))

    h = list(fit.history)
    age = fit.lam ** np.arange(len(h))

    def plain_sum(theta):
        _, total, _ = class_logliks_of(theta)
        return np.sum(age * total)

    def class_logliks_of(theta):
        return fm.class_logliks(theta, [o[0] for o in h], [o[1] for o in h], [o[2] for o in h], CFG)

    for prior in ((None, 0.0), (0.05, 1.0)):
        grid = fit.grid_theta(*prior)
        plain = float(plain_sum(grid)) + fit._prior_term(float(grid[0]), *prior)
        assert fit.weighted_loglik(grid, *prior) == plain
        best = fit.best(*prior)
        r = fit._history_arrays()
        refined = fit._refine(grid, *prior, r, fit._terms(grid, r))[0]
        theta = next(c for c in (refined, grid) if (best.t_s, best.w_s, best.tg_s) == (c[0], c[1], c[3]))
        assert best.quality == float(plain_sum(theta) / np.sum(age))
