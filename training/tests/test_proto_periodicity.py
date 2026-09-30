import numpy as np
import pytest

from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.bank import power_response
from kz4ap_proto.periodicity import Periodicity, spectrum_estimate, t_grid
from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.messages import random_text

RATE = 1500.0
OPEN = dict(comb_confidence_min=0.0, edge_confidence_min=0.0, spectrum_confidence_min=0.0)  # all confident


def keyed_p(intervals, duration_s):
    """1 while keyed, 0 otherwise, through branch 1's boxcar (14 samples, 9.3 ms), at r."""
    n = int(duration_s * RATE)
    p = np.zeros(n)
    for a, b in intervals:
        p[int(round(a * RATE)):min(n, int(round(b * RATE)))] = 1.0
    return np.convolve(p, np.ones(14) / 14)[:n]


def ten_seconds(wpm, style="machine", farnsworth=None):
    rng = np.random.default_rng(1)
    text = random_text(rng, 300)
    iv = timed_intervals(text, wpm, style, rng, farnsworth_wpm=farnsworth)
    return keyed_p(iv, iv[-1][1] + 1.0)[int(RATE):int(11 * RATE)]


@pytest.mark.parametrize("method", ["comb", "edge", "spectrum"])
@pytest.mark.parametrize("wpm,style,farnsworth", [(5.0, "machine", None), (12.0, "machine", None),
                                                  (25.0, "machine", None), (40.0, "machine", None),
                                                  (100.0, "machine", None), (25.0, "paddle", None),
                                                  (18.0, "machine", 10.0)])
def test_periodicity_finds_the_dit(method, wpm, style, farnsworth, request):
    # The edge comb runs on every case here, clean: measured 232.8 ms (0.970 T) at 5 WPM and 66.4 ms at Farnsworth
    # 18/10. (The review's edge-comb failures at 5 WPM were at S500 = 3 dB, which these tests do not cover; E1 does.)
    if method == "comb" and farnsworth == 10.0:
        # Contradicts spec 4.4's statement that Farnsworth does not disturb the comb; E1 decides among the three
        # methods with this in view, including the 2 s-window misses (7/50, at 0.19-1.38 T).
        request.applymarker(pytest.mark.xfail(strict=True, reason=(
            "measured: comb T = 204.5 ms (score 0.1464) vs true 66.7 ms, locking near the Farnsworth gap timebase "
            "T_g = 207 ms (about 3.1 T); the comb's best within +/-5% of T scores 0.1190; miss rates over seeds 1-5 x "
            "10 windows: 2/50 at 10 s, 4/50 at 5 s, 7/50 at 2 s (at 0.19-1.38 T); the spectrum fit (67.8 ms) and "
            "edge comb (66.4 ms) get it right")))
    # Checked while planning (10 s windows, with a centered 7-sample smoother at 750 samples/s rather than this
    # module's causal 14-sample boxcar averaged down to 750 samples/s): comb within 0.3% (paddle 0.7%, Farnsworth
    # 0.7%; this module's comb misses Farnsworth 18/10 on this stream, xfail above), spectrum within 1.1%
    # (Farnsworth 1.7%); the review found the comb within 5% with teeth +/-0.075 Pi wide as well.
    # Expected value T = 1.2 s / WPM (derived).
    per = Periodicity(ProtoConfig(periodicity_method=method, periodicity_windows_s=(10.0,), **OPEN), RATE)
    per.push(ten_seconds(wpm, style, farnsworth))
    t, _, window, updated = per.update(force=True)
    assert updated and window == pytest.approx(10.0)
    assert t == pytest.approx(1.2 / wpm, rel=0.05)


@pytest.mark.parametrize("method", ["comb", "spectrum"])
def test_noise_scores_below_keying(method):
    rng = np.random.default_rng(5)
    noise = np.convolve(rng.random(int(10 * RATE)) * 0.2, np.ones(14) / 14)[:int(10 * RATE)]
    scores = []
    for p in (ten_seconds(25.0), noise):
        per = Periodicity(ProtoConfig(periodicity_method=method, periodicity_windows_s=(10.0,), **OPEN), RATE)
        per.push(p)
        scores.append(per.update(force=True)[1])
    assert scores[0] > scores[1]


def test_the_shortest_full_window_is_used_and_updates_follow_the_interval():
    per = Periodicity(ProtoConfig(periodicity_windows_s=(2.0, 5.0, 10.0), **OPEN), RATE)
    p = ten_seconds(25.0)
    per.push(p[:int(1.0 * RATE)])
    assert per.update(force=True)[0] is None  # no window is full yet
    per.push(p[int(1.0 * RATE):int(1.1 * RATE)])
    assert per.update()[3] is False           # less than 0.25 s since the last update
    per.push(p[int(1.1 * RATE):])
    t, _, window, updated = per.update()
    assert updated and window == pytest.approx(2.0) and t == pytest.approx(0.048, rel=0.05)
    assert [w[0] is not None for w in per.per_window] == [True, True, True]


def test_an_unconfident_estimate_is_not_used():
    per = Periodicity(ProtoConfig(periodicity_windows_s=(10.0,), comb_confidence_min=10.0), RATE)
    per.push(ten_seconds(25.0))
    t, confidence, window, _ = per.update(force=True)
    assert t is None and window is None and confidence > 0


def boxcar_noise(n_taps, seed):
    """10 s of uniform noise through an n_taps-sample boxcar at r: a front end's own shape, no keying."""
    rng = np.random.default_rng(seed)
    return np.convolve(rng.random(int(10 * RATE)), np.ones(n_taps) / n_taps)[:int(10 * RATE)]


def boxcar_null_locks(n_taps):
    """T = (k/m) N/r, k, m = 1..3: candidates whose k-th keying null falls on the boxcar's m-th null, s (derived)."""
    return [k / m * n_taps / RATE for k in (1, 2, 3) for m in (1, 2, 3)]


def assert_no_lock(t, score, n_taps):
    assert score < ProtoConfig().spectrum_confidence_min
    assert t is None or all(abs(t / c - 1.0) > 0.03 for c in boxcar_null_locks(n_taps))


@pytest.mark.parametrize("seed", [1, 2, 3, 4])
def test_the_spectrum_fit_ignores_branch_1s_own_nulls(seed):
    # Before the fix (review; measured again for seeds 1-8): 14-sample boxcar noise locked at 13.9, 18.6-19.0 or
    # 28.0 ms with 1.22-1.51 nats (threshold 1.5 nats).
    per = Periodicity(ProtoConfig(periodicity_method="spectrum", periodicity_windows_s=(10.0,),
                                  spectrum_confidence_min=0.0), RATE)
    per.push(boxcar_noise(14, seed))
    per.update(force=True)
    assert_no_lock(per.per_window[0][0], per.per_window[0][1], 14)


@pytest.mark.parametrize("seed", [1, 2, 3, 4])
def test_the_spectrum_fit_ignores_a_longer_boxcars_nulls(seed):
    # Before the fix: 20-sample boxcar noise locked at 13.3-13.4 ms with 3.3-3.6 nats.
    cfg = ProtoConfig()
    t, score = spectrum_estimate(boxcar_noise(20, seed), RATE, t_grid(cfg), cfg.spectrum_nulls,
                                 cfg.spectrum_null_width, front_power=lambda f: power_response(f, 20, RATE),
                                 floor_db=cfg.spectrum_front_floor_db)
    assert_no_lock(t, score, 20)
