import math

import numpy as np
import pytest

from kz4ap_proto.bank import boxcar
from kz4ap_proto.keying import BankKeyer, edges, hysteresis, rekey
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()


def rectangles(intervals, n, start_s):
    env = np.zeros(n)
    for a, b in intervals:
        env[int(round((a + start_s) * RATE)):int(round((b + start_s) * RATE))] = 1.0
    return env


def noise(n, power, seed):
    rng = np.random.default_rng(seed)
    return (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)


def marks_of(key, before=False):
    out, down = [], None
    for i, d in edges(key[None, :], np.array([before]), 0)[0]:
        if d:
            down = i
        elif down is not None:
            out.append((down, i))
            down = None
    return out


def test_hysteresis_holds_between_the_thresholds():
    down = np.array([[0, 1, 0, 0, 0, 0]], bool)
    up = np.array([[0, 0, 0, 1, 0, 0]], bool)
    assert hysteresis(down, up, np.array([False])).tolist() == [[False, True, True, False, False, False]]
    assert hysteresis(down & False, up & False, np.array([True])).tolist() == [[True] * 6]


def test_edges_list_every_change_of_state():
    key = np.array([[False, True, True, False]])
    assert edges(key, np.array([True]), 100) == [[(100, False), (101, True), (103, False)]]


def test_unknown_amplitude_thresholds_follow_the_rayleigh_tail():
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE, 276 / RATE]))
    assert keyer.x_on[0] == pytest.approx(math.sqrt(-2 * math.log(0.01 * 14 / RATE)))  # 4.31
    assert keyer.x_on[1] == pytest.approx(math.sqrt(-2 * math.log(0.01 * 276 / RATE)))  # 3.54
    assert keyer.x_off == pytest.approx(1.5518, abs=1e-4)
    assert keyer.a_min[0] == pytest.approx(3 * (14 / RATE / 0.016) ** 0.25)


def test_marks_through_a_matched_branch_keep_their_length():
    # At high SNR the +/-1 nat crossings sit at x = a/2 + O(ln a / a): half the amplitude, where the boxcar's
    # ramps are L apart, so a rectangular mark d >= L keeps its length (derived).
    n_box = 60  # 40 ms, 0.83 of the 25 WPM dit
    iv = keying_intervals("PARIS PARIS", 25.0)
    n = int(round((iv[-1][1] + 1.0) * RATE))
    u = rectangles(iv, n, 0.5) + noise(n, 1e-4, 1)
    P = np.abs(boxcar(u, n_box)) ** 2
    sigma2 = 0.5 * 1e-4 / n_box
    keyer = BankKeyer(CFG, RATE, np.array([n_box / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = 1.0
    key, _, _, _ = keyer.step(P[None, :], np.array([sigma2]))
    measured = [(b - a) / RATE for a, b in marks_of(key[0])]
    truth = [b - a for a, b in iv]
    assert len(measured) == len(truth) == 28
    assert measured == pytest.approx(truth, abs=2.5 / RATE)


def test_the_squelch_keeps_noise_out_without_an_amplitude():
    n = int(20 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 2), 14)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE]))
    keyer.unknown[:] = False
    key, p, _, _ = keyer.step(P, np.array([0.5 / 14]))
    assert not key.any() and not p.any()


def test_calibrated_thresholds_replace_the_nominal_ones():
    keyer = BankKeyer(CFG.with_values(x_on_values=[5.0, 4.0]), RATE, np.array([14 / RATE, 276 / RATE]))
    assert keyer.x_on.tolist() == [5.0, 4.0]
    with pytest.raises(ValueError):
        BankKeyer(CFG.with_values(x_on_values=[5.0]), RATE, np.array([14 / RATE, 276 / RATE]))


def test_an_unknown_amplitude_counts_only_keyed_samples():
    # Review I5: with a reset amplitude p = 0.44 on every sample, so a p-weight fills whatever the key does.
    # Noise alone: (almost) nothing is keyed, so the re-key weight stays near 0 after 10 s.
    n = int(10 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 12), 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    keyer.step(P, np.array([0.5 / 60]))
    assert keyer.weight[0] < 0.05 * RATE and not keyer.ready_to_rekey()[0]
    # A keyed 1 FS carrier: the weight is the keyed time and the amplitude is seeded from the keyed samples
    # (T and S's dah and dits give flat tops on more than 10% of the keyed samples through a 40 ms boxcar).
    iv = keying_intervals("TEST TEST", 25.0)
    n = int(4 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 13)
    P = (np.abs(boxcar(u, 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 * 1e-3 / 60]))
    assert keyer.weight[0] == pytest.approx(key.sum())
    assert keyer.amp2[0] == pytest.approx(1.0, rel=0.1)  # the 0.9 quantile sits on the marks' flat tops
    assert keyer.ready_to_rekey()[0] == (key.sum() >= 0.4 * RATE)


def test_unknown_amplitude_test_keys_noise_rarely():
    # Noise alone with the nominal threshold: the independent-sample argument says R_fa = 0.01 /s (2 in 200 s), but
    # the envelope's upcrossings make it 7-10x more (review: 0.070 /s at 9.3 ms, 9-18 here over seeds). E9
    # calibrates x_on by measurement; this test only bounds the nominal rate and records it.
    n = int(200 * RATE)
    P = (np.abs(boxcar(noise(n, 1.0, 3), 14)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 / 14]))
    count = len(marks_of(key[0]))
    print(f"false key-downs in 200 s of noise: {count}")  # recorded in the Task 13 results as the measured rate
    assert count <= 20


def test_unknown_amplitude_test_keys_a_strong_station_at_once():
    iv = keying_intervals("TEST", 25.0)
    n = int(3 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 4)
    P = (np.abs(boxcar(u, 60)) ** 2)[None, :]
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    key, _, _, _ = keyer.step(P, np.array([0.5 * 1e-3 / 60]))
    first = marks_of(key[0])[0]
    assert first[0] / RATE == pytest.approx(0.5, abs=62 / RATE)  # within the boxcar's length of the true start


def test_rekey_matches_the_full_llr_keying():
    iv = keying_intervals("TEST", 25.0)
    n = int(3 * RATE)
    u = rectangles(iv, n, 0.5) + noise(n, 1e-3, 5)
    P = np.abs(boxcar(u, 60)) ** 2
    sigma2 = 0.5 * 1e-3 / 60
    keyer = BankKeyer(CFG, RATE, np.array([60 / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = 1.0
    key, _, _, _ = keyer.step(P[None, :], np.array([sigma2]))
    assert np.array_equal(rekey(P, sigma2, 1.0, CFG, keyer.a_min[0]), key[0])
    assert not rekey(P, sigma2, 0.0, CFG, keyer.a_min[0]).any()  # no amplitude: squelched


def test_start_over_keeps_the_established_amplitude_as_fallback():
    keyer = BankKeyer(CFG, RATE, np.array([14 / RATE, 60 / RATE]))
    keyer.unknown[:] = False
    keyer.amp2[:] = [2.0, 3.0]
    keyer.weight[:] = 100.0
    keyer.start_over(1)
    assert keyer.unknown.tolist() == [False, True]
    assert keyer.amp2[1] == 0.0 and keyer.weight[1] == 0.0 and keyer.prev_amp2[1] == 3.0
    assert math.isnan(keyer.prev_amp2[0])
    keyer.finish_over_start(1, 2.5, True)
    assert not keyer.unknown[1] and keyer.amp2[1] == 2.5 and keyer.key[1]
