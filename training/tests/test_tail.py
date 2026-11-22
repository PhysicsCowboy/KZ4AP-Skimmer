import math

import numpy as np
import pytest

from kz4ap_synth.generate import SignalSpec, generate, labels, plan_intervals, qso_spec, Sender
from kz4ap_synth.messages import Over
from kz4ap_synth.tail import TAIL_PROFILE_SPAN, TAIL_TAP_S, reach_s, tap_weights

FS = 8000


def _mark(tail_db=-10.0, decay_s=0.03, fd=20.0, fading_hz=0.0):
    # one dah of 0.5 s (7.2 WPM: a dit is 1/6 s), keyed from 0.1 s, VE3NEA's 2 ms centered edges
    return SignalSpec("T", 0.0, 7.2, 30.0, 0.1, edge_s=0.002, edge_placement="centered", fading_hz=fading_hz,
                      tail_db=tail_db, tail_decay_s=decay_s, tail_fading_hz=fd)


def test_tap_weights_hold_the_tail_power_over_five_decay_times():
    delays, w = tap_weights(-10.0, 0.03)
    assert w.sum() == pytest.approx(0.1, rel=1e-12)
    assert len(delays) == math.ceil(TAIL_PROFILE_SPAN * 0.03 / TAIL_TAP_S)
    assert delays[0] == pytest.approx(TAIL_TAP_S / 2) and np.all(np.diff(w) < 0)
    assert reach_s(0.03) == pytest.approx(delays[-1] + TAIL_TAP_S / 2)


def test_off_by_default_and_left_out_of_the_labels():
    spec = SignalSpec("PARIS", 500.0, 25.0, 20.0, 0.5)
    assert spec.tail_db is None
    entry = labels([spec], FS, 3.0, seed=1)["signals"][0]
    assert not any(k.startswith("tail_") for k in entry)  # existing labels files stay byte-identical
    on = labels([SignalSpec("PARIS", 500.0, 25.0, 20.0, 0.5, tail_db=-10.0)], FS, 3.0, seed=1)["signals"][0]
    assert (on["tail_db"], on["tail_decay_s"], on["tail_fading_hz"]) == (-10.0, 0.015, 8.0)


def test_the_tail_only_adds_and_leaves_the_direct_path_and_the_noise_alone():
    # the same recording with and without the tail differs by the tail only: nothing before the first key-down, nothing
    # past the last key-up plus the tail's reach, and the plan (keying) is the same
    on = SignalSpec("PARIS", 500.0, 25.0, 20.0, 0.5, fading_hz=3.0, edge_s=0.002, edge_placement="centered", tail_db=-10.0)
    off = SignalSpec("PARIS", 500.0, 25.0, 20.0, 0.5, fading_hz=3.0, edge_s=0.002, edge_placement="centered")
    assert plan_intervals([on], 3)[0].intervals == plan_intervals([off], 3)[0].intervals
    d = generate([on], FS, 3.0, seed=3) - generate([off], FS, 3.0, seed=3)
    last = 0.5 + plan_intervals([off], 3)[0].intervals[-1][1]
    t = np.arange(len(d)) / FS
    assert np.all(d[t < 0.5 - 0.001] == 0)
    assert np.all(d[t > last + 0.001 + reach_s(on.tail_decay_s) + 2 / FS] == 0)
    assert np.mean(np.abs(d[(t > 0.6) & (t < last)]) ** 2) > 0


def test_mean_power_after_a_long_mark_decays_as_the_profile():
    # after a 0.5 s mark, the tail's mean power t later is 10^(tail_db/10) exp(-t/tau) (derived; the mark itself holds 1 + 0.1)
    acc = None
    for seed in range(30):
        p = np.abs(generate([_mark()], FS, 1.2, seed=seed, add_noise=False)) ** 2
        acc = p if acc is None else acc + p
    acc /= 30
    off = 0.1 + 0.5
    mark = acc[int(0.3 * FS):int(0.55 * FS)].mean()
    for ms in (5, 10, 20, 40, 80):
        i = int(round((off + ms / 1000) * FS))
        got = acc[i - 8:i + 8].mean() / mark
        want = 0.1 * math.exp(-ms / 30) / 1.1
        assert 10 * math.log10(got / want) == pytest.approx(0.0, abs=1.0), ms


def test_the_tail_is_incoherent_with_the_direct_path():
    # E[tail x conj(direct)] = 0: over many recordings the normalized cross-correlation during the mark is near 0 (1/sqrt(n))
    cross = norm_t = norm_d = 0.0
    for seed in range(40):
        with_tail = generate([_mark()], FS, 1.2, seed=seed, add_noise=False)
        direct = generate([_mark(tail_db=None)], FS, 1.2, seed=seed, add_noise=False)
        tail = with_tail - direct
        k = slice(int(0.3 * FS), int(0.55 * FS))
        cross += np.vdot(direct[k], tail[k]); norm_t += np.vdot(tail[k], tail[k]).real; norm_d += np.vdot(direct[k], direct[k]).real
    assert abs(cross) / math.sqrt(norm_t * norm_d) < 0.1


