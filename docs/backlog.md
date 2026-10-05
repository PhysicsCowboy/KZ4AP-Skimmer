# Backlog

Work deliberately deferred from milestone 1, so it isn't forgotten. Each item
says what's wrong, why it was deferred, and how to measure a fix. The design
spec (`docs/design/`) remains the authority; this is a to-do list.

Sections follow the development order after milestone 1 (design spec §3.1;
owner's decisions, 2026-09-27): (1) decoder robustness; (2) GUI and live
display; (3) receiver-audio input; (4) callsign matching; (5) telnet spot
server for local logging programs. RBN upload, and whether it is KZ4AP's
own server or a contribution to manta, are decided later.

## 1. Decoder robustness (next milestone)

Start by adding benchmark scenarios that expose each problem below, then fix
against the numbers. Several of these were found by review but are invisible
to the current benchmark, which only has clean, well-separated signals of
10–30 dB SNR (500 Hz, key-down).

### The development set's distribution of speeds and noise levels (owner, 2026-10-04; open for discussion)

The owner's judgment: the development set (`kz4ap_proto.experiments.DEV`, seed 1; 509 scored
signals) has a poorly chosen distribution of speeds, and to some extent of noise levels. Speeds
are concentrated near 25 WPM (groups C, E, G and a third of A at 25 WPM; B, H and I mostly 18 to
31 WPM); below 15 WPM there are only A's 12 WPM third, a few B signals and D's 10 WPM step;
above 40 WPM, A's 40 WPM third, a few B signals and D's 60 WPM step. S₅₀₀ (dB SNR in 500 Hz)
covers −10 to +20 dB on a regular grid only in group A; elsewhere mostly 0 to 20 dB. A set
built this way can show what a change costs near 25 WPM but hardly what it gains at slow or
fast speeds, which is where the stage-2 time-base work (time constants in dits) is aimed
(Plan B task B4a, results record section 6).

It is kept for now only so that results stay comparable with stage 1's and Plan A's
experiments. Open question, for discussion with the owner: whether to start new comparisons
on a redesigned development set (speeds spread evenly in ln WPM, e.g. 8 to 60 WPM; S₅₀₀
spread evenly across the decision-relevant range in every group), re-measuring the current
reference on it, rather than carrying the old set forward.

### Top priority: research, then implement a probabilistic decoder

The classical decoder is a hard-decision baseline (see
`docs/signal-processing.md`, section 8): it keys each sample against
thresholds, then times and classifies elements. It does not yet do what the
design spec asks, find the most probable character sequence given timing
statistics and a prior over likely text. Replace it with a probabilistic
decoder: an HMM with Viterbi decoding, or a beam search, over key states and
characters, with likelihoods computed from the measured signal and noise
levels and an optional text/callsign prior, and no hard thresholds.

Research survey: `docs/research/decoder-survey.md` (notes under
`docs/research/research_notes/`). Its ranked recommendation, in order of
evidence per CPU cycle, is:
1. a pre-detection filter matched to the dit, with soft (likelihood) output,
   on the existing baseline; this is also stage 2 of "Channel filtering, two
   stages" below.
   - **Prerequisite: precise frequency re-centering.** A dit-matched filter
     is only about 1/T wide (T the dit duration, s), so a station off
     center by Δf (Hz) loses a factor |sinc(Δf·T)| in amplitude. The
     channelizer's bin rounding alone leaves up to ±11.7 Hz, which costs up
     to 5.1 dB at 25 WPM and 8.8 dB at 20 WPM (signal power, relative to a
     centered station), half or more of the filter's expected gain. Each
     channel must track drift and be re-centered on a fine frequency
     estimate (for example to ±2 Hz, a 0.2 dB loss at 20 WPM) before any
     narrow filter ("Track frequency drift" below;
     `docs/research/proakis-ook-notes.md`, item 15).
   - **Soft likelihoods are correlated.** The log-likelihood ratio
     −a²/2 + ln I₀(a·x) (x the envelope and a the key-down amplitude, both
     normalized by the noise RMS per real component) is exact for one
     matched-filter output per element. Consecutive samples of a
     narrow-filtered envelope at 1500 samples/s are strongly correlated, so
     summing per-sample values overcounts the evidence. Scale them, or
     decimate to about one sample per 1/B (B the filter's noise bandwidth,
     Hz), before a sequence decoder sums them (proakis-ook-notes.md,
     item 13).
   - **Built** (milestone 2, part 1): `FrontEnd::Matched`, the default
     since the owner's decision of 2026-09-29; measured against the
     baseline in `docs/signal-processing.md` §8b. Tuning its parameters
     waits for evidence from these measurements. Group A at 25 WPM (oracle,
     3 seeds): S₅₀₀ at CER 0.10 = 5.1 dB (4.8 to 5.2 dB) Envelope, 2.7 dB
     (−0.0 to 10.4 dB) Matched; at CER 0.05, 5.6 dB (5.5 to 5.7 dB) and
     3.4 dB (1.7 to 11.2 dB). CPU per channel-second (whole process,
     i7-12700H): 0.202 ms Envelope, 0.381 ms Matched (decoders alone
     0.014 and 0.177 ms).
2. a small streaming CNN+LSTM network trained with CTC, following VE3NEA's
   DeepCW;
3. a Bell-style explicit-duration HMM with beam search and Kalman amplitude
   tracking. Start from Bell 1977's verified design
   (`docs/research/bell-1977-notes.md`, sections 2.2 and 4):
   - speed states 10–60 WPM (integers), changing only at element boundaries,
     in steps of ±2/±4 WPM after marks and element spaces, ±5/±10 after word
     spaces and ±10/±20 after pauses;
   - element durations normalized by the dit length at the hypothesized
     speed, with a Laplacian (two-sided exponential) density about the
     nominal 1, 3, 7 or 14 dits, applied as a hazard (transition
     probability given the time already spent in the element);
   - pruning: keep the best path for each of the 6 element types, then add
     paths in decreasing probability until they hold 0.9 of the total;
   - at most 25 paths, each extended into at most 30 successors (6 element
     types × 5 speed steps) per sample;
   - characters released at the common ancestor of all paths, forced at a
     1 s decision delay;
   - one scalar amplitude Kalman filter per path; estimated cost about
     2–3 Mflop/s per channel at about 200 samples/s.
   Change from Bell: feed it option 1's matched-filter likelihoods instead
   of his 100 Hz filter's envelope, work in log probabilities, and add an
   interference/impulse state (his field failures came from interference).
   **Lessons from manta's `hsmm` decoder**, a built and tested
   explicit-duration decoder of this kind (`docs/research/manta-notes.md`,
   sections 3–5):
   - it models element durations as **log-normal** (standard deviation of
     ln(duration) 0.22, durations admitted only within 0.6–1.5 × nominal),
     not Laplacian as Bell does; compare the two on benchmark category C
     (poor fists);
   - **one speed per hypothesis:** each token carries its own dit length,
     updated by an exponential average (weight 0.2 per element) within
     8–60 WPM and seeded at 5 speeds (about 12–50 WPM); tokens merge on
     (tree node, phase, quantized dit length);
   - **beam of 12 tokens** (Bell: at most 25 paths);
   - its likelihood is **Gaussian** on an amplitude normalized between the
     noise and mark levels, with a fixed spread (0.30 of the mark-to-noise
     difference), **not Rician**; ours should use option 1's Rician LLR,
     whose spread depends on SNR;
   - it **sums correlated samples without decimation:** per-sample LLRs at
     375 samples/s from a channel of 82.2 Hz noise bandwidth, whose noise
     correlation time is about 1/(82.2 Hz) ≈ 12 ms ≈ 4.6 samples, so its
     evidence is overweighted by a factor of about 4–5 against its duration
     and type priors (the caveat under option 1 above);
   - **results:** on manta's real-recording oracle (see "Real-recording
     scoring: manta's oracle" below), the call appeared as a whole decoded
     word in **56.1%** of 221 windows around K5TR's RBN spots, against
     26.7% for manta's threshold-keyed `legacy` decoder; it still failed its
     own acceptance gate (3 of 9 "real-conditions" synthetic vectors);
   - **CPU:** about 3.5 ms of CPU per track-second (Apple M4 Pro),
     **4.25× over its own budget**, and 3.9× the CPU of `legacy` on the
     real recording. Its anchor-based segmentation, in which cost scales
     with the number of keying edges rather than samples, is worth copying.
   manta is MIT OR Apache-2.0, so its code may be ported with its notices
   kept (design spec §3.2);
