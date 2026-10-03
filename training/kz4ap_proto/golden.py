"""Golden values: the prototype's outputs on fixed, seeded inputs, written as small JSON files that the
C++ port's tests read (engine/tests/data/bank/<name>.json) and must reproduce (continuous values to
relative 1e-9, discrete values exactly).

Run:  python -m kz4ap_proto.golden --out engine/tests/data/bank

Each module of the decoder gets one function, golden_<module>(cfg) -> dict (JSON-serializable), added to
GOLDEN below; write_all writes every registered function's dict to <out_dir>/<module>.json."""

from __future__ import annotations

import argparse
import dataclasses
import json
from pathlib import Path
from typing import Callable

import numpy as np

from . import bank, detect
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


# name of the JSON file (without extension) -> function producing its content.
# Later tasks register golden_<module> here.
GOLDEN: dict[str, Callable[[ProtoConfig], dict]] = {
    "config": golden_config,
    "filters": golden_filters,
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
