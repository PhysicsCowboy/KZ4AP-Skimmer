"""Named benchmark suites: which synthetic recordings to make, a runner that
scores each with kz4ap-bench, and a summary.

    python -m kz4ap_synth.suites generate --suite full --out build/suite/full [--seeds 3]
    python -m kz4ap_synth.suites run --out build/suite/full --bench PATH/kz4ap-bench \
        --front-end baseline --front-end matched
    python -m kz4ap_synth.suites summarize --out build/suite/full

"smoke" is the recording bench/smoke.sh makes (the CI check); "full" is for
local runs, sized for --seeds 3. S500 everywhere: key-down carrier power over
noise power in 500 Hz, dB.

Group B is anchored to VE3NEA's DeepCW benchmark: his Butterworth fading
spectrum and f_D grid, his SNR points converted to S500 (his key-on SNR in
3 kHz + 7.78 dB), his keying styles with a per-operator imbalance, his 2 ms
centered keying edges, his style mix and speed range (one recording), and
filler text with his statistics. Groups G and H send whole ragchew QSOs
(G: one speed and style for both stations on one carrier; H: two operators
0-200 Hz apart). Every rate in the summary carries a bootstrap 95% interval
over signals, so a difference smaller than the intervals is not a result.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
import zlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from .generate import (MESSAGES, Sender, SignalSpec, draw_answer_offset_hz, fill_text, generate, labels,
                       plan_intervals, qso_spec, random_callsign, scenario_band, signal_end_s, station_labels,
                       with_interferer, write_wav)
from .keying import VE3NEA_WPM_RANGE, draw_imbalance_dits, draw_style
from .messages import ragchew as ragchew_overs
from .messages import random_operator, random_text
from .morse import keying_intervals

BIN_HZ = 48000 / 2048  # the engine's FFT bin width at 48 kHz (and at 192 kHz), Hz
DETECTOR_SEPARATION_BINS = 3  # the detector's min_separation_bins: closer peaks become one track
SWEEP_SNR_DB = [float(x) for x in range(-10, 22, 2)]  # S500 sweep: -10 ... +20 dB
VE3NEA_RHO_DB = (-16.0, -12.0, -6.0, -3.0, 0.0, 6.0, 10.0, 20.0, 30.0, 50.0)  # his key-on SNR, noise in 3 kHz, dB
RHO_TO_S500_DB = 10 * math.log10(3000.0 / 500.0)  # 7.78 dB: the same white noise measured in 500 Hz, not 3 kHz
VE3NEA_SNR_DB = [round(r + RHO_TO_S500_DB, 2) for r in VE3NEA_RHO_DB]  # S500: -8.22 ... 57.78 dB
VE3NEA_SPREADS_HZ = (0.1, 0.3, 1.0, 3.0)  # his f_D grid, Hz
VE3NEA_EDGE_S = 0.002  # his raised-cosine keying edges, s, centered on each element's ends
CER_THRESHOLDS = (0.05, 0.10)
FRONT_ENDS = ("baseline", "matched")
BENCH_FRONT_END = {"baseline": "envelope", "matched": "matched"}  # kz4ap-bench --front-end; its default is Matched
COUNT_KEYS = ("symbols", "edits", "chars", "char_edits", "spaces", "space_edits",
              "first_word_symbols", "first_word_edits", "nospace_symbols", "nospace_edits")
BOOTSTRAP_RESAMPLES = 1000
MIN_SIGNALS_FOR_INTERVAL = 2  # a bootstrap over one signal has no spread: no interval is printed
MIN_SIGNALS_PER_POINT = 2     # an S500 crossing is computed only if every point holds this many signals
# The oracle channel's response relative to its passband (measured, docs/signal-processing.md
# section 7): -1.17 dB at 100 Hz from its center, -6.02 dB at 150 Hz, -18.0 dB at 200 Hz. An
# oracle QSO label (channel on the caller) holds the answering station below the -6 dB point.
ORACLE_CHANNEL_CUTOFF_HZ = 150.0
# With oracle channels (no detector), the Matched tracker's anchor is the labeled frequency and it
# fine-tunes only within +/-12 Hz of it (docs/signal-processing.md section 7, "Frequency
# re-centering"): a carrier that drifts, or an answering station that sits, farther from the
# label is out of its reach, so those Matched rows are "not meaningful (oracle anchor)".
ORACLE_ANCHOR_RANGE_HZ = 12.0
ANCHOR_NOTE = "not meaningful (oracle anchor)"
PLANNED_SEEDS = 3  # the full suite's per-point sizes below assume --seeds 3

FADING_ROWS = ([(keying, 24.0, f_d) for keying in ("paddle", "hand") for f_d in VE3NEA_SPREADS_HZ]
               + [("paddle", 12.0, 0.1), ("paddle", 40.0, 0.1)])  # (keying, WPM, f_D Hz)
FIST_STYLES = ("machine", "computer", "paddle", "bug", "hand")
SPEED_CASES = [(20.0, 35.0, "step", "step 20->35"), (35.0, 20.0, "step", "step 35->20"),
               (15.0, 30.0, "ramp", "ramp 15->30"), (30.0, 15.0, "ramp", "ramp 30->15"),
               (10.0, None, "step", "10 wpm"), (60.0, None, "step", "60 wpm")]
QRM_OFFSETS_HZ = (20.0, 50.0, 100.0, 150.0)
QRM_RELATIVE_DB = (-10.0, 0.0, 10.0, 20.0)
TUNING_OFFSETS_HZ = (0.0, 2.9, 5.9, 8.8, 11.7)  # from the bin center: 0 to 1/2 bin
DRIFT_HZ_PER_S = (0.2, 0.5, 1.0, 2.0)
PAUSES_S = (2.0, 5.0, 10.0, 20.0)
TUNE_UP_S = (0.3, 0.6, 1.0, 2.0)
CROWDED_SPACING_HZ = (200.0, 100.0, 50.0, 0.0)
RAGCHEW_SNR_DB = (0.0, 4.0, 8.0, 12.0, 16.0, 20.0)
QSO_OFFSETS_HZ = (0.0, 10.0, 25.0, 50.0, 100.0, 200.0)  # the answering station's carrier offset from the caller's, Hz
QSO_WPM_RANGE = (20.0, 32.0)


@dataclass
class Recording:
    name: str
    group: str         # scenario group, used in summaries
    sample_rate: int
    duration_s: float
    noise_seed: int    # the seed passed to generate() and labels()
    oracle: bool       # score with kz4ap-bench --oracle
    specs: list[SignalSpec]
    station_labels: bool = False  # also score against one label per QSO station (generate.station_labels)


def qso_regime(offset_hz: float) -> str:
    """How the detector sees a QSO whose stations are offset_hz apart (derived from its
    3-bin minimum peak separation and 23.4 Hz bins): within 2 bins the two peaks are always
    closer than 3 bins (one track); from 3 bins on they never are (two tracks); between,
    it depends on where the stations fall within their bins."""
    df = abs(offset_hz)
    if df <= (DETECTOR_SEPARATION_BINS - 1) * BIN_HZ:
        return "same-track"
    if df < DETECTOR_SEPARATION_BINS * BIN_HZ:
        return "ambiguous"
    return "separate-track"


def _message(rng) -> str:
    return MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))


def _text(rng, wpm: float, available_s: float, keying: str = "machine") -> str:
    """A repeated contest message that fits available_s; random keying runs longer, so it gets 30% slack."""
    return fill_text(_message(rng), wpm, available_s * (1.0 if keying == "machine" else 0.7))


def _filler(rng, wpm: float, available_s: float, keying: str = "hand") -> str:
    """VE3NEA-statistics random text (messages.random_text) that fits available_s at exact
    timing; random keying runs longer, so it gets 30% slack."""
    budget = available_s * (1.0 if keying == "machine" else 0.7)
    # VE3NEA's words average 3.06 characters, about 0.6 of a PARIS word: 2 per PARIS word is ample
    words = random_text(rng, int(2 * budget / 60.0 * wpm) + 20).split()
    lo, hi = 1, len(words)
    while lo < hi:  # the longest prefix that fits
        mid = (lo + hi + 1) // 2
        if keying_intervals(" ".join(words[:mid]), wpm)[-1][1] <= budget:
            lo = mid
        else:
            hi = mid - 1
    return " ".join(words[:lo])


def _fitted_duration(specs: list[SignalSpec], noise_seed: int) -> float:
    """The recording length that holds every signal, plus 2 s, rounded up to whole seconds."""
    ends = [signal_end_s(s, p) for s, p in zip(specs, plan_intervals(specs, noise_seed))]
    return float(math.ceil(max(ends) + 2.0))


def _start(rng) -> float:
    return round(float(rng.uniform(0.5, 2.0)), 3)


def _slots(count: int, spacing_hz: float, rng, jitter_hz: float) -> list[float]:
    """count frequencies spacing_hz apart, centered on 0 Hz, each moved by a uniform offset
    within +/-jitter_hz so stations fall anywhere relative to the FFT bins."""
    centers = (np.arange(count) - (count - 1) / 2) * spacing_hz
    return [round(float(c + rng.uniform(-jitter_hz, jitter_hz)), 1) for c in centers]


def _bin_centers(count: int, spacing_bins: int) -> list[float]:
    """count exact FFT-bin centers spacing_bins apart around 0 Hz (48 kHz recordings)."""
    return [(k - count // 2) * spacing_bins * BIN_HZ for k in range(count)]


def sensitivity(seed: int) -> list[Recording]:
    """Two 120 s recordings per speed, each with 2 stations per S500 point: 4 stations per
    point per seed. Filler text with VE3NEA's statistics, machine keying."""
    recs = []
    points = [snr for snr in SWEEP_SNR_DB for _ in range(2)]
    for code, wpm in enumerate((12.0, 25.0, 40.0)):
        for part in (0, 1):
            rng = np.random.default_rng([seed, 1, code, part])
            specs = []
            for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
                start = _start(rng)
                specs.append(SignalSpec(_filler(rng, wpm, 118.0 - start, "machine"), f, wpm, snr, start,
                                        tag=f"{wpm:g} wpm"))
            recs.append(Recording(f"A-awgn-{wpm:g}wpm-{part}-s{seed}", "A sensitivity", 48000, 120.0,
                                  1000 * seed + 10 + 2 * code + part, True, specs))
    return recs


