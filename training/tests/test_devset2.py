"""The jittered development set's analysis (kz4ap_proto.devset2) and figures (kz4ap_proto.figures), on synthetic CER
data with a known model. Figure tests skip where matplotlib is absent (CI installs only requirements.txt)."""

import json
import math

import numpy as np
import pytest

from kz4ap_proto import devset2 as d2
from kz4ap_proto import experiments
from kz4ap_synth.jitter import SNR_CELLS, SPEED_CELLS

# A known model: s0 = 1 + 1.5 x + 1.0 x^2 dB SNR in 500 Hz, w = 1.2 exp(0.2 x) dB, floor 0.003 (x = ln(v / 25.30 WPM)).
TRUTH = d2.Fit((1.0, 1.5, 1.0, math.log(1.2), 0.2, 0.0, d2._logit(0.003)))
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
    high_floor = d2.Fit(TRUTH.params[:6] + (d2._logit(0.2),))
    assert np.all(np.isnan(high_floor.crossing_db(0.10, CENTERS)))


@pytest.mark.parametrize("method", d2.METHODS)
def test_the_fit_recovers_a_known_s0_and_w(method):
    """Tolerances: about 4x the spread measured over 40 synthetic sets (task D2 report): s0 0.064 dB, w 4.3%,
    the CER-0.10 crossing 0.10 dB, the floor 0.0006 (worst cell, either method)."""
    s, v, k, n = synth(np.random.default_rng(7))
    fit = d2.fit_cer(s, v, k, n, method=method)
    assert fit.converged
    assert np.max(np.abs(fit.s0_db(CENTERS) - TRUTH.s0_db(CENTERS))) < 0.25
    assert np.max(np.abs(fit.w_db(CENTERS) / TRUTH.w_db(CENTERS) - 1)) < 0.20
    assert np.max(np.abs(fit.crossing_db(0.10, CENTERS) - TRUTH.crossing_db(0.10, CENTERS))) < 0.4
    assert np.max(np.abs(fit.crossing_db(0.05, CENTERS) - TRUTH.crossing_db(0.05, CENTERS))) < 0.4
    assert abs(fit.floor - TRUTH.floor) < 0.003


def test_least_squares_tracks_the_mean_cer_when_whole_signals_fail():
    """3% of the signals fail as a whole (CER 0.5-1, mean 0.75): the mean CER is 0.97 f + 0.0225, whose 0.10
    crossing is where f = 0.0799. Over 40 sets, least squares (the default) follows it with a mean bias of 0.01-0.06
    dB per cell (RMS 0.18-0.45 dB; the mean's standard error up to 0.07 dB), the binomial likelihood with 0.20-0.27 dB
    (RMS 0.33-0.62 dB): the measurement behind FIT_METHOD."""
    target = TRUTH.crossing_db((0.10 - 0.03 * 0.75) / 0.97, CENTERS)
    errors = {m: [] for m in d2.METHODS}
    for rep in range(40):
        s, v, k, n = synth(np.random.default_rng(rep), fail=0.03)
        for m in d2.METHODS:
            errors[m].append(d2.fit_cer(s, v, k, n, method=m).crossing_db(0.10, CENTERS) - target)
    bias = {m: np.abs(np.mean(e, axis=0)) for m, e in errors.items()}
    assert d2.FIT_METHOD == "wls"
    assert np.max(bias["wls"]) < 0.15
    assert np.mean(bias["wls"]) < 0.5 * np.mean(bias["binomial"])


def test_the_bootstrap_intervals_cover_the_truth_and_are_reproducible():
    s, v, k, n = synth(np.random.default_rng(11))
    fit = d2.fit_cer(s, v, k, n)
    boots = d2.bootstrap_fits(s, v, k, n, fit, "test", resamples=200)
    assert sum(b is None for b in boots) == 0
    cross = d2.crossings(fit, boots, CENTERS, 0.10)
    truth = TRUTH.crossing_db(0.10, CENTERS)
    covered = sum(c["interval"][0] <= t <= c["interval"][1] for c, t in zip(cross, truth))
    assert covered >= 8  # 95% intervals: 10 of 10 expected, 8 allowed
    assert all(0.0 < c["interval"][1] - c["interval"][0] < 1.0 for c in cross)
    again = d2.crossings(fit, d2.bootstrap_fits(s, v, k, n, fit, "test", resamples=200), CENTERS, 0.10)
    assert again == cross


def test_the_fit_against_e_n0_is_the_s500_fit_in_other_coordinates():
    # s0 quadratic in ln v absorbs the shift 10 log10(600 / v) = const - 4.34 ln v, so both fits are one model
    s, v, k, n = synth(np.random.default_rng(3))
    fs = d2.fit_cer(s, v, k, n, axis="s500")
    fe = d2.fit_cer(d2.en0_db(s, v), v, k, n, axis="en0")
    assert np.allclose(fe.crossing_db(0.10, CENTERS), d2.en0_db(fs.crossing_db(0.10, CENTERS), CENTERS), atol=0.02)
    assert fe.params[1] == pytest.approx(fs.params[1] - 10.0 / math.log(10.0), abs=0.05)


def test_the_invariance_test_accepts_an_invariant_decoder_and_rejects_a_fixed_s500_one():
    invariant = d2.Fit((12.0, 0.0, 0.0, math.log(1.0), 0.0, 0.0, d2._logit(0.003)), axis="en0")
    s, v, k, n = synth(np.random.default_rng(5), truth=invariant, axis="en0")
    e = d2.en0_db(s, v)
    fit = d2.fit_cer(e, v, k, n, axis="en0")
    wald = d2._wald(d2.bootstrap_fits(e, v, k, n, fit, "inv", resamples=200), fit)
    assert wald["p_value"] > 0.01
    # a decoder whose curve is fixed in S500 (s0 = 0 dB SNR in 500 Hz at every speed): s0 in E/N0 falls 4.34 dB per
    # unit of ln v, 10 dB over 8-80 WPM
    fixed = d2.Fit((0.0, 0.0, 0.0, math.log(1.0), 0.0, 0.0, d2._logit(0.003)))
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
    recordings, with results for decoders "ref" and "var" (var's s0 0.5 dB lower) drawn from the known model."""
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


def test_the_development_analysis_selects_dev2_and_the_detector_copies():
    import re
    assert re.search(d2.DEV2_ANALYSIS, "A2-detector-c03-1-s1")
    assert re.search(d2.DEV2_ANALYSIS, "H2-qso-c01-0-s1")
    assert not re.search(d2.DEV2_ANALYSIS, "A2-awgn-c03-1-s2")
    assert not re.search(d2.DEV2_ANALYSIS, "A-awgn-25wpm-0-s1")
    # every DEV2 test case is in the analysis's selection
    for name in ("A2-awgn-c01-0-s1", "S2-stretch-c05-0-s1", "F2-drift-c02-0-s1"):
        assert re.search(experiments.DEV2, name) and re.search(d2.DEV2_ANALYSIS, name)
