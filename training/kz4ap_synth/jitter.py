"""Jittered grids for the development set (docs/plans/2026-10-06-development-set-redesign.md).

A condition's range is divided into contiguous cells; each signal's value is drawn at random within
its cell (uniform in the value for a "linear" scale, uniform in its natural logarithm for "log"), with
a recorded seed. Every region gets equal coverage, no two signals share a value exactly, and nothing
sits only on grid points (the owner's jittered grid, principle 1).

Cells are numbered from 1, as in the plan's tables: cell k spans edges[k - 1] to edges[k]. Every
drawn value is recorded with its cell number and the cell's range (`record`), so a label file alone
says where each value came from.
"""

from __future__ import annotations

import math
from dataclasses import dataclass

import numpy as np

SCALES = ("linear", "log")


@dataclass(frozen=True)
class Cells:
    """Contiguous cells between increasing edges. scale "log": values drawn uniformly in ln(value)
    (edges must be positive); "linear": uniformly in the value. unit: the values' unit, for labels."""
    edges: tuple[float, ...]
    scale: str = "linear"
    unit: str = ""

    def __post_init__(self) -> None:
        if self.scale not in SCALES:
            raise ValueError(f"unknown scale {self.scale!r}")
        if len(self.edges) < 2:
            raise ValueError("cells need at least two edges")
        if any(b <= a for a, b in zip(self.edges, self.edges[1:])):
            raise ValueError(f"edges must increase: {self.edges}")
        if self.scale == "log" and self.edges[0] <= 0:
            raise ValueError("log-scale edges must be positive")

    def __len__(self) -> int:
        return len(self.edges) - 1

    @property
    def numbers(self) -> range:
        """The cell numbers, 1 ... len(self)."""
        return range(1, len(self) + 1)

    def bounds(self, index: int) -> tuple[float, float]:
        """Cell `index`'s (lower, upper) edges; index counts from 1."""
        if not 1 <= index <= len(self):
            raise IndexError(f"cell {index} outside 1 ... {len(self)}")
        return self.edges[index - 1], self.edges[index]

    def center(self, index: int) -> float:
        """The cell's center on its scale: the geometric mean of its edges for "log", the mean for "linear"."""
        lo, hi = self.bounds(index)
        return math.sqrt(lo * hi) if self.scale == "log" else 0.5 * (lo + hi)

    def cell_of(self, value: float) -> int | None:
        """The number of the cell holding value (an edge belongs to the cell above it, the top edge to the
        top cell); None outside the range."""
        if not self.edges[0] <= value <= self.edges[-1]:
            return None
        for k in self.numbers:
            if value < self.edges[k]:
                return k
        return len(self)


def draw_between(lo: float, hi: float, scale: str, rng: np.random.Generator) -> float:
    """A value uniform within [lo, hi] (in ln for "log"), clamped to the interval against rounding."""
    if scale == "log":
        value = math.exp(math.log(lo) + float(rng.uniform()) * (math.log(hi) - math.log(lo)))
    elif scale == "linear":
        value = lo + float(rng.uniform()) * (hi - lo)
    else:
        raise ValueError(f"unknown scale {scale!r}")
    return min(max(value, lo), hi)


def draw(cells: Cells, index: int, rng: np.random.Generator, within: tuple[float, float] | None = None) -> float:
    """A value uniform within cell `index` (uniform in ln for a "log" scale). within: draw only from the part of
    the cell inside this interval (a group whose other condition makes part of the cell infeasible)."""
    lo, hi = cells.bounds(index)
    if within is not None:
        lo, hi = max(lo, within[0]), min(hi, within[1])
        if hi < lo:
            raise ValueError(f"cell {index} {cells.bounds(index)} does not meet {within}")
    return draw_between(lo, hi, cells.scale, rng)


def record(cells: Cells, index: int, value: float, **extra) -> dict:
    """A drawn value as the label file records it: the value, its cell number, the cell's range, scale and unit,
    and any extra fields (e.g. drawn_within)."""
    lo, hi = cells.bounds(index)
    return {"value": value, "cell": index, "range": [lo, hi], "scale": cells.scale, "unit": cells.unit, **extra}


def pick(cells: Cells, index: int, rng: np.random.Generator,
         within: tuple[float, float] | None = None) -> tuple[float, dict]:
    """draw() and record() together: (value, its record)."""
    value = draw(cells, index, rng, within)
    extra = {"drawn_within": [max(cells.bounds(index)[0], within[0]), min(cells.bounds(index)[1], within[1])]} \
        if within is not None else {}
    return value, record(cells, index, value, **extra)


# The plan's speed cells (principle 2): 10 cells from 8 to 80 WPM, evenly spaced in ln WPM, each a factor
# 10^(1/10) = 1.259 wide; edges 8 * 10^(i/10) WPM, i = 0 ... 10.
SPEED_CELLS = Cells(tuple(8.0 * 10.0 ** (i / 10.0) for i in range(11)), "log", "WPM")
# The plan's S500 cells (principle 3): 14 contiguous cells 2 dB wide from -8 to +20 dB (dB SNR in 500 Hz).
SNR_CELLS = Cells(tuple(float(x) for x in range(-8, 21, 2)), "linear", "dB SNR in 500 Hz")