def fading(seed: int) -> list[Recording]:
    """VE3NEA-anchored: his Butterworth fading spectrum and f_D grid, his SNR points (as S500),
    his keying styles with a per-operator imbalance, his 2 ms centered edges, and his
    random-text statistics. 3 stations per point for 180 s (240 s below 20 WPM, so every
    point holds at least 1000 characters over 3 seeds)."""
    recs = []
    points = [snr for snr in VE3NEA_SNR_DB for _ in range(3)]
    for code, (keying, wpm, f_d) in enumerate(FADING_ROWS):
        rng = np.random.default_rng([seed, 2, code])
        duration = 240.0 if wpm < 20.0 else 180.0
        specs = []
        for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
            start = _start(rng)
            specs.append(SignalSpec(_filler(rng, wpm, duration - 2.0 - start, keying), f, wpm, snr, start,
                                    keying=keying,
                                    imbalance_dits=round(draw_imbalance_dits(rng), 3), fading_hz=f_d,
                                    fading_shape="butterworth", edge_s=VE3NEA_EDGE_S, edges_centered=True,
                                    tag=f"{keying} {wpm:g} wpm fD {f_d:g} Hz"))
        recs.append(Recording(f"B-fading-{keying}-{wpm:g}wpm-{f_d:g}Hz-s{seed}", "B fading", 48000, duration,
                              1000 * seed + 20 + code, True, specs))
    rng = np.random.default_rng([seed, 2, len(FADING_ROWS)])
    specs = []
    for f, snr in zip(_slots(len(points), 1200.0, rng, BIN_HZ / 2), points):
        start = _start(rng)
        wpm = round(float(rng.uniform(*VE3NEA_WPM_RANGE)), 1)
        f_d = float(rng.choice(VE3NEA_SPREADS_HZ))
        keying = draw_style(rng)
        specs.append(SignalSpec(_filler(rng, wpm, 178.0 - start, keying), f, wpm, snr, start, keying=keying,
                                imbalance_dits=round(draw_imbalance_dits(rng), 3), fading_hz=f_d,
                                fading_shape="butterworth", edge_s=VE3NEA_EDGE_S, edges_centered=True,
                                tag="VE3NEA mix"))
    recs.append(Recording(f"B-fading-mix-s{seed}", "B fading", 48000, 180.0, 1000 * seed + 20 + len(FADING_ROWS),
                          True, specs))
    return recs


