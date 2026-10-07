"""The jittered development set's analysis (kz4ap_proto.devset2) and figures (kz4ap_proto.figures), on synthetic CER
data with a known model. Figure tests skip where matplotlib is absent (CI installs only requirements.txt)."""

import json
import math

import numpy as np
import pytest

from kz4ap_proto import devset2 as d2
from kz4ap_proto import experiments
from kz4ap_synth.jitter import SNR_CELLS, SPEED_CELLS

# A known model: s0 = 1 + 1.5 x + 1.0 x^2 dB SNR in 500 Hz, w = 1.2 exp(0.2 x) dB, floor 0.003 in every speed cell
# (x = ln(v / 25.30 WPM)).
SHAPE = (1.0, 1.5, 1.0, math.log(1.2), 0.2, 0.0)


def model(shape=SHAPE, floors=(0.003,) * 10, axis="s500"):
    return d2.Fit(tuple(shape) + tuple(d2._logit(c) for c in floors), axis=axis)


TRUTH = model()
CENTERS = np.array([SPEED_CELLS.center(k) for k in SPEED_CELLS.numbers])


def synth(rng, truth=TRUTH, per_cell=4, fail=0.0, axis="s500"):
    """A2-like signals: per_cell per (speed cell, S500 cell), speed log-uniform and S500 uniform within the cell,
    125-139 reference symbols, edits binomial from truth (on `axis`); a fraction `fail` of the signals fail as a
    whole (CER 0.5-1 at any S500: a wrong speed lock)."""
    v, s = [], []
    for c in SPEED_CELLS.numbers:
        lo, hi = SPEED_CELLS.bounds(c)
        for k in SNR_CELLS.numbers:
            a, b = SNR_CELLS.bounds(k)
            for _ in range(per_cell):
                v.append(math.exp(rng.uniform(math.log(lo), math.log(hi))))
                s.append(rng.uniform(a, b))
    v, s = np.array(v), np.array(s)
    n = rng.integers(125, 140, len(v))
    level = s if axis == "s500" else d2.en0_db(s, v)
    k = rng.binomial(n, truth.cer(level, v)).astype(float)
    bad = rng.random(len(v)) < fail
    k[bad] = np.round(n[bad] * rng.uniform(0.5, 1.0, bad.sum()))
    return s, v, k, n


def test_energy_per_dit_and_the_genie_bound_match_the_stage_1_record():
    # E/N0 = S500 + 10 log10(500 Hz x 1.2 s / WPM): +13.8 dB at 25 WPM, +17.0 dB at 12 WPM (stage-1 record 5.1.1)
    assert d2.en0_db(0.0, 25.0) == pytest.approx(13.80, abs=0.01)
    assert d2.en0_db(0.0, 12.0) == pytest.approx(16.99, abs=0.01)
    assert d2.s500_from_en0_db(d2.en0_db(3.7, 40.0), 40.0) == pytest.approx(3.7)
    # the record's bounds: CER 0.10 11.9 / 10.3 dB re 1, CER 0.05 12.7 / 11.2 dB re 1; S500 -1.9 dB at 25 WPM
    assert d2.ideal_en0_db(0.10) == pytest.approx(11.9, abs=0.05)
    assert d2.ideal_en0_db(0.10, coherent=True) == pytest.approx(10.3, abs=0.05)
    assert d2.ideal_en0_db(0.05) == pytest.approx(12.7, abs=0.05)
    assert d2.ideal_en0_db(0.05, coherent=True) == pytest.approx(11.2, abs=0.05)
    assert d2.ideal_s500_db(0.10, 25.0) == pytest.approx(-1.9, abs=0.05)
    assert d2.ideal_s500_db(0.10, 12.0) == pytest.approx(-5.0, abs=0.1)


def test_the_crossing_is_where_the_model_reaches_the_threshold():
    for t in d2.CER_LEVELS:
        x = TRUTH.crossing_db(t, CENTERS)
        assert np.allclose(TRUTH.cer(x, CENTERS), t, atol=1e-12)
    high_floor = model(floors=(0.2,) + (0.003,) * 9)
    x = high_floor.crossing_db(0.10, CENTERS)
    assert np.isnan(x[0]) and np.allclose(x[1:], TRUTH.crossing_db(0.10, CENTERS[1:]))
    # each speed takes its own cell's floor: an edge belongs to the cell above it, the top edge to the top cell
    assert list(d2.cell_index([8.0, 10.06, SPEED_CELLS.edges[1], 79.9, 80.0, 90.0])) == [0, 0, 1, 9, 9, 9]
    assert list(high_floor.floor_at([9.0, 11.0])) == pytest.approx([0.2, 0.003])


@pytest.mark.parametrize("method", d2.METHODS)
def test_the_fit_recovers_a_known_s0_and_w(method):
    """Tolerances: about 4x the spread measured over 40 synthetic sets (task D2 report): s0 0.064 dB SNR in 500 Hz, w 4.3%,
    the CER-0.10 crossing 0.10 dB SNR in 500 Hz, the floor 0.0006 (worst cell, either method)."""
    s, v, k, n = synth(np.random.default_rng(7))
    fit = d2.fit_cer(s, v, k, n, method=method)
    assert fit.converged
    assert np.max(np.abs(fit.s0_db(CENTERS) - TRUTH.s0_db(CENTERS))) < 0.25
    assert np.max(np.abs(fit.w_db(CENTERS) / TRUTH.w_db(CENTERS) - 1)) < 0.20
    assert np.max(np.abs(fit.crossing_db(0.10, CENTERS) - TRUTH.crossing_db(0.10, CENTERS))) < 0.4
    assert np.max(np.abs(fit.crossing_db(0.05, CENTERS) - TRUTH.crossing_db(0.05, CENTERS))) < 0.4
    assert np.max(np.abs(np.array(fit.floors) - 0.003)) < 0.006


