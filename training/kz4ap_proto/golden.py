"""Golden values: the prototype's outputs on fixed, seeded inputs, written as small JSON files that the
C++ port's tests read (engine/tests/data/bank/<name>.json) and must reproduce (continuous values to
relative 1e-9, discrete values exactly).

Run (from the repository root):  PYTHONPATH=training python -m kz4ap_proto.golden --out engine/tests/data/bank

Each module of the decoder gets one function, golden_<module>(cfg) -> dict (JSON-serializable), added to
GOLDEN below; write_all writes every registered function's dict to <out_dir>/<module>.json."""

from __future__ import annotations

import argparse
import dataclasses
import json
import math
from pathlib import Path
from typing import Callable

import numpy as np

from . import bank, detect, fit, noise, testsignals
from .params import ProtoConfig


def golden_config(cfg: ProtoConfig) -> dict:
    """The settled configuration, field by field (tuples become lists)."""
    return dataclasses.asdict(cfg)


def golden_filters(cfg: ProtoConfig) -> dict:
    """bank.py and detect.py on fixed inputs: the ladder, the boxcar of a seeded complex sequence (numpy
    default_rng(7), 400 samples, real and imaginary parts standard normal, drawn as one (400, 2) array), the
    boxcar power response, ln I0 and the envelope log-likelihood ratio on a 5 x 5 grid."""
    lengths = bank.branch_lengths_s(cfg)
    rng = np.random.default_rng(7)
    ri = rng.standard_normal((400, 2))
    u = ri[:, 0] + 1j * ri[:, 1]
    out: dict = {
        "branch_lengths_s": lengths.tolist(),
        "boxcar_input_re": u.real.tolist(),
        "boxcar_input_im": u.imag.tolist(),
    }
    for rate in (1500, 2000):
        out[f"branch_samples_{rate}"] = bank.branch_samples(lengths, rate).tolist()
        out[f"realized_lengths_s_{rate}"] = bank.realized_lengths_s(cfg, rate).tolist()
    for n in (14, 276):
        v = bank.boxcar(u, n)
        out[f"boxcar_{n}_re"] = v.real.tolist()
        out[f"boxcar_{n}_im"] = v.imag.tolist()
    f_hz = [0.0, 10.0, 50.0, 150.0]
    out["power_response_f_hz"] = f_hz
    out["power_response_n14_1500"] = bank.power_response(f_hz, 14, 1500.0).tolist()
    z = [0.0, 0.5, 3.74, 3.76, 10.0, 50.0]
    out["log_bessel_i0_z"] = z
    out["log_bessel_i0"] = detect.log_bessel_i0(z).tolist()
    xs = [0.0, 0.5, 1.0, 2.0, 4.0]
    as_ = [0.5, 1.0, 2.0, 5.0, 12.0]
    out["envelope_llr_x"] = xs
    out["envelope_llr_a"] = as_
    # row-major: index i * 5 + j is x = xs[i], a = as_[j]
    out["envelope_llr"] = [float(detect.envelope_llr(x, a)) for x in xs for a in as_]
    return out


def _noise_stream(rate: float) -> np.ndarray:
    """golden_noise's stream (also golden_keying's input): 20 s of testsignals.stream, a 1 FS carrier keyed at
    25 WPM by kz4ap_synth's random_text(default_rng(4), 60) from 1.0 s, S500 = 15 dB, seed 21, through the
    channel filter's shape (testsignals.lowpass, np.convolve mode="same"); complex128, FS."""
    from kz4ap_synth.messages import random_text
    from kz4ap_synth.morse import keying_intervals

    iv = keying_intervals(random_text(np.random.default_rng(4), 60), 25.0)
    u = np.convolve(testsignals.stream(iv, 1.0, 20.0, 15.0, 21, rate), testsignals.lowpass(rate), mode="same")
    return np.asarray(u, np.complex128)


