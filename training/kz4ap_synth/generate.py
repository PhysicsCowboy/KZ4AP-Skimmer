"""Synthetic CW I/Q recordings with known answers, for testing and benchmarking.

Writes a 16-bit stereo WAV file (left = I, right = Q) and, next to it, a JSON
labels file describing every signal in it.
"""

from __future__ import annotations

import argparse
import json
import wave
from dataclasses import asdict, dataclass, field
from pathlib import Path

import numpy as np

from .fading import gain_at, rayleigh_gain, slow_gain
from .keying import timed_intervals
from .messages import Over
from .morse import keying_intervals

SNR_BANDWIDTH_HZ = 500.0
DEFAULT_NOISE_SIGMA = 0.02
RISE_S = 0.005
TUNE_GAP_S = 0.5  # silence between a tune-up carrier and the first element, s

CALL_PREFIXES = ["K", "W", "N", "AA", "KB", "DL", "G", "F", "JA", "VE",
                 "EA", "I", "OH", "SM", "UA", "PY", "VK", "ZL"]
MESSAGES = ["CQ TEST {c} {c}", "CQ CQ DE {c} {c} K", "TU {c}", "{c} 5NN 14", "CQ {c} {c} TEST"]


@dataclass
class Sender:
    """One station of a multi-sender signal (a QSO heard on one frequency)."""
    call: str
    wpm: float
    keying: str = "machine"        # keying style, a key of keying.STYLES
    imbalance_dits: float = 0.0    # this operator's marks longer and spaces shorter by this, dits
    offset_hz: float = 0.0         # carrier offset from the signal's freq_offset_hz, Hz
    relative_db: float = 0.0       # key-down power relative to the signal's S500, dB


@dataclass
class SignalSpec:
    text: str
    freq_offset_hz: float        # carrier offset from the recording's center at start_s, Hz
    wpm: float
    snr_db: float                # S500: key-down carrier power over noise power in 500 Hz, dB
    start_s: float
    repeats: int = 1             # the text is sent this many times
    pause_s: float = 0.0         # silence between sendings, s
    tune_s: float = 0.0          # unkeyed carrier before the first sending, s (0 = none)
    drift_hz_per_s: float = 0.0  # carrier frequency change from start_s on, Hz/s
    keying: str = "machine"          # keying style, a key of keying.STYLES
    wpm_end: float | None = None     # speed at the end of each sending; None = constant
    speed_profile: str = "step"      # "step" (at the middle word) or "ramp" (linear per word)
    imbalance_dits: float = 0.0      # marks longer and spaces shorter by this, dits
    edge_s: float = RISE_S           # raised-cosine rise and fall time, s
    edges_centered: bool = False     # False: edges inside each mark (milestone 1); True: centered on its ends
    fading_hz: float = 0.0           # Rayleigh fading frequency spread f_D (2 sigma), Hz; 0 = none
    fading_shape: str = "gaussian"   # Doppler spectrum: "gaussian" or "butterworth" (VE3NEA's)
    score: bool = True               # False: an interferer, left out of the score
    tag: str = ""                    # the condition this signal represents, for summaries
    senders: list[Sender] = field(default_factory=list)  # a QSO's stations (empty: one sender)
    overs: list[Over] = field(default_factory=list)      # a QSO's overs, in order (empty: send text)
    turn_s: tuple[float, float] = (0.5, 2.0)             # silence before each over after the first, s (uniform)


@dataclass
class SignalPlan:
    intervals: list[tuple[float, float]]      # every key-down interval, s from start_s
    transmissions: list[tuple[float, float]]  # (first key-down, last key-up) of each sending, s from start_s
    senders: list[int] = field(default_factory=list)  # a QSO: the sender of each transmission


def amplitude_for_snr(snr_db: float, sample_rate: int) -> float:
    """Carrier amplitude giving snr_db against DEFAULT_NOISE_SIGMA noise, measured in 500 Hz."""
    noise_in_band = DEFAULT_NOISE_SIGMA**2 * SNR_BANDWIDTH_HZ / sample_rate
    return float(np.sqrt(10 ** (snr_db / 10) * noise_in_band))