@pytest.mark.parametrize("fd", [10.0, 40.0])
def test_the_tail_fades_at_its_own_doppler_spread(fd):
    # inside a long mark the tail is a sum of independent Gaussian taps of the same spectrum: its autocorrelation is the
    # taps' exp(-2 pi^2 sigma^2 tau^2), sigma = f_D / 2, at the lag where that is 0.5
    sigma = fd / 2
    lag_s = math.sqrt(math.log(2) / (2 * math.pi ** 2 * sigma ** 2))
    lag = int(round(lag_s * FS))
    r = p = 0.0
    for seed in range(40):
        tail = generate([_mark(fd=fd)], FS, 1.2, seed=seed, add_noise=False) - generate([_mark(tail_db=None)], FS, 1.2, seed=seed,
                                                                                       add_noise=False)
        x = tail[int(0.35 * FS):int(0.58 * FS)]  # the carrier is at 0 Hz
        r += np.vdot(x[:-lag], x[lag:]); p += np.vdot(x, x).real * (len(x) - lag) / len(x)
    assert abs(r) / p == pytest.approx(math.exp(-2 * math.pi ** 2 * sigma ** 2 * (lag / FS) ** 2), abs=0.12)


def test_a_qso_with_a_tail_is_refused():
    spec = qso_spec([Over(0, "CQ")], [Sender("K1ABC", 25.0)], 500.0, 20.0, 0.5, tail_db=-10.0)
    with pytest.raises(ValueError, match="single-sender"):
        generate([spec], FS, 3.0, seed=1)


def test_bad_tail_settings_are_refused():
    with pytest.raises(ValueError):
        generate([_mark(decay_s=0.0)], FS, 1.2, seed=1)
    with pytest.raises(ValueError):
        generate([_mark(decay_s=float("nan"))], FS, 1.2, seed=1)


# --- review of synth-tail-1 ---

import kz4ap_synth.generate as generate_module
from kz4ap_synth.fading import FADING_RATE_HZ, full_rate_hz, rayleigh_gain

PLACEMENTS = ("inside", "centered", "start")


def _mixed(tail):
    """Signals of every edge placement, with fading, drift and a QSO, the last keyed up to the recording's end (4.0 s)."""
    t = {"tail_db": -7.0} if tail else {}
    specs = [SignalSpec("PARIS PARIS", 300.0, 25.0, 10.0, 0.2, fading_hz=5.0, drift_hz_per_s=0.5, edge_s=0.004,
                        edge_placement=p, **t) for p in PLACEMENTS]
    specs.append(SignalSpec("TEST", -900.0, 30.0, 15.0, 3.2, fading_hz=2.0, fading_shape="butterworth", edge_s=0.002,
                            edge_placement="centered", **t))
    specs.append(qso_spec([Over(0, "CQ"), Over(1, "R")], [Sender("K1ABC", 25.0), Sender("K2XYZ", 28.0, offset_hz=40.0)],
                          -2000.0, 12.0, 0.4, fading_hz=1.0))
    return specs


def test_with_the_tail_made_zero_the_recording_is_bitwise_the_one_without(monkeypatch):
    # the direct path, the noise and every other signal are the same samples with the tail on: only the tail is added, for
    # every edge placement, during the marks as elsewhere, and for a signal keyed up to the recording's end
    off = generate(_mixed(False), FS, 4.0, seed=5)
    monkeypatch.setattr(generate_module, "tail_baseband", lambda envelope, n, *a, **k: np.zeros(n, dtype=np.complex128))
    zero = generate(_mixed(True), FS, 4.0, seed=5)
    assert np.array_equal(zero.view(np.uint64), off.view(np.uint64))


@pytest.mark.parametrize("placement", PLACEMENTS)
def test_the_tail_starts_with_the_direct_path_and_ends_within_its_reach(placement):
    on = SignalSpec("T", 0.0, 7.2, 20.0, 0.1, edge_s=0.004, edge_placement=placement, tail_db=-7.0)
    off = SignalSpec("T", 0.0, 7.2, 20.0, 0.1, edge_s=0.004, edge_placement=placement)
    a, b = generate([on], FS, 1.0, seed=2, add_noise=False), generate([off], FS, 1.0, seed=2, add_noise=False)
    d = a - b
    first_direct = np.argmax(np.abs(b) > 0); last_direct = len(b) - np.argmax(np.abs(b[::-1]) > 0)
    first_tail = np.argmax(np.abs(d) > 0); last_tail = len(d) - np.argmax(np.abs(d[::-1]) > 0)
    assert first_tail >= first_direct
    assert last_tail <= last_direct + int(np.ceil(reach_s(on.tail_decay_s) * FS)) + 2