def test_least_squares_tracks_the_mean_cer_when_whole_signals_fail():
    """3% of the signals fail as a whole (CER 0.5-1, mean 0.75): the mean CER is 0.97 f + 0.0225, whose 0.10
    crossing is where f = 0.0799. With one floor per speed cell (task D4), each cell's floor absorbs its own few
    failures. Over 200 sets of 4 signals per cell, least squares (the default) follows the mean with a mean bias of
    +0.05 to +0.15 dB SNR in 500 Hz per cell (RMS 0.38-0.75 dB; the mean's standard error up to 0.05 dB), the binomial
    likelihood with +0.18 to +0.29 dB (RMS 0.50-0.76 dB); at 48 signals per cell (20 sets) +0.00 to +0.11 dB against
    +0.14 to +0.29 dB. Task D2's single floor gave 0.01-0.06 dB and 0.20-0.27 dB. This test's 40 sets: least squares
    at most 0.22 dB in any cell (the 40-set mean's standard error is up to 0.12 dB), on average under a third of the
    binomial likelihood's. The measurement behind FIT_METHOD."""
    target = TRUTH.crossing_db((0.10 - 0.03 * 0.75) / 0.97, CENTERS)
    errors = {m: [] for m in d2.METHODS}
    for rep in range(40):
        s, v, k, n = synth(np.random.default_rng(rep), fail=0.03)
        for m in d2.METHODS:
            errors[m].append(d2.fit_cer(s, v, k, n, method=m).crossing_db(0.10, CENTERS) - target)
    bias = {m: np.abs(np.mean(e, axis=0)) for m, e in errors.items()}
    assert d2.FIT_METHOD == "wls"
    assert np.max(bias["wls"]) < 0.3
    assert np.mean(bias["wls"]) < 0.5 * np.mean(bias["binomial"])


def test_the_bootstrap_intervals_cover_the_truth_and_are_reproducible():
    """The percentile intervals' coverage, measured over 40 sets x 10 cells (200 resamples, task D4): 0.907 with one
    floor per speed cell, 0.895 with task D2's single floor (same sets): about 0.90, not 0.95, for both. At 0.90 per
    cell, 7 or fewer of 10 has probability about 0.07; this set covers 7 with the per-cell floors (at least 8 with D2's)."""
    s, v, k, n = synth(np.random.default_rng(11))
    fit = d2.fit_cer(s, v, k, n)
    boots = d2.bootstrap_fits(s, v, k, n, fit, "test", resamples=200)
    assert sum(b is None for b in boots) == 0
    cross = d2.crossings(fit, boots, CENTERS, 0.10)
    truth = TRUTH.crossing_db(0.10, CENTERS)
    covered = sum(c["interval"][0] <= t <= c["interval"][1] for c, t in zip(cross, truth))
    assert covered >= 7  # measured coverage about 0.90 per cell (docstring); the pooled test below is the sharper one
    assert all(0.0 < c["interval"][1] - c["interval"][0] < 1.0 for c in cross)
    again = d2.crossings(fit, d2.bootstrap_fits(s, v, k, n, fit, "test", resamples=200), CENTERS, 0.10)
    assert again == cross


def test_the_bootstrap_coverage_pooled_over_sets():
    """Coverage pooled over 10 sets x 10 cells (100 resamples each), so that it can tell about 0.90 from 0.80 (task D4
    review, M3). Measured: 0.91 over seeds 11-20, 0.95 over seeds 100-109; 40 x 10 cells at 200 resamples gave 0.907.
    The cells of one set share the fit, so the 100 trials are not independent; 0.82 leaves a margin below 0.91."""
    truth = TRUTH.crossing_db(0.10, CENTERS)
    covered = []
    for seed in range(11, 21):
        s, v, k, n = synth(np.random.default_rng(seed))
        fit = d2.fit_cer(s, v, k, n)
        cross = d2.crossings(fit, d2.bootstrap_fits(s, v, k, n, fit, ("cov", seed), resamples=100), CENTERS, 0.10)
        covered += [c["interval"] is not None and c["interval"][0] <= t <= c["interval"][1] for c, t in zip(cross, truth)]
    assert np.mean(covered) >= 0.82


def test_task_d2_fits_are_read_with_their_single_floor_in_every_cell():
    old = {"params": {"a0": 1.0, "a1": 1.5, "a2": 1.0, "b0": math.log(1.2), "b1": 0.2, "b2": 0.0, "g": d2._logit(0.02)},
           "axis": "s500", "method": "wls", "objective": 1.0, "converged": True, "iterations": 9, "signals": 560,
           "free": ["a0", "a1", "a2", "b0", "b1", "b2", "g"], "floor": 0.02}
    fit = d2.Fit.from_json(old)
    assert fit.floors == pytest.approx((0.02,) * 10) and all(fit.free)
    assert np.allclose(fit.crossing_db(0.10, CENTERS), model(floors=(0.02,) * 10).crossing_db(0.10, CENTERS))