4. hybrids of 2 and 3.
See the survey's "Rank the candidates by evidence per CPU cycle" for the
evidence and costs. Verification of the survey's sources against Bell 1977
and Proakis & Salehi, and then against a second batch of six documents
(Gold 1959, the IEEE 2023 LSTM-CTC paper, AG1LE's 2012 eHam article, Wang
et al. 2018, YFDM 2023 and G4ILO's 2012 blog post), left the ranking
unchanged: none of the six is a decoder benchmark comparable to the ones
the ranking rests on. Notes: `docs/research/gold-1959-notes.md`,
`neural-papers-notes.md`, `wang-yfdm-notes.md`, `g4ilo-2012-notes.md`.
A third check, of morseformer (`docs/research/morseformer-notes.md`) and
three GitHub repositories (`docs/research/github-repos-notes.md`), also
left the ranking unchanged. morseformer has ready weights but costs about
100–140× option 2 per channel, does not stream, was trained on 16–28 WPM only,
and scores 28.6% CER on its one held-out real operator (the 17.75%
headline includes an operator it was fine-tuned on). It becomes a
reference decoder ("Integrate morseformer as a reference decoder" below),
not a ranked option. The three repositories publish no CER results.
A fourth check, of manta (`docs/research/manta-notes.md`), left the ranking
unchanged too: its `hsmm` decoder is a worked example of option 3, not
evidence that reorders the options (survey, "Rank the candidates").

The one paper from the survey still unread, the CNKI *Radio Engineering*
(无线电工程) 3D-CNN + bidirectional ConvLSTM paper, was deliberately
skipped by the owner (owner's decision, 2026-09-27); it is not on the
to-do list.

Optional, later: once the software suite works, offer several classical
decoders as a user choice.

### Stage-2 evaluation of the filter bank through the detector

The filter-bank redesign (`docs/design/2026-09-30-filter-bank-speed-estimator-design.md`)
is prototyped in stage 1 on oracle channels and on the channels the Matched
path's detector opens (group H through the detector, band, crowded, late
openings; owner, 2026-09-30), and it reports **detection recall, false tracks
and tracks per QSO** there for the prototype beside Matched and Envelope: as
the bench counts them (a track counts only if it decoded text) they depend on
the decoder. Stage 1 mixes by the detector's frequency without the frequency
tracker, on recorded channels. **Stage 2 confirms these measures** when the
bank runs as C++ (`--front-end bank`) behind the live detector, with the
tracker in the loop, on the 3-seed full suite, against the current Matched and
Envelope paths, with intervals (spec §7, stage 2: a required part of the
evaluation). Which tracks the detector opens is unchanged by the redesign. No
acceptance gate: the owner decides from the comparison.

### Let the text model take part in character decisions (stage 2)

In the stage-1 prototype the character model (VE3NEA's weights; invalid codes very unlikely) is used
only to break near-ties between branches, and it has almost no effect: turning it off raised the CER
by +0.0012 (+0.0001 to +0.0025), and no setting of its weight or window changed the CER by more than
0.003 (measured, E8; `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, section 3.12). Each
character is still decided from its durations alone. A text prior is the one thing that can take a
decoder past the noise-only bound (section 5.1.1), so in stage 2 consider letting the model take part
in the character decisions themselves, for example a sequence decoder that weighs the timing evidence
for each candidate character against its probability in context, and measure it on text that does not
come from the model's own frequencies (groups G and H; real recordings).

Owner, 2026-10-03: deferred until after stage 2 (`docs/design/2026-10-03-filter-bank-stage-2-design.md`,
§6), to be designed with real recordings scored against real spots; the owner shares the concern that a
text model can pull unusual callsigns toward plausible but wrong text.

### Following a speed jump: no switch setting meets the 10-mark target (stage 2)

Spec §4.6 asks the decoder to follow a jump in speed within about 10 marks. Stage 1's E6 measured the
marks needed to follow a 15 → 30 WPM step (10 seeds) at switch persistence M = 1, 2, 4, 6 and 8: medians
10.5, 11.5, 14, 16 and 18 marks; every seed was followed (`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`,
section 3.11). No M meets the target, and M = 4 was kept. So the limit is not the switch setting alone.
Conjectured, not traced: the fit memory of 48 elements (E4, adopted before E6) makes each branch's fit
follow a step about twice as slowly as 24 did (the fit's step test needs about 144 elements against 72,
section 3.7). Stage 2: find what limits following (fit memory, eligibility, the T_P prior) before
tuning M.

### Farnsworth word gaps start false overs (stage 2)

A silence longer than T_new = max(0.5 s, 12·T_g) starts a new over (fresh fit and amplitude, first marks
re-keyed). Farnsworth's long word gaps pass that test inside a transmission: in the reference before
E7, 16 of 48 Farnsworth transmissions had a false over start (0.333 per transmission) against the
rule's limit of 0.05; the variants tried did not fix it (the nearest, 16·T_g, gave 0.0625), so the
threshold was kept (measured, E7; results section 3.10). A false over start discards the station's
fit and amplitude mid-transmission. Stage 2: make the over-start test aware of the station's own
spacing, for example from its fitted word gaps, rather than a fixed multiple of T_g.

### Time-base invariance of the decoder (stage 2; known issue)

A decoder whose every time constant scales with the dit has a CER that depends only on the energy per
dit, E/N₀ = S₅₀₀ × 500 Hz × T, so its CER crossings in S₅₀₀ must move 10·log₁₀ k lower when the speed
falls by a factor k (derived; `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, section 5.1.1).
From 25 to 12 WPM that is 3.2 dB of S₅₀₀. Measured in stage 1, the CER-0.10 crossing moves 0.1 dB for the
prototype and 1.3 dB for Matched, and the prototype fails abruptly at 12 WPM, S₅₀₀ = −2 dB. Hypothesis,
not traced: settings fixed in seconds rather than in dits (amplitude average τ_a = 0.5 s, noise average
τ_n = 2 s, periodicity windows 2/5/10 s, re-key wait 0.8 s and time-out 2 s, new-over threshold at least
0.5 s, first-mark threshold calibrated per second).

Test (small): stretch a 25 WPM group A condition in time by 2.08 (into 12 WPM at the same E/N₀ per dit)
and decode it twice, as is and with those settings also multiplied by 2.08. If the second run recovers
the 3.2 dB, the fixed-second settings are the cause; then find which ones matter, and in stage 2 express
each time constant in dits unless there is a stated reason not to (a physical time such as fading or
the 20 s correction reach).

A second, derived reason the seconds-based windows hurt slow stations: the comb on 2T reaches 9.15·T
(4 teeth, ±15% of T), so a periodicity window of length W can measure dits only up to T = W / 18.3:
109 ms (about 11 WPM) in the 2 s window, 273 ms in the 5 s window, 546 ms in the 10 s window. A 5 WPM
station (dit 240 ms) needs a window of at least 4.4 s, so the 2 s window can never give it a T_P (spec
§6, comb lag reach; results section 3.4). This fits the losses at 10 WPM (results section 5.3), but
that link is conjectured.

**Audit of fixed times (do first in stage 2's design; owner, 2026-10-02).** The spec and the stage-1
plan set many time constants in seconds. Before the stretch test, list every time constant in the spec
(§4, §6) and in `ProtoConfig`, and give each a class and its reason:
1. **scales with the dit**: anything that means "enough marks to estimate something" (amplitude and
   noise averages, periodicity windows, re-key wait, new-over threshold, a false-mark rate per second);
   express it in dits or marks;
2. **physical, seconds are right**: fading rate, frequency drift, the 20 s correction reach a reader
   tolerates, display latency;
3. **unclear**: mixes both (for example the 2 s re-key time-out: how long a user waits against how many
   marks are needed); decide it explicitly.
The result is a table in the stage-2 spec; the stretch test then checks it.

### A validated offline measure for the speed estimate T_P (stage 2)

Stage 1's E1 judged the periodicity methods offline, by precision and coverage
inside transmissions of constant-speed stations at S₅₀₀ ≥ 0 dB, and that
measure did not predict decoding. The edge comb won it (coverage 0.749 against
the comb's 0.589 at precision 0.95) yet decoded worse: paired CER +0.0502
(+0.0279 to +0.0742) against the reference
(`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, sections 3.2 and
3.6). Owner's decision 2026-10-01 (option A): stage 1 does not redesign the
measure. For stage 2, build an offline measure that also counts:
- confident T_P outside transmissions;
- points under fading, interference and tuning;
- the time to the first confident estimate.

Validate it against decoding runs before using it to choose, so that speed-
estimator tuning does not need a full decoding run (about 40 min of wall time)
per candidate. Also measure the mechanism the stage-1 record leaves
conjectured: whether the edge comb is confident and wrong outside
transmissions, in fading gaps and under interference.

### Milestone 2b, stage 2: items found in stage 1's final evaluation

From `docs/plans/2026-09-30-milestone-2b-stage-1-results.md`, sections 4–6
(the Python prototype on the 3-seed full suite, against the engine's Matched
and Envelope decoders on the same signals; paired CER = prototype minus the
reference, per signal, with bootstrap 95% intervals over signals). They matter
only if the owner decides to do stage 2.

- **The tracker's input from the bank.** The frequency tracker now weights
  the Matched front end's own filter output by its key-down probability.
  With the bank there is no single filter: stage 2 must decide which branch's
  output and probability feed it (the selected branch, branch 1, or a fixed
  one), and measure it. Stage 1 mixed by the label (oracle) or by the
  detector's frequency, without the tracker's ±12 Hz fine-tuning, so it does
  not answer this.
Every regime listed below is worse than Matched or Envelope beyond its
interval (all three seeds; group E levels in dB relative to the wanted
station's key-down power). The complete per-tag tables are in the results'
section 4.3.1.

- **Strong neighbors (group E).** Worse than Matched in 6 of 16 rows, all
  with the neighbor at +10 or +20 dB, 20–150 Hz away: 150 Hz +20 dB,
  +1.912 (+1.159 to +2.759); 100 Hz +20 dB, +1.332 (+0.698 to +2.093);
  50 Hz +10 dB, +0.517 (+0.365 to +0.761); 150 Hz +10 dB, +0.308 (+0.264
  to +0.364); 50 Hz +20 dB, +0.305 (+0.063 to +0.677); 20 Hz +10 dB,
  +0.209 (+0.056 to +0.288). Worse than Envelope in 5: 150 Hz +20 dB,
  +1.223 (+0.391 to +2.397); 100 Hz +20 dB, +1.141 (+0.733 to +1.701);
  50 Hz +10 dB, +0.558 (+0.320 to +0.731); 50 Hz +20 dB, +0.465 (+0.234
  to +0.675); 20 Hz +10 dB, +0.430 (+0.376 to +0.505). The selected speed
  is off by more than ×1.5 at 0.315 of selection instants. Conjectured,
  not measured: the short branches pass the neighbor, and the periodicity
  estimate follows its keying (section 3.6.1 found its precision 0.57–0.68
  under interference).
- **Crowded channels.** Against Matched: oracle copies, spacing 0 Hz
  +0.192 (+0.088 to +0.318), 50 Hz +0.141 (+0.045 to +0.250), 100 Hz
  +0.106 (+0.022 to +0.235); through the detector path, 50 Hz +0.066
  (+0.002 to +0.143) and 100 Hz +0.073 (+0.005 to +0.162). Better than
  Envelope in 4 of these 5 rows (the oracle copy at 0 Hz is unchanged
  against Envelope, −0.126, −0.252 to +0.008).
- **Slow and slowing keying (group D).** 10 WPM: +0.130 (+0.037 to +0.222)
  against Matched and +0.191 (+0.125 to +0.264) against Envelope; ramp
  30 → 15 WPM +0.010 (+0.003 to +0.021) against both.
- **Regular keying, small losses (group C), against Matched:** machine
  imbalance +0.0, +0.002 (+0.000 to +0.004), and −0.1, +0.004 (+0.001 to
  +0.008); computer +0.0, +0.004 (+0.000 to +0.008), and +0.1, +0.004
  (+0.001 to +0.009); paddle −0.1, +0.014 (+0.003 to +0.024). First-word
  CER on machine and computer keying 0.17–0.42 against Matched's
  0.11–0.19. Conjectured: the lost first character of an over that the
  channel tests show since W_min = 0.8 s (section 3.9; cause not traced).
- **Group H per station.** Against Matched, same-track QSOs: oracle 0 Hz
  +0.179 (+0.080 to +0.291) and 10 Hz +0.066 (+0.012 to +0.128); through
  the detector path, drawn offset +0.049 (+0.023 to +0.079), 0 Hz +0.081
  (+0.011 to +0.169), 10 Hz +0.038 (+0.002 to +0.089).
- **Fast fading, hand keying.** Group B hand 24 WPM at f_D = 3 Hz: +0.060
  (+0.040 to +0.080) against Matched (better in the other 10 group B rows).
- **Group A 12 WPM**: +0.029 (+0.006 to +0.056) CER against Matched, while
  its crossings and first-word CER do not lose (section 5). The loss is at
  the edge of decoding: CER 0.973 at S₅₀₀ = −2 dB against Matched's 0.384
  (all seeds). A higher T_P confidence threshold removes it (held-out CER
  0.045–0.073 at S₅₀₀ = −2 dB, section 5.5) at a cost elsewhere; not traced.
- **Tune-up through the detector path**: 0.6 s, +0.029 (+0.005 to +0.048)
  against Matched and +0.032 (+0.015 to +0.048) against Envelope; 1 s,
  +0.466 (+0.143 to +0.829), and 2 s, +0.904 (+0.791 to +0.981), against
  Envelope (Matched is as bad on those tracks).
- **Against Envelope's own tracks, through the detector path** (compared
  per label; Envelope's detector opens different tracks there, so these
  mix decoding with track choice): group H QSO labels, ambiguous drawn
  offset +0.325 (+0.303 to +0.354), separate-track drawn offset +0.067
  (+0.052 to +0.080), separate-track 100 Hz +0.240 (+0.090 to +0.404),
  separate-track 200 Hz +0.013 (+0.005 to +0.022); group H per station,
  separate-track drawn offset +0.049 (+0.032 to +0.065); pauses 20 s,
  +0.038 (+0.019 to +0.055).
- **Score the provisional text and the corrections.** Stage 1 scored the
  prototype on its final text, after corrections reaching back up to 20 s
  (4.09 per channel-minute; reach median 1.544 s, 99th percentile
  19.861 s), while Matched and Envelope publish text that only grows. What
  a user reads before a correction is not measured (results §5, §5.6).
  Stage 2's bench scoring of corrections (spec §7) must measure it, for
  example the CER of the text as published and how long a wrong character
  stays before it is corrected.
- **Three regressions behind strict expected failures, causes not traced**
  (results §6), if the owner does not have them traced in stage 1: an
  over's first character lost since W_min = 0.8 s (25 WPM, S₅₀₀ = 20 dB:
  "Q TEST K1ABC K1ABC"; conjectured to drive group C's +0.002 to +0.014
  CER against Matched and part of group I's +0.0808); a spurious leading
  "C" in the same-speed turnover test since N_mem = 48; T_g fitted 7.2% low
  (192.2 ms against 207.0 ms) on Farnsworth 18/10 WPM with E5's coarse
  grids.
- **Re-decode an over when its fresh fit wins late?** The prototype does
  not (controller ruling, Task 11): at a 15 → 30 WPM turnover "TEST" read
  "5T" until a branch switch corrected it. Re-decoding would instead
  corrupt the same-speed case with no element spaces ("EE TT …"), where
  the fresh fit takes over at n = 12 (penalty 5.0 nats) with T = 102.4 ms
  against the true 48 ms. Neither choice is measured; stage 2 must choose,
  ideally by measuring both.
- **No renormalization of the fit's class priors** (controller ruling,
  Task 7): a class whose median is not positive is dropped without
  renormalizing the others, a penalty of about −1.04 nats per space on a
  reading with no element spaces; renormalizing broke regression R2
  ("HI" at 12 WPM read as T = 49.8 ms). Carry it into C++ as is, or
  revisit with a measurement (results §6).
- **Placeholders still unsettled**: periodicity windows (2, 5, 10 s), comb
  teeth and width, and the three confidence thresholds (comb 0.03, edge
  comb 0.03, spectrum fit 1.5 nats), measured in stage 1 but not adopted
  (E1–E3 measured the edge comb and were reverted, results §3.5; the comb on
  2T's windows and teeth were never measured); kept by rules no setting
  met: switch persistence M = 4, quality tie 0.05 nats per element, text
  window 10 characters, T_new = max(0.5 s, 12·T_g), R_fa 0.01 /s; and the
  T grid step 1%, kept because E5's adopted variant used it.
  Heuristics never measured in stage 1: the log-normal scatter (0.15 marks,
  0.25 spaces), the outlier class (ε = 0.05, log-uniform 1 ms–10 s), the T_P
  prior's width (0.1 in ln T) and its gate at the confidence threshold, the
  minimum fit weight (8 elements), the noise spectrum's segment (170.7 ms),
  smoothing (±25 Hz), guard reach (20 ms) and clean fraction (0.5), the
  estimate block (21.3 ms), the squelch constant 3, the text tie
  (0.1 nats per character) and separation (1 nat per character).
  Mechanisms added by controller rulings during implementation, never
  varied (results §6): the fresh fit of a new over needs at least
  `fresh_fit_min_obs` = 8 marks and spaces and must beat the previous
  over's fit by ½·k·ln n nats (k = 4 fitted parameters, n = observations;
  BIC-style); and the re-key time-out `rekey_timeout_s` = 2.0 s of channel
  time, which caused 8 284 of the final evaluation's 31 158 corrections
  (26.6%; results §4.4).

### Benchmark scenarios to add first

Done in milestone 2, part 1 (`training/kz4ap_synth/suites.py`).

### Real-recording scoring: manta's oracle

A cheap real-signal benchmark that tests the decoder apart from the detector
(`docs/research/manta-notes.md`, sections 4 and 5, item 2). Take a real I/Q
recording made while a reference skimmer was spotting to the RBN. For each
of that skimmer's spots, open a channel at the spotted frequency and decode a
window of the recording around the spot time (manta: 40 s), without the
detector. Score each window by whether the spotted call appears:
- **as_word:** as a whole decoded word;
- **framed:** as a whole word following `CQ`, `DE` or `TEST`;
- **substring:** anywhere in the decoded text.

manta's run: 15 min of 192 kS/s I/Q from the 2025 CQ WW CW contest, 40 m
near 7.080 MHz, 221 windows around spots by the skimmer K5TR. as_word /
framed / substring: `legacy` 26.7 / 13.1 / 48.0%, `hsmm` 56.1 / 31.7 /
58.8%. By the RBN-reported SNR of each spot (< 15, 15–25, ≥ 25 dB; the
reference is presumably CW Skimmer's dB SNR in 500 Hz, key-down, though
manta does not state it), `hsmm` as_word was 24, 58 and 65%. Because K5TR
copied every one of these calls, the score is a recall relative to CW
Skimmer at a different receiver, not an absolute one.

Work items: record our own contest I/Q with the SDRplay while RBN spots are
logged (manta's recording is not redistributable); add an oracle mode to
`bench/` that takes (time, frequency, call) triples; report the three scores
by SNR bucket, with the SNR convention stated. Before any spot-level scoring
against the RBN, adopt manta's critique of it (no time matching, mismatched
counting units, a union of all RBN skimmers taken as truth). This is a
concrete method for benchmark category H (`decoder-survey.md`) and for spec
§8.3's "labeled approximately from RBN spots" (spec §9, open question 5).
Running KZ4AP's decoder on manta's own oracle is also the comparison the
manta decision needs ("Later: RBN upload and the manta decision" below).

### Integrate morseformer as a reference decoder

Plan and evidence: `docs/research/morseformer-notes.md` (sections 8–9).
morseformer (sderhy, v0.6.4) is a 4.13M-parameter bidirectional Conformer
with an RNN-T head, Apache-2.0 for code and weights. It is the only neural
Morse decoder with ready weights and some real-audio evidence, so it gives
the benchmark a neural baseline before option 2 is trained. **It does not
replace option 2 and is not scheduled ahead of it.** It comes after the
benchmark scenarios above, because its value is its scores on them.

Two stages:
1. **Now: reference decoder in the evaluation harness.** Score it on the
   survey's benchmark categories A–I (`decoder-survey.md`, last table),
   especially H (real recordings).
2. **Later, only if the benchmark shows it beats option 2 on the stations
   where it would run:** an opt-in, channel-capped "second-opinion"
   decoder for low-confidence stations (survey option 4, arbitration),
   for example at most about 10 stations on a desktop and none on a Pi.

Work items:
- **Runtime:** export the encoder plus CTC head once to ONNX (15.9 MB,
  standard operators, already verified against PyTorch) and run it with
  ONNX Runtime, in a Python benchmark harness for stage 1 or in the C++
  engine for stage 2. Ship the converted `.onnx`, never the PyTorch pickle.
  Stagger each channel's window phase to spread the load over the 2 s hop.
- **Feature front end from the complex channel:** per 6 s window of 9000
  complex samples at 1500 samples/s: a zero-phase 4th-order Butterworth
  low-pass at 100 Hz (run forward and backward over the window), magnitude,
  ln(|y| + 10⁻⁶), a 3-sample box mean to 500 frames/s (3000 frames), then
  per-window zero mean and unit variance. This matched the author's 8 kHz
  audio path with correlation 0.994–0.999 and identical decodes on one
  synthetic message; re-check on the benchmark. No 600 Hz tone needs to be
  synthesized.
- **Decoding loop:** RNN-T greedy decoding (the path the author shipped and
  benchmarked): a hand-coded LSTM(128) prediction step and joint network
  (about 0.22M parameters), up to 5 emissions per encoder frame, with the
  0.6 confidence gate (0.9 for digits). Beam search is optional later. CTC
  greedy decoding is simpler and could take the callsign prior later, but
  needs its own scoring. Sliding window: 6 s, re-decoded every 2 s,
  committing only tokens in each window's central 2 s; `flush` decodes the
  last partial window.
- **Symbol mapping to `DecodedSymbol`:** A–Z, 0–9 and `. , ? / - ! '` pass
  through; the explicit space token becomes a word space; `=` → `<BT>`,
  `+` → `<AR>`. **Prosign limitation:** SK, KN and BK arrive as ordinary
  letter pairs, indistinguishable from the same letters sent with a
  character gap; fusing them needs an unvalidated timing rule. `É` and `À`
  map through their Morse patterns. Times: each emission's encoder frame
  (8 ms resolution) marks roughly the start of a character. Probability:
  the joint softmax value of the emitted token, calibrated on the
  benchmark. There is no speed output, so `wpm` must be estimated from
  token spacing or left at 0.
- **Apache-2.0 obligations:** keep the copyright and attribution (cite the
  GitHub handle `sderhy`; the author's given name differs between the
  README and the model card), include the Apache-2.0 license text with the
  weights and any ported code, and mark modified files as changed. There is
  no NOTICE file to carry. Apache-2.0 code and weights may be combined into
  the GPL-3.0 engine.
- **Effort:** 1–2 developer-weeks for the export script, front end, RNN-T
  greedy loop, windowing, symbol mapping and unit tests against the Python
  reference outputs; another 1–2 weeks to score it on categories A–I.

Risks, most serious first:
1. **CPU:** about 2.1 GMAC per channel-second, roughly 100–140× option 2;
   measured 7–20 channels per performance core (ONNX Runtime, fp32, one
   thread, i7-12700H), and an estimated 8–20 channels per Raspberry Pi 5.
2. **Speed range:** trained on 16–28 WPM only; contest speeds above about
   30 WPM are out of distribution.
3. **Latency:** up to 4 s, fixed 6 s window; the model is not robust to
   other window lengths.
4. **Weak real-audio evidence:** 28.6% CER on one held-out operator, an
   unpublished corpus, no SNR curve for the shipped checkpoint, and an SNR
   definition (white noise over 0–4000 Hz, duty-dependent signal estimate)
   that converts to S₅₀₀ (key-on, 500 Hz) only within a band about 10 dB
   wide.
5. **False characters on noise:** about 1 per 6 s of pure noise in the
   author's gate test, multiplied across channels.
6. **Prosign ambiguity** (SK/KN/BK as letter pairs).
7. **Single maintainer, dormant since June 2026;** the shipped file is a
   mid-training checkpoint.

**Optional baseline: pd0wm/nn-morse** (MIT; `docs/research/github-repos-notes.md`).
A Dense×4 → LSTM(256) → CTC network with no convolution, about 740,000
parameters and about 37 million MAC/s per channel. Retrained on the
project generator (down to S₅₀₀ ≤ 0 dB, key-on, 500 Hz, and with fading),
it would be a cheap no-convolution ablation of option 2. Its shipped weights
never saw anything below S₅₀₀ = +1 dB and are useful only as a smoke test.
Low priority.

### Wrong or missing first characters

A station's channel opens only after the detector has seen it persist (about
0.5 s, plus a 1 s warm-up at the start of a recording), so the decoder starts
mid-transmission, often mid-character, and emits a confident wrong symbol
(e.g. `M6Z` for `W6Z`, `RQ` for `CQ`). These produce busted callsigns.

Fix, in two parts:
1. **Replay:** keep the last ~2 s of signal; when a track is born, feed its
   channel from that buffer first so the decoder hears the transmission from
   its start. Decoding runs ~39× real time, so catching up is instant.
2. **Second pass:** once a station's speed and timing estimates have settled,
   re-decode its first characters. For real-time display, show text as it
   arrives and correct it when the better decode lands.

Must land before callsign matching.

**Matched, a channel that opens mid-mark (milestone 2, part 1, Task 16):**
the partial mark is no longer counted for speed (`docs/signal-processing.md`
§8, "Marks whose start was not observed"); measured before/after (Task 17;
§8b, "A channel that opens mid-transmission"): the smoke recording's
Matched CER 0.0622 → 0.0436 (60 → 42 edits in 964 symbols), its
+7617.6 Hz station 0.234 → 0.043; in a focused run of 80 oracle openings
per speed (machine keying, S₅₀₀ 25 and 10 dB), openings worse than
Envelope by at least 5 edits went 6 → 0 and 3 → 0 at 18.5 WPM, 0 → 0
and 2 → 1 at 25 WPM, and 5 → 6 and 12 → 15 at 12 WPM, where a milder
loss is new (the first characters read as T's; §8, cause inferred), and
every opening that had lost the rest of its text now decodes after its
first word (one channel at 25 WPM, S₅₀₀ 10 dB, still decodes nothing;
not diagnosed). This item stays open: replay is still the fix for the
late opening itself and for the lost first word.

**Also covers the retune delay after a QSO turnover** (milestone 2, part 1,
option 1; owner, 2026-09-29). When the answering station is on a different
frequency, within 47 Hz, from the one the channel is tuned to, the channel
moves to it only when the detector's 1 s power average says the peak has
moved: 1.3–1.8 s into the answer at 10–25 Hz, 2.3 s at 40 Hz for a station
10 dB weaker than the caller (simulated; 2.6 s after the caller stops,
derived). A station that pauses and resumes on its own frequency loses
nothing. Median characters of the answering station lost at the start of
its over (simulated, 30 runs each; levels re the caller's key-down power):
0 at 10 Hz; at 25, 40 and 50 Hz, 1, 3 and 3 at −10 dB, 0, 1 and 2 at −6 dB,
0 at 0 and +6 dB. Replay would feed the retuned channel its first seconds
again. **Tried and rejected:** a faster 0.2 s spectrum average during the
decoder's re-acquisition window found the answering station within 0.04 s
and lost nothing at 25–40 Hz, but at 50 Hz its quick swings carried the
channel across the 47 Hz boundary (the caller's next over exact in only
15–20 of 30 runs, and a new track for the caller), and in 2 of 60 runs a
channel jumped 21–25 Hz to a noise peak (the short average is noisier:
about 23% spread per bin, against 10% for the 1 s one).

### Speed estimate derailed by a short tune-up carrier

An unkeyed carrier of about 0.5–0.95 s pins the classical decoder's speed
estimate at 5 WPM and garbles what follows (carriers of 1 s or more are
already excluded). Milestone 1's original estimator fails the same way, so it
is a limitation of the cluster-based design, not a regression. Candidate
fixes: estimate speed from the mark-plus-space period, or wait for a minimum
number of marks before trusting an update. Related to the second pass above.

### Stray E's after a station stops

After a station stops, a few `E`s can be decoded from noise before its track
is dropped (6 of 26 cases in review). The decoder's noise test uses wideband
noise, but the engine delivers noise narrowed by the channel filter, whose
envelope fluctuates more. Tune the squelch against channel-filtered noise and
add an engine-level test: signal, then 10 s of noise, then no text after the
last real character. Tune together with the next item.

Measured on the Matched front end (milestone 2, part 1, 3 seeds, through
the detector): pauses of 2, 5, 10 and 20 s, CER 0.038, 0.039, 0.034 and
0.707 (Envelope 0.050, 0.069, 0.144 and 0.644), first-word CER 0.333 at
2–10 s and 1.000 at 20 s (Envelope 0.167–0.833); strong stations at S₅₀₀ 30, 40, 50 and
60 dB, CER 0.053, 0.055, 0.045 and 0.048 (Envelope 0.034, 0.052, 0.028 and
0.028), first-word CER 0.857–1.000 (Envelope 0.571–0.867). The benchmark
charges stray characters after a station stops as insertions, so they are
inside these numbers but not separated out. `MatchedNoiseAfterStationStopsDecodesNothing`
covers the decoder alone (one seed, S₅₀₀ = 20 dB, 10 s of noise after the
last character): it does not cover the engine, where the track lingers
about 8 s, nor the ~1% stray character after a silence ("Stray noise after
a silence" below).

### Tracks outlive their stations; separate station identity from decoding

A track is dropped about 8 s after its station stops even with a 1 s timeout,
because the detector's one-second power average takes that long to decay.
The longer a track lingers, the more noise it can decode. At the same time,
pauses longer than about 16 s (average decay plus the 10 s default timeout)
end the track, so a station that resumes gets a new track and a decoder that
has forgotten its speed. Both lifetimes are accidents of the current
parameters.

Separate the two: a station's identity (track) should survive long pauses,
a minute or more, and re-attach when it resumes on the same frequency, while
decoding gates off within a second or two of silence.

This also matters in QSOs (simulated, milestone 2 part 1 plan, 2026-09-29):
when the answering station has its own track (more than 47 Hz away) and
sends a 16 s over, the caller is silent for 18 s and its track (S₅₀₀ =
15 dB) dies before it resumes; its next over goes to a new track that loses
its first word (every run at 200 Hz, and at 100 Hz with the answering
station 6–10 dB weaker than the caller's key-down power). Real overs are
often longer.

### Ghost tracks beside very strong signals

Around 60 dB SNR (500 Hz), extra tracks appear a few hundred Hz either side of a
station and decode runs of `E` and `I`. Fix idea: reject a peak that sits
inside a much stronger track's skirt (more than X dB below that track's level within
±N Hz).

### Express the remaining bin- and sample-counted settings in physical units

Owner's standing rule (2026-09-29): parameters and calculations are in
physical units (Hz, s, FS, dB with a named reference), never in units tied
to an implementation choice (FFT bins, samples, decimation); conversion to
bins or samples happens only at the point of use, from the physical value,
so changing the bin width or a sample rate keeps the physics the same.
The milestone-2 part 1 plan restates the settings it touches: the
detector's peak neighborhood (±47 Hz), track-level neighborhood (±23 Hz)
and candidate step (23 Hz), the new channel distance (47 Hz), and the
frequency tracker's lag (5.33 ms) and update interval (21.3 ms). Still
counted in bins or samples, and not touched by that plan:
- `EngineConfig::fft_size` / the analyzer's N: state the bin width (Hz) or
  the window duration (42.7 ms) instead; `choose_fft_size`'s rule is
  already in Hz (bins of at least 20 Hz).
- The hop, N/2 samples: state it as 21.3 ms (50% overlap).
- `EngineConfig::channel_bins` = 64: state the channel output rate
  (1500 samples/s) or bandwidth instead.
- The channel filter's length, N/2 + 1 taps: state it as 21.3 ms.
- `DetectorConfig::min_separation_bins` = 3 (70 Hz): kept only for the
  milestone-1 attribution rule (`Attribution::Bins`), which stays for
  comparison; if that rule stays, state it in Hz.
- The detector's warm-up and persistence are already in seconds, but the
  averaging is updated per frame (α from the hop): check that a hop change
  keeps τ = 1 s.

Do this before sweeping the bin width ("Choose the FFT bin width and
channel filter by measurement", next item), so bin width is the only
variable.

### Choose the FFT bin width and channel filter by measurement

Both are guesses. Bins are ~23 Hz (the FFT size is now chosen from the sample
rate to keep that width), and every channel uses a ±150 Hz filter sized for
fast code. Sweep bin width (e.g. 12, 23, 47 Hz) and channel bandwidth against
the scenarios above and pick values by results. Prerequisite: the remaining
settings expressed in physical units (previous item).

### Channel filtering, two stages

The best decoding bandwidth depends on speed: roughly ±30 Hz is enough at
15 WPM (an estimate), while 50 WPM needs ±100–150 Hz; the fixed ±150 Hz
gives slow stations about 6 dB more noise than they need. Split the
filtering in two:
1. The shared FFT channelizer stays fixed and wide enough for the fastest
   code (±150 Hz, or a user setting), with a fixed decimation sized for that
   widest filter.
2. An optional per-station narrow filter at 1500 samples/s, *before*
   magnitude detection, chosen from the station's estimated speed (possibly
   a filter matched to the dit). Combine with the replay so buffered signal
   is re-decoded through the narrower filter. **Prerequisite:** precise
   frequency re-centering of each channel ("Track frequency drift" below).
   A filter about 1/T wide loses up to 5.1 dB at 25 WPM and 8.8 dB at
   20 WPM (signal power, relative to a centered station) to today's
   ±11.7 Hz bin-rounding offset.

Should help weak, slow signals and crowded bands most.

Stage 2 is built as the Matched front end (a boxcar of 0.8 dit); stage 1 is
unchanged.

### Detector: averaging, noise floor and detection theory

All of the detector's parameters are heuristics (see
`docs/signal-processing.md`, section 6).
- **Averaging:** compare a boxcar mean over T = 1–2 s with the current
  exponential average (τ = 1 s): false tracks, detection delay, time to drop
  a track. Derive T from the false-alarm rate across N bins, an averaging time
  covering several characters at the slowest speed of interest, and latency.
- **Noise floor:** compare a local floor (median or a low percentile over
  ±2–5 kHz) and minimum statistics (Martin's method) against today's global
  median, using real SDRplay recordings (non-flat passband, DC spike, band
  edges). Include manta's two estimates (`docs/research/manta-notes.md`,
  section 5, items 3–4):
  - its per-station **running minimum** (minimum statistics): the minimum
    over 1.5 s of the channel power smoothed over 40 ms, raised by a
    measured **+2.16 dB** (a power ratio, mean noise power over the
    expected minimum) to remove the minimum's low bias. That correction was
    measured for manta's channel (82.2 Hz noise bandwidth, 375 samples/s)
    and must be re-measured for any other bandwidth, smoothing or window;
  - its detector floor: the 25th percentile of each channel's power over
    10 s, capped at the median of a block of 32 channels + 3 dB (power
    ratio), because a CW duty cycle of 50–60% inflates a median.
- **Detection-theory analysis** of the parameters: the threshold from the
  false-alarm and detection probabilities; neighborhoods from the window's
  main lobe plus the keying bandwidth at the fastest speed; persistence set
  jointly with T.

### Track frequency drift

A track's frequency is fixed at birth; drift is tolerated only passively
(about ±35 Hz before the track's level drops noticeably). Re-center tracks on
their peak each frame, move the channel with them, and update the reported
frequency.

This is a prerequisite of the pre-detection matched filter (top-priority
item 1 and "Channel filtering, two stages"), and for that it must be finer
than whole bins. The channel must be shifted by the fractional offset too,
so the station sits within a small fraction of 1/T of 0 Hz (for example
±2 Hz), with drift tracked continuously, before any narrow filter. The
wide ±150 Hz channel filter does not need this; a dit-matched one does
(`docs/research/proakis-ook-notes.md`, section 2.7 and item 15).

Done within ±75 Hz of the channel's center in Matched mode (each detector
track follows its own peak within the channel distance D_ch = 47 Hz; the channel's NCO fine-tunes
within ±12 Hz of it; slow drift only, owner 2026-09-29). Still open: moving
the channel's center bin for larger drifts, drift in Envelope mode, and
drift in oracle mode (no detector; the tracker covers ±12 Hz of the
labeled frequency). Measured (group F, oracle): the re-centered frequency
is within a median of 0.11 Hz of the truth at S₅₀₀ = 5 dB (largest 0.51 Hz,
30 signals); drifts of 1 and 2 Hz/s (19–51 Hz over the signal) lose the
Matched decode in oracle mode (CER 0.486 and 0.777), as expected of the
±12 Hz anchor; no suite group yet measures drift through the detector.

### Speed window

The decoder estimates speed from the last 24 marks (a heuristic; about 4 s at
25 WPM). Evaluate other window sizes and a window measured in time.

### max_tracks

`DetectorConfig::max_tracks` = 200 is a guess at a CPU guard, not exposed to
users. Measure throughput against the number of tracks (on this PC and on a
Raspberry Pi), set the default with margin, and expose it in the app.

### Spectrum frames in linear power

Spectrum frames are stored in dBFS and the detector converts every bin back
to linear power to average it. Store linear power (FS²) and convert to dB
only where it is needed: locally for peak interpolation, and in the display.

### SNR calibration against CW Skimmer / the RBN

The detector reports SNR per bin (35 Hz), the generator in 500 Hz. The
definition is now known (`docs/research/decoder-survey.md`, "One SNR
yardstick"): CW Skimmer reports key-down signal power, measured through a
50 Hz filter with key-up and transition samples discarded and a Rayleigh
fading model, divided by the noise density × a nominal rectangular 500 Hz,
with the density estimated from the flat part of the whole receiver span.
For clean, non-fading signals that is the same quantity as our synthetic
SNR. What remains is calibration: how each side *estimates* signal and noise
from real audio (CW Skimmer's band-wide floor reads high in crowded
segments; our median floor and per-bin averages differ again). Report an SNR
in that convention from the engine, then check the offset against
simultaneous recordings and RBN spots.

### Measure where answering stations really are

The synthetic two-station QSOs (milestone 2, part 1, group H) put the
answering station 0–200 Hz from the caller, with offsets drawn toward small
values: |Δf| in 0–10 Hz with probability 0.40, 10–50 Hz 0.30, 50–100 Hz
0.15 and 100–200 Hz 0.15. That distribution is a guess (heuristic):
zero-beating by ear leaves a few to tens of Hz, while a sidetone pitch that
differs from the rig's CW offset, or RIT, leaves 100–200 Hz. Once the
owner's SDRplay recordings exist, measure the real distribution of |Δf|
between a CQ and the stations that answer it, and replace
`ANSWER_OFFSET_BANDS_HZ` in `training/kz4ap_synth/generate.py` with it. It
matters because the pipeline treats a station within the channel distance
D_ch = 47 Hz of a channel's station as the same channel (the detector's track
moves to its peak and the channel's decoder re-centers on it), and one
farther away as a separate track (milestone 2, part 1, Design decisions B),
with an ambiguous band from 47 Hz to about 94 Hz where the interpolated
frequencies it compares can read closer than D_ch (derived bound;
`docs/signal-processing.md` §11, "QSO regimes"); the milestone-1 detector
(the Envelope path) hears stations within about 47 Hz as one track, from
about 70 Hz as two, and either in between.

Measured on group H (milestone 2, part 1, 3 seeds, through the detector;
`docs/signal-processing.md` §8b): tracks per QSO, Envelope / Matched, were
1.00 / 1.00 same-track (45 QSOs), 1.56 / 1.67 ambiguous (9) and 6.78 / 6.78
separate-track (18; each station's track dies during the other's over and
is re-born). QSO-label CER was 0.118 / 0.197 same-track and 0.085 / 0.297
ambiguous; station-label CER 0.616 / 0.620 separate-track (Task 14; after
Tasks 15 and 16 the Matched figures are 0.212, 0.334 and 0.633, each
interval overlapping Task 14's). So the regime
decides the result: the ambiguous band (47–70 Hz on the Envelope path,
47–94 Hz on the Matched path; the suite's QSOs there are at 50–70.2 Hz) is
where Matched does worst on the QSO label (2 of 6 grid QSOs at 50 Hz above
CER 0.4 in Task 14, 0.533 and 0.415, and still the two worst after Tasks
15 and 16, 0.557 and 0.374: exactly the two that split into two tracks on
the Matched path; §8b), and separate-track QSOs are dominated by track
turnover on both paths. How many real answers fall in each band
therefore matters.

### Tune the Matched front end by measurement

Tuning waits for evidence from the benchmark (owner, 2026-09-27 and
2026-09-29: only with strong evidence; the benchmark exists to find better
values). The Matched front end is the default from milestone 2, part 1
(owner decision 2026-09-29). Its parameters are heuristic
(`docs/signal-processing.md` §7, §8, §8b): the filter length β = 0.8 dit
(sweep 0.6, 0.8, 1.0), the amplitude and noise time constants (0.5 s, 2 s;
the fading group is the test), the squelch a_min = 3·(T_v/16 ms)^(1/4)
(T_v the filter's duration), the keying hysteresis ±1 nat, the noise guards
κ = 1.75 and κ_n = 4, the noise floor's margin (2.5), clean fraction (0.25)
and restart ratio (4), the re-acquisition silence (max(0.5 s, 12 dits)) and
window (2 s), the tracker's fine-tuning range (±12 Hz around the
detector's frequency) and minimum weight (0.6), when the filter starts
following the speed (8 marks), the dit-estimate growth bound (decided
×1.25 per mark, also from the filter's first follow step; applied once
per physical mark since Task 15, see "Growth bound per mark" below), and
the channel distance (D_ch = 47 Hz). The amplitude estimate is biased
low by the filter's ramps (0.85–0.88 of the true amplitude for PARIS at
25 WPM, simulated), which lengthens marks by about 7 ms at 25 WPM; measure
whether that matters.

The four limits below were postponed by the owner on 2026-09-29. Each is a
known limitation of the Matched front end as planned.

**Measured bearing (milestone 2, part 1, full suite, 3 seeds;
`docs/signal-processing.md` §8b, "Measured: Matched against Envelope";
re-measured in Task 17 after Tasks 15 and 16, Task 14's figure after
"was").** Matched is better than Envelope in most conditions and at low
S₅₀₀, and **worse** in these (paired CER difference, interval excluding
0): slow fading (f_D 0.1 Hz) at high S₅₀₀ with paddle or hand keying
(+0.07 to +0.22 per point at 13.78–57.78 dB, 7 points, plus 2 low
points at f_D 1 and 3 Hz; was f_D 0.1–0.3 Hz, +0.09 to +0.34, 18
points); fists with bug or hand keying or a positive
imbalance at 5–20 dB (bug +0.1: +0.349 over the condition, was +0.317;
bug +0.0: +0.239 and hand +0.1: +0.157, newly worse); a 20 Hz
interferer 10 dB above the wanted station's key-down power (+0.221, was
+0.349); group H through the detector (same-track QSO label +0.104,
was +0.078; ambiguous +0.442, was +0.352; separate-track per station
+0.034 to +0.055, was +0.029 to +0.057; same-track at 10 Hz +0.043,
newly worse); group D at 10 WPM (+0.061, newly worse); strong stations
(+0.018 to +0.020); tune-up carriers of 1 s (+0.562) and 2 s (+0.899),
unchanged. Group D's speed step from 20 to 35 WPM is not worse than
Envelope but got worse with Task 15 (Matched CER 0.113 → 0.394, the
text stopping after the step in 5 of 6 signals; not diagnosed).
Three mechanisms were diagnosed in Task 14; one is fixed, one partly:
- **The growth bound acted per speed update, not per mark** (a defect
  against the owner's decision; **fixed by Task 15**, "Growth bound per
  mark" below). Widening the filter at a key-up re-opened the mark just
  ended; the decoder merged the "dropout", re-measured a longer mark and
  widened again, so the filter reached its target within one mark (20 →
  95 ms in 35 ms, measured). With a wrong estimate it jumped to the wrong
  width at once. Measured after the fix: the two start-up runaways of
  §8b read Matched CER 0.000 and 0.015 (were 0.464 and 0.981), and group
  A's CER-0.10 crossing at 25 WPM is 1.1 dB (0.4 to 1.4 dB; was 2.7 dB,
  −0.0 to 10.4 dB).
- **A misleading first mark** (the front end's 0.32 s warm-up ending inside
  a mark, a first mark keyed late while ŝ rises, or merged elements at low
  S₅₀₀) derailed the speed estimate: a 15 ms fragment pinned the dit at
  20 ms (the smoke recording's +7617.6 Hz station, CER 0.234), and a mark
  between dit and dah length made the estimator take the mean of dits and
  dahs as the dit (runaways to dits of 94–210 ms, 12.8–5.7 WPM,
  measured). In a focused run of 80 late openings per speed, Matched lost
  at least 5 more characters than Envelope in 4–15% of openings at
  12–18.5 WPM and in 0–3% at 25–40 WPM. **Task 16** keeps a mark whose
  start was not observed (the first two causes) out of the speed
  estimate; merged elements at low S₅₀₀ are not addressed. Measured
  after it: the +7617.6 Hz station reads CER 0.043 and the smoke
  recording 0.0436 (was 0.0622); at 18.5 WPM no late opening is worse
  than Envelope any more (was 6 and 3 of 80 at S₅₀₀ 25 and 10 dB), but
  at 12 WPM 6 and 15 of 80 are (was 5 and 12), now by a milder loss: the
  first characters of a station slower than 12.5 WPM read as a string of
  T's (group A, 12 WPM, first-word CER 0.591 → 0.985 at S₅₀₀ 6–20 dB;
  cause inferred in §8, "Marks whose start was not observed", not
  instrumented).
- **Tune-up carriers of 1 s or more** leave the Matched channel silent when
  the channel opens during the carrier (measured by cutting the recording;
  a 2 s carrier also when the channel is open before it; unchanged by
  Tasks 15 and 16, re-measured in Task 17). Likely cause, not
  instrumented: the warm-up and the noise floor (a 1.02 s window at the
  acquisition width) take the carrier as noise, and the station's own
  marks then hold σ̂_v up.

#### Acquisition floor (postponed)

A station is first heard through a short "acquisition" filter (16 ms, set
for 60 WPM), because the decoder does not know its speed yet. That filter
lets in more noise than the dit-matched filter used later, so a weak
station cannot be caught at all below about S₅₀₀ = −2.5 dB (derived, any
speed); in simulation half its marks were caught near −1.8 dB at 25 WPM and
−2.6 dB at 12 WPM. Once caught and narrowed, it could be followed down to
−4.4 dB (25 WPM) or −6.0 dB (12 WPM), but it has to be caught first.
**Measured** (group A, filler text, machine keying, oracle, 3 seeds;
after Tasks 15 and 16, Task 17): the Matched CER at S₅₀₀ = −4, −2, 0, 2
and 4 dB was 0.881, 0.384, 0.077, 0.030 and 0.036 at 12 WPM, 1.000,
0.670, 0.185, 0.024 and 0.003 at 25 WPM, and 0.992, 0.892, 0.644, 0.158
and 0.027 at 40 WPM; CER 0.10 is crossed at −0.2, 1.1 and 2.9 dB
(intervals −0.5 to 0.7, 0.4 to 1.4 and 2.2 to 3.3 dB), still above the
expected −2.6 to 0 dB at 25 and 40 WPM. Task 14 (before the fixes):
0.811, 0.324, 0.106, 0.055, 0.021; 1.000, 0.711, 0.122, 0.151, 0.006;
0.986, 0.857, 0.612, 0.199, 0.021; crossings 0.2, 2.7 and 3.1 dB, the
start-up runaways adding to them at 25 and 40 WPM.
Options: (1) acquire with a longer filter (costs fast CW and the range of
frequencies it can pull in); (2) key at the acquisition width without the
squelch, but require several consistent marks before trusting them;
(3) squelch on the level the dit-matched filter would have (raises false
keying on noise). The detector has a similar floor of its own, which
matters in the full pipeline (not in oracle mode): its 6 dB threshold per
bin, in the Hann window's 35.2 Hz noise bandwidth, for a keyed station
down 44% of the time, is reached at about S₅₀₀ = 6 − 11.5 + 3.6 ≈ −1.9 dB
(a rough estimate: 10·log₁₀(500/35.2) = 11.5 dB, 10·log₁₀(1/0.44) = 3.6 dB;
not measured, and up to 1.4 dB worse for a station between bins).

#### Noise-rise recovery (postponed; affects accuracy)

If the band noise rises and stays up (a sustained rise of 6 dB, in power),
the front end's noise estimate climbs back slowly: about 43 s (derived;
24–56 s simulated). Until it has caught up, noise can look like signal, and
stray characters can be decoded for up to about 16 s. Brief noise bursts
are not affected (the estimate does not chase them), and a fall in the
noise is followed within a few seconds. A faster climb would need to tell a
real noise rise from a station that fills most of the channel, and every
method tried either did not separate the two or let weak stations inflate
the estimate. Not measured by the suite (no group raises the noise). The
related stuck-high case, a noise estimate lifted by a tune-up carrier, is
measured above (tune-up 1 s and 2 s).

#### Stray noise after a silence (postponed)

After each silence of max(0.5 s, 12 dits) the front end restarts its
amplitude estimate, in case the next over is from another station. In
about 1% of silences in noise alone (4 of 400 simulated), the restarted
estimate latched onto noise and one stray character was decoded. One
option: give the restart a prior weight, as the warm-up has.
Measured bearing: pauses of 2–10 s decoded at CER 0.034–0.039 with Matched
(first-word CER 0.333), better than Envelope; strong stations at S₅₀₀
30–60 dB 0.045–0.055, slightly worse than Envelope (paired +0.004 to
+0.020); stray characters are not separated out in these numbers.

#### A strong neighbor 60–70 Hz away leaks through the matched filter (stated limit)

The matched filter is a boxcar (a plain moving average). Its frequency
response has sidelobes: a station 60 Hz from the channel's station comes
through the 25 WPM filter only 18.7 dB down (relative to a centered
station), and one 70 Hz away through the short acquisition filter 19.6 dB
down (derived). If that neighbor is as strong as the channel's station or
stronger, its marks are keyed in fragments in the wrong channel, and those
fragments corrupt the speed estimate. Simulated (milestone 2, part 1,
option 1; 30 runs each; levels re the channel's station's key-down power):
with the neighbor answering 60 Hz away, the first station's next over came
through exactly in 6 of 30 at equal level and 0 of 30 at +6 dB (its last
three words were intact in 26 and 30; typically "FARIS" for "PARIS"); at
70 Hz and +6 dB, 19 of 30, with the filter running away to a slow speed in
6. The neighbor itself was decoded in every run, on its own channel in all but one (at 70 Hz and +6 dB, once by the first station's channel). The fix
belongs in the filter's design, for example a tapered filter with lower
sidelobes; take it up with the co-channel item below (owner, 2026-09-29).
Measured bearing (group E, oracle, 3 stations per row; Task 17, after
Tasks 15 and 16, Task 14's figure after "was"): a neighbor 50 Hz away at
+0 dB re the wanted station's key-down power, Matched CER 0.090 (was
0.064) against Envelope's 0.857; at +10 and +20 dB, 0.817 and 1.031 (was
0.814 and 1.123; Envelope 0.787 and 0.855). Group H through the detector
at 50 Hz (ambiguous on both paths): per QSO label Matched read 0.557,
0.234, 0.046, 0.374, 0.025 and 0.043 (was 0.533, 0.154, 0.046, 0.415,
0.032, 0.040; Envelope 0.023–0.140); the two worst, the only ones above
0.3, are the QSOs that split into two tracks on the Matched path, so the
QSO label, which does not fit a split QSO, charges one station's overs
as missing (the answering station's in Task 14, on its own track and not
passed by the caller's re-centered channel; in one of the two now the
caller's, not diagnosed); in the 4 that stayed one track Matched read
0.025–0.234 (`docs/signal-processing.md` §8b). The answering station's
first-word CER was 0.379 (was 0.444; Envelope 0.032). This is not the
60–70 Hz sidelobe leak of this item. The retune delay (see "Wrong or
missing first characters"): the answering station's first-word CER with
Matched was 0.287, 0.110 and 0.190 at 0, 10 and 25 Hz (was 0.235, 0.147
and 0.181; Envelope 0.199, 0.213, 0.422; an upper bound, no interval).
Whether the first-step growth bound removed the 50 Hz runaway (12 of 30
failures in the simulation before it): with the bound now applied per
mark (Task 15) the two split QSOs still read worst, so a runaway is not
what fails them in these runs (inferred); the per-station view, which
would show the answering station on its own track, reads poorly at 50 Hz
on both paths (0.947 Envelope, 0.977 Matched, was 0.953).

#### Two stations keying at the same time within a few tens of Hz (postponed)

A second station keying at the same time as the channel's station, a few
tens of Hz away, cannot be separated by the Matched front end: both pass
its filter, and the frequency tracker settles on the stronger or between
the two. Simulated (engine level, 30 runs each; levels in dB re the
channel's station's key-down power): 6 dB weaker at 40–60 Hz, the
channel's station decoded in 30 of 30 and the other in 0 (no track of its
own); at equal level, the channel's station decoded in 0 (40 Hz: the
tracker settled between them), 13 (50 Hz) and 28 (60 Hz) of 30; 6 dB
stronger, the channel moved to the other station in 30 of 30. Candidate
mitigations to explore (measured bearing: group E, a 20 Hz interferer
at +10 dB re the wanted station's key-down power gave Matched CER 0.765
against Envelope's 0.544, paired +0.221, +0.088 to +0.352, after Tasks
15 and 16 (Task 14: 0.885, +0.349); at +0 dB 0.786 (Task 14: 0.872) and
0.793; 3 stations each): hold the tracker when its average stops being
coherent (a sign of two tones); choose the filter length so the other
station sits on one of its nulls (at multiples of 1/T_v); estimate each
mark's own frequency and assign marks to stations; run a second tracker
and decoder in the channel for the second tone; a finer detector
spectrum (a longer FFT) to see both peaks; or a probabilistic decoder that
models two stations (top-priority item).

### Growth bound per mark (fixed in milestone 2, part 1, Task 15)

The owner's decision of 2026-09-29 bounds the Matched decoder's dit
estimate, and the filter's own dit from its first follow step, to ×1.25
growth **per mark**. The code applies the bound in `update_speed()` at
every key-up counted for speed. When the filter widens at a key-up, the
wider boxcar re-opens the mark that just ended; the dropout merge in
`key_down()` pops that mark from the speed window but does not restore
`dit_s_` or `filter_dit_s_`, so the next key-up re-measures the longer mark
and applies ×1.25 again. Measured (Task 14, debug prints): 7 updates on
one mark, the filter's dit 20 ms × 1.25⁷ = 95.4 ms within 35 ms. The merge
also does not decrement `marks_since_reacquire_`, so one physical mark can
count twice toward the 8 marks before the filter follows. Fix: save
`dit_s_`, `filter_dit_s_` and the mark counters at each key-up and restore
them when the next key-down merges into that mark, so a re-measured mark
gets one bounded update; then re-run the suite (the start-up runaways and
group H's 50 Hz rows in `docs/signal-processing.md` §8b are the cases to
check). This is a code fix to meet a decision already made, not a
parameter change, but the owner decides when.

**Fixed (Task 15, commit 59cd450):** each key-up counted for speed saves
the dit estimate, the filter's dit and length, the count of marks since
the last re-acquisition and the speed window, and a dropout merge that
re-opens that mark restores them, so each physical mark is bounded once
(`docs/signal-processing.md` §8, "Once per physical mark"). **Measured
before → after** (Task 17; full suite, 3 seeds, the same recordings;
the after figures include Task 16; Task 15 alone was also scored, and
its figures are given where they differ): group A, Matched S₅₀₀ at CER 0.10
(dB): 0.2 (−0.5 to 2.2) → −0.2 (−0.5 to 0.7) at 12 WPM, 2.7 (−0.0 to
10.4) → 1.1 (0.4 to 1.4) at 25 WPM, 3.1 (2.0 to 3.6) → 2.9 (2.2 to 3.3)
at 40 WPM; the two start-up runaways of §8b, Matched CER 0.464 → 0.000
(`A-awgn-25wpm-1-s1`, +6606.5 Hz, S₅₀₀ 10 dB) and 0.981 → 0.015
(`A-awgn-25wpm-0-s2`, −2991.9 Hz, S₅₀₀ 2 dB; 0.074 with Task 15 alone);
a regression from Task 15 alone, group D's speed step from 20 to 35 WPM,
0.113 → 0.394, the text stopping after the step in 5 of 6 signals (not
diagnosed; §8b, "Where Matched is worse"); group H through the
detector at 50 Hz (grid, 6 QSOs), QSO-label CER 0.533, 0.154, 0.046,
0.415, 0.032, 0.040 → 0.557, 0.234, 0.046, 0.374, 0.025, 0.043: the two
QSOs that split into two tracks (the same two; tracks per QSO 1.33 before
and after) still read worst, consistent with the split, not a runaway,
failing them (inferred; §8b, "Groups G and H").

### A mark whose key-up the squelch forces (possible limit; derived from the code, not observed)

The end-of-mark counterpart of Task 16's rule (`docs/signal-processing.md`
§8, "Marks whose start was not observed"). In the Matched decoder a mark
ends either by the log-odds (g < −1 nat) or because keying becomes
impossible: the squelch closes (a < a_min), for example when the noise
floor's stuck-low restart sets ŝ back to 0 in the middle of a mark, or ŝ
decays or σ̂_v rises at a weak station. A key-up forced that way still
counts the mark for speed, with a duration truncated where the squelch
closed, which can mislead the speed estimate the way a fragment at the
start did. Not observed in the suite; how often the squelch closes
inside a mark is not measured. A fix in the spirit of Task 16: count a
mark for speed only if its key-up came from the log-odds (no new
parameter). Owner's decision.

### Tracks converging on one peak (possible limit; derived from the code, not observed)

With `Attribution::Distance` (the Matched path), `SignalDetector::follow_peaks`
(engine/src/signal_detector.cpp) moves each track to the strongest
qualifying peak within D_ch = 47 Hz of its own frequency without checking
whether another track already holds that peak, and channels are never
merged. Two tracks born just over D_ch apart could both end on one
station's peak when the other station falls silent, and then two decoders
print the same text until one track dies. The bench cannot show it yet:
a converged track is still matched to its own label by its birth
frequency (so it is not a false track), and the results record only birth
frequencies. Group H's drawn QSO at 53.9 Hz (seed 1) may be a case: the
caller's track ended on the answering station's carrier, and that station
also had a track born at its own carrier (whether both were alive at once
is not recorded; `docs/signal-processing.md` §11, "QSO regimes"). To do: record each track's
frequency history (or last frequency) in the bench's results, count
tracks that share a peak, check the crowded group at 50 Hz spacing, and,
if it happens, decide the rule (for example, a track may not move onto a
peak another track holds). Owner's decision; not a parameter change.

### VE3NEA's pitch error in group B

Add VE3NEA's ±30 Hz pitch-error option to the benchmark's group B only if
we compare with his published curve directly or test his published model
(owner, 2026-09-29).

## 2. GUI and live display

The Qt app with live input: SDRplay source, ring buffer, worker threads and
status events, a waterfall over one band, and decoded text for the selected
signal (design spec §3, feature A; §4.1). This is the owner's first goal: a
live, single-band, waterfall-style operator view like CW Skimmer's. It gets
its own plan. Related item above: "Spectrum frames in linear power".

- **A channel that follows a QSO turnover** (owner, 2026-09-29): with the
  channel distance (milestone 2, part 1), one channel follows both stations
  of a QSO within 47 Hz of each other. When it decodes "DE <call>", its
  displayed callsign should change to that call, and change back when the
  other station's call is decoded. The same rule belongs in callsign
  matching (section 4).

## 3. Receiver-audio input (directly after the live display)

Design spec §3.3. Decode whatever the operator is listening to on his own
receiver, from its audio output through a sound card. It comes directly after
the live display, before callsign matching (owner's decision, 2026-09-27),
because it reuses the live-input plumbing (a sound-card source feeding the
same ring buffer).

- **Analytic signal.** The input is real, sampled at f_a (typically
  48 kHz), with content only in the receiver's audio passband (typically
  about 300–3000 Hz). Form the complex signal by a Hilbert transform or by
  mixing the passband center to 0 Hz and low-pass filtering, then run the
  existing engine on it. At a complex rate of 48 kHz,
  `choose_fft_size` gives N = 2048 and 23.4 Hz bins, and the channel rate
  stays r = 1500 samples/s (`docs/signal-processing.md` §2, §7).
- **Noise floor.** Most of the span lies outside the receiver's filter and
  holds almost no noise, so today's median of all bins would sit far below
  the in-band noise. Restrict the floor to the passband, or mix and
  decimate the span down to it (see "Detector: averaging, noise floor and
  detection theory" above).
- **Receiver effects to test:** AGC (gain changing between and during
  elements, which moves the decoder's mark and space levels), the
  receiver's filter shape (colored noise; stations near the filter edges
  attenuated), and the BFO/tone offset (audio frequency = RF offset from
  the dial frequency plus the tone offset, sign set by the sideband; RF
  frequency needs the dial frequency from radio control).
- **SNR and units.** Label SNRs from this input as receiver-audio values:
  they are not comparable with SNRs from SDR I/Q even in the same bandwidth
  (e.g. dB SNR in 500 Hz). dBFS here is relative to the sound card's full
  scale.
- **Test:** record receiver audio and SDR I/Q of the same stations at the
  same time and compare decodes and SNRs.

## 4. Callsign matching

Needs its design session first (spec §6). Items from milestone 1's review
that must land before it are in section 1 ("Wrong or missing first
characters"). A channel that follows a QSO turnover carries two stations'
calls in turn; relabel it when it decodes "DE <call>" (section 2, owner
2026-09-29).

### Input to the design session: manta's callsign acceptance rules

manta's `manta-spot` crate (`docs/research/manta-notes.md`, section 3, last
rows, and section 5, item 6) is a worked, tested set of rules to compare
against spec §6's candidates:
- parse the context: `CQ`, `DE`, `TEST` and beacon patterns;
- a callsign-grammar prefilter;
- `cty.dat` allocation check: a call whose prefix is not allocated is
  **rejected**;
- `MASTER.SCP` only **raises confidence**, never gates (one answer to
  spec §6's "soft prior, hard filter, or not at all");
- the call must **repeat as a distinct message within 90 s** before its
  first spot (beacons and an operator allowlist are exempt);
- **variant arbitration:** a weaker rival call confusable with a stronger
  one is withheld;
- **dedupe** on (call, frequency bucket); re-spot after 10 min unless the
  SNR improves or the spot type changes.
Cautionary example: the beacon exemption produced 29 garbage spots in one
overnight run. manta bundles `cty.dat`, `MASTER.SCP` and a DXCC table; check
their own terms (its `SOURCES.md`) before reusing them.

## 5. Telnet spot server

Spots in DX-cluster format for local logging and contest programs (spec §3,
feature D). Not an RBN feed. Needs spots, so it follows section 3. manta's
DX-cluster telnet server (port 7300, RBN `DX de` line format) is a reference
for the line format and client handling.

## Later: RBN upload and the manta decision

- **RBN upload** (spec §3, feature F): maybe, later. Not a current goal.
- **Decide: KZ4AP's own RBN server, or contribute to manta** (spec §3.2,
  §9 open question 7). Evaluate manta first and consider integrating parts
  of it into the decoder and any later RBN server. The owner's idea: if
  KZ4AP's decoder outperforms manta's on manta's own tests (its oracle and
  golden vectors), a merge or a fork could produce the RBN tool. License
  facts: manta is MIT OR Apache-2.0, so KZ4AP (GPL-3.0) may copy or port
  from it, keeping its notices; contributing KZ4AP code upstream would
  require the owner to license that code MIT or Apache-2.0; a GPL-3.0 fork
  of manta is allowed. Obstacles noted in manta-notes.md section 6: Rust
  versus C++20, and a different channel format (375 samples/s magnitude
  from 93.75 Hz-spaced channels versus 1500 samples/s complex from a
  ±150 Hz channel).

## Smaller items worth keeping

- **Engine events:** the "station gone" event carries the track as it was when
  found, so its SNR and last-active time are stale; carry the final values.
  Tracks still alive at the end of a recording get no "gone" event.
- **Timestamps:** document the time bases — detection times lag the true start
  by up to ~43 ms, and decoded symbol times include ~10.7 ms of filter delay.
- **Benchmark matching** of labeled signals to tracks is greedy in file order;
  fine at 1 kHz spacing, but needs a proper assignment once crowded scenarios
  exist.
- **Neutral attribution of ambiguous edits in first-word scoring:** the
  bench's alignment traceback charges an ambiguous edit to the earliest
  reference symbol it could belong to (`docs/signal-processing.md` §11),
  which biases first-word and per-transmission CER upward when text repeats
  or a whole word is dropped. Among the alignment's optimal paths, split an
  ambiguous edit's charge (for example half to each candidate symbol), or
  also report the forward-traceback figure as a bracket around the current
  one. Until then, treat first-word CER as an upper bound, not an exact
  attribution.
- **Determinism check** in CI runs the same block size twice; add a bench
  option to vary the block size and compare.
- **Test gaps** noted in review: detector hysteresis band and equal-power peak
  ties; channelizer band-edge and adjacent-channel tests use exact-bin tones;
  a direct test that a track's final text precedes its "gone" event (needs a
  way to inject a test decoder).
- **Input checks:** reject infinite values in decoder settings; generator
  arguments (zero or negative counts and durations).
