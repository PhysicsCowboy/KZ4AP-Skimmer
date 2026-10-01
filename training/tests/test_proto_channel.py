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
    "branch-13 fit follows at the next observation, and the switch then needs M = 4 eligible instants in a row "
    "(the one fallback pick of branch 13 just before restarts the count): 11.776 ... 12.331 s. Without the T_P "
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


@pytest.mark.xfail(strict=True, reason=(
    "Finding (Task 11), design: no word gap starts an over (that part passes), but the text reads 'CQ TEST U1ABC'. "
    "The default comb's T_P locks on T_g for Farnsworth 18/10 WPM in the 5 s window (202.5-206.6 ms against "
    "T = 66.7 ms; the Task 9 finding), and the confident T_P prior (0.1 in ln T, about 63 nats at "
    "ln(204.5/66.8)) pulls the selected branch's fit (index 18, L = 53.3 ms) from T = 66.6 ms to T = 149.5 ms, "
    "w = 71 ms, so K's first dah (199 ms) reads as a dit. With the edge comb, or without the prior, the text is "
    "right. Periodicity method: E1."))
def test_farnsworth_word_gaps_do_not_start_a_new_over():
    iv = timed_intervals("CQ TEST K1ABC", 18.0, farnsworth_wpm=10.0)  # word gaps 1.45 s, T_new 2.5 s
    start = 1.0
    r = run(stream(iv, start, iv[-1][1] + start + 3.0, 20.0, 5))
    assert [t for t in r.over_starts if t < start + iv[-1][1]] == []
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