def _failing_cell_1(rng, form):
    """synth()'s A2-like signals with speed cell 1 failing at every S500. form "model": cell 1's floor 0.5, the same
    transition (the model's own form); "misfit": the transition 1 dB higher, then a floor falling from 0.70 at 0 dB to
    0.45 at +20 dB SNR in 500 Hz (like the bank in the pilot; not the model's form)."""
    s, v, _, n = synth(rng)
    p = TRUTH.cer(s, v)
    one = d2.cell_index(v) == 0
    if form == "model":
        p[one] = 0.5 + 0.5 * (p[one] - 0.003) / 0.997
    else:
        z = -(s[one] - (TRUTH.s0_db(v[one]) + 1.0)) / TRUTH.w_db(v[one])
        floor = 0.70 - 0.25 * np.clip(s[one] / 20.0, 0.0, 1.0)
        p[one] = floor + (1.0 - floor) / (1.0 + np.exp(-z))
    return s, v, rng.binomial(n, p).astype(float), n


@pytest.mark.parametrize("form, tolerance_db", [
    # measured over 10 sets (task D4), the largest |difference| per cell 2 ... 10, dB SNR in 500 Hz:
    # model form 0.09 0.04 0.01 0.02 0.03 0.04 0.02 0.01 0.04; misfit 0.49 0.23 0.06 0.08 0.13 0.14 0.09 0.03 0.24
    # (a single floor for all cells, task D2's model: 7.3 and 8.2 dB in cell 2, 0.3-3.6 dB in cells 3-10)
    ("model", (0.25,) * 9),
    ("misfit", (0.8, 0.4) + (0.4,) * 7)])
def test_a_failing_speed_cell_reports_no_crossing_and_leaves_its_neighbors(form, tolerance_db):
    """One floor per speed cell (owner, 2026-10-06): a speed cell that fails at every S500 reports "no crossing", and
    its neighbors' crossings stay within the stated tolerances of the fit without that cell."""
    for rep in range(3):
        s, v, k, n = _failing_cell_1(np.random.default_rng(200 + rep), form)
        keep = d2.cell_index(v) > 0
        fit = d2.fit_cer(s, v, k, n)
        without = d2.fit_cer(s[keep], v[keep], k[keep], n[keep])
        assert fit.converged and without.converged
        top = d2.top_levels(s, v)
        cells = d2.crossings(fit, [], CENTERS, 0.10, top)
        assert cells[0]["value"] is None and cells[0]["no_crossing"] == "floor" and cells[0]["floor"] > 0.3
        assert cells[0]["cer_at_top"] > 0.3
        diff = np.abs(np.array([c["value"] for c in cells[1:]]) - without.crossing_db(0.10, CENTERS[1:]))
        assert np.all(diff < np.array(tolerance_db)), diff
        # the fit without cell 1 has no floor there and reports it as without data
        assert without.fitted_cells == tuple(range(2, 11)) and without.to_json()["floors"]["1"] is None
        assert d2.crossings(without, [], CENTERS[:1], 0.10, d2.top_levels(s[keep], v[keep]))[0]["no_crossing"] == "no data"


def test_a_crossing_above_the_data_is_no_crossing_and_one_below_is_kept():
    # s0 = 15 + 8 x dB SNR in 500 Hz: about +24 dB at 71 WPM (cell 10) and +4 dB at 9 WPM (cell 1)
    steep = model((15.0, 8.0, 0.0, math.log(1.0), 0.0, 0.0))
    top = np.full(10, 20.0)
    cells = d2.crossings(steep, [], CENTERS, 0.10, top)
    assert cells[9]["value"] is None and cells[9]["no_crossing"] == "above the data"
    assert cells[9]["cer_at_top"] > 0.10 and cells[9]["top_db"] == 20.0
    assert cells[0]["value"] == pytest.approx(float(steep.crossing_db(0.10, CENTERS[0])))
    # without top (no data range given) every crossing is reported
    assert all(c["value"] is not None for c in d2.crossings(steep, [], CENTERS, 0.10))
    # the resamples follow the same rule: those above the data give no value, and the share that cross is reported
    shifted = [model((15.0 + dz, 8.0, 0.0, math.log(1.0), 0.0, 0.0)) for dz in np.linspace(-6.0, 0.0, 20)]
    c10 = d2.crossings(steep, shifted, CENTERS[9:], 0.10, top)[0]
    assert 0.0 < c10["resamples_with_crossing"] < 0.95 and c10["interval"] is None


def test_the_fit_against_e_n0_is_the_s500_fit_in_other_coordinates():
    # s0 quadratic in ln v absorbs the shift 10 log10(600 / v) = const - 4.34 ln v, so both fits are one model
    s, v, k, n = synth(np.random.default_rng(3))
    fs = d2.fit_cer(s, v, k, n, axis="s500")
    fe = d2.fit_cer(d2.en0_db(s, v), v, k, n, axis="en0")
    assert np.allclose(fe.crossing_db(0.10, CENTERS), d2.en0_db(fs.crossing_db(0.10, CENTERS), CENTERS), atol=0.02)
    assert fe.params[1] == pytest.approx(fs.params[1] - 10.0 / math.log(10.0), abs=0.05)