def golden_noise(cfg: ProtoConfig) -> dict:
    """noise.py driven exactly as ChannelDecoder.run drives it, at 1500 samples/s: the stream is 20 s of
    testsignals.stream (a 1 FS carrier keyed at 25 WPM by kz4ap_synth's random_text(default_rng(4), 60) from
    1.0 s, S500 = 15 dB, seed 21) through the channel filter's shape (testsignals.lowpass, np.convolve
    mode="same"), so the noise is channel-shaped. P = |boxcar(u, N_k)|^2 stored as float32 (as run stores it);
    the C++ test recomputes P from u. Each method ("spectrum", "spectrum-level", "branch") is updated block by
    block (block_s) and its sigma2() (FS^2 per real component, one per branch) is recorded after every 10th
    block (block index b with b % 10 == 9), with n1 (samples) and, for the spectrum methods, the accepted and
    offered segment counts. guard_mean is recorded at three kappa."""
    import dataclasses as dc

    rate = 1500.0
    n = bank.branch_samples(bank.branch_lengths_s(cfg), rate)
    u = _noise_stream(rate)
    total = len(u)
    P = np.stack([np.abs(bank.boxcar(u, int(k))) ** 2 for k in n]).astype(np.float32)
    block = max(1, int(round(cfg.block_s * rate)))
    out: dict = {
        "rate_hz": rate,
        "u_re": u.real.tolist(),
        "u_im": u.imag.tolist(),
        "guard_mean_kappa": [1.0, 1.75, 4.0],
        "guard_mean": [noise.guard_mean(k) for k in (1.0, 1.75, 4.0)],
    }
    for method in ("spectrum", "spectrum-level", "branch"):
        est = noise.make_noise(dc.replace(cfg, noise_method=method), rate, n)
        snaps, n1s, accepted, offered = [], [], [], []
        for b, n0 in enumerate(range(0, total, block)):
            n1 = min(n0 + block, total)
            est.update(u, P, n0, n1)
            sigma2 = est.sigma2()
            if b % 10 == 9:
                snaps.append(sigma2.tolist())
                n1s.append(n1)
                if method != "branch":
                    accepted.append(est.segments)
                    offered.append(est.segments_offered)
        out[f"{method}_n1"] = n1s
        out[f"{method}_sigma2"] = snaps
        if method != "branch":
            out[f"{method}_segments"] = accepted
            out[f"{method}_segments_offered"] = offered
    return out


def _signed_edges(changes) -> list[int]:
    """(sample index n, key down after it) -> n + 1 for a key-down, -(n + 1) for a key-up."""
    return [(n + 1) if down else -(n + 1) for n, down in changes]


