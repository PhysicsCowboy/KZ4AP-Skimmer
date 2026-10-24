# New decoder: the first prototype

> **Status: approved through its decisions (owner, 2026-10-20); building starts.** The owner does not read this
> plan: it is the executors' document, and every decision is brought to him in chat, in words. **His answers
> (2026-10-20):** D1 yes, a new branch; D2 `viterbi`; D3, D4, D5 and D6 "whatever we've already decided": the
> design notes' decisions, implemented as §2 and §3 state them, where a starting value only sets where its
> measurement begins (D5: no tail until measured); D7 (c), the whole development set for every measurement; D8 (a),
> the noise-only group; D9 subagent-driven for the building tasks (1–8). For the measuring tasks (9–12) the
> controller launches and watches the runs, and a fresh agent verifies each results section (numbers recomputed
> from the committed tables, the rule applied as written) before it goes to the owner: the controller's proposal of
> 2026-10-20, in force unless he objects.
>
> **Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.1 (the Classical mode), §5.2 step 1 and step 3
> (amendments of 2026-10-19) and §4.1 item 3. **Design:** `docs/design/2026-10-09-new-decoder-design-notes.md`, the
> authority for every design choice, cited here as "notes, topic N, item M". **Evidence:**
> `docs/research/2026-10-14-new-decoder-review-1.md` (review 1) and `docs/research/2026-10-14-standard-practice-check/`.
>
> **Branch:** `new-decoder-1`, from `bell-stage-1` (decision D1).
>
> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development (the owner's choice, D9).
> Steps use checkbox (`- [ ]`) syntax for tracking.

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

**The mark's rise and the pieces' noise** (D10, owner, 2026-10-20; Task 4b, second design, as built). Two changes
replace V(m) and the plain sum in the evidence:
1. **Pieces are scored as independent at the noise's long-run rate.** A block's sum has the variance
   4 500 Hz × N₀ per piece (1 500 Hz × N₀ per channel sample; the R[k] add up to 1 500 Hz), instead of N₀ · c(m).
   - This is exact for long blocks. It removes the first-piece bonus: c(3) = 2 104.66 Hz made a block's first full
     piece earn |S|²/V = 213.8 at +20 dB S₅₀₀ against 100.0 per piece later (derived), because a block's noise was
     counted apart from its correlation with the samples before it.
   - Cost: a 6-piece mark (a 100 WPM dot) is credited about 7% less than c(m) would credit it in isolation (derived).
2. **A mark's first block is the matched filter to the rise.** S_w = Σ_j g_j x_j, with E[S_w] = 3a Σ g_j² and
   Var S_w = 4 500 Hz × N₀ × Σ g_j², so b = 3a √(Σ g_j²) / √(4 500 Hz × N₀). Later blocks keep g = 1.
   - The fractions g_1 … g_J are the expected part of a full piece's carrier sum in a mark's first J pieces:
     - the keying edge, a raised cosine of `keying_rise_s` = 5 ms (heuristic placeholder, the generator's), passed
       through the channelizer's taps and decimation and summed into pieces (derived);
     - averaged over the edge's position within a piece (derived: the expectation for an edge placed uniformly);
     - from the first piece at 0.01 of full or more to the last piece more than 0.005 from full (both heuristic).
   - At 48 kHz this gives J = 4: 0.1934, 0.6137, 0.9427, 1.0192 (the last is the filter's overshoot; measured
     against the real channelizer within 1.6 × 10⁻⁷).
- A newborn's first block (item 3) uses the same statistic: b² = 10^{x/10} · Σ g_j² (derived).
- The fall at a mark's end is not modeled. By then the story's level is learned from the mark itself, so a weak last
  piece only costs the story that keeps it (derived).
- Why (measured, Task 4 and Task 4b):
  - The approved design's piece-aligned rectangle gave the story that starts its mark on a part-amplitude first piece
    a one-piece lead of about 25 nats at +20 dB S₅₀₀. That narrowed the 40-nat beam to about 8 nats at every letter
    start, and the speed step failed at +20 to +24 dB S₅₀₀.
  - The rise in the expected sum alone failed too: a newborn's scale-free prior escaped it, and the unweighted sum
    still rewarded skipping weak pieces.
  - With both changes the step decodes exactly from +11 to +28 dB S₅₀₀ at beams of 40 and 80 nats, and constant-speed
    stations at all 57 points.
  - The one remaining failure, +10 dB S₅₀₀ on one seed, is the ceiling's: rescues born in a word gap are dropped by
    the 1 700-story ceiling (pending the owner).

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
   ceiling remain, the ceiling's best are kept, ties broken by the state, then by creation order (so a run never
   depends on a container's order or an address). Every successor with a finite score is created first: there is no separate
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
| Keying edge, for the mark's rise | raised cosine, 5 | ms | placeholder, heuristic: the generator's edge (D10, §2.3) |
| Speeds | 12.0 × 1.1^s, s = 0 … 31 | ms per dot | owner (notes, topic 2, item 1) |
| Farnsworth factor | none: standard spacing only (D14, 2026-10-21; the grid 1.1^f, f = 0 … 22, removed) | — | owner (notes, topic 2, item 3, amended) |
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
- **Decided (owner, 2026-10-20): (c), the whole of DEV2 for every measurement**, with the noise-only group (D8)
  where a check needs it. The reference's convergence check (R1b) covers a quarter of DEV2, as costed in that option
  (on the whole set it alone would add 6–20 full-run equivalents, for a check of the method). The genie and tracker
  runs cover the groups they concern, in full. About 23–37 full-run equivalents in all (§6.3). The options put to
  him were (a) the whole set only for R4 and the subset W elsewhere (7–11), (b) the whole set also for R1–R3
  (14–28), (c) the whole set for everything.

**D8. A noise-only group, N2:** 100 channels × 360 s (10 channel-hours) of white noise, with labeled stations at
S₅₀₀ −60 dB, so that the channels open and the decoder's false characters per hour are counted (step 1, P5(h), in
the standard-practice check). DEV2 has no such group. **Decided (owner, 2026-10-20): (a), added.**

**D9. How the plan is executed. Decided (owner, 2026-10-20): subagent-driven** for the building tasks (a fresh
implementer per task and a fresh reviewer after each, then a review of the whole branch). The measuring tasks: the
controller launches and watches the runs (hours each), and a fresh agent verifies each results section before it
goes to the owner (the controller's proposal, in force unless he objects).

**D10. The mark's rise. Decided (owner, 2026-10-20): (B), model it before the long runs.**

*The problem.* The search's speed-step criterion (§5, item 2) failed at the defaults: "PARIS PARIS PARIS P<AR>5
P<AR>IS PARIS", CER 0.143 (Task 4, measured).
- The keying's edge and the channel filter put a mark's first piece at part of the full amplitude (0.0267 FS against
  0.0615 FS).
- The story that starts its mark there can explain the piece only as a fade. Under a level about 20% uncertain after
  a 124 ms gap, it leads by about 25 nats for that one piece; the story starting one piece later then leads by 27–34
  nats.
- The one-piece lead raises the best score at every letter start, leaving any reading behind the leader about 8 nats
  of the 40-nat beam. The rescue at the new speed died 7.66 nats behind.
- The step failed at +20 to +24 dB S₅₀₀ with the 40-nat beam and at +23 to +25 dB with 80 nats. A ceiling of 17 000
  changed nothing. Constant-speed stations decoded exactly from +19 to +24 dB.
- DEV2's speed-change group lies at +10 to +20 dB S₅₀₀, so measuring the design as it stood would barely have shown
  the flaw.

*The standard.* The matched filter to the pulse as received: the keyed envelope passed through the receiver's filter
(Proakis and Salehi §4.5-1). The notes' "edges fall where the hypothesis puts them (to one piece); there are no
filter delays" (topic 4, item 2) had been costed by review 1 and the standard-practice check only as an SNR loss
(0.37 dB). Neither looked at the beam's comparisons.

*The options put to the owner:*
- (A) keep the design and measure it, the test a known failure;
- (B) model each mark's rise, §2.3 (recommended);
- (C) change the test's conditions.

*(B) chosen.* It is measured first on the speed step from +10 to +28 dB S₅₀₀ and on constant-speed stations (Task 4b).
The owner hears the result if it does not remove the failing band.

*Outcome, and the second round (owner, 2026-10-20).*
- **The first version failed (measured).** It put the rise into the expected sum only. The band moved (+18 to
  +23 dB S₅₀₀ at 40 nats), and constant-speed stations lost exactness at 11 points. The reasons:
  - a newborn's scale-free prior escaped the rise;
  - every block's first piece earned about twice its share, because the noise model ignored its correlation with
    the samples before it;
  - the unweighted sum rewarded starts that skip weak pieces.
- **The owner asked whether a mark's first pieces could simply be ignored.** They cannot: ignoring moves where
  counting starts, and the story that starts counting first jumps ahead of those still waiting.
- **He chose to keep fixing the problem, with no fallback to the approved design.** The second version, the pieces
  at the noise's long-run rate and the matched filter to the rise (§2.3), removes the band (measured; §2.3) and is
  built.
- **The remaining failure,** at +10 dB S₅₀₀ on one seed, comes from the ceiling. It is put to the owner as
  (a) a newborn's first mark exempt from the ceiling, (b) a ceiling of 2 500, or (c) left to R3.

**D11. The ceiling at the speed step's last failing point. Decided (owner, 2026-10-21): (c), left to R3.** The +10 dB
S₅₀₀ seed-12 point stays a known failure until R3 sets the ceiling. The options were:
- (a) a newborn's first mark exempt from the ceiling;
- (b) a ceiling of 2 500, measured to fix it at 42–50% more CPU;
- (c) left to R3.

**D12. The tracker's two remaining failures. Decided (owner, 2026-10-21): (B), then (A).**
- The failures (Task 6b, 1 000 seeds): a weak station beside one 20 dB stronger 50 Hz away passed 43%; holding a keyed
  carrier passed 94.8%.
- **(B):** four standard steps, each kept only if measured to help:
  - association inside the acceptance window first;
  - confirmation from windows that share no samples;
  - a larger uncertainty for a station's first, partly filled window;
  - each measurement's uncertainty corrected for keyed carriers and for the correlation between windows.
- **(A):** whatever still fails stays a known failure, measured in R9.
- **Not the owner's decision: the trust tolerance.** It was first ruled while building to follow the block length,
  0.26 / T_block. The tracker's review then measured that this makes tracking worse wherever it acts: below about a
  0.57 Hz spread a keyed station at 0 dB S₅₀₀ is never confirmed. So it was fixed at 0.667 Hz, the default block's
  value, at every spread (decided 2026-10-21 from those measurements, and reported to the owner). The decoder's
  stricter need at longer blocks is a stated limitation, measured in R6.

**D13. Very strong stations. Decided (owner, 2026-10-21): (A), model the fall.**
- Task 7a found that stations above about +37 dB S₅₀₀ decode wrongly: the unmodeled fall leaves the tail's evidence for
  a one-piece over to claim.
- Task 4c modeled the mark's fall as the other half of the matched filter. It did not fix the strong stations and broke
  the speed step at +21 to +28 dB S₅₀₀ (measured), so it was set aside; D15 replaced it.

**D14. Farnsworth spacing. Decided (owner, 2026-10-21): removed from the first prototype.**
- **Why** (Task 7a and its review, measured): the flat prior over 23 factors cost every reading of standard code 3.1 nats
  at its first letter gap, which a reading that merges letters avoids, so "TEST" sent alone decoded as "-". The
  factor's branching was also the search's largest CPU cost.
- **The owner:** "we eliminate the Farnsworth factor entirely for now and allow any Farnsworth decoding to fail until a
  future fix, recording somewhere what we've discovered and our thoughts for how to fix it."
- **What changes** (Task 4d):
  - The factor is removed from the decoder: no factor in a reading's state, no factor grid, no open-factor tables or
    scan. Letter and word gaps have medians of 3 and 7 dots.
  - The owner asked why a run would want the 23-factor code. No measurement here needs it, and the branch's history
    keeps it for the later fix.
  - The Farnsworth tests become known failures; TheGridsEnds' slow end is sent at standard spacing.
- **What is recorded:** the findings and the thoughts for a fix are in `docs/backlog.md`, "Farnsworth spacing in the
  new decoder"; the design notes' topic 2, item 3 is amended.
- **Consequence for the measurements:** DEV2's Farnsworth groups (I, I2) are expected to decode poorly. In R4 all of
  I2 is reported apart from the comparison with the bank, as a known gap of this prototype, and O7's cut at F = 8 is
  moot.

**D15. Very strong stations, after the fall. Decided (owner, 2026-10-21): a systematic term in the error budget.**
- Modeling the mark's fall (D13, Task 4c) did not fix stations above about +38 dB S₅₀₀ (measured), and it broke the
  speed step at +21 to +28 dB S₅₀₀. It is set aside, not committed.
- The cause: the decoder's error budget had a statistical term, the receiver's noise, and no systematic term. At
  high SNR a pulse-model mismatch of a percent reads as overwhelming evidence (about 100 nats at +60 dB S₅₀₀, against a
  new over's 34 nats). The mismatches are the edge's position within a piece and the filter's ringing.
- The owner: "how this decoder can be failing on strong stations, period. They should be the ones that have the least
  trouble with anything, period."
- **The remedy (Task 4e), the standard one:** the model's systematic uncertainty is added in quadrature to the
  noise in each piece's error budget. It is proportional to the carrier's amplitude, with a relative size from the
  template's spread over edge positions plus a floor (`systematic_floor`, placeholder 0.02). Evidence then levels off
  at what the model supports.
- **Terminology (owner, 2026-10-21):** this term is a systematic effect, not noise, and is never called noise.
- Measured first in a copy, on strong stations to +60 dB S₅₀₀ and on everything that passes now.
- **Outcome (Task 4e, measured; commit 36084bc).** The strong stations now decode exactly at every one of 16
  keying-edge positions from +30 to +60 dB S₅₀₀; before, 16, 16, 13, 10, 2, 0 and 0. Ordinary levels are unchanged and
  the CPU is the same. Three details were settled by measurement:
  - **The carrier power** in the term comes from the data, never from a story's level: the larger of the channel's
    largest piece power over the last 390 ms and the block's own estimate. The block's own estimate depends on where a
    story claims its mark began, and can only raise its variance. A story's own level fails, because a story can then
    lower its own uncertainty.
  - **A 4 ms look-ahead** keeps the filter's pre-ringing from reading as a weak mark.
  - **A 2% floor** on the relative error (`systematic_floor`, heuristic).
- **A neighbor check** with a stronger neighbor keying throughout found the term costs nothing. It also measured that the
  decoder loses a weak station to an equally strong neighbor 40–70 Hz away, with or without the term; the plan's O2
  (no interference model) stands. See `docs/backlog.md`.
- **Then Task 4e's review measured a case the check missed.** A strong neighbor that stopped 0.15–0.30 s before a weak
  station started still set the station's carrier power through the 390 ms memory. 39 of 72 runs were exact with the
  term, against 72 of 72 without it.
- **A ruling made while building:** the carrier power must come from the station's own power, a narrowband estimate
  or a shorter memory, whichever passes every test. Being measured (Task 4e, fix round 1).
- **Neither passed** (fix round 1, measured): a narrowband estimate and a shorter memory each broke strong stations.
  **Decided (owner, 2026-10-21): keep the term as 36084bc has it, with its trades recorded as limitations** (commits
  4813600, b229b79 and 2160978): a neighbor 30 to 40 dB stronger (dB relative to the station's key-down power) that
  stops 0.10 to 0.30 s before a weak station starts costs the station its exact text in 194 of 288 runs; under Rayleigh
  fading the term is better in 242 of 360 runs and worse in 29. Tables in
  `docs/plans/data/2026-10-20-new-decoder-prototype-1/`.

**D16. Fading: model the change in gain. Decided (owner, 2026-10-21): the direction; the design's details come to the
owner before any code.**
- **The finding (Task 4e's fading check and diagnosis, measured).** At a fading spread of 2 Hz no run decoded exactly
  from +10 to +60 dB S₅₀₀, with or without the systematic term; at 1 Hz few did. Three causes: the gain moves inside a
  block, which the decoder scores as one constant gain (the misfit grows with the signal's energy, while a wrong reading
  costs a fixed 20 nats or so); the level's prediction across gaps scales its spread by the current level instead of the
  station's long-run power; and deep fades, partly beyond any decoder.
- **The owner (2026-10-21):** "Yes but don't do the cheap solutions" (on fixing only the power estimate), then "I didn't
  know we were doing anything with blocks, and I don't even understand what their purpose was in the first place,
  period. It seems to me that we have to model the change in gain."
- **The direction:** each story tracks the station's complex gain, amplitude and phase, with a Kalman filter on a model
  of the fading, and scores each piece by the filter's prediction (the estimator–correlator; per-survivor processing;
  the standard answer of the 2026-10-14 standard-practice check, P1.4 and P1.5). It replaces the blocks and the
  level's prediction (design notes, topic 3 item 1 and topic 4 item 9, which record blocks as decided on 2026-10-15; the
  owner did not know of them, so that decision is reopened), and may reopen per-story frequency (topic 4, item 8).
- **Next:** a design amendment (the fading model's order, an unknown fading rate, the station's unknown long-run power,
  the leftover frequency error, the systematic term, cost), reviewed against standard practice; each decision to the
  owner in chat; then a prototype in a copy, measured against today's decoder, before anything enters the branch. The
  long runs wait.
- **The design's decisions (owner, 2026-10-22),** on the draft amendment
  (`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/fading-design-draft.md`, reviewed three times):
  - **The model's order: second order** (the gain and its rate of change, the Butterworth shape fitted to the Gaussian
    spectrum); higher orders a measured variant.
  - **The carrier's phase across key-up: both assumptions inside each story** (continuous, and restarted at key-down),
    chosen by the evidence.
  - **Clicks: left a known limitation** for later, with the interference work (backlog); no click model now.
  - **The systematic term** carries on as decided in D15, per piece, with both parts of C²; not a new decision. The
    length of the channel peak's memory (390 ms today, 16 ms proposed) is measured in the prototype.
  - **Still open, one at a time:** the unknown fading rate (the owner asks why a per-story estimate is not tried
    first), the station's unknown long-run power (the owner: the 6.25 dB grid is "too arbitrary"), and the leftover
    frequency error (the owner: how can a fading model absorb a detuning under coherent detection?).
- **How decisions reach the owner (owner, 2026-10-22):** one decision at a time, and he gives one answer.
- **Superseded the same day: standard practice, not coherent detection (owner, 2026-10-22).** The owner: "Are we perhaps
  crazy to be trying to do phase-sensitive detection for Morse code? ... Should we be doing amplitude detection
  instead?", then "We want *standard practice*!". A blind survey
  (`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/detection-standard-practice.md`) found that every working Morse
  decoder detects noncoherently, behind a filter 30 to 100 Hz wide, tracks strength as an amplitude, and accepts
  chirp, key clicks and static as limitations. **Decided: the standard-practice direction** ("I want to go in this
  direction"): today's noncoherent evidence with the coherent sum capped at about 60 ms instead of 390 ms; each story
  tracks its amplitude, with its spread within marks and across gaps scaled by a decision-free long-run level, not by
  the story's current level; chirp beyond about ±10 Hz, key clicks and static are limitations. The coherent draft's
  answers above (second order, both phases) are void; its details are worked out again, one decision at a time.
  - **The coherent window: standard practice (owner, 2026-10-22: "Standard fucking practice!", on being offered 60 ms
    as a recommendation).** CW Skimmer's and the RBN's 50 Hz decoders: a coherent sum of about 20 ms, with longer marks
    combined noncoherently. Rule for the rest of this design: the standard value is the choice; departures only with a
    stated, measured reason, put to the owner.
  - **The layout: a 50 Hz filter in front of the decoder, which works on its envelope (owner, 2026-10-22: "Yes").** The
    standard layout (CW Skimmer, fldigi, Bell): one filter per channel instead of per-story coherent sums. The filter's
    output samples are correlated over about 20 ms, and the evidence must count them as such. Detail 2 (the strength
    tracking) is still open; the owner asked first why the decoder adds up pieces instead of filtering, then how often
    the filter's output needs sampling.
  - **The filter's width and the sampling: about 80 Hz, sampled every 3 ms (owner, 2026-10-22: "A").** The range stays
    5.2 to 100 WPM. 80 Hz is the filter matched to a 100 WPM dot of 12 ms (Bell used 100 Hz); 3 ms is a quarter of that
    dot, the standard frame step. Slower signals pay up to about 2 dB against a 50 Hz filter (derived); a narrower
    filter for slower signals is in the backlog (owner).
  - **A story's amplitude prediction (owner, 2026-10-22):** "It should be set by the long run amplitude but it depends
    heavily on how long since the last estimate of the story's amplitude was made." The spread is the long-run
    amplitude times √((1 − ρ²)/2), ρ = exp(−π² f_D² Δt²/2) over the time Δt since the story's last estimate, and the
    predicted value drifts from the last estimate toward the long-run amplitude as Δt grows (Rayleigh fading; Bell's
    form). At 2 Hz: 0.013 after 3 ms, 0.21 after 48 ms, 0.53 after 144 ms, 0.70 after 336 ms (derived).
  - **Limitations (owner, 2026-10-22: "Agreed"):** chirp beyond about ±25 Hz (the 80 Hz filter's reach), key clicks
    and static crashes are known limitations, as in standard practice; recorded in `docs/backlog.md`.
  - **The fading rate: one fixed rate on the fast side, 2 Hz (owner, 2026-10-22: "A, with B in backlog").** Bell used
    1 Hz; 2 Hz covers the development set's 5 Hz fading better. Several rates per story are in the backlog.
  - **CPU (owner, 2026-10-22):** "something like B, and all the other choices which add to the number of potential
    stories needs to be considered very carefully given how much CPU we're currently requiring per channel." Any
    option that multiplies stories or hypotheses within them comes to the owner with its measured CPU cost.
  - **The long-run amplitude: each story's RMS of its own key-down samples since its over began, with a hang AGC as
    the backup (owner, 2026-10-22: "adopt it with the hang AGC as the backup").** As CW Skimmer measures strength
    (key-on samples only, VE3NEA). Per sample: predict from the estimate so far, score, then update. A new story's first
    mark is scored against the owner's flat prior (−15 to +85 dB S₅₀₀) and seeds the RMS. Its weak spot, a start in a
    deep fade (10 dB or more down in about 10% of starts, 20 dB in about 1%), is measured in the prototype. The backup: a
    decision-free hang AGC on the channel; **its parameters (rise, hang, release) are not decided** (owner,
    2026-10-22). The values discussed (rising at once, holding 1.6 s, the word gap at 5.2 WPM, then falling quickly)
    were a proposal only. The owner rejected time constants not derived for this decoder (fldigi's 0.4 s and 2 s, an
    instant attack, a 24 ms hold-off: "Every single one of these three options is completely arbitrary and made up")
    and a full Bayesian estimate per story as too complicated.
  - **The amplitude prediction's exact form, to be derived (owner, 2026-10-22: "Then that is what we should do.
    Proceed.").** A start in a fade showed the design over-confident in its long-run amplitude, which rests on few
    independent fades early in an over. Before choosing any form: (1) derive the exact predictive distribution of the
    amplitude after a time Δt (Rician given the last amplitude and Ω; Ω inverse-gamma after ν independent fades under
    the owner's flat prior; their mixture), with its exact mean and variance; (2) check it against the generator's
    fading by simulation, including ν's count (Satterthwaite) and conditioning on the last amplitude only (Bell's
    simplification); (3) measure the evidence each simple form loses against it (a Gaussian with the exact moments, the
    rough three-term formula, the owner's rule as decided), with the threshold argued, not picked; (4) independent
    review; then to the owner. Scratch calculations only; nothing built.
  - **Findings of that derivation (2026-10-22; reviewed: 0 Critical, 3 Important, 4 Minor; scratch only, 25 WPM; the
    8 WPM calibration run unfinished).** Files: `.superpowers/sdd/2026-10-20-new-decoder-prototype-1/amp-predictive.md`
    and `fading-amp-predictive-review.md`.
    - **Exact predictive (derived):** given the long-run power Ω and the last amplitude r₀, the amplitude after Δt is
      Rician (center ρr₀, spread Ω(1 − ρ²)/2 per component); averaged over Ω's truncated inverse-gamma after ν
      independent fades, its density and mean are closed forms (₂F₁) and E[r²] = ρ²(r̂₀² + P_m) + (1 − ρ²)Ω̄ exactly. Its
      tail is a power law, (1 + x²/(2κS))^(−ν), so no Gaussian fits it at small ν.
    - **ν (measured):** counted on the key-down sample times it matches simulation within 3% at 0.1 to 5 Hz; counted on
      elapsed time it over-counts (10% at 25 WPM, up to 46% at 8 WPM, at 2 Hz).
    - **The starts (measured):** the rule as decided, with the early RMS taken as exact, loses up to about 12 nats per
      gap in the first half-second and up to about 100 nats per message in marks (2 Hz, +30 dB S₅₀₀). Any of the
      widened forms cuts this to a few nats, and they differ by about a nat or less. **Which form is not decided.**
    - **Larger losses elsewhere (measured):** the one-number amplitude model updated every 3 ms is over-confident inside
      marks even with the true level and rate (about three times too many 1% surprises); 5 Hz fading predicted at the
      fixed 2 Hz costs hundreds of nats per message. A predictor using the full complex history could gain 18 to 342
      nats per message at 2 Hz: an upper bound using phase the decoder does not see.
    - **Splits** come from the loss of the continuing story relative to a fresh one (for example a start in a fade, then
      a recovery), not from losses every reading shares.
  - **The fix for starts: settled (owner, 2026-10-22: "Isn't omega bar already a well defined number?").** The rule's
    long-run power is Ω̄, the expected long-run power given the story's key-down samples and the owner's flat prior (the
    law of total variance: the rule's variance term is linear in Ω, so averaging over Ω's uncertainty replaces it by
    its expectation), not the plain RMS Ω̂. About Ω̂·ν/(ν − 1) once ν exceeds 1, ν counted on key-down sample times; on
    the first fade or two it depends on the prior's top, +85 dB S₅₀₀ (a placeholder). The third term of the rough
    formula is dropped as negligible (within 0.2 nats, measured). The 95% confidence bound offered beside it is not
    part of the design.
  - **The amplitude model's over-confidence inside marks under fast fading: a known limitation, backlogged (owner,
    2026-10-22: "I think we record that as a known limitation and backlog it").** It matters with fading of about 2 Hz
    and faster at 25 WPM, earlier at slow speeds, and mainly on strong signals (`docs/backlog.md`).

**D17. The test set's keying edges. Decided (owner, 2026-10-23): fix the generator and remake the test set, before any
long run; the timing convention and rise times wait for the research.**
- **The finding (the owner's, 2026-10-22):** "the development set can't be making good code at 100 WPM if it uses 5 ms
  rise and fall times." The generator (`training/kz4ap_synth/generate.py`) uses 5 ms raised-cosine edges at every
  speed (`RISE_S`), placed by default inside each element, so every mark is 5 ms short at half amplitude and every space
  5 ms long: 3% at 8 WPM, 10% at 25 WPM, 33% at 80 WPM (the development set's top speed cell). The groups built on
  VE3NEA's centered 2 ms edges are unaffected.
- **Decided:** fix the generator, regenerate the development set from the same seeds, and rerun the bank decoder's
  reference on it, before the new decoder's long runs. Old and new scores are not compared.
- **Not decided (owner, 2026-10-23):** "you are assuming without evidence that the intended length should be measured
  between edge centers. That decision needs to wait for your agent to finish its research." Where the edges sit relative
  to the keyed timing, and the rise and fall times, wait for the research on real transmitters' keying envelopes
  (`.superpowers/sdd/2026-10-20-new-decoder-prototype-1/keying-envelope-research.md`). The 80 Hz filter's exact shape
  (D16) waits for the same research.
- **The research (2026-10-23):** every keying shaper whose design could be read starts the rise at key-down and the
  fall at key-up, so a mark keeps its keyed length at half amplitude (QRP Labs AN005, KE3HO, Morse Runner, DeepCW);
  current rigs' rise-time settings run 1 to 8 ms (defaults 2 to 6 ms), ARRL lab 10–90% rise times 2.5 to 4.7 ms at
  defaults; real transmit paths add a mark bias of −7 to +4.7 ms, both signs; at 100 WPM a 12 ms running sum loses
  only 0.08 to 0.33 dB against a filter matched to real dots, while a 5 ms short mark costs 4.7 dB.
- **Decided (owner, 2026-10-23: "I choose A with a one and a half millisecond raised cosine profile"):** the
  generator's edges start the rise at key-down and the fall at key-up (each mark keeps its keyed length at half
  amplitude), with a raised-cosine edge whose 10–90% rise (and fall) time is 1.5 ms (owner, 2026-10-23: "the 10% to
  90% time is actually 1.5 milliseconds"). A raised cosine's 10–90% time is (π − 2·arccos 0.8)/π = 0.5903 of its full
  length, so the generator's full edge length is 1.5 ms / 0.5903 = 2.54 ms (derived).
  **Revised (owner, 2026-10-23):** "I want to use 2.5 milliseconds instead of 1.5 milliseconds as the default in the
  generator, but I want to decrease the rise time at higher speeds where it makes more of a difference." The default
  10–90% time is 2.5 ms (full length 2.5 / 0.5903 = 4.23 ms). **Settled (owner, 2026-10-23: "Agreed"): 2.5 ms at
  every speed, no shortening at high speed.** At 100 WPM a dot is 65% flat, about the ITU-based guide of a fifth of a
  dot (the research's heuristic); today's generator edge is 5 ms full length, a 2.95 ms 10–90% rise, so the rise is
  about 15% shorter than now, and the main change at high speed is the placement. Real transmit paths' bias is not added to every group. **This is for the generator
  only (owner, 2026-10-23): the decoder's filter shape (D16) is not decided.**
- **The decoder's filter shape (D16), decided (owner, 2026-10-23: "Agreed"):** the matched filter to a real 100 WPM
  dot, 12 ms at half amplitude with raised-cosine edges of 2.5 ms 10–90% (full length 4.23 ms), as the generator now
  keys; noise bandwidth 76 Hz. Against a plain 12 ms running sum (83 Hz) it gains 0.17 dB on the dot and suppresses
  neighbors more: −15.5 against −14.8 dB at 70 Hz, −15.4 against −13.3 dB at 120 Hz, −24.5 against −18.0 dB at 200 Hz
  (derived; scratch `filt_resp.py`). The owner's concern about the ITU's necessary bandwidth (250 to 417 Hz at
  100 WPM) was answered: that is a transmitter's occupied bandwidth for its waveform's shape; a detector's best
  bandwidth is the matched filter's, about 1/T (Turin 1960), and a 250 to 417 Hz filter would admit 5.3 to 7.5 dB
  more noise.
- **The detector's frequency following and the fixed channel: deferred to the backlog (owner, 2026-10-23: "a web of
  issues here which I don't think we need to tackle right now, but which should be recorded in the backlog"; "My
  decision was only about the detector, but I would say all the decisions that have to do with the decoder are still
  open").** Recorded in `docs/backlog.md`. **Still open, as decoder decisions:** the tracker's trust tolerance (0.667 Hz,
  derived for the old block, against about 21 Hz by the same 1 dB rule for the 76 Hz filter); whether the decoder reads
  the detector's frequency after its first block; and how an untrusted tracker searches and restarts (today: it holds,
  searches ±15 Hz of its own frequency and restarts on two agreeing measurements).
- **The channelizer follows the detector (owner, 2026-10-23: "B"; asked first: "we need to decide on what kind of
  frequency tracking the channelizer does before we worry about the decoder's frequency tracking").** Each channel
  block is shifted by the detector's current frequency for the track (a phase-continuous rotation on the 1 500
  samples/s stream, one complex multiplication per sample), and the channel's bins move when the track moves by more
  than half a bin (11.7 Hz): the usual coarse-plus-fine layout of FFT channelizers (design notes, topic 4, item 8).
  Decision-free: the detector decides where the station is (the owner's rule of 2026-09-29). Every decoder receives
  the station near 0 Hz. Response (derived from the detector's 1 s average): a drift of 1 Hz/s is followed 1.1 to
  1.6 Hz behind; an equally strong answering station moves the peak after about 0.7 s, during which a station 30 Hz
  off loses about 2 dB in the 76 Hz filter. This reverses the fixed channel recorded in the backlog the same day (that
  item updated). The bank's and Matched's own shifts by the detector's frequency become redundant; how they adapt is
  for the implementation plan.
- **No frequency tracker in the new decoder (owner, 2026-10-23: "A").** With the channelizer following the detector,
  the station arrives off-center only by the detector's error (about 0.2 Hz steady, 1.1 to 1.6 Hz behind a 1 Hz/s
  drift, briefly up to 47 Hz for about 0.7 s at a turnover); the 76 Hz filter loses almost nothing at 2 Hz and under
  0.5 dB at 15 Hz (derived). The tracker's trust tolerance, its untrusted search and its use of the detector's frequency
  are no longer decisions. Consequence for measurement: on oracle channels the detector's frequency is the label's,
  without drift, so drifting test stations need the label's true, drifting frequency supplied (a test-harness change).
- **A mark's expected shape: the smeared prediction (owner, 2026-10-23: "I want option B, but we need to discuss the
  exact shape").** Each story predicts the filter's actual output for its claimed keying (the gradual rise and fall),
  not a square on/off shape, which would misread every mark's timing by several milliseconds. Standard in sequence
  detection over a smearing channel (MLSE with intersymbol interference, Forney 1972). **The exact shape (owner,
  2026-10-23: "OK agreed"):** a keyed carrier with 2.5 ms (10–90%) raised-cosine edges, through the channelizer's filter
  (Blackman-windowed sinc, 21.3 ms, −6 dB at ±150 Hz; included because it is known exactly and costs nothing in a
  precomputed table, though it changes the shape by at most 0.6% of full at 25 WPM and 1.2% at 100 WPM), then through
  the 76 Hz filter, computed from the story's claimed key-down and key-up times, averaged over where an edge falls
  within a 3 ms sample; the misfit for other rigs' edges (1.2 to 3% of full for 0 to 4 ms rises, 6 to 12.5% for 8 ms;
  derived) goes into the systematic term. The filter's output rises 9.9 ms (10–90%) for a long mark; a 100 WPM dot
  peaks at 0.90 of full (derived; scratch `template_shape.py`).
- **The sampling interval: 2.667 ms, every 4th channel sample (owner, 2026-10-23: "Agreed"), replacing 3 ms.** The
  owner asked "Why would we not just change our own sampling rate to be a multiple of the channel sample rate"; 3 ms is
  4.5 channel samples (1 500 samples/s), so every other instant fell between samples. 2.667 ms (375 samples/s) keeps
  the rule of at most a quarter of a 100 WPM dot (12 ms / 4 = 3 ms); 3.333 ms (5 samples) would break it. About 12%
  more computation than 3 ms (3 / 2.667 = 1.125).
- **The samples in a story's long-run RMS (owner, 2026-10-23: "B"):** every sample where the story expects signal,
  each counting in proportion to the square of its template value (the least-squares estimate, Σ(e_k² − σ_y²)/Σ s_k²);
  on a mark's flat part this is CW Skimmer's average, and the edge samples count less instead of being discarded.
  A departure from Skimmer's practice, for a derived reason: at 100 WPM a dot's output peaks at 0.90 of full and has
  no flat part, so discarding edges would leave dots at high speed, and an all-dot word, with no estimate.
- **Counting the independent fades ν: binned, exact on the bins (owner, 2026-10-23: "Agreed"; his idea: "What if we
  just did a very long-term average and decimated...").** Each story sums its key-down weight (s_k²) in bins of
  42.67 ms (two engine blocks) and computes ν = (Σw)² / ΣΣ w_i w_j ρ²(t_i − t_j) over the bins; binning changes ν by
  about (π f_D b)²/12 = 0.6% at 2 Hz (derived), against the Satterthwaite form's own ~3%. Each story keeps its last
  10 bins (ρ² < 0.001 beyond 0.42 s). Cost about 1.6 operations per story per sample (1 addition each sample,
  10 multiply-adds per closed bin) and about 12 numbers per story; option (C), an exponential-sum fit, would take about
  10 operations per sample, 4 numbers, and an unchecked fit error (rough counts).
- **The amplitude prediction in full (owner, 2026-10-23: "agreed"):** after a time Δt since the story's last estimate
  r̂₀ (uncertainty P_m), the predicted amplitude is ρ_a·r̂₀ + (1 − ρ_a)·0.886·√Ω̄ and its variance ρ_a²·P_m +
  ½(1 − ρ²)·Ω̄. ρ = exp(−π² f_D² Δt²/2) is the signal's correlation at f_D = 2 Hz; ρ_a is the amplitude's own
  correlation, (π/4)(₂F₁(−½, −½; 1; ρ²) − 1)/(1 − π/4) (Rayleigh; 0.902, 0.416 and 0.011 at 48, 144 and 336 ms, against
  ρ = 0.956, 0.664 and 0.108); 0.886·√Ω̄ = (√π/2)·√Ω̄ is the Rayleigh mean amplitude. The owner's rule (the middle
  term) is unchanged. Form (iv) of the derivation, within 0.2 nats of the exact predictive (measured in scratch).
- **The backup hang AGC: dropped from the design for now (owner, 2026-10-23: "A").** The Ω̄ fix covers the starts it
  was meant to back up, and a channel-level AGC knows a new station's level only from an earlier transmission within
  its hang. Recorded in `docs/backlog.md` as a fallback, its role and settings to be worked out only if the prototype
  shows the per-story RMS failing.
- **The systematic term's memory: 10 samples, 26.67 ms (owner, 2026-10-23: "I agree"), replacing 390 ms.** The owner:
  "The 390 ms is arbitrary at this point and hence stupid": it was the old block length (the longest block losing at
  most 0.5 dB at 1 Hz), and the blocks are gone. The memory supplies C² on a mark's fall, where the template still
  tails off after the signal has dropped; elsewhere it adds nothing (on the flat part the peak equals the current
  power, on the rise the look-ahead does the work, in a gap the template is zero). Derived: it reaches from the last
  sample at full carrier (99% of full amplitude or more; 99.9% gives the same) to the last sample where the leeway
  C²(f²s_k² + v_k) can equal the noise σ_y², for the strongest station the newborn prior allows (+85 dB S₅₀₀), over
  every position of the edge within a sample: 10 samples (8 at +60 dB S₅₀₀, 7 at +40 dB S₅₀₀). A first proposal of
  16 ms rested on a 1% tail cutoff borrowed from the own-power rule and was withdrawn. Noise in the peak (scratch
  simulation, correlated filter noise): it reads high only, 1.77, 1.20, 1.06 and 1.02 times the true power at 0, +10,
  +20 and +30 dB S₅₀₀, while the leeway is 0.005, 0.03, 0.28 and 2.7 times the noise, so it matters only where the
  peak is within a few percent. Task 4e's failure of a 20 ms memory came from per-block booking, which per-sample
  booking removes (derived, not yet measured); the prototype measures 26.67 ms on the strong-station grid, the
  stopped-neighbor grid, the speed step and TEST/TU, with no other variants.
- **The look-ahead: 10 samples, 26.67 ms, the mirror of the memory (owner, 2026-10-23: "Agree"), replacing 2 samples.**
  On a rise the template climbs while the signal at the sample may still be low, so C² must come from the flat part
  ahead. Derived by the memory's rule, computed on the rise separately: from the first sample where the leeway can
  equal the noise to the first sample at full carrier, at +85 dB S₅₀₀, over every edge position: 10 samples (8 at
  +60 dB S₅₀₀, 7 at +40 dB S₅₀₀). It covers D15's 4 ms minimum against the filter's ringing before a mark. C² is thus
  the largest e² over a symmetric window of 21 samples (10 back, the current one, 10 ahead), with the sample's own
  e_k²/s_k² as before. Costs: 26.67 ms more delay before a sample is scored; one running maximum per channel, not per
  story. For reference (derived, scratch `memspan\risetimes.py`), the decoder's input rises 10.04 ms (10–90%),
  14.75 ms (1–99%) and 19.04 ms (0.1–99.9%) for a 2.5 ms keyed edge through the channelizer and the 76 Hz filter.
- **The floor f: 2%, a placeholder until the prototype measures it (owner, 2026-10-23).** The owner: "F seems like it
  might depend on a huge number of things that we have zero control over, and it seems very hard to choose an
  appropriate value." Its 2% was measured in the block design (D15), where any amplitude change within a block had
  nowhere else to go; the new design models the amplitude per sample and gives edges their own term v_k. What remains
  for f is unmodeled and uncontrolled: a fading rate other than 2 Hz or non-Rayleigh fading, transmitter amplitude
  ripple, the receiving radio's AGC, a neighbor's leakage. The prototype measures it: decoding with the true story
  (the label), the scatter of strong stations' flat-part samples about the model's prediction beyond the noise,
  relative to the carrier, on the synthetic set and on real recordings; the value chosen from that comes back to the
  owner, and f goes to zero if the scatter is negligible on both.
- **Other transmitters' edges go into the edge term v_k (owner, 2026-10-23: "Agree"; option B).** v_k = the average
  over u and r of (s_k(u, r) − s̄_k)², with u the edge's position within the 2.667 ms sample (uniform) and r the
  transmitter's 10–90% rise time, uniform over 0.8 to 4.7 ms (the ARRL Lab's measured range,
  `keying-envelope-research.md`; uniform is the maximum-entropy choice given only the range); each rise's edge is
  aligned to the template at its half-amplitude point, the story absorbing the delay in its timing; s̄_k stays the
  2.5 ms template. Zero on the flat part and in gaps; one precomputed number per template sample; no CPU per story, no
  stories added. Misfit after alignment (derived, scratch `memspan\misfit.py`, through the channelizer and the 76 Hz
  filter, 48 ms mark and 12 ms dot): 0.9–1.6% of full at 0.8 ms, 0.6–1.1% at 1.5 ms, 0.8–1.5% at 3.5 ms, 3.8–4.6% at
  4.7 ms; unshaped keying (about 5.3%) stays outside, with key clicks a known limitation. Chosen over the floor alone
  because f is now a placeholder that may go to zero. The prototype tests stations keyed at 0.8 and 4.7 ms. (An
  earlier figure of 6–12.5% for "8 ms rises" used a 13.5 ms full edge, outside the measured range, without the
  alignment; withdrawn.)

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
| **R0** | CPU per channel-second, live stories, how often the ceiling decides, newborns' fates; at the defaults and at the reference settings | one recording per group, and one of N2 | Measurement only. **Stop:** the owner sees R0's measured costs (in words, in chat) before the long runs start. |
| **R1** | the wide-beam reference (beam 80 nats, ceiling 17 000); the largest deficit any newborn reaches before it leads, per group | all of DEV2 | If the largest deficit exceeds the 10-nat margin, the margin and the minimum beam are raised to it (notes, topic 5, item 4). |
| **R1b** | the reference's convergence (beam 120 nats, ceiling 34 000) | a quarter of DEV2 | R1 counts as converged if it differs from R1b by at most 0.1 per 1 000 letters; otherwise R1 is rerun at R1b's settings. |
| **R2** | search errors at beams 28, 35, 45 and 60 nats; newborns the beam drops within their first element | all of DEV2 | The default beam is the narrowest, never below the minimum, with at most 1 search error per 1 000 letters in every group, not only pooled. If none meets it, the errors are split by cause (beam or ceiling, from the newborns' fates) and brought to the owner. §5, item 4 is checked on the 28-nat run. |
| **R3** | CER against the ceiling: 300, 600, 1 000, 1 700, 3 000; newborns the ceiling removes | all of DEV2 | The default ceiling is the smallest whose paired CER against the 3 000 run is not worse in any group of O6 (the knee); the floor is the largest that is worse in every one. |
| **R4** | the default against the bank, by group and speed cell, crossings and paired differences; CPU and live stories per group; the space error rate per group; false characters per hour on N2 | all of DEV2, and N2 | The main result. I2 above F = 8 left out on both sides and listed separately (O7). |
| **R5** | each rate at 10 times and a tenth: P, then ε (four runs) | all of DEV2, and N2 | Checks, not tuning (notes, topic 5, item 3): false characters per hour (N2), first words (G2, H2), the elements taken to follow D2's steps against false restarts in A2. A value far from the counted rate that scores better is reported as pointing to an error elsewhere, not adopted. |
| **R6** | the fading spread: 0.3, 2 and 5 Hz against R4's 1 Hz; at the best spread, blocks losing 0.25 and 1 dB | all of DEV2 | The default spread is the one with the lowest B2 CER among those not worse on A2 than the best on A2 (notes, topic 4, item 9). |
| **R7** | the 5% speed grid, 61 values | all of DEV2 | Adopted only with a real advantage (notes, topic 2, item 1): better somewhere, worse nowhere, and the owner judges the gain worth its CPU. |
| **R8** | genie timing: each signal's true speed and fist | C2 | Measurement: the gap per style in dB S₅₀₀ at CER 0.10, the most a better timing model could gain (notes, topic 2, item 6). |
| **R9** | the tracker's frequency error; the decoder with the genie frequency | F2, G2, E2 (neighbors 0–120 Hz away), A2's cells below S₅₀₀ +2 dB | Measurement against Δf · T ≤ 0.26 (§2.9). Per-story frequencies are reconsidered only if turnovers show a real loss (notes, topic 4, item 8). |
| **R10** | genie fading: each signal's true spread | B2 | Measurement: what one fixed spread loses, per spread cell (notes, topic 4, item 9). |
| **R11** | the hiss estimator's error against the generator's noise (σ = 0.02 FS) | every DEV2 recording | Measurement only (no decoding). |

R2 and R3 set the beam and the ceiling before R4 runs. R6 and R7 come after it and change the defaults only
through their rules; if R6 moves the spread or R7 is adopted, R4 is rerun with the new defaults.

### 6.2 The sets (D7, decided: the whole set for every measurement)

- **All of DEV2:** 1 065 742 channel-seconds and roughly 1 050 000 letters (estimated from the groups' 100-character
  signals), so a search-error rate of 0.1% means about 1 000 errors pooled.
- **A quarter of DEV2,** for R1b only: every fourth recording of each group, by name, about 266 000 channel-seconds.
- **The genie and tracker runs** (R8–R10) cover the groups they concern, in full; R11 reads every recording.
- **N2:** 36 000 channel-seconds (D8), with R4 and R5.

### 6.3 What it costs

One full-run equivalent, all of DEV2 at the default, costs 1 065 742 channel-seconds × c, where c is R0's CPU per
channel-second. The slice gives 9–55 ms on the Linux machine (§2.11), and the prototype costs more. At c = 30 and
80 ms, one FRE is 8.9 and 23.7 CPU-hours: 0.9 and 2.4 hours on the machine's 10 cores.

| Runs | FRE at the decided sizes |
|---|---|
| R1 + R1b (reference cost 3–10× the default; R1b on a quarter of DEV2 at twice R1's cost) | 4.5–15 |
| R2 (4 beams) | 2.2–4.6 |
| R3 (4 new ceilings) | 3.3–4.8 |
| R4 | 1.03 |
| R5 (4 runs) | 4 |
| R6 (5 runs) | 5 |
| R7 (cost about 1.9×) | 1.9 |
| R8, R9, R10, R11 | 0.5 |
| **Total** | **about 23–37** |

So the decided sizes take about 205–880 CPU-hours: 20–88 hours of the Linux machine's 10 cores. (The options put to
the owner: 7–11 with the whole set only for R4; 14–28 with it also for R1–R3.) R0 replaces c and the reference's
multiple with measured values, and the owner sees them before the long runs start.

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
- **Determinism:** the same recording and settings give the same output on every run on one platform (spec §4.1:
  "the same recording with the same settings must always produce the same spots"). Builds on different platforms
  (MSVC, glibc) may differ in the last bit of a floating-point value, so tests compare with tolerances, and every
  measurement runs on the Linux machine (corrected 2026-10-20: this line had said "every run and platform", which
  the spec does not ask).
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
| 4b | The mark's rise (D10, added 2026-10-20) | 2, 3, 4 |
| 5 | The hiss estimator in the engine | — |
| 6 | The frequency tracker | — |
| 7 | `ViterbiDecoder`; engine and bench; `docs/signal-processing.md`; the results record | 4, 4b, 5, 6 |
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

### Task 4b. The mark's rise (D10, added 2026-10-20)

**Files:** modify `engine/include/kz4ap/viterbi/config.hpp` (`keying_rise_s`), `observation.hpp/.cpp`,
`level.hpp/.cpp` and `search.hpp/.cpp` under `engine/…/viterbi/`, with their tests.

**Interfaces:**
- `std::vector<double> rise_fractions(std::span<const double> taps, double input_rate_hz, int decimation,
  int samples_per_piece, double rise_s)`: the fractions g_1 … g_J of §2.3.
- The evidence functions take the carrier's sum as `double m_signal` where they took `int m` for it.
- `FlatPrior` takes the fractions.

- [ ] **Step 1: Measure first.** Try the rise in a copy, without touching the branch, and measure:
  - the speed step of §5, item 2 at +10 to +28 dB S₅₀₀, three seeds each, at beams of 40 and 80 nats;
  - "PARIS PARIS" at 12, 25 and 40 WPM over the same range;
  - the one-piece lead at a mark's edge.

  If the step's CER exceeds 0.05 anywhere at the default beam, stop: the owner decides.
- [ ] **Step 2: Failing tests.**
  - the fractions against the real channelizer's output for edges at a piece's start, middle and end (48 and
    192 kHz);
  - the evidence with a non-whole m_signal against the formula;
  - the flat prior with the rise against its direct integral (within 0.01 nats);
  - `Search.FollowsASpeedStep` restored to §5's criterion;
  - `Search.FollowsASpeedStepAtEverySnr` (label `full-sweep`): Step 1's sweep, as a test.
- [ ] **Step 3: Implement. Step 4: they pass, and every earlier test with them. Step 5: Commit.**

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
- [ ] **Step 4: Stop.** The owner sees R0's measured costs, in words, in chat, before Task 10 starts the long runs.

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
