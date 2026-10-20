# New decoder: the first prototype

> **Status: draft for the owner's approval (2026-10-20).** Nothing is built before the owner approves it. The
> decisions are in §3. **The owner's answers (2026-10-20):** D1 yes, a new branch; D2 `viterbi`; D3, D4, D5 and D6
> "whatever we've already decided": the design notes' decisions, implemented as §2 and §3 state them, where a
> starting value only sets where its measurement begins; D5 is no tail until measured. D7 and D8 await him, with the
> background he asked for (given in chat). The owner does not read this plan (2026-10-20): it is the executors'
> document, and every decision is brought to him in chat, in words.
>
> **Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.1 (the Classical mode), §5.2 step 1 and step 3
> (amendments of 2026-10-19) and §4.1 item 3. **Design:** `docs/design/2026-10-09-new-decoder-design-notes.md`, the
> authority for every design choice, cited here as "notes, topic N, item M". **Evidence:**
> `docs/research/2026-10-14-new-decoder-review-1.md` (review 1) and `docs/research/2026-10-14-standard-practice-check/`.
>
> **Branch:** `new-decoder-1`, from `bell-stage-1` (decision D1).
>
> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or
> superpowers:executing-plans, as the owner chooses (D9). Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** build the new decoder of the design notes in C++ in the engine, verify it, and measure it on the
development set against the frozen bank decoder.

**Architecture:** one decoder per channel. A frequency tracker that uses none of the decoder's decisions re-centers
the channel; its samples are summed into 2 ms pieces; an explicit-duration (semi-Markov) model is searched by
time-synchronous Viterbi beam search over hypotheses, each carrying its own timing state and its own Kalman estimate
of the signal level, each scoring the pieces it claims as marks against a hiss level that the engine reads from the
spectrum around the station.

**Tech stack:** C++20 (MSVC 14.5x on Windows, g++ 11.4 on the Linux machine), CMake presets, GoogleTest. Python 3.12
only for the generator's new noise group, the suite runner and the analysis (CLAUDE.md, "Languages").

## 0. Terms

- **Story, hypothesis:** one continuous account of what has been sent on the channel since its start: activity, the
  current mark or gap and how long it has lasted, speed, Farnsworth factor, the letter in progress, the signal
  level, the text. "Hypothesis" is the search's word for the same thing (notes, topic 5).
- **Score:** the natural logarithm of a story's probability together with the signal so far, measured against the
  story "noise only, throughout" (so a gap piece adds 0). In **nats**; a prior of probability p costs ln(1/p) nats.
  Scores are held relative to the best at every piece.
- **Piece:** the sum of 3 consecutive channel samples, 2.000 ms at 1 500 samples/s (derived: the input rate × 64 /
  the FFT size, 48 000 × 64 / 2 048 for DEV2's 48 kHz recordings and 192 000 × 64 / 8 192 at 192 kHz). The
  decoder's time step (notes, topic 4, items 1 and 3).
- **Block:** the coherent sum of a mark's pieces, no longer than the block length; a mark longer than one block is
  several blocks, combined noncoherently (notes, topic 4, item 9).
- **Beam:** stories more than the beam's width (nats) behind the best are dropped at every piece. **Ceiling:** the
  most stories kept at once (notes, topic 5, items 4 and 5).
- **Newborn:** a story that starts at a mark: **from silence** (an over starts) or a **rescue** newborn (a sender
  begins a new story at a letter start, keeping the text) (notes, topic 5, item 3).
- **Over:** one transmission by a station, from its start until the other station takes over.
- **Search error:** a difference between the text a run decodes and the text of a run with a much wider beam and
  higher ceiling (the **wide-beam reference**), counted per 1 000 reference letters.
- **Genie run:** a run in which the decoder is told something it normally estimates (true timing, true frequency,
  true fading spread), to bound what a better estimate could gain.
- **Variant:** one setting of the decoder in a comparison.
- **DEV2:** the development set, seed 1, at the owner's option B sizes (`docs/plans/2026-10-06-development-set-results.md`,
  §4.4 and §5.1): 498 recordings, 14 204 oracle channels, 1 065 742.1 channel-seconds. Its groups: A2 sensitivity,
  B2 fading, C2 keying styles, D2 speed changes, E2 a neighbor station, F2 tuning offsets and drift, G2 QSOs on one
  track, H2 QSOs per station, I2 Farnsworth spacing, S2 stretched copies of A2.
- **The bank:** the bank decoder at its current defaults, frozen as the reference; its run on DEV2 is `d5-bank`
  (28.85 ms of CPU per channel-second on the Linux machine; results record §6).
- **Channel-second:** one second of one channel's stream, the unit of decoding cost. **Full-run equivalent (FRE):**
  the cost of one run over all of DEV2 at the default settings.
- CER, character CER, space error rate, first-word CER and the paired comparison are those of
  `docs/signal-processing.md`, appendix A.11. S₅₀₀ is the key-down carrier power over the noise in 500 Hz, in dB.

## 1. What the first prototype delivers, and what it leaves out

It delivers:

1. **The decoder,** in C++ in the engine behind the `Decoder` interface, selectable as `--decoder viterbi` (D2), not
   the default.
2. **Two parts at channel level** that it needs and that use none of its decisions: the hiss estimator, read from
   the spectrum around the station (notes, topic 3, item 3), and the frequency tracker (notes, topic 4, item 8).
3. **Instrumentation and genie options** for the measurements the notes require: live stories per piece, how often
   the ceiling decides, what becomes of each newborn, the largest deficit a newborn reaches before it leads; and
   true timing, true frequency and true fading spread from the labels.
4. **Verification** written before any run (§5), and **the measurements on DEV2** (§6), with the comparison against
   the bank.
5. **Documents:** `docs/signal-processing.md` (the body's new section in full, the appendix's exact form and
   parameter rows), and the results record `docs/plans/2026-10-20-new-decoder-prototype-1-results.md`, with its
   committed score tables.

It leaves out, each settled later by the notes' own rules:

- the text model: every symbol equally likely (notes, topic 6, item 6);
- the non-Morse states (steady carrier, nonsense keying) and the channel's other contents: other signals, static
  crashes, sequences that fit no letter (obvious item O1 in §3);
- an interference model (O2); E2 shows what its absence costs;
- opening and closing channels: every run uses oracle channels, open for the whole recording (notes, "Opening and
  closing channels": nothing there affects the first prototype);
- station changes between overs (topic 7), output timing and corrections (topic 8: the prototype prints its best
  story when the recording ends, O4), the fist's heavy tail (D5), and the overload governor, which serves many
  channels on a machine short of CPU (topic 5, item 7);
- the refinements the notes hold back until a measurement shows a need, each later a variant: small speed steps
  (topic 2, item 2), gaps conditioned on the mark before (topic 2, item 4), per-sender adaptation (topic 2, item 6),
  several fading spreads in each story (topic 4, item 9), a level prior centered on the detector's level (topic 3,
  item 5), rescue newborns that keep their parent's level (topic 5, item 3);
- the measurements on real recordings (the fist on real keying, the hiss on HamSCI's recordings, the real rates of
  overs and restarts): they run beside the prototype, outside this plan (notes, topic 2, item 5; topic 3, item 3;
  topic 5, item 3).

## 2. The decoder, stage by stage

This section is what the code must do. Each value carries its unit and its class: **derived**, **measured**,
**heuristic**, **placeholder** (kept until a measurement in §6 or a later real-data measurement replaces it) or
**owner**. "Standard" and "ours" say where a method comes from, as the notes label it. The full table of values is
§2.10.

### 2.1 The channel and the pieces

1. **Input:** the channelizer's stream for the channel, complex, in FS, at r = 1 500 samples/s, with the station
   within ±11.7 Hz of 0 Hz (`docs/signal-processing.md` §7).
2. **Re-centering:** the samples are multiplied by a phase-continuous oscillator at −f̂(t), the frequency tracker's
   current estimate of the station's offset (§2.9), so every story reads the same re-centered samples (notes, topic 4,
   item 8).
3. **Pieces:** consecutive groups of 3 samples are summed: x_k = Σ_{j=0}^{2} y[3k + j] (FS). Pieces run across the
   engine's 32-sample blocks: a remainder of 1 or 2 samples waits for the next block. Piece k is at time
   t_k = t_first + (3k + 3)/r, the end of its last sample.

### 2.2 The noise model

1. **The hiss level per hertz, N₀** (FS²/Hz), comes from the engine's hiss estimator (§2.8) before every block. The
   decoder never estimates noise from its own gaps (notes, topic 3, item 3).
