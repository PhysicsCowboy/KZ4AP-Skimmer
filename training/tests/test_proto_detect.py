import numpy as np
import pytest

from kz4ap_proto.detect import envelope_llr, log_bessel_i0, logistic


def test_log_bessel_i0_matches_numpy():
    z = np.linspace(0.0, 50.0, 501)
    assert np.max(np.abs(log_bessel_i0(z) - np.log(np.i0(z)))) < 1e-6  # A&S: relative error in I0 below 5e-7


def test_llr_is_zero_without_amplitude_and_grows_with_the_envelope():
    assert envelope_llr(np.array([0.5, 3.0]), 0.0).tolist() == pytest.approx([0.0, 0.0])
    llr = envelope_llr(np.array([[0.5, 3.0, 6.0]]), np.array([[4.0]]))
    assert llr[0, 0] < 0 < llr[0, 2] and llr[0, 1] < llr[0, 2]
    assert logistic(np.array([0.0, 100.0]))[1] == pytest.approx(1.0) and logistic(np.array([0.0]))[0] == 0.5