def test_the_invariance_test_accepts_an_invariant_decoder_and_rejects_a_fixed_s500_one():
    invariant = model((12.0, 0.0, 0.0, math.log(1.0), 0.0, 0.0), axis="en0")
    s, v, k, n = synth(np.random.default_rng(5), truth=invariant, axis="en0")
    e = d2.en0_db(s, v)
    fit = d2.fit_cer(e, v, k, n, axis="en0")
    wald = d2._wald(d2.bootstrap_fits(e, v, k, n, fit, "inv", resamples=200), fit)
    assert wald["p_value"] > 0.01
    # a decoder whose curve is fixed in S500 (s0 = 0 dB SNR in 500 Hz at every speed): s0 in E/N0 falls 4.34 dB per
    # unit of ln v, 10 dB of E/N0 per dit over 8-80 WPM
    fixed = model((0.0, 0.0, 0.0, math.log(1.0), 0.0, 0.0))
    s, v, k, n = synth(np.random.default_rng(6), truth=fixed)
    e = d2.en0_db(s, v)
    fit = d2.fit_cer(e, v, k, n, axis="en0")
    wald = d2._wald(d2.bootstrap_fits(e, v, k, n, fit, "fixed", resamples=200), fit)
    assert wald["p_value"] < 1e-3
    assert wald["intervals"]["a1"][1] < -3.0
    restricted = d2.fit_cer(e, v, k, n, axis="en0", free=d2.S0_CONSTANT, start=[fit.params[0], 0, 0, *fit.params[3:]])
    assert restricted.params[1] == 0.0 and restricted.params[2] == 0.0
    assert restricted.objective > fit.objective


def test_wilson_interval():
    assert d2.wilson(0, 4) == pytest.approx([0.0, 1.959964 ** 2 / (4 + 1.959964 ** 2)], abs=1e-6)
    lo, hi = d2.wilson(4, 4)
    assert hi == 1.0 and lo == pytest.approx(1 - 1.959964 ** 2 / (4 + 1.959964 ** 2), abs=1e-6)
    assert d2.wilson(0, 0) is None


def _row(decoder, case, index, edits, symbols=100, group="A2 sensitivity", cell=1):
    return {"decoder": decoder, "group": group, "test_case": case, "index": index, "path": "oracle",
            "speed_wpm": SPEED_CELLS.center(cell), "speed_cell": cell, "s500_db": 0.0, "s500_cell": 5,
            "en0_db": 0.0, "edits": edits, "symbols": symbols, "detected": True, "beyond_oracle_anchor": False}


def test_paired_differences_by_group_and_speed_cell_on_signals_both_decoders_scored():
    rows = [_row("ref", "t1", 0, 10), _row("var", "t1", 0, 5),
            _row("ref", "t1", 1, 20), _row("var", "t1", 1, 10),
            _row("ref", "t2", 0, 0, group="B2 fading", cell=3), _row("var", "t2", 0, 30, group="B2 fading", cell=3),
            _row("ref", "t3", 0, 50)]  # no variant row: left out
    out = d2.paired(rows, "ref", "var")
    assert out["all"]["signals"] == 3 and out["all"]["test_cases"] == 2
    assert out["all"]["mean"] == pytest.approx((-0.05 - 0.10 + 0.30) / 3)
    assert out["groups"]["A2 sensitivity"]["mean"] == pytest.approx(-0.075)
    assert out["groups"]["B2 fading"]["signals"] == 1 and out["groups"]["B2 fading"]["interval"] is None
    assert out["speed_cells"]["3"]["mean"] == pytest.approx(0.30)
    assert out["all"]["interval"][0] <= out["all"]["mean"] <= out["all"]["interval"][1]
    assert out["all"]["interval_test_cases"] is None  # 2 test cases, fewer than MIN_TEST_CASES_FOR_INTERVAL


def test_pooled_paired_sets_leave_out_the_detector_path_and_the_stretched_copies():
    detector = {**_row("ref", "d1", 0, 10, group="A2 sensitivity, detector"), "path": "detector"}
    rows = [_row("ref", "t1", 0, 10), _row("var", "t1", 0, 5),
            detector, {**detector, "decoder": "var", "edits": 50},
            _row("ref", "s1", 0, 10, group="S2 stretch", cell=2), _row("var", "s1", 0, 90, group="S2 stretch", cell=2)]
    out = d2.paired(rows, "ref", "var")
    assert out["all"]["signals"] == 1 and out["all"]["mean"] == pytest.approx(-0.05)
    assert out["speed_cells"]["1"]["signals"] == 1 and "2" not in out["speed_cells"]
    assert out["groups"]["A2 sensitivity, detector"]["mean"] == pytest.approx(0.40)
    assert out["groups"]["S2 stretch"]["mean"] == pytest.approx(0.80)


def test_the_interval_over_test_cases_resamples_whole_test_cases():
    # five test cases, every signal of a test case with the same difference: the resampled means are averages of
    # whole test cases' signals, so the interval lies within the test cases' range
    rows = []
    for case, (d, count) in enumerate([(-0.10, 1), (-0.05, 3), (0.0, 2), (0.05, 2), (0.20, 1)]):
        for i in range(count):
            rows += [_row("ref", f"t{case}", i, 10), _row("var", f"t{case}", i, 10 + round(100 * d))]
    out = d2.paired(rows, "ref", "var")
    assert d2.MIN_TEST_CASES_FOR_INTERVAL == 5 and out["all"]["test_cases"] == 5
    lo, hi = out["all"]["interval_test_cases"]
    assert -0.10 <= lo < out["all"]["mean"] < hi <= 0.20


