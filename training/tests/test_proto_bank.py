import numpy as np
import pytest

from kz4ap_proto.bank import boxcar, branch_lengths_s, branch_samples, power_response, realized_lengths_s
from kz4ap_proto.params import ProtoConfig


def test_ladder_spans_100_to_5_wpm_in_32_steps_of_ten_percent():
    lengths = branch_lengths_s(ProtoConfig())
    assert len(lengths) == 32
    assert lengths[0] == pytest.approx(0.0096)                   # 0.8 x the 100 WPM dit
    assert lengths[-1] == pytest.approx(0.0096 * 1.1 ** 31)      # 184.3 ms
    assert np.allclose(lengths[1:] / lengths[:-1], 1.1)
    # the 5 WPM optimum, 0.8 x 240 ms, is within one step: a 0.18 dB loss of output SNR relative to the ideal length (spec 4.1, derived)
    assert 10 * np.log10(0.8 * 0.24 / lengths[-1]) == pytest.approx(0.18, abs=0.005)


def test_branch_lengths_in_samples_are_distinct_at_the_channel_rate():
    n = branch_samples(branch_lengths_s(ProtoConfig()), 1500.0)
    assert n.tolist() == [14, 16, 17, 19, 21, 23, 26, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
                          117, 129, 142, 156, 172, 189, 208, 228, 251, 276]
    assert realized_lengths_s(ProtoConfig(), 1500.0)[0] == pytest.approx(14 / 1500)


def test_boxcar_is_the_causal_running_mean():
    assert boxcar(np.arange(1, 7, dtype=complex), 3).real.tolist() == pytest.approx([1 / 3, 1, 2, 3, 4, 5])


def test_boxcar_passes_a_carrier_at_unity_gain_and_white_noise_at_one_over_n():
    rng = np.random.default_rng(1)
    u = (rng.standard_normal(200000) + 1j * rng.standard_normal(200000)) / np.sqrt(2)  # 1 FS² per sample
    v = boxcar(u, 60)[60:]
    assert np.mean(np.abs(v) ** 2) == pytest.approx(1 / 60, rel=0.05)
    assert boxcar(np.ones(100, complex), 60)[-1].real == pytest.approx(1.0)


def test_power_response_integrates_to_one_over_n():
    f = np.linspace(-750.0, 750.0, 150001)[:-1]
    assert np.mean(power_response(f, 14, 1500.0)) == pytest.approx(1 / 14, rel=1e-3)  # Parseval
    assert power_response(np.array([0.0]), 14, 1500.0)[0] == 1.0
    assert power_response(np.array([1500.0 / 14]), 14, 1500.0)[0] == pytest.approx(0.0, abs=1e-20)  # first null