def golden_keying(cfg: ProtoConfig) -> dict:
    """keying.py driven by ChannelDecoder.run itself on golden_noise's stream (1500 samples/s, 20 s; the C++
    test reads u from noise.json and recomputes P as float32 and the default "spectrum" noise, as run does).
    run's BankKeyer is replaced by a subclass that records, changing nothing:
    - every block b (block_s): the branches' edges (keying.edges of step's key against `before`, as run
      computes them), all blocks concatenated per branch, each edge n + 1 (key down after sample n) or -(n + 1)
      (key up); `unknown` at step (one "0"/"1" per branch); and at each block with b % 10 == 9 or following a
      block with a start_over or finish_over_start call: step's a_k (dimensionless), the row sums of step's
      squelched posterior p (samples) and `weight` after step (samples);
    - every start_over and finish_over_start call, in run's order: [block, name, k, (finish only: amp2 FS^2,
      key_now)], then the keyer's weight (samples), amp2 (FS^2), prev_amp2 (FS^2, None for NaN) and
      ready_to_rekey of branch k just before the call (what run reads to decide).
    Plus keying.rekey on a fixed stretch: P of branch 14 over samples [0, 3008), the stretch of run's first
    re-key of that branch (block 93), at that block's sigma_14^2 (FS^2) and three amplitudes s^2 (FS^2): run's
    winner, a quarter of it, and 0 (squelched), as signed edges from key up; and keying.hysteresis on a hand
    case."""
    from . import channel, keying

    rate = 1500.0
    u = _noise_stream(rate)
    block = max(1, int(round(cfg.block_s * rate)))
    rec: dict = {"edges": None, "unknown": [], "a": [], "p_sum": [], "weight": [], "sampled_blocks": [],
                 "calls": [], "sigma2": []}
    state = {"b": -1, "n0": 0, "called_in": set()}

    class Recording(keying.BankKeyer):
        def step(self, P, sigma2):
            state["b"] += 1
            b, n0 = state["b"], state["n0"]
            if rec["edges"] is None:
                rec["edges"] = [[] for _ in range(len(self.amp2))]
            rec["unknown"].append("".join("1" if x else "0" for x in self.unknown))
            key, p, before, a = super().step(P, sigma2)
            for k, ch in enumerate(keying.edges(key, before, n0)):
                rec["edges"][k].extend(_signed_edges(ch))
            if b % 10 == 9 or (b - 1) in state["called_in"]:
                rec["sampled_blocks"].append(b)
                rec["a"].append(np.asarray(a, float).tolist())
                rec["p_sum"].append(np.asarray(p, float).sum(axis=1).tolist())
                rec["weight"].append(self.weight.tolist())
            rec["sigma2"].append(np.asarray(sigma2, float).copy())
            state["n0"] = n0 + np.asarray(P).shape[1]
            return key, p, before, a

        def _before_call(self, k):
            state["called_in"].add(state["b"])
            prev = float(self.prev_amp2[k])
            return [float(self.weight[k]), float(self.amp2[k]), None if np.isnan(prev) else prev,
                    bool(self.ready_to_rekey()[k])]

        def start_over(self, k):
            rec["calls"].append([state["b"], "start_over", int(k)] + self._before_call(k))
            super().start_over(k)

        def finish_over_start(self, k, amp2, key_now):
            rec["calls"].append([state["b"], "finish_over_start", int(k), float(amp2), bool(key_now)]
                                + self._before_call(k))
            super().finish_over_start(k, amp2, key_now)

    saved = channel.BankKeyer
    channel.BankKeyer = Recording
    try:
        text = channel.ChannelDecoder(cfg, rate).run(u).text
    finally:
        channel.BankKeyer = saved

    # rekey on run's first re-key stretch of branch 14: block 93, samples [0, 3008) (s = 0: the amplitude has
    # been unknown since the stream's start, and 3008 samples are within the 20 s reach).
    k, b_rekey = 14, 93
    first = next(c for c in rec["calls"] if c[1] == "finish_over_start" and c[2] == k)
    assert first[0] == b_rekey, first
    n = bank.branch_samples(bank.branch_lengths_s(cfg), rate)
    n1 = (b_rekey + 1) * block
    P = (np.abs(bank.boxcar(u, int(n[k]))) ** 2).astype(np.float32)[0:n1]
    sigma2 = float(rec["sigma2"][b_rekey][k])
    a_min = float(keying.BankKeyer(cfg, rate, n / rate).a_min[k])
    amps = [first[3], 0.25 * first[3], 0.0]
    rekeyed = [_signed_edges(keying.edges(keying.rekey(P, sigma2, a2, cfg, a_min)[None, :], np.zeros(1, bool),
                                          0)[0]) for a2 in amps]

    down = np.array([[0, 1, 0, 0, 1, 1, 0, 0, 0, 1], [0, 0, 0, 1, 0, 0, 0, 1, 0, 0]], bool)
    up = np.array([[0, 0, 1, 0, 1, 0, 0, 1, 0, 0], [1, 0, 0, 0, 1, 0, 0, 0, 1, 0]], bool)
    initial = np.array([False, True])
    return {
        "rate_hz": rate,
        "block_samples": block,
        "blocks": state["b"] + 1,
        "text": text,
        "edges": rec["edges"],
        "unknown": rec["unknown"],
        "sampled_blocks": rec["sampled_blocks"],
        "a": rec["a"],
        "p_sum": rec["p_sum"],
        "weight": rec["weight"],
        "calls": rec["calls"],
        "rekey_branch": k,
        "rekey_n1": n1,
        "rekey_sigma2": sigma2,
        "rekey_a_min": a_min,
        "rekey_amp2": amps,
        "rekey_edges": rekeyed,
        "hysteresis_down": down.astype(int).tolist(),
        "hysteresis_up": up.astype(int).tolist(),
        "hysteresis_initial": initial.astype(int).tolist(),
        "hysteresis": keying.hysteresis(down, up, initial).astype(int).tolist(),
    }