# --- an end-to-end fixture: a suite folder with labels and two decoders' scored files ---

def _fixture(tmp_path):
    """A2 (one recording per speed cell, 14 S500 cells x 3 signals), A2's detector-path copies and two B2
    recordings, with results for decoders "ref" and "var" (var's s0 0.5 dB SNR in 500 Hz lower) drawn from the known model."""
    out = tmp_path / "suite"
    out.mkdir()
    rng = np.random.default_rng(1)
    recs, specs = [], {}
    for c in SPEED_CELLS.numbers:
        lo, hi = SPEED_CELLS.bounds(c)
        sig = []
        for k in SNR_CELLS.numbers:
            a, b = SNR_CELLS.bounds(k)
            for _ in range(3):
                sig.append((math.exp(rng.uniform(math.log(lo), math.log(hi))), rng.uniform(a, b), c, k))
        for prefix, group, oracle in (("A2-awgn", "A2 sensitivity", True),
                                      ("A2-detector", "A2 sensitivity, detector", False)):
            name = f"{prefix}-c{c:02d}-0-s1"
            recs.append({"name": name, "group": group, "oracle": oracle, "wav": f"{name}.wav",
                         "labels": f"{name}.json", "station_labels": None})
            specs[name] = sig
    for part in range(2):
        name = f"B2-fading-c{part + 1:02d}-0-s1"
        recs.append({"name": name, "group": "B2 fading", "oracle": True, "wav": f"{name}.wav",
                     "labels": f"{name}.json", "station_labels": None})
        specs[name] = [(math.exp(rng.uniform(math.log(8), math.log(80))), rng.uniform(0, 20), None, None)
                       for _ in range(12)]
    recs.append({"name": "A-awgn-25wpm-0-s1", "group": "A sensitivity", "oracle": True, "wav": "x.wav",
                 "labels": "A-awgn-25wpm-0-s1.json", "station_labels": None})  # the old set: not in DEV2
    specs["A-awgn-25wpm-0-s1"] = [(25.0, 0.0, None, None)]
    (out / "manifest.json").write_text(json.dumps({"suite": "dev2", "recordings": recs}))
    for name, sig in specs.items():
        labels = [{"text": "X", "freq_offset_hz": 1000.0 * i, "wpm": v, "snr_db": s, "start_s": 1.0, "end_s": 30.0,
                   "tag": "t", "design": {"speed_wpm": {"value": v, "cell": c}, "s500_db": {"value": s, "cell": k}}}
                  for i, (v, s, c, k) in enumerate(sig)]
        (out / f"{name}.json").write_text(json.dumps({"signals": labels}))
    for decoder, shift in (("ref", 0.0), ("var", -0.5)):
        model = d2.Fit((TRUTH.params[0] + shift,) + TRUTH.params[1:])
        folder = out / "results" / decoder
        folder.mkdir(parents=True)
        for name, sig in specs.items():
            scored = []
            for i, (v, s, _, _) in enumerate(sig):
                n = int(rng.integers(125, 140))
                e = int(rng.binomial(n, float(model.cer(s, v))))
                detected = (not name.startswith("A2-detector")) or rng.random() < 1 / (1 + math.exp(-(s + 2.0)))
                scored.append({"index": i, "snr_db": s, "scored": True, "track_id": 1 if detected else None,
                               "tracked_freq_hz": None, **{key: 0 for key in d2_count_keys()},
                               "symbols": n, "edits": e})
            (folder / f"{name}.json").write_text(json.dumps({"score": {"signals": scored}, "timing": {},
                                                             "channel_seconds": 0.0}))
    return out


def d2_count_keys():
    from kz4ap_synth.suites import COUNT_KEYS
    return COUNT_KEYS


def test_the_analysis_end_to_end(tmp_path):
    out = _fixture(tmp_path)
    path = d2.write_analysis(out, ["ref", "var"], "fixture", reference="ref", resamples=100)
    a = json.loads(path.read_text())
    assert {r["test_case"] for r in a["signals"]} >= {"A2-awgn-c01-0-s1", "A2-detector-c10-0-s1", "B2-fading-c02-0-s1"}
    assert all(not r["test_case"].startswith("A-awgn") for r in a["signals"])  # DEV2 only
    ref = a["decoders"]["ref"]
    assert ref["fit_signals"] == 10 * 14 * 3
    cells = ref["fit"]["s500"]["crossings_0.10"]
    assert [c["cell"] for c in cells] == list(SPEED_CELLS.numbers)
    truth = TRUTH.crossing_db(0.10, CENTERS)
    assert all(abs(c["value"] - t) < 0.6 for c, t in zip(cells, truth))
    assert len(ref["fit"]["s500"]["curve_0.10"]) == len(d2.FINE_SPEEDS_WPM)
    assert ref["fit"]["en0"]["invariance"]["p_value"] is not None
    assert len(ref["recall"]) == 10 * 14 and all(c["labels"] == 3 for c in ref["recall"])
    (comp,) = a["paired"]
    assert comp["variant"] == "var" and comp["all"]["mean"] < 0
    assert set(comp["groups"]) == {"A2 sensitivity", "A2 sensitivity, detector", "B2 fading"}
    # the pooled and per-speed-cell sets hold each signal once: the oracle path only (A2's detector copies are the
    # same signals and noise; review I1); the detector path stays its own group
    assert comp["all"]["signals"] == 10 * 14 * 3 + 2 * 12
    assert comp["groups"]["A2 sensitivity, detector"]["signals"] == 10 * 14 * 3
    a2_cell_1 = sum(1 for r in a["signals"] if r["decoder"] == "ref" and r["path"] == "oracle" and r["speed_cell"] == 1)
    assert comp["speed_cells"]["1"]["signals"] == a2_cell_1
    md = path.with_suffix(".md").read_text(encoding="utf-8")
    assert "dB SNR in 500 Hz" in md and "dB re 1" in md and "Paired: var minus ref" in md
    # committed beside the results record: no machine's folders
    assert a["meta"]["results_dirs"] == ["results"]
    assert str(tmp_path) not in md and str(tmp_path) not in json.dumps(a["meta"])


