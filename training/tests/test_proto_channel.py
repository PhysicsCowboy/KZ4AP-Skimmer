import functools
import math

import numpy as np
import pytest

from kz4ap_proto.bank import realized_lengths_s
from kz4ap_proto.channel import Char, ChannelDecoder, Output
from kz4ap_proto.params import ProtoConfig
from kz4ap_synth.generate import keying_envelope
from kz4ap_synth.keying import timed_intervals
from kz4ap_synth.morse import keying_intervals

RATE = 1500.0
CFG = ProtoConfig()


def stream(intervals, start_s, duration_s, s500_db, seed):
    """A 1 FS carrier keyed by intervals (5 ms raised-cosine edges) from start_s, in white noise giving S500
    = s500_db: noise power per complex sample 1 / (10^(S500/10) x 500 Hz / r) = 3 x 10^(-S500/10) FS^2."""
    n = int(round(duration_s * RATE))
    rng = np.random.default_rng(seed)
    env = keying_envelope(intervals, start_s, n, int(RATE)) if intervals else np.zeros(n)
    power = 3.0 * 10 ** (-s500_db / 10)
    return env * np.exp(1j * rng.uniform(0, 2 * np.pi)) + (
        rng.standard_normal(n) + 1j * rng.standard_normal(n)) * math.sqrt(power / 2)


def norm(s):
    return " ".join(s.split())


def cer(reference, decoded):
    a, b = reference, decoded
    row = list(range(len(b) + 1))
    for i in range(1, len(a) + 1):
        diag, row[0] = row[0], i
        for j in range(1, len(b) + 1):
            above = row[j]
            row[j] = min(row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1]))
            diag = above
    return row[len(b)] / len(a)


def run(u):
    return ChannelDecoder(CFG, RATE).run(u)


def test_output_appends_and_corrects_within_the_reach():
    out = Output(20.0)
    out.append_new([Char("A", 1.0, 1.1), Char("B", 2.0, 2.1)])
    out.append_new([Char("A", 1.0, 1.1), Char("B", 2.0, 2.1), Char("C", 3.0, 3.1)])
    assert out.text() == "ABC"
    out.replace_from(2.0, [Char("X", 2.0, 2.1), Char("Y", 3.0, 3.1)], 4.0, "switch")
    assert out.text() == "AXY"
    c = out.corrections[-1]
    assert (c.from_s, c.reach_s, c.old, c.new, c.reason) == (2.0, 2.0, "BC", "XY", "switch")
    out.replace_from(0.0, [Char("Z", 1.0, 1.1)], 30.0, "rekey")  # reaches only 20 s back: from 10 s
    assert out.text() == "AXY" and len(out.corrections) == 1


def test_output_replaces_a_character_that_two_branches_time_a_few_ms_apart_once():
    # The old branch's copy of Q starts 0.7 ms before the new branch's (each removes its own group delay, but their
    # edges cross at different points of their ramps): the switch must replace it, not publish it twice.
    out = Output(20.0)
    out.append_new([Char("C", 1.004, 1.40), Char("Q", 1.6750, 1.95), Char(" ", 1.95, 1.95)])
    out.replace_from(1.6757, [Char("C", 1.0045, 1.40), Char("Q", 1.6757, 1.95), Char(" ", 1.95, 1.95)], 2.8, "switch")
    assert out.text() == "CQ "
    assert out.chars[1].start_s == 1.6757  # the new branch's copy
    assert out.corrections == []           # same text: no correction
    # a character that starts before the reach (t - 20 s) is kept, and a new one overlapping it is not taken
    out = Output(20.0)
    out.append_new([Char("A", 9.98, 10.10), Char("B", 10.5, 10.6)])
    out.replace_from(9.0, [Char("A", 9.99, 10.10), Char("X", 10.5, 10.6)], 30.0, "switch")
    assert out.text() == "AX"
    c = out.corrections[-1]
    assert (c.from_s, c.reach_s, c.old, c.new) == (10.5, 19.5, "B", "X")


def test_decodes_a_clean_station():
    iv = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 3.0, 20.0, 1))
    assert norm(r.text) == "CQ TEST K1ABC K1ABC"


def test_decodes_a_station_from_the_first_sample():
    iv = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    r = run(stream(iv, 0.0, iv[-1][1] + 3.0, 20.0, 2))
    assert cer("CQ TEST K1ABC K1ABC", norm(r.text)) <= 0.1


def test_slow_first_word_is_right_after_corrections():
    # Regression R2: a 12 WPM station's first dits must not stay read as dahs.
    iv = keying_intervals("HI HI TEST", 12.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 3))
    assert norm(r.text).startswith("HI HI")


