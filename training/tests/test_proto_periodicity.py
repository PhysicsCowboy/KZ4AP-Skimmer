import numpy as np
import pytest

from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.periodicity import Periodicity
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
    if method == "edge" and (wpm == 5.0 or farnsworth):
        pytest.skip("the edge comb is checked only where the review found it working (E1 measures the rest)")
    if method == "comb" and farnsworth == 10.0:
        # Contradicts spec 4.4's statement that Farnsworth does not disturb the comb; E1 decides among the three
        # methods with this in view.
        request.applymarker(pytest.mark.xfail(strict=True, reason=(
            "measured: comb T = 204.5 ms (score 0.1464) vs true 66.7 ms, locking near the Farnsworth gap timebase "
            "T_g = 207 ms (about 3.1 T); the comb's best within +/-5% of T scores 0.1190; miss rates over seeds 1-5 x "
            "10 windows: 2/50 at 10 s, 4/50 at 5 s; the spectrum fit (67.8 ms) and edge comb (66.4 ms) get it right")))
    # Checked while planning (10 s windows): comb within 0.3% (paddle 0.7%, Farnsworth 0.7%), spectrum
    # within 1.1% (Farnsworth 1.7%); the review found the comb within 5% with teeth +/-0.075 Pi wide as well.
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
