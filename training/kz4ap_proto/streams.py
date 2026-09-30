"""Recorded oracle channel streams (kz4ap-bench --record-channels) and the oracle re-centering."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np


def baseband(y, rate_hz: float, f_off_hz: float, drift_hz_per_s: float = 0.0, start_s: float = 0.0,
             first_sample_index: int = 0) -> np.ndarray:
    """u[n]: y mixed down so the labeled carrier sits at 0 Hz, y * exp(-j(2 pi f_off t + pi fdot (t - t0)^2))
    with the drift from t0 on (the generator's phase law, docs/signal-processing.md section 11 "Drift"),
    t = (first index + n) / r."""
    t = (first_sample_index + np.arange(len(y))) / rate_hz
    phase = 2.0 * np.pi * f_off_hz * t
    if drift_hz_per_s:
        phase = phase + np.pi * drift_hz_per_s * np.maximum(t - start_s, 0.0) ** 2
    return np.asarray(y, np.complex128) * np.exp(-1j * phase)


@dataclass
class ChannelStream:
    label_index: int
    rate_hz: float            # r, samples/s
    y: np.ndarray             # channelizer output, FS (complex128)
    first_sample_index: int
    f_off_hz: float           # labeled carrier minus the channel's center, Hz
    drift_hz_per_s: float     # the label's drift, Hz/s
    start_s: float            # the label's start, the drift's reference, s
    label: dict

    def baseband(self) -> np.ndarray:
        return baseband(self.y, self.rate_hz, self.f_off_hz, self.drift_hz_per_s, self.start_s,
                        self.first_sample_index)


def read_manifest(record_dir) -> dict:
    return json.loads((Path(record_dir) / "channels.json").read_text())


def load_channel(record_dir, labels_path, position: int) -> ChannelStream:
    """The position-th channel of a recording directory, with its label from labels_path."""
    record_dir = Path(record_dir)
    manifest = read_manifest(record_dir)
    ch = manifest["channels"][position]
    label = json.loads(Path(labels_path).read_text())["signals"][ch["label_index"]]
    y = np.fromfile(record_dir / ch["file"], dtype="<c8").astype(np.complex128)
    if len(y) != ch["samples"]:
        raise ValueError(f"{ch['file']}: {len(y)} samples, the manifest says {ch['samples']}")
    return ChannelStream(int(ch["label_index"]), float(manifest["sample_rate_hz"]), y, int(ch["first_sample_index"]),
                         float(ch["label_freq_hz"]) - float(ch["center_hz"]),
                         float(label.get("drift_hz_per_s") or 0.0), float(label["start_s"]), label)
