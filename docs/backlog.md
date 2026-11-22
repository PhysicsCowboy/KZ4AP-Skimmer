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

### A test group that opens late on the detector's path (fourth review, 2026-10-31)

Every station of the test set starts 0.5 to 2 s into its stream, so no group opens a channel late, and the stream's-clock bug in the choice of filter went unseen. Add E2 and A2 through the detector with 15 s before the stations. Measure: E2 late on the detector's path, 0.657 at checkpoint 2 (`docs/plans/data/2026-10-31-checkpoint-2/e2-detector.csv`). Also there: 5 of E2's 30 stations at 60 to 120 Hz get no track (conjectured: a neighbor's main lobe within the peak radius; a radius of 23.5 Hz did not help). **Traced and fixed in `det-1` (2026-11-04):** the neighbor's main lobe, and the DC guard for the station the set places at the center; on the finer detector every E2 station of seeds 1 and 2 gets a channel (decision log). The late-opening group itself is still to add.

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

### Are the bandwidths wide enough above 50 WPM? (owner, 2026-10-06)

Test, at representative speeds above 50 WPM (e.g. 50, 60, 80, 100 WPM;
machine and paddle keying; S₅₀₀ across the decision-relevant range), whether
every bandwidth in the chain is wide enough:

- the FFT bins (23.44 Hz) and the detector: a keyed signal's sidebands
  spread over about ±1/T (±50 Hz at 60 WPM, ±83 Hz at 100 WPM), so the
  track's level (and its birth at 6 dB above the floor, dB SNR per bin)
  depends on speed; measure the track level against the key-down SNR per
  bin by speed;
- the channel filter (±150 Hz, sized heuristically for about 60 WPM): how
  much keying-sideband power and edge sharpness it removes at higher speeds,
  and the effect on mark timing;
- the decoders' fast limits: Envelope's and Matched's speed clamp of
  5–60 WPM and Matched's 16 ms acquisition width (60 WPM); the bank's
  shortest branch, 9.6 ms (100 WPM);
- the channel rate, 1500 samples/s (0.67 ms per sample against a 12 ms dit
  at 100 WPM).

The current development set has almost nothing above 40 WPM (D's 60 WPM
step, a few B signals), so this needs its own test group. Related: the
development set's distribution (above).

### Does the detector miss stations the chosen decoder could copy? (owner, 2026-10-06)

Once the decoder is decided, check whether the live detector misses tracks
at low SNR that the decoder could still copy. The detector's birth
threshold (6 dB above the floor, on a 1 s average) compares signal plus
noise with the floor, so the station's averaged power must be 4.7 dB above
the noise per bin: about S₅₀₀ = −3.0 dB at 12 WPM, −2.7 dB at 25 WPM,
−2.3 dB at 40 WPM and −1.5 dB at 80 WPM (dB SNR in 500 Hz; derived, white
noise, PARIS text with 5 ms edges, tone on a bin center, without the
noise's fluctuations; 1.0–1.3 dB more between bins). Corrected 2026-10-16:
these figures had read the 6 dB as signal over noise (−1.0 dB at 25 WPM,
−1.9 dB for slow keying, +1.6 dB for very fast keying). A simulation with
the fluctuations, and the false-track rate on white noise, are in the
design notes' "Opening and closing channels"
(`docs/design/2026-10-09-new-decoder-design-notes.md`). The bank's and
Matched's CER-0.10 crossings lie at about 0 to +3 dB (measured, group A,
oracle), so the threshold is not the limit today; an ideal decoder would
reach CER 0.10 at about −1.9 dB at 25 WPM and −5.0 dB at 12 WPM (derived
bound, stage-1 results record §5.1.1; corrected 2026-10-16 from −5.1 dB
and §5.1), below the threshold at slow speeds.

**Taken up by the new decoder's design (owner, 2026-10-16):** the
threshold is to be set from a false-channel rate against a local floor,
with the decoder confirming (design notes, "Opening and closing
channels"). The measurement below is that topic's test.

Measure: detection recall against S₅₀₀ at 12, 25, 40 and 80 WPM (owner;
group A has only 12, 25 and 40 WPM, so 80 WPM needs new recordings),
through the detector path, not only oracle, next to the chosen decoder's oracle CER
against S₅₀₀; the gap between the two curves is what the detector costs.
If it matters, candidates (heuristic until measured): a longer average, or
a lower birth threshold with longer persistence (the threshold sits about
10 standard deviations of the averaged noise above the floor, derived, so
there is room), each traded against latency and the false-track rate (also
to be measured on pure noise, which has not been done; simulated on white
noise 2026-10-16, design notes, "Opening and closing channels").

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
     baseline in `docs/plans/2026-09-27-milestone-2a-results.md` §3 and §8.3. Tuning its parameters
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
**Status (2026-10-07): obsolete** — specific to the filter-bank decoder, frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


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
**Status (2026-10-07): applies to the Bell decoder** (design spec §5.2, amendments of 2026-10-07); written for the filter-bank decoder, the measure carries over.


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
**Status (2026-10-07): obsolete** — specific to the filter-bank decoder's branch selection, frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


Spec §4.6 asks the decoder to follow a jump in speed within about 10 marks. Stage 1's E6 measured the
marks needed to follow a 15 → 30 WPM step (10 seeds) at switch persistence M = 1, 2, 4, 6 and 8: medians
10.5, 11.5, 14, 16 and 18 marks; every seed was followed (`docs/plans/2026-09-30-milestone-2b-stage-1-results.md`,
section 3.11). No M meets the target, and M = 4 was kept. So the limit is not the switch setting alone.
Conjectured, not traced: the fit memory of 48 elements (E4, adopted before E6) makes each branch's fit
follow a step about twice as slowly as 24 did (the fit's step test needs about 144 elements against 72,
section 3.7). Stage 2: find what limits following (fit memory, eligibility, the T_P prior) before
tuning M.

### Farnsworth word gaps start false overs (stage 2)
**Status (2026-10-07): obsolete** — specific to the filter-bank decoder's new-over rule, frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


A silence longer than T_new = max(0.5 s, 12·T_g) starts a new over (fresh fit and amplitude, first marks
re-keyed). Farnsworth's long word gaps pass that test inside a transmission: in the reference before
E7, 16 of 48 Farnsworth transmissions had a false over start (0.333 per transmission) against the
rule's limit of 0.05; the variants tried did not fix it (the nearest, 16·T_g, gave 0.0625), so the
threshold was kept (measured, E7; results section 3.10). A false over start discards the station's
fit and amplitude mid-transmission. Stage 2: make the over-start test aware of the station's own
spacing, for example from its fitted word gaps, rather than a fixed multiple of T_g.

### Farnsworth spacing in the new decoder (owner, 2026-10-21: removed from the first prototype)
**Status (2026-10-31): done in the segmental decoder, a story's fist being a speed and a gap stretch (decision log); open in viterbi.** The new decoder's first prototype (viterbi) assumes standard spacing only.
- The Farnsworth factor is removed from its code: the factor in each reading's state, the 23-factor grid, and the
  open-factor tables and scan.
- The removed code is in the branch's history, in the commits before the prototype's Task 4d.
- Farnsworth-spaced signals (DEV2's groups I and I2) are expected to decode poorly until a fix. Owner's decision: "we eliminate the Farnsworth factor entirely for now and allow any Farnsworth decoding to fail
until a future fix ... That will help with CPU and will restore proper decoding of properly sent Morse."

**What was found** (the first prototype; measured unless marked):
- **How it worked.** Each reading carried a factor F. There were 23 values, 10% apart from 1 to 8.14, all equally
  likely (the flat prior, owner 2026-10-19). A reading chose its F at its first gap between letters or words, paying
  ln 23 = 3.1 nats.
- **The cost to standard code.** Every reading of well-sent standard code pays those 3.1 nats at its first letter gap,
  as if F = 8 were as likely as F = 1. A reading that never takes a letter gap avoids the payment.
- **The misreading.** Add the uniform letter cost (ln 62 = 4.13 nats) and the timing model's spread (0.3 in ln length;
  a 3-dot gap read as a 1-dot gap costs about 6.7 nats, derived), and merging letters into one longer symbol can win.
  - "TEST" sent alone decoded as "-" (-....-) in 12 of 12 runs at +10 to +30 dB S500. The one-symbol reading misfits
    by 14.6 nats at a speed 1.33 times slower, but saves 16.7 nats of priors.
  - With a single factor, "TEST" decoded correctly, by 1.1 nats.
  - "TU" sent alone would read as "X" by about 1 nat even at the right speed (derived).
  - Evidence: `docs/plans/data/2026-10-20-new-decoder-prototype-1/one-word-over.csv` and the prototype's results
    record.
- **The CPU.** The factor's branching, 23 factors × 2 gap kinds at every possible gap end, was the search's largest
  cost: 82% of its time on noise before the exact speed-ups.
- **Thin margins even at F = 1.** At a later letter gap the correct reading wins by only about 2 nats at exact timing
  (derived), so real fists may still merge letters.

**Thoughts for the fix** (the discussion of 2026-10-21; none tried):
- (a) **A starting probability concentrated on standard spacing,** counted from on-air traffic: the design notes'
  "measured from the population" form, the alternative to flat. For example, F = 1 with most of the mass, and
  Farnsworth readings paying a few nats once.
- (b) **Standard spacing by default, Farnsworth on demand.** Farnsworth readings would be started as rescues when a
  letter gap is much longer than standard spacing allows. This removes most of the branching.
- (c) **Spacing learned per sender,** like the level: each reading estimates its own letter-gap and word-gap stretch
  from its gaps. This covers Farnsworth and irregular hand senders alike.
- **Independently of Farnsworth:**
  - a character-frequency prior instead of the uniform 1/62, since merged symbols are usually rare ones;
  - over the factors, an exact average instead of the best one (about ln(23/k), with k the factors that fit).

**How to measure a fix:**
- DEV2's Farnsworth groups (I, I2);
- the one-word overs (TEST, TU);
- the speed step and the constant-speed stations, against the standard-spacing default;
- CPU per channel-second on noise.

### Short overs of dots read as one longer symbol in the new decoder (found 2026-10-21; open)

The new decoder's first prototype has no text model (every symbol costs ln 62 = 4.127 nats) and a broad timing model
(0.3 in ln(length), a placeholder). With the Farnsworth factor removed, "TEST" and "TU" sent alone decode correctly, but
only by about 1 nat at exact timing. Overs of repeated dots lose (derived with the fist's own ln P at 25 WPM; not run
through the decoder):
- "EE" alone reads as "I" by 0.06 nats; "TT", "ET" and "TE" behave alike.
- "EEE" alone reads as "S" by 1.05 nats, and "EEEE" as "H" by 2.07.

With no other elements to pin the speed, the merged reading takes a speed about 1.44 times slower. There a 3-dot gap
read as a 1-dot gap costs about 4.5 nats of misfit, against the 4.52 nats an extra letter and its gap's kind cost.
"EE" is the "dit dit" that often closes a contact.

**Thoughts for a fix** (none tried):
- A timing spread measured on real keying, which is likely tighter for well-sent code.
- A model of whole words or of a contact's closing words ("EE", "TU", "73").
- A speed prior that keeps a one-word over near the speed of the transmission before it.

A character-frequency prior would not help "EE" against "I", which are both common letters.

**How to measure:** DEV2's QSO group (H2) and the one-word overs above, sent alone, at several levels and seeds.

### A neighbor inside the channel takes over a weak station in the new decoder (found 2026-10-21; open)
**Status (2026-10-31): in the segmental decoder, a neighbor outside its narrowest filter is fixed (two widths and the leak floor; decision log). Open: an equal neighbor at 30 Hz (CER 0.976, bank 0.363), where every width fails; neighbors +10 dB re wanted inside about 100 Hz (0.398 at 100 Hz, about 0.7 at 80 Hz) and +20 dB re wanted at 100 and 150 Hz (0.772, 0.723); on the detector's path a steady carrier that takes the channel (checkpoint 2's record). Untried: a second carrier in the level filter, the neighbor's beat in the complex samples, subtracting a neighbor track.**


