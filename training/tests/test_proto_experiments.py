import re

import pytest

from kz4ap_proto import experiments
from kz4ap_proto.params import ProtoConfig


def row(fe, index, edits, symbols=100, group="A sensitivity"):
    return {"front_end": fe, "group": group, "tag": "t", "recording": "r", "index": index, "scored": True,
            "edits": edits, "symbols": symbols, "first_word_edits": 0, "first_word_symbols": 0}


def test_the_development_set_is_seed_one_without_the_per_row_fading_recordings():
    names = ["A-awgn-12wpm-0-s1", "B-fading-mix-s1", "B-fading-paddle-24wpm-0.1Hz-s1", "C-fists-bug-s1",
             "D-speed-s1", "H-qso-oracle-s1.stations", "I-farnsworth-machine-s1", "A-awgn-12wpm-0-s2", "strong-s1"]
    assert [n for n in names if re.search(experiments.DEV, n)] == [
        "A-awgn-12wpm-0-s1", "B-fading-mix-s1", "C-fists-bug-s1", "D-speed-s1", "H-qso-oracle-s1.stations",
        "I-farnsworth-machine-s1"]


def test_pooled_paired_differences_by_group_and_overall():
    rows = [row("ref", i, 10) for i in range(4)] + [row("var", i, 5) for i in range(4)]
    rows += [row("ref", 9, 10, group="D speed"), row("var", 9, 30, group="D speed")]
    d = experiments.pooled_paired(rows, "ref", "var")
    assert d["A sensitivity"]["mean"] == pytest.approx(-0.05) and d["A sensitivity"]["signals"] == 4
    assert d["all"]["mean"] == pytest.approx((4 * -0.05 + 0.2) / 5)
    assert d["D speed"]["interval"] is None  # one signal: no interval


def test_calibrated_thresholds_make_noise_key_at_the_target_rate():
    # Calibrated at 0.5 key-downs per second (fast to measure); fresh channel-shaped noise must then key branch 1
    # near that rate: 100 expected in 200 s (Poisson spread about 10).
    import numpy as np
    from kz4ap_proto.bank import boxcar
    from kz4ap_proto.keying import BankKeyer, edges
    cfg = ProtoConfig(false_marks_per_s=0.5)
    x_on = experiments.calibrate_x_on(cfg, events=200)
    assert len(x_on) == 32 and all(1.55 < x < 7.0 for x in x_on)
    rng = np.random.default_rng(99)
    n = int(200 * 1500)
    h = experiments.lowpass()
    u = np.convolve((rng.standard_normal(n) + 1j * rng.standard_normal(n)) / np.sqrt(2), h, mode="same")
    sigma2 = 0.5 * np.sum(np.convolve(h, np.ones(14) / 14) ** 2)
    keyer = BankKeyer(cfg.with_values(x_on_values=x_on), 1500.0, np.array([14 / 1500] + [0.1] * 31))
    P = np.abs(boxcar(u, 14)) ** 2
    key, _, _, _ = keyer.step(np.vstack([P] * 32)[:, :], np.full(32, sigma2))
    downs = sum(1 for _, down in edges(key[:1], np.zeros(1, bool), 0)[0] if down)
    assert 60 <= downs <= 150


def test_follow_marks_counts_marks_until_a_matched_branch_is_selected():
    counts = experiments.follow_marks(ProtoConfig(), [1])
    assert len(counts) == 1 and (counts[0] is None or 0 <= counts[0] <= 40)