def test_the_command_line_writes_the_analysis_and_the_figures(tmp_path):
    pytest.importorskip("matplotlib")
    out = _fixture(tmp_path)
    experiments.main(["devset2", "--out", str(out), "--name", "cli", "--decoder", "ref", "--decoder", "var",
                      "--reference", "ref", "--resamples", "50"])
    analysis = out / "experiments" / "devset2-cli.json"
    assert analysis.exists() and analysis.with_suffix(".md").exists()
    experiments.main(["figures", "--analysis", str(analysis)])
    folder = out / "experiments" / "figures-cli"
    for stem in ("fig1-ref", "fig1-var", "fig2-ref", "fig2-var", "fig3", "fig4", "fig5-ref", "fig5-var"):
        assert (folder / f"{stem}.png").stat().st_size > 0 and (folder / f"{stem}.svg").stat().st_size > 0
    captions = (folder / "captions.md").read_text(encoding="utf-8")
    assert "Runs (decoders by results folder): ref, var" in captions and "fig3" in captions


def test_each_figure_from_a_small_fixture(tmp_path):
    pytest.importorskip("matplotlib")
    from PIL import Image

    from kz4ap_proto import figures
    out = _fixture(tmp_path)
    a = json.loads(d2.write_analysis(out, ["ref", "var"], "small", reference="ref", resamples=30).read_text())
    drawn = [figures.figure_cer_s500(a, "ref", tmp_path / "f1"), figures.figure_cer_en0(a, "ref", tmp_path / "f2"),
             figures.figure_crossings(a, tmp_path / "f3"), figures.figure_paired(a, tmp_path / "f4"),
             figures.figure_recall(a, "ref", tmp_path / "f5")]
    for paths in drawn:
        png, svg = paths
        assert png.suffix == ".png" and svg.suffix == ".svg"
        assert Image.open(png).info["dpi"] == pytest.approx((150, 150), abs=0.1)
        text = svg.read_text(encoding="utf-8")
        assert "WPM" in text or "dB SNR in 500 Hz" in text or "variant" in text
    svgs = {i: p[1].read_text(encoding="utf-8") for i, p in enumerate(drawn, 1)}
    assert "dB SNR in 500 Hz" in svgs[1] and "dB re 1" in svgs[2] and "WPM" in svgs[3]
    assert "dB SNR in 500 Hz" in svgs[5]


def test_a_crossing_far_outside_the_data_does_not_set_the_s500_axes(tmp_path, monkeypatch):
    """Pilot (task D3): the bank's fit put speed cell 1's crossing at +31 dB SNR in 500 Hz with an interval to +211 dB,
    which stretched figures 1 and 3 to hundreds of dB. The S500 axes span the data (plus the genie bound in figure 3);
    the far crossing is marked at the edge with its value."""
    pytest.importorskip("matplotlib")
    from kz4ap_proto import figures
    out = _fixture(tmp_path)
    a = json.loads(d2.write_analysis(out, ["ref", "var"], "far", reference="ref", resamples=30).read_text())
    s = a["decoders"]["ref"]["fit"]["s500"]
    s["crossings_0.10"][0].update(value=31.0, interval=[14.2, 210.7])
    s["curve_0.10"][0].update(value=300.0, interval=[20.0, 5000.0])
    lo, hi = a["decoders"]["ref"]["fit"]["s500_range_db"]
    captured = []
    monkeypatch.setattr(figures, "_save", lambda fig, stem: captured.append(fig) or [])
    figures.figure_cer_s500(a, "ref", tmp_path / "f1")
    figures.figure_crossings(a, tmp_path / "f3")
    fig1, fig3 = captured
    assert fig1.axes[0].get_xlim() == pytest.approx((lo - 1.0, hi + 1.0))
    texts = [t.get_text() for t in fig1.axes[0].texts]
    assert any("+31.0 dB" in t and "outside the data" in t for t in texts)
    y_lo, y_hi = fig3.axes[0].get_ylim()
    # the genie bound is lowest at 8 WPM (coherent) and stays below the data's top (about +3 dB at 80 WPM)
    assert (y_lo, y_hi) == pytest.approx((min(lo, float(d2.ideal_s500_db(0.10, 8.0, coherent=True))) - 1.0, hi + 1.0))
    assert any("+31.0 dB" in t.get_text() for t in fig3.axes[0].texts)