def fists(seed: int) -> list[Recording]:
    """3 stations per (S500, imbalance) combination for 120 s."""
    recs = []
    for code, keying in enumerate(FIST_STYLES):
        rng = np.random.default_rng([seed, 3, code])
        combos = [(snr, imb) for snr in (5.0, 10.0, 20.0) for imb in (0.0, 0.1, -0.1) for _ in range(3)]
        specs = []
        for f, (snr, imb) in zip(_slots(len(combos), 1200.0, rng, BIN_HZ / 2), combos):
            start = _start(rng)
            specs.append(SignalSpec(_text(rng, 25.0, 118.0 - start, keying), f, 25.0, snr, start, keying=keying,
                                    imbalance_dits=imb, tag=f"{keying} imbalance {imb:+.1f}"))
        recs.append(Recording(f"C-fists-{keying}-s{seed}", "C fists", 48000, 120.0, 1000 * seed + 30 + code,
                              True, specs))
    return recs


def speed(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 4])
    cases = SPEED_CASES * 2
    specs = []
    for f, (wpm, wpm_end, profile, tag) in zip(_slots(len(cases), 1500.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        slowest = min(wpm, wpm_end if wpm_end is not None else wpm)  # timed at the slowest speed, so it fits
        specs.append(SignalSpec(_text(rng, slowest, 58.0 - start), f, wpm, 15.0, start, wpm_end=wpm_end,
                                speed_profile=profile, tag=tag))
    return [Recording(f"D-speed-s{seed}", "D speed", 48000, 60.0, 1000 * seed + 40, True, specs)]


def interference(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 5])
    pairs = [(df, rel) for df in QRM_OFFSETS_HZ for rel in QRM_RELATIVE_DB]
    specs = []
    for f, (df, rel) in zip(_slots(len(pairs), 1200.0, rng, BIN_HZ / 2), pairs):
        start = _start(rng)
        wanted = SignalSpec(_text(rng, 25.0, 58.0 - start), f, 25.0, 10.0, start,
                            tag=f"df {df:g} Hz, {rel:+g} dB re wanted key-down power")
        specs += with_interferer(wanted, df, rel, 30.0, _text(rng, 30.0, 58.0 - start))
    return [Recording(f"E-qrm-s{seed}", "E interference", 48000, 60.0, 1000 * seed + 50, True, specs)]


def tuning(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 6, 0])
    cases = [(wpm, off, snr) for wpm in (20.0, 25.0) for off in TUNING_OFFSETS_HZ for snr in (0.0, 5.0)]
    specs = []
    for c, (wpm, off, snr) in zip(_bin_centers(len(cases), 51), cases):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, wpm, 58.0 - start), round(c + off, 3), wpm, snr, start,
                                tag=f"offset {off:g} Hz {wpm:g} wpm"))
    offsets = Recording(f"F-offset-s{seed}", "F tuning", 48000, 60.0, 1000 * seed + 60, True, specs)
    rng = np.random.default_rng([seed, 6, 1])
    drifts = [d for d in DRIFT_HZ_PER_S for _ in range(2)]
    specs = []
    for c, d in zip(_bin_centers(len(drifts), 51), drifts):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 28.0 - start), round(c, 3), 25.0, 5.0, start, drift_hz_per_s=d,
                                tag=f"drift {d:g} Hz/s"))
    drift = Recording(f"F-drift-s{seed}", "F tuning", 48000, 30.0, 1000 * seed + 61, True, specs)
    return [offsets, drift]


def strong(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 7])
    snrs = [s for s in (30.0, 40.0, 50.0, 60.0) for _ in range(2)]
    specs = []
    for f, snr in zip(_slots(len(snrs), 5000.0, rng, BIN_HZ / 2), snrs):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 28.0 - start), f, 25.0, snr, start, tag=f"S500 {snr:g} dB"))
    return [Recording(f"strong-s{seed}", "strong", 48000, 30.0, 1000 * seed + 70, False, specs)]


def pauses(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 8])
    cases = [p for p in PAUSES_S for _ in range(2)]
    specs = []
    for f, p in zip(_slots(len(cases), 2400.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        call = random_callsign(rng)
        specs.append(SignalSpec(f"CQ TEST {call} {call}", f, 25.0, 15.0, start, repeats=3, pause_s=p,
                                tag=f"pause {p:g} s"))
    return [Recording(f"pauses-s{seed}", "pauses", 48000, 80.0, 1000 * seed + 80, False, specs)]


def tune_up(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 9])
    cases = [t for t in TUNE_UP_S for _ in range(2)]
    specs = []
    for f, t in zip(_slots(len(cases), 2400.0, rng, BIN_HZ / 2), cases):
        start = _start(rng)
        specs.append(SignalSpec(_text(rng, 25.0, 27.5 - start - t), f, 25.0, 15.0, start, tune_s=t,
                                tag=f"tune-up {t:g} s"))
    return [Recording(f"tune-up-s{seed}", "tune-up", 48000, 30.0, 1000 * seed + 90, False, specs)]


def first_sample(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 10])
    specs = [SignalSpec(_text(rng, 25.0, 18.0), f, 25.0, 15.0, 0.0, tag="from the first sample")
             for f in _slots(4, 3000.0, rng, BIN_HZ / 2)]
    return [Recording(f"first-sample-s{seed}", "first sample", 48000, 20.0, 1000 * seed + 100, False, specs)]


def crowded(seed: int) -> list[Recording]:
    recs = []
    for code, spacing in enumerate(CROWDED_SPACING_HZ):
        rng = np.random.default_rng([seed, 11, code])
        specs = scenario_band(rng, 25, 40.0, 48000, min_spacing_hz=spacing, span_hz=5000.0,
                              wpm_range=(10.0, 60.0), snr_range=(10.0, 30.0))
        for s in specs:
            s.tag = f"spacing {spacing:g} Hz"
        recs.append(Recording(f"crowded-{spacing:g}Hz-s{seed}", "crowded", 48000, 40.0, 1000 * seed + 110 + code,
                              False, specs))
    return recs


def band(seed: int) -> list[Recording]:
    rng = np.random.default_rng([seed, 12])
    specs = scenario_band(rng, 20, 30.0, 192000, wpm_range=(10.0, 60.0), snr_range=(10.0, 60.0))
    for s in specs:
        s.tag = "band"
    return [Recording(f"band-s{seed}", "band", 192000, 30.0, 1000 * seed + 120, False, specs)]


