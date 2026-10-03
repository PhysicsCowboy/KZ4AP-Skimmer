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

    from kz4ap_synth.messages import random_text
    from kz4ap_synth.morse import keying_intervals

    rate = 1500.0
    n = bank.branch_samples(bank.branch_lengths_s(cfg), rate)
    iv = keying_intervals(random_text(np.random.default_rng(4), 60), 25.0)
    u = np.convolve(testsignals.stream(iv, 1.0, 20.0, 15.0, 21, rate), testsignals.lowpass(rate), mode="same")
    u = np.asarray(u, np.complex128)
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


# name of the JSON file (without extension) -> function producing its content.
# Later tasks register golden_<module> here.
GOLDEN: dict[str, Callable[[ProtoConfig], dict]] = {
    "config": golden_config,
    "filters": golden_filters,
    "noise": golden_noise,
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