def _durations(intervals) -> list[tuple[bool, float]]:
    """(is_mark, duration s) for every mark and the space before it, in order (test_proto_fit.durations)."""
    out = []
    for i, (a, b) in enumerate(intervals):
        if i:
            out.append((False, a - intervals[i - 1][1]))
        out.append((True, b - a))
    return out


def _fit_sequence() -> list[tuple[bool, float, float]]:
    """golden_fit's 300 observations (is_mark, duration s, sigma_t^2 s^2): the marks and spaces of
    keying_intervals of random_text(default_rng(5), 5) at 25 WPM (89 observations), then
    random_text(default_rng(6), 4) at 15 WPM (71), then random_text(default_rng(7), 12) at 30 WPM (233; the
    sequence stops at 300), each later text preceded by a word gap of its own speed (7 T), each duration times exp(0.08 N(0, 1)) (timing jitter, default_rng(31)), with a tune-up carrier
    (space 0.5 s, mark 2.0 s, space 0.5 s) inserted after observation 60; sigma_t^2 = resolution_var_s2(0.8 T,
    a, 1500 samples/s), a uniform on [3, 30) from the same generator, T the segment's dit, s."""
    from kz4ap_synth.messages import random_text
    from kz4ap_synth.morse import keying_intervals

    rng = np.random.default_rng(31)
    raw: list[tuple[bool, float, float]] = []
    for seed, words, wpm in ((5, 5, 25.0), (6, 4, 15.0), (7, 12, 30.0)):
        t = 1.2 / wpm
        obs = _durations(keying_intervals(random_text(np.random.default_rng(seed), words), wpm))
        if raw:
            obs = [(False, 7.0 * t)] + obs
        raw += [(m, d, t) for m, d in obs]
    raw = raw[:60] + [(False, 0.5, 0.048), (True, 2.0, 0.048), (False, 0.5, 0.048)] + raw[60:]
    out = []
    for m, d, t in raw[:300]:
        jitter = math.exp(0.08 * rng.standard_normal())
        a = float(rng.uniform(3.0, 30.0))
        out.append((bool(m), float(d * jitter), fit.resolution_var_s2(0.8 * t, a, 1500.0)))
    assert len(out) == 300
    return out