The new decoder's first prototype has no interference model (the plan's O2): its stories model one station per channel,
and everything else counts as noise or as that station.

Measured with the genie frequency (the tracker plays no part):
- The setup: a station sending "PARIS PARIS" at 25 WPM, a keyed neighbor at 22 WPM above it, three seeds.
- **Lost to its neighbor:** a station at +6 or +10 dB S500 beside an equally strong neighbor 40 or 70 Hz away, or beside
  one 10 dB stronger within 100 Hz.
- **Lost in every cell tried:** neighbors 20 to 40 dB stronger within 100 Hz, 54 of 54 runs.
- **E2-like cells** (station +8 to +14 dB S500, neighbor -6 to +12 dB relative, 12 to 90 Hz away): 6 of 108 runs exact.
- **The systematic term** (the plan's D15) changed none of this while the neighbor is keyed. Evidence:
  `docs/plans/data/2026-10-20-new-decoder-prototype-1/systematic-neighbors.csv` and the prototype's results record.
- **A neighbor that has stopped** (the search alone, a neighbor 30 to 40 dB stronger (dB relative to the station's
  key-down power), one key-down, 15 to 200 Hz away, stopping 0.10 to 0.30 s before the station starts): the systematic
  term remembers its power for 390 ms and costs the station its first element in 194 of 288 runs, all exact without the
  term (owner, 2026-10-21: kept as a measured trade). Evidence: `systematic-stopped-neighbor.csv` beside the table
  above, and the results record, section 1.7. A fix that tells the station's own power from a neighbor's would remove
  the trade. Updated 2026-10-25 to the design of plan D16–D17: the systematic term's memory is now 10 samples, 26.67 ms
  (owner, 2026-10-23; plan D17), replacing 390 ms; the prototype measures it on this stopped-neighbor grid.

The channel passes about ±150 Hz around the station, so a neighbor's carrier and its keying sidebands within that range
reached every story's sums. Updated 2026-10-25 to the design of plan D16–D17: the decoder now reads one front filter per
channel, matched to a 100 WPM dot (noise bandwidth 76 Hz), which rejects neighbors 70 Hz or more away by 15.3 dB (dB
relative to 0 Hz) or more; a likely gain, to be measured. A neighbor inside the filter still reaches the station's
envelope samples and its long-run power estimate (design notes, amendment of 2026-10-25, part 13).

**Thoughts for a fix** (none tried):
- a narrower or adaptive channel filter for the decoder;
- stories that carry a second station, an interference state;
- cancelling the stronger neighbor first (successive interference cancellation);
- a level model that refuses a level jump to the neighbor's.

**How to measure:** DEV2's QRM group (E2) in the measurement runs (R4; R9, the tracker's run, is void, since the new
decoder has no tracker; updated 2026-10-25 to the design of plan D16–D17), the grid above, and neighbors 40 to 70 Hz
away (design notes, amendment of 2026-10-25, part 15, item 15).

### Fading splits marks in the new decoder (found 2026-10-21; fix designed 2026-10-25, plan D16–D17)

**Status 2026-10-30:** the design of plan D16–D17 did not cure this; the level-dependent added variance did, up to +30 dB S500 (see "The amplitude model inside marks"; decision log).

The owner made this the next piece of work (2026-10-21): diagnose first, then a principled fix proposed to him.

Measured: "PARIS PARIS" at 25 WPM under flat Rayleigh fading with the generator's Gaussian Doppler spectrum, through
the decoder.
- **At f_D = 2 Hz** no run decoded exactly at any level from +10 to +60 dB S500, with or without the systematic term.
- **At f_D = 1 Hz** 7 of 20 runs at +20 dB S500 decoded exactly with the term, none without it.
- **The texts split marks:** "PA" reads "W I" or "A A".
- **The systematic term** was better in 242 of 360 runs, by character error rate, and worse in 29. Those 29 come from
  a mechanism of their own: the key-up overshoot of a station fading up raises C² inside its mark.

Evidence: `docs/plans/data/2026-10-20-new-decoder-prototype-1/systematic-fading.csv` and the prototype's results
record, section 1.7.

**How to measure:** that grid, DEV2's fading group (B2), and the genie fading spread.

**The fix chosen (owner, 2026-10-22; plan D16):** standard practice, noncoherent detection behind a fixed filter, with
each story's strength tracked as an amplitude. Chirp, key clicks and static are declared limitations.

**Status updated 2026-10-25 to the design of plan D16–D17:** the diagnosis is done and the fix is designed: one 76 Hz
front filter per channel, envelope samples every 2.667 ms, each story's predicted output (the smeared prediction), its
amplitude predicted from the long-run power Ω̄ at a fixed 2 Hz fading spread, and no blocks (design notes, amendment of
2026-10-25). The grid above is the prototype's first measurement of it (part 15, item 3), with the same grid at 8 and
5.2 WPM.

### Chirp, key clicks and static crashes: known limitations of the new decoder (owner, 2026-10-22)

Standard practice accepts these rather than modeling them (survey,
`docs/research/2026-10-22-cw-detection-standard-practice.md`); the owner agreed (plan D16).
- **Chirp,** a transmitter's frequency jump at key-down: the front filter passes up to about ±25 Hz; beyond
  that a mark's first samples are lost. Updated 2026-10-25 to the design of plan D16–D17: the filter is the 76 Hz one
  decided on 2026-10-23 (plan D17), −1.42 dB at 25 Hz and −3.8 dB at 40 Hz (dB relative to 0 Hz); "80 Hz" was D16's
  first figure. A model that learns each sender's chirp from its recent marks is possible later.
- **Key clicks,** splatter from hard keying: mainly a neighbor's problem; on the station itself, edges sharper than the
  template.
- **Static crashes** (impulsive noise): a filtered crash has the shape of a short dot, so a strong one will likely print
  a false E or T. A crash inside a story's key-down samples also enters its long-run power estimate, raising it, the
  safe direction (updated 2026-10-25 to the design of plan D16–D17). A wideband noise blanker ahead of the channelizer,
  as CW Skimmer offers, is the standard remedy, for later.

The test generator has none of these; measuring them needs generator changes, which need the owner's approval.

### A hang AGC as a fallback for the new decoder's long-run amplitude (owner, 2026-10-23; only if needed)

The new decoder takes each story's long-run amplitude from the RMS of its own key-down samples, with Ω̄, the expected
long-run power given the fades seen and the owner's flat prior (plan D16). Updated 2026-10-25 to the design of plan
D16–D17: the RMS is the ordinary-least-squares fit Σ s_k²(e_k² − σ_y²)/Σ s_k⁴ over every sample where the story's
template is nonzero, and Ω̄ takes it clipped at zero (owner, 2026-10-25; plan D17). A decision-free hang AGC on the
channel was adopted as its backup, then dropped from the design (owner, 2026-10-23): the Ω̄ fix covers the starts, and a
channel-level AGC knows a new station's level only from an earlier transmission within its hang. Its role and its
settings (rise, hang, release) are undecided; work them out only if the prototype shows the per-story RMS failing.

### The detector's frequency following and the fixed channel (owner, 2026-10-23; later)

Found while discussing the new decoder's tracker (plan D17). The owner: "There seem to be a web of issues here which I
don't think we need to tackle right now, but which should be recorded in the backlog", then: "My decision was only
about the detector, but I would say all the decisions that have to do with the decoder are still open." From
`docs/signal-processing.md` §6 and §7:
- **The detector follows a station's frequency after its first determination:** every hop (21.33 ms) each track moves
  to the strongest qualifying peak within D_ch = 47 Hz of where it is. What that serves today: keeping a drifting
  station from being born again as a second track, the Matched decoder (which follows it at every block) and the bank
  (which mixes by it). The new decoder reads it only at its channel's first block (a decoder matter, still open in the
  plan). Updated 2026-10-25 to the design of plan D16–D17: since decided, the new decoder has no frequency tracker and
  does not read the detector's frequency (owner, 2026-10-23: "A"; plan D17); the channelizer's following brings it the
  station near 0 Hz.
- **The channel never moves (decided away the same day):** the owner then chose that the channelizer follow the
  detector's frequency at every block (plan D17), so this item is no longer deferred.
- **Unmeasured on the detector path:** QSO turnovers (an answering station within 47 Hz moves the track) and drift;
  the new decoder's first prototype is measured on oracle channels only.

### Matched and the channelizer's frequency following (coordinator's ruling, 2026-10-26; later)

The spec's amendment of 2026-10-25 (§5.2, step 1) says the channelizer follows the signal detector's frequency for
each track (the shared FFT's nearest bin, moved at more than half a bin, 11.72 Hz, then a fine phase-continuous
shift), and D17 left to the implementation plan how the bank and Matched adapt, since their own shifts by the
detector's frequency become redundant. The amended build (`docs/plans/2026-10-26-new-decoder-prototype-1b.md`, O2)
applies the following to the new decoder's channels only. The bank stays frozen as the reference: its replay decodes
fixed-bin channel streams and mixes by the label's offset and drift itself. **Matched, the product's default, still
receives fixed-bin channels and re-centers with its own frequency tracker, which diverges from the spec's
amendment.** To settle later: whether Matched takes the followed channel (its tracker then sees the station near
0 Hz, and its re-centering becomes a residual correction or is removed), what that changes in its scores on the
development set, and whether the frozen bank reference must then be re-recorded. Until then the spec's sentence holds
for the new decoder only.

### The amplitude model inside marks under fast fading (owner, 2026-10-22: a known limitation)

**Status 2026-11-21 (`text-fast-1`), segmental decoder:** the walk and the frame now follow the Doppler spread the decoder measures
(from 3 Hz, at +10 dB S500 or more): VE3NEA's conditions at f_D 3 Hz read 0.073, 0.033, 0.023, 0.025, 0.024 (hand) and 0.055, 0.028,
0.020, 0.019, 0.022 (paddle) at +13.78, +17.78, +27.78, +37.78, +57.78 dB S500, against 0.103 to 0.083 and 0.071 to 0.052 before
(DeepCW 0.04 and 0.02 at the top); the matched fast-fading set's 7 to 14 Hz and 21 to 42 Hz cells (requested; about 6.8 to 12.4 and 16.7 to 23.9 Hz realized, signal-processing.md A.11, "Fading") 0.032 to 0.008 and 0.026 to 0.017
from 0.17 and 0.31. Open: under +10 dB S500 the walk stays slow (the fast one cost B2 there); DEV2 has no fast-fading cell above
+10 dB S500 (the item "Strong fading in the development set" below); the short-fade limit at strong levels is smaller, not gone.

