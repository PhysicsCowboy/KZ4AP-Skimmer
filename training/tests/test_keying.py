import math

import numpy as np
import pytest

from kz4ap_synth.keying import STYLES, Duration, draw_imbalance_dits, draw_style, farnsworth_gap_s, timed_intervals
from kz4ap_synth.morse import keying_intervals


def marks(intervals):
    return [off - on for on, off in intervals]


def spaces(intervals):
    return [b[0] - a[1] for a, b in zip(intervals, intervals[1:])]


def test_machine_style_is_exact_paris_timing():
    assert timed_intervals("PARIS CQ", 20.0) == keying_intervals("PARIS CQ", 20.0)


def test_styles_are_ve3nea_table():
    assert STYLES["paddle"].dah == Duration(math.exp(1.10), 0.016)
    assert STYLES["paddle"].char_gap == Duration(math.exp(1.10), 0.2)
    assert STYLES["computer"].word_gap == Duration(math.exp(1.94), 0.008)
    assert STYLES["bug"].dah == Duration(math.exp(1.10), 0.2)
    assert STYLES["hand"].dit == Duration(1.0, 0.15)
    assert STYLES["hand"].word_gap == Duration(math.exp(2.0), 0.2)


def test_paddle_dahs_are_steadier_than_its_character_spaces():
    rng = np.random.default_rng(1)
    iv = timed_intervals(" ".join(["TT"] * 300), 20.0, "paddle", rng)
    dit = 1.2 / 20.0
    dahs = np.array(marks(iv)) / dit
    char_gaps = np.array(spaces(iv))[0::2] / dit  # T T | T T | ...: character and word spaces alternate
    assert np.median(dahs) == pytest.approx(math.exp(1.10), rel=0.01)
    assert np.std(np.log(dahs)) == pytest.approx(0.016, abs=0.004)
    assert np.std(np.log(char_gaps)) == pytest.approx(0.2, abs=0.03)


def test_hand_key_dah_median_and_spread_follow_ve3nea():
    rng = np.random.default_rng(2)
    iv = timed_intervals(" ".join(["TE"] * 400), 20.0, "hand", rng)
    dit = 1.2 / 20.0
    lengths = np.array(marks(iv)) / dit
    dahs = lengths[0::2]
    dits = lengths[1::2]
    assert np.median(dahs) / np.median(dits) == pytest.approx(math.exp(1.5), rel=0.06)
    assert np.std(np.log(dahs)) == pytest.approx(0.3, abs=0.03)


def test_same_generator_state_gives_the_same_timing():
    a = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(5))
    b = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(5))
    c = timed_intervals("CQ TEST", 25.0, "bug", np.random.default_rng(6))
    assert a == b
    assert a != c


def test_speed_step_switches_at_the_middle_word():
    iv = timed_intervals("E E E E", 20.0, wpm_end=40.0, profile="step")
    assert marks(iv) == pytest.approx([0.06, 0.06, 0.03, 0.03])


def test_speed_ramp_changes_linearly_per_word():
    iv = timed_intervals("E E E E E", 20.0, wpm_end=40.0, profile="ramp")
    assert marks(iv) == pytest.approx([1.2 / w for w in (20.0, 25.0, 30.0, 35.0, 40.0)])


def test_imbalance_lengthens_marks_and_shortens_spaces():
    iv = timed_intervals("EE", 20.0, imbalance_dits=0.1)
    assert marks(iv) == pytest.approx([1.1 * 0.06, 1.1 * 0.06])
    assert spaces(iv) == pytest.approx([2.9 * 0.06])


def test_style_mix_and_imbalance_follow_ve3nea():
    rng = np.random.default_rng(3)
    styles = [draw_style(rng) for _ in range(4000)]
    assert set(styles) == {"hand", "paddle", "computer"}
    assert styles.count("hand") / 4000 == pytest.approx(0.25, abs=0.03)
    assert styles.count("paddle") / 4000 == pytest.approx(0.50, abs=0.03)
    imbalances = [draw_imbalance_dits(rng) for _ in range(4000)]
    assert np.std(imbalances) == pytest.approx(0.1, abs=0.01)
    assert np.mean(imbalances) == pytest.approx(0.0, abs=0.01)


def test_unknown_style_or_profile_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "straight")
    with pytest.raises(ValueError):
        timed_intervals("E E", 20.0, wpm_end=30.0, profile="sine")


def test_random_style_without_a_generator_raises():
    with pytest.raises(ValueError):
        timed_intervals("E", 20.0, "hand")


def test_every_style_is_defined():
    assert set(STYLES) == {"machine", "computer", "paddle", "bug", "hand"}


def test_farnsworth_gap_timebase_follows_the_arrl_formula():
    # T_g = (60/s - 37.2/c) / 19: the added time per PARIS spread over its 19 gap units.
    assert farnsworth_gap_s(18.0, 5.0) == pytest.approx((60.0 / 5.0 - 37.2 / 18.0) / 19.0)
    assert farnsworth_gap_s(18.0, 5.0) == pytest.approx(0.52281, abs=1e-5)
    assert farnsworth_gap_s(25.0, 25.0) == pytest.approx(1.2 / 25.0)  # no stretch: the dit
    with pytest.raises(ValueError):
        farnsworth_gap_s(18.0, 20.0)
    with pytest.raises(ValueError):
        farnsworth_gap_s(18.0, 0.0)


def test_farnsworth_stretches_only_character_and_word_gaps():
    c, s = 18.0, 10.0
    dit, tg = 1.2 / c, farnsworth_gap_s(c, s)
    iv = timed_intervals("AN IT", c, farnsworth_wpm=s)
    marks = [b - a for a, b in iv]
    spaces = [iv[i + 1][0] - iv[i][1] for i in range(len(iv) - 1)]
    assert marks == pytest.approx([dit, 3 * dit, 3 * dit, dit, dit, dit, 3 * dit])
    # .- | -. || .. | -  : element, character, element, word, element, character
    assert spaces == pytest.approx([dit, 3 * tg, dit, 7 * tg, dit, 3 * tg])


def test_one_paris_takes_sixty_seconds_over_the_overall_speed():
    # PARIS: 31 units at T and 19 at T_g; 31 * 1.2/c + (60/s - 37.2/c) = 60/s (derived).
    iv = timed_intervals("PARIS PARIS", 25.0, farnsworth_wpm=13.0)
    assert iv[14][0] == pytest.approx(60.0 / 13.0)  # PARIS has 14 elements


def test_farnsworth_at_the_character_speed_is_standard_spacing():
    assert timed_intervals("CQ TEST", 20.0, farnsworth_wpm=20.0) == pytest.approx(keying_intervals("CQ TEST", 20.0))


def test_farnsworth_with_random_keying_scales_the_gap_medians():
    rng = np.random.default_rng(3)
    c, s = 20.0, 8.0
    tg = farnsworth_gap_s(c, s)
    iv = timed_intervals(" ".join(["TEST"] * 200), c, "paddle", rng, farnsworth_wpm=s)
    spaces = np.array([iv[i + 1][0] - iv[i][1] for i in range(len(iv) - 1)])
    words = spaces[spaces > 5 * tg]
    assert np.median(words) == pytest.approx(np.exp(1.94) * tg, rel=0.05)  # paddle word gap median e^1.94 units