def ragchew(seed: int) -> list[Recording]:
    """Whole ragchew QSOs, both sides on one frequency at one speed and style, so only the
    text (prosigns, abbreviations, overs and the pauses between them) differs from group A."""
    rng = np.random.default_rng([seed, 13])
    points = [snr for snr in RAGCHEW_SNR_DB for _ in range(2)]
    specs = []
    for f, snr in zip(_slots(len(points), 1500.0, rng, BIN_HZ / 2), points):
        a, b = random_operator(rng), random_operator(rng)
        senders = [Sender(a.call, 25.0, "paddle"), Sender(b.call, 25.0, "paddle")]
        specs.append(qso_spec(ragchew_overs(rng, a, b), senders, f, snr, _start(rng), tag="ragchew 25 wpm"))
    noise_seed = 1000 * seed + 130
    return [Recording(f"G-ragchew-s{seed}", "G ragchew", 48000, _fitted_duration(specs, noise_seed), noise_seed,
                      True, specs)]


def _qso(rng, f: float, offset_hz: float, tag: str) -> SignalSpec:
    """A whole ragchew by two operators with their own speeds (QSO_WPM_RANGE), styles
    (VE3NEA's mix) and imbalances; the answering one offset_hz away, -6 ... +6 dB re the
    caller's key-down power; the caller at S500 = 15 dB."""
    a, b = random_operator(rng), random_operator(rng)
    senders = [Sender(op.call, round(float(rng.uniform(*QSO_WPM_RANGE)), 1), draw_style(rng),
                      round(draw_imbalance_dits(rng), 3)) for op in (a, b)]
    senders[1].offset_hz = offset_hz
    senders[1].relative_db = round(float(rng.uniform(-6.0, 6.0)), 1)
    return qso_spec(ragchew_overs(rng, a, b), senders, f, 15.0, _start(rng), tag=tag)


def two_station_qso(seed: int) -> list[Recording]:
    """Whole ragchew QSOs as a listener hears them, in three regimes of the answering
    station's offset (qso_regime): on the grid QSO_OFFSETS_HZ, 2 QSOs each, through the
    detector and again with oracle channels (the front end's turnover apart from
    detection); and 12 QSOs with offsets drawn from generate.ANSWER_OFFSET_BANDS_HZ.
    Each is also scored with one label per station."""
    rng = np.random.default_rng([seed, 14])
    offsets = [df for df in QSO_OFFSETS_HZ for _ in range(2)]
    specs = [_qso(rng, f, df, f"{qso_regime(df)}, offset {df:g} Hz")
             for f, df in zip(_slots(len(offsets), 2400.0, rng, BIN_HZ / 2), offsets)]
    noise_seed = 1000 * seed + 140
    duration = _fitted_duration(specs, noise_seed)
    grid = Recording(f"H-qso-s{seed}", "H two-station QSO", 48000, duration, noise_seed, False, specs, True)
    oracle = Recording(f"H-qso-oracle-s{seed}", "H two-station QSO, oracle", 48000, duration, noise_seed, True,
                       specs, True)
    rng = np.random.default_rng([seed, 15])
    specs = []
    for f in _slots(12, 2400.0, rng, BIN_HZ / 2):
        df = draw_answer_offset_hz(rng)
        specs.append(_qso(rng, f, df, f"{qso_regime(df)}, drawn offset"))
    noise_seed = 1000 * seed + 141
    drawn = Recording(f"H-qso-drawn-s{seed}", "H two-station QSO", 48000, _fitted_duration(specs, noise_seed),
                      noise_seed, False, specs, True)
    return [grid, oracle, drawn]


def smoke_suite(seeds: int = 1) -> list[Recording]:
    """The recording bench/smoke.sh makes: band scenario, 8 signals, 30 s, seed 1 (noise seed 2)."""
    specs = scenario_band(np.random.default_rng(1), 8, 30.0, 192000)
    return [Recording("smoke-band", "smoke", 192000, 30.0, 2, False, specs)]


def full_suite(seeds: int = 1) -> list[Recording]:
    recs = []
    for seed in range(1, seeds + 1):
        for build in (sensitivity, fading, fists, speed, interference, tuning, ragchew, two_station_qso, strong,
                      pauses, tune_up, first_sample, crowded, band):
            recs += build(seed)
    return recs


SUITES = {"smoke": smoke_suite, "full": full_suite}


def check_recording(rec: Recording) -> None:
    """Raises ValueError if a signal runs past the end or lies outside the span."""
    for spec, plan in zip(rec.specs, plan_intervals(rec.specs, rec.noise_seed)):
        end = signal_end_s(spec, plan)
        if end > rec.duration_s:
            raise ValueError(f"{rec.name}: signal at {spec.freq_offset_hz} Hz ends at {end:.2f} s, "
                             f"after the recording's {rec.duration_s} s")
        if abs(spec.freq_offset_hz) + max((abs(s.offset_hz) for s in spec.senders), default=0.0) \
                > 0.45 * rec.sample_rate:
            raise ValueError(f"{rec.name}: signal at {spec.freq_offset_hz} Hz is outside the span")