2. **The noise of a sum** (derived, exact; notes, topic 4, item 7). The channel's noise autocovariance at a lag of k
   channel samples is R[k] = N₀ · fs · Σ_m h[m] h[m + D k], with fs the input rate, D = N/64 the decimation and h the
   channelizer's N/2 + 1 taps normalized to Σh = 1: at 48 kHz (DEV2's rate) N = 2 048, D = 32 and 1 025 taps; at
   192 kHz N = 8 192, D = 128 and 4 097 taps. Both are the same 21.3 ms Blackman-windowed sinc, so R[0] ≈ N₀ · 252 Hz
   at either rate (252 Hz measured from the taps at 192 kHz, `docs/signal-processing.md` A.7; Task 2 checks both). A
   sum of m consecutive channel samples has noise variance V(m) = Σ_{|k|<m} (m − |k|) R[k] = N₀ · c(m), where c(m),
   in Hz, is computed once from the channelizer's own taps, decimation and rate. For long sums c(m) → 1 500 Hz × m
   (derived), so a sum over T seconds has E/N₀ = a²T/N₀. On DEV2's recordings, σ = 0.02 FS at 48 kHz gives
   N₀ = σ²/fs = 8.33 × 10⁻⁹ FS²/Hz.

### 2.3 The evidence of a block

A block of n pieces (m = 3n samples) has the complex sum S. Its log-likelihood ratio against noise alone, for a
carrier of amplitude a and unknown phase (the standard noncoherent statistic; Proakis and Salehi §4.5-1; notes,
topic 4, item 2):

  Λ(S; a) = −b² + ln I₀(2 b z),  z = |S| / √V(m),  b = m · a / √V(m)  (nats).

1. **Known level:** the formula above. ln I₀ by Abramowitz and Stegun 9.8.1 (x ≤ 3.75) and 9.8.2 (x > 3.75). Their
   stated bounds, 1.6 × 10⁻⁷ on I₀ and 1.9 × 10⁻⁷ on √x e^{−x} I₀ (whose value is at least 0.39), keep the error of
   ln I₀ below 5 × 10⁻⁷ nats (derived).
2. **A level known to a Gaussian spread** a ~ N(μ, P) (the Kalman predictive density; notes, topic 3, item 1):
   Λ_P(S) = ln ∫ N(a; μ, P) e^{Λ(S; a)} da, by Gauss–Hermite quadrature of order K = 5 in a (Abramowitz and
   Stegun 25.4.46), nodes with a < 0 dropped and the weights renormalized (decision D4). It is exact to 0.01 nats over
   the tested range (Task 2's test).
3. **A newborn's first block, against the flat prior** (notes, topic 5, item 3, the level): the level x, in dB S₅₀₀,
   is uniform over [−15, +85] dB, with a² = 10^{x/10} · N₀ · 500 Hz, so b² = 10^{x/10} · 500 Hz · m² / c(m) does not
   depend on N₀ (derived). Λ_flat(n, z) = ln (1/(100 dB)) ∫_{−15 dB}^{+85 dB} e^{Λ(S; a(x))} dx is tabulated once
   per configuration over the block's length n (1 to the block length, in pieces) and z, with the posterior mean and
   variance of a beside it. Above the table's last z, the large-z form Λ_flat ≈ z² − ln(2z²) + ln(20 / (ln 10 · 100))
   (derived, Laplace's method) is used, matched to the table within 0.01 nats where they meet.
4. **Booking:** while a mark lasts, the current block's evidence so far, Λ of its running sum, is part of the
   story's score at every piece (notes, topic 5, item 9). A finished block's evidence enters the score for good, and
   its sum updates the story's level (§2.4). Gap pieces add 0.

### 2.4 The level

Each story carries its level: a Gaussian N(μ, P) for the key-down amplitude a (FS), the time t_m of its last update,
and a flag "not yet learned" (notes, topic 3, item 1).

1. **Prediction** to time t (decision D4, the model behind the notes' numbers): ρ = exp(−π² f_D² (t − t_m)² / 2), the
   correlation of the complex gain of Rayleigh fading with a Gaussian Doppler spectrum of spread f_D at lag t − t_m
   (derived), taken as the amplitude's correlation (an approximation, ours, as in the notes' numbers), and
   P(t) = ρ² P + (s μ)² (1 − ρ²), μ unchanged, with s = √(4/π − 1) = 0.523, the spread of a Rayleigh amplitude about
   its mean (derived). From a well-measured level, the next mark two dots later is then uncertain by ±49%, ±19% and
   ±10% at 5, 20 and 40 WPM for f_D = 1 Hz (notes, topic 3, item 2). The form is ours; f_D is one fixed spread for
   every signal, set by measurement (§6, R6; notes, topic 4, item 9).
2. **Update** at each finished block: the posterior of a given the block's sum, under the predicted Gaussian and the
   Rician likelihood of §2.3, replaced by the Gaussian with its mean and variance (moment matching), by the same
   quadrature. At high SNR this is the Kalman update with gain P / (P + V(m)/(2m²)) (derived; Task 2's test).
3. **A newborn** has its level "not yet learned": its first block is scored by Λ_flat, and when that block finishes
   its level starts from the flat prior's posterior mean and variance (a Gaussian approximation; notes, topic 5,
   item 3). Rescue newborns start their level again the same way.
4. **Merging** keeps the better story's level (the standard per-survivor approximation; notes, topic 5, item 2).

### 2.5 Timing

1. **Speeds:** 32 dot lengths T_s = 12.0 ms × 1.1^s, s = 0 … 31 (12.0–230.3 ms; 100–5.2 WPM). **Farnsworth factors:**
   F_f = 1.1^f, f = 0 … 22 (1–8.14). Both fixed within a story (notes, topic 2, items 1–3).
2. **The fist** (notes, topic 2, item 4; placeholders, heuristic): five kinds, with the length of each element or gap
   log-normal about its median, σ = 0.3 in ln(length) for every kind. Medians in dots: dot 1, dash 3, element gap 1,
   letter gap 3F, word gap 7F. No heavy tail (D5).
3. **In pieces:** a kind's length D has the log-normal of median M pieces (its median in dots × T_s / 2 ms), with
   distribution function F. Rounded to the nearest piece and never shorter than one (ours): P(1) = F(3/2) and
   P(τ) = F(τ + ½) − F(τ − ½) for τ ≥ 2, so the P(τ) sum to 1; the survival is S(1) = 1 and S(τ) = 1 − F(τ − ½).
   ln S and ln P are computed from a table of ln Φc on a fine grid of z = (ln τ − ln M)/σ, not from tables per
   speed (the longest median, a word gap at 5.2 WPM with F = 8.14, is 6 562 pieces).
4. **Hazard form** (standard: Murphy 2002; notes, topic 5, item 9): a story still in its mark or gap at τ pieces has
   paid ln S(τ); one that ends there pays ln P(τ).
5. **One merged mark:** a story in a mark keeps the better of ln S_dot and ln S_dash, each plus its label's text cost
   at that tree node (0 where the child exists, −∞ where not; §2.6). When the mark ends it branches into the dot and
   the dash with their own ln P (notes, topic 1, item 2; topic 5, item 9).
6. **One merged gap:** a story in a gap keeps the best of the kinds allowed at its tree node (§2.6), each with its
   own prior and survival; the kind is chosen when the gap ends.
7. **A newborn's speed:** while its first mark lasts, a newborn keeps the best over the 32 speeds and both labels of
   ln(1/32) + ln S; when that mark ends it branches into every speed and label with a finite ln P, each paying
   ln(1/32) and its own ln P (notes, topic 5, item 3: "one newborn").
8. **A story's Farnsworth factor** is open until its first gap between letters or words: the merged gap keeps the
   best over the 23 factors; when that gap ends as a letter or word gap it branches into the factors, each paying
   ln(1/23) and its own ln P (notes, topic 2, item 3).

### 2.6 Text without a text model

1. **The Morse tree** is built from the engine's table (`kz4ap::morse`): 62 symbols (letters, digits, 14 punctuation
   marks, 12 prosigns; notes, topic 6, item 7). A node is a prefix of at least one symbol; a mark that leaves the tree
   is not created.
2. **Letters:** each letter costs ln 62 = 4.127 nats when it starts, by look-ahead (notes, topic 5, item 9); nothing
   more is charged inside it.
3. **Where words and overs end** (notes, topic 5, item 3): after a mark at a node holding a symbol, the gap is between
   elements (if the node has children; cost 0), between letters (ln(1/(1 − p)) = 0.396 nats), between words
   (ln(1/(p(1 − q))) = 1.161 nats), or the over's end (ln(1/(pq)) = 4.296 nats), with p = 1/3.06 and q = 1/24
   (counted constants). Which kind it is follows from the gap's length when it ends; the over's end is taken at the
   mark's end, as a step to the no-signal state.
4. **Text** is a chain of letters and word spaces shared between stories (each letter links to the one before it).
   A new over after silence begins with a word space if the story's text is not empty (the labels join a signal's
   transmissions with word spaces; `docs/signal-processing.md` A.11).

