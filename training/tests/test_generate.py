import json
import wave

import numpy as np
import pytest

from kz4ap_synth.generate import (
    DEFAULT_NOISE_SIGMA,
    SignalSpec,
    amplitude_for_snr,
    generate,
    main,
    write_wav,
)
from kz4ap_synth.morse import keying_intervals


def test_noise_power_matches_sigma():
    iq = generate([], 8000, 4.0, seed=1)
    assert np.mean(np.abs(iq) ** 2) == pytest.approx(DEFAULT_NOISE_SIGMA**2, rel=0.05)


def test_tone_lands_on_its_frequency():
    fs = 48000
    iq = generate([SignalSpec("TTTTT", 1500.0, 20.0, 30.0, 0.0)], fs, 3.0, seed=1, add_noise=False)
    spectrum = np.abs(np.fft.fft(iq))
    peak_hz = np.fft.fftfreq(len(iq), 1 / fs)[np.argmax(spectrum)]
    assert peak_hz == pytest.approx(1500.0, abs=fs / len(iq))


def test_keying_follows_intervals():
    fs = 8000
    spec = SignalSpec("PARIS", 1000.0, 20.0, 20.0, 0.5)
    iq = generate([spec], fs, 5.0, seed=1, add_noise=False)
    amplitude = amplitude_for_snr(20.0, fs)
    intervals = keying_intervals("PARIS", 20.0)
    for on, off in intervals:
        mid = int((spec.start_s + (on + off) / 2) * fs)
        assert abs(iq[mid]) == pytest.approx(amplitude, rel=1e-6)
    for (_, off), (next_on, _) in zip(intervals, intervals[1:]):
        mid = int((spec.start_s + (off + next_on) / 2) * fs)
        assert abs(iq[mid]) < amplitude * 1e-6


def test_same_seed_same_output():
    spec = [SignalSpec("CQ", 500.0, 25.0, 10.0, 0.1)]
    a = generate(spec, 8000, 2.0, seed=7)
    b = generate(spec, 8000, 2.0, seed=7)
    c = generate(spec, 8000, 2.0, seed=8)
    assert np.array_equal(a, b)
    assert not np.array_equal(a, c)


def test_wav_is_16_bit_stereo_iq(tmp_path):
    iq = np.array([0.5 + 0.25j, -0.5 - 0.25j, 0.0 + 0.0j])
    path = tmp_path / "x.wav"
    write_wav(path, iq, 48000)
    with wave.open(str(path), "rb") as w:
        assert w.getnchannels() == 2
        assert w.getsampwidth() == 2
        assert w.getframerate() == 48000
        raw = np.frombuffer(w.readframes(3), dtype="<i2").reshape(3, 2)
    assert raw[0, 0] == round(0.5 * 32767)
    assert raw[0, 1] == round(0.25 * 32767)
    assert raw[1, 0] == -round(0.5 * 32767)


def test_wav_scales_down_instead_of_clipping(tmp_path):
    path = tmp_path / "loud.wav"
    write_wav(path, np.array([2.0 + 0.0j, -1.0 + 0.0j]), 48000)
    with wave.open(str(path), "rb") as w:
        raw = np.frombuffer(w.readframes(2), dtype="<i2").reshape(2, 2)
    assert raw[0, 0] == round(0.95 * 32767)
    assert raw[1, 0] == -round(0.475 * 32767)


def test_band_scenario_writes_labels(tmp_path):
    out = tmp_path / "band.wav"
    main(["--scenario", "band", "--signals", "5", "--duration", "10",
          "--sample-rate", "48000", "--seed", "3", "--out", str(out)])
    labels = json.loads(out.with_suffix(".json").read_text())
    assert labels["sample_rate"] == 48000
    assert labels["snr_bandwidth_hz"] == 500.0
    signals = labels["signals"]
    assert len(signals) == 5
    freqs = sorted(s["freq_offset_hz"] for s in signals)
    assert all(b - a >= 1000 for a, b in zip(freqs, freqs[1:]))
    for s in signals:
        assert s["text"]
        assert 0 <= s["start_s"] < s["end_s"] <= 10.0
    with wave.open(str(out), "rb") as w:
        assert w.getnframes() == 480000


def test_single_scenario_duration_too_short_errors(tmp_path):
    out = tmp_path / "short.wav"
    with pytest.raises(SystemExit):
        main(["--scenario", "single", "--duration", "5", "--out", str(out)])


def test_band_scenario_too_many_signals_errors_instead_of_hanging(tmp_path):
    out = tmp_path / "toomany.wav"
    with pytest.raises(SystemExit):
        main(["--scenario", "band", "--signals", "100", "--sample-rate", "8000", "--out", str(out)])