def write_suite(recordings: list[Recording], out_dir: Path, suite_name: str) -> None:
    """Writes every recording (WAV plus labels) and manifest.json into out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for rec in recordings:
        check_recording(rec)
        wav = out_dir / f"{rec.name}.wav"
        write_wav(wav, generate(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed), rec.sample_rate)
        extra = {"recording": rec.name, "group": rec.group, "oracle": rec.oracle}
        lab = labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
        wav.with_suffix(".json").write_text(json.dumps({**lab, **extra}, indent=2) + "\n")
        entry = {"name": rec.name, "group": rec.group, "oracle": rec.oracle, "wav": wav.name,
                 "labels": wav.with_suffix(".json").name, "station_labels": None}
        if rec.station_labels:
            per_station = station_labels(rec.specs, rec.sample_rate, rec.duration_s, seed=rec.noise_seed)
            path = out_dir / f"{rec.name}.stations.json"
            path.write_text(json.dumps({**per_station, **extra}, indent=2) + "\n")
            entry["station_labels"] = path.name
        entries.append(entry)
        print(f"wrote {wav.name}")
    (out_dir / "manifest.json").write_text(json.dumps({"suite": suite_name, "recordings": entries}, indent=2) + "\n")


def _scorings(rec: dict) -> list[tuple[str, str, str]]:
    """(labels file, result name, group) for each way a recording is scored."""
    out = [(rec["labels"], rec["name"], rec["group"])]
    if rec.get("station_labels"):
        out.append((rec["station_labels"], f"{rec['name']}.stations", f"{rec['group']} (per station)"))
    return out


def run_suite(out_dir: Path, bench: Path, front_ends) -> None:
    """Scores every recording of the manifest with kz4ap-bench, once per front end (and
    once more per front end against per-station labels where the manifest has them)."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for fe in front_ends:
        if fe not in FRONT_ENDS:
            raise ValueError(f"unknown front end {fe!r}")
        results = out_dir / "results" / fe
        results.mkdir(parents=True, exist_ok=True)
        for rec in manifest["recordings"]:
            for label_file, result, _group in _scorings(rec):
                cmd = [str(bench), str(out_dir / rec["wav"]), "--labels", str(out_dir / label_file),
                       "--json", str(results / f"{result}.json")]
                if rec["oracle"]:
                    cmd.append("--oracle")
                cmd += ["--front-end", BENCH_FRONT_END[fe]]
                done = subprocess.run(cmd, capture_output=True, text=True)
                if done.returncode != 0:
                    raise RuntimeError(f"kz4ap-bench failed on {result} ({fe}):\n{done.stderr}")
                out_lines = done.stdout.strip().splitlines()
                if not out_lines:
                    raise RuntimeError(f"kz4ap-bench printed nothing on {result} ({fe}) although it exited 0; "
                                       f"stderr:\n{done.stderr}")
                print(f"{fe:9s} {result}: {out_lines[-1]}")


def crossing_snr(points, threshold: float) -> float | None:
    """S500 (dB) at which CER falls to threshold. points: (S500 dB, CER) pairs. Scanning
    down from the highest S500, the first point above threshold and the point above it
    bracket the crossing (linear interpolation in dB). None if the highest point already
    fails; the lowest S500 if no point fails (an upper bound)."""
    pts = sorted(points)
    if not pts or pts[-1][1] > threshold:
        return None
    for i in range(len(pts) - 1, 0, -1):
        lo, hi = pts[i - 1], pts[i]
        if lo[1] > threshold:
            return lo[0] + (lo[1] - threshold) / (lo[1] - hi[1]) * (hi[0] - lo[0])
    return pts[0][0]


def _ratio(num, den) -> float:
    return num / den if den else 0.0


def _rng_for(key) -> np.random.Generator:
    """A generator seeded by the group's key, so every interval is reproducible."""
    return np.random.default_rng(zlib.crc32(repr(key).encode()))


def _interval(values) -> tuple[float, float] | None:
    """The central 95% of bootstrap values; None unless at least 95% of resamples gave one."""
    kept = [v for v in values if v is not None]
    if len(kept) < 0.95 * len(values):
        return None
    return float(np.percentile(kept, 2.5)), float(np.percentile(kept, 97.5))


def bootstrap_cer(signals, rng) -> tuple[float, float] | None:
    """95% interval of pooled CER (summed edits over summed symbols) by resampling signals
    with replacement: errors cluster within a signal, so signals, not symbols, are the units."""
    if not signals:
        return None
    edits = np.array([e for e, _ in signals], dtype=float)
    syms = np.array([s for _, s in signals], dtype=float)
    picks = rng.integers(len(signals), size=(BOOTSTRAP_RESAMPLES, len(signals)))
    return _interval([_ratio(edits[p].sum(), syms[p].sum()) for p in picks])


def bootstrap_crossing(by_snr, threshold: float, rng) -> tuple[float, float] | None:
    """95% interval of crossing_snr, resampling signals within each S500 point."""
    snrs = sorted(by_snr)
    values = []
    for _ in range(BOOTSTRAP_RESAMPLES):
        points = []
        for snr in snrs:
            sig = by_snr[snr]
            p = rng.integers(len(sig), size=len(sig))
            points.append((snr, _ratio(sum(sig[i][0] for i in p), sum(sig[i][1] for i in p))))
        values.append(crossing_snr(points, threshold))
    return _interval(values)


def aggregate(rows) -> dict:
    """Pools scored signals by (front end, group, tag); rates are summed edits over summed
    symbols, each with a bootstrap 95% interval over signals."""
    groups: dict = {}
    for r in rows:
        if not r["scored"]:
            continue
        key = (r["front_end"], r["group"], r["tag"])
        g = groups.setdefault(key, {"signals": 0, "detected": 0, "by_snr": {}, "freq_errors": [],
                                    "beyond_oracle_anchor": False, **{k: 0 for k in COUNT_KEYS}})
        g["signals"] += 1
        g["beyond_oracle_anchor"] |= bool(r.get("beyond_oracle_anchor"))
        g["detected"] += int(r["detected"])
        for k in COUNT_KEYS:
            g[k] += r[k]
        g["by_snr"].setdefault(r["snr_db"], []).append((r["edits"], r["symbols"]))
        if r.get("freq_error_hz") is not None:
            g["freq_errors"].append(r["freq_error_hz"])
    out = {}
    for key, g in groups.items():
        rng = _rng_for(key)
        points = [(snr, _ratio(sum(e for e, _ in sig), sum(s for _, s in sig)))
                  for snr, sig in sorted(g["by_snr"].items())]
        # A crossing needs a designed sweep: at least three S500 points, each with at least
        # MIN_SIGNALS_PER_POINT signals (groups that draw S500 per signal have one per point).
        crossing = len(points) >= 3 and all(len(sig) >= MIN_SIGNALS_PER_POINT for sig in g["by_snr"].values())
        out[key] = {
            "signals": g["signals"],
            "detected": g["detected"],
            "cer": _ratio(g["edits"], g["symbols"]),
            "cer_interval": (bootstrap_cer([s for sig in g["by_snr"].values() for s in sig], rng)
                             if g["signals"] >= MIN_SIGNALS_FOR_INTERVAL else None),
            "char_cer": _ratio(g["char_edits"], g["chars"]),
            "space_error_rate": _ratio(g["space_edits"], g["spaces"]),
            "first_word_cer": _ratio(g["first_word_edits"], g["first_word_symbols"]),
            "nospace_cer": _ratio(g["nospace_edits"], g["nospace_symbols"]),
            "cer_by_snr": points,
            "min_symbols_per_point": min(sum(s for _, s in sig) for sig in g["by_snr"].values()),
            "snr_at_cer": {f"{t:g}": crossing_snr(points, t) for t in CER_THRESHOLDS} if crossing else {},
            "snr_at_cer_interval": ({f"{t:g}": bootstrap_crossing(g["by_snr"], t, rng) for t in CER_THRESHOLDS}
                                    if crossing else {}),
            # True where no point fails: the reported crossing is the lowest point, an upper bound
            "snr_at_cer_upper_bound": ({f"{t:g}": all(c <= t for _, c in points) for t in CER_THRESHOLDS}
                                       if crossing else {}),
            "freq_error_hz_median": float(np.median(g["freq_errors"])) if g["freq_errors"] else None,
            "beyond_oracle_anchor": g["beyond_oracle_anchor"],
        }
    return out