@pytest.mark.xfail(strict=True, reason=(
    "Finding (Task 11), design/placeholders: the step is followed after 11 marks (1.73 s), the same for seeds 1-4. "
    "The fits stay at the 15 WPM dit (about 80 ms) while the 2 s periodicity window's confident T_P is 80.3 ms "
    "(the T_P prior, 0.1 in ln T, holds them); T_P changes to 40.0 ms 1.0 s after the step (t = 11.605 s), the "
    "index-13 fit follows at the next observation, and the switch then needs M = 4 eligible instants in a row "
    "(the one fallback pick of index 13 just before restarts the count): 11.776 ... 12.331 s. Without the T_P "
    "prior it takes 15 marks (2.22 s). Placeholders: M (E6), windows (E2); prior width heuristic."))
def test_follows_a_speed_step_within_ten_marks():
    # Spec 4.6: a real speed jump (15 -> 30 WPM) must be followed within about 10 marks.
    text = "CQ CQ CQ DE K1ABC K1ABC K1ABC K1ABC"
    iv = timed_intervals(text, 15.0, wpm_end=30.0, profile="step")
    start = 1.0
    step_s = start + iv[28][0]  # words 0-3 (CQ CQ CQ DE) have 28 marks; word 4 is the first at 30 WPM
    r = run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, 4))
    lengths = realized_lengths_s(CFG, RATE)
    followed = next((t for t, k, _ in r.selections
                     if t > step_s and abs(math.log(lengths[k] / (0.8 * 1.2 / 30.0))) <= math.log(1.1)), None)
    assert followed is not None, "no branch matched to 30 WPM was ever selected after the step"
    marks = sum(1 for a, _ in iv if step_s <= start + a <= followed)
    print(f"followed {followed - step_s:.2f} s and {marks} marks after the step")
    assert marks <= 10


@functools.lru_cache(maxsize=None)
def farnsworth():
    iv = timed_intervals("CQ TEST K1ABC", 18.0, farnsworth_wpm=10.0)  # word gaps 1.45 s, T_new 2.5 s
    start = 1.0
    return iv, start, run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, 5))


def test_farnsworth_word_gaps_do_not_start_a_new_over():
    iv, start, r = farnsworth()
    assert [t for t in r.over_starts if t < start + iv[-1][1]] == []


@pytest.mark.xfail(strict=True, reason=(
    "Finding (Task 11), design: no word gap starts an over (the test above), but the text reads 'CQ TEST U1ABC'. "
    "The default comb's T_P locks on T_g for Farnsworth 18/10 WPM in the 5 s window (202.5-206.6 ms against "
    "T = 66.7 ms; the Task 9 finding), and the confident T_P prior (0.1 in ln T, about 63 nats at "
    "ln(204.5/66.8)) pulls the selected branch's fit (index 18, L = 53.3 ms) from T = 66.6 ms to T = 149.5 ms, "
    "w = 71 ms, so K's first dah (199 ms) reads as a dit. With the edge comb, or without the prior, the text is "
    "right. Periodicity method: E1."))
def test_farnsworth_text_is_right():
    _, _, r = farnsworth()
    assert norm(r.text) == "CQ TEST K1ABC"


def test_noise_alone_leaves_no_text():
    r = run(stream([], 0.0, 30.0, 20.0, 6))
    assert len(norm(r.text).replace(" ", "")) <= 2
    assert all(c.reach_s <= 20.0 + 1e-9 for c in r.corrections)


def test_two_overs_at_different_speeds():
    a = keying_intervals("CQ DE K1ABC K", 25.0)
    b = keying_intervals("K1ABC DE W9XYZ K", 15.0)
    gap = a[-1][1] + 2.0
    iv = a + [(s + gap, e + gap) for s, e in b]
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 7))
    assert cer("CQ DE K1ABC K K1ABC DE W9XYZ K", norm(r.text)) <= 0.1


def test_a_tune_up_carrier_does_not_derail_decoding():
    body = keying_intervals("CQ TEST K1ABC K1ABC", 25.0)
    iv = [(0.0, 2.0)] + [(s + 2.5, e + 2.5) for s, e in body]
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 8))
    assert "TEST K1ABC K1ABC" in norm(r.text)


def test_corrections_never_reach_back_more_than_20_s():
    iv = keying_intervals(" ".join(["CQ TEST K1ABC"] * 8), 20.0)
    r = run(stream(iv, 1.0, iv[-1][1] + 3.0, 8.0, 9))
    assert all(c.reach_s <= 20.0 + 1e-9 for c in r.corrections)
    assert r.to_json()["corrections"] == [c.__dict__ for c in r.corrections]


