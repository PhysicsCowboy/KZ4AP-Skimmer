import json

import numpy as np
import pytest

from kz4ap_proto.streams import baseband, load_channel


def test_baseband_puts_the_labeled_carrier_at_zero_hz_including_drift():
    rate, f_off, drift, t0 = 1500.0, 9.0, 0.5, 2.0
    t = np.arange(30000) / rate
    y = np.exp(1j * (2 * np.pi * f_off * t + np.pi * drift * np.maximum(t - t0, 0.0) ** 2 + 0.3))
    assert np.allclose(baseband(y, rate, f_off, drift, t0), np.exp(0.3j))


def test_load_channel_reads_the_stream_its_offset_and_its_label(tmp_path):
    y = (np.arange(10) + 1j).astype("<c8")
    y.tofile(tmp_path / "channel-1.c64")
    (tmp_path / "channels.json").write_text(json.dumps({
        "recording": "x.wav", "labels": "x.json", "sample_rate_hz": 1500.0, "format": "complex64",
        "channels": [{"track_id": 1, "label_index": 0, "label_freq_hz": 1009.0, "center_hz": 1000.0,
                      "first_sample_index": 0, "samples": 10, "file": "channel-1.c64"}]}))
    (tmp_path / "x.json").write_text(json.dumps({"signals": [{"text": "E", "start_s": 0.5, "drift_hz_per_s": 0.0}]}))
    ch = load_channel(tmp_path, tmp_path / "x.json", 0)
    assert (ch.label_index, ch.rate_hz, ch.f_off_hz, ch.start_s) == (0, 1500.0, 9.0, 0.5)
    assert np.allclose(ch.y, y)


def test_anchored_baseband_follows_the_detector_frequency_with_a_continuous_phase():
    from kz4ap_proto.streams import anchored_baseband
    rate, center = 1500.0, 1000.0
    n = np.arange(3000)
    f = np.where(n < 1500, 9.0, 11.0)                      # the station moves from +9 Hz to +11 Hz off center
    y = np.exp(1j * (2 * np.pi * (np.cumsum(f) - f) / rate + 0.4))  # phase before each sample's own advance
    u = anchored_baseband(y, rate, center, [(4000, center + 9.0), (5500, center + 11.0)], first_sample_index=4000)
    assert np.allclose(u, u[0])                            # no rotation left, no phase jump at the change


def test_load_detector_channel_reads_track_opening_and_anchors(tmp_path):
    from kz4ap_proto.streams import load_detector_channel
    y = (np.ones(64) + 0j).astype("<c8")
    y.tofile(tmp_path / "channel-7.c64")
    (tmp_path / "channels.json").write_text(json.dumps({
        "recording": "x.wav", "labels": "", "sample_rate_hz": 1500.0, "format": "complex64",
        "channels": [{"track_id": 7, "label_index": None, "label_freq_hz": None, "birth_freq_hz": 1011.0,
                      "center_hz": 1000.0, "first_sample_index": 3000, "open_s": 2.0, "close_s": None,
                      "samples": 64, "file": "channel-7.c64", "anchors": [[3000, 1011.0]]}]}))
    ch = load_detector_channel(tmp_path, 0)
    assert (ch.track_id, ch.label_index, ch.label, ch.birth_freq_hz, ch.open_s, ch.close_s) == (7, None, None, 1011.0, 2.0, None)
    assert ch.first_sample_index == 3000 and ch.anchors == [(3000, 1011.0)]
    # a DC input mixed down by anchor - center = 11 Hz, phase 0 at the first recorded sample
    assert np.allclose(ch.baseband(), np.exp(-2j * np.pi * 11.0 * np.arange(64) / 1500.0))