def paired_differences(rows) -> dict:
    """Matched minus baseline CER, signal by signal on the same recordings, pooled by
    (group, tag): mean difference and its bootstrap 95% interval over signals."""
    by_front_end: dict = {}
    for r in rows:
        if r["scored"]:
            by_front_end.setdefault(r["front_end"], {})[(r["group"], r["tag"], r["recording"], r["index"])] = r
    base, matched = by_front_end.get("baseline", {}), by_front_end.get("matched", {})
    diffs: dict = {}
    limited: dict = {}
    for key in sorted(set(base) & set(matched)):
        b, m = base[key], matched[key]
        diffs.setdefault(key[:2], []).append(_ratio(m["edits"], m["symbols"]) - _ratio(b["edits"], b["symbols"]))
        limited[key[:2]] = limited.get(key[:2], False) or bool(m.get("beyond_oracle_anchor"))
    out = {}
    for key, d in diffs.items():
        values = np.array(d)
        picks = _rng_for(key).integers(len(values), size=(BOOTSTRAP_RESAMPLES, len(values)))
        out[key] = {"signals": len(values), "mean": float(values.mean()), "beyond_oracle_anchor": limited[key],
                    "interval": (_interval([float(values[p].mean()) for p in picks])
                                 if len(values) >= MIN_SIGNALS_FOR_INTERVAL else None)}
    return out


def _label_truth_hz(label: dict) -> float:
    """The frequency the latest text came from: a QSO's last over's sender's carrier."""
    transmissions = label.get("transmissions") or []
    return label["freq_offset_hz"] + (transmissions[-1].get("offset_hz", 0.0) if transmissions else 0.0)


def _excursion_hz(label: dict) -> float:
    """How far the label's sound gets from its labeled frequency, Hz: the drift over the
    signal's length, or a QSO's answering station's offset."""
    drift = abs(label.get("drift_hz_per_s") or 0.0) * (label.get("end_s", label["start_s"]) - label["start_s"])
    offsets = [abs(s.get("offset_hz", 0.0)) for s in label.get("senders") or []]
    return max([drift] + offsets)


def beyond_oracle_anchor(oracle: bool, label: dict) -> bool:
    """Whether an oracle channel's fixed anchor (the label, +/-ORACLE_ANCHOR_RANGE_HZ) cannot
    reach the label's sound, so the Matched front end's result on it is not meaningful."""
    return oracle and _excursion_hz(label) > ORACLE_ANCHOR_RANGE_HZ


def _anchor_limited(front_end: str, v: dict) -> bool:
    return front_end == "matched" and v.get("beyond_oracle_anchor", False)


def load_results(out_dir: Path):
    """Per-signal rows and per-recording timings from every results/<front end>/ directory."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows, timings = [], []
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            for label_file, result_name, group in _scorings(rec):
                path = fe_dir / f"{result_name}.json"
                if not path.exists():
                    continue
                result = json.loads(path.read_text())
                label_signals = json.loads((out_dir / label_file).read_text())["signals"]
                for sig in result["score"]["signals"]:
                    label = label_signals[sig["index"]]
                    tracked = sig.get("tracked_freq_hz")
                    freq_error = (abs(tracked - _label_truth_hz(label))
                                  if tracked is not None and not label.get("drift_hz_per_s") else None)
                    rows.append({"front_end": fe_dir.name, "recording": result_name, "group": group,
                                 "tag": label.get("tag", ""), "index": sig["index"], "snr_db": sig["snr_db"],
                                 "scored": sig["scored"], "detected": sig["track_id"] is not None,
                                 "freq_error_hz": freq_error,
                                 "beyond_oracle_anchor": beyond_oracle_anchor(rec["oracle"], label),
                                 **{k: sig[k] for k in COUNT_KEYS}})
                if result_name == rec["name"]:
                    timing = result.get("timing", {})
                    channel_s = result.get("channel_seconds", 0.0)
                    timings.append({"front_end": fe_dir.name, "channel_seconds": channel_s,
                                    "cpu_s": timing.get("cpu_s", 0.0),
                                    "decoder_s": timing.get("decoder_ms_per_channel_s", 0.0) * channel_s / 1000.0})
    return rows, timings


def over_rows(out_dir: Path) -> list[dict]:
    """One row per over of every scored QSO (labels whose transmissions name a sender):
    the over's reference symbols and the edits the bench charged to them (its
    per-transmission counts, so the bench's own alignment decides)."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    rows = []
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            path = fe_dir / f"{rec['name']}.json"
            if not path.exists():
                continue
            label_signals = json.loads((out_dir / rec["labels"]).read_text())["signals"]
            for sig in json.loads(path.read_text())["score"]["signals"]:
                label = label_signals[sig["index"]]
                overs = label.get("transmissions", [])
                if not sig["scored"] or not overs or "sender" not in overs[0]:
                    continue
                for k, (over, counts) in enumerate(zip(overs, sig["transmissions"])):
                    rows.append({"front_end": fe_dir.name, "group": rec["group"],
                                 "tag": label_signals[sig["index"]].get("tag", ""), "over": k,
                                 "sender": over["sender"], "keying": over["keying"], "wpm": over["wpm"],
                                 "symbols": counts["symbols"], "edits": counts["edits"],
                                 "beyond_oracle_anchor": beyond_oracle_anchor(rec["oracle"], label)})
    return rows


