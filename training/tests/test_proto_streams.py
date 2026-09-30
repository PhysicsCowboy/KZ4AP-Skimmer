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
