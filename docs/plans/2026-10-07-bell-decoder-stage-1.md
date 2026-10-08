# Bell 1977 decoder, stage 1: Bell as published

> **Status: draft for the owner's approval, 2026-10-07. No code before approval.**
>
> **Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.2 step 3, amendment "Bell 1977 instead of
> manta" (owner, 2026-10-07). **Evidence:** `docs/research/bell-1977-notes.md` (the thesis as read for the
> decoder survey), `docs/research/2026-10-07-decoder-chain-audit.md` (why the bank's back end is replaced),
> `docs/research/2026-10-07-manta-hsmm-study.md` (why not manta).
>
> **Branch:** `bell-stage-1` (decision D8).

## 0. Terms

- **Bell's thesis:** E. L. Bell, *Optimal Bayesian Estimation of the State of a Probabilistically Mapped
  Memory-Conditional Markov Process with Application to Manual Morse Decoding*, Doctor of Engineering thesis,
  Naval Postgraduate School, 1977. "p. N" is the printed page; in the local scan, printed page N is PDF page
  2N + 3.
- **Bell's program:** the Fortran listing in the thesis's "Computer programs" appendix, printed pp. 159–191
  (33 pages). It holds the main program, his signal simulator, his receiver chain, the decoder and its
  displays. It ran on a DEC PDP-10.
- **Path:** Bell's word for one hypothesis. A path is a sequence of states (element, letter-tree node, speed,
  time spent in the element), with its own Kalman estimate of the signal amplitude and its own posterior
  probability. The decoder keeps at most 25 paths.
- **Receiver chain:** Bell's subroutines RCVR (a quadrature down-converter with a low-pass filter, then back
  up to 1000 Hz), BPFDET (band-pass filter, envelope detector, low-pass filter) and NOISE (the noise-floor
  estimate). It is not called a "front end", which in this project means the signal detector.
- **S₁₀₀:** Bell's SNR: key-down carrier power over the noise power in 100 Hz, dB. For white noise,
  S₅₀₀ = S₁₀₀ − 6.99 dB (derived: 10·log₁₀ 5).
- **Letter error:** Bell's measure, the percentage of wrong letters among about 200 sent. Its counterpart here
  is the **character CER** of `docs/signal-processing.md` A.11 (character edits over reference symbols that
  are not spaces). Task 1 records how Bell counted.
- **Keyboard Morse:** exact timing, our "machine" style. **Hand-keyed:** his simulated fists, "good", "fair"
  and "poor".
- **V1–V5:** the five verification steps of §5.

## 1. What stage 1 delivers, and what it does not

It delivers four things:

1. Bell's program in C++, each subroutine translated from the listing. His receiver chain is fed from our
   channel stream. The decoder sits behind the engine's `Decoder` interface, selectable as `--decoder bell`,
   and is not the default.
2. Evidence that it is Bell's decoder: V1–V5 (§5).
3. Its measurement on the development set, against the frozen bank decoder (§6).
4. A proposed order for stage 2, for the owner (§7).

It does not include any modernization: the bank's matched likelihoods, the 8–80 WPM range, merging, a text
model or Farnsworth timing. It does not tune his constants to our data either. A changed constant is a
stage-2 step.

## 2. What a first look at the listing found (2026-10-07)

These findings were read today from the page images and the scan's text layer. Task 1's transcription
confirms or corrects each.

- **The listing is legible.** At 170 dpi the page images read cleanly (checked on p. 189). The scan's text
  layer garbles code, so the transcription must read the page images.
- **The program is complete except for two data files.**
  - The listing has the main program and these subroutines:
    - INITL and INPUTL: tables and settings;
    - SIMSG and KEY: the simulator;
    - RCVR, BPFDET and NOISE: the receiver chain;
    - PROCES, with TRPROB, XTRANS, PTRANS, SPDTR, PATH, LIKHD, KALFIL, MODEL, PROBP, SPROB, SAVEP, TRELIS and
      TRANSL: the decoder;
    - STATS, AUTOCR and DISPLA: displays.
  - The main loop calls SIMSG, RCVR and BPFDET for every 8 kHz sample, and NOISE and PROCES for every 40th
    sample, i.e. at 200 samples/s (p. 159).
- **Two data files are not printed: `MORSE` and `TEXT`.**
  - INITL reads `MORSE`: 400 rows of 8 integers. For each letter-tree state, a row gives the letter
    (LTRMAP), the element state (IELMST) and the successor state for each of the 6 element types (MEMFCN)
    (pp. 160–161). Only the first 16 rows are in DATA statements.
  - The simulator reads its text from `TEXT` (p. 162).
  - So the letter tree must be **reconstructed**, from two sources: the Morse code of Bell's alphabet, and
    how PTRANS, PATH and TRELIS use the table. The alphabet is IALPH in INITL: letters, digits, some
    punctuation and prosigns such as AR, SK and BT (read from the text layer; to be confirmed).
  - The reconstruction is an inference. It is labeled so and tested (V1).
  - The text is replaced by random letters as the thesis describes them. KEY also seems to have a
    random-letter branch (p. 164, text layer).
- **The simulator** (pp. 163–165):
  - Element durations are Gaussian, with mean ESEP·T and standard deviation EDEV·T, where T is the dot length
    and ESEP is 1, 3, 1, 3, 7 or 14. Durations are floored, at 20 ms according to the text layer; the notes
    say 16 ms, and the transcription will tell.
  - The amplitude takes a random step at key transitions. It adds a Gauss–Markov fading term, with a factor
    GAMMA per 8 kHz sample, and is floored at 0.001.
  - The phase takes random steps, and a chirp decays after each key-down.
  - White Gaussian noise of variance RNOISE is added at each 8 kHz sample.
  - The settings are typed in at run time with TYPE and ACCEPT statements (p. 160). So the values behind
    each published table must come from the thesis text, and any value it does not give is a gap.