def aggregate_overs(rows) -> dict:
    """Per-over CER pooled by (front end, group, keying style of the over's sender)."""
    groups: dict = {}
    for r in rows:
        g = groups.setdefault((r["front_end"], r["group"], r["keying"]),
                              {"overs": 0, "symbols": 0, "edits": 0, "beyond_oracle_anchor": False})
        g["overs"] += 1
        g["beyond_oracle_anchor"] |= bool(r.get("beyond_oracle_anchor"))
        g["symbols"] += r["symbols"]
        g["edits"] += r["edits"]
    return {k: {**g, "cer": _ratio(g["edits"], g["symbols"])} for k, g in groups.items()}


def track_splits(out_dir: Path) -> dict:
    """Group H through the detector: for each QSO label, the tracks that decoded text within
    25 Hz of either station's carrier (birth frequency), averaged by (front end, tag).
    1 means the QSO stayed one track; 2 means it split into one per station."""
    manifest = json.loads((out_dir / "manifest.json").read_text())
    counts: dict = {}
    for fe_dir in sorted(p for p in (out_dir / "results").iterdir() if p.is_dir()):
        for rec in manifest["recordings"]:
            path = fe_dir / f"{rec['name']}.json"
            if rec["oracle"] or not rec.get("station_labels") or not path.exists():
                continue
            tracks = [t for t in json.loads(path.read_text()).get("tracks", []) if t["text"].strip()]
            for label in json.loads((out_dir / rec["labels"]).read_text())["signals"]:
                carriers = [label["freq_offset_hz"] + s["offset_hz"] for s in label.get("senders", [])]
                near = sum(any(abs(t["freq_hz"] - c) <= 25.0 for c in carriers) for t in tracks)
                counts.setdefault((fe_dir.name, label.get("tag", "")), []).append(near)
    return {k: {"qsos": len(v), "mean_tracks": float(np.mean(v))} for k, v in counts.items()}


def cpu_summary(timings) -> dict:
    out: dict = {}
    for t in timings:
        c = out.setdefault(t["front_end"], {"channel_seconds": 0.0, "cpu_s": 0.0, "decoder_s": 0.0})
        for k in ("channel_seconds", "cpu_s", "decoder_s"):
            c[k] += t[k]
    return {fe: {"channel_seconds": c["channel_seconds"],
                 "cpu_ms_per_channel_s": 1000.0 * _ratio(c["cpu_s"], c["channel_seconds"]),
                 "decoder_ms_per_channel_s": 1000.0 * _ratio(c["decoder_s"], c["channel_seconds"])}
            for fe, c in out.items()}


def _tag_offset_hz(tag: str) -> float | None:
    """The answering station's offset named in a group-H tag ("..., offset 100 Hz"), Hz."""
    m = re.search(r"offset (-?\d+(?:\.\d+)?) Hz", tag)
    return float(m.group(1)) if m else None


def view_fits(group: str, tag: str) -> bool:
    """Whether a group-H scoring view fits the QSO. Through the detector, by its regime
    (qso_regime): labels per QSO for a QSO heard as one track, labels per station for one
    heard as two; ambiguous QSOs fit both. With oracle channels (one opened per label), by
    the channel's passband: the QSO label's channel, on the caller, holds the answering
    station when |offset| < ORACLE_CHANNEL_CUTOFF_HZ (its -6 dB point, measured), so the QSO
    view fits there and the station view fits otherwise."""
    if not group.startswith("H two-station QSO"):
        return True
    per_station = group.endswith("(per station)")
    offset = _tag_offset_hz(tag)
    if ", oracle" in group and offset is not None:
        inside = abs(offset) < ORACLE_CHANNEL_CUTOFF_HZ
        return not inside if per_station else inside
    if per_station:
        return not tag.startswith("same-track")
    return not tag.startswith("separate-track")


def _db(x) -> str:
    return "—" if x is None else f"{x:.1f}"


def _with_interval(value, interval, fmt: str) -> str:
    if value is None:
        return "—"
    if interval is None:
        return f"{value:{fmt}}"
    return f"{value:{fmt}} ({interval[0]:{fmt}}–{interval[1]:{fmt}})"


def _crossing_cell(v: dict, threshold: str) -> str:
    """A crossing with its interval, or "≤ x" where no point fails (the lowest point, an upper bound)."""
    value = v["snr_at_cer"].get(threshold)
    if value is not None and v.get("snr_at_cer_upper_bound", {}).get(threshold):
        return f"≤ {value:.1f}"
    return _with_interval(value, v["snr_at_cer_interval"].get(threshold), ".1f")


