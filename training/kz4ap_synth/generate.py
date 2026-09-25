"""Synthetic CW I/Q recordings with known answers, for testing and benchmarking.

Writes a 16-bit stereo WAV file (left = I, right = Q) and, next to it, a JSON
labels file describing every signal in it.
"""

from __future__ import annotations

import argparse
import json
import wave
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from .morse import keying_intervals

SNR_BANDWIDTH_HZ = 500.0
DEFAULT_NOISE_SIGMA = 0.02
RISE_S = 0.005

CALL_PREFIXES = ["K", "W", "N", "AA", "KB", "DL", "G", "F", "JA", "VE",
                 "EA", "I", "OH", "SM", "UA", "PY", "VK", "ZL"]
MESSAGES = ["CQ TEST {c} {c}", "CQ CQ DE {c} {c} K", "TU {c}", "{c} 5NN 14", "CQ {c} {c} TEST"]


@dataclass
class SignalSpec:
    text: str
    freq_offset_hz: float
    wpm: float
    snr_db: float
    start_s: float


def amplitude_for_snr(snr_db: float, sample_rate: int) -> float:
    """Carrier amplitude giving snr_db against DEFAULT_NOISE_SIGMA noise, measured in 500 Hz."""
    noise_in_band = DEFAULT_NOISE_SIGMA**2 * SNR_BANDWIDTH_HZ / sample_rate
    return float(np.sqrt(10 ** (snr_db / 10) * noise_in_band))


def keying_envelope(intervals, offset_s: float, n: int, sample_rate: int) -> np.ndarray:
    """0..1 envelope with raised-cosine edges; intervals are shifted by offset_s."""
    env = np.zeros(n)
    ramp_len = max(1, int(round(RISE_S * sample_rate)))
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ramp_len) + 0.5) / ramp_len)
    for on, off in intervals:
        i0 = max(0, int(round((offset_s + on) * sample_rate)))
        i1 = min(n, int(round((offset_s + off) * sample_rate)))
        if i1 <= i0:
            continue
        env[i0:i1] = 1.0
        r = min(ramp_len, (i1 - i0) // 2)
        if r > 0:
            env[i0:i0 + r] *= ramp[:r]
            env[i1 - r:i1] *= ramp[:r][::-1]
    return env


def generate(signals, sample_rate: int, duration_s: float, seed: int, add_noise: bool = True) -> np.ndarray:
    """Complex I/Q samples containing the given signals plus white noise."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    if add_noise:
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


def write_wav(path, iq: np.ndarray, sample_rate: int) -> None:
    """Write I/Q as 16-bit stereo PCM, scaling everything down if needed to avoid clipping."""
    peak = max(float(np.max(np.abs(iq.real))), float(np.max(np.abs(iq.imag))), 1e-12)
    scale = min(1.0, 0.95 / peak)
    stereo = np.empty((len(iq), 2), dtype="<i2")
    stereo[:, 0] = np.round(iq.real * scale * 32767)
    stereo[:, 1] = np.round(iq.imag * scale * 32767)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(stereo.tobytes())


def signal_end_s(spec: SignalSpec) -> float:
    """Time the signal's keying finishes, relative to the recording start."""
    intervals = keying_intervals(spec.text, spec.wpm)
    return spec.start_s + (intervals[-1][1] if intervals else 0.0)


def labels(signals, sample_rate: int, duration_s: float) -> dict:
    entries = []
    for s in signals:
        entries.append({**asdict(s), "end_s": round(signal_end_s(s), 3)})
    return {
        "sample_rate": sample_rate,
        "duration_s": duration_s,
        "snr_bandwidth_hz": SNR_BANDWIDTH_HZ,
        "signals": entries,
    }


def random_callsign(rng) -> str:
    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    prefix = CALL_PREFIXES[rng.integers(len(CALL_PREFIXES))]
    suffix = "".join(letters[rng.integers(26)] for _ in range(int(rng.integers(1, 4))))
    return f"{prefix}{rng.integers(10)}{suffix}"


def fill_text(message: str, wpm: float, available_s: float) -> str:
    """Repeat message, space-separated, as many times as fits in available_s."""
    text = message
    while True:
        candidate = f"{text} {message}"
        if keying_intervals(candidate, wpm)[-1][1] > available_s:
            return text
        text = candidate


def scenario_single() -> list[SignalSpec]:
    return [SignalSpec("CQ TEST K1ABC K1ABC", 1000.0, 25.0, 20.0, 0.5)]


def scenario_band(rng, count: int, duration_s: float, sample_rate: int) -> list[SignalSpec]:
    """count signals spread over 80% of the span, at least 1 kHz apart."""
    span = 0.4 * sample_rate
    specs: list[SignalSpec] = []
    max_attempts = 1000 * count
    for _attempt in range(max_attempts):
        if len(specs) >= count:
            break
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < 1000 for s in specs):
            continue
        wpm = round(float(rng.uniform(18, 36)), 1)
        snr = round(float(rng.uniform(10, 30)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    if len(specs) < count:
        raise ValueError(
            f"cannot place {count} signals at least 1 kHz apart within +/-{span:.0f} Hz"
        )
    return specs


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["single", "band"], default="single")
    parser.add_argument("--signals", type=int, default=10)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--sample-rate", type=int, default=192000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", type=Path, required=True,
                        help="output .wav file; the labels go next to it as .json")
    args = parser.parse_args(argv)

    rng = np.random.default_rng(args.seed)
    if args.scenario == "single":
        specs = scenario_single()
    else:
        try:
            specs = scenario_band(rng, args.signals, args.duration, args.sample_rate)
        except ValueError as exc:
            parser.error(str(exc))

    for spec in specs:
        end = signal_end_s(spec)
        if end > args.duration:
            parser.error(
                f"signal {spec.text!r} needs at least {end:.3f}s of recording "
                f"but --duration is {args.duration}s"
            )

    iq = generate(specs, args.sample_rate, args.duration, seed=args.seed + 1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_wav(args.out, iq, args.sample_rate)
    args.out.with_suffix(".json").write_text(
        json.dumps(labels(specs, args.sample_rate, args.duration), indent=2) + "\n")


if __name__ == "__main__":
    main()