- **The receiver chain may differ from the thesis text.**
  - RCVR low-passes its quadrature components with y ← y + 0.070·(x − y) per 8 kHz sample. That puts the 3 dB
    point 92 Hz from the carrier (derived: cos ω = 0.99737 at half power).
  - BPFDET holds two resonator coefficient sets, (1.37158, 0.9409, gain 0.0150) and (1.2726, 0.8100, gain
    0.1900), and a 3-pole low-pass (p. 189).
    - Their poles have radius 0.97 and 0.90, both at 1000 Hz (derived: r = √0.9409, √0.81; cos θ = 0.7071, so
      θ = π/4 at 8 kHz).
    - Their 3 dB widths are about 76 Hz and 250 Hz (derived: (1 − r)·f_s/π).
  - The thesis text instead describes a 500 Hz single-pole filter, then a 100 Hz filter of two resonators.
  - Task 1 settles which coefficients are in the signal path, and so the chain's actual bandwidths.
- **Known differences between the text and the code** (notes §2.2, §3.1, §3.2):

  | Item | Text | Code |
  |---|---|---|
  | Duration constant α | 3.61, 1.81 and 0.90 per baud | 3.0, 1.5 and 1.0 |
  | Φ on marks | 0.97 | 1 |
  | Amplitude process variance Q when a mark begins | rules | a smooth formula in the length of the space before it |
  | Noise window | 240 samples | buffers of 200 and 50 |

## 3. Decisions for the owner

**D1. The listing is the authority.**
- The listing is the program that produced the published results.
- The text decides only where the listing is unreadable or silent.
- Every difference is recorded in the reference sheet (Task 1).
- *Recommended.*

**D2. A running reference: Bell's own program, compiled.** The port needs the transcription anyway.
Compiling it with gfortran, a free Fortran compiler, gives what Plan A had in the Python prototype: a running
reference. The changes are limited to input and output, the random-number source and the missing `MORSE` file,
and each is marked in the code.
- It makes possible three checks the C++ alone cannot give:
  - **V2:** his program, with his simulator, against his tables. That separates "the transcription is wrong"
    from "our conditions differ from his".
  - **V3:** the C++ against his program on identical inputs, path by path.
  - **Golden data** for the C++ unit tests.
- Cost:
  - the transcription made to compile. It is DEC-10 Fortran: TYPE/ACCEPT, Hollerith constants and RAN, which
    gfortran accepts with its legacy options;
  - gfortran installed on the Linux machine, inside the repository's `build/` folder: micromamba and
    conda-forge's gfortran, about 0.5 GB. Nothing goes outside the repository folder, and no administrator
    rights are needed.
- Options:
  - **(a)** Compile it. *Recommended.*
  - **(b)** Do not. The C++ is then verified only by unit tests and by reproducing his tables (V4, V5). If V5
    fails, the cause cannot be located among the transcription, the port and our conditions.

**D3. Signal level on real channels.**
- Bell's constants assume a key-down amplitude of about 1 at the input of the receiver chain:
  - the Kalman filter starts at 0.5, with variance 0.1;
  - the process variances are absolute, in that amplitude unit: 10⁻⁴ per 5 ms on marks, up to about 0.25
    after long spaces (notes §3.1).
- His simulator delivers amplitude 1 by construction. His field recordings went through a receiver with a
  fast-attack AGC of about 200 ms decay (p. 141). Our channel's level is whatever the station's is, in FS.
- Options for the development-set measurement:
  - **(A) An oracle gain** from the label, making the key-down amplitude 1 as in his simulation. It is not
    deployable; it measures the decoder under his conditions.
  - **(B) An AGC emulating his field receiver** on our channel. It is deployable. The "about 200 ms" is his;
    the rest is our emulation, heuristic: a peak follower on the channel envelope, with instantaneous attack,
    exponential decay with a time constant of 200 ms, and gain = 1/peak.
- *Recommended: run both.*
  - (B) is stage 1's deployable result and is compared with the bank.
  - The gap between (A) and (B) says whether level handling must come early in stage 2.
  - The reproduction of his tables (V2, V5) uses his simulator's scale, which is (A) by definition.

**D4. The reproduction suite uses Bell's fixed conditions,** an exception to the jittered-grid rule
(CLAUDE.md, 2026-10-06). To compare with a published table, the speeds, S₁₀₀ values and fists must be his:
20, 30 and 50 WPM, and S₁₀₀ from 3 to 12 dB. The development-set measurement keeps the jittered grid. *Needs the
owner's approval.*

**D5. The decoder is C++ only (owner); Bell's conditions are added to our generator in Python.**
- The generator (`training/kz4ap_synth`) and the scoring and analysis tooling are Python.
- Reproducing his tables with our generator needs three of his models there: his fists, his amplitude and
  fading process, and his random text.
- Writing them in C++ would mean a second generator.
- *Recommended: Python, generator only.*

**D6. `docs/signal-processing.md` needs room.** Its body is at 5 989 of its 6 000 words, and a body section for
Bell needs about 900.
- *Recommended:* the frozen bank's body section (§8c, 2 656 words) shrinks to about 600 words: how the bank
  works and where it stands. Its full description is already in appendix A.8c, which stays as it is.
- Alternative: Bell's section in the appendix only, with a paragraph in the body.

**D7. Provenance note.** The reference program's README cites the thesis. Stating that the thesis is a
public-domain U.S. Government work would be a legal statement, which the owner's rules reserve to him.
*Include that sentence or not?*

**D8. Branch.** `bell-stage-1` from `milestone-2c-stage2`, after the cleanup commit. If the owner merges
`milestone-2c-stage2` into `main` first, the branch starts from `main`.

## 4. Design

### 4.1 Data flow in the engine

Per channel, in order:

1. **Mix down** by the frequency anchor, phase-continuous, as `BankDecoder` does, so the station is at 0 Hz.
   The channel is complex baseband at 1500 samples/s, in FS.
