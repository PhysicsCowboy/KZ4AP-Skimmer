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
from pathlib import Path
from typing import Callable

import numpy as np

from . import bank, detect, noise, testsignals
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


# name of the JSON file (without extension) -> function producing its content.
# Later tasks register golden_<module> here.
GOLDEN: dict[str, Callable[[ProtoConfig], dict]] = {
    "config": golden_config,
    "filters": golden_filters,
    "noise": golden_noise,
    "keying": golden_keying,
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