**Status 2026-11-21 (`text-level-1`), segmental decoder:** on VE3NEA's conditions at f_D 3 Hz, after the level grid moved
onto the channel's floor, CER stays at about 0.07 to 0.08 (hand) and 0.045 to 0.052 (paddle) from +17.78 to
+57.78 dB S500, where DeepCW falls to 0.04 and 0.02. Traced: a fade to 20 to 30 dB below the mark's level (dB re its key-down
power) lasting 10 to 20 ms inside a mark is cheaper
as a short gap (its prior paid once, the short element's template dipping as the fade does) than as a mark (about 10 nats a
sample for the belief's distance); a 7-dot dash reads as M. A larger systematic floor (f = 0.2) about halves it on two recordings of each style of those
conditions but costs A2, B2, C2, F2 and I2 on the development set (decision log, 2026-11-21). Candidates: the phase (a Rayleigh
null turns the carrier's phase, a key-up does not; the channel stream is complex), a heavy-tailed or Rayleigh-consistent level
step (a finite-state Markov channel's transitions from the level-crossing rate, Wang and Moayeri 1995, whose steps in ln a
are large near a null), or f growing with the level. Measure on the strong-fading cells below.

**Status 2026-10-30:** eased, not cured: the variance added at lag 1 now grows with the level (`added_variance_multiplier_max`, `added_variance_level_knee`, heuristics; decision log). Still open above +30 dB S500, where no constant is right ("PARIS PARIS", f_D = 1 Hz: CER 0.48 at +60 dB S500 against 0.00 with the plain rule); a rate-adaptive variance is the likely next step.

The new decoder's amplitude per story is one number (Bell 1977's form), updated at every envelope sample, every
2.667 ms (updated 2026-10-25 to the design of plan D16–D17; 3 ms when this was measured), with the owner's rule for its
change. Fading is smooth, so a fade keeps its direction for a while; a one-number model treats each sample's change as
an independent step, so over n steps it expects √n times one step's movement instead of about n. Inside long marks
during fading it is therefore over-confident, even when it knows the station's true level and rate: about three times
too many 1% surprises at 2 Hz and 25 WPM (measured in scratch with 3 ms samples, not remeasured at 2.667 ms;
`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/amp-predictive.md` and its review).

Where it matters: the fading rate times the mark length, and mainly strong signals. Steady signals and fading up to
about 1 Hz at 25 WPM are unaffected (the fixed 2 Hz rule is calibrated or cautious there); 2 Hz and faster at 25 WPM is
affected; at slow speeds, with longer marks, it starts at slower fading (an 8 WPM dash of 450 ms, likely from about
1 Hz; derived, the 8 WPM check unfinished).

It also sets the newborn's first-mark tail (plan D24; the newborn-fading study, 2026-10-27/28, under the ratio form):
with the prediction step every 8 samples the newborn follows this same one-number model, and the first marks it still
loses against the exact fading reference, up to 52.5 nats worst at 5 Hz and 20.5 at 2 Hz (against the reference at
2 Hz), are a level rising out of a deep fade, which the model cannot follow in the newborn or after it.

A possible remedy is a second number per story, the amplitude's rate of change. That is standard in target tracking
(the nearly-constant-velocity model, Bar-Shalom) and in fading-channel estimation of the complex gain (Baddour and
Beaulieu 2005), but not established for amplitude-only tracking in a Morse decoder, and a Rayleigh amplitude turns
sharply at deep nulls, where a trend model misjudges. It needs a standard-practice check before being offered.

### Several fading rates per story in the new decoder (owner, 2026-10-22; later)

The owner chose one fixed fading rate, 2 Hz, on the fast side as Bell did (plan D16). Real fading runs from about
0.05 to 5 Hz. Running a few rates side by side in each story (for example 0.3, 1 and 3 Hz), weighted by the evidence
(multiple models: Magill 1965; Blom and Bar-Shalom 1988), would follow each station's own time scale, at about 3 times
the work per story. The owner (2026-10-22): "something like B, and all the other choices which add to the number of
potential stories needs to be considered very carefully given how much CPU we're currently requiring per channel."
Measure the CPU first.

### The edge's position handled jointly across its samples in the new decoder (found 2026-10-28; later)

The template's edge variance v_k (design notes, Part 5) covers where within its 2.667 ms sample an edge fell, but is
added independently at each of the 5 to 7 samples the edge touches, though one position is shared by all of them. The
standard treatment averages the likelihood over that one position, jointly across the edge's samples. Measured in the
score-form study of 2026-10-28 (C++, scratch; plan D23), with 8 positions of the latest edge: the true timing leads
its best one-sample neighbor by 131.7 nats at +40 dB S₅₀₀ and 25 WPM against 13.9 without it (47.1 against 6.6 at
40 WPM; 8.2 against 7.6 at +20 dB S₅₀₀); decoding of "PARIS PARIS" unchanged on that grid; ordinary scoring 17.8 times
the cost as built (+920% of the projected 425.7 ms per channel-second). A cheaper build (the shared-reference scoring
per position, marginalizing only the samples 4 to 11 after the edge, where the position matters) is estimated at
about 4 times today's scoring (rough). D23 rejected it for now at +920% CPU and put it in the backlog as a measured
variant; whether to take it up is left open, to the owner.

### A narrower filter for slower signals in the new decoder (owner, 2026-10-22; later)

**Status (2026-10-31): done in the segmental decoder as three filter widths with a choice of stream (decision log). Open: a joint search across widths; the slots' memory, about 3 MB a track (a 16-byte slot or per-fist blocks would cut most of it); a fine frequency estimate, without which a narrower filter on the detector's path pays for a track up to half a bin off.**

The owner chose one fixed filter for every speed: about 80 Hz wide, matched to a 12 ms dot at 100 WPM, sampled every
3 ms (plan D16). Updated 2026-10-25 to the design of plan D16–D17: the filter is matched to a real 100 WPM dot (12 ms at
half amplitude, 2.5 ms raised-cosine edges), noise bandwidth 76 Hz, sampled every 2.667 ms (owner, 2026-10-23; plan
D17). A slower signal pays for the extra width:
- the extra noise against a 50 Hz filter is 10·log₁₀(76/50) = 1.8 dB of noise power (derived; 2.0 dB with D16's
  80 Hz);
- summing the envelope over longer marks recovers part of it, not all.

The owner (2026-10-22): "record in the backlog that we can look later at how we might want to make the filter narrower
for slower signals." Possible forms: a few filters of different widths, each story reading the one matched to its
speed (as the bank decoder's speed ladder does), or a narrower filter once a station's speed is known. To be measured
against the single filter on DEV2's slow signals.

### Time-base invariance of the decoder (stage 2; known issue)
**Status (2026-10-07): applies to the Bell decoder** (design spec §5.2, amendments of 2026-10-07); written for the filter-bank decoder, the measure carries over.


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
**Status (2026-10-07): obsolete** — specific to the filter-bank decoder's periodicity estimate, frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


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
**Status (2026-10-07): obsolete** — specific to the filter-bank decoder, frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


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

### Fast fading with an echo: the owner's "auroral" clips (found 2026-11-21, `text-level-1`; the owner's top plain-text priority)

**Status 2026-11-21 (`text-fast-1`):** both causes modeled, by default. The clips' decorrelation is mostly the tail: kz4ap-fading's
ln |ρ| over every pair of a run is a straight line in τ, as pairs across a mark's end into another path's tail give, and the decoder's
estimator, inside the marks only, reads f_D about 4.6 Hz, not 9 to 10 Hz; the tail itself is incoherent with the mark (coherence 0.94
at the fall, 0.24 at 10 ms) and lasts past 40 ms (−15 dB re the mark there). The decoder now learns the tail per stream and scores a
gap's first samples against noise plus that power, and sets the level's walk and frame from the spread it measures (decision log,
2026-11-21; signal-processing.md A.8e, "The channel's tail and the walk set by the Doppler spread"). Against the owner's copy, cut to
38 s so that the track starts fresh as DeepCW's does (the fair comparison), the clips read 0.386 and 0.325 (DeepCW 0.398 and 0.338;
`text-level-1` 0.693 and 0.766); as whole tracks 0.341 and 0.234 (`text-level-1` 0.443 and 0.766). Open: the tail in the level filter (a third key
state was tried and cost the clips: 0.659 and 0.584), in the next rise's window, and a tail that changes within a track; the
generator's two-path option, so that a matched set with a tail exists (the tests' channel is the only synthetic one); more clips of
the owner's (aurora, long path) to see whether the shape holds; where the remaining errors on the clips are.
Resolved (review of `text-fast-1`, decision log 2026-11-22): the three window-confirmed calls once lost (WA2SCB 40 m 2026-11-17,
KE3TW 20 m and WB4OSS 40 m 2026-11-18) were lost by the fast walk on run stations' channels, their callers a few Hz apart read as a
spread; with the run-by-run check all three are back. The one window call lost against main, N8K (40 m 2026-11-18), is judged false:
a misreading of N8EA ("CQ N8EA CQ N8K CQ N8EA") that one RBN skimmer shares.

Two clips of W1AW's 35 WPM practice text on 80 m (the 2026-11-19 recording, 3581.504 kHz, 1049 to 1080 s and 879 to 910 s, about 26
to 28 dB re the noise key-down; the owner copied both nearly completely, and hears an auroral feel and fading) read at no-space CER
0.44 to 0.77 against his copy, with main and with `text-level-1` alike; DeepCW reads 0.398 and 0.338, nn-morse 0.295 and 0.351.
Measured with `kz4ap-fading` (bench/src/fading_main.cpp; `build/tl-1/clips/`, git-ignored):
- **Doppler spread.** The complex gain's correlation inside marks, |ρ| at 2.67, 6.67, 13.3, 26.7 ms: 0.983, 0.944, 0.882, 0.785
  (clip03) and 0.985, 0.944, 0.875, 0.749 (clip11); a Gaussian fit gives f_D = 2σ_D = 9.3 and 10.0 Hz. The same tool on our port of
  VE3NEA's generator reads f_D 3, 10, 30 Hz requested (3.0, 9.6, about 20.8 Hz realized; A.11, "Fading") as 3.6, 9.6, 20.4 Hz (|ρ| at 26.7 ms 0.956, 0.730, 0.237), so the clips' core is about
  f_D 10 Hz; their faster drop at the shortest lag (0.983 against 0.996) says a few % of the power is spread wider.
- **Slow fading.** The median key-down level per second, dB re the noise: a spread of 3.4 and 3.6 dB rms and ranges (largest less
  smallest) of 13.4 and 14.8 dB over 30 s (the 31 s spans the owner copied; over the 38 s cuts 3.9 and 3.6 dB rms).
- **An echo.** After a mark falls 6 dB, the power stays at −9 to −13 dB re the mark for 20 to 30 ms before reaching the noise
  (−26 to −28 dB re the mark): a delayed copy or a scattered tail, about 10 dB below the mark. A clean synthetic carrier falls to
  the noise within 2.7 ms; at f_D 10 Hz it falls to −16 to −25 dB re the mark. That is what stretches the marks (median 58 to 62 ms against 40.7 ms for a 32 WPM
  machine through the same tool) and fills the gaps (median 13 to 14 ms against 34.7 ms).
What was tried (decision log, 2026-11-21): a channel floor at the 35th percentile with no margin, which covers the echo, reads the
clips 0.364 and 0.364 but costs DEV2 +0.12 (A2, B2, F2, S2); a faster level walk reads the matched fast-fading set much better (the item on the amplitude model under fast fading)
but the clips worse (0.85, 0.81), since it follows the echo into the gaps. So two causes: fast fading (the level model) and the echo
(not modeled). The fix to try for the echo: the fall's template extended by the channel's measured tail (the shelf's level and
length, learned per track from the falls the search reads), so that a gap's first 20 to 30 ms are scored against an echo of the
mark rather than against noise; standard practice for a dispersive channel (a channel impulse response estimated from decided
symbols, as in decision-directed equalization). The generator would need a two-path option to make a matched test set.

### A test that differs by compiler: EngineCopies.TwoCallersKeyingAtOnceKeepTheirTracks (noted 2026-11-21, `text-fast-1`)

It fails on Linux with GCC at main 5eac08d (3 of the 4 calls it asks for) and passes on MSVC; not investigated.

### Strong fading in the development set: cells above +20 dB S500 (proposed 2026-11-21, `text-level-1`)