2. **Convert to Bell's input.**
   - Linear interpolation to 8000 samples/s: output n at t_n = n/8000 s, which is input position 3n/16.
   - The real signal x_n = Re{c·u(t_n)·e^{j2π f_c n/8000}}. Here f_c is his simulated carrier (his FREQ
     setting, from Task 1; 1000 Hz if not given) and c is the level (D3).
   - Derived properties, each tested in Task 4:
     - **Droop.** Linear interpolation attenuates by sinc²(f/1500 Hz): 0.03 dB at 50 Hz from the carrier.
     - **Images.** Images of a component at the channel's edge (150 Hz) fall 1350 Hz from the carrier.
       Linear interpolation attenuates them by 38.5 dB there. RCVR's low-pass and the resonators then bring
       them to about −80 dB or less at BPFDET's output.
     - **Noise beyond ±150 Hz.** Our channel is band-limited to ±150 Hz, whereas Bell's simulated noise was
       white to 4 kHz. At BPFDET's output, the noise his chain would have passed from beyond ±150 Hz is
       about −20 to −28 dB relative, depending on which resonators are in the path. So the noise level
       differs by less than 0.1 dB (a derived estimate, to be confirmed in Task 4).
3. **RCVR and BPFDET**, exactly as the listing, with his coefficients at 8 kHz.
4. **Keep every 40th sample** of BPFDET's output: 200 samples/s, τ = 5 ms exactly. That is his rate, so no
   constant needs rescaling.
5. **NOISE:** the noise-floor mean μ_n and R = 2μ_n², per the listing; then z_k = envelope − μ_n.
6. **PROCES**, once per sample, in this order:
   - transition probabilities (TRPROB, XTRANS, PTRANS, SPDTR);
   - path labels (PATH);
   - the Kalman step and the likelihood (LIKHD, KALFIL, MODEL);
   - posteriors (PROBP);
   - marginals (SPROB);
   - pruning (SAVEP);
   - the decision (TRELIS);
   - letters (TRANSL).
7. **Output.**
   - Each letter is published when TRELIS decides it, about 1 s (200 samples) after the letter ends. It is
     final: there are no corrections.
   - `DecodedSymbol.probability` is the posterior mass, at the decision, of the saved paths that descend from
     the decided node. This one is ours, derived from his posteriors; his program prints no confidence.
   - `DecodeUpdate.wpm` is SPROB's conditional-mean speed, as in his program.
   - A word space or a pause becomes " ".

**Cost** (estimated here, measured in Task 6):
- **Decoder:** at most 25 paths × 30 successors per sample. With the 8–16 paths Bell saved on average, that is
  about 250 Kalman steps per sample, or 5·10⁴ per channel-second.
- **Receiver chain:** about 0.3 Mflop/s at 8 kHz.
- **Expected:** a few ms of CPU per channel-second. For comparison, the bank takes 42 ms on the Linux machine.

### 4.2 The decoder as known before the transcription

These values are from the code, per the notes. Task 1 confirms each one against the transcription.

| Component | Value (code) | Notes |
|---|---|---|
| Speeds | integers, 10–60 WPM; transitions that leave the range are zeroed | §2.2 |
| Start | 25 paths: 5 each at 10, 20, 30, 40 and 50 WPM | §2.2 |
| Duration model | hazard form of a Laplacian, α = 3.0, 1.5 and 1.0 per baud (dot, dash and the spaces up to c-sp; w-sp; pause) | §2.2 |
| Speed changes | only when the element changes; increments and probabilities from Table X, by the element just completed | §2.2 |
| Successors | 30 per path (6 elements × 5 speed steps); those with probability ≤ 10⁻⁴ skipped | §4 |
| Kalman filter | scalar amplitude; ŷ₀ = 0.5, P₀ = 0.1; ŷ floored at 0.01 | §3.1 |
| Amplitude model | marks Φ = 1, Q = 10⁻⁴; a mark after a space: Q = 0.15·e^{0.6(B−14)} + 0.01·B·e^{0.2(1−B)}, B the space in bauds, capped at 14; key-up Φ = 10^(−2/(22.4·Δ_ms)), Q = 0 | §3.1 |
| Likelihood | P_z^(−1/2)·exp(−z̃²/(2P_z)); 0 when the exponent exceeds 1000 | §3.2 |
| Pruning | the best path per element if its probability is ≥ 10⁻⁶; then by decreasing probability until 0.90 is reached, at most 25 paths in all; at least 7 | §4 |
| Decision | a node all saved paths share; otherwise the best path's node 200 samples (1 s) back, and the paths not descending from it are deleted | §4 |
| Text | independent letters, equally likely | §2.2 |

### 4.3 C++ structure

- **Headers in `engine/include/kz4ap/bell/`** and sources in `engine/src/bell/`, one C++ function per Fortran
  subroutine.
  - Each function is named after its subroutine, in lower case (`xtrans`, `ptrans`, `spdtr`, `model`,
    `kalfil`, …).
  - Arguments keep the same meaning and the same order.
- **Bell's integer codes stay as he numbered them,** 1-based: elements 1–6, tree states 1–400, speeds in WPM.
  Only container indexing converts, in one helper. *Ruling:* converting every table to 0-based would be the
  likeliest source of translation errors.
- **Double precision throughout.** The PDP-10's single precision had a 27-bit mantissa. The Fortran reference
  is compiled in double precision as well (`-fdefault-real-8`), so V3 compares like with like.
- **Configuration.** One `BellConfig` holds every constant of the listing, with its unit and source line.
  Where a constant has a physical meaning, it carries it, e.g. `decision_delay_s = 1.0` (200 samples at
  5 ms). Per-step variances are stated per 5 ms step.
- **Engine.** The `BellDecoder` in `engine/src/bell_decoder.cpp` wraps it for the engine, as `BankDecoder`
  wraps the bank.

## 5. Verification, written before any run