### 2.7 The search, piece by piece

1. **At the channel's first piece** there is one story: no signal, score 0, empty text.
2. **For every story, at every piece,** in this order:
   - **in a mark:** add the piece to the block's running sum; if the block is full, finish it (§2.3, §2.4) and open
     the next. The story continues with its updated score. It also ends here as a dot and as a dash (§2.5), each a
     successor in a gap at the child node, with the block finished, its sum updating the level; and, where the
     child node holds a symbol, a successor in the no-signal state that ends the over after that letter (§2.6).
   - **in a gap:** the story continues (the piece adds 0; the survival changes). It also ends here as each kind
     allowed (§2.6), each a successor starting a mark at the next piece: an element gap keeps the node; a letter or
     word gap pays the letter at the root and, for a word gap, adds a word space. Each story that starts a letter
     also pays ln(1/(1 − ε)) for not restarting.
   - **no signal:** the story continues (ln(1 − P) per piece). It needs no clock (notes, topic 1, item 9), since
     the rate of overs is the same at every piece of silence.

   Every successor starts its next element at the next piece.
3. **Births:**
   - **from silence:** while the no-signal story lives, a newborn starts a mark at the next piece, at every piece:
     its score is the no-signal story's plus ln P plus ln(1/62) (9.210 + 4.127 nats behind it), its speed, factor
     and level open;
   - **rescue:** at every piece where at least one story starts a letter, one newborn starts that mark. Its parent is
     the best such story, with the gap read as between letters or words; it takes that story's score plus ln ε
     (6.908 nats behind it) and its text, with its speed, factor and level open (notes, topic 5, item 3).
4. **Merging:** successors created at the same piece in the same state are merged, keeping the better score, its
   text and its level (standard: the Viterbi algorithm's merge; notes, topic 5, item 2). The state is: activity; key
   up or down; pieces in the current element; speed or "open"; factor or "open"; tree node. Continuing stories never
   collide, since a story's element began at the piece its state was created.
5. **Pruning:** the best score is found; every story more than the beam behind it is dropped; if more than the
   ceiling remain, the ceiling's best are kept, ties broken by the state, then by creation order (so runs are
   identical on every platform). Every successor with a finite score is created first: there is no separate
   threshold for creating branches (O12).