def golden_fit(cfg: ProtoConfig) -> dict:
    """fit.py on _fit_sequence's 300 observations, added one by one to one DurationFit. After observations
    1, 2, 8, 48, 100 and 300 (snapshot s): weight (elements) and the retained history's length; grid_theta
    (theta = T, w, qT, T_g, s) and its grid indices (T, w, q, T_g) without a prior and with T_P = 0.05 s at
    weight 3; best (t_s, q, w_s, tg_s, quality, weight) for both; weighted_loglik (nats) at each grid_theta
    and best.theta(); classify_mark and classify_space of 20 probe durations (log-spaced 10 ms to 1 s,
    sigma_t^2 = 1e-6 s^2) under both bests; observations_loglik of the last (up to) 20 observations under
    both bests (nats per element). After 8, 48 and 300 only: the full mark table (T, q, w) and space table
    (T, w, T_g), row-major, nats. Also the class priors and the grid axes."""
    seq = _fit_sequence()
    marks, spaces = fit.class_priors()
    f = fit.DurationFit(cfg)
    probes = [float(x) for x in np.geomspace(0.01, 1.0, 20)]
    priors = {"none": (None, 0.0), "prior": (0.05, 3.0)}
    snaps = []
    for n, (m, d, v) in enumerate(seq, start=1):
        f.add(m, d, v)
        if n not in (1, 2, 8, 48, 100, 300):
            continue
        s: dict = {"n": n, "weight": f.weight, "history": len(f.history)}
        for name, (pt, pw) in priors.items():
            th = f.grid_theta(pt, pw)
            t = th[0]
            s[f"grid_theta_{name}"] = th.tolist()
            s[f"grid_index_{name}"] = [int(np.flatnonzero(f.t == t)[0]),
                                       int(np.flatnonzero(f.w * t == th[1])[0]),
                                       int(np.flatnonzero(f.q * t == th[2])[0]),
                                       int(np.flatnonzero(f.g * t == th[3])[0])]
            s[f"grid_loglik_{name}"] = f.weighted_loglik(th, pt, pw)
            b = f.best(pt, pw)
            s[f"best_{name}"] = [b.t_s, b.q, b.w_s, b.tg_s, b.quality, b.weight]
            s[f"best_loglik_{name}"] = f.weighted_loglik(b.theta(), pt, pw)
            s[f"classify_mark_{name}"] = [int(fit.classify_mark(b, p, 1e-6, cfg)) for p in probes]
            s[f"classify_space_{name}"] = [fit.classify_space(b, p, 1e-6, cfg) for p in probes]
            s[f"observations_loglik_{name}"] = fit.observations_loglik(b, seq[max(0, n - 20):n], cfg)
        if n in (8, 48, 300):
            s["mark_table"] = f.mark_table.ravel().tolist()
            s["space_table"] = f.space_table.ravel().tolist()
        snaps.append(s)
    return {
        "class_priors_marks": marks.tolist(),
        "class_priors_spaces": spaces.tolist(),
        "t_grid_s": f.t.tolist(),
        "q_grid": f.q.tolist(),
        "w_grid": f.w.tolist(),
        "tg_grid": f.g.tolist(),
        "lambda": f.lam,
        "history_capacity": f.history.maxlen,
        "obs_mark": "".join("1" if m else "0" for m, _, _ in seq),
        "obs_d_s": [d for _, d, _ in seq],
        "obs_var_t_s2": [v for _, _, v in seq],
        "probes_s": probes,
        "snapshots": snaps,
    }


def _model_durations(seed: int, words: int, cfg: ProtoConfig, wpm: float = 25.0) -> list[tuple[bool, float]]:
    """test_proto_fit.model_durations: machine-keyed medians with the fit's own log-normal scatter."""
    from kz4ap_synth.messages import random_text
    from kz4ap_synth.morse import keying_intervals

    rng = np.random.default_rng(seed)
    iv = keying_intervals(random_text(rng, words), wpm)
    out = []
    for i, (a, b) in enumerate(iv):
        if i:
            out.append((False, float((a - iv[i - 1][1]) * np.exp(cfg.sigma_ln_space * rng.standard_normal()))))
        out.append((True, float((b - a) * np.exp(cfg.sigma_ln_mark * rng.standard_normal()))))
    return out


