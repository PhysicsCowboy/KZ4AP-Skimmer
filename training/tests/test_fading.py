import numpy as np
import pytest

from kz4ap_synth.fading import rayleigh_gain


@pytest.mark.parametrize("shape", ["gaussian", "butterworth"])
def test_mean_power_is_one(shape):
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(1), shape)
    assert np.mean(np.abs(g) ** 2) == pytest.approx(1.0, rel=0.1)


@pytest.mark.parametrize("shape", ["gaussian", "butterworth"])
def test_power_is_exponentially_distributed(shape):
    g = rayleigh_gain(600 * 200, 200, 3.0, np.random.default_rng(2), shape)
    power = np.abs(g) ** 2 / np.mean(np.abs(g) ** 2)
    # Rayleigh envelope: P(|g|^2 < 0.1 mean) = 1 - exp(-0.1) = 0.095
    assert np.mean(power < 0.1) == pytest.approx(0.095, abs=0.02)


def test_correlation_time_matches_the_spread():
    fs = 200
    f_d = 1.0
    g = rayleigh_gain(1200 * fs, fs, f_d, np.random.default_rng(3))
    sigma = f_d / 2
    # Gaussian Doppler spectrum: correlation exp(-2 pi^2 sigma^2 tau^2) is 0.5 at this lag
    tau = np.sqrt(np.log(2) / (2 * np.pi**2 * sigma**2))
    lag = int(round(tau * fs))
    rho = np.vdot(g[:-lag], g[lag:]) / np.vdot(g, g)
    assert abs(rho) == pytest.approx(0.5, abs=0.1)


@pytest.mark.parametrize("shape, expected", [("gaussian", 6.3e-5), ("butterworth", 0.0092)])
def test_power_beyond_twice_the_spread_follows_the_shape(shape, expected):
    # Gaussian, sigma = f_D/2: P(|f| > 2 f_D) = P(|z| > 4) = 6.3e-5. Butterworth, f_c = 0.625 f_D:
    # the f^-4 tail beyond 3.2 f_c holds 0.0092 of the power (VE3NEA's spectrum is not Gaussian).
    fs = 200
    g = rayleigh_gain(1200 * fs, fs, 1.0, np.random.default_rng(5), shape)
    power = np.abs(np.fft.fft(g)) ** 2
    freqs = np.fft.fftfreq(len(g), 1 / fs)
    assert power[np.abs(freqs) > 2.0].sum() / power.sum() == pytest.approx(expected, rel=0.3)


def test_same_generator_state_gives_the_same_fading():
    a = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    b = rayleigh_gain(1000, 100, 1.0, np.random.default_rng(4))
    assert np.array_equal(a, b)


def test_unknown_shape_raises():
    with pytest.raises(ValueError):
        rayleigh_gain(1000, 100, 1.0, np.random.default_rng(5), "jakes")