def test_the_development_analysis_selects_dev2_and_the_detector_copies():
    import re
    assert re.search(d2.DEV2_ANALYSIS, "A2-detector-c03-1-s1")
    assert re.search(d2.DEV2_ANALYSIS, "H2-qso-c01-0-s1")
    assert not re.search(d2.DEV2_ANALYSIS, "A2-awgn-c03-1-s2")
    assert not re.search(d2.DEV2_ANALYSIS, "A-awgn-25wpm-0-s1")
    # every DEV2 test case is in the analysis's selection
    for name in ("A2-awgn-c01-0-s1", "S2-stretch-c05-0-s1", "F2-drift-c02-0-s1"):
        assert re.search(experiments.DEV2, name) and re.search(d2.DEV2_ANALYSIS, name)


def test_a_fit_that_did_not_converge_is_flagged_in_the_summary():
    s, v, k, n = synth(np.random.default_rng(2))
    fit = d2.fit_cer(s, v, k, n)
    assert fit.converged and fit.stop in ("converged", "stationary")
    assert d2.Fit.from_json(fit.to_json()) == fit
    ok = {"converged": True, "iterations": 9}
    entry = {"s500": {"fit": ok}, "en0": {"fit": ok, "s0_constant": {"fit": {"converged": False, "iterations": 200},
                                                                      "objective_increase": -1.0}}}
    warnings = d2._fit_warnings(entry)
    assert any("s0 constant did not converge" in w for w in warnings)
    assert any("lower objective" in w for w in warnings)
    entry["en0"]["s0_constant"] = {"fit": ok, "objective_increase": 5.0}
    assert d2._fit_warnings(entry) == []


def test_figure_curves_break_at_every_speed_cell_edge():
    """Figure 3 draws the per-cell-floor curve one speed cell at a time (task D4 review, I2): a NaN separates
    consecutive points in different cells, so no line or band joins two cells."""
    from kz4ap_proto import figures
    v = np.array(d2.FINE_SPEEDS_WPM)
    y = np.arange(len(v), dtype=float)
    vb, yb = figures.break_at_cell_edges(v, y)
    gaps = np.flatnonzero(np.isnan(vb))
    assert len(gaps) == len(SPEED_CELLS) - 1 and np.all(np.isnan(yb[gaps]))
    for segment in np.split(vb, gaps):
        cells = set(d2.cell_index(segment[np.isfinite(segment)]))
        assert len(cells) == 1
    assert list(yb[np.isfinite(yb)]) == list(y)


def test_figures_name_decoders_not_folders():
    from kz4ap_proto import figures
    assert figures.display_name("baseline") == "Envelope"
    assert figures.display_name("matched") == "Matched"
    assert figures.display_name("d4-bank") == "bank (d4-bank)"


# --- score tables (the results-data rule: the record's numbers come from the committed, rounded tables) ---

def _without_cpu(x):
    """The analysis without its measured CPU times (fit_decoder's cpu_s), which differ from run to run."""
    if isinstance(x, dict):
        return {k: _without_cpu(v) for k, v in x.items() if k != "cpu_s"}
    return [_without_cpu(v) for v in x] if isinstance(x, list) else x


def test_round_row_rounds_speed_and_s500_and_recomputes_e_n0():
    r = {**_row("ref", "t1", 3, 7), "speed_wpm": 12.345678, "s500_db": -3.14159, "s500_cell": None, "en0_db": 99.0,
         "detected": False, "beyond_oracle_anchor": True}
    out = d2.round_row(r)
    assert out["speed_wpm"] == 12.35 and out["s500_db"] == -3.14
    assert out["en0_db"] == pytest.approx(float(d2.en0_db(-3.14, 12.35)), abs=1e-12)  # from the rounded values
    assert out["s500_cell"] is None and out["speed_cell"] == 1 and out["index"] == 3
    assert out["detected"] is False and out["beyond_oracle_anchor"] is True


def test_a_score_table_round_trips_the_rounded_rows(tmp_path):
    rng = np.random.default_rng(5)
    rows = []
    for i in range(40):
        v, s = float(np.exp(rng.uniform(np.log(8), np.log(80)))), float(rng.uniform(-8, 20))
        rows.append({**_row("ref" if i % 2 else "var", f"A2-awgn-c01-{i // 10}-s1", i, int(rng.integers(0, 200))),
                     "group": "A2 sensitivity, detector" if i % 7 == 0 else "A2 sensitivity",
                     "path": "detector" if i % 7 == 0 else "oracle", "speed_wpm": v, "s500_db": s,
                     "speed_cell": SPEED_CELLS.cell_of(v), "s500_cell": None if i % 9 == 0 else SNR_CELLS.cell_of(s),
                     "en0_db": float(d2.en0_db(s, v)), "detected": bool(i % 3), "beyond_oracle_anchor": i % 5 == 0})
    path = d2.write_scores(rows, tmp_path / "t.csv")
    text = path.read_text(encoding="utf-8")
    assert text.splitlines()[0] == ",".join(d2.SCORE_COLUMNS)
    assert '"A2 sensitivity, detector"' in text and "True" not in text and "\r" not in text
    back = d2.read_scores(path)
    assert back == [d2.round_row(r) for r in rows]
    assert all(isinstance(r["edits"], int) and isinstance(r["detected"], bool) for r in back)
    # selection as load_rows: by decoder and by a regular expression on the test case
    assert {r["decoder"] for r in d2.read_scores(path, decoders=["ref"])} == {"ref"}
    assert {r["test_case"] for r in d2.read_scores(path, only="-[02]-s1$")} == {"A2-awgn-c01-0-s1", "A2-awgn-c01-2-s1"}
    # an en0_db column that is not the speed's and S500's E/N0 per dit is refused
    lines = text.splitlines()
    cols = lines[2].split(",")  # row i = 1: no comma inside a field
    cols[d2.SCORE_COLUMNS.index("en0_db")] = f"{float(cols[d2.SCORE_COLUMNS.index('en0_db')]) + 0.02:.2f}"
    (tmp_path / "bad.csv").write_text("\n".join([lines[0], ",".join(cols)]) + "\n", encoding="utf-8")
    with pytest.raises(ValueError, match="en0_db"):
        d2.read_scores(tmp_path / "bad.csv")