def keying_envelope(intervals, offset_s: float, n: int, sample_rate: int, rise_s: float = RISE_S,
                    centered: bool = False) -> np.ndarray:
    """0..1 envelope with raised-cosine edges rise_s long; intervals are shifted by offset_s.
    By default (milestone 1) each edge lies inside its interval, so a mark is rise_s shorter,
    and a space rise_s longer, at 50% amplitude than the interval says. centered: each edge
    is centered on the interval's end, so the 50%-amplitude duration is the interval's
    (VE3NEA's convention)."""
    env = np.zeros(n)
    ramp_len = max(1, int(round(rise_s * sample_rate)))
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ramp_len) + 0.5) / ramp_len)
    shift = rise_s / 2 if centered else 0.0
    for on, off in intervals:
        i0 = max(0, int(round((offset_s + on - shift) * sample_rate)))
        i1 = min(n, int(round((offset_s + off + shift) * sample_rate)))
        if i1 <= i0:
            continue
        env[i0:i1] = 1.0
        r = min(ramp_len, (i1 - i0) // 2)
        if r > 0:
            env[i0:i0 + r] *= ramp[:r]
            env[i1 - r:i1] *= ramp[:r][::-1]
    return env


def sending_intervals(spec: SignalSpec, rng: np.random.Generator) -> list[tuple[float, float]]:
    """Key-down intervals of one sending of spec.text, from 0 s."""
    return timed_intervals(spec.text, spec.wpm, spec.keying, rng, spec.wpm_end, spec.speed_profile,
                           spec.imbalance_dits)


def plan_signal(spec: SignalSpec, rng: np.random.Generator) -> SignalPlan:
    """All key-down intervals of one signal: the tune-up carrier, then each sending."""
    if spec.overs:
        return plan_overs(spec, rng)
    intervals: list[tuple[float, float]] = []
    transmissions: list[tuple[float, float]] = []
    t = 0.0
    if spec.tune_s > 0:
        intervals.append((0.0, spec.tune_s))
        t = spec.tune_s + TUNE_GAP_S
    for _ in range(spec.repeats):
        sent = sending_intervals(spec, rng)
        if not sent:
            break
        if transmissions:
            t = transmissions[-1][1] + spec.pause_s
        intervals.extend((t + on, t + off) for on, off in sent)
        transmissions.append((t + sent[0][0], t + sent[-1][1]))
    return SignalPlan(intervals, transmissions)


def plan_overs(spec: SignalSpec, rng: np.random.Generator) -> SignalPlan:
    """A QSO: each over at its sender's speed, keying and imbalance, each after a
    silence drawn uniformly from spec.turn_s (none before the first)."""
    plan = SignalPlan([], [], [])
    t = 0.0
    for over in spec.overs:
        who = spec.senders[over.sender]
        sent = timed_intervals(over.text, who.wpm, who.keying, rng, imbalance_dits=who.imbalance_dits)
        if not sent:
            raise ValueError(f"over {over.text!r} has nothing to key")
        if plan.transmissions:
            t = plan.transmissions[-1][1] + float(rng.uniform(*spec.turn_s))
        plan.intervals.extend((t + on, t + off) for on, off in sent)
        plan.transmissions.append((t + sent[0][0], t + sent[-1][1]))
        plan.senders.append(over.sender)
    return plan


def plan_intervals(signals, seed: int) -> list[SignalPlan]:
    """Plans every signal. Random timing for signal i comes from its own generator,
    seeded by (seed, i), so a random feature of one signal never changes another."""
    return [plan_signal(s, np.random.default_rng([seed, i, 2])) for i, s in enumerate(signals)]


def generate(signals, sample_rate: int, duration_s: float, seed: int, add_noise: bool = True) -> np.ndarray:
    """Complex I/Q samples containing the given signals plus white noise."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    if add_noise:
        iq += (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * (DEFAULT_NOISE_SIGMA / np.sqrt(2))
    for index, (s, plan) in enumerate(zip(signals, plan_intervals(signals, seed))):
        phase = rng.uniform(0, 2 * np.pi)
        if s.overs:
            add_overs(iq, s, plan, sample_rate, seed, index)
            continue
        intervals = plan.intervals
        if not intervals:
            continue
        tail = s.edge_s / 2 if s.edges_centered else 0.0  # a centered edge reaches this far past an interval
        i0 = max(0, int((s.start_s - tail) * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1] + tail) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate, s.edge_s,
                              s.edges_centered)
        t = np.arange(i0, i1) / sample_rate
        angle = 2 * np.pi * s.freq_offset_hz * t + phase
        if s.drift_hz_per_s:
            angle = angle + np.pi * s.drift_hz_per_s * (t - s.start_s) ** 2
        signal = amplitude_for_snr(s.snr_db, sample_rate) * env * np.exp(1j * angle)
        if s.fading_hz > 0:
            signal = signal * rayleigh_gain(i1 - i0, sample_rate, s.fading_hz,
                                            np.random.default_rng([seed, index, 1]), s.fading_shape)
        iq[i0:i1] += signal
    return iq


def add_overs(iq: np.ndarray, spec: SignalSpec, plan: SignalPlan, sample_rate: int, seed: int,
              index: int) -> None:
    """Adds a QSO to iq: each over at its sender's carrier (freq_offset_hz + offset_hz),
    level (snr_db + relative_db) and phase, faded by that sender's own fading path."""
    if not plan.intervals:
        return
    n = len(iq)
    phases = np.random.default_rng([seed, index, 3]).uniform(0, 2 * np.pi, size=len(spec.senders))
    paths = {}
    if spec.fading_hz > 0:
        for k in sorted(set(plan.senders)):
            paths[k] = slow_gain(plan.intervals[-1][1] + 1.0, spec.fading_hz,
                                 np.random.default_rng([seed, index, 1, k]), spec.fading_shape)
    for (on, off), k in zip(plan.transmissions, plan.senders):
        who = spec.senders[k]
        tail = spec.edge_s / 2 if spec.edges_centered else 0.0
        i0 = max(0, int((spec.start_s + on - tail) * sample_rate))
        i1 = min(n, int(np.ceil((spec.start_s + off + tail) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        part = [iv for iv in plan.intervals if on <= iv[0] and iv[1] <= off]
        env = keying_envelope(part, spec.start_s - i0 / sample_rate, i1 - i0, sample_rate, spec.edge_s,
                              spec.edges_centered)
        t = np.arange(i0, i1) / sample_rate
        angle = 2 * np.pi * (spec.freq_offset_hz + who.offset_hz) * t + phases[k]
        if spec.drift_hz_per_s:
            angle = angle + np.pi * spec.drift_hz_per_s * (t - spec.start_s) ** 2
        signal = amplitude_for_snr(spec.snr_db + who.relative_db, sample_rate) * env * np.exp(1j * angle)
        if k in paths:
            signal = signal * gain_at(paths[k], t - spec.start_s)
        iq[i0:i1] += signal


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


def reference_text(spec: SignalSpec) -> str:
    """What a perfect decoder would print for the whole signal."""
    if spec.overs:
        return " ".join(o.text for o in spec.overs)
    return " ".join([spec.text] * spec.repeats)


def signal_end_s(spec: SignalSpec, plan: SignalPlan) -> float:
    """Time the signal's keying finishes, relative to the recording start."""
    return spec.start_s + (plan.intervals[-1][1] if plan.intervals else 0.0)


def labels(signals, sample_rate: int, duration_s: float, seed: int) -> dict:
    """Labels for a recording; seed must be the one passed to generate()."""
    entries = []
    for s, plan in zip(signals, plan_intervals(signals, seed)):
        if s.overs:
            texts = [o.text for o in s.overs]
            extra = [{"sender": s.senders[k].call, "sender_index": k, "wpm": s.senders[k].wpm,
                      "keying": s.senders[k].keying, "imbalance_dits": s.senders[k].imbalance_dits,
                      "offset_hz": s.senders[k].offset_hz, "relative_db": s.senders[k].relative_db}
                     for k in plan.senders]
        else:
            texts = [s.text] * len(plan.transmissions)
            extra = [{}] * len(plan.transmissions)
        entries.append({
            **asdict(s),
            "text": reference_text(s),
            "end_s": round(signal_end_s(s, plan), 3),
            "transmissions": [
                {"text": text, "start_s": round(s.start_s + on, 3), "end_s": round(s.start_s + off, 3), **more}
                for text, (on, off), more in zip(texts, plan.transmissions, extra)
            ],
        })
    return {
        "sample_rate": sample_rate,
        "duration_s": duration_s,
        "snr_bandwidth_hz": SNR_BANDWIDTH_HZ,
        "signals": entries,
    }


def station_labels(signals, sample_rate: int, duration_s: float, seed: int) -> dict:
    """labels(), but with each QSO split into one entry per station: that station's overs
    only, at its own carrier (freq_offset_hz + offset_hz) and level (snr_db + relative_db),
    so a station the receiver hears as its own track is scored against its own frequency.
    Other signals are as in labels(). Each station entry names its QSO (qso_index)."""
    base = labels(signals, sample_rate, duration_s, seed)
    entries = []
    for i, (s, entry) in enumerate(zip(signals, base["signals"])):
        if not s.overs:
            entries.append(entry)
            continue
        for k, who in enumerate(s.senders):
            # offset_hz 0: these transmissions are at the entry's own frequency
            mine = [{**t, "offset_hz": 0.0} for t in entry["transmissions"] if t["sender_index"] == k]
            if not mine:
                continue
            entries.append({
                "text": " ".join(t["text"] for t in mine),
                "freq_offset_hz": round(s.freq_offset_hz + who.offset_hz, 3),
                "wpm": who.wpm, "snr_db": round(s.snr_db + who.relative_db, 3),
                "start_s": mine[0]["start_s"], "end_s": mine[-1]["end_s"],
                "keying": who.keying, "imbalance_dits": who.imbalance_dits, "drift_hz_per_s": s.drift_hz_per_s,
                "score": s.score, "tag": s.tag, "qso_index": i, "sender": who.call, "transmissions": mine,
            })
    return {**base, "signals": entries}


# Where an answering station's carrier lands relative to the caller's (heuristic: no
# measured distribution yet). Zero-beating by ear leaves a few to tens of Hz; a sidetone
# pitch that differs from the rig's CW offset, or RIT, leaves 100-200 Hz.
ANSWER_OFFSET_BANDS_HZ = ((0.0, 10.0, 0.40), (10.0, 50.0, 0.30), (50.0, 100.0, 0.15), (100.0, 200.0, 0.15))


def draw_answer_offset_hz(rng) -> float:
    """An answering station's carrier offset from the caller's, Hz: a band from
    ANSWER_OFFSET_BANDS_HZ by its probability, uniform within it, either sign."""
    band = int(rng.choice(len(ANSWER_OFFSET_BANDS_HZ), p=[p for _, _, p in ANSWER_OFFSET_BANDS_HZ]))
    low, high, _ = ANSWER_OFFSET_BANDS_HZ[band]
    return round(float(rng.uniform(low, high)) * (1.0 if rng.random() < 0.5 else -1.0), 1)


def qso_spec(overs: list[Over], senders: list[Sender], freq_offset_hz: float, snr_db: float, start_s: float,
             **options) -> SignalSpec:
    """A QSO as one signal. Its text is the whole QSO; its wpm and keying are the first
    sender's (for summaries); options are further SignalSpec fields (fading_hz, tag, ...)."""
    return SignalSpec(" ".join(o.text for o in overs), freq_offset_hz, senders[0].wpm, snr_db, start_s,
                      keying=senders[0].keying, senders=list(senders), overs=list(overs), **options)


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


def scenario_band(rng, count: int, duration_s: float, sample_rate: int, min_spacing_hz: float = 1000.0,
                  span_hz: float | None = None, wpm_range: tuple[float, float] = (18.0, 36.0),
                  snr_range: tuple[float, float] = (10.0, 30.0)) -> list[SignalSpec]:
    """count signals within +/-span_hz (default: 80% of the recording's span), at least
    min_spacing_hz apart (0 allows any overlap), with speeds and S500 drawn uniformly
    from the given ranges."""
    span = 0.4 * sample_rate if span_hz is None else span_hz
    specs: list[SignalSpec] = []
    max_attempts = 1000 * count
    for _attempt in range(max_attempts):
        if len(specs) >= count:
            break
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < min_spacing_hz for s in specs):
            continue
        wpm = round(float(rng.uniform(*wpm_range)), 1)
        snr = round(float(rng.uniform(*snr_range)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    if len(specs) < count:
        raise ValueError(
            f"cannot place {count} signals at least {min_spacing_hz:.0f} Hz apart within +/-{span:.0f} Hz"
        )
    return specs


def with_interferer(wanted: SignalSpec, offset_hz: float, relative_db: float, wpm: float,
                    text: str) -> list[SignalSpec]:
    """The wanted signal plus an unscored interferer offset_hz from it, relative_db dB
    stronger in key-down power, at its own speed, starting at the same time."""
    interferer = SignalSpec(text, wanted.freq_offset_hz + offset_hz, wpm, wanted.snr_db + relative_db,
                            wanted.start_s, score=False, tag=wanted.tag)
    return [wanted, interferer]


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["single", "band"], default="single")
    parser.add_argument("--signals", type=int, default=10)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--sample-rate", type=int, default=192000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--min-spacing", type=float, default=1000.0,
                        help="band: minimum spacing between stations, Hz (0 allows overlap)")
    parser.add_argument("--span", type=float, default=None,
                        help="band: stations lie within +/- this many Hz (default: 0.4 x sample rate)")
    parser.add_argument("--wpm-min", type=float, default=18.0)
    parser.add_argument("--wpm-max", type=float, default=36.0)
    parser.add_argument("--snr-min", type=float, default=10.0, help="band: lowest S500, dB")
    parser.add_argument("--snr-max", type=float, default=30.0, help="band: highest S500, dB")
    parser.add_argument("--out", type=Path, required=True,
                        help="output .wav file; the labels go next to it as .json")
    args = parser.parse_args(argv)

    rng = np.random.default_rng(args.seed)
    if args.scenario == "single":
        specs = scenario_single()
    else:
        try:
            specs = scenario_band(rng, args.signals, args.duration, args.sample_rate,
                                  min_spacing_hz=args.min_spacing, span_hz=args.span,
                                  wpm_range=(args.wpm_min, args.wpm_max),
                                  snr_range=(args.snr_min, args.snr_max))
        except ValueError as exc:
            parser.error(str(exc))

    noise_seed = args.seed + 1
    for spec, plan in zip(specs, plan_intervals(specs, noise_seed)):
        end = signal_end_s(spec, plan)
        if end > args.duration:
            parser.error(
                f"signal {spec.text!r} needs at least {end:.3f}s of recording "
                f"but --duration is {args.duration}s"
            )

    iq = generate(specs, args.sample_rate, args.duration, seed=noise_seed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_wav(args.out, iq, args.sample_rate)
    args.out.with_suffix(".json").write_text(
        json.dumps(labels(specs, args.sample_rate, args.duration, seed=noise_seed), indent=2) + "\n")


if __name__ == "__main__":
    main()