DEV2's fading group B2 stops at +20 dB S500 (two S500 cells, 0-10 and 10-20 dB), so it never showed the rise of CER with the
level that VE3NEA's conditions showed (hand, f_D 0.1 Hz: 0.013 at +37.78 dB, 0.112 at +57.78 dB S500; fixed in `text-level-1`),
nor the short-fade limit above. Proposed, not generated: a group B3 built as `suites.fading2` is (the same 10 speed cells and
4 f_D cells drawn in ln f_D, 0.05 to 5 Hz, VE3NEA's style mix and imbalance, his 2 ms centered edges, the same packing of
stations 1.2 kHz apart), with three S500 cells, 20-35, 35-50 and 50-65 dB (jittered within each cell), one signal per cell and
speed (120 a seed), and an edge cell for half of them, hard edges (0.8 ms 10-90% rise, ARRL's fastest) against 2 ms, so that a
station's own keying splatter is in the set. Its own signal-seed code (not B2's 2), so that its noise is independent, and its
own builder entry in `DEV2_BUILDERS` as B3, scored with B2's rules; about a tenth more time per seed. What it would have caught:
the level grid's defect (a strong station's own keying read as a leak floor moved its level inside marks; hand, f_D 0.1 Hz, +57.78 dB S500), and it holds the fast-fading cases of the item above.
`text-fast-1` adds a need: f_D cells above 5 Hz (to 40 Hz, as the matched set's request, which realized only about 24 Hz: such a cell needs `fading_full_rate`, A.11) at +10 dB S500 and up, where the Doppler walk acts, and
a two-path cell with a diffuse tail 10 to 15 dB under the mark for 20 to 50 ms, where the tail model acts; neither is in DEV2, so its
whole-set runs show only that both leave the other groups alone.
The generator's fading rate (review of `synth-tail-1`, 2026-11-22): at its default 50 samples/s a requested f_D above about 5 Hz is
realized lower (21, 30, 42 Hz as about 16.7, 20.8, 23.9 Hz by coherence time; signal-processing.md A.11, "Fading"), and
`fading_full_rate` realizes it. No committed set requests more than 5 Hz (DEV2's B2 at most 5 Hz, B and VE3NEA's grid at most 3 Hz),
so none needs regenerating; a fast-fading cell made for this item, and the scratch matched set (`build/tl-1/fast/`) if it is used
again for its 7 to 14 Hz and 21 to 42 Hz cells, should be made with `fading_full_rate` on.

### σ² in steps finer than the level grid's (found 2026-11-21, review of `text-level-1`)

With the level on the channel's floor, the segmental decoder takes σ² at F × 10^(1.5 d / 10), d the nearest whole 1.5 dB grid
step above the floor, so z can be taken at up to 0.75 dB re σ² below σ² itself, below the leak floor when that sets σ². The 2017
caller K4RUM (the recording KC2LRC-2, 73 Hz from a run station) is lost by it. σ² rounded up, and z at the true σ² with only the
tables' shift rounded, both keep K4RUM but cost E2 +0.0078 and +0.0096 against main, where the nearest costs +0.0044
(`docs/signal-processing.md`, A.8e, Limitations). To try: shifts finer than a step, by interpolating each term between the two
table rows either side of the unrounded shift (in its constants, or in the value x), or by tables in 0.5 dB sub-steps below the
grid (120 rows for 60 dB re F, the levels themselves still 1.5 dB apart). Measure on E2 (both seeds, oracle and detector's path),
the strong pairs, and the three replay sets (2026-11-17, 2026-11-18, 2017) with the RBN judge, K4RUM among them. E2's cost of the
level on the floor is not the rounding's (neither alternative reads it better), so its cause is a question of its own.

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
   for example at most about 10 stations on a desktop.

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
   thread, i7-12700H).
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

**For the new decoder (owner, 2026-10-16):** replay at every opening is
part of its design for live use (design notes,
`docs/design/2026-10-09-new-decoder-design-notes.md`, "Opening and closing
channels", decision 3); its length is set there from the measured opening
delays, and the 2 s above is a placeholder.

**Matched, a channel that opens mid-mark (milestone 2, part 1, Task 16):**
the partial mark is no longer counted for speed (`docs/signal-processing.md`
appendix A.8, "Marks whose start was not observed"); measured before/after (Task 17;
`docs/plans/2026-09-27-milestone-2a-results.md` §8.3, "A channel that opens mid-transmission"): the smoke recording's
Matched CER 0.0622 → 0.0436 (60 → 42 edits in 964 symbols), its
+7617.6 Hz station 0.234 → 0.043; in a focused run of 80 oracle openings
per speed (machine keying, S₅₀₀ 25 and 10 dB), openings worse than
Envelope by at least 5 edits went 6 → 0 and 3 → 0 at 18.5 WPM, 0 → 0
and 2 → 1 at 25 WPM, and 5 → 6 and 12 → 15 at 12 WPM, where a milder
loss is new (the first characters read as T's; cause inferred in
`docs/signal-processing.md` appendix A.8, "Slow stations at start-up
(12 WPM)"), and
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

**For the new decoder (owner, 2026-10-16):** its no-signal hypothesis
decides when a station has stopped, and it closes the channel (design
notes, "Opening and closing channels", decision 2); what it prints on
noise is measured as characters per hour on noise-only channels (the
standard-practice check, step 1, P5(h)).

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

**For the new decoder (owner, 2026-10-16):** the decoder closes the
channel when it finds no station (design notes, "Opening and closing
channels", decision 2); whether a station's identity, speed and level
survive a pause is topic 7's question. Derived there: a channel now closes
11–21 s after its station stops (the average's decay, ln R seconds for an
averaged SNR per bin R as a power ratio, plus the 10 s timeout).

### Ghost tracks beside very strong signals

Around 60 dB SNR (500 Hz), extra tracks appear a few hundred Hz either side of a
station and decode runs of `E` and `I`. Fix idea: reject a peak that sits
inside a much stronger track's skirt (more than X dB below that track's level within
±N Hz).

Measured (stage-1 results record §5.4): on the strong group, Matched and
Envelope printed text on 12 extra tracks, the bank prototype on 1
(presumably these ghosts; not checked track by track). A lower
opening threshold, as the new decoder's design intends, opens more of
them; they are counted on real recordings and handled with the channel's
other contents (design notes, "Opening and closing channels", 2026-10-16).

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
`docs/signal-processing.md`, section 6). The new decoder's design takes
this item up (owner, 2026-10-16; design notes, "Opening and closing
channels"): the threshold is set from a false-channel rate against a local
floor, topic 3's hiss estimator, and the average and persistence are
derived with it. A simulation on white noise found no false track in an
hour at 2.25 dB above the floor or higher, 5 at 2.0 dB.
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

`DetectorConfig::max_tracks` = 400 is a guess at a CPU guard, not exposed to
users. Measure throughput against the number of tracks (on the desktop and the
Linux machine), set the default with margin, and expose it in the app. A
Raspberry Pi is not a target (owner, 2026-10-26). A contest band holds a
few hundred signals (CW Skimmer copied 326 on 40 m in CQ WW CW); the
new decoder's eventual target is about 500 tracks on one band (plan D18).

### Spectrum frames in linear power

Done: frames carry `power_fs2` beside `power_db` since 3f99f4f, the detector
averages linear power, and the segmental path's detector's analyzer computes
no decibels (e766e64; signal-processing.md, A.4).

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
`docs/signal-processing.md` appendix A.11, "QSO regimes"); the milestone-1 detector
(the Envelope path) hears stations within about 47 Hz as one track, from
about 70 Hz as two, and either in between.

Measured on group H (milestone 2, part 1, 3 seeds, through the detector;
`docs/plans/2026-09-27-milestone-2a-results.md` §3.6 and §8.3): tracks per QSO, Envelope / Matched, were
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
the Matched path; `docs/plans/2026-09-27-milestone-2a-results.md` §8.3,
"Groups G and H"), and separate-track QSOs are dominated by track
turnover on both paths. How many real answers fall in each band
therefore matters.

### Tune the Matched front end by measurement
**Status (2026-10-07): obsolete** — specific to the Matched decoder (kept as a baseline, no longer tuned), frozen as the reference now that the project follows Bell 1977 (design spec §5.2, amendments of 2026-10-07). Kept for the record.


Tuning waits for evidence from the benchmark (owner, 2026-09-27 and
2026-09-29: only with strong evidence; the benchmark exists to find better
values). The Matched front end is the default from milestone 2, part 1
(owner decision 2026-09-29). Its parameters are heuristic
(`docs/signal-processing.md` appendix A.6, A.7, A.8, A.8b): the filter length
β = 0.8 dit (sweep 0.6, 0.8, 1.0), the amplitude and noise time constants (0.5 s, 2 s;
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
`docs/plans/2026-09-27-milestone-2a-results.md` §8.3, "Measured: Matched against Envelope";
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
  `docs/plans/2026-09-27-milestone-2a-results.md` §8.3 ("Limits
  measured") read Matched CER 0.000 and 0.015 (were 0.464 and 0.981), and group
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
  cause inferred in `docs/signal-processing.md` appendix A.8, "Slow
  stations at start-up (12 WPM)", not instrumented).
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
0.025–0.234 (`docs/plans/2026-09-27-milestone-2a-results.md` §8.3). The answering station's
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
group H's 50 Hz rows in `docs/plans/2026-09-27-milestone-2a-results.md` §8.3 are the cases to
check). This is a code fix to meet a decision already made, not a
parameter change, but the owner decides when.

**Fixed (Task 15, commit 59cd450):** each key-up counted for speed saves
the dit estimate, the filter's dit and length, the count of marks since
the last re-acquisition and the speed window, and a dropout merge that
re-opens that mark restores them, so each physical mark is bounded once
(`docs/signal-processing.md` appendix A.8, "Once per physical mark"; history in `docs/plans/2026-09-27-milestone-2a-results.md` §8.2). **Measured
before → after** (Task 17; full suite, 3 seeds, the same recordings;
the after figures include Task 16; Task 15 alone was also scored, and
its figures are given where they differ): group A, Matched S₅₀₀ at CER 0.10
(dB): 0.2 (−0.5 to 2.2) → −0.2 (−0.5 to 0.7) at 12 WPM, 2.7 (−0.0 to
10.4) → 1.1 (0.4 to 1.4) at 25 WPM, 3.1 (2.0 to 3.6) → 2.9 (2.2 to 3.3)
at 40 WPM; the two start-up runaways of
`docs/plans/2026-09-27-milestone-2a-results.md` §8.3 ("Limits measured"),
Matched CER 0.464 → 0.000
(`A-awgn-25wpm-1-s1`, +6606.5 Hz, S₅₀₀ 10 dB) and 0.981 → 0.015
(`A-awgn-25wpm-0-s2`, −2991.9 Hz, S₅₀₀ 2 dB; 0.074 with Task 15 alone);
a regression from Task 15 alone, group D's speed step from 20 to 35 WPM,
0.113 → 0.394, the text stopping after the step in 5 of 6 signals (not
diagnosed; the same record's §8.3, "Where Matched is worse than
Envelope"); group H through the
detector at 50 Hz (grid, 6 QSOs), QSO-label CER 0.533, 0.154, 0.046,
0.415, 0.032, 0.040 → 0.557, 0.234, 0.046, 0.374, 0.025, 0.043: the two
QSOs that split into two tracks (the same two; tracks per QSO 1.33 before
and after) still read worst, consistent with the split, not a runaway,
failing them (inferred; the same record's §8.3, "Groups G and H").

### A mark whose key-up the squelch forces (possible limit; derived from the code, not observed)

The end-of-mark counterpart of Task 16's rule (`docs/signal-processing.md`
appendix A.8, "Marks whose start was not observed"). In the Matched decoder a mark
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
is not recorded; `docs/plans/2026-09-27-milestone-2a-results.md` §8.5). To do: record each track's
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

- **From the CW Skimmer survey and the owner (2026-11-18):** the owner's GUI changes, which he will list. A
  decoder-only mode (section 4, the owner's rulings): done in `app-modes-1`, with the signed-only list and the known-call
  list's file and download button (`docs/signal-processing.md`, A.12, "The app's modes and list"). Listening to the
  selected station live through the sound card, picked by a click; the owner: a clip saved to a WAV file the user then
  opens is not useful in a release. The survey's other display gaps: calls drawn on the waterfall, done in
  `app-settings-1` (README, "Calls on the waterfall"), without CW Skimmer's CQ or DE before a label, bold for a call
  seen twice, QRL? and 599 labels or its label filter; a one-signal text
  pane, done in `app-settings-1` (README, "One signal"), still without CW Skimmer's colors by kind, pale low-SNR
  characters and » for a long pause (they need a per-character confidence and pause times the table does not keep), and
  following a click rather than the radio's tuned frequency until there is tuning; settings kept between runs, done in
  `app-settings-1` (README, "Settings kept between runs"). Not kept: the decoder and the threads, which the window does
  not offer (only `--decoder` and `--threads`); a choice of decoder in the window would be kept too.

- **A channel that follows a QSO turnover** (owner, 2026-09-29): with the
  channel distance (milestone 2, part 1), one channel follows both stations
  of a QSO within 47 Hz of each other. When it decodes "DE <call>", its
  displayed callsign should change to that call, and change back when the
  other station's call is decoded. The same rule belongs in callsign
  matching (section 4).
- **Showing each character's confidence** (owner, 2026-10-06; much later,
  something to consider for the UI): shade decoded characters darker or
  lighter by the decoder's confidence in them. Needs a per-character
  confidence that means something first: the bank decoder now publishes a
  placeholder probability of 1 per character (signal-processing.md appendix
  A.8c, "The bank decoder behind the engine");
  candidates are the fit's class posteriors per element, the text model's
  score, and whether a later correction is still possible (a character
  inside the 20 s correction reach is provisional).
- **Found building the app's first stage (2026-10-30):**
  - **Worker pool** (done on `app-2`): the pipeline was one thread (`StreamRunner`), so at the bank's
    105 ms CPU per channel-second measured on the desktop under load, about 9 tracks
    fill it. The spec's pool (section 4.1, step 4) comes before a busy band.
  - **A silent bank track** (understood and fixed on `app-2`; decision log): on a generated recording (`--seed 11 --signals 8` at
    192 kHz, 30 s, S₅₀₀ from 12 to 32 dB) the station at -57 051.6 Hz, 26 WPM,
    +24.0 dB S₅₀₀, starting at 0.057 s, gets a track but no text on the detector
    path, in `kz4ap-bench` too; with `--oracle` it decodes without an error. Not
    yet understood.
  - **The new decoder in the app:** it runs by name at 48 kHz, but gave no text in
    the first 15 s, gives no speed, costs about 1.2 s CPU per channel-second and
    refuses an 8 kHz recording (the sample interval is not a whole number of
    channel samples).
  - **Not there yet:** seeking in a recording, and the replay that lets a channel
    hear a transmission from its start (the design's amendment of 2026-10-19), so a
    track's text starts about 1 s after the station does.

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
- **Done (branch `audio-input-1`, 2026-11-19), files only:** a one-channel WAV
  (or `--audio`, `--audio-channel N`) is read as receiver audio, resampled to
  48 kHz and made analytic by an FIR Hilbert transformer, its delays taken out
  exactly; the receiver's passband (`--passband-hz`, default 300–3000 Hz)
  bounds the detector's floor, peaks and the hiss estimator's bins; RF from
  `--dial-khz`, `--pitch-hz` (default 600 Hz) and `--sideband usb|lsb`, RF =
  dial ± (audio − pitch). `kz4ap-calls` and `kz4ap-bench` take it and label
  its SNRs as receiver-audio values; `kz4ap-listen --filter-center-hz` makes
  receiver-like clips (a 2.4 kHz filter with the station at a 600 Hz pitch).
  `docs/signal-processing.md` section 1 and A.1, "Receiver audio".
- **Still to do:** a sound-card source (live, needs the owner's say for any
  device), the plain decoder's spartan window (one or a few signals) and the
  app's use of the passband; the dial frequency from CAT; AGC and a real
  receiver's filter (the clips so far are the SDR's, without AGC); recordings of
  the K3's audio beside SDR I/Q of the same stations; a passband estimated from
  the spectrum for a filter narrower than the stated passband; a real-input or
  FFT-folded resampler (about 3.5 ms per s of stream at 8, 12 and 44.1 kHz).

## 4. Callsign matching

Needs its design session first (spec §6). Items from milestone 1's review
that must land before it are in section 1 ("Wrong or missing first
characters"). A channel that follows a QSO turnover carries two stations'
calls in turn; relabel it when it decodes "DE <call>" (section 2, owner
2026-09-29).

### The owner's rulings of 2026-11-18 (after the CW Skimmer survey)

The survey is `docs/research/2026-11-18-cw-skimmer-survey.md`. Its section 3 lists the candidate gaps for each
purpose.
- **Two purposes.** The program is a skimmer that acts a lot like CW Skimmer, and also a plain CW decoder, which
  "has nothing to do with callsign identification". So the app needs a decoder-only mode: no call stage, call list
  or known-call list. Until `app-modes-1` the call stage always ran.
- **Which calls are listed.** Every call by default, bare and named included, as CW Skimmer does, so that the
  program can one day feed the RBN. An option shows signed calls only.
- **The known-call list.** Optional and skimmer only; the program never ships one. The user either names their own
  file or presses a button that downloads MASTER.SCP for them; the app reaches the network only then.
- **Extras.** Features CW Skimmer lacks are welcome. The survey's purpose is that nothing of CW Skimmer's that we
  want is left out.
- *Done in `app-modes-1` (2026-11-18):* the first three, in the app (Mode, List and Known calls on the toolbar's third row;
  `--decoder-only`, `--signed-only`).
- **The plain decoder is its own window (owner, 2026-11-19).** "The 'decoder-only' mode is intended almost as an
  entirely separate window/app, paired with audiocard decoding instead of SDR. ... As a CW decoder-only thing, we would
  be interested in decoding the audio from my K3, for example. The interface for that mode would then be far more
  spartan and would focus only on decoding one or a very small number of signals." So the skimmer window's
  decoder-only mode is not that product; the plain decoder is a separate, spartan window on receiver audio (section 3
  above), one or a few signals. GUI work stays after the decoder's quality in the priorities; testing on a sound device
  needs the owner's say each time.
- **Receiver audio for the skimmer too (owner, 2026-11-19).** "I guess we should implement the same kind of '3-kHz
  Radio' receiver in skimmer mode, too": the skimmer runs on a radio's audio as on SDR I/Q, as CW Skimmer's 3-kHz
  receiver does (the user sets the CW pitch; the dial frequency from CAT, or entered), every signal in the passband
  decoded and its calls listed at RF = dial + (audio − pitch) in upper sideband.

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

- **Code comments and labels that review 16's subagent flagged** (checkpoint 4, not one-line document fixes; the flags came
  without detail, so the defect named for each is my reading, to check first): `engine.cpp:91-92`
  (the prominence and sideband settings, labeled together "heuristics, chosen by measurement"; A.6 says prominence chosen by
  measurement, the sideband values heuristics) and `:98-99` (30 Hz and 12 Hz together "chosen by measurement"); `callsign.hpp:157-158`
  (the report thresholds labeled "chosen by measurement" though `points_named` 2 is standard practice) and `:192` (`history_s`'s
  provenance). Each to check against A.10 and relabel in the code.
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
  reference symbol it could belong to (`docs/signal-processing.md` appendix A.11),
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

## Fast code between whole samples (new decoder, found 2026-10-30)

**Fixed 2026-10-30:** the merge key now holds the edges of the last 12 samples: 45 to 100 WPM, 84 of 84 runs exact (decision log). The diagnosis below, "cannot be claimed exactly", was wrong: claims were dropped at the merge before their evidence existed, and the joint edge-offset item is not needed for this.

"PARIS PARIS" at 80 and 100 WPM reads wrongly on most seeds, with letters merged or split (`P<AR>IS`, `PAR5`, `LARIS`), and gets worse from +20 to +40 dB S500, while 70 and 90 WPM are mostly exact and 90 WPM (a dot of exactly 5 samples of 2.667 ms) is exact at +40 dB S500 on 6 of 6 seeds. Dots of 5.6 and 4.5 samples cannot be claimed exactly, and the misfit grows with the signal. The joint edge-offset item (design notes, part 5, the audit's row 10) is the first thing to try. Measured on 6 seeds per point, a 25 WPM to 100 WPM slice of synthetic machine keying only.

## The app on the air (found building app-2, 2026-10-30)

**Where it stands (`fast-4`, 2026-11-01; Linux machine).** On 30 min of real 40 m 6.07 ms per channel-second as fast as it goes, 73 ms per s of stream, against 11.53 ms and 138 ms at checkpoint 2; the whole test set 3.45 ms against 6.88 ms; the same 20 rows. So the goal of about 8 ms is met on the air as fast as it goes. In real time on the real display (the desktop, by cycles, measured in review 7): 22.88 to 11.80 ms per channel-second over those 30 min, GUI thread 109.9 to 25.2 ms per s of wall; so the goal is met at real time on a busy band (the contest 4.21 ms) and not on a quiet one, at 12 channels, where what does not scale with tracks is spread thin. What is left is the same in shape: on `live2` about 30% of all CPU in the evidence and 50% in the search (a profile by samples); on `real30` 61% of decoder CPU off its stations' places. Tried and dropped, with their costs: `docs/decision-log.md`, 2026-11-01. What follows is at `app-7`.

**Top CPU item: junk tracks on real 40 m (review 5, 2026-10-31).** On the 30 min of real 40 m, measured at `app-7`: 63.8% of decoder CPU goes on junk tracks (1 034 of 1 178 tracks, 159.3 s of 249.6 s); at real stations' places 90.3 s, 11.56 ms per channel-second alone, so removing junk does not by itself reach the 8 ms goal. Junk by life: 824 tracks of 5 to 15 s hold 37% of decoder CPU. By place: 7072 to 7081 kHz 25.3%, 7102 to 7110 kHz 12.8%, no call in either. The faster judge (`kUnprovenS` 8 s, `kQuietNewS` 5 s) saves 21.5% of decoder CPU but loses E51MWA, whose only exact word falls in the gap after his first track is retired, and raises I2 by half on the detector's path: not taken, recall comes first. Not yet run: a rule on the text's letters (a track of 30 s or more, at least half T and E: offline, 15 junk tracks, 9.1% of decoder CPU, against 2 tracks and 0.2 s at real places; derived), and a mask on those two segments (38% of decoder CPU, derived; it hides any CW there). Measure: CPU per second of stream or capacity, since fewer junk channel-seconds raise ms per channel-second (12.31 to 14.20 ms in that run); keep E51MWA, and I2 on the detector's path.

Real 40 m at 192 kHz from an RSP1A (7030 kHz center, 62 s, kept out of the repository):

- **False tracks on wide signals (app-3: judged and retired, `track_evidence.hpp`).** What is left: tracks still open on
  wide signals and live 4 to 14 s each, about 25 of the 42 live on the 62 s recording, at 15 ms per channel-second; 35% to
  63% of the rows listed are junk (35% on that recording, 63% on a later 30 min one), data at +91 kHz most of all, whose text cannot be told from a contest exchange by its
  letters alone. A test before a channel opens, or a bandwidth test that does not fail on speech's harmonics, would save
  that CPU. Weak and wide channels still cost the most, because more stories stay within the beam; since `fast-4` a ceiling of 1 500 elements bounds them.
- **Short overs and carriers under a hold (`app-4`'s review, F6).** Text during a hold is lost, not late. A station in short
  overs with little proven, a carrier before a CQ, a runner with callers within 47 Hz: each can lose an over. No-hold-on-quiet
  gained 1 caller of 170 and cost 28% more channel-seconds; the rule stands. The replay made most of it smaller.
- **Junk rows.** Data and speech rows stay on the track list; the call list beside it has none. A view of the rows by plausibility is not built.
- **Fine-tuning off the center.** Tuning the radio a little off and shifting digitally would remove the dead zone at 0 Hz.
- **Done in `det-2` (2026-11-15): two or three tracks on one station** (the owner's recording, 2026-11-04: K4TZ's CWT run read on
  three tracks at 9 790, 9 847 and 9 897 Hz from the center, 7029.803, 7029.860 and 7029.910 kHz, 57 and 50 Hz apart; A.6). A track whose decoder's envelope follows an older, proven neighbor's keying is retired as its copy
  (signal-processing.md, section 6 and A.6; what is left: "Copies after det-2" below). As it stood: two tracks on one station,
  40 to 135 Hz apart, on a strong keyed carrier (W1AW at 7047.5 kHz), seen live while the pipeline lost samples.
- **A real station's first seconds under a hold.** A place retired stays closed 8 s and more (at most 120 s; a region 30 s and
  more): a station that starts there waits. Measured only as no station lost on three recordings of 10, 62 and 20 s.
- **The fixed cost's rest:** the channelizer's FFT could share the spectrum's, the Hann window applied in the frequency
  domain (7 ms per s of stream at 192 kHz); a table lookup for the display's logarithm.
- **RSPduo:** single tuner A is set in the source, not run on one. The API's other models' LNA tables are the API's to refuse.
- **Two radios on one machine (`sdr-1`, 2026-11-15).** The source takes only the radio with exactly the serial number asked
  for (`--sdrplay-serial`, or the Radio serial box), and none when none is given; `--list-sdrplay` lists them without taking any.
  Tested against the stand-in API only, not on two real radios. The serial is kept between runs since `app-settings-1`
  (an unattended start, which reads no settings, still names it on its command line). Not built: a list of the radios found
  to pick from in the window (the message of Start radio with no serial names them).
- **A long recording:** the WAV form stops at 4 GiB (93 min at 192 kHz); rolling to a second file is not built.

## Callsign stage (call-1, 2026-10-31)

Deferred from the first stage (`docs/signal-processing.md`, section 11):

- `cty.dat` for allocation by DXCC entity (the stage knows only the ITU's series), and a watch list distinct from MASTER.SCP.
- A pattern table in place of the flat bare and framed thresholds, as CW Skimmer's `patt3ch.lst` ranks common and rare prefixes; only after the owner's hour shows it would pay. `call-form-1` took the first step: four letters after one digit is an unusual form, from a list only.
- **Call forms left open by `call-form-1`** (A.12, limitations): SH5SHI, a run of dots read three times as a whole word, has a real call's form (dot-only calls such as EE5E and SE5S are real, listed and spotted), so no form rule refuses it; national suffix rules beyond the FCC's (the UK, Spain) were not taken, for want of a source as firm as the FCC's file. **Done in `call-form-2` (2026-11-19):** the words of CW (K1EL, K5NN, K9AY, N1MM, N5NN, W2DU, W8JK, active licenses) and every call of one or two letters before 5NN (DJ5NN, W5NN) are reported from a list only, not refused, so MASTER.SCP rescues K9AY, N1MM and its five 5NN calls; a slash with a prefix of a call's form (VP2V/K1ABC) reads the call.
- **Left open by `call-arb-1`** (A.12, limitations): a fragment read joined itself or glued (K 1L GQ, N 2E DD, W 1B HC, DEK4VR T) is covered by no joined reading, so K1L, N2E, W1B and WA5D still beat K1LGQ, N2EDD, W1BHC and WA5DSS on the 2026-11 recordings; covering them as tried lost window-confirmed calls (HB9T, KZ8R, K8BZ, HC5A, W6SX). The gap before the added letter does not tell (K4VR T: 7.8 dits, a word space, against 3.4 to 4.3 dits inside the call), so a rule needs other evidence: the longer call read whole elsewhere, the RBN-style vote of several transmissions, or the operator's spacing measured over his other words. Other run-together keywords than TU (X) before a call are not read (DE run on is B: BW3RJ). Since the review, the longer call needs 1.5 points of its own (`covers_own_points`), so W4BOH on N2GZ (one whole word at its place) is lost again to W4B; a stray E, T or I stripped off a listed call still weighs triple (`flat_stripped` 2 lost M2E with MASTER.SCP).
- **Left open by `call-form-2`:** without a list the 5NN calls (18 active US licenses) and the CW-word licenses are lost; a pattern table, or the RBN's own rate of busts by form, might let the common ones through. The RBN spots calls of an unallocated U and a digit (U1TN, U5IN, U5PJ, U5YG on both 2026-11 days), which `allocated_pair` refuses: not checked against Russia's or Ukraine's allocations. **Since `calls-list-1`** these forms pass unlisted on a points bar (4, 3 signed; the item below).
- **Left open by `call-short-1`** (A.12, limitations): CWT is not a keyword of the Role rule, so "CQ CWT call" reads bare and a 1x1 running in the CWT needs three readings; adding it would change every call's signed flag, so it was not done here. **Done in the review of `calls-list-1`:** CQ and one event word (CQ CWT) sign the call after them (`cq_event_signed`). Fragments of a longer call of four characters or more (MO0M, OQ0MM held out on 2026-11-19) and CWT numbers run onto names in other shapes (A78JIM) are untouched; the pattern table above, or a rule against a longer call read nearby, would be the next step. Since the review the cut-number rule is a points bar (5 bare, 2 signed: the owner's ruling, several repeats and no signature required), so a CWT runner's own number (2T7A, 3N7N, 4T2A; 12 on the 2026-11-18 CWT, read 7 to 71 times) passes as a call would; a rule reading the exchange's context (a name before the number) would tell them apart. **The RBN judge's `us_license` for a 1x1** (review): the FCC's HD.dat holds no US 1x1 (0 of 1 607 236 distinct US-form calls), so every US 1x1 read `never`, real or not; done in the review, the judge (`training/kz4ap_proto/rbn_judge.py`) reports `-` for one, with its test.
- **Left open by `calls-list-1`** (A.12, limitations): the four forms once from a list only (special, unusual, prefix-digit, words of CW) now pass unlisted on a flat bar (4 points, 3 signed), whole and glued words only, holding no rival back until reported. The data give no recall evidence for any bare bar (every call gained on it was junk or an exchange number); a recording with real calls of these forms heard bare (a DXpedition of a prefix-digit series, a special-event station calling others) would. Junk read often still passes (D4Y, 44 whole words on 20 m, 2026-11-17; A2EW), and a CWT runner's number starting with a 1, sent A, has the prefix-digit form (A7TA, 1701, read 71 times), as the cut-shaped numbers do (a rule reading the exchange's context would cover both). A pattern table per series, as CW Skimmer's Patt3Ch.lst (C6@, D4@, E7@, J6@ typical; E5 only E51@@), would let the series that issue prefix-digit calls through at a lower bar and hold the others to a higher one; MASTER.SCP's 92 prefix-digit calls and the RBN's spotted ones would seed it. A keyword before 5NN (24 strings, K5NN and GM5NN among them) needs a signed reading, so such a station calling bare is lost unlisted. **The generated sets' scorer** (`training/kz4ap_proto/calls_score.py`) refuses a prefix-digit call by its own call pattern and skips its row, so a false call of that form is not counted (L3I, found by hand in the review of `calls-list-1`). **Done in that review:** the prefix-digit form is taken, and a CSV row outside the reader's grammar is scored as it stands and counted. **CQ and an event word** sign the call after them (20 words, exact); other event words, or a rule for any one word between CQ and a call, are untried. **BK as a sign-off:** <BK> now reads as the keyword BK, but BK has no role, so "call BK" reads bare where "call K" signs; whether BK after a call should sign it, as K and KN do, is untried. **BK in the app's text:** the plain text keeps <BK> (one symbol for CER, as the scorers and the outside-decoder comparison count it); the owner "would normally write just bk", so a display option could print BK in the app without changing the scored text.
- **Left open by `calls-form-3`** (A.12, limitations): each form's evidence now adjusts its bar and excludes no reading (the owner's ruling, 2026-11-22). No lower bar was measured safe: a 1x1 bar of 2 on readings no reported call contains gained 7 generated 1x1 callers but added 7 misreadings on 2026-11-17 and 2 on 2017 (the generator draws 8.6% to 12.5% of its stations as 1x1 calls, the RBN spots 0.63% to 0.73%); a prefix-digit bar of 3 (2 signed) brought back L3I on the generated contest. So a C6AGU-like caller heard fewer than 4 times bare, or 3 signed, is still lost without a list; a recording with real callers of these forms heard bare would show whether a lower bar pays. A fragment of a call never reported (the generated contest's DL3I, read only as P L3I and L3I C) is not weighed. The join form has no rule: the one tried changed no call (decision log); a call glued to TU or a name (VE3NA TU, 5A3 JOHN) or to the over's K passes on the form's bar as before. Words of CW in running text count, by the ruling: watch for such junk in new recordings and report it to the owner rather than exclude it. **Gear words** (names of rigs, antennas and software with a call's form: G5RV, K3S, N1MM, K9AY): since the review of `calls-form-3` no fixed list (the owner's ruling, 2026-11-22: "No on keeping the list of calls for now"), so each is a call of its own shape; study them from the live decode-only corpus (`build\livelog\`) once it has weeks of text: which are read in conversation, how often, and in which frames, before any rule for them. Only the 5NN form (one or two letters before 5NN) stays a word of CW, measured.
- **Committed score tables of outside programs, rescored** (review of `calls-list-1`): tables scored before `calls_score.base_call` took the prefix-digit form (C6AGU, L3I) may count such calls differently if rescored now (a call refused then is a call now, and a CSV row outside the grammar is scored, not skipped); check them at the next rescoring.
- A posterior on a call in place of points. `call-2` built per-symbol margins in the segmental decoder (A.8e, "Margins"), off by default since review 10, and the stage's `known_margin_nats`, off too: on the held-out 2017 recordings it added more misreadings than confirmed calls. **Margins, next:** compare several runners-up per slot (with the best four, 6 sure readings of W3OA's 1 347 instead of 651; skip a runner-up whose parent holds the winner's text entry, to keep the CPU near +5%); fix the two word-space defects of the walk (a birth's zero-length space not matched to the same space; a substitution of the next letter stepping only the winner); send new characters as characters, not inside confidence corrections (the bench's immediate text misses them); have the stage read a word's margin at its least, after later falls; then re-measure on the 2017 recordings with a held-out check, and on the generated ones.
- **A known-call list by default** (call-2, 2026-11-04): with the calls of CW Skimmer's own MASTER.DTA of 2018 (taken out of the binary file as text) our misreadings on the 2017 recordings fall by about half against no list, 45 to 26 tuned and 51 to 25 held out, and the confirmed calls rise, 97 to 102 and 108 to 117 (decision log). No list ships: MASTER.SCP is published by supercheckpartial.com and the terms of redistributing it, or of fetching it at first start, are the owner's to decide. Until then `--known-calls PATH`.
- **Sure misreadings of a word named once:** K8VC for K8VJ and W6UM for W1UM on W3OA read with no rival within 16 nats, so a margin cannot hold them back; whether the running station sent them so (its own bust) or the decoder misread them surely is not known (conjecture: some are busts, which CW Skimmer avoids by not spotting a call on one word). Since `rob-1` named words do not pass alone (`points_named` 2), so these are off the list without one; the cost is the callers whose own signal is never read (on the four 2017 recordings 18 confirmed calls and 36 spotted elsewhere that day; the contest 11 of 221 true calls). A test that told a caller's named word from a garble would win them back: none found (margins, a pattern table, 1.5 points all tried or judged not to separate).
- **Read once where CW Skimmer read twice (`rob-1`'s diagnosis of W3OA):** of the 13 calls CW Skimmer lists that the RBN confirms and `main` did not, 5 were one transmission (K5LN beside the fragment 5LN, N4KGL, NZ9Z, VE3KP, KQ2M), 5 were never read whole (N0AIS as N0AI and 0AIS, N4YDU, W2DX, W5TMT, K5SBR: weak or garbled text), W0OPW twice as joined letters (1.5 points), K9BGL glued to itself (fixed: `glued_pieces_timed`), W5FM an RBN bust of W5FMH (a judge artifact). The decoder's second reading is the fix; a fragment beside a whole call read as a repeat (`partial_points` 1) gave 2 confirmed calls for 7 misreadings and was dropped; `joined_points` 1 gave 4 for 16 unconfirmed.
- **Judge artifacts (`rob-1`):** the RBN confirms W5FM (W3OA, 1 skimmer, a bust of W5FMH, which we list), W6SE (N2GZ, 1 skimmer, "W6S SEQP" run together; W6S is a 1x1 call sending SEQP), TK4TZ (the owner's file, a bust of K4TZ); each counts as a call CW Skimmer confirms and we do not.
- **Non-Morse signals read as calls:** near KC2LRC's center (14.083 to 14.089 MHz, the digital segment) RTTY-like keying reads as EILILIL and gives SD5S, MS5WW, N7RL, SS7SSDH, T34BIEI on both KC2LRC recordings, each read twice or more. The detector's not-Morse test passes them; the callsign stage cannot tell them from a weak station.
- **The stage's cost at density (review 15 of `rob-1`):** pooled support loops over every candidate for each occurrence, so its cost grows with the band's population: about 0.081 ms per channel-second at about 118 channels against `main`'s 0.0075, linear in the channels, about 0.27 ms per channel-second extrapolated to 400 tracks, on the app's GUI thread. Negligible against the bar of 40 ms, but measure it on the owner's busy-band hour; an index of candidates by length and call area would cut the loop.
- **Band-wide evidence against 150 Hz rivalry (review 15):** points pool over the band but a variant holds a call back only within 150 Hz, so a misreading read the same way at places where its true call is not read passes (real30's K0HBN beside IK0HBN). Band-wide arbitration would have killed N5ZC (KC2LRC-2, real, 5 kHz from N5ZO). A rule between the two (a rival's support anywhere, weighed by its distance) is open.
- **No test of pooling with a known-call list** (review 15): the listed and unlisted thresholds with pooled points are measured on the 2017 recordings with MASTER.DTA, not tested in `callsign_test.cpp`.
- **A tie at distance 1 goes to the earlier call** (N2GZ: AA3D, read 3 times from 29 s, beats AA3B, which the RBN confirms with 23 skimmers, read 3 times from 129 s). Without a list nothing tells which is the station.
- **A judge that does not count real stations as misreadings** (review 11, checkpoint 3): the RBN judge of checkpoint 3's record calls a call a misreading when it is one edit from a call the RBN spotted that day, and 221 of the 323 calls the RBN spotted in the 2017 recordings' windows have such a neighbor, so a real station the RBN missed lands there about two times in three (20 of CW Skimmer's 53 "misreadings" are among our 96). A misreading only when its one-edit partner was heard on about the same frequency in the same recording (by either program or the RBN), or a person's reading of a sample, would make the precision ratio measurable.
- **Done in `ckpt-4` (2026-11-15): the judge committed** as `training/kz4ap_proto/rbn_judge.py`, with `training/tests/test_rbn_judge.py`; with checkpoint 4's `windows.csv` it reproduces every class, skimmer count and one-edit list of checkpoint 3's `calls.csv`. What is left: its windows start where checkpoint 3's scratch judge put them, at a row's first reading less its stream time (W3OA 18:00:00.05, KC2LRC-1 18:00:00.029 UTC), so a spot in the recording's first whole second is left out; the recordings' own start times would be cleaner, at the cost of moving a few classes against checkpoint 3.
- The spots of design section 3 (the telnet server) read this stage; re-spotting after 10 min is not done, the list has one row per call.

From `app-7`'s look at what it dropped (checkpoint 2's record):

- Spellings of one station: real 40 m's IK6QOP is read exactly once and shows as K6QOP and IK6QO; a word's stray-letter reading gives half a point to the shorter call and none back. Pooling a family of near calls, or giving a word's own reading its due first. Since `rob-1` pooling over places brings back K0HBN and K6QO on real30 (the decoder drops IK0HBN's I the same way at several places). Tried in `rob-1` and dropped: a transmission read two ways counted for the better reading only (`shadow_alternatives`): K0HBN off real30, but on the 2017 recordings 1 confirmed call fewer and 3 misreadings more, and OH2J false on H2.
- A word glued to DE that also has a call's form: the start of a call is now held as a variant (`prefix_held`), and H2's false calls are 1 of 80 (PA8PG).
- `variant_hz` 300 Hz: 3 false calls fewer on H2, 1 true fewer on the contest. Decide with CW Skimmer's list on the owner's hour.
- A real MASTER.SCP against the recordings: tried at `app-8` (`calls-rules.csv`): contest + 1 true + 1 false, real 40 m the same list. Its worth against CW Skimmer's repeats table is for the owner's hour. Measured in `call-2` on the 2017 recordings against CW Skimmer (decision log, 2026-11-04).
- `channel_follower.hpp:13` still gives the hold's 21.33 ms frame; on the segmental path it is 85.3 ms (review 11; the document is right).
- The list reader (review 10's re-review): its NUL-byte check has no test of its own (the test's binary sample is also refused for having no call); a UTF-8 byte-order mark costs the first call; a UTF-16 file is refused with MASTER.DTA's message. The app reads a list's count to stderr only; show it in the status bar.
- One reading with no list, after DE: `points_framed` 1 adds 20 true and 22 false on the contest, so not as is; on real 20 s it adds K3AV and KB8UGP, which read well. DE alone and not TEST might do.

From review 5, of `app-7` (2026-10-31); the review's full evidence is summarized in checkpoint 2's record:

- **A group of strong neighbors in the test set.** E2's wanted stations stand at 8 to 14 dB S500, so neither it nor any group can judge the leak floor, or a later fix, where it matters most. Add an E2-like group at +25 to +35 dB S500, offsets 60 to 300 Hz, neighbors -10 to +10 dB re wanted. Until then: `level-pairs.csv`.
- **Done in `det-1` (2026-11-04): the segmental path's detector reads its own spectrum of 5.86 Hz bins** (signal-processing.md, section 6; what is left: "The detector after det-1" below). As it stood: **Next for neighbors: a detector's spectrum finer than 23.4 Hz.** `fast-5` took E = T/4 at m = 3 (log, 2026-11-01), which leaves the oracle path's limit at 40 Hz and at +12 dB re wanted or more, and shows the detector's: a station 60 Hz from a neighbor at +10 dB re wanted or more gets no track (CER 1.000; oracle 0.000). `fft_size` is the channelizer's too, so this means a second analyzer, at some CPU, tried first on those cells, `w11`, `f11` and the busy-band hour. Review 7 tried the cheap rules and rejected them: a peak within +/-23 Hz and a channel distance of 35 Hz gain little and cost false calls or +25% of channel-seconds on real 40 m. If real recordings show neighbors matter less than weak text, the word-space model goes first (KB3RKL's false word space, ZL2AIM's). `w11` stands at 40 of 48 where CW Skimmer has 46.
- **Traced and fixed in `det-1` (2026-11-04): the DC guard** kept every station within 47 Hz of the center from a track; the DC blocker replaces it, and the cell reads 0.020. As it stood: **A station within a few Hz of the stream's center, beside a neighbor (not traced; found by `fast-5`):** the pairs' cell of 120 Hz, 0 dB re wanted reads CER 1.000 on the detector's path at every level, 833 symbols, with E = T/4 and without, and 0.000 on the oracle path. The neighbor gets a track and the wanted station, at -7.2 and -3.8 Hz in the two looked at, gets none. That the center is the cause is a conjecture: the other cells stand elsewhere in the band. It adds about 0.037 to every level of the pairs' detector's path.
- **A better bound in `StreamRunnerBudget.APipelineBehindALiveSourceGivesUpTracks`** (review 7, its S2): `EXPECT_LT(behind.track_budget, most)`, `most` what `run_to_end` already counts: 8 or 25 against 83.
- **40 Hz at or below the wanted station's level,** where the bank reads 0.15 to 0.39 at 0 dB re wanted against 0.72 to 0.86 here, from +15 dB S500; and +10 dB re wanted at 80 Hz, 0.74 before `fast-5` and 0.54 after over all levels.
- **A keyed neighbor at 30 Hz, 0 dB re wanted (0.976, the bank 0.363), and carriers (`fast-4`, 2026-11-01): stopped, no clear way within reasonable effort.** Forcing the narrow stream reads worse (1.444 at 30 Hz, 0.888 at 60 Hz, +40 dB re wanted), since a keyed neighbor's edges leak through the 36 ms filter's null where a carrier's do not; a 12 ms stream cannot read 20 to 30 WPM (its speeds stop at about 16 WPM), and {1, 6} loses 60 Hz at 0 dB re wanted (0.717); a switch at 60 nats loses KK7D on real 40 m and gains nothing here. What is left is a separate design: evidence from two tones resolved over a longer FFT, or a notch for steady carriers with a detector that does not sit on them. Real recordings show little of either; the busy-band hour should say.
- **The choice of stream loses about 8 dB against a carrier:** at some offsets the right text exists in the narrow stream and is not chosen (26, 30, 52, 60 Hz, 0 dB re wanted). On the detector's path none of these windows exists: the station is lost inside about 66 Hz of a carrier at its own level, since the track sits on the carrier. Real recordings show little of it, so it waits. Since `det-1` the station has its own track from 30 Hz: beside a carrier there first, 0 to 10 dB re wanted stronger, it reads 0.33 to 0.64 at 45 to 90 Hz (0.98 to 0.99 before), and inside 45 Hz 0.95 still.
- **Letter-digit calls** (D4C, E7DX, C6AGU, P3F, T6A, J6M, 92 of MASTER.SCP). **Done in `call-form-2` (2026-11-19):** read as the prefix-digit form, from a list only (`prefix_digit_listed_only` 1). Left open: as any call they added only junk on the three replay sets (E5E, T5E, A9E: 2026-11-17 misread 85 to 115), yet the RBN spots many real ones that MASTER.SCP lacks (C8K by 136 skimmers on 2026-11-18, E6DX, T2DE, A6BB), so without a list, or with MASTER.SCP alone, they are lost; a stronger test than 2 points for this form (CW Skimmer's 5 repeats for an unusual pattern) is the next thing to try. **Since `calls-list-1`** these forms pass unlisted on a points bar (4, 3 signed; the `calls-list-1` item above).
- **Rows that hang on one transient reading:** K6QOP on real 40 m, whose word is corrected away 10.30 s after its end, and `settle_s` 10.5 to 14 s loses it; E51MWA's one exact word in the final text.
- **Damaged calls a person votes on letter by letter** (IV3TMV, IT9YBL, K5UR, HK3QQ on real 40 m): the one attempt, near variants adding to a call's points, lists far more junk than stations (24 to 76 rows).
- **A correction near `history_s`:** the final decoder's longest on real 40 m is 199.2 s against 200 s (205.8 s at the 1 s hold).
- **The list never ages out;** CW Skimmer's deletes a call after 10 min of inactivity.
- **"Last heard" is clipped** at some display scales (`app/src/main_window.cpp`, 75 px).
- **Fixed 2026-11-15 (`ci-1`): a fresh `.venv` failed 1 test,** `training/tests/test_keying.py:157`, `pytest.approx` on nested tuples under pytest 9.2.0; the pairs are now compared flattened. It had stopped the automated builds on GitHub before the C++ steps.
- **A correction that shortens text below `TrackText::base`** would misnumber symbols in `CallSearch`; it never happens on the recordings measured.
- **The model of the call list** has no unit test in `app/tests`.
- **The same check on the owner's hour:** the Reverse Beacon Network's day file against what we list, as checkpoint 2's record does for `real30` (`rbn-check.csv`): other antennas, CQ spots only, a rough figure, not the bar. Done for `real30`; the product never matches calls against it.

From review 6, of `app-8` (2026-10-31):

- **KF7WX, a known real miss:** 10 North American skimmers spot it at 7034.9 kHz during `real30`; our text reads KFQWX, a 7 taken for Q on a weak station (95th percentile 7.4 dB, 99th 10.5 dB re a floor 170 to 330 Hz away, per 23.4 Hz bin). The decoder's to fix: digits against letters, weak stations. `fast-4` looked: the track reads at 5.7 WPM with one word, `ETFQWX`, so it is more than one digit; a 7's last dots filling to a dash is the mechanism of the Q (dot, gap, dot is 3 dots long). Nothing cheap was found.
- **X with the short hold:** at a hold of 1 s, X 0 dB gave the best pairs (0.063 to 0.128 at +10 to +40 dB S500); X 0 dB at 0.2 s is not swept and might beat both. Look at seed 2 as well: the hold was chosen looking at E2 and the pairs.
- **The floor's two systematic mismatches** (A.8e): inside about 60 Hz it stands under the true leak; near a filter's null, over it, since it reads the response at each bin and not at the tone's frequency. A fix would read the bins' peak and take the response there.
- **CW Skimmer's comparison, pending a record:** CW Skimmer 2.1 has been replayed on the recordings (choices: `docs/decision-log.md`, 2026-11-01; reader: `calls_score`). Its output and the comparison are not committed; they wait for a results record.
- **The GUI thread's cost:** at checkpoint 2 about 100 ms per s of wall on the real display, 50 ms offscreen. Since `fast-4` (12.5 pictures/s, cached chrome, only changed cells) 11.2 ms per s of wall offscreen, from 62.7, and the whole process 9.9 ms per channel-second, from 19.3, by cycles over the 30 min recording in real time (the desktop, other programs running). **On the real display (review 7, by cycles, other programs holding 13% to 24% of all logical processors before each run):** over the 30 min recording 22.88 to 11.80 ms per channel-second, GUI thread 109.9 to 25.2 ms per s of wall; a window of 3458 x 1398 pixels, the first 150 s, 24.35 to 12.05 ms per channel-second, GUI thread 159.5 to 32.6 ms per s of wall; the contest at real time 8.58 to 4.21 ms per channel-second. Not tested: a quiet desktop, 4K.
- **Stations spotted elsewhere and not listed:** 23 of the 33 calls North American skimmers spotted in `real30`; by level most were near the noise here (conjecture from level alone). The busy-band hour should say whether any are the decoder's.
- **A build in a folder with a root of about 200 characters fails** (paths over 260 characters, MSB6003). Not a concern in a short folder.

## The detector after det-1 (2026-11-04)

`det-1` gave the segmental path's detector its own spectrum of 5.86 Hz bins, a prominence and a keying-sideband test, 30 Hz between tracks, a DC blocker in place of the DC guard and a second chance at a retired place (signal-processing.md, section 6 and A.6; decision log). Left open:

- **Junk tracks on real bands.** Against `main`, real30's tracks 1 159 to 2 225 and channel-seconds 21 671 to 34 249; live2's 241 to 473 and 2 758 to 5 363: weak signals and spurs the finer bins see, and the retired place's second chance; the judge retires them. CPU per channel-second is about the same, CPU per second of stream rises by about half to double. A cheaper first look at an unproven track (a pre-screen, or a shorter first verdict) would pay; measure on the owner's busy-band hour, where the busy case is.
- **The second analyzer's CPU,** about +7 ms per s of stream (real30, Linux): a hop of a whole FFT instead of half, or the floor less often, measured against births.
- **Stations closer than 30 Hz share a channel.** On the contest 42 of the 60 missed stations have a track within 20 Hz of them; most share their frequency with another station or read their call glued or once (the callsign stage's). 10 never get a channel: 8 fail the peak or prominence test beside a neighbor 20 to 30 Hz away, 2 lie within D of an older track. Below about 30 Hz a detector cannot help; the decoder reads 20 to 45 Hz badly beside an equal or stronger neighbor even on the oracle path (det-1's cells, 0 to 20 dB re wanted: 0.75 to 0.97).
- **60 Hz beside a neighbor 20 dB or more stronger:** the neighbor's skirt fills the valley and the station fails the prominence test (stress cell 1.07). A skirt model of a keyed neighbor (its expected sideband level by offset) in place of the valley would reach further.
- **Chirp** breaks the sideband test's symmetry; the generator has none. Look on the owner's hour beside strong stations.
- **A row's support across a track's split:** real30's E51MWA, read whole twice on `main`, is read twice by `det-1` too but the second reading lost a dot; its first track retired quiet while it listened and the next began about 2 s later. Listing a call read once in a framed position in a pile-up would need the callsign stage's rule, not the detector.
- **E2's remaining losses** are on channels: 11 of 180 signals read badly (the oracle path as bad on most), 5 retired early as not Morse, 4 born late.

## Copies after det-2 (2026-11-15)

`det-2` retires a track whose keying is an older, proven neighbor's within 150 Hz (signal-processing.md, section 6 and A.6;
decision log). Left open:

- **The runner's track still prints its callers.** Only an older track's copy is retired, since a runner's track retired during a
  louder caller's over found no new track beside the caller's lingering peak (W8CAR on the owner's file). A runner's track that
  prints a caller's over is a duplicate the other way. A test of which station a channel's text belongs to over the over, not the
  window, would cover it. **Status (2026-11-17, `copy-2`):** symbol by symbol, a caller's over on the runner's row is now given to
  the caller's track, and left out of the runner's row where the caller's row holds it (signal-processing.md A.6, "A neighbor's text").
- **A copy prints its first 6 s** (the window) and its replay's 2 s before it retires. A shorter window raises the spread of rho for
  independent stations as 1/sqrt(N); not tried (4 and 8 s were queued and dropped for time). **Status (2026-11-17, `copy-2`):** what
  it prints of its source is now mostly given to the source and left out of its row; its track still lives the 6 s.
- **A copy is a second reading.** On KC2LRC-2 a copy 48 Hz from N4BP was the only track to read N4BP's "KI4MZC 5NN"; KI4MZC
  (spotted by the RBN that day) is no longer listed. Keeping a copy's text for the callsign stage while hiding its row would keep
  such readings, at the risk of the false support duplicates gave (K3QP and WW5TOM on the owner's file came from duplicates' readings).
  **Status (2026-11-17, `copy-2`):** for tracks that are not retired, a neighbor's stretch that the neighbor's own track reads
  otherwise is kept, on the row and for the callsign stage (UA8B on the strong pairs' fresh seed); a retired copy's text is as before.
- **Weak copies near the noise** (a few dB SNR at the copy's filter) can keep their tracks: own power is uncertain there. In the
  ghost cells 2 of 60 QSOs kept more than 10% of the caller's over.
- **A copy whose place is still a peak** opens again 0.5 s after it retires and is judged again 6 s later; a hold tied to the source's
  keying would end that, at the risk of blocking a QSO partner.
- **The test set has no group of copies.** The ghost cells (A.11) are scratch; a group like them in DEV2 would let later changes see
  the duplicates.
- **A hidden row's own text is dropped on a copy's end** (review 14): `TrackTable` drops a row not yet shown when its track ends as
  `Copy`, as a merged one's. Many copy retirements are real callers' tracks that read their own call first and then their runner
  (the owner's file: 577 "N4ZZ DON", 644 "NU4J", 600 "W4CMG", 570 "W4SSF", 536 "KK4E"). Display only: the call list reads every
  track's text. Keeping the row, as for a faded track, would show them. **Status (2026-11-17): fixed on `copy-2`:** a hidden row that
  ends as a copy waits as a faded one when it holds text not heard from a neighbor, and its neighbor's text is left out of it.
- **No test holds the source-proven condition alone** (review 14): det-1's carrier cell is its evidence (A.6), but a unit scene of a
  station between a carrier's track and a side track did not reproduce the side track (tried, 2026-11-15).
- **A caller's row beside the runner prints the runner's exchange too** (checkpoint 4): on the owner's file the app's track at
  +9 885 Hz reads KK4E, K8WWS, VE3NE and "TU K4TZ CQ", since its ±150 Hz channel hears K4TZ 42 Hz away: one QSO's text on two rows,
  not a copy by the test. The owner may read it as a copy. **Status (2026-11-17): fixed on `copy-2`:** the row reads "D KK4E KK4E DON
  3654 K VE3NE 3 LALI 128" (the callers' own), K4TZ's "TOM 1993" and "TU K4TZ CQ" left out; over the five real recordings the text
  shown twice on pairs of proven tracks falls 68% to 76%, every reported call the same (decision log, 2026-11-17). That counts exact
  twins only: symbols given to a proven neighbor and still shown, the neighbor's keying read otherwise on the weaker row, remain (301
  of 827 on the owner's file; 1 044 of 1 459, 333 of 736, 288 of 517, 819 of 1 457 on the four 2017 recordings), and the owner may
  still see them as a neighbor's text.
- **A long row's speed reads low at its end** (review 16, checkpoint 4): in the app's track table at the end of the owner's file
  K4TZ's row shows 5.7 WPM, and two other long rows 6.9 and 7.6 WPM. GUIDE.txt tells the owner it is being looked at.
  **Status (2026-11-17): fixed on `main` (merge 2b0efde, review 17).** The speed sent was the best story's at the last publish, here a
  slow story's reading a burst at 222.5 s as E; the decoder now sends its text's speed, the median over its last 16 letters
  (decision log, 2026-11-17; `signal-processing.md` A.9b, "Speed sent"). The three rows read 30.8, 33.5 and 31.7 WPM, the call
  list unchanged. The owner's trial folder was updated with it.

## A neighbor's text after copy-2 (2026-11-17)

`copy-2` gives a symbol that is a neighbor's keying, heard through the channel's skirt, to the neighbor's track
(`DecodedSymbol::heard_from`; signal-processing.md, section 6 and A.6, "A neighbor's text"); a stretch of them leaves the row where the
neighbor's own listed row holds every symbol of it. The callsign stage reads every symbol (its skip is a variant, off: review 18).
Left open:

- **Words split.** The test is per symbol, so a word can be half given (K4TZ's row on the owner's file: "4" and "VE" of the callers'
  KK4E and VE3NE stay). A word-level vote would hide or show whole words, but a word glued across an over's end (KK4ETUK4TZ) needs the
  split.
- **Read two ways.** A stretch the neighbor's row reads otherwise stays on both rows, by design (a second reading can be the better);
  a junk row on a wide signal and its neighbors read one keying two ways often.
- **A correction after the source closed** is not attributed again, so what it re-sends shows on the row.
- **The benchmark does not use it.** The test set's scores read every symbol; a scorer of shown text on H2 (QSO, two tracks) and E2
  would measure what the rows show.
- **No unit scene of a weak track reading better than its loud neighbor** (UA8B's case): the stage's and the list's unit tests hold the
  rule, the strong pairs' fresh seed the evidence.
- **A neighbor's keying read otherwise** stays on the weaker row (the residual above, about a third to two thirds of what is given to
  a proven neighbor): a rule by keying alone, not text, would hide it, at the risk of hiding the better reading (UA8B).
- **The stage's skip** (`skip_neighbors_text` 1) moved calls' places and frames without gaining one; a version that moves a skipped
  reading to the neighbor's place with its own track's frame would be needed before it could be on.
- **The loudest owner** went to a nearer copy, not the runner's own track, in a unit scene of a runner and callers 30 and 60 Hz
  above (review 18; not traced).