def test_the_analysis_from_the_score_tables_equals_the_analysis_of_the_rounded_rows(tmp_path):
    """The rule's guarantee: a record computed from the committed tables is the analysis of the original rows rounded
    as the tables round them, with the same bootstrap draws. The tables are exported by the command line, one per
    run (decoder and path), and analyzed with no suite folder."""
    suite = _fixture(tmp_path)
    original = json.loads(d2.write_analysis(suite, ["ref", "var"], "orig", reference="ref", resamples=60).read_text())
    rounded = [d2.round_row(r) for r in original["signals"]]
    expected = d2.analyze(rounded, ["ref", "var"], "ref", resamples=60)
    tables = []
    for decoder, path in (("ref", "oracle"), ("ref", "detector"), ("var", "oracle"), ("var", "detector")):
        tables.append(tmp_path / "tables" / f"{decoder}-{path}.csv")
        experiments.main(["scores", "--analysis", str(suite / "experiments" / "devset2-orig.json"), "--decoder", decoder,
                          "--path", path, "--csv", str(tables[-1])])
    # the suite is gone: the analysis reads the tables alone
    import shutil
    shutil.rmtree(suite)
    out = tmp_path / "analysis"
    experiments.main(["devset2", "--out", str(out), "--name", "tables", "--decoder", "ref", "--decoder", "var",
                      "--reference", "ref", "--resamples", "60"] + sum((["--scores", str(t)] for t in tables), []))
    got = json.loads((out / "experiments" / "devset2-tables.json").read_text())
    # each decoder's oracle rows keep their order across the per-path tables, so the fits' bootstrap draws are the
    # same (everything but the fits' CPU time); the signals list is the tables' concatenation
    assert _without_cpu(got["decoders"]) == _without_cpu(json.loads(json.dumps(expected["decoders"])))
    assert got["paired"] == json.loads(json.dumps(expected["paired"]))
    assert sorted(map(json.dumps, got["signals"])) == sorted(json.dumps(r) for r in json.loads(json.dumps(rounded)))
    assert got["meta"]["scores"] and got["meta"]["results_dirs"] == []
    md = (out / "experiments" / "devset2-tables.md").read_text(encoding="utf-8")
    assert "score tables" in md and str(tmp_path) not in md
    # rounding moves the fit, but only slightly: the crossings agree with the unrounded analysis to 0.05 dB
    for name in ("ref", "var"):
        for c, o in zip(got["decoders"][name]["fit"]["s500"]["crossings_0.10"],
                        original["decoders"][name]["fit"]["s500"]["crossings_0.10"]):
            assert (c["value"] is None) == (o["value"] is None)
            if c["value"] is not None:
                assert c["value"] == pytest.approx(o["value"], abs=0.05)


def test_a_rounded_speed_across_a_cell_edge_keeps_the_floor_of_its_drawn_cell():
    """Rounding to 0.01 WPM can carry a speed across a cell edge (10.0716 WPM in cell 2 -> 10.07 WPM, below the
    10.0715 WPM edge). The fit takes each signal's floor from its row's speed cell, so the rounded rows of a fit over
    cells 2-10 still fit no cell-1 floor (before this, one such signal fitted cell 1's floor alone, to CER 1)."""
    s, v, k, n = synth(np.random.default_rng(3), per_cell=2)
    keep = v >= SPEED_CELLS.edges[1]
    rows = [{**_row("ref", f"A2-awgn-c{SPEED_CELLS.cell_of(vi):02d}-0-s1", i, int(ki), int(ni),
                    cell=SPEED_CELLS.cell_of(vi)), "speed_wpm": float(vi), "s500_db": float(si),
             "en0_db": float(d2.en0_db(si, vi))}
            for i, (si, vi, ki, ni) in enumerate(zip(s[keep], v[keep], k[keep], n[keep]))]
    edge = SPEED_CELLS.edges[1]
    rows[0] = {**rows[0], "speed_wpm": edge + 0.0004, "speed_cell": 2, "s500_db": -7.5, "edits": 120, "symbols": 120}
    rounded = [d2.round_row(r) for r in rows]
    assert rounded[0]["speed_wpm"] < edge and SPEED_CELLS.cell_of(rounded[0]["speed_wpm"]) == 1
    assert list(d2.row_cells(rounded)) == [r["speed_cell"] - 1 for r in rows]
    assert list(d2.row_cells(rows)) == list(d2.cell_index([r["speed_wpm"] for r in rows]))  # unrounded: the same
    fit = d2.fit_decoder(rounded, resamples=10, key=("edge",))
    assert fit["s500"]["fit"]["fitted_cells"] == list(range(2, 11)) and fit["s500"]["fit"]["floors"]["1"] is None
    assert fit["s500"]["crossings_0.10"][0]["no_crossing"] == "no data"