def golden_fit_cases(cfg: ProtoConfig) -> dict:
    """The inputs of training/tests/test_proto_fit.py, which the C++ port of those tests replays (the synthesis
    library is Python only). Each case is "<name>_mark" (one "0"/"1" per observation) and "<name>_d_s"
    (durations, s), built exactly as the Python test builds it: text() = random_text(default_rng(1), 30);
    machine: keying_intervals(text(), 25 WPM); farnsworth: timed_intervals(text(), 18 WPM, farnsworth_wpm=10);
    key_weighting: timed_intervals(text(), 25 WPM, "machine", None, imbalance_dits=0.2); heavy_dahs:
    timed_intervals(random_text(default_rng(1), 60), 24 WPM, "hand", default_rng(2)); hi: keying_intervals("HI",
    12 WPM); speed_before / speed_after: random_text(default_rng(3), 30) at 20 WPM / random_text(default_rng(4),
    40) at 35 WPM (whole; the tests take prefixes); random: machine's kinds with durations exp(U(ln 0.01,
    ln 1.0)) from default_rng(5); model<words>_<seed>: model_durations(seed, words) (200 words, seeds 1-4; 30
    words, seeds 1-3); fast_paths: test_fast_paths_are_bit_identical_to_the_plain_formulas's 414 observations
    (default_rng(11)), with their sigma_t^2 (fast_paths_var_t_s2, s^2)."""
    from kz4ap_synth.keying import farnsworth_gap_s, timed_intervals
    from kz4ap_synth.messages import random_text
    from kz4ap_synth.morse import keying_intervals

    def text(seed=1, words=30):
        return random_text(np.random.default_rng(seed), words)

    cases: dict[str, list[tuple[bool, float]]] = {
        "machine": _durations(keying_intervals(text(), 25.0)),
        "farnsworth": _durations(timed_intervals(text(), 18.0, farnsworth_wpm=10.0)),
        "key_weighting": _durations(timed_intervals(text(), 25.0, "machine", None, imbalance_dits=0.2)),
        "heavy_dahs": _durations(timed_intervals(text(words=60), 24.0, "hand", np.random.default_rng(2))),
        "hi": _durations(keying_intervals("HI", 12.0)),
        "speed_before": _durations(keying_intervals(text(3), 20.0)),
        "speed_after": _durations(keying_intervals(text(4, 40), 35.0)),
    }
    rng = np.random.default_rng(5)
    machine = cases["machine"]
    cases["random"] = [(m, float(d)) for (m, _), d in
                       zip(machine, np.exp(rng.uniform(np.log(0.01), np.log(1.0), len(machine))))]
    for seed in range(1, 5):
        cases[f"model200_{seed}"] = _model_durations(seed, 200, cfg)
    for seed in range(1, 4):
        cases[f"model30_{seed}"] = _model_durations(seed, 30, cfg)
    rng = np.random.default_rng(11)
    fast, fast_var = [], []
    for i in range(2 * int(math.ceil(4 * cfg.fit_memory)) + 30):
        is_mark = i % 2 == 0
        d = 0.05 * float(rng.choice([1, 3] if is_mark else [1, 3, 7])) * math.exp(0.2 * rng.standard_normal())
        var_t = fit.resolution_var_s2(float(rng.choice([0.0093, 0.04, 0.184])), float(rng.uniform(0.5, 50.0)), 1500.0)
        fast.append((is_mark, d))
        fast_var.append(var_t)
    cases["fast_paths"] = fast
    out: dict = {"farnsworth_gap_s": farnsworth_gap_s(18.0, 10.0), "fast_paths_var_t_s2": fast_var}
    for name, obs in cases.items():
        out[f"{name}_mark"] = "".join("1" if m else "0" for m, _ in obs)
        out[f"{name}_d_s"] = [float(d) for _, d in obs]
    return out


# name of the JSON file (without extension) -> function producing its content.
# Later tasks register golden_<module> here.
GOLDEN: dict[str, Callable[[ProtoConfig], dict]] = {
    "config": golden_config,
    "filters": golden_filters,
    "noise": golden_noise,
    "keying": golden_keying,
    "fit": golden_fit,
    "fit_cases": golden_fit_cases,
}


def write_all(out_dir: Path, cfg: ProtoConfig | None = None) -> list[Path]:
    """Write every registered golden file into out_dir (created if needed); return the paths written."""
    cfg = ProtoConfig() if cfg is None else cfg
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, fn in GOLDEN.items():
        path = out_dir / f"{name}.json"
        # repr-exact doubles: json writes the shortest string that round-trips.
        path.write_text(json.dumps(fn(cfg), indent=1, sort_keys=True) + "\n", encoding="utf-8")
        paths.append(path)
    return paths


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="output directory")
    args = parser.parse_args()
    for path in write_all(args.out):
        print(path)


if __name__ == "__main__":
    main()
