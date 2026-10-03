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

from .params import ProtoConfig


def golden_config(cfg: ProtoConfig) -> dict:
    """The settled configuration, field by field (tuples become lists)."""
    return dataclasses.asdict(cfg)


# name of the JSON file (without extension) -> function producing its content.
# Later tasks register golden_<module> here.
GOLDEN: dict[str, Callable[[ProtoConfig], dict]] = {
    "config": golden_config,
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