def format_markdown(agg: dict, cpu: dict, overs: dict | None = None, paired: dict | None = None,
                    splits: dict | None = None) -> str:
    lines = ["# Benchmark summary", "",
             "S₅₀₀: key-down carrier power over noise power in 500 Hz, dB. CER counts word spaces; "
             "character CER and space error rate split its edits; first-word CER scores the first word "
             "of each transmission (each over, for a QSO); no-space CER is VE3NEA's metric (Levenshtein "
             "distance with spaces removed). First-word CER (and the per-over CER below) is an upper "
             "bound, not an exact attribution: an edit the alignment could place in more than one "
             "position is charged to the earliest, and insertions decoded before a transmission are "
             "charged to its first word, so it can exceed 1 (docs/signal-processing.md, section 11). "
             "Parentheses: bootstrap 95% interval over signals; none is printed for a row with fewer "
             "than 2 signals. S₅₀₀ at a CER threshold is computed only for a sweep of at least three "
             "S₅₀₀ points with at least 2 signals each; groups that draw S₅₀₀ per signal (band, crowded) "
             "have one signal per point and show —. ≤ x : no point fails, so x, the lowest point, is an "
             "upper bound. — : not reached, or no sweep. "
             f"{ANCHOR_NOTE}: a Matched row of an oracle recording in which some label's sound gets more than "
             f"{ORACLE_ANCHOR_RANGE_HZ:g} Hz from its labeled frequency (a drift over the signal's length, or a "
             "QSO's answering station): with no detector, the tracker's anchor is the label and it fine-tunes "
             f"only within ±{ORACLE_ANCHOR_RANGE_HZ:g} Hz of it. † : this group-H view does not fit the QSO. "
             "Through the detector, by its regime: labels per station for a same-track QSO, where both "
             "match one track and each is charged the other's text; labels per QSO for a separate-track "
             "QSO, where the caller's track lacks the answering station's overs. With oracle channels, by "
             "the channel's passband (measured: −1.17 dB relative to the passband at 100 Hz from its "
             "center, −6.02 dB at 150 Hz, −18.0 dB at 200 Hz): labels per QSO fit when the answering "
             "station is less than 150 Hz from the caller (inside the −6 dB point), labels per station "
             "otherwise. Read the other view.", ""]
    for group in sorted({k[1] for k in agg}):
        lines += [f"## {group}", "",
                  "| tag | front end | signals | detected | CER | character CER | space error rate | "
                  "first-word CER | no-space CER | fewest symbols at one S₅₀₀ point | "
                  "S₅₀₀ at CER 0.10 (dB) | S₅₀₀ at CER 0.05 (dB) | median frequency error (Hz) |",
                  "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
        for (fe, g, tag), v in sorted(agg.items(), key=lambda kv: (kv[0][2], kv[0][0])):
            if g != group:
                continue
            mark = ("" if view_fits(g, tag) else " †") + (f" ({ANCHOR_NOTE})" if _anchor_limited(fe, v) else "")
            lines.append(f"| {tag}{mark} | {fe} | {v['signals']} | {v['detected']} | "
                         f"{_with_interval(v['cer'], v['cer_interval'], '.3f')} | "
                         f"{v['char_cer']:.3f} | {v['space_error_rate']:.3f} | {v['first_word_cer']:.3f} | "
                         f"{v['nospace_cer']:.3f} | {v['min_symbols_per_point']} | "
                         f"{_crossing_cell(v, '0.1')} | {_crossing_cell(v, '0.05')} | "
                         f"{_db(v['freq_error_hz_median'])} |")
        lines.append("")
    if paired:
        lines += ["## Matched − baseline, paired by signal", "",
                  "Mean over signals of (Matched CER − baseline CER) on the same recordings; negative favors "
                  "Matched. An interval that contains 0 is no evidence either way.", "",
                  "| group | tag | signals | mean CER difference |", "|---|---|---|---|"]
        for (group, tag), v in sorted(paired.items()):
            note = f" ({ANCHOR_NOTE})" if v.get("beyond_oracle_anchor") else ""
            lines.append(f"| {group} | {tag}{note} | {v['signals']} | "
                         f"{_with_interval(v['mean'], v['interval'], '+.3f')} |")
        lines.append("")
    if overs:
        lines += ["## Per over", "",
                  "CER of each over (from its first to its last symbol) pooled by the sending station's "
                  "keying style. An upper bound, like first-word CER: ambiguous edits are charged to the "
                  "earliest position and insertions before an over to its first word, so it can exceed 1 "
                  "(docs/signal-processing.md, section 11).", "",
                  "| group | keying | front end | overs | CER |", "|---|---|---|---|---|"]
        for (fe, group, keying), v in sorted(overs.items(), key=lambda kv: (kv[0][1], kv[0][2], kv[0][0])):
            note = f" ({ANCHOR_NOTE})" if _anchor_limited(fe, v) else ""
            lines.append(f"| {group} | {keying}{note} | {fe} | {v['overs']} | {v['cer']:.3f} |")
        lines.append("")
    if splits:
        lines += ["## Tracks per QSO (group H, detector)", "",
                  "Tracks that decoded text within 25 Hz of either station's carrier; 1 = one track for the "
                  "QSO, 2 = one per station. Values above 2 mean a station's track dropped and was re-born "
                  "(about once per over), not false tracks.", "",
                  "| tag | front end | QSOs | mean tracks |", "|---|---|---|---|"]
        for (fe, tag), v in sorted(splits.items(), key=lambda kv: (kv[0][1], kv[0][0])):
            lines.append(f"| {tag} | {fe} | {v['qsos']} | {v['mean_tracks']:.2f} |")
        lines.append("")
    lines += ["## CPU", "",
              "| front end | channel-seconds (s) | CPU per channel-second (ms/s) | decoders per channel-second (ms/s) |",
              "|---|---|---|---|"]
    for fe, c in sorted(cpu.items()):
        lines.append(f"| {fe} | {c['channel_seconds']:.1f} | {c['cpu_ms_per_channel_s']:.3f} | "
                     f"{c['decoder_ms_per_channel_s']:.3f} |")
    return "\n".join(lines) + "\n"


def _intervals_json(v: dict) -> dict:
    return {**v, "cer_by_snr": [list(p) for p in v["cer_by_snr"]]}


def write_summary(out_dir: Path) -> None:
    rows, timings = load_results(out_dir)
    agg = aggregate(rows)
    cpu = cpu_summary(timings)
    overs = aggregate_overs(over_rows(out_dir))
    paired = paired_differences(rows)
    splits = track_splits(out_dir)
    summary = {
        # beyond_oracle_anchor is a Matched limit: set only on Matched rows, as the markdown marks them
        "groups": [{"front_end": fe, "group": g, "tag": tag, **_intervals_json(v),
                    "beyond_oracle_anchor": _anchor_limited(fe, v)}
                   for (fe, g, tag), v in sorted(agg.items())],
        "paired": [{"group": g, "tag": tag, **v} for (g, tag), v in sorted(paired.items())],
        "overs": [{"front_end": fe, "group": g, "keying": k, **v, "beyond_oracle_anchor": _anchor_limited(fe, v)}
                  for (fe, g, k), v in sorted(overs.items())],
        "track_splits": [{"front_end": fe, "tag": tag, **v} for (fe, tag), v in sorted(splits.items())],
        "cpu": cpu,
    }
    (out_dir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    (out_dir / "summary.md").write_text(format_markdown(agg, cpu, overs, paired, splits), encoding="utf-8")
    print(f"wrote {out_dir / 'summary.md'}")


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    g = sub.add_parser("generate", help="write a suite's recordings and manifest")
    g.add_argument("--suite", choices=sorted(SUITES), required=True)
    g.add_argument("--seeds", type=int, default=1)
    g.add_argument("--out", type=Path, required=True)
    r = sub.add_parser("run", help="score every recording with kz4ap-bench")
    r.add_argument("--out", type=Path, required=True)
    r.add_argument("--bench", type=Path, required=True)
    r.add_argument("--front-end", dest="front_ends", action="append", choices=FRONT_ENDS)
    s = sub.add_parser("summarize", help="write summary.json and summary.md")
    s.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == "generate":
        write_suite(SUITES[args.suite](args.seeds), args.out, args.suite)
    elif args.command == "run":
        run_suite(args.out, args.bench, args.front_ends or ["baseline"])
    else:
        write_summary(args.out)


if __name__ == "__main__":
    main()
