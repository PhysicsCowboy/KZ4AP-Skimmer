import math

import numpy as np
import pytest

from kz4ap_synth.jitter import SNR_CELLS, SPEED_CELLS, Cells, draw, pick, record

# The plan's speed-cell table (docs/plans/2026-10-06-development-set-redesign.md, principle 2): from, to, center, WPM.
PLAN_SPEED_TABLE = [(8.00, 10.07, 8.98), (10.07, 12.68, 11.30), (12.68, 15.96, 14.23), (15.96, 20.10, 17.91),
                    (20.10, 25.30, 22.55), (25.30, 31.85, 28.39), (31.85, 40.09, 35.73), (40.09, 50.48, 44.99),
                    (50.48, 63.55, 56.64), (63.55, 80.00, 71.30)]
# Chi-squared threshold for 19 degrees of freedom (20 bins) at the 99.9th percentile: a uniform draw fails it with
# probability 0.001; the seeded draws below are fixed, so the test is deterministic.
CHI2_19_P999 = 43.82


def test_speed_cells_are_the_plans_table():
    assert len(SPEED_CELLS) == 10 and SPEED_CELLS.scale == "log" and SPEED_CELLS.unit == "WPM"
    assert SPEED_CELLS.edges[0] == 8.0 and SPEED_CELLS.edges[-1] == pytest.approx(80.0, abs=1e-12)
    for k, (lo, hi, center) in enumerate(PLAN_SPEED_TABLE, start=1):
        got_lo, got_hi = SPEED_CELLS.bounds(k)
        assert (round(got_lo, 2), round(got_hi, 2), round(SPEED_CELLS.center(k), 2)) == (lo, hi, center)
        assert got_hi / got_lo == pytest.approx(10 ** 0.1, abs=1e-12)
        dit_lo, dit_hi = 1200.0 / got_hi, 1200.0 / got_lo  # ms, the table's dit column
        assert dit_lo < dit_hi


def test_snr_cells_are_2_db_wide_from_minus_8_to_plus_20():
    assert len(SNR_CELLS) == 14 and SNR_CELLS.scale == "linear"
    assert SNR_CELLS.edges == tuple(float(x) for x in range(-8, 21, 2))
    assert SNR_CELLS.unit == "dB SNR in 500 Hz"


@pytest.mark.parametrize("cells", [SPEED_CELLS, SNR_CELLS, Cells((0.05, 0.2, 0.6, 2.0, 5.0), "log")])
def test_the_cells_tile_the_range(cells):
    # (each upper edge is the next lower edge by construction, from one edges tuple; what can fail is cell_of)
    for k in cells.numbers:
        assert cells.cell_of(cells.bounds(k)[0]) == k  # an edge belongs to the cell above it
        assert cells.cell_of(cells.center(k)) == k
    assert cells.cell_of(cells.edges[-1]) == len(cells)
    assert cells.cell_of(cells.edges[0] - 1e-9) is None and cells.cell_of(cells.edges[-1] + 1e-9) is None


@pytest.mark.parametrize("cells", [SPEED_CELLS, SNR_CELLS, Cells((-0.1, 0.1)), Cells((0.0, 25.0, 60.0, 120.0))])
def test_every_draw_lies_inside_its_cell(cells):
    rng = np.random.default_rng(7)
    for k in cells.numbers:
        lo, hi = cells.bounds(k)
        values = [draw(cells, k, rng) for _ in range(500)]
        assert all(lo <= v <= hi for v in values)
        assert len(set(values)) == len(values)  # jittered: no two draws coincide


def test_a_draw_within_a_sub_interval_stays_in_both():
    rng = np.random.default_rng(3)
    lo, hi = SPEED_CELLS.bounds(9)
    values = [draw(SPEED_CELLS, 9, rng, within=(0.0, 80.0 / 1.3)) for _ in range(500)]
    assert all(lo <= v <= 80.0 / 1.3 for v in values)
    value, rec = pick(SPEED_CELLS, 9, np.random.default_rng(3), within=(0.0, 80.0 / 1.3))
    assert rec["drawn_within"] == [lo, 80.0 / 1.3] and rec["cell"] == 9 and rec["value"] == value
    with pytest.raises(ValueError):
        draw(SPEED_CELLS, 10, rng, within=(0.0, 80.0 / 1.3))


def test_draws_are_reproducible_by_seed():
    a = [draw(SPEED_CELLS, k, np.random.default_rng([1, k])) for k in SPEED_CELLS.numbers]
    b = [draw(SPEED_CELLS, k, np.random.default_rng([1, k])) for k in SPEED_CELLS.numbers]
    c = [draw(SPEED_CELLS, k, np.random.default_rng([2, k])) for k in SPEED_CELLS.numbers]
    assert a == b and a != c


def _chi2_uniform(u, bins: int = 20) -> float:
    counts, _ = np.histogram(u, bins=bins, range=(0.0, 1.0))
    expected = len(u) / bins
    return float(((counts - expected) ** 2 / expected).sum())


@pytest.mark.parametrize("cell", [1, 5, 10])
def test_the_log_draw_is_uniform_in_ln_wpm(cell):
    # 10 000 draws in one speed cell, their ln WPM in 20 equal bins of the cell: chi-squared below CHI2_19_P999.
    rng = np.random.default_rng(2026)
    lo, hi = SPEED_CELLS.bounds(cell)
    v = np.array([draw(SPEED_CELLS, cell, rng) for _ in range(10_000)])
    u = (np.log(v) - math.log(lo)) / (math.log(hi) - math.log(lo))
    assert _chi2_uniform(u) < CHI2_19_P999


def test_the_chi2_test_tells_log_from_linear_over_8_to_80_wpm():
    # Over the whole range the test has the power to see the difference: log draws pass, linear draws fail.
    rng = np.random.default_rng(2026)
    log_cells, linear_cells = Cells((8.0, 80.0), "log"), Cells((8.0, 80.0), "linear")
    to_u = lambda v: (np.log(v) - math.log(8.0)) / math.log(10.0)  # noqa: E731
    assert _chi2_uniform(to_u(np.array([draw(log_cells, 1, rng) for _ in range(10_000)]))) < CHI2_19_P999
    assert _chi2_uniform(to_u(np.array([draw(linear_cells, 1, rng) for _ in range(10_000)]))) > CHI2_19_P999


def test_a_record_carries_value_cell_range_scale_and_unit():
    assert record(SNR_CELLS, 3, -3.5) == {"value": -3.5, "cell": 3, "range": [-4.0, -2.0], "scale": "linear",
                                          "unit": "dB SNR in 500 Hz"}


def test_bad_cells_are_rejected():
    with pytest.raises(ValueError):
        Cells((1.0,))
    with pytest.raises(ValueError):
        Cells((1.0, 1.0))
    with pytest.raises(ValueError):
        Cells((0.0, 1.0), "log")
    with pytest.raises(ValueError):
        Cells((0.0, 1.0), "cubic")
    with pytest.raises(IndexError):
        SPEED_CELLS.bounds(0)
    with pytest.raises(IndexError):
        SPEED_CELLS.bounds(11)