| Step | What it checks | How | Pass |
|---|---|---|---|
| **V1** | the transcription and the reconstructed tree | two independent readings of every page; each difference settled on the page image; every symbol of his alphabet decoded from its noiseless element sequence (Fortran and C++) | no unresolved difference; every symbol decodes |
| **V2** | his program reproduces his tables | his simulator and decoder, compiled (D2); the 30 target cells of §5.1, 20 runs of 200 letters each | the rule of §5.2 |
| **V3** | the C++ is his program | the golden inputs (Task 3) through both | identical letters; at every sample the same set of saved paths (element, speed, letter state, duration); probabilities, ŷ and P within 10⁻⁹ relative; any difference traced to a near-tie at SAVEP, with the values |
| **V4** | his formulas and tables | unit tests computing each formula independently of the implementation (closed forms from the reference sheet), Table X, the MODEL formula, the pruning and decision rules on constructed path sets | all pass |
| **V5** | our generator and channel at his conditions | the reproduction suite (Task 7) through the oracle channel and the C++ decoder, level (A) | the rule of §5.2 against his tables, and against V2 cell by cell |

### 5.1 The reproduction targets

The independent-letter results of Table XV (keyboard Morse, p. 132) and Table XVI (hand-keyed, p. 134).
Bell's settings for both: P_opt 0.9, a 1 s delay, his fading channel and white Gaussian noise. Each value is
the letter error, in %, of one experiment of about 200 letters.

| Table | Speed | Sender | S₁₀₀ 12 dB | 9 dB | 8 dB | 7 dB | 6 dB | 4 dB | 3 dB |
|---|---|---|---|---|---|---|---|---|---|
| XV | 50 WPM | keyboard | 0 | 3 | 5 | 16 | 35 | | |
| XV | 20 WPM | keyboard | | 0 | | | 3 | 6 | 31 |
| XVI | 30 WPM | good | | 3 | | | 5 | 36 | – |
| XVI | 30 WPM | fair | | 5 | | | 7 | 42 | – |
| XVI | 30 WPM | poor | | 12 | | | 13 | 46 | – |
| XVI | 20 WPM | good | | 1 | | | 4 | 6 | 31 |
| XVI | 20 WPM | fair | | 4 | | | 6 | 8 | 34 |
| XVI | 20 WPM | poor | | 11 | | | 13 | 14 | 38 |

That is 30 cells; the three "–" cells were not reported by Bell.

### 5.2 The reproduction rule (V2 and V5)

**Our estimate per cell.** For each cell c, we have 20 runs of 200 letters, each with a new seed.
- p̂_c is the pooled character CER: character edits summed over the runs, divided by reference characters
  summed over the runs.
- s_c² is the variance of the 20 per-run CERs.

**Is Bell's single experiment consistent with us?** We model it as one draw of 200 letters from a beta-binomial
with mean p̂_c, its overdispersion fitted to s_c² by moments, or a plain binomial when s_c² does not exceed
the binomial variance.
- His value is **consistent** if it lies in the central 90% of that distribution.
- For a reported 0, consistent means P(0 errors) ≥ 0.05.

**PASS requires both:**
- (i) at most 5 of the 30 cells are inconsistent;
- (ii) a two-sided sign test gives p ≥ 0.05. The test counts the cells where Bell's value is above our p̂_c
  against those where it is below, leaving out cells where both are 0.

**V5 against V2,** cell by cell: z = (p̂_V5 − p̂_V2)/√(s²_V5/20 + s²_V2/20). PASS if at most 5 of the 30 cells
have |z| > 1.645 and the same sign test gives p ≥ 0.05.

**Classes.**
- The statistics are derived.
- The 90% level, the 0.05 levels and the threshold of 5 cells are heuristic choices, made now. If every
  cell's interval were exact, 6 or more inconsistent cells out of 30 would occur with probability about
  0.07 (derived: Binomial(30, 0.10)). That is the false-failure risk of criterion (i).

**Also reported, not part of the rule:** each cell's difference, and the S₁₀₀ shift that best aligns our curve
with his.

### 5.3 What may change when a check fails

Allowed, each recorded in the results record with its evidence:
- correcting a transcription error, as the page image decides;
- correcting our reading of his settings or definitions (S₁₀₀, letter-error counting, fist parameters) where
  the thesis supports the correction;
- correcting the C++ to match the Fortran (V3);
- correcting our generator or our conversion (V5).

**Not allowed: changing any constant of his decoder to improve agreement.** If the cause is not found, the
task stops and reports to the owner, with the evidence.

**If V5 fails while V2 passes,** the diagnostic is V5′: his simulator's 8 kHz signal fed straight into the C++,
bypassing our generator and channel. If V5′ passes, the fault lies in our generator, channel or conversion.

## 6. The development-set measurement (Task 9)

This is descriptive. Stage 1 makes no adoption decision; the numbers go to the owner with the stage-2
proposal.

- **Decoders:**
  - `bell-agc`, level (B);
  - `bell-oracle`, level (A);
  - the frozen bank, `d5-bank`, already decoded on seed 1, as the reference.
- **Groups**, by the owner's priorities, all on seed 1 and oracle channels:
  - A2: noise × speed;
  - D2: speed changes;
  - G2 and H2: overs and QSOs;
  - C2: fists;
  - I2: Farnsworth.
