"""Recorded channel streams (kz4ap-bench --record-channels): oracle channels with the oracle re-centering, and
the channels the detector opens, mixed by the detector's frequency for the track."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np


def baseband(y, rate_hz: float, f_off_hz: float, drift_hz_per_s: float = 0.0, start_s: float = 0.0,
             first_sample_index: int = 0) -> np.ndarray:
    """u[n]: y mixed down so the labeled carrier sits at 0 Hz, y * exp(-j(2 pi f_off t + pi fdot (t - t0)^2))
    with the drift from t0 on (the generator's phase law, docs/signal-processing.md section 11 "Drift"),
    t = (first index + n) / r. The channelizer's group delay is not removed from t, so the drift term's time origin
    lags the generator's by it: about 0.1 Hz of frequency error at 1 Hz/s (estimated from the ledger's Task 4
    review, not measured)."""
    t = (first_sample_index + np.arange(len(y))) / rate_hz
    phase = 2.0 * np.pi * f_off_hz * t
    if drift_hz_per_s:
        phase = phase + np.pi * drift_hz_per_s * np.maximum(t - start_s, 0.0) ** 2
    return np.asarray(y, np.complex128) * np.exp(-1j * phase)


def anchored_baseband(y, rate_hz: float, center_hz: float, anchors, first_sample_index: int = 0) -> np.ndarray:
    """u[n]: y mixed down by the detector's frequency for the track minus the channel's center, as the engine gave
    it block by block (anchors: [(first sample index, Hz from the span's center)], in order), with a continuous
    phase across changes. This is the anchor the Matched path's NCO starts from and follows (option 1), without
    the tracker's fine-tuning within +/-12 Hz of it (the tracker is out of scope for stage 1)."""
    index = first_sample_index + np.arange(len(y))
    starts = np.array([a[0] for a in anchors], dtype=np.int64)
    offsets = np.array([a[1] for a in anchors], float) - center_hz
    offset = offsets[np.maximum(np.searchsorted(starts, index, side="right") - 1, 0)]
    phase = 2.0 * np.pi * (np.cumsum(offset) - offset) / rate_hz  # the phase before each sample's own advance
    return np.asarray(y, np.complex128) * np.exp(-1j * phase)


@dataclass
class ChannelStream:
    label_index: int | None
    rate_hz: float            # r, samples/s
    y: np.ndarray             # channelizer output, FS (complex128)
    first_sample_index: int
    f_off_hz: float           # labeled carrier minus the channel's center, Hz
    drift_hz_per_s: float     # the label's drift, Hz/s
    start_s: float            # the label's start, the drift's reference, s
    label: dict | None
    track_id: int | None = None           # detector channels: the track
    center_hz: float | None = None        # the channel's center, Hz from the span's center
    birth_freq_hz: float | None = None    # the detector's frequency at the track's birth, Hz
    open_s: float | None = None           # when the channel opened, s
    close_s: float | None = None          # when its track died, s (None: open to the end)
    anchors: list | None = None           # [(first sample index, Hz)]: the detector's frequency by block

    def baseband(self) -> np.ndarray:
        if self.label is None:  # a detector channel: no label, mixed by the detector's frequency
            return anchored_baseband(self.y, self.rate_hz, self.center_hz, self.anchors, self.first_sample_index)
        return baseband(self.y, self.rate_hz, self.f_off_hz, self.drift_hz_per_s, self.start_s,
                        self.first_sample_index)


def read_manifest(record_dir) -> dict:
    return json.loads((Path(record_dir) / "channels.json").read_text(encoding="utf-8"))


def load_channel(record_dir, labels_path, position: int) -> ChannelStream:
    """The position-th channel of a recording directory, with its label from labels_path."""
    record_dir = Path(record_dir)
    manifest = read_manifest(record_dir)
    ch = manifest["channels"][position]
    label = json.loads(Path(labels_path).read_text(encoding="utf-8"))["signals"][ch["label_index"]]
    y = np.fromfile(record_dir / ch["file"], dtype="<c8").astype(np.complex128)
    if len(y) != ch["samples"]:
        raise ValueError(f"{ch['file']}: {len(y)} samples, the manifest says {ch['samples']}")
    return ChannelStream(int(ch["label_index"]), float(manifest["sample_rate_hz"]), y, int(ch["first_sample_index"]),
                         float(ch["label_freq_hz"]) - float(ch["center_hz"]),
                         float(label.get("drift_hz_per_s") or 0.0), float(label["start_s"]), label)


def load_detector_channel(record_dir, position: int) -> ChannelStream:
    """The position-th channel of a detector-path recording directory (kz4ap-bench --record-channels without
    --oracle): no label; it starts where the channel opened."""
    manifest = read_manifest(record_dir)
    return detector_channel(record_dir, float(manifest["sample_rate_hz"]), manifest["channels"][position])


def detector_channel(record_dir, rate_hz: float, ch: dict) -> ChannelStream:
    """load_detector_channel from a manifest already parsed: rate_hz is its sample_rate_hz, ch one of its channels
    (parsing channels.json once per channel costs time quadratic in the channel count on a crowded recording, so a
    runner parses it once per recording)."""
    record_dir = Path(record_dir)
    y = np.fromfile(record_dir / ch["file"], dtype="<c8").astype(np.complex128)
    if len(y) != ch["samples"]:
        raise ValueError(f"{ch['file']}: {len(y)} samples, the manifest says {ch['samples']}")
    anchors = [(int(i), float(f)) for i, f in ch["anchors"]] or [(int(ch["first_sample_index"]), float(ch["birth_freq_hz"]))]
    return ChannelStream(None, float(rate_hz), y, int(ch["first_sample_index"]), 0.0, 0.0, 0.0,
                         None, track_id=int(ch["track_id"]), center_hz=float(ch["center_hz"] or 0.0),
                         birth_freq_hz=float(ch["birth_freq_hz"]), open_s=float(ch["open_s"]),
                         close_s=None if ch["close_s"] is None else float(ch["close_s"]), anchors=anchors)
