import json
import wave

import numpy as np
import pytest

from kz4ap_synth.generate import (
    DEFAULT_NOISE_SIGMA,
    TUNE_GAP_S,
    SignalSpec,
    amplitude_for_snr,
    generate,
    keying_envelope,
    labels,
    main,
    plan_intervals,
    scenario_band,
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


def _milestone1_generate(signals, sample_rate, duration_s, seed):
    """Frozen copy of the milestone-1 generator, to prove the defaults did not change."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    iq += (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * (DEFAULT_NOISE_SIGMA / np.sqrt(2))
    for s in signals:
        phase = rng.uniform(0, 2 * np.pi)
        intervals = keying_intervals(s.text, s.wpm)
        if not intervals:
            continue
        i0 = max(0, int(s.start_s * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1]) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate)
        t = np.arange(i0, i1) / sample_rate
        carrier = np.exp(1j * (2 * np.pi * s.freq_offset_hz * t + phase))
        iq[i0:i1] += amplitude_for_snr(s.snr_db, sample_rate) * env * carrier
    return iq


def test_default_signals_match_milestone_1_generator():
    specs = scenario_band(np.random.default_rng(3), 6, 20.0, 48000)
    new = generate(specs, 48000, 20.0, seed=4)
    old = _milestone1_generate(specs, 48000, 20.0, seed=4)
    assert np.array_equal(new, old)


def test_repeats_send_the_text_again_after_each_pause():
    spec = SignalSpec("TEST", 1000.0, 20.0, 20.0, 1.0, repeats=3, pause_s=2.0)
    plan = plan_intervals([spec], seed=1)[0]
    single = keying_intervals("TEST", 20.0)
    assert len(plan.intervals) == 3 * len(single)
    assert len(plan.transmissions) == 3
    first, second, third = plan.transmissions
    assert first == pytest.approx((0.0, single[-1][1]))
    assert second[0] == pytest.approx(first[1] + 2.0)
    assert third[0] == pytest.approx(second[1] + 2.0)


def test_labels_list_transmissions_and_the_full_reference_text():
    spec = SignalSpec("CQ K1ABC", 1000.0, 25.0, 20.0, 0.5, repeats=2, pause_s=3.0)
    entry = labels([spec], 8000, 20.0, seed=1)["signals"][0]
    assert entry["text"] == "CQ K1ABC CQ K1ABC"
    assert [t["text"] for t in entry["transmissions"]] == ["CQ K1ABC", "CQ K1ABC"]
    assert entry["transmissions"][0]["start_s"] == pytest.approx(0.5)
    assert entry["transmissions"][1]["start_s"] == pytest.approx(
        entry["transmissions"][0]["end_s"] + 3.0, abs=2e-3)
    assert entry["end_s"] == pytest.approx(entry["transmissions"][1]["end_s"], abs=1e-3)


def test_tune_up_carrier_precedes_the_keying():
    fs = 8000
    spec = SignalSpec("E", 500.0, 20.0, 20.0, 1.0, tune_s=1.0)
    plan = plan_intervals([spec], seed=1)[0]
    assert plan.intervals[0] == (0.0, 1.0)
    assert plan.intervals[1][0] == pytest.approx(1.0 + TUNE_GAP_S)
    assert plan.transmissions == [plan.intervals[1]]
    iq = generate([spec], fs, 4.0, seed=1, add_noise=False)
    assert abs(iq[int(1.5 * fs)]) == pytest.approx(amplitude_for_snr(20.0, fs), rel=1e-6)  # mid-carrier
    assert abs(iq[int(2.25 * fs)]) < 1e-9                                                 # the gap


def test_drift_moves_the_carrier_linearly():
    fs = 8000
    spec = SignalSpec("E", 100.0, 20.0, 20.0, 0.5, tune_s=4.0, drift_hz_per_s=5.0)
    iq = generate([spec], fs, 6.0, seed=1, add_noise=False)

    def freq_at(t_s):
        i = int(t_s * fs)
        return np.angle(iq[i + 1] * np.conj(iq[i])) * fs / (2 * np.pi)

    assert freq_at(1.5) == pytest.approx(100.0 + 5.0 * 1.0, abs=0.1)
    assert freq_at(3.5) == pytest.approx(100.0 + 5.0 * 3.0, abs=0.1)


def test_signal_can_start_at_the_first_sample():
    fs = 8000
    spec = SignalSpec("T", 500.0, 20.0, 20.0, 0.0)
    iq = generate([spec], fs, 2.0, seed=1, add_noise=False)
    assert abs(iq[0]) > 0.0
    assert abs(iq[int(0.05 * fs)]) == pytest.approx(amplitude_for_snr(20.0, fs), rel=1e-6)
    assert labels([spec], fs, 2.0, seed=1)["signals"][0]["transmissions"][0]["start_s"] == 0.0