- **Measures:**
  - the S₅₀₀ crossing at CER 0.10 per speed cell, with bootstrap intervals (devset2's logistic fit);
  - first-word CER on the final text (immediate and final are the same for Bell);
  - CER by group, and paired per-signal differences against the bank;
  - CPU per channel-second on the Linux machine.
- **Reported separately:**
  - the floor set by reference symbols his alphabet cannot produce;
  - the speed cells outside his 10–60 WPM range. Cell 1 (8–10.07 WPM) lies partly below it; cells 9 and 10
    (50.5–80 WPM) lie partly or wholly above it.
- **Noise only:** false characters per minute in pure noise (the suite's group N, §Task 7), for Bell and for
  the bank. This is the audit's test 4, and the failure that ruled out manta (about 195 per minute).
- **Expectations, stated now** (judgment, to be compared with the results):
  - Bell's observation is an envelope of fixed bandwidth, not matched to the speed. At slow speeds it lets
    in more noise per element than the bank's matched branch; summing likelihoods over an element's samples
    recovers part of that.
  - So Bell should trail the bank at slow speeds and outside his range. Any advantage should show at first
    words and speed changes, where his model has no hard decisions to undo.

## 7. Stage 2: proposed order and rule template

This is for the owner's approval of the order only. Each step gets its own plan, with its numbers set before
its run.

1. **The bank's matched likelihoods as the observation.** This serves the first priority, noise: each path
   reads the branch matched to its speed. Bell puts the cost of his fixed filter at 6 dB at 50 WPM and 10 dB
   at 20 WPM, for a filter-and-threshold detector (pp. 111–112). How much of that his sequence decoder
   recovers by summing over an element's samples is what stage 1 measures against the bank.
2. **Speed range 8–80 WPM and beyond** (the fastest and slowest speeds). At 5 ms per sample, an 80 WPM dot is
   3 samples and a 100 WPM dot 2.4. The range needs a finer time step, and with it his per-sample constants
   re-expressed in time: Q per second, the delay and windows in seconds or dits. It follows step 1, whose
   observation already comes at the bank's rate.
3. **Level handling,** if stage 1 shows a large gap between (A) and (B): a level-invariant amplitude model, with
   process noise relative to the estimated amplitude.
4. **Text model.**
   - First, his alphabet extended if it lacks "/" or "?".
   - Then letter n-grams and callsign structure. Bell's own conclusion is that the largest remaining gain lies
     there (pp. 106–108).
5. **Merging identical paths:** efficiency first, then a larger effective beam.
6. **Farnsworth timing:** character and word gaps longer than the element rate implies.

Overs (first words) and speed changes are measured at every step, since they are priority groups. A dedicated
step is added if stage 1 shows Bell's pause model insufficient: amplitude variance 0.25 and speed ±20 WPM after
a pause.

**Rule template** (the margins are heuristic; each step's plan states its own). The comparison is paired, with
the previous step, on seed 1's priority groups. A step is adopted if all four hold:
- its target improves, with a 95% paired bootstrap interval that excludes zero;
- no priority group's crossing worsens by more than 0.5 dB of S₅₀₀ (failing means the interval lies entirely
  beyond that);
- first-word CER worsens by no more than 0.02 (failing means the interval lies entirely beyond that);
- CPU stays within the step's budget.

Seeds 2 and 3 are used only at the end of stage 2.

## 8. Global constraints

- **The decoder is C++ only** (owner). Python is used only in the generator and the analysis tooling (D5).
- **Builds and tests.** The engine's C++ standard and toolchain, in both presets: Windows (MSVC) and Linux
  (g++). `ctest` passes on both, and CI is green. The Fortran reference is not part of the CMake build or CI;
  CI tests the C++ against committed golden data.
- **`docs/signal-processing.md` describes the engine's signal processing as it actually is.** Any change to
  signal processing — a parameter value, an algorithm, the order of stages, a new stage — updates the body
  and, where a derivation, provenance or limitation changes, the appendix, including the parameter table, in
  the same commit. Each value is classed derived, measured, heuristic, placeholder or owner. Bell's values are
  given as "Bell's", with his own class where he states it.
- **Units:** every quantity carries its unit; every dB names its reference (dBFS, dB S₁₀₀, dB S₅₀₀, dB
  relative to …).
- **Git:**
  - one git command per call, never chained;
  - commit freely on `bell-stage-1`;
  - no push and no merge without the owner's approval, each time;
  - never rewrite commits;
  - never discard work: use `git stash` or a worktree;
  - no attribution lines.
- **No private infrastructure** in committed files: the server is "the Linux machine", with no host, user,
  path or location.
- **Results data and figures** (CLAUDE.md):
  - the rounded per-signal score table of every reported run is committed;
  - records compute their numbers from the committed tables;
  - figures follow the rule.
- **Deleting:** delete only files created in the current task.
- **Wording:** American spelling. Bell's chain is a "receiver chain", never a "front end". A candidate setting
  is a "variant".

## 9. Conditions the tasks must also test

These are not covered by the reproduction, and each is pinned by a test in the task named.

1. **Exact zeros or a gap in the input,** as at a channel's start. NOISE's floor estimate becomes 0, so R = 0,
   and the likelihood divides by the innovation variance.
   - Expected: no division by zero and no NaN; the decoder keeps running.
   - If the listing has no guard, the guard acts only on exact zeros and is a documented deviation.
   - Tested in Tasks 4 and 5.
2. **A channel that opens mid-character:** no failure, and the following letters decode (Task 5).
3. **Ten minutes of pure noise:**
   - every probability and Kalman value stays finite;
   - probabilities are renormalized every sample, and ŷ is floored;
   - false letters are counted (Tasks 5 and 9).
4. **Very strong (S₅₀₀ +30 dB) and very weak signals under the AGC (B):** the level reaching the chain stays
   in the range his constants assume (Task 6).
5. **8 and 80 WPM signals,** outside his range: the decoder runs and its output stays bounded (Tasks 6 and 9).

## 10. Tasks

| Task | What | Depends on |
|---|---|---|
| 1 | Transcription, reference sheet, specification of the letter tree | — |
| 2 | The model's tables and functions in C++; the letter tree; the `MORSE` exporter | 1 |
| 3 | Bell's program compiled and run: V2; golden data | 1, 2 |
| 4 | The receiver chain in C++, with the conversion from our channel | 1, 3 |
| 5 | The decoder loop (PROCES) in C++: V3 | 2, 3, 4 |
| 6 | Engine, bench and replay integration; level options; `docs/signal-processing.md`; CPU | 5 |
| 7 | Bell's conditions in the generator; the reproduction suite | 1 |
| 8 | V5: the C++ on our generator and channel at his conditions | 6, 7 |
| 9 | Stage 1 on the development set; noise only; the stage-2 proposal | 8 |

If V2 (Task 3) fails and its cause is not found within §5.3, work stops and is reported to the owner, since
every later task depends on the transcription.

If the owner chooses D2 (b), Task 3 is dropped, and V2 and V3 with it. The golden comparisons in Tasks 4 and 5
then become values computed by hand from the reference sheet.

The results record is `docs/plans/2026-10-07-bell-stage-1-results.md`. It is created in Task 3, and each later
task adds its section.

### Task 1. Transcription, reference sheet, specification of the letter tree

Research only; no engine code.

**Files:**
- Create `reference/bell1977/listing.txt`: the transcription, verbatim, page by page, with Bell's sequence
  numbers and a header per page, `=== p. N (PDF page 2N + 3) ===`.
- Create `reference/bell1977/README.md`: what the folder is, the page map, the reading conventions, and the
  marks for uncertain characters.
- Create `docs/research/bell-1977-reference.md`: the reference sheet.
- Modify `docs/research/bell-1977-notes.md`: a pointer to the two new documents, and a dated correction
  wherever the transcription contradicts a statement.

**Steps:**
- [ ] **Step 1: Render the pages.**
  - Use Ghostscript: `gswin64c -q -dNOPAUSE -dBATCH -sDEVICE=pnggray -r170 -dFirstPage=M -dLastPage=M
    -sOutputFile=build/bell1977/pages/pM.png <thesis PDF>`.
  - Render the listing (printed pp. 159–191, PDF pages 321–385, odd pages only; the even pages are blank
    versos), plus the thesis pages that give settings and definitions: pp. 86–140 (PDF 175–283).
  - At 300 dpi for any line hard to read.
  - The images are git-ignored and stay in `build/`.
- [ ] **Step 2: Two independent readings.**
  - Reader A writes `build/bell1977/reader-a.txt`. Reader B, a separate agent not shown A's work, writes
    `reader-b.txt`.
  - Conventions:
    - exact characters;
    - Bell's slashed zero as `0`, the letter O as `O`;
    - layout and sequence numbers kept;
    - an uncertain character as `[?x|y]`.
- [ ] **Step 3: Settle every difference.**
  - Diff A against B, and settle each difference on the image at 300 dpi.
  - The result is `listing.txt`.
  - Record the number of differences and of `[?]` marks left; aim for none.
- [ ] **Step 4: The reference sheet,** in sections:
  - (a) **Program structure:** the call graph, and the order of operations per 8 kHz sample and per 5 ms
    sample.
  - (b) **Each subroutine:** its purpose, arguments, COMMON blocks, every constant with its line, and the
    computation written as mathematics, with units.
  - (c) **The receiver chain, derived:**
    - each filter's transfer function, center frequency, and 3 dB and noise bandwidths, computed from the
      coefficients with the method shown;
    - the envelope's scale for a carrier of unit amplitude;
    - the group delay;
    - the decimation phase.
  - (d) **The simulator:**
    - each process (durations, amplitude, fading, phase, chirp, noise) with its units per 8 kHz sample;
    - S₁₀₀ as the program implements it, in terms of RNOISE and the signal amplitude, derived.
  - (e) **The settings behind Tables XV and XVI**, and XIV and XVII if stated:
    - every value with its page, or "not given";
    - for each value not given, a proposed value with its reason, marked as our choice. For example, from the
      decoder's own model in §VII.C, or from the stated P_es, the probability that a fist's dot is read as a
      dash or the reverse: .00143, .0149 and .0403.
  - (f) **The letter tree:**
    - what LTRMAP, IELMST and MEMFCN mean, from the code that reads them;
    - the 16 rows in DATA;
    - his alphabet (IALPH) with its Morse codes;
    - the rule that reconstructs states 17–400. The numbering is ours; the meaning is his;
    - the tests it must pass:
      - every symbol decodes from its elements;
      - every state is reachable;
      - the 16 DATA rows are reproduced.
  - (g) **Text against listing:** a table with columns item, text value and page, listing value and line,
    used here. Under D1, the listing's value is the one used.
  - (h) **Library routines** the listing calls but does not contain, for example a Gaussian generator, with the
    standard equivalent proposed for each.
  - (i) **How Bell counted letter errors,** with pages, or "not stated".
  - (j) **A "gaps" section:** everything the plan must fill by its own choice, each with the choice and its
    reason.
- [ ] **Step 5: Reviews.**
  - A reviewer checks 5 pages chosen at random, character by character, against the images.
  - A second review checks the reference sheet against the transcription.
- [ ] **Step 6: Commit.**

### Task 2. The model's tables and functions in C++

**Files:**
- Create:
  - `engine/include/kz4ap/bell/config.hpp`
  - `engine/include/kz4ap/bell/tree.hpp`
  - `engine/include/kz4ap/bell/model.hpp`
  - `engine/src/bell/tree.cpp`
  - `engine/src/bell/model.cpp`
  - `engine/tests/bell/tree_test.cpp`
  - `engine/tests/bell/model_test.cpp`
  - `bench/src/bell_tables_main.cpp`
- Modify:
  - `engine/CMakeLists.txt`: the Bell sources, and a test executable `kz4ap_bell_tests`;
  - `bench/CMakeLists.txt`: the tool `kz4ap-bell-tables`.

**Interfaces:**
- Produces `struct BellConfig`: every constant of the reference sheet, defaulting to the listing's values, each
  with its unit and its listing line in a comment.
- Produces `struct MorseTree`, with:
  - `std::vector<int> ltrmap, ielmst`;
  - `std::vector<std::array<int, 6>> memfcn`;
  - `std::vector<std::string> alphabet`.

  Bell's 1-based codes are stored as values. It comes with `MorseTree bell_tree()` and
  `void write_morse_file(const MorseTree&, std::ostream&)`, which writes INITL's format.
- Produces `xtrans`, `ptrans`, `spdtr` and `model`, as the listing's routines (§4.3), with the argument lists
  the reference sheet gives.

**Steps:**
- [ ] **Step 1: Failing tests.**
  - XTRANS against the closed form of p. 100, with the listing's α, at chosen (element, D₀, speed),
    including D₀ below, at and above the mode.
  - SPDTR against Table X: each row sums to 1.
  - MODEL's Φ and Q at B = 1, 3, 7 and 14 bauds, and the key-up Φ at 10, 20 and 60 WPM.
  - PTRANS on the independent-letter model: from each node, the allowed elements and their probabilities.
  - The tree:
    - each symbol of IALPH, fed its elements from the root, reaches the node whose LTRMAP is that symbol;
    - every state is reachable;
    - the 16 DATA rows are equal.
  - `write_morse_file` reproduces INITL's format, checked by reading it back.
- [ ] **Step 2: Implement** from the reference sheet.
- [ ] **Step 3:** `ctest` on Windows.
- [ ] **Step 4: Commit.**

### Task 3. Bell's program compiled and run: V2; golden data

**Files:**
- Create `reference/bell1977/bell1977.f`: the listing made to compile. Each change is marked `C KZ4AP:` with its
  reason. Only these changes are allowed:
  1. TYPE/ACCEPT replaced by reading a settings file and writing to standard output;
  2. RAN and any missing library generator replaced by gfortran's RANDOM_NUMBER with a fixed seed. Gaussian
     numbers are made by the listing's own method, or, for a missing library routine, by Box–Muller;
  3. TRANSL's display output written to a text file;
  4. a trace option: per sample, z_k and R, and for each saved path its state, probability, ŷ and P;
  5. `MORSE` produced by `kz4ap-bell-tables`.

  Nothing else changes.
- Create `reference/bell1977/run/*.settings`: one settings file per target cell.
- Create `reference/bell1977/build.sh`: `gfortran -std=legacy -fdec -fdefault-real-8 -fdefault-double-8 -O2`,
  the flags adjusted as needed and recorded.
- Create `engine/tests/data/bell/`: the golden data, under 1 MB in all.
- Create `docs/plans/2026-10-07-bell-stage-1-results.md`: the results record, with sections 1 (conditions),
  2 (V1) and 3 (V2).
- Create `docs/plans/data/2026-10-07-bell-stage-1/v2-fortran.csv`: one row per run, rounded.
- Modify `reference/bell1977/README.md`: how to build and run it, and the change list.

**Steps:**
- [ ] **Step 1: Install gfortran on the Linux machine,** inside the repository folder:
  - micromamba (a single binary) into `build/micromamba/`, with `MAMBA_ROOT_PREFIX` there;
  - an environment `build/gfortran/` with conda-forge's `gfortran`;
  - record the versions.
- [ ] **Step 2: Make it compile,** with only the changes listed.
- [ ] **Step 3: Smoke test.** Keyboard Morse at 20 WPM, without noise: the decoded text equals the text sent.
- [ ] **Step 4: V2 runs.**
  - Each of the 30 target cells, 20 runs of 200 letters, each run with its own seed.
  - Character CER is computed with the project's scorer, the same as for the C++.
  - Write the CSV.
- [ ] **Step 5: Apply §5.2's rule; record it.** If it fails, follow §5.3. If no cause is found, stop and
  report.
- [ ] **Step 6: Golden data:** three inputs of 1 s each, written as little-endian float64:
  - keyboard Morse at 20 WPM and S₁₀₀ 12 dB;
  - the fair fist at 30 WPM and 6 dB;
  - a pause followed by a speed change.

  Each holds his 8 kHz signal, z_k, R, and the per-sample path trace.
- [ ] **Step 7: Commit.**

### Task 4. The receiver chain in C++, with the conversion from our channel

**Files:**
- Create `engine/include/kz4ap/bell/receiver.hpp`.
- Create `engine/src/bell/receiver.cpp`.
- Create `engine/tests/bell/receiver_test.cpp`.

**Interfaces:**
- Consumes `BellConfig`.
- Produces `struct BellSample { double t_s; double z; double r; }`.
- Produces `class BellReceiver`, with:
  - `BellReceiver(double channel_rate_hz, const BellConfig&)`;
  - `void push_channel(std::span<const std::complex<double>> mixed, double level, std::vector<BellSample>& out)`.
    The input is the channel already mixed to 0 Hz, in FS; `level` is the gain c of §4.1;
  - `void push_8k(double x, std::vector<BellSample>& out)`, the seam that takes his 8 kHz signal, for V3
    and V5′.

**Steps:**
- [ ] **Step 1: Failing tests.**
  - From the golden 8 kHz signals, z_k and R equal the Fortran's within 10⁻¹² relative.
  - The derived properties of §4.1, each against its derivation:
    - the droop;
    - the image level;
    - the output noise level with band-limited against white input;
    - a steady carrier's envelope;
    - output times at exactly 5k ms.
  - Exact zeros at the start: finite output (§9, item 1).
- [ ] **Step 2: Implement.** Write the derivations of §4.1 into the reference sheet's section (c) if they change.
- [ ] **Step 3:** `ctest`.
- [ ] **Step 4: Commit.**

### Task 5. The decoder loop (PROCES) in C++: V3

**Files:**
- Create `engine/include/kz4ap/bell/core.hpp`.
- Create `engine/src/bell/core.cpp`: PROCES, PATH, LIKHD, KALFIL, PROBP, SPROB, SAVEP, TRELIS and TRANSL.
- Create `engine/tests/bell/core_test.cpp`.

**Interfaces:**
- Consumes the Task 2 functions and `BellSample`.
- Produces `struct BellPath`: one saved path, holding what Bell's arrays hold for it. Its fields:
  - element, letter-tree state and speed, as Bell's codes;
  - the time in the element, in ms;
  - the posterior probability;
  - the Kalman ŷ and P;
  - the index of its parent node in TRELIS's store.

  The field names follow the reference sheet's section (b).
- Produces `struct BellLetter { std::string text; double probability; double start_s; double end_s; }`.
- Produces `class BellCore`, with:
  - `explicit BellCore(const BellConfig&)`;
  - `void step(const BellSample&)`: one PROCES;
  - `std::vector<BellLetter> take_letters()`;
  - `void finish()`: at the end of input, the best path's remaining letters;
  - `const std::vector<BellPath>& paths() const`;
  - `double speed_wpm() const`;
  - `double key_down_probability() const`.

**Steps:**
- [ ] **Step 1: Failing tests.**
  - Per subroutine, hand-computed: one Kalman step; the likelihood with its 1000 cut-off and its 10⁻⁴ skip;
    PROBP's normalization.
  - SAVEP on constructed path sets: the best per element if ≥ 10⁻⁶; then up to 0.90 or 25 paths; at least 7.
  - TRELIS on constructed trees: the common ancestor, and the forced decision at 200 samples.
  - TRANSL's letters and spaces.
  - V3 on the three golden inputs (rule in §5).
  - A channel that opens mid-character, and 10 minutes of pure noise: all values finite; letters counted
    (§9, items 2 and 3).
- [ ] **Step 2: Implement** from the transcription, subroutine by subroutine.
- [ ] **Step 3:** `ctest`. Any V3 difference is traced, with the values.
- [ ] **Step 4: Commit.** Update the results record, section 4 (V3, V4).

### Task 6. Engine, bench and replay integration; level options; documents; CPU

**Files:**
- Create:
  - `engine/include/kz4ap/bell_decoder.hpp`
  - `engine/src/bell_decoder.cpp`
  - `engine/tests/bell_decoder_test.cpp`
- Modify:
  - `engine/include/kz4ap/classical_decoder.hpp` (`FrontEnd::Bell`), `engine/src/engine.cpp` (create
    `BellDecoder`);
  - `bench/src/main.cpp` (`--decoder bell`);
  - `bench/src/replay.cpp` (`--decoder bank|bell`, and `--bell-level agc|oracle`). The oracle gain makes the
    key-down amplitude 1 in the channel. It is computed from three things: the label's S₅₀₀, the generator's
    noise level, and the channelizer's gain for a tone (derived, `docs/signal-processing.md` §7). A test on a
    generated tone checks it;
  - the bench tests;
  - `docs/signal-processing.md`: the body's §8d "Bell decoder", about 900 words; appendix A.8d with the exact
    form; A.10 rows; the bank's body section condensed per D6;
  - the results record, section 5 (CPU).

**Interfaces:**
- Consumes `BellReceiver` and `BellCore`.
- Produces `class BellDecoder final : public Decoder`, with:
  - `BellDecoder(double rate_hz, const BellConfig&, double residual_hz, BellLevel level)`;
  - `enum class BellLevel { Agc, Fixed }`, where `Fixed` carries the gain;
  - mixing by the anchor as `BankDecoder` does;
  - no corrections.

**Steps:**
- [ ] **Step 1: Failing tests.**
  - The decoder through the engine on a test signal; the replay's decoded file has the expected keys.
  - The AGC: instantaneous attack, and decay to 1/e in 200 ms (±1 sample).
  - Signals at S₅₀₀ +30 dB and −8 dB through the AGC: the level stays in range (§9, item 4).
  - 8 and 80 WPM signals: the output stays bounded (§9, item 5).
- [ ] **Step 2: Implement.**
- [ ] **Step 3: Documents,** per the rule, in the same commit.
- [ ] **Step 4: CPU** per channel-second on the Linux machine, on one development-set test case, against the
  bank's 42 ms.
- [ ] **Step 5:** `ctest` on Windows and Linux.
- [ ] **Step 6: Commit.**

### Task 7. Bell's conditions in the generator; the reproduction suite

**Files:**
- Create `training/kz4ap_synth/bell.py`: his amplitude, fading, phase and chirp processes, per SIMSG; his
  random text.
- Create `training/tests/test_bell_conditions.py`.
- Modify `training/kz4ap_synth/keying.py`: a style "bell", with Gaussian durations per KEY, its floor, and
  EDEV per fist.
- Modify `training/kz4ap_synth/generate.py`: hooks for the above.
- Modify `training/kz4ap_synth/suites.py`: the suite `BELL`, with three groups:
  - XV: keyboard Morse;
  - XVI: the three fists;
  - N: noise only. 32 channels × 300 s, each with a station at S₅₀₀ −60 dB, i.e. pure noise to within
    10⁻⁶ in power, so the channels have labels.

  Each target cell has 20 runs of 200 letters, packed 32 stations to a 48 kHz recording, as DEV2 is: about
  24 h of signal, 0.75 h of recording, about 1 GB.

**Steps:**
- [ ] **Step 1: Failing tests.**
  - The generated durations per fist: the mean and standard deviation, and P_es computed from them, against
    his stated .00143, .0149 and .0403.
  - S₁₀₀ to S₅₀₀ consistent with his RNOISE definition (reference sheet (d)).
  - The amplitude process's statistics against his.
  - The text's letter frequencies uniform over his alphabet.
- [ ] **Step 2: Implement.**
- [ ] **Step 3:** `pytest`.
- [ ] **Step 4: Commit.**

### Task 8. V5: the C++ on our generator and channel at his conditions

**Steps:**
- [ ] **Step 1: Generate and record** `BELL` on the Linux machine:
  - generate it;
  - record the oracle channels (`kz4ap-bench --oracle`);
  - run `kz4ap-bank-replay --decoder bell --bell-level oracle`;
  - score.
- [ ] **Step 2: Apply §5.2's rule:** against his tables, and against V2 cell by cell. If it fails: V5′, then
  §5.3.
- [ ] **Step 3: Write the score table** `docs/plans/data/2026-10-07-bell-stage-1/v5-cpp.csv`, and the results
  record's section 6.
- [ ] **Step 4: Commit.**

### Task 9. Stage 1 on the development set; noise only; the stage-2 proposal

**Steps:**
- [ ] **Step 1: Replay** seed 1's A2, D2, G2, H2, C2 and I2 with `bell-agc` and `bell-oracle`. Replay group N
  with Bell and with the bank.
- [ ] **Step 2: Analyze** per §6.
  - Score tables in `docs/plans/data/2026-10-07-bell-stage-1/`.
  - Figures by the rule, in `docs/plans/figures/2026-10-07-bell-stage-1/`.
- [ ] **Step 3: The results record's section 7.** It lays out the stage-2 order, revised by the results, as
  options for the owner. It gives no verdict.
- [ ] **Step 4: Commit.**