def test_short_and_empty_streams():
    assert run(np.zeros(0, complex)).text == ""
    assert isinstance(run(stream([], 0.0, 0.3, 20.0, 10)).text, str)


def test_keeps_branch_ones_posterior_for_the_offline_experiments():
    u = stream(keying_intervals("TEST", 25.0), 0.5, 2.0, 20.0, 11)
    r = ChannelDecoder(CFG, RATE).run(u, keep_p1=True)
    assert r.p1.shape == (len(u),) and r.p1.dtype == np.float32 and 0.0 <= r.p1.min() <= r.p1.max() <= 1.0
    assert ChannelDecoder(CFG, RATE).run(u).p1 is None


@functools.lru_cache(maxsize=None)
def same_speed_turnover():
    """Task 11 review's case: two overs at the same speed, 3 s apart; the second starts with E and T only, so its
    first re-keyed marks and spaces fit a wrong speed as well as the right one."""
    a = keying_intervals("CQ DE K1ABC K", 25.0)
    b = keying_intervals("EE TT EE TT K1ABC", 25.0)
    gap = a[-1][1] + 3.0
    iv = a + [(s + gap, e + gap) for s, e in b]
    return iv, gap, run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 7))


def test_a_same_speed_turnover_keeps_the_previous_over_s_fit():
    # Before the fix the fresh fit won the re-key on 2-3 durations (T = 101.6 ms, true 48 ms): "IN EE TE U1ABC".
    _, _, r = same_speed_turnover()
    assert norm(r.text).startswith("CQ DE K1ABC K EE TT EE TT ")


@pytest.mark.xfail(strict=True, reason=(
    "Finding (Task 11 fix round 1), design: the second over reads 'EE TT EE TT U1ABC'. The previous fit wins the "
    "re-key (11.2 s), but its rival fresh fit takes over at about 12.4 s (12 marks and spaces of the over, penalty "
    "1/2 x 4 x ln 12 = 5.0 nats) with T = 102.4 ms, q = 2.0, w = -54.7 ms: it reads the character gaps of EE TT "
    "(144 ms) as element spaces (157 ms) and the word gaps as character gaps, an ambiguity this text cannot "
    "resolve, favored by the element-space prior (0.647 against 0.238, about 1 nat per space). K's first dah "
    "(143 ms) then reads as a dit under T = 128 ms before the fit recovers (T = 47.9 ms by 13.26 s)."))
def test_a_same_speed_turnover_decodes_the_whole_second_over():
    _, _, r = same_speed_turnover()
    assert norm(r.text) == "CQ DE K1ABC K EE TT EE TT K1ABC"


def test_noise_after_the_last_over_is_corrected_away_and_one_over_start_per_over():
    # Before the fix: false key-downs after the last over were published as "E E" and never corrected, and each false
    # key-up started an over (16.597, 17.429, 18.155 s). Now the re-key time-out deletes them, and an over start
    # counts only once its re-key keyed a mark: one, between the first over's end and the second's start.
    _, gap, r = same_speed_turnover()
    first_end = 1.0 + keying_intervals("CQ DE K1ABC K", 25.0)[-1][1]
    assert norm(r.text).endswith("1ABC")
    assert len(r.over_starts) == 1 and first_end < r.over_starts[0] < 1.0 + gap
    assert any(c.reason == "timeout" for c in r.corrections)


def test_a_speed_change_across_a_turnover_is_taken_up():
    # 15 WPM, then 3 s of silence, then 30 WPM: the second over's fresh fit replaces the first over's once it has
    # enough marks and spaces and beats it by the penalty, and a branch matched to 30 WPM decodes the rest.
    a = keying_intervals("CQ DE K1ABC K", 15.0)
    b = keying_intervals("TEST DE W9XYZ W9XYZ K", 30.0)
    gap = a[-1][1] + 3.0
    iv = a + [(s + gap, e + gap) for s, e in b]
    r = run(stream(iv, 1.0, iv[-1][1] + 4.0, 20.0, 12))
    lengths = realized_lengths_s(CFG, RATE)
    _, k, t = r.selections[-1]
    assert abs(math.log(lengths[k] / (0.8 * 1.2 / 30.0))) <= math.log(1.1) and abs(t / 0.040 - 1.0) < 0.05
    assert norm(r.text).startswith("CQ DE K1ABC K ") and norm(r.text).endswith("DE W9XYZ W9XYZ K")
    assert len(r.over_starts) == 1