def test_a_tail_cut_by_the_recordings_end_is_cut_cleanly():
    # keyed to 0.6 s in a 0.62 s recording: the tail's reach (75 ms) runs past the end and is cut there
    s = SignalSpec("T", 0.0, 7.2, 20.0, 0.1, edge_s=0.002, edge_placement="centered", tail_db=-7.0)
    x = generate([s], FS, 0.62, seed=1, add_noise=False)
    assert len(x) == int(round(0.62 * FS)) and np.all(np.isfinite(x))
    assert np.sum(np.abs(x[-int(0.01 * FS):]) ** 2) > 0


@pytest.mark.parametrize("bad", [float("nan"), float("inf"), float("-inf")])
def test_a_non_finite_tail_level_is_refused(bad):
    with pytest.raises(ValueError, match="tail_db"):
        generate([_mark(tail_db=bad)], FS, 1.2, seed=1)


def test_a_negative_tail_spread_is_refused():
    with pytest.raises(ValueError, match="tail_fading_hz"):
        generate([_mark(fd=-1.0)], FS, 1.2, seed=1)


def test_tail_spread_zero_is_a_static_tail():
    # f_D 0: each tap a constant gain, so inside a long mark the tail is constant once every tap has arrived
    tail = generate([_mark(fd=0.0)], FS, 1.2, seed=3, add_noise=False) - generate([_mark(tail_db=None)], FS, 1.2, seed=3,
                                                                                  add_noise=False)
    mid = tail[int(0.3 * FS):int(0.55 * FS)]
    assert np.abs(mid).max() > 0 and np.allclose(mid, mid[0])


def test_labels_refuse_a_qso_with_a_tail():
    spec = qso_spec([Over(0, "CQ")], [Sender("K1ABC", 25.0)], 500.0, 20.0, 0.5, tail_db=-10.0)
    with pytest.raises(ValueError, match="single-sender"):
        labels([spec], FS, 3.0, seed=1)


def test_the_default_fading_rate_is_unchanged_and_left_out_of_the_labels():
    a = rayleigh_gain(4000, 2000, 30.0, np.random.default_rng(7), "butterworth")
    b = rayleigh_gain(4000, 2000, 30.0, np.random.default_rng(7), "butterworth", FADING_RATE_HZ)
    assert np.array_equal(a, b)
    assert "fading_full_rate" not in labels([SignalSpec("E", 0.0, 20.0, 20.0, 0.5, fading_hz=30.0)], FS, 2.0, seed=1)["signals"][0]
    assert labels([SignalSpec("E", 0.0, 20.0, 20.0, 0.5, fading_hz=30.0, fading_full_rate=True)], FS, 2.0,
                  seed=1)["signals"][0]["fading_full_rate"] is True
    assert full_rate_hz(2.0) == FADING_RATE_HZ and full_rate_hz(42.0) == 420.0


def _rho_at(g, lag):
    return abs(np.vdot(g[:-lag], g[lag:])) / np.vdot(g, g).real * len(g) / (len(g) - lag)


@pytest.mark.parametrize("f_d", [7.0, 21.0, 42.0])
def test_the_full_rate_realizes_the_requested_spread(f_d):
    # a steady faded carrier (a 20 s tune-up at 0 Hz, no noise), Gaussian spectrum: |rho| is 0.5 at the lag
    # sqrt(ln 2 / (2 pi^2 sigma^2)), sigma = f_D / 2, with fading_full_rate; at 50 samples/s 42 Hz stays far above it
    fs = 2000
    sigma = f_d / 2
    lag = int(round(math.sqrt(math.log(2) / (2 * math.pi ** 2 * sigma ** 2)) * fs))
    rho = {True: [], False: []}
    for full in (True, False):
        for seed in range(4):
            s = SignalSpec("E", 0.0, 20.0, 30.0, 0.1, tune_s=20.0, fading_hz=f_d, fading_full_rate=full)
            x = generate([s], fs, 21.5, seed=seed, add_noise=False)[int(1.0 * fs):int(19.5 * fs)]
            rho[full].append(_rho_at(x, lag))
    assert np.mean(rho[True]) == pytest.approx(0.5, abs=0.07)
    if f_d == 42.0:
        assert np.mean(rho[False]) > 0.75  # the default rate realizes about 23 Hz (A.11)
