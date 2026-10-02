import numpy as np
import pytest

from kz4ap_proto.bank import boxcar, branch_lengths_s, branch_samples
from kz4ap_proto.noise import BranchNoise, SpectrumNoise, ThreeTapNoise, guard_mean
from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.testsignals import lowpass
from kz4ap_synth.generate import keying_envelope
from kz4ap_synth.messages import random_text
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()
N = branch_samples(branch_lengths_s(CFG), RATE)


def white(n, seed):
    rng = np.random.default_rng(seed)
    return (rng.standard_normal(n) + 1j * rng.standard_normal(n)) / np.sqrt(2)  # 1 FS^2 per sample


def powers(u, ns):
    return np.stack([np.abs(boxcar(u, int(n))) ** 2 for n in ns]).astype(np.float32)


def averaged(est, u, P, from_s):
    """Run est block by block; the mean of sigma2() over the blocks after from_s."""
    block = int(round(CFG.block_s * RATE))
    kept = []
    for n0 in range(0, len(u), block):
        n1 = min(n0 + block, len(u))
        est.update(u, P, n0, n1)
        if n1 / RATE > from_s:
            kept.append(est.sigma2())
    return np.mean(kept, axis=0)


def true_sigma2(h, ns):
    """sigma_v,k^2 per real component for unit-power white noise through h, then the n-sample boxcar (derived)."""
    return np.array([0.5 * np.sum(np.convolve(h, np.ones(n) / n) ** 2) for n in ns])


def test_guard_mean_is_the_truncated_exponential_mean():
    assert guard_mean(1.75) == pytest.approx(0.632, abs=1e-3)


def test_three_tap_estimate_is_unbiased_in_white_noise():
    # 60 s averaged over the last 50 s: branch 32 (184 ms) has about 270 independent |v|^2 samples there, so its
    # scatter is about 6% (derived, rough); 15% leaves room for it without hiding a bias of the guard's size.
    ns = N[[0, 15, 31]]
    u = white(int(60 * RATE), 1)
    est = BranchNoise(CFG, RATE, ns)
    assert averaged(est, u, powers(u, ns), 10.0) == pytest.approx(0.5 / ns, rel=0.15)


def test_spectrum_gives_one_over_n_in_white_noise():
    u = white(int(60 * RATE), 2)
    est = SpectrumNoise(CFG, RATE, N)
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(0.5 / N, rel=0.1)


def test_spectrum_level_arm_corrects_the_mask_bias_in_white_noise():
    u = white(int(60 * RATE), 7)
    est = SpectrumNoise(CFG, RATE, N, level="spectrum")
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(0.5 / N, rel=0.1)


def test_the_mask_keeps_most_noise_and_its_whole_band_power_reads_low():
    # White noise: the mask keeps about 2/3 of noise-only samples (Task 5: 67.2%, seed 8), whose whole-band power
    # reads 0.9745 of the truth (Task 5); per branch, where the low frequencies weigh most, it reads 16-21% low
    # (params.mask_bias).
    u = white(int(60 * RATE), 8)
    est = SpectrumNoise(CFG, RATE, N)
    averaged(est, u, powers(u, N), 0.0)
    kept = est.kept_fraction_sum / est.segments_offered
    bias = est.masked_power_sum / est.segments  # true power per sample is 1 FS^2
    print(f"kept fraction {kept:.3f}, accepted segments {est.segments}/{est.segments_offered}, mask bias {bias:.4f}")
    assert 0.5 < kept < 0.8 and 0.95 < bias < 1.0


def test_spectrum_follows_channel_shaped_noise():
    h = lowpass()
    u = np.convolve(white(int(30 * RATE), 3), h, mode="same")
    truth = true_sigma2(h, N)
    assert truth[0] > 2 * 0.5 * np.sum(h ** 2) / N[0]  # the white-noise formula would be off by more than 2x
    est = SpectrumNoise(CFG, RATE, N)
    assert averaged(est, u, powers(u, N), 10.0) == pytest.approx(truth, rel=0.15)


def keyed_stream(lead_in_s, duration_s):
    """A 25 WPM station at 10 FS (S500 about 25 dB) starting lead_in_s into channel-shaped noise."""
    h = lowpass()
    n = int(duration_s * RATE)
    iv = keying_intervals(random_text(np.random.default_rng(4), 60), 25.0)
    carrier = 10.0 * keying_envelope(iv, lead_in_s, n, int(RATE))
    return carrier + np.convolve(white(n, 5), h, mode="same"), true_sigma2(h, N)


def test_spectrum_keeps_marks_out_when_a_station_starts_after_the_warm_up():
    # 1.0 s of noise alone first (about 3x the 0.32 s three-tap warm-up), then 12 s of keying; averaged over the
    # last 5 s, i.e. from 7 s after the station starts.
    u, truth = keyed_stream(1.0, 13.0)
    P = powers(u, N)
    assert averaged(SpectrumNoise(CFG, RATE, N), u, P, 8.0) == pytest.approx(truth, rel=0.3)
    assert averaged(SpectrumNoise(CFG, RATE, N, level="spectrum"), u, P, 8.0) == pytest.approx(truth, rel=0.3)


def test_the_three_tap_level_decays_slowly_when_a_station_keys_from_the_first_sample():
    # Pins a known limitation, not a goal: with a station keying from sample 0 the milestone-2 three-tap estimate
    # starts at about 6.6x the true level (its warm-up takes the 20% quantile of |v_1|^2 over keyed signal) and
    # decays slowly, so variant (a)'s branch 1 still reads 1.331x the truth averaged over 7-12 s (measured, Task 5).
    # Why it decays so slowly is not established: a linearized relaxation from the warm-up gives about 5.7 s, the
    # measured decay takes about 18 s; the running-mean phase of the update and ramp leakage past the guard may
    # contribute. E10 compares this against variant (b).
    u, truth = keyed_stream(0.0, 12.0)
    est = SpectrumNoise(CFG, RATE, N)
    ratio = averaged(est, u, powers(u, N), 7.0)[0] / truth[0]
    print(f"branch 1 variant (a) / truth, keyed from sample 0, 7-12 s: {ratio:.3f}")
    assert 1.28 < ratio < 1.38


def test_the_per_branch_mask_bias_table_is_its_white_noise_measurement():
    # Re-measures params.mask_bias: white noise, 1 FS^2, seeds 101-110, 60 s each.
    num = np.zeros(len(N))
    segments = 0
    for seed in range(101, 111):
        u = white(int(60 * RATE), seed)
        est = SpectrumNoise(CFG, RATE, N)
        averaged(est, u, powers(u, N), 0.0)
        num += est.masked_branch_power() * est.segments
        segments += est.segments
    measured = num / segments / (0.5 / N)
    table = ", ".join(f"{x:.4f}" for x in measured)
    assert measured == pytest.approx(np.array(CFG.mask_bias), abs=1e-4), f"re-measured mask_bias: ({table})"


def test_the_fallback_estimates_each_branch_on_its_own():
    h = lowpass()
    u = np.convolve(white(int(40 * RATE), 6), h, mode="same")
    ns = N[[0, 15, 31]]
    assert averaged(BranchNoise(CFG, RATE, ns), u, powers(u, ns), 10.0) == pytest.approx(true_sigma2(h, ns), rel=0.15)
