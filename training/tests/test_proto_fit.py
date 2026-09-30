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


def test_copy_is_independent_and_best_needs_an_observation():
    fit = DurationFit(CFG)
    assert fit.best() is None
    fit.add(True, 0.048, 1e-8)
    other = fit.copy()
    other.add(False, 0.048, 1e-8)
    assert len(fit.history) == 1 and len(other.history) == 2
    assert fit.weight == pytest.approx(1.0) and other.weight == pytest.approx(1.0 + np.exp(-1 / 24))