6. **Scores** are then held relative to the best (subtracting the best's score; numerical convenience).
7. **At the end of the input** (`flush()`), the best story's text is published, with each letter's start and end
   times; if that story stands at a node holding a symbol, the symbol is its last letter. Each character's
   probability is reported as 1 (no per-character probability before topic 8; O4).

### 2.8 The hiss estimator (channel level, in the engine)

1. **Average:** every spectrum frame's bins (linear power, FS²) are averaged as the signal detector averages them: a
   one-pole average with time constant 1 s, a plain running mean while 1/n exceeds the pole's step
   (`engine/src/signal_detector.cpp`; heuristic). It is computed for the decoder's channels in oracle mode too, where
   the detector does not run, and it does not touch the detector's code (O10).
2. **The station's neighborhood:** the 171 bins within ±85 bins (±2 kHz) of the channel's center bin; their 25th
   percentile, taken as the 43rd smallest (placeholders, heuristic; notes, topic 3, item 3).
3. **Bias:** on hiss alone that percentile reads low by a factor β = (the 25th percentile of a Gamma distribution
   with the average's shape) / (its mean). The shape follows from the average's weights and the correlation 1/36 of
   successive frames' powers in a bin (Hann window, 50% overlap: the amplitude correlation is 1/6; derived): k = 1/v,
   v the average's variance relative to its squared mean, computed frame by frame (v₁ = 1). In steady state k ≈ 89
   and β ≈ 0.927 (derived; a scratch simulation of 171 averaged bins gave 0.925, and 0.288 after one frame, the
   exponential's ln(4/3)). The estimate is divided by β.
4. **Per hertz:** N₀ = (the corrected percentile) / 35.16 Hz, one bin's equivalent noise bandwidth
   (`docs/signal-processing.md` §4). Exact zeros give N₀ = 0, and the decoder decodes nothing while N₀ = 0 (as the
   bank treats exact zeros; A.8c).

### 2.9 The frequency tracker (channel level, in the decoder)

A standard design (notes, topic 4, item 8): the maximum-likelihood estimate of a tone's frequency at the peak of the
spectrum (Rife and Boorstyn 1974), tracked with its drift rate by a Kalman filter. It reads the carrier line that
on/off keying leaves in the spectrum (0.44² of the key-down power for PARIS text; derived), so it needs no key-down
decisions (decision D6; every value here a placeholder, measured in R9).

1. **Start:** f̂ is the engine's frequency anchor (the label's frequency in oracle mode; the detector's otherwise),
   drift 0.
2. **Every 0.5 s,** the last 1.0 s of channel samples, shifted by −f̂, Hann-windowed and zero-padded to 8 192 points
   (0.18 Hz bins), are searched for the largest bin within ±15 Hz; a parabola through the peak and its neighbors (in
   dB) gives the measured offset.
3. **The measurement is used** only if the peak stands at least 13 dB above the median of the searched bins: on
   noise alone, one of the 167 searched bins passes 10 dB in about 15% of the updates and 13 dB about once in 50
   minutes, while a keyed carrier at S₅₀₀ −5 dB stands about 15 dB above the median (derived, bins taken as
   independent). Its variance is (0.1 Hz)². The Kalman state is the offset and its drift rate (Hz, Hz/s); the
   drift rate's process noise is (0.1 Hz/s)² per second.
4. **The oscillator** follows f̂(t) = f̂ + ḟ̂ · (t − t_update) between updates, phase-continuous.
5. **Accuracy needed** (derived; notes, topic 4, item 8): a residual Δf costs under 1 dB of signal power while
   Δf · T ≤ 0.26 for a block of T; 0.67 Hz for 390 ms blocks.

### 2.10 Values

| Value | Default | Unit | Class and source |
|---|---|---|---|
| Piece | 3 samples = 2.000 | ms | derived from r = 1 500 samples/s (notes, topic 4, item 1) |
| Speeds | 12.0 × 1.1^s, s = 0 … 31 | ms per dot | owner (notes, topic 2, item 1) |
| Farnsworth factors | 1.1^f, f = 0 … 22 | — | owner (notes, topic 2, item 3) |
| Fist medians | 1, 3, 1, 3F, 7F | dots | placeholder, heuristic (ITU-R M.1677-1) |
| Fist σ | 0.3, every kind | ln(length) | placeholder, heuristic |
| Fist tail | none until measured on real keying | — | owner (D5, 2026-10-20) |
| Speed and factor priors | 1/32, 1/23 (3.466, 3.135 nats) | — | owner (flat) |
| Letter cost | 1/62 (4.127 nats) | — | owner (no text model) |
| p, q | 1/3.06, 1/24 | — | p measured (VE3NEA's mean word length of on-air traffic), q counted (DEV2's own); placeholders until recounted on real traffic (notes, topic 5, item 3) |
| Rate of overs from silence | 0.05 (P = 10⁻⁴ per piece) | s⁻¹ | placeholder: DEV2's own (notes, topic 5, item 3) |
| Rescue rate ε | 10⁻³ | per letter start | placeholder: DEV2's own |
| Level prior | flat, −15 to +85 | dB S₅₀₀ | placeholder (notes, topic 5, item 3) |
| Fading spread f_D | 1 | Hz | placeholder, swept in R6 (D3) |
| Block length | 390 at f_D = 1 Hz | ms | derived from f_D: the longest block losing at most 0.5 dB of energy (the 0.5 dB heuristic; notes, topic 4, item 9) |
| Quadrature order K | 5 | — | D4; checked by test |
| Beam | 40 | nats | placeholder, at least 28 (derived minimum; notes, topic 5, item 4); set in R2 (D3) |
| Ceiling | 1 700 | stories | placeholder (notes, topic 5, item 5); set in R3 (D3) |
| Hiss neighborhood, rank | ±2 kHz (±85 bins), 25th percentile | Hz, % | placeholder (notes, topic 3, item 3) |
| Hiss average | one-pole, 1 s | s | heuristic (the detector's) |
| Tracker window, hop, search, gate | 1.0 s, 0.5 s, ±15 Hz, 13 dB re the median bin | s, s, Hz, dB | placeholders (D6), measured in R9 |
| Tracker noise | measurement (0.1 Hz)², drift (0.1 Hz/s)² s⁻¹ | Hz², Hz² s⁻³ | placeholders (D6) |
| Wide-beam reference | beam 80, ceiling 17 000 | nats, stories | placeholders, checked in R1b (D3) |

### 2.11 What a piece costs (measured before this plan)

A timing slice (scratch code, not in the repository) did one piece's work for every story on synthetic tables and
noise alone, so that the ceiling binds: continuing, ending into successors with the level's update, one rescue
newborn and one birth from silence, merging, pruning (notes, "After review 1", item 7). Measured with one thread:

| Machine | at 1 700 stories | at 300 stories |
|---|---|---|
| Laptop, Intel i7-12700H, MSVC /O2 (2026-10-19) | 61 ns per story per piece; 51.6 ms of CPU per channel-second | 9.0 ms |
| The Linux machine, g++ 11.4 -O3 (2026-10-20, three runs) | 63–64 ns; 53.5–54.6 ms | 59–60 ns; 8.9–9.0 ms |
| The development desktop, Intel i7-9700F, MSVC /O2 (2026-10-20, three runs) | 83–94 ns; 70.3–80.1 ms | 57–65 ns; 8.6–9.8 ms |

The slice leaves out the Farnsworth branching, the over's end, the newborns' bookkeeping and the quadrature of §2.3,
so the prototype costs more; R0 measures it. The development desktop is slower at the ceiling (conjectured: its
256 kB of L2 cache per core against 1.25 MB on the other two), so timings are taken on the Linux machine.

## 3. Decisions for the owner

### Decisions

**D1. The branch: `new-decoder-1`, from `bell-stage-1`** after this plan's commit. Earlier milestones each had their
own branch. *Obvious; stated for approval.*

**D2. The decoder's name.** The bench names decoders by a word (`envelope`, `matched`, `bank`), and the code's
"classical" already names Envelope's and Matched's shared back end (`ClassicalDecoder`), so the spec's mode name
would collide.
- (a) **`viterbi`**: `--decoder viterbi`, `ViterbiDecoder`, namespace `kz4ap::viterbi`. It names the search, as
  "bank" names the filter bank. *Recommended.*
- (b) `story`: the notes' word for a hypothesis.
- (c) `classical`, renaming the existing `ClassicalDecoder` first: churn in code that is otherwise untouched.

**D3. Starting values for the parameters that measurements set** (the 2026-10-19 item "starting values that sweeps
replace later are listed in the plan and approved with it"): beam 40 nats (the minimum is 28); ceiling 1 700;
fading spread 1 Hz (390 ms blocks), the middle of R6's sweep over 0.3, 1, 2 and 5 Hz; wide-beam reference at beam
80 nats and ceiling 17 000, checked by a wider one (R1b). Each is replaced by its run in §6. *Recommended as listed.*

**D4. Two numerical choices the notes leave open.**
- **The level's uncertainty in the score and the level's update:** (a) Gauss–Hermite quadrature of order 5 (§2.3,
  §2.4): exact to 0.01 nats, about five Bessel evaluations per story in a mark per piece, so perhaps 1.5–2 times the
  slice's cost (a guess; R0 measures it). (b) A closed-form approximation, as in the timing slice: cheaper, with an
  error nobody has measured. *Recommended: (a); a cheaper form replaces it only if it matches the quadrature within
  0.05 nats on Task 2's test grid and CPU shows a need.*
- **How fast the level may change:** the Gaussian conditional with the fading's own correlation at the elapsed time
  (§2.4, item 1). It is the model behind the notes' numbers (±49%, ±19%, ±10%). A first-order Gauss–Markov process
  (correlation e^{−t/τ}) is the alternative; it lets the level wander faster over short gaps than Rayleigh fading
  with a Gaussian spectrum does. *Recommended: the Gaussian conditional, ours, with f_D swept (R6).*

**D5. The fist's heavy tail** (the notes, topic 2, item 4). **Decided by the owner, 2026-10-20: (a), no tail until
it is measured on real keying.**
- *Background.* At σ 0.3 a length k times its median costs (ln k)²/0.18 nats more than a perfectly timed one
  (derived): 2.7 nats at 2×, 6.7 at 3×, 10.7 at 4×; on timing alone one element must be about 9 times its median to
  cost the 28 nats of the minimum beam. A tail of weight w (Huber's contaminated model) caps one wild element's cost
  near ln(1/w) plus a few nats: about 6 nats at w = 1% for a tail flat in ln(length) over a 20 : 1 range (derived).
  The generator draws every length log-normal with no tail (`training/kz4ap_synth/keying.py`), so on DEV2 a tail
  can only cost: about w nats per element, and cheaper wild readings for wrong stories (conjectured).
- (a) No tail until it is measured on real keying: the rule chosen for the rates, the development set's own until
  counted. **Chosen.**
- (b) A small fixed tail now, e.g. 1% over a 20 : 1 range: two heuristic numbers, each needing a measurement plan.

**D6. The frequency tracker** (§2.9): the design above, with its placeholders measured in R9. The default runs use
the tracker; a genie variant re-centers on the label's true frequency and drift, to measure what the tracker
loses. *Recommended.* The alternative, a hidden Markov model over frequency cells (Streit and Barrett 1990), is the
standard at low SNR; it is tried only if R9 shows the tracker losing lock where the decoder could still copy.

**D7. Which runs use the whole development set, and which a subset.** §6 proposes the subsets and the costs. The
owner chooses at the stop after R0 (Task 9), when the prototype's own CPU is measured.
- Proposed: the whole of DEV2 (with the new noise group, D8) only for the default run R4, the comparison with the
  bank. Search errors, the beam and the ceiling (R1–R3) on the subset W, about 27% of DEV2: a quarter of the
  recordings of A2, B2, C2 and I2, and all of D2, E2, F2, G2 and H2. The other runs on the groups their checks name.
  About 7–11 full-run equivalents in all, against about 21–32 if every run used the whole set (§6.3).

**D8. A noise-only group, N2:** 100 channels × 360 s (10 channel-hours) of white noise, with labeled stations at
S₅₀₀ −60 dB, so that the channels open and the decoder's false characters per hour are counted (step 1, P5(h), in
the standard-practice check). DEV2 has no such group. *Recommended.*

**D9. How the plan is executed:** subagent-driven (a fresh implementer per task and a fresh reviewer after each) or
in this session (one reviewer over the whole branch at the end). Asked when the plan is approved.

### Obvious items, stated for approval

- **O1.** Only Morse and no-signal stories: no steady-carrier or nonsense-keying states (stated 2026-10-19).
- **O2.** No interference model (stated 2026-10-19).
- **O3.** Long runs on the Linux machine (stated 2026-10-19).
- **O4.** The prototype prints its best story when the recording ends; each character's probability is reported
  as 1 (notes, "After review 1", item 7).
- **O5.** It is scored by the bench's scorer (CER, character CER, space error rate, first-word CER), and the
  rounded per-signal score table of every reported run is committed (CLAUDE.md).
- **O6.** The owner's group priorities: A2, D2, G2 and H2 first, then C2 and I2.
- **O7.** I2's signals above a Farnsworth factor of 8 are left out on both sides of every comparison with the bank,
  and listed separately (spec §5.2, step 3). The score tables gain each signal's overall speed and factor.
- **O8.** Every placeholder ends with a measurement: in §6 for DEV2's, later on real recordings for the rest.
- **O9.** Runs go through `kz4ap-bench --oracle` on DEV2's recordings, not through replayed channel streams: the
  hiss estimator needs the spectrum ±2 kHz around the station, and the recorded streams carry ±750 Hz.
- **O10.** The hiss average runs in the engine for oracle channels too, reimplementing the detector's rule without
  touching the detector (§2.8).
- **O11.** The alphabet is the engine's table, 62 symbols.
- **O12.** No separate threshold for creating branches: every successor with a finite score is created, then the
  beam and the ceiling prune (the notes' "pruning threshold, value not yet set", topic 5, item 1, becomes
  unnecessary).
- **O13.** The bank's reference is `d5-bank`, its score table re-exported with the two Farnsworth columns (its
  other columns unchanged), as D5 re-exported `d4-bank`.
- **O14.** The instrumentation is off unless a run asks for it, and changes no decoding.

## 4. Structure in the code

| Path | What it holds |
|---|---|
| `engine/include/kz4ap/viterbi/config.hpp` | `ViterbiConfig`: every value of §2.10 in physical units, with genie fields |
| `engine/include/kz4ap/viterbi/morse_tree.hpp`, `engine/src/viterbi/morse_tree.cpp` | the tree of §2.6 |
| `engine/include/kz4ap/viterbi/fist.hpp`, `engine/src/viterbi/fist.cpp` | §2.5: ln S and ln P per kind, speed, factor and length |
| `engine/include/kz4ap/viterbi/observation.hpp`, `engine/src/viterbi/observation.cpp` | §2.2–2.3: c(m), ln I₀, Λ, Λ_P, the flat-prior table, the update's moments |
| `engine/include/kz4ap/viterbi/level.hpp`, `engine/src/viterbi/level.cpp` | §2.4 |
| `engine/include/kz4ap/viterbi/search.hpp`, `engine/src/viterbi/search.cpp`, `engine/src/viterbi/text.hpp` | §2.7, the shared text, the statistics |
| `engine/include/kz4ap/viterbi/hiss.hpp`, `engine/src/viterbi/hiss.cpp` | §2.8 |
| `engine/include/kz4ap/viterbi/tracker.hpp`, `engine/src/viterbi/tracker.cpp` | §2.9 |
| `engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp` | `ViterbiDecoder`: tracker, pieces, search, behind `Decoder` |
| `engine/tests/viterbi/*_test.cpp` | one test file per component |
| `bench/src/viterbi_json.hpp`, `bench/src/viterbi_json.cpp` | `--set` names and the statistics file |
| `training/kz4ap_synth/suites.py`, `training/kz4ap_proto/devset2.py`, `training/kz4ap_proto/experiments.py` | N2; parallel runs with names and settings; the new columns, the I2 cut, search errors |

Changes to existing code:
- `engine/include/kz4ap/decoder.hpp`: `virtual void set_noise_density_fs2_per_hz(double)` with an empty default; the
  engine calls it before every block, like the frequency anchor.
- `engine/include/kz4ap/classical_decoder.hpp`: `FrontEnd::Viterbi`. `engine/include/kz4ap/engine.hpp`:
  `EngineConfig::viterbi`. `engine/src/engine.cpp`: a `HissEstimator` fed every spectrum frame when the decoder is
  Viterbi; `open_channel` creates `ViterbiDecoder`.
- `engine/include/kz4ap/channelizer.hpp`: `std::vector<double> channel_filter_taps(const ChannelizerConfig&)`, the
  taps the channelizer builds, so the decoder's c(m) uses the same filter.
- `bench/src/main.cpp`: `--decoder viterbi`; `--set NAME=VALUE` (repeatable, `ViterbiConfig` fields by name, an
  unknown name an error); `--stats FILE`; `--genie fist|frequency|fading` (repeatable; oracle mode only, values from
  the labels).

## 5. Verification, written before any run

1. **Unit tests** of every formula of §2 against an independent computation (Tasks 1–6), including the notes' own
   numbers where the notes derived them: the fist's misfit costs, the level's ±49%/±19%/±10%, the noise variance's
   factor of 1.9–2.1 over independent pieces, the block lengths of topic 4, item 9, and the hiss estimator's bias.
2. **Synthetic stations end to end** (Tasks 4 and 7), each with a stated pass:
   - "PARIS PARIS" at 12, 25 and 40 WPM, S₅₀₀ +20 dB, machine keying: the exact text;
   - a step from 20 to 30 WPM in mid-text at +20 dB: CER at most 0.05 over the text;
   - Farnsworth 18/5 WPM (F = 7.84) at +20 dB: the exact text;
   - 5 s of silence, then "CQ CQ" at 25 WPM, +10 dB: the exact text;
   - a station at +60 dB and one at −10 dB: finite scores; the first's exact text;
   - 5.2 WPM with F = 8 and 100 WPM, +20 dB: CER at most 0.05.
3. **Determinism:** two runs give byte-identical output, and so do inputs split into blocks of 1, 7, 32 and 1 000
   samples (Task 7).
4. **Strong clean signals** (notes, topic 5, item 9): A2's signals at S₅₀₀ 18–20 dB, every speed cell, decoded at the
   minimum beam of 28 nats, give the wide-beam reference's text exactly (R2).
5. **Ten minutes of noise:** every score finite, live stories never above the ceiling, the shared text's memory
   bounded (Task 7).

## 6. The measurements on the development set

### 6.1 The runs

Every run decodes DEV2's recordings through `kz4ap-bench --oracle --decoder viterbi` on the Linux machine, one test
case per process, ten at a time. Each run's rule is written here before it runs; "paired" is the project's paired
comparison per signal with its 95% bootstrap interval (A.11); "better" and "worse" mean that interval lies wholly
below or above zero.

| Run | What it measures | Set | Rule |
|---|---|---|---|
| **R0** | CPU per channel-second, live stories, how often the ceiling decides, newborns' fates; at the defaults and at the reference settings | one recording per group, and one of N2 | Measurement only. **Stop:** the owner confirms §6.3's sizes with R0's costs. |
| **R1** | the wide-beam reference (beam 80 nats, ceiling 17 000); the largest deficit any newborn reaches before it leads, per group | W | If the largest deficit exceeds the 10-nat margin, the margin and the minimum beam are raised to it (notes, topic 5, item 4). |
| **R1b** | the reference's convergence (beam 120 nats, ceiling 34 000) | a quarter of W | R1 counts as converged if it differs from R1b by at most 0.1 per 1 000 letters; otherwise R1 is rerun at R1b's settings. |
| **R2** | search errors at beams 28, 35, 45 and 60 nats; newborns the beam drops within their first element | W | The default beam is the narrowest, never below the minimum, with at most 1 search error per 1 000 letters in every group, not only pooled. If none meets it, the errors are split by cause (beam or ceiling, from the newborns' fates) and brought to the owner. §5, item 4 is checked on the 28-nat run. |
| **R3** | CER against the ceiling: 300, 600, 1 000, 1 700, 3 000; newborns the ceiling removes | W | The default ceiling is the smallest whose paired CER against the 3 000 run is not worse in any group of O6 (the knee); the floor is the largest that is worse in every one. |
| **R4** | the default against the bank, by group and speed cell, crossings and paired differences; CPU and live stories per group; the space error rate per group; false characters per hour on N2 | all of DEV2, and N2 | The main result. I2 above F = 8 left out on both sides and listed separately (O7). |
| **R5** | each rate at 10 times and a tenth: P, then ε (four runs) | N2, D2, G2, H2, a quarter of A2 | Checks, not tuning (notes, topic 5, item 3): false characters per hour (N2), first words (G2, H2), the elements taken to follow D2's steps against false restarts in A2. A value far from the counted rate that scores better is reported as pointing to an error elsewhere, not adopted. |
| **R6** | the fading spread: 0.3, 2 and 5 Hz against R4's 1 Hz; at the best spread, blocks losing 0.25 and 1 dB | B2, a quarter of A2 | The default spread is the one with the lowest B2 CER among those not worse on A2 than the best on A2 (notes, topic 4, item 9). |
| **R7** | the 5% speed grid, 61 values | W | Adopted only with a real advantage (notes, topic 2, item 1): better somewhere, worse nowhere, and the owner judges the gain worth its CPU. |
| **R8** | genie timing: each signal's true speed and fist | C2 | Measurement: the gap per style in dB S₅₀₀ at CER 0.10, the most a better timing model could gain (notes, topic 2, item 6). |
| **R9** | the tracker's frequency error; the decoder with the genie frequency | F2, G2, E2 (neighbors 0–120 Hz away), A2's cells below S₅₀₀ +2 dB | Measurement against Δf · T ≤ 0.26 (§2.9). Per-story frequencies are reconsidered only if turnovers show a real loss (notes, topic 4, item 8). |
| **R10** | genie fading: each signal's true spread | B2 | Measurement: what one fixed spread loses, per spread cell (notes, topic 4, item 9). |
| **R11** | the hiss estimator's error against the generator's noise (σ = 0.02 FS) | every DEV2 recording | Measurement only (no decoding). |

R2 and R3 set the beam and the ceiling before R4 runs. R6 and R7 come after it and change the defaults only
through their rules; if R6 moves the spread or R7 is adopted, R4 is rerun with the new defaults.

### 6.2 The subsets

- **W:** a quarter of the recordings of A2, B2, C2 and I2 (every fourth by name, in each speed cell), and all of D2,
  E2, F2, G2 and H2: about 287 800 channel-seconds, 27% of DEV2, and roughly 280 000 letters (estimated from the
  groups' 100-character signals), so 0.1% means about 280 search errors pooled, and each group of O6 has enough
  letters to tell 0.1% from 1%.
- **A quarter of A2:** every fourth A2 recording: about 104 800 channel-seconds.
- **N2:** 36 000 channel-seconds (D8).

### 6.3 What it costs

One full-run equivalent, all of DEV2 at the default, costs 1 065 742 channel-seconds × c, where c is R0's CPU per
channel-second. The slice gives 9–55 ms on the Linux machine (§2.11), and the prototype costs more. At c = 30 and
80 ms, one FRE is 8.9 and 23.7 CPU-hours: 0.9 and 2.4 hours on the machine's 10 cores.

| Runs | FRE as proposed (D7) | FRE on the whole set |
|---|---|---|
| R1 + R1b (reference cost 3–10× the default) | 1.2–4.1 | 3–10 |
| R2 (4 beams) | 0.6–1.3 | 2.2–4.6 |
| R3 (4 new ceilings) | 0.9–1.3 | 3.3–4.8 |
| R4 | 1.03 | 1.03 |
| R5 (4 runs) | 0.7 | 4 |
| R6 (5 runs) | 1.2 | 5 |
| R7 (cost about 1.9×) | 0.5 | 1.9 |
| R8, R9, R10, R11 | 0.4 | 0.5 |
| **Total** | **about 7–11** | **about 21–32** |

So the proposed sizes take about 60–260 CPU-hours (6–26 hours on the Linux machine), against 190–760 if every run
used the whole set. R0 replaces c and the reference's multiple with measured values before the owner decides.

## 7. Global constraints

- **Signal processing is C++, in the engine** (owner, 2026-10-07 and 2026-10-20). Python only extends the generator
  (N2), the suite runner and the analysis, with their tests.
- **The code compiles with MSVC 14.5x and g++ 11.4** (the Linux machine's): no `std::format`, `<print>` or
  `std::expected`. `ctest` passes in both presets and CI is green. The engine stays free of Qt and of nlohmann/json.
- **`docs/signal-processing.md` describes the engine's signal processing as it actually is.** Any change to signal
  processing — a parameter value, an algorithm, the order of stages, a new stage — updates the body and, where a
  derivation, provenance or limitation changes, the appendix (including the parameter table), in the same commit.
  The body's new section (§8d, the new decoder, in full, at most about 2 000 words) keeps the body under 6 000 words.
  Every implementer is told this rule.
- **Units:** every quantity carries its unit; every dB names its reference (dB S₅₀₀, dB relative to the median bin,
  dB of E/N₀). Parameters are in physical units (Hz, s, FS, dB, nats), converted to pieces or bins only where used.
  Timing medians are in dots; the block length and the level's rate are in seconds, because fading does not scale
  with the sending speed.
- **Determinism:** the same recording and settings give the same output on every run and platform (spec §4.1).
- **Git:** one git command per call, never chained; commit freely on `new-decoder-1`; never push or merge without the
  owner's approval, each time; never rewrite commits; never discard work (stash or a worktree); no attribution
  lines.
- **The Linux machine:** code goes there by git bundle and results come back by scp; long jobs run under the
  machine's job helper; result folders are never deleted or overwritten, and every run gets a new name; before
  starting anything, check that it is not already running. The workflow is in the plan's git-ignored workspace
  (`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/server-workflow.md`).
- **No private infrastructure** in anything committed or pushed: "the Linux machine", with no host, user, path or
  location. The staged diff and the message are checked before every commit.
- **Results:** the rounded per-signal score table of every reported run is committed in
  `docs/plans/data/2026-10-20-new-decoder-prototype-1/`, and the record's numbers are computed from those tables;
  figures follow CLAUDE.md's rule.
- **Deleting:** only files created in the current task.
- **Words:** American spelling; "variant", never "arm"; a decoder is scored on a recording; "front end" means the
  signal detector.

## 8. Review focus

The inputs and conditions most likely to break the decoder that §5 does not already pin, most likely first; each is
tested in the task named.

1. **Long silence at the ceiling:** minutes of noise with the population at 1 700 stories. The shared text must not
   grow without bound, and nothing may overflow (Task 7: ten minutes of noise, the text's node count at most a fixed
   multiple of the ceiling; Task 4: scores relative to the best stay finite).
2. **Extreme levels and the hiss at zero:** a station at +60 dB (z near 10⁵, the flat prior's large-z form, the
   level's variance near 0) and exact zeros before the first sample (N₀ = 0) (Tasks 2, 5 and 7).
3. **Input split anywhere:** a piece straddling two engine blocks, a mark straddling a block boundary, a tracker
   update in mid-block (Task 7: split blocks give identical output).
4. **The grid's ends:** 5.2 WPM dashes of 691 ms (two blocks at 390 ms), word gaps of 13 s at F = 8.14, 100 WPM dots
   of 6 pieces (Task 1: the survival over the whole range; Task 7: both ends decode).
5. **The tracker near noise and a neighbor:** with no station it must stay within its search range, and a station
   20 dB stronger 50 Hz away must not pull it off a weak one (Task 6). Nearer neighbors, whose keying sidebands
   reach into the search range, are measured on E2 (R9).

## 9. Tasks

| Task | What | Depends on |
|---|---|---|
| 1 | Configuration, the Morse tree, the fist | — |
| 2 | The noise model and the evidence | 1 |
| 3 | The level | 2 |
| 4 | The search | 1, 2, 3 |
| 5 | The hiss estimator in the engine | — |
| 6 | The frequency tracker | — |
| 7 | `ViterbiDecoder`; engine and bench; `docs/signal-processing.md`; the results record | 4, 5, 6 |
| 8 | N2, the suite runner, the analysis (Python) | 7 |
| 9 | R0 on the Linux machine; **stop for the owner** | 7, 8 |
| 10 | R1–R3: the reference, the beam, the ceiling | 9 |
| 11 | R4: the default against the bank | 10 |
| 12 | R5–R11 | 11 |

Every task ends with `ctest --preset windows` passing (and `pytest training -q` where Python changed), its
documents updated per §7, and one commit. Tasks 9–12 also build and run on the Linux machine.

### Task 1. Configuration, the Morse tree, the fist

**Files:** create `engine/include/kz4ap/viterbi/config.hpp`, `engine/include/kz4ap/viterbi/morse_tree.hpp`,
`engine/src/viterbi/morse_tree.cpp`, `engine/include/kz4ap/viterbi/fist.hpp`, `engine/src/viterbi/fist.cpp`,
`engine/tests/viterbi/morse_tree_test.cpp`, `engine/tests/viterbi/fist_test.cpp`; modify `engine/CMakeLists.txt`.

**Interfaces (produced):**
- `struct ViterbiConfig` with the fields of §2.10 in their units (`beam_nats`, `ceiling`, `fading_spread_hz`,
  `block_loss_db`, `over_rate_per_s`, `rescue_epsilon`, `word_end_p`, `over_end_q`, `level_min_db`, `level_max_db`,
  `fist_sigma`, `fist_medians_dots[5]`, `quadrature_order`, `hiss_half_width_hz`, `hiss_percentile`, the tracker's
  fields, `stats`, and the genie fields), and `double block_length_s(double spread_hz, double loss_db)`.
- `class MorseTree`: `int root() const`, `int child(int node, bool dash) const` (−1 if none),
  `std::string_view symbol(int node) const` (empty if none), `bool has_children(int node) const`,
  `std::size_t size() const`.
- `enum class Kind { Dot, Dash, ElementGap, LetterGap, WordGap }`; `class Fist`:
  `Fist(const ViterbiConfig&)`, `double log_survival(Kind, int speed, int factor, int tau) const`,
  `double log_end(Kind, int speed, int factor, int tau) const` (τ in pieces, ≥ 1).

- [ ] **Step 1: Failing tests.**
  - `MorseTree.HoldsTheEngines62Symbols`: every symbol of `kz4ap::morse` is reached by its pattern from the root and
    is the node's symbol; 62 symbol nodes; the root holds none.
  - `MorseTree.MarksOutsideTheTreeHaveNoNode`: `child` is −1 below every node with no longer symbol.
  - `Fist.EndProbabilitiesSumToOne`: Σ_τ e^{log_end} = 1 within 10⁻⁹ for each kind at speeds 0 and 31 and factors 0
    and 22, τ up to 40 times the median.
  - `Fist.SurvivalIsTheRoundedLogNormal`: `log_survival(Dot, 0, 0, τ)` = ln Φc((ln(τ − ½) − ln 6)/0.3) within 10⁻⁶
    for τ = 1 … 60 (dot of 6 pieces at speed 0).
  - `Fist.FarnsworthStretchesOnlyLetterAndWordGaps`: changing the factor changes only those two kinds; a word gap at
    speed 31, factor 22 has median 6 562 ± 1 pieces.
  - `Fist.MisfitCostsMatchTheNotes`: at speed 31 (dot median 115.17 pieces), ln P(230) − ln P(115) =
    −(ln 2)²/0.18 − ln 2 = −3.36 nats and ln P(345) − ln P(115) = −(ln 3)²/0.18 − ln 3 = −7.80 nats, each within
    0.05: the notes' misfit cost (ln k)²/(2σ²) plus the log-normal density's 1/length factor.
  - `Config.BlockLengthFollowsTheSpread`: `block_length_s` gives 78.07, 195.19, 390.37 and 1 301.25 ms at 5, 2, 1 and
    0.3 Hz for 0.5 dB, within 0.5% (computed 2026-10-20; the notes round them to 78 ms, 195 ms, 390 ms and 1.3 s,
    topic 4, item 9).
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.** ln Φc from a table on z in [−8, 40] with a step of 0.001 and linear interpolation, or
  `std::erfc` where the table ends.
- [ ] **Step 4: Run them; they pass.** `ctest --preset windows`.
- [ ] **Step 5: Commit.**

### Task 2. The noise model and the evidence

**Files:** create `engine/include/kz4ap/viterbi/observation.hpp`, `engine/src/viterbi/observation.cpp`,
`engine/tests/viterbi/observation_test.cpp`; modify `engine/include/kz4ap/channelizer.hpp` and
`engine/src/channelizer.cpp` (`channel_filter_taps`, the existing code calling it).

**Interfaces:**
- Consumes `ViterbiConfig`.
- Produces:
  - `std::vector<double> sum_noise_hz(std::span<const double> taps, double input_rate_hz, int decimation, int max_samples)`:
    c(m) for m = 1 … max_samples, in Hz;
  - `double ln_i0(double x)`;
  - `double block_llr(double s_abs, int m, double a, double v)`;
  - `double block_llr_uncertain(double s_abs, int m, double mu, double p, double v, int order)`;
  - `struct Moments { double mean, var; }`, `Moments level_posterior(double s_abs, int m, double mu, double p, double v, int order)`;
  - `class FlatPrior { FlatPrior(const ViterbiConfig&, std::span<const double> c, int max_pieces); double llr(int pieces, double z) const; Moments posterior(int pieces, double z) const; }`,
    the moments in units of √V(m)/m (the caller scales to FS).

- [ ] **Step 1: Failing tests.**
  - `Observation.SumNoiseMatchesNoiseThroughTheChannelizer`: 600 s of complex white noise, σ = 0.02 FS, at 48 kHz
    (DEV2's rate) and at 192 kHz, through `Channelizer`: the variance of sums of 3, 24 and 585 channel samples equals
    N₀ · c(m), N₀ = σ²/fs, within 2%, with c(m) computed from that rate's taps.
  - `Observation.LongSumsGrowAt1500Hz`: c(m)/m within 0.5% of 1 500 Hz for m ≥ 300.
  - `Observation.IndependentPiecesUnderstateTheNoise`: V(m) / ((m/3) · V(3)) lies in [1.9, 2.1] for marks of 8 to
    720 ms (notes, topic 4, item 7).
  - `Observation.LnI0IsAccurate`: error at most 10⁻⁶ nats against `std::cyl_bessel_i` for x in [0, 700] and
    against the asymptotic series above.
  - `Observation.QuadratureMatchesADenseIntegral`: `block_llr_uncertain` within 0.01 nats of a 20 001-point
    integral over a, for b in {1, 3, 10, 30}, z in {0, b/2, b, 2b}, relative spread in {0.03, 0.1, 0.3, 0.5}.
  - `Observation.ReproducesTheNotesExample`: in the notes' units (independent pieces), a 20 WPM dot at S₅₀₀ 10 dB,
    10 dB below the stored level: 1 107 nats better as noise with the level exact, and 271 nats better as a mark
    with the level uncertain by ±10% (notes, topic 3, item 1), each within 1%.
  - `Observation.UpdateIsTheKalmanGainAtHighSnr`: for b ≥ 30 and relative spread 0.1, the posterior mean equals
    μ + P/(P + V/(2m²)) · (|S|/m − μ) within 1%.
  - `Observation.FlatPriorTableMatchesDirectIntegration`: at 20 points (n, z), within 0.01 nats; and the large-z
    form meets the table within 0.01 nats.
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.** The flat-prior table on z from 0 to 40 in steps of 0.01, per n; the integral over the
  level x in steps of 0.01 dB.
- [ ] **Step 4: Run them; they pass.**
- [ ] **Step 5: Commit.**

### Task 3. The level

**Files:** create `engine/include/kz4ap/viterbi/level.hpp`, `engine/src/viterbi/level.cpp`,
`engine/tests/viterbi/level_test.cpp`.

**Interfaces:** consumes Task 2's `level_posterior` and `FlatPrior`. Produces `struct Level { double mu, p, t_s; bool learned; }`,
`Level predict(const Level&, double t_s, double spread_hz)`, `Level update(const Level&, double s_abs, int m, double v, int order)`,
`Level start(const FlatPrior&, double s_abs, int pieces, double v, double t_s)`.

- [ ] **Step 1: Failing tests.**
  - `Level.PredictionGivesTheNotesUncertainties`: from P = 0, after two dots at f_D = 1 Hz, √P/μ is 0.49, 0.19 and
    0.10 (±0.01) at 5, 20 and 40 WPM.
  - `Level.NoSpreadKeepsTheVariance`: f_D = 0 leaves P unchanged over any time.
  - `Level.OneDotMovesTheLevel`: the notes' dot (10 dB below a level known to ±10%) moves the estimate at least 95%
    of the way to its own level (the notes give 98% for the linear gain).
  - `Level.ANewbornStartsFromItsFirstBlock`: after one block at z = 20 the level's mean is within 2% of z·√V/m.
- [ ] **Step 2: Run them; they fail.** **Step 3: Implement.** **Step 4: They pass.** **Step 5: Commit.**

### Task 4. The search

**Files:** create `engine/include/kz4ap/viterbi/search.hpp`, `engine/src/viterbi/search.cpp`,
`engine/src/viterbi/text.hpp`, `engine/tests/viterbi/search_test.cpp`; extend `engine/tests/test_signals.hpp` with
Farnsworth keying and a speed step.

**Interfaces:**
- Consumes `MorseTree`, `Fist`, Task 2's functions, `Level`.
- Produces `class Search`: `Search(const ViterbiConfig&, std::span<const double> c)`,
  `void step(std::complex<double> piece_fs, double t_s, double n0_fs2_per_hz)`,
  `std::vector<DecodedSymbol> best_text() const`, `const SearchStats& stats() const`. `SearchStats` holds the live
  count per piece (a histogram and a 0.1 s series), pieces where the ceiling decided, and, per newborn line (a
  newborn and its descendants), its birth time and kind, whether and when it led, its largest deficit before
  leading, and which stage removed its last story (beam or ceiling) and whether within its first element.

- [ ] **Step 1: Failing tests** (pieces built from `keyed_signal` through `Channelizer`, re-centered by the true
  frequency).
  - `Search.DecodesACleanStationAtThreeSpeeds` (§5, item 2).
  - `Search.FollowsASpeedStep`, `Search.FarnsworthTextIsRight`, `Search.AnOverStartsAfterSilence` (§5, item 2).
  - `Search.MergesIdenticalStates`: two successors in one state leave one, the better.
  - `Search.PrunesByBeamThenCeiling`: at ceiling 10, never more than 10 live; every survivor within the beam.
  - `Search.BooksEveryPointAtEveryPiece`: a story τ pieces into a dash has paid exactly ln S_dash(τ) and its blocks'
    evidence so far.
  - `Search.NoiseKeepsScoresFinite`: 60 s of noise at the defaults: every score finite, the best relative score 0.
  - `Search.IsDeterministic`: two runs give identical texts and statistics.
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement** §2.7 in its order.
- [ ] **Step 4: Run them; they pass.** Note the CPU per piece on one test for Task 9.
- [ ] **Step 5: Commit.**

### Task 5. The hiss estimator in the engine

**Files:** create `engine/include/kz4ap/viterbi/hiss.hpp`, `engine/src/viterbi/hiss.cpp`,
`engine/tests/viterbi/hiss_test.cpp`.

**Interfaces:** produces `class HissEstimator`: `HissEstimator(int sample_rate, int fft_size, const ViterbiConfig&)`,
`void push(const SpectrumFrame&)`, `double noise_density_fs2_per_hz(int center_bin) const` (0 before any frame or on
exact zeros), and `double bias(int frames) const`.

- [ ] **Step 1: Failing tests.**
  - `Hiss.MeasuresTheGeneratorsNoise`: white noise, σ = 0.02 FS at 48 kHz (DEV2's rate; and at 192 kHz): from 1 s
    on, the mean estimate over
    100 channels within ±1% of σ²/fs, and their rms deviation from it at most 2.5% (the notes: ±1.1% for
    independent bins, about 40% more for the detector's overlapping ones).
  - `Hiss.BiasMatchesSimulation`: `bias(n)` within 0.5% of the simulated ratio (171 bins, 2 000 frames) at n = 1, 10,
    47 and in steady state.
  - `Hiss.ToleratesNeighbors`: four keyed stations at S₅₀₀ +30 dB within ±2 kHz: the estimate at most 5% high.
  - `Hiss.ExactZerosGiveZero`: zero input gives 0, never NaN.
- [ ] **Step 2–4: fail, implement, pass. Step 5: Commit.**

### Task 6. The frequency tracker

**Files:** create `engine/include/kz4ap/viterbi/tracker.hpp`, `engine/src/viterbi/tracker.cpp`,
`engine/tests/viterbi/tracker_test.cpp`.

**Interfaces:** produces `class CarrierTracker`: `CarrierTracker(double rate_hz, const ViterbiConfig&, double start_offset_hz)`,
`void push(std::span<const Sample>, double t0_s)`, `double offset_hz(double t_s) const`.

- [ ] **Step 1: Failing tests.**
  - `Tracker.HoldsAKeyedCarrier`: PARIS at 25 WPM, S₅₀₀ 0 dB, true offset +7.3 Hz, start +5 Hz: error at most
    0.2 Hz from 3 s on.
  - `Tracker.FollowsDrift`: 1 Hz/s for 20 s at S₅₀₀ +10 dB: error at most 0.67 Hz from 3 s on.
  - `Tracker.StaysPutOnNoise`: noise only for 60 s: within ±15 Hz of the start, and at most 2 Hz from it rms.
  - `Tracker.RelocksAtATurnover`: a station, 1 s of silence, then another 8 Hz away: error at most 0.67 Hz within
    2 s of the second's first mark.
  - `Tracker.ResistsAStrongNeighbor`: a station at S₅₀₀ 0 dB and one 20 dB stronger 50 Hz away: error on the weak
    one at most 0.67 Hz (Review focus 5).
- [ ] **Step 2–4: fail, implement, pass. Step 5: Commit.**

### Task 7. `ViterbiDecoder`; engine and bench; documents

**Files:** create `engine/include/kz4ap/viterbi_decoder.hpp`, `engine/src/viterbi_decoder.cpp`,
`engine/tests/viterbi_decoder_test.cpp`, `bench/src/viterbi_json.hpp`, `bench/src/viterbi_json.cpp`,
`bench/tests/viterbi_json_test.cpp`; modify `engine/include/kz4ap/decoder.hpp`, `engine/include/kz4ap/classical_decoder.hpp`,
`engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`, `bench/src/main.cpp`, `bench/CMakeLists.txt`,
`docs/signal-processing.md` (overview, §8 "Which decoder runs", new §8d in full, §9's table; appendix A.8d with the
exact form, A.10 rows, A.11 definitions of a search error and of live stories), `README.md` (the decoder list; the
Linux compiler line, "GCC 13+", becomes "GCC 11.4 or later", the compiler the Linux machine builds and tests with);
create the results record with its §1, conditions.

**Interfaces:** produces `class ViterbiDecoder final : public Decoder`: `ViterbiDecoder(double rate_hz, const ViterbiConfig&, double residual_hz)`;
`process`, `flush`, `reset`, `set_frequency_anchor_hz`, `set_noise_density_fs2_per_hz`; `const SearchStats& stats() const`.
The bench: `--decoder viterbi`, `--set`, `--stats`, `--genie`; the statistics file's format is fixed in
`viterbi_json.hpp`.

- [ ] **Step 1: Failing tests.**
  - `ViterbiDecoder.DecodesThroughTheEngine`: four stations at S₅₀₀ +15 dB, 25 WPM, oracle: each text exact.
  - `ViterbiDecoder.InputSplitDoesNotMatter`: blocks of 1, 7, 32 and 1 000 samples give identical output (§5, item 3).
  - `ViterbiDecoder.ExactZerosThenAStation`: 2 s of exact zeros, then a station: no failure; its text decoded.
  - `ViterbiDecoder.ExtremeLevels` and `ViterbiDecoder.TheGridsEnds` (§5, item 2).
  - `ViterbiDecoder.TenMinutesOfNoise` (§5, item 5; Review focus 1).
  - Bench: `--set beam_nats=30` appears in the JSON's configuration; an unknown name fails; `--genie` without
    `--oracle` fails; the statistics file round-trips.
- [ ] **Step 2: Run them; they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Documents** (§7's rule), in the same commit.
- [ ] **Step 5: Run** `ctest --preset windows` and `bench/smoke.sh`; build with `cmake --preset linux` on the Linux
  machine and run `ctest --preset linux`.
- [ ] **Step 6: Commit.**

### Task 8. N2, the suite runner, the analysis

**Files:** modify `training/kz4ap_synth/suites.py` (group N2; `viterbi` among the decoders; `run --jobs N --name NAME
--set NAME=VALUE --stats`), `training/kz4ap_proto/devset2.py` and `training/kz4ap_proto/experiments.py` (the columns
`farnsworth_wpm` and `farnsworth_factor` at 0.01; `--max-farnsworth`; `search-errors --run A --reference B`; a
statistics summary per group); tests in `training/tests/`.

- [ ] **Step 1: Failing tests.**
  - `test_n2_noise_group`: 4 recordings at 48 kHz (DEV2's rate, σ = 0.02 FS) of 25 channels × 360 s, stations at
    S₅₀₀ −60 dB, reproducible by seed.
  - `test_run_suite_runs_in_parallel_with_a_name_and_settings` (with a stand-in bench).
  - `test_score_tables_carry_the_farnsworth_columns`, and older tables still read.
  - `test_the_i2_cut_leaves_out_the_same_signals_on_both_sides`.
  - `test_search_errors_count_edits_between_two_runs_per_1000_letters`.
- [ ] **Step 2–4: fail, implement, pass** (`pytest training -q`).
- [ ] **Step 5: Re-export `d5-bank.csv`** with the two columns, its other columns unchanged (checked).
- [ ] **Step 6: Commit.**

### Task 9. R0 on the Linux machine; stop for the owner

- [ ] **Step 1:** bundle the branch to the Linux machine; build; `ctest --preset linux`; generate N2.
- [ ] **Step 2:** run R0 at the defaults and at the reference settings, with `--stats`.
- [ ] **Step 3:** write the results record's §2: CPU per channel-second per group (and the desktop's, for
  comparison), live stories, the ceiling's share of pieces, newborns' fates, the reference's cost multiple; §6.3's
  table with the measured values; commit.
- [ ] **Step 4: Stop.** The owner confirms the sizes (D7) before Task 10.

### Task 10. R1–R3: the reference, the beam, the ceiling

- [ ] **Step 1:** R1 and R1b; their rules (§6.1). **Step 2:** R2; its rule; §5, item 4. **Step 3:** R3; its rule.
- [ ] **Step 4:** the adopted defaults into `ViterbiConfig`, `docs/signal-processing.md` (§8d, A.10) and the
  record, with the score tables; commit.

### Task 11. R4: the default against the bank

- [ ] **Step 1:** R4 on DEV2 and N2. **Step 2:** the analysis against `d5-bank`, I2 cut per O7. **Step 3:** the
  record's main section, with at most three figures, its score tables committed; commit.

### Task 12. R5–R11

- [ ] **Step 1:** each run with its rule (§6.1), one record section each. **Step 2:** adopted values, documents and
  tables; commit. **Step 3:** the record's conclusions and the owner's decisions it raises.
