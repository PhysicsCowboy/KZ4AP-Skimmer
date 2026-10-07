# manta's `hsmm` decoder: a study for a faithful C++ port (2026-10-07)

The design amendment of 2026-10-07 (`docs/design/2026-09-25-kz4ap-skimmer-design.md` §5.2, step 3) decides to
port manta's `hsmm` decoder faithfully from Rust into the C++ engine, keep manta's MIT/Apache notice on the
ported files, check the port against manta's own behavior, and only then make measured changes one at a time.
This document studies manta's code so that the port can be planned. It changes no engine code.

**Source.** https://github.com/HagaleTechnologies/manta, branch `main`, commit
`1555009ce5c144714f3c26334343edfef578c5e2` (2026-10-07 09:23 UTC), cloned to `build/third-party-src/manta`
(git-ignored). Every manta reference below is `path:line` at that commit, relative to manta's root; `decode/`
abbreviates `crates/manta-decode/src/`. The `hsmm` module has not changed since PR #161 was squash-merged
(`4e71cba344`, 2026-09-10); `evidence.rs` changed once since, cosmetically (`%` replaced by `is_multiple_of`,
`0792f22ad2`).

**Labels.** **[F]** fact from manta's code or documents, with the reference. **[M]** measured in this study,
with the file that holds it; all such files are under `build/` (git-ignored, temporary) and listed in §5.3.
**[I]** inference: a derivation or a judgment. Units follow `docs/signal-processing.md` §0: amplitudes in FS,
powers in FS², log-likelihoods in nats, and every dB names its reference.

**manta's time base.** manta counts time in **hops** of its channelizer: 1 hop = 1/375 s = 2.667 ms
[F: `decode/lib.rs:19–21`]. Its speed variable **u** is a dit length in hops; WPM = 450/u, from PARIS,
1200 ms per WPM-dit / (8/3 ms per hop) [F: `docs/SPEC-decode-core-v2.md:25–28`; `decode/decoder.rs:309`].
So u = 18 hops is a 48 ms dit, 25 WPM.

## Summary

1. **What it is.** An explicit-duration (semi-Markov) decoder over per-hop evidence: hypotheses ("tokens")
   carry a Morse-tree node, a phase (after a mark or after a space), their own dit length u and a score;
   segments (dit, dah, three gap types, silence) start and end only at *anchors* (half-amplitude crossings and
   every 8th hop); per anchor the best 12 tokens survive; characters are committed when all 12 agree or 25
   dits after the fact. About 1 160 lines of Rust code without comments, glue included (§6.3), and no
   dependency but `serde` [F: `crates/manta-decode/Cargo.toml`].
2. **What it reads.** The squared magnitude, once per 2.667 ms hop, of one channel of manta's polyphase
   filterbank: a 93.75 Hz-wide channel (ENBW 82.2 Hz, −6.0 dB relative to the passband at ±46.9 Hz). Phase is
   discarded. Everything downstream is in hops, so the decoder is tied to that rate and that bandwidth.
3. **Least invasive faithful adaptation** (§4): emulate manta's channel on our 1500 samples/s complex stream
   with a 128-tap FIR, manta's own filter design at 16 channels (Kaiser β = 7.857, cutoff 46.875 Hz, the same
   85.3 ms support), and keep every 4th output: 375 power samples per second, then manta's code unchanged.
   Computed: the cascade with our ±150 Hz channel filter matches manta's channel response, both relative to
   the passband, within 0.04 dB out to 46.9 Hz; ENBW 82.14 Hz against 82.23 Hz [M].
4. **Reference outputs can be produced here** (§5). Rust 1.98.0, manta's pinned version, is now installed
   inside the repository (`build/rust`, 1.1 GB with its crate cache, nothing system-wide); manta-decode
   builds and its 120 unit tests pass on Windows (42 of them cover the hsmm chain); a 40-line driver
   (`build/manta-ref`) feeds any recorded power stream to manta's decoder and writes its events; the whole of
   manta builds too, and its oracle reads our I/Q recordings as they are [M]. The port can be checked bit for
   bit: manta's arithmetic is IEEE single precision in a fixed order, with no randomness (§5.4).
5. **What a faithful port will reproduce** (measured on synthetic streams, §3): steady keying decodes well
   (no error in 6 repetitions of "CQ TEST K1ABC K1ABC TEST" at S₅₀₀ +15 dB at 12, 25 and 40 WPM), but a 1 s
   silent lead-in makes the decoder miss a whole 12 or 18 WPM message; noise is decoded as text (about 195
   characters per minute in noise alone); the last character of an over is withheld until the next over or
   about 4 s, and after 4 s or more of pause it is committed twice; an 8 s pause fills with 34 phantom
   `E`s; after a speed drop the first word of the next over is wrong. §3.2 traces the pause behavior to the
   code.
6. **CPU** (§6.2): 16 ms per channel-second at 25 WPM on this laptop (Rust), growing with the dit length
   (31.5 ms at 12 WPM); manta's own bench costs 2.6× its published figure here, for the gate's code as for
   this one.
7. **manta's published numbers are not for this code.** Its stage-2 gate (oracle 56.1% `as_word`, CPU
   4.25× over budget) was measured before 21 rounds of review fixes changed the hsmm code (§6.1) [F].
8. **License** (§7): manta is `MIT OR Apache-2.0`, copyright 2026 Hagale Technologies, LLC; its source files
   carry no per-file notice. Ported files should carry the MIT notice verbatim, the manta file and commit they
   derive from, and a statement of modification.

## 1. Data flow, from channel input to committed text

```
 I/Q (fs) ──► polyphase filterbank (93.75 Hz channels, 375 frames/s) ──► X[k, m] complex
                                                                            │ P = |X|²  (phase discarded)
                                         one channel per station:  P[i], a[i] = √P[i], per input hop i
                         ┌──────────────────────────────────────────────────┴────────┐
                         ▼                                                           ▼
          noise power N[i] (1.5 s minimum statistics)          evidence: a_s (8 ms EMA), M (centered max
                         └──────────────────────┬──────────────  over ±4 dits), present, û, llr, anchor, C
                                                ▼                (emitted with a delay of h = 4 dits)
                      HSMM token passing: at each anchor, extend tokens from earlier anchors by one segment,
                      merge by (node, phase, u bin), keep 12          ▲ h follows the best token's u
                                                ▼                     │
                      commit: consensus of the 12, or forced 25 dits later ──► CharDecoded / WordBoundary
                                                                               (+ confidence, alternatives)
                      best token's u ──► SpeedUpdate (WPM = 450/u)
```

### 1.1 The channelizer's output

- **Filterbank** [F]: N = fs/93.75 channels (2048 at 192 kS/s), every channel computed every hop of N/4 input
  samples, so 375 complex frames per second per channel (`crates/manta-dsp/src/channelizer.rs:100–119,
  147–193`; `docs/SPEC-decode-core.md:20–35`). Prototype low-pass: windowed sinc of 8N taps (85.33 ms),
  Kaiser β = 7.857, cutoff 46.875 Hz (half the spacing), unity gain at 0 Hz (`crates/manta-dsp/src/proto.rs:
  5–51`). manta measured −6.02 dB relative to the passband at ±46.875 Hz, ENBW 82.23 Hz, support 85.33 ms,
  group delay 42.66 ms (`docs/DECISIONS/2026-09-09-decoder-recall-research.md:37, 510`).
- **What one channel is** [I, derived]: the magnitude of a weighted-overlap-add filterbank channel equals the
  magnitude of the input mixed down to the channel's center, low-pass filtered by the prototype and kept once
  per hop; the rotation step (`channelizer.rs:169–174`) changes only the phase. So the decoder sees a 375
  samples/s magnitude stream from an 82 Hz-ENBW channel.
- **What the decoder is given** [F]: P = |X|² (`channelizer.rs:177–178`) and a = √P, as f32
  (`crates/manta-engine/src/track.rs:599–606`; `crates/manta-testkit/src/oracle.rs:299–302`). Only a and P
  are used; the complex value is not.
- **Which channel** [F]: manta's *oracle* (the configuration its stage-2 gate was measured with) takes, for
  each 40 s window around a spot, the one of the three channels around the spotted frequency with the highest
  mean power, keeps it fixed, and passes no neighbor noise reference (`oracle.rs:289–301`). The live engine
  instead takes the strongest of the track's three owned channels anew every hop
  (`docs/SPEC-decode-core.md:239–242`; `track.rs:1101`) and passes a neighbor reference
  (`track.rs:1075, 1120, 1175`). A narrowband "refiner" exists but is off by default
  (`decode/config_file.rs:94–96`).
- **Scale** [I, from `decode/evidence.rs:244–251`, `decode/noise.rs:72–99`]: the chain is invariant to a
  constant gain on a (the gate and the normalized amplitude are ratios; the noise estimate is linear), apart
  from the 1e-9 and 1e-18 guards. The channel's absolute calibration does not matter.

### 1.2 Noise floor

Per input hop i, before the evidence stage [F: `decode/noise.rs:72–99`, `decode/decoder.rs:288`]:

- P_s[i] = P_s[i−1] + α_N (P[i] − P_s[i−1]), α_N = 1 − e^(−2.667/40) = 0.0645 (τ = 40 ms); P_s[0] = P[0].
- N[i] = 10^0.216 · min(P_s over the last 563 hops) = 1.644 × the minimum over 1.5 s (a monotonic deque).
  10^0.216 is the bias correction manta measured for this minimum on independent exponential samples.
- n[i] = √max(N[i], 1e-18): the noise RMS amplitude, in the same units as a.
- With a neighbor reference r: N = max(N_temp, 0.5 · 10^0.15 · r) (`noise.rs:95–98`). The oracle passes none.

n is causal: each input hop's n is stored with that hop and used when the hop is emitted, while M (below)
looks ±h hops around it [F: `evidence.rs:121–132, 235`].

### 1.3 Evidence (manta's "evidence front end", `decode/evidence.rs`)

- **Amplitude EMA** [F: 121–132]: a_s[i] = a_s[i−1] + α_a (a[i] − a_s[i−1]), α_a = 1 − e^(−2.667/8) = 0.2835
  (τ_a = 8 ms); a_s[0] = a[0]. Used only for the mark level.
- **Delay line and emission** [F: 151–173, 82, 106–114]: each input hop is stored as (a, a_s, n, sample_ts).
  Hop t is emitted once input hop t + h has arrived, with h = max(1, round(4 · u_ref)) hops; u_ref is the best
  token's u, set by the glue after every emitted hop (`decode/decoder.rs:302–311`), 15 hops before any token
  exists. So h = 60 hops (160 ms) at the start, 72 hops (192 ms) at 25 WPM, 150 hops (400 ms) at 12 WPM.
- **Mark level** [F: 236–243]: M[t] = max of a_s over hops t − h … t + h (a centered maximum: the level of the
  strongest keyed sample within ±4 dits).
- **Keying-present gate** [F: 244]: present[t] = (M[t] ≥ 2 · n[t]), a peak at least 6.02 dB above the noise RMS
  amplitude.
- **Normalized amplitude** [F: 246]: û = clamp((a[t] − n[t]) / max(M[t] − n[t], 1e-9), −0.5, 1.5), on the raw
  a, not a_s.
- **Per-hop log-likelihood ratio** [F: 245–251]: llr = clamp((û − 0.5)/σ_u², −10, +10) nats if present, else
  −10. With σ_u = 0.30 the slope is 11.1 nats per unit of û and the clip is reached at û ≤ −0.4 or ≥ 1.4.
  [I, derived] This is ln N(û; 1, σ_u²) − ln N(û; 0, σ_u²): key down and key up as equal-variance Gaussians
  at û = 1 and û = 0, so its zero is the half-amplitude point at every keying depth.
- **Anchors** [F: 273–326]: hop t is an anchor if (a) it is present and the *next* hop's llr, computed with
  that hop's own M and n, has another sign (−1, 0, +1) than this hop's, so the anchor is the last hop before a
  half-amplitude crossing (at the stream's end, compared with the previous hop instead); or (b) t is a
  multiple of 8 (every 21.3 ms); or (c) present changed at t. The specification says the later hop of a
  crossing (`docs/SPEC-decode-core-v2.md:86–91`); the code deliberately tags the earlier one (comment at
  `evidence.rs:260–272`).
- **Prefix sum** [F: 329]: C[t] = Σ llr over hops ≤ t, in f64. The mark evidence of a segment (s, t] is
  C[t] − C[s]; a space's is its negative.
- Output per emitted hop [F: 31–42]: `HopEvidence {hop, sample_ts, llr, present, anchor, prefix, amp,
  mark_level, noise_level}`.

### 1.4 The segment decoder (`decode/hsmm/`)

**Grammar** [F: `decode/tree.rs:60–167`]: a binary Morse tree (dit = left, dah = right) built from 56 patterns:
A–Z, 0–9, 16 punctuation marks and the prosigns AR, SK, AS and SN (`+` is absent: `.-.-.` is AR; BT reads `=`,
KN reads `(`). Node ids are assigned in table order; ties in the token order use them.

**Token** [F: `hsmm/token.rs:21–29`]: (node, phase ∈ {AfterSpace, AfterMark}, u in hops (f32), score (f32),
hist, anchor hop). hist is the list of entries not yet committed: a glyph or a word boundary, each with the
sample time of the gap that closed it and the hop at which it was appended.

**Segments** [F: `hsmm/segments.rs`]:

| Type | Nominal length k·u | Allowed from | Then | Valid durations d | Type prior (nats) |
|---|---|---|---|---|---|
| Dit | 1 u | AfterSpace, if the dit child exists | dit child, AfterMark | 0.6 u … 1.5 u | ln 0.6 − 1.5 = −2.011 |
| Dah | 3 u | AfterSpace, if the dah child exists | dah child, AfterMark | 1.8 u … 4.5 u | ln 0.4 − 1.5 = −2.416 |
| EGap (element) | 1 u | AfterMark | same node, AfterSpace | 0.6 u … 1.5 u | ln 0.62 = −0.478 |
| CGap (character) | 3 u | AfterMark at a glyph node | emit glyph; root, AfterSpace | 1.8 u … 4.5 u | ln 0.28 = −1.273 |
| WGap (word) | 7 u | AfterMark at a glyph node | emit glyph and word boundary; root | 4.2 u … 10.5 u | ln 0.10 = −2.303 |
| Silence | ≥ 10 u | AfterMark at a glyph node | as WGap | 10 u … 80 u | ln 0.10 − 4 = −6.303 |

Duration prior: lp_dur = −(ln(d/(k u)))² / (2 · 0.22²) inside the window, 0 for Silence (flat), invalid
outside (`segments.rs:66–83`). A mark and a space between 1.5 u and 1.8 u has no valid type for that token
[I, from the windows]. Marks and spaces alternate (`segments.rs:47–53`, `token.rs:80–82`).

**Per-anchor step** [F: `hsmm/mod.rs:155–328`], at each anchor hop t:

1. u_max = max(7.5, the largest u in the current beam); reach_norm = ⌈10.5 u_max⌉, reach_sil = ⌈80 u_max⌉ hops
   (`mod.rs:212–214`). Stored anchors older than reach_sil are dropped (`mod.rs:229–235`).
2. Candidates (`mod.rs:236–265`): for every stored anchor s, oldest first, every token stored there in stored
   order, and every segment type in the table's order, with d = t − s: skip d = 0 and d > reach_sil; skip
   non-Silence types if d > reach_norm and Silence if d < ⌊10 u⌋. Otherwise the successor
   (`token.rs:70–127`), if valid, scores score' = score ± (C[t] − C[s]) + lp_dur + type prior, with + for a
   mark and − for a space, in f32 from the f64 difference.
3. **Speed per token** (`token.rs:86–90`): after a Dit, Dah, EGap or CGap, u ← clamp(u + 0.2 (d/k − u), 7.5, 56);
   WGap and Silence leave u unchanged. So a token's speed follows its own segments with a memory of about
   5 segments; the range is 60 to 8.04 WPM.
4. **Merge and beam** (`mod.rs:269–280`; `token.rs:31–62`): sort the candidates by score (descending, IEEE
   total order), then hist (lexicographic by glyph rank: word boundary 0, AR 1, SK 2, AS 3, SN 4, error 5,
   characters 16 + code point), then u (ascending), node, phase; keep the first of each key (node, phase,
   round(2u)), i.e. u in bins of 0.5 hop (1.33 ms); keep the best 12.
5. Commit (§1.5); the beam becomes the live set and is stored as the anchor (t, sample_ts, C[t], tokens)
   (`mod.rs:282–300`).

**What a score is** [I, derived from step 2]: over a common time span, Σ(+llr over mark hops) +
Σ(−llr over space hops) = 2 Σ ln p(û | the path's label) − Σ [ln p(û | key down) + ln p(û | key up)], and the
last sum is the same for every path. So a score is twice the path's Gaussian log-likelihood plus its duration
and type log-priors once: the evidence is weighted double against the priors, before counting the correlation
between neighboring hops (§4.1).

**Seeding** [F: `mod.rs:160–206`]: only when present rises (false to true). All stored tokens' scores are
shifted down by the score of the best token of the latest beam (so that token reads 0); a new anchor at this hop
holds five seeds: root, AfterSpace, u = 9, 13, 18, 26, 38 hops (50.0, 34.6, 25.0, 17.3, 11.8 WPM), score 0,
empty hist. Old tokens and seeds then compete. Nothing is decoded before the first rising edge. The
specification also seeds "after any committed Silence" (`SPEC-decode-core-v2.md:258–264`); the code does not,
by the plan's decision (`docs/superpowers/plans/2026-09-09-decode-core-v2.md:1659`).

**Speed reported** [F: `mod.rs:131–136`; `decoder.rs:307–310, 351–363`]: the u of the best live token that is
not a fresh seed; a SpeedUpdate is emitted whenever 450/u moves by ≥ 1 WPM from the last report, and the same
u sets the evidence window h.

### 1.5 Commit rules (`decode/hsmm/commit.rs:134–194`)

After pruning, in a loop [F]:

1. From every token, drop leading hist entries already committed (a list `sealed` of (sample_ts, is
   character) pairs; needed because stored older anchors still hold copies of committed entries); re-sort.
2. Stop if the best token's hist is empty. Let head be its oldest entry.
3. **Consensus:** every token in the beam (up to 12, at this anchor only) has the same oldest entry (same
   glyph, or both word boundaries). **Forced:** t − head's append hop > 25 · u_best hops (25 dits; 1.2 s at 25 WPM).
4. If neither, stop. Else emit head with its confidence (§1.6), drop tokens whose oldest entry differs, remove
   the entry from the rest, add it to `sealed`, and repeat.

Committed entries are final: no event retracts or corrects them [F: `decode/events.rs:9–94`]. At the end of
the stream (`mod.rs:332–404`; `decoder.rs:541–575`) the remaining hops are emitted with the forward window
truncated, then the best token's whole hist is committed, and an open character (AfterMark at a glyph node)
is emitted with confidence 0.5.

### 1.6 Confidence and alternatives

[F: `commit.rs:12–60`; `decoder.rs:327–346, 470–500`]

- s_alt = the highest score among the other beam tokens whose entry at that position differs (including
  tokens that have no entry there yet); if none, s_best − 24 (= 4κ).
- confidence = σ((s_best − s_alt)/κ), κ = 6 nats, σ the logistic function: 0.5 for a tie, 0.982 for no rival.
- Alternatives: up to two other glyphs at that position, each with σ((s_i − s_best)/κ) (below 0.5), ordered by
  score.
- The glue multiplies by q = clamp(SNR_2500/20, 0.3, 1.0), where SNR_2500 = 20 log₁₀(M/n) − 14.3 (dB SNR in
  2500 Hz; 14.3 dB is the bandwidth ratio 10 log₁₀(2500/93.75)) taken at the present hop nearest the entry's
  sample time.

### 1.7 Output format

[F: `decode/events.rs:9–94`; `decoder.rs:327–418, 880–896`]

- `CharDecoded {track_id, sample_ts, glyph, confidence, alternatives}`; sample_ts is the input sample counter
  at the anchor where the closing gap began (the last hop of the character's last mark).
- `WordBoundary {track_id, sample_ts, confidence}`, same instant.
- `SpeedUpdate {track_id, wpm}` (450/u, on every change of ≥ 1 WPM; about 30 per second in our runs, §3.3).
- `TrackMeta {track_id, sample_ts, snr_2500_db, freq_hz}` every 375 hops (1 s).
- Text (`events_to_text`): characters appended in order, one space per word boundary, prosigns dropped.

**Latency** [M]: on a clean 25 WPM stream (`lead4`, §3.3) the 11 characters were returned 0.36 to 1.26 s
(median 0.55 s) after their closing gap began: the evidence delay h (192 ms) plus the wait for consensus.

## 2. Parameters

Every parameter of the hsmm chain and of the channel it reads. "Basis" is manta's own statement; "Class" uses
this project's terms. Time base: **dits** when the value scales with u, **s** when it is fixed in time. SPEC v1
and SPEC v2 are manta's `docs/SPEC-decode-core.md` and `docs/SPEC-decode-core-v2.md`.

| Parameter (manta name) | Value | Physical meaning | Where | manta's basis | Class |
|---|---|---|---|---|---|
| **Channel** (replaced by the emulation of §4 in a port) | | | | | |
| Channel spacing Δ | 93.75 Hz | channel width; N = fs/Δ a power of two | `crates/manta-dsp/src/channelizer.rs:8`; SPEC v1 §1.1 | design choice | heuristic |
| Taps per branch L | 8 | prototype support 8/Δ = 85.33 ms | `crates/manta-dsp/src/proto.rs:7` | SPEC v1 §1.2 | heuristic |
| Kaiser β | 7.857 | stopband target 80 dB below the passband | `proto.rs:5` | β = 0.1102 (A − 8.7), A = 80 dB of stopband attenuation | derived from a heuristic target |
| Cutoff | 46.875 Hz (Δ/2) | −6.02 dB relative to the passband | `proto.rs:34–50` | "adjacent channels cross at −6 dB" | heuristic |
| Hop | N/4 samples | 375 per s, 2.667 ms | `channelizer.rs:110`; `decode/lib.rs:19–21` | 4× oversampling | heuristic |
| ENBW, delay | 82.23 Hz, 42.66 ms | noise bandwidth, linear-phase delay | `docs/DECISIONS/2026-09-09-decoder-recall-research.md:37` | measured | property |
| **Noise floor** (`decode/noise.rs`) | | | | | |
| `tau_ms` | 40 ms (α = 0.0645) | EMA of channel power | `noise.rs:33, 52` | reuses the detector gate's EMA | heuristic; s |
| `noise_window_ms` | 1500 ms = 563 hops | minimum-statistics window | `noise.rs:29, 53` | default | heuristic; s |
| `noise_min_bias_db` | 2.16 dB (× 1.644) | the noise estimate relative to the window minimum | `noise.rs:10–20, 30, 54` | measured: the uncorrected minimum read −2.159 dB relative to the true mean on i.i.d. exponential samples | measured (on channel-filtered noise the corrected estimate reads −1.85 dB relative to the true mean [M], §4.1) |
| `spectral_min_bias_db`, `spectral_beta` | 1.5 dB (factor 1.413), 0.5 | weights of the neighbor-channel reference | `noise.rs:31–32` | default | heuristic; unused by the oracle |
| **Evidence** (`decode/evidence.rs`) | | | | | |
| `tau_a_ms` | 8 ms (α_a = 0.2835) | amplitude EMA for M | `evidence.rs:25, 81` | "a single-hop impulse must not set the mark level" | heuristic; s |
| `hold_dits` | 4 dits | half-width of the mark-level window and the evidence delay | `evidence.rs:23, 82, 107` | default | heuristic; dits |
| `u_init_hops` | 15 hops (40 ms, 30 WPM) | u_ref before any token | `evidence.rs:26` | default | heuristic |
| Gate | M ≥ 2 n | peak 6.02 dB above noise RMS amplitude | `evidence.rs:244` | "the same bar as v1 §3.2" | heuristic |
| û clamp | −0.5 … 1.5 | normalized amplitude range | `evidence.rs:246` | SPEC v2 §1.5 | heuristic |
| `sigma_u` | 0.30 | spread of û; LLR slope 11.1 nats per unit û | `evidence.rs:21, 247` | default | heuristic (the LLR form is derived) |
| `llr_clip` | 10 nats per hop | bounds one hop's influence | `evidence.rs:22, 248–250` | "impulse QRM" | heuristic |
| `fallback_hops` | 8 hops = 21.3 ms | extra anchors | `evidence.rs:24, 325` | "cuts the work ~5×" | heuristic; s |
| `MAX_RETAIN` | 4096 hops = 10.9 s | delay-line bound | `evidence.rs:77` | implementation bound | — |
| **Segment decoder** (`decode/hsmm/`) | | | | | |
| `dur_sigma` | 0.22 (in ln d) | log-normal width of every duration | `mod.rs:28`; `segments.rs:82` | "≈ ±25% at 1σ; keying jitter 10–20% plus edge rounding" | heuristic |
| Duration window | 0.6 … 1.5 × k u | hard limits | `segments.rs:78` | SPEC v2 §4.3 | heuristic; dits |
| Silence window | 10 … 80 u, flat | 0.48 … 3.84 s at 25 WPM | `segments.rs:70–76` | SPEC v2 §4.3 | heuristic; dits |
| Nominal lengths k | 1, 3, 1, 3, 7, 10 | dit, dah, element, character, word gaps, silence | `segments.rs:30–37` | PARIS | derived (standard) |
| Type priors | 0.6, 0.4, 0.62, 0.28, 0.10, 0.10 e^(−4) | dit, dah, gaps, silence | `segments.rs:55–64` | "from Morse-text statistics (≈)"; the −4 is unexplained | heuristic |
| `mark_insert_penalty` | −1.5 nats per mark | against noise read as `E` | `mod.rs:29`; `segments.rs:57–58` | stated purpose only | heuristic |
| `beam` | 12 tokens per anchor | | `mod.rs:30, 280` | default; 24 tried on one vector, no gain | heuristic |
| Merge key | (node, phase, round(2u)) | u bins of 0.5 hop | `token.rs:60–62` | SPEC v2 §4.6 | heuristic |
| `lookahead_dits` | 25 dits | forced commit (1.2 s at 25 WPM) | `mod.rs:31`; `commit.rs:175–176` | "≈ two characters plus a word gap" | heuristic; dits |
| `speed_alpha` | 0.2 per segment | per-token speed update | `mod.rs:32`; `token.rs:87–90` | default | heuristic |
| `seed_units_hops` | 9, 13, 18, 26, 38 hops | 50.0, 34.6, 25.0, 17.3, 11.8 WPM | `mod.rs:33` | "≈ 50, 35, 25, 17, 12 WPM" | heuristic |
| `u_min`, `u_max` | 7.5, 56 hops | 60 to 8.04 WPM | `mod.rs:35–36` | "60…8 WPM" | heuristic |
| `conf_kappa` κ | 6 nats | confidence scale | `mod.rs:34`; `commit.rs:44–58` | default | heuristic |
| No-rival margin | 4κ = 24 nats | confidence 0.982 | `commit.rs:44–48` | SPEC v2 §4.9 | heuristic |
| reach_norm, reach_sil | ⌈10.5 u_max⌉, ⌈80 u_max⌉ hops | longest gap and silence | `mod.rs:213–214` | from the windows | derived |
| **Glue** (`decode/decoder.rs`) | | | | | |
| q | clamp(SNR_2500/20, 0.3, 1.0) | confidence scaling | `decoder.rs:328–331` | from v1 §4.5 | heuristic |
| `SNR_BW_CORR_DB` | 14.3 dB, the bandwidth ratio 10 log₁₀(2500/93.75) | in-channel SNR to dB SNR in 2500 Hz | `decoder.rs:96` | uses the spacing, not the 82.2 Hz ENBW (which would give 14.8 dB) | derived (nominal) |
| `WPM_REPORT_DELTA` | 1 WPM | SpeedUpdate threshold | `decoder.rs:86` | v1 §5 | heuristic |
| `META_INTERVAL_HOPS` | 375 hops = 1 s | TrackMeta cadence | `decoder.rs:85` | v1 §5 | heuristic |
| Open-character confidence at the end | 0.5 | σ(0) | `mod.rs:392–401` | SPEC v2 §4.10 | heuristic |

Sources of the stated bases: `docs/SPEC-decode-core-v2.md` §1–§7 (lines 32–354) and
`docs/superpowers/specs/2026-09-09-decode-core-real-hf-design.md` §5.1 and §5.4 (lines 469–578). The design
document predicted a bias correction of "≈ +2.5 dB" (lines 482–484); the code uses the measured 2.16 dB.

## 3. Silence and overs

### 3.1 What the code does after a long key-up

In order, after the last mark of an over [F unless marked]:

1. **Evidence.** For h hops (4 dits) after the last mark, M still holds the mark level, so key-up hops get
   llr between about −5.6 nats (û ≈ 0, noise well below the mark) and −10 nats each. Then M falls to the
   maximum of the smoothed noise; when M < 2n the gate closes: present = false, llr = −10 for every hop, and
   anchors come only every 8 hops
   (`evidence.rs:244–251, 324–326`). In noise the gate reopens on noise peaks (§3.3). The noise floor keeps
   running (1.5 s minimum). There is **no stored amplitude**: the mark level is only the ±4-dit window, so
   the old level is gone h hops after the last mark and the next over's level is seen h hops before its first
   mark [I, from `evidence.rs:236–243`].
2. **Tokens.** Nothing is reset. Tokens that ended on the last mark close the character with a CGap, WGap or
   Silence (≤ 80 dits; flat prior, type prior −6.3 nats), and keep their u: WGap and Silence do not update
   speed (`segments.rs:39–41`). Stored anchors are kept for 80 · u_max hops (3.84 s at 25 WPM) and then
   dropped (`mod.rs:229–235`).
3. **Longer than 80 dits.** No space segment can be longer than 80 u, and a space must be followed by a mark
   (`segments.rs:70–76, 47–53`). So once a key-up is longer than 80 dits of every surviving token, the best
   path must contain marks inside it [I, from those lines]. In noise-free runs this appears as trains of `E`s
   [M, §3.3]; §3.2 traces how they arise.
4. **Re-seeding.** Only when present rises again (`mod.rs:160–206`): when the next over's first mark enters the
   mark-level window, i.e. about h hops (4 dits of the old speed) *before* that mark in evidence time, or
   whenever noise lifts M above 2n. Seeds are placed at that rising hop, in phase AfterSpace, so their first
   segment must be a mark starting there: a mark of length h + (first element), or a short phantom mark in the
   key-up followed by a gap [I, from `mod.rs:197–205`, `segments.rs:47–53`]. The old tokens, shifted so the
   best reads 0, keep competing with their old speeds.
5. **Speed after the pause.** Each surviving token keeps its u; the seeds offer 50, 34.6, 25, 17.3 and 11.8 WPM.
   Whichever explains the new over's first segments best wins; u then moves 20% of the way toward each new
   segment's implied dit.

### 3.2 Traced: what fills a long pause

`build/manta-inst` printed the beam at every anchor of the noise-free 25 WPM run with an 8 s pause
(`build/manta-ref/exp/trace-25-8.txt`; hop numbers are evidence hops, 375 per second) [M]:

1. **The beam stops changing.** The last mark ends near hop 2256 (6.02 s). From hop 2400 on, all 12 tokens
   are "silence since the last mark" (a Silence segment re-derived from the last mark's anchors) or a few
   variants with one phantom dit, and every one gains the same 10 nats per hop of silence. They differ in
   their *earlier* history: the last character's closing gap starting at hop 2248, 2253 or 2256, and minority
   readings of the last characters (`N N`, `K E` for the final C, or a word ending in `K` at hop 2224).
   Because the beam keeps 12 tokens whatever their score gap (here up to about 320 nats), the minority
   readings are not dropped.
2. **So nothing is committed during the pause.** Consensus fails on those minority readings, and the forced
   commit never fires: the best token's oldest entry is re-created at every anchor (its Silence is re-derived
   from the last mark), so its append hop is always the current one (`token.rs:104–108`, `commit.rs:175–176`).
   The final C of the first over is committed at hop 3768 (10.05 s), the first anchor after the last minority
   reading (the word ending in `K` at hop 2224, u = 19.25 hops) has passed 80 of its own dits (1540 hops).
3. **The duplicate.** Consensus compares glyphs only (`commit.rs:167–170`), so lineages that closed the same C
   at hop 2253 and at hop 2256 agree; the commit seals (2256, character) (`commit.rs:191`), and the copies
   stamped 2253, still held by stored anchors, are committed again at hop 3896 (10.39 s).
4. **The phantom `E`s.** Once the pause exceeds 80 dits of the best tokens' own speed (u = 19.8 hops: 1584
   hops, 4.2 s), no single Silence spans it, and every surviving path contains a phantom dit, each at its own
   place (gap starts at hops 2720, 3752, 3776, 3848, …). Their oldest entries are all `E`, so consensus commits
   one (hop 3912); the copies with other sample times, still held by stored anchors, come back as further
   commits, the mechanism of item 3: 34 `E`s, keyed from 7.25 s to the end of the pause at 13.8 s and
   returned from 10.6 s on, mostly in pairs 8 hops apart.

Measured consequence [M, `exp/o-25-*.jsonl`]: the last character of an over is withheld until the next over
begins or the pause reaches about 4 s. Keyed at 6.01 s, it was returned at 7.14, 7.55 and 8.55 s for pauses of
0.5, 1 and 2 s (each time after the next over began), at 10.0 s (with a duplicate at 11.45 s) for a 4 s pause,
and at 10.25 s (duplicate at 10.58 s) for an 8 s pause.

### 3.3 Measured on synthetic streams

**Set-up** [M]. `build/manta-ref/exp/gen.py` writes per-hop power streams: complex baseband at 1500 samples/s,
a keyed carrier of 1 FS with 5 ms raised-cosine edges and PARIS timing, plus white complex Gaussian noise at a
stated S₅₀₀ (key-down carrier power over the noise power in 500 Hz); filtered by the channel emulation of §4
and decimated by 4; with the filter's 42.3 ms delay removed. Noise-free streams use a power floor of 1e-4 FS²
(−40 dB relative to the key-down power), as manta's own tests use an amplitude of 0.01 FS. `build/manta-ref`
decodes them with manta-decode at the commit studied, in the oracle configuration (fixed channel, a = √P, no
neighbor reference), and writes every event to a `.jsonl` file next to the stream. Text is "CQ TEST K1ABC" at
25 WPM unless stated. One run per cell unless seeds are listed.

**Lead-in before the first mark** (noise-free; `exp/lead*.jsonl`, `exp/w*.jsonl`):

| Lead-in | Decoded |
|---|---|
| 0 hops | `EQ TEST K1ABC` (C read as E) |
| 4, 8, 12 hops (11–32 ms) | `CQ TEST K1ABC` |
| 20 hops (53 ms) | `<AR>Q TEST K1ABC` (a phantom dit before C: `.-.-.`) |
| 40, 100, 1500 hops | `ECQ TEST K1ABC` (a phantom E) |
| 375 hops (1 s) | `E CQ TEST K1ABC` |
| 1 s, at 35 and 45 WPM | `E CQ TEST K1ABC` |
| 1 s, at 18 WPM | `E MTMT MMTM M T TTT M MTM TMMMM TM MTTT MTMT` (never locks) |
| 1 s, at 12 WPM | `E MTWT MMTO M T TTT G MTM TMWMMM TM MTTMT MTMT` (never locks) |
| 4 hops, at 12, 18, 35, 45 WPM | correct |

manta's own end-to-end tests prepend 4 hops of floor, because without them the first character failed; their
comment calls a stream that starts marking on its first sample "not a real-world scenario"
(`decoder.rs:1392–1417`). The sweep above shows that a lead-in longer than about one dit fails too, in another
way. In manta's live engine a decoder starts when the detector promotes the track, after the station's first
marks have held the detector's 40 ms average up for 19 hops (`docs/SPEC-decode-core.md:183–192`), so a long
silent lead-in is rare there [I]; in an oracle window, at the start of a recording, or for a new over on a
channel that stayed open it is the normal case.

**Noise before the message** (5 s of noise, S₅₀₀ = +15 dB, 3 seeds; `exp/n-*.jsonl`): the noise itself is
decoded as text (`E`, `I`, `T`, `S`, …) in every run. The message after it: at 40 WPM correct in 3 of 3; at 25
WPM `CQ TEST K1AB` in one run (the final C lost among noise `E`s), only `1ABC` in another, nothing in the
third (`MTMT MMTM …`); at 12 WPM lost in 3 of 3.

**Noise alone** (60 s, 3 seeds; `exp/z-*.jsonl`): 193, 196 and 196 characters, about 3.2 per second, mostly
`E`, `I`, `T`. The level does not matter (the chain is scale-free, §1.1).

**Two overs** (25 WPM "CQ TEST K1ABC", a pause, then "TEST W9XYZ" at 25, 35 or 15 WPM; 4-hop lead-in):

| Pause | Noise-free, then 25 WPM | then 35 WPM | then 15 WPM | S₅₀₀ +15 dB, 25→25 WPM, seeds 1 / 2 |
|---|---|---|---|---|
| 0.5 s | correct | correct | `… M T TTT C T W9XYZ` | — |
| 1 s | correct | correct | `… K1ABC C T EST W9XYZ` | correct / correct + trailing `E` |
| 2 s | correct | correct | `… TM T TTT T W9XYZ` | `TESST` + trailing `E` / correct + trailing `E` |
| 4 s | `K1ABCC`, rest correct | `K1ABCC`, rest correct | `K1ABCC`, rest correct | `I E EI EE EE I` in the pause / pause noise and `M T TTST` |
| 8 s | `K1ABC C EE E EEE …` (34 phantom `E`s) | the same | the same, then `T EST` | pause noise and `M T TI TT M` / and `M T TTT I E EIT` |

(Files `exp/o-<speed>-<pause>.jsonl` and `exp/m-<pause>-<seed>.jsonl`; "trailing `E`" is decoded from the
final second of noise.) Read [M]: with pauses up to 2 s the next over at the same or a higher speed decodes; a
speed *drop* to 15 WPM spoils its first word at every pause up to 2 s (at 1 s, `T EST`: a 15 WPM character gap
read as a word gap by the surviving 25 WPM tokens [I]). Noise-free, the last character of the first over is
committed twice at 4 and 8 s (and at 1 s before the 15 WPM over), and at 8 s 34 phantom `E`s fill the
pause. With noise, pauses of 4 and 8 s fill with noise characters and the next over's first word is wrong in
3 of 4 runs. The two copies of the duplicated character carry sample times 3 hops apart (hops 2253 and 2256,
6.01 and 6.02 s, in the 8 s run), so the `sealed` list, which matches sample times exactly, does not catch it
(§3.2, item 3) [M; F: `commit.rs:146, 191`].

**Speed reports** [M]: at a true 25 WPM, about 30 SpeedUpdate events per second, median 25.1 WPM, 90% of them
between 23.0 and 27.8 WPM, noise-free or at S₅₀₀ +15 dB (`exp/e0.jsonl`, `exp/lead4.jsonl`,
`exp/cpu-noisy.jsonl`):
the reported u is the best token's at each emitted hop, and the best token changes between anchors [I].

### 3.4 Answers

- **Speed:** kept, per token, across any pause (WGap and Silence do not update u); new candidate speeds come
  only from the five seeds at the next rising edge of the gate.
- **Amplitude and level:** nothing stored; the mark level is a ±4-dit window and the noise floor a running
  1.5 s minimum, so a new over's level is used as soon as its first mark is within 4 dits.
- **Tokens:** never cleared; re-seeded only at the gate's rising edge, not after a silence; old tokens are
  normalized to score 0 at that moment and compete with the seeds.

## 4. What depends on manta's front end, and the adaptation

### 4.1 Dependencies

| Depends on | Where it enters | Why it matters |
|---|---|---|
| The hop rate, 375 per s | every duration: u, h, seeds, u range, fallback anchors, windows, look-ahead (§2) | all constants are in hops |
| The channel bandwidth (ENBW 82 Hz) | the noise statistics behind the gate (M ≥ 2n), σ_u, and the 2.16 dB bias correction | a wider channel has more noise per hop and other order statistics |
| Correlation between hops | the noise bias correction; the sum of llr over a segment | manta calibrated its bias on independent samples; the channel's power is correlated over about 3 hops (lag 1, 2, 3: 0.83, 0.47, 0.15 [M]), so its floor reads **1.85 dB below the true mean noise power** [M: `build/manta-ref/exp/noise_bias.py`]; and a segment's evidence counts each independent observation about 4 times [I] |
| The step response (10–90% in 9.7 ms: `docs/superpowers/specs/2026-09-09-decode-core-real-hf-design.md:162–167`) and delay (42.7 ms) | edge placement, timestamps | delay only shifts times |
| Magnitude only | a = √P | no phase is needed |
| Centering: a station anywhere within ±46.9 Hz of a channel center | up to −6.0 dB relative to the passband at the edge | ours is centered by the detector's anchor |
| The neighbor channels k ± 2…4 | optional noise reference | unused by the oracle |
| `SNR_BW_CORR_DB` = 14.3 dB | q only | assumes 93.75 Hz |
| The track lifecycle | when the stream starts and how long pauses last | manta starts at promotion and stops after a 5 s hang; ours opens 0.5 s after the 1 s average first shows the station and closes 10 s after that average falls below 3 dB SNR per bin, about 16–17 s after a strong station stops (`docs/signal-processing.md` §6, §9) |

[F for the "where" column, from §1–§2; I for the "why" column except where marked M.]

### 4.2 Options

**A. Emulate manta's channel at 1500 samples/s, run manta's code unchanged at 375 hops/s (recommended).**
Per channel, after the engine's mix to the detector's anchor (as the bank decoder does): a linear-phase FIR of
128 taps at 1500 samples/s, h[i] = sinc((i − 63.5)/16) · I₀(β√(1 − (2i/127 − 1)²))/I₀(β), β = 7.857,
normalized to Σh = 1, which is manta's `design_prototype(16, 8)` (`proto.rs:36–51`): the same 85.33 ms
continuous-time kernel sampled at 1500 instead of fs. Keep every 4th output: 375 complex values per second;
P = |y|², a = √P in f32; then the noise floor, evidence and segment decoder exactly as manta's. One engine
block of 32 samples (21.33 ms) is exactly 8 hops. Cost: 128 complex multiply-adds per hop, 48 000 per second
per channel [I]. In the engine this is one more `Decoder` (`engine/include/kz4ap/decoder.hpp`): `process`
mixes, filters and decimates, then runs manta's per-hop loop (`decoder.rs:281–314`) and turns committed
entries into appended characters; `flush` runs manta's `finish` (`decoder.rs:541–575`); `reset` starts a new
instance [I].

Computed [M: `build/manta-ref/exp/response.py`, direct DTFT of the three filters]:

| f (Hz) | manta at 192 kS/s (dB) | emulation at 1500 (dB) | our ±150 Hz channel filter (dB) | cascade − manta (dB) |
|---|---|---|---|---|
| 0 | 0.000 | 0.000 | 0.000 | 0.000 |
| 10 | −0.002 | −0.002 | +0.001 | 0.000 |
| 20 | −0.012 | −0.014 | +0.001 | −0.001 |
| 30 | −0.431 | −0.444 | −0.003 | −0.016 |
| 40 | −2.588 | −2.607 | −0.017 | −0.036 |
| 46.875 | −6.022 | −6.022 | −0.038 | −0.038 |
| 60 | −19.78 | −19.63 | −0.12 | +0.03 |
| 75 | −66.3 | −64.6 | −0.34 | +1.4 |
| ≥ 110 | ≤ −87.5 | ≤ −88.2 | | |

All values in dB relative to the passband (0 Hz). ENBW: 82.23 Hz (manta, N = 2048 at 192 kS/s) against
82.14 Hz (emulation); delay 42.66 ms against 42.33 ms. Our channel is centered by the anchor, so the station's
residual offset (≤ 11.7 Hz before any tracking) costs at most 0.003 dB relative to the passband, where manta's
own stations sit anywhere within ±46.9 Hz of a channel center [I, from the table].

**B. Run manta's filterbank on the whole band.** Port `manta-dsp`'s channelizer as a second channelizer beside
ours (a 2048-channel, 16 384-tap polyphase filterbank at 192 kS/s), take the channel nearest each track, and
drop our own channel for this decoder. Faithful to the letter, but it is a second shared front end, loses our
anchor centering, and adds manta's 46.9 Hz quantization [I].

**C. Re-scale manta's constants to our 1500 samples/s, 252 Hz channel.** Not a faithful port: the noise per
hop is 4.9 dB higher (the ENBW ratio, 10 log₁₀(252/82.2)), and the gate, σ_u and the bias correction were set
for manta's channel [I].

**Recommendation: A.** It changes nothing in manta's logic, needs one small filter, and its fidelity can be
shown by the response table above and by the end-to-end check in §5.4 (d).

### 4.3 What A cannot reproduce, and does not need to

- The live engine's per-hop choice among three owned channels (the oracle does not do it either).
- The neighbor-channel noise reference (the oracle passes none; manta's gate numbers were measured without it,
  `docs/SPEC-decode-core-v2.md:141–150`).
- manta's detector and track lifecycle: our engine decides when a channel opens and closes. Pauses on our
  channels can last until the track dies, about 16–17 s after a strong station stops, against manta's 5 s hang
  (`docs/SPEC-decode-core.md:190–192`), which makes the pause behavior of §3.2 more frequent [I].

## 5. Verifying a faithful port

### 5.1 manta's own tests

`cargo test -p manta-decode --release` runs 120 unit tests; all pass here [M]. 42 cover the hsmm chain [F,
`decode/`]: hsmm (14: `mod.rs` 6, `token.rs` 5, `segments.rs` 2, `commit.rs` 1), `evidence.rs` 11,
`noise.rs` 2, `tree.rs` 7, and 8 end-to-end tests in `decoder.rs:1391–1695` (clean text at 19, 35 and 45 WPM;
speed convergence; commit within the look-ahead; no duplicate commits; bit determinism; confidence and
alternatives; no spurious speed report at a re-seed). Their inputs are hand-built 375 Hz amplitude envelopes,
so they port directly to C++ tests.

manta's golden vectors V1–V10 and VR1–VR8 are end-to-end (filterbank, detector, spot validator) and run
through `manta-cli` (`crates/manta-cli/tests/golden_*.rs`); with the hsmm engine manta reports 4 of 11 V and 3
of 9 VR passing (`docs/DECISIONS/2026-09-09-decode-core-v2-stage2-gate.md:142–207`), measured before the review
fixes of §6.1. They test the whole of manta, not the decoder, and are not the port's reference [I].

### 5.2 Toolchain

- **Before:** no Rust on this machine; Visual Studio Build Tools 2026 present (its linker serves Rust's MSVC
  target) [M].
- **Installed in this study, inside the repository:** `build/rust/rustup-init.exe` (12.7 MB) run with
  `CARGO_HOME=build/rust/cargo`, `RUSTUP_HOME=build/rust/rustup`, `--no-modify-path --profile minimal
  --default-toolchain 1.98.0` (manta's pin, `rust-toolchain.toml`); a few minutes, no prompt, nothing
  refused. No PATH, registry or profile change; nothing outside `build/` [M]. To use it in a shell: set
  those two variables and put `build/rust/cargo/bin` first on PATH. Deleting `build/rust` removes it.
- **Disk** [M]: `build/rust` 1.1 GB: the toolchain 614 MB (with rustfmt and clippy, which manta's
  `rust-toolchain.toml` adds on first use), the crates.io cache 441 MB, manta's `coppa` crates from GitHub
  10 MB (cargo fetches them because it resolves manta's whole workspace). Build outputs: 647 MB in
  `build/third-party-src/manta/target` (217 MB before the CLI build below), 51 MB in `build/manta-ref/target`.
- manta-decode itself needs only serde (dev: approx, serde_json, toml, criterion).
- **The whole of manta builds too** [M]: `cargo build --release -p manta-cli` took 2 min 48 s and produced
  `build/third-party-src/manta/target/release/manta.exe` (8.9 MB). Its `oracle` subcommand reads our recording
  format as it is: a 16-bit stereo WAV, I left and Q right, divided by 32768, with the center frequency from an
  optional `<stem>.json` sidecar `{"center_freq_hz": …}` (`crates/manta-input/src/lib.rs:150–205`), and a spot
  list in RBN's CSV format (`oracle.rs:185–232`: columns callsign, freq in kHz, dx, db, date, speed). So
  manta's own decoding of our labeled stations, through manta's own filterbank, needs only a script that writes
  that CSV from our labels [I]; its `--jsonl` output carries each station's decoded text. Not run in this
  study.
- A Linux build machine could build the same with rustup in the repository folder there [I].

### 5.3 Reference driver and study files (all under `build/`, git-ignored, temporary)

- `build/manta-ref/` (Rust): `manta-ref <power.f32> <events.jsonl> [samples_per_hop]` reads little-endian f32
  power per hop, feeds manta's `TrackDecoder` with the hsmm engine exactly as manta's oracle does
  (`push_hop(√p, p, None, i · 512)`, then `finish()`), writes one JSON line per event tagged with the input hop
  at which it was returned, and prints the text.
- `build/manta-inst/` (Rust): manta's `tree.rs`, `evidence.rs`, `noise.rs` and `hsmm/*.rs` copied unmodified,
  plus a debug dump appended to `hsmm/mod.rs`, driven by a copy of the glue loop; prints the beam at each
  anchor. This is how module-level golden values can be dumped without touching manta's tree.
- `build/manta-ref/exp/gen.py`, `show.py`: the synthetic streams and the summaries of §3.3; the `.f32` streams
  and `.jsonl` event files of every run listed there.
- `build/manta-ref/exp/noise_bias.py` (the noise-floor bias and the hop correlation of §4.1) and
  `response.py` (the response table of §4.2), Python with numpy, run with the repository's `.venv`;
  `exp/trace-25-8.txt`, the beam trace of §3.2.
- `build/manta-ref/src/bin/bench300.rs` (manta's bench workload, timed once) and `timefile.rs` (the decoder
  timed on a power file), §6.2; `build/manta-ref-gate/` builds the same two against the gate's code,
  downloaded as a tarball of commit `bd492c36da6a336f4af846858c2823958242ff65` into
  `build/third-party-src/manta-bd492c36/`.

### 5.4 The equivalent of Plan A's golden-value tests

Plan A checked the bank port module by module against values the Python prototype wrote, then the whole
decoder on the development set (`docs/plans/2026-10-03-milestone-2c-bank-port.md:41–42`). For this port the
reference is manta's Rust, and a stricter criterion is reachable [I]:

- **(a) Unit level:** port manta's 42 hsmm-chain tests one to one (same inputs, same assertions).
- **(b) Module golden values,** written by a `manta-inst`-style driver from manta's own code on fixed streams:
  per emitted hop, the evidence (llr, present, anchor, C, M, n) with u_ref scripted; per input hop, N; per
  anchor, the beam (node, phase, u, score, hist) and the commits. Criterion: **bit-identical** f32 and f64
  values and identical discrete states.
- **(c) System level:** identical event streams (glyph, sample time, confidence bits, alternatives, speed
  reports) from the Rust driver and the C++ port on (i) manta's test envelopes, (ii) this study's synthetic
  streams (cold starts, overs, noise), (iii) the development set's recorded oracle channels
  (`docs/plans/2026-10-06-development-set-redesign.md`), mixed by their labeled offset as the bank's replay
  does and passed once through the C++ channel emulation, with the resulting power files read by both.
- **(d) The channel emulation,** which is ours, not manta's: its taps equal manta's `design_prototype(16, 8)`
  to 1e-7 (manta's own pinning tolerance, `docs/SPEC-decode-core.md:63–65`); its response as in §4.2; and,
  optionally, its power stream against manta's real filterbank on a synthetic 192 kS/s I/Q file with a keyed
  tone on a channel center, to a stated tolerance (not bits: the first stages differ).

**Conditions for bit identity** [I, from manta's code and the language rules]:
- Keep manta's types: f32 for llr, a, M, n, N, score and u; f64 for C, the hop arithmetic and the EMA
  constants computed before the cast (`evidence.rs:81`, `noise.rs:52`). Same order of operations.
- No fused multiply-add contraction. Rust never contracts. MSVC's default `/fp:precise` does not; GCC in GNU
  mode contracts where FMA exists (ARM64, or x86 with `-mfma`), so the ported files need `-ffp-contract=off`
  on the Linux and Raspberry Pi builds.
- f32 `ln`, `exp` and `powf` from the same C runtime on both sides of a comparison (Rust's `f32::ln` calls
  the platform's `logf`; `sqrt`, `round` and `ceil` are exact anyway); compare on one platform, keep these
  calls out of loops the C++ compiler may vectorize with its own math routines, and trace any remaining
  difference to a last-bit library difference as Plan A did. Constants such as ln 0.6 may be folded by the
  Rust compiler; take their exact f32 values from manta's build (the driver can print them) rather than
  recomputing them.
- Orderings exactly as Rust's: `total_cmp` (−0.0 before +0.0), lexicographic comparison of hist with a shorter
  prefix first, `round` half away from zero (as `std::round`), casts that truncate toward zero.
- The Morse tree built from the same table in the same order (node ids break ties).
- Time stamps: any strictly increasing sample counter works; sealing compares them for equality only.

## 6. CPU and code structure

### 6.1 What manta reports, and for which code

- Criterion bench `hsmm_300_tracks` (`crates/manta-decode/benches/hsmm_300_tracks.rs`): 300 decoders × 10 s
  of a clean 35 WPM envelope (dit, gap, dah, gap repeated), decoder only: **10.625 s** on an Apple M-series
  core, against a budget of 2.5 s, i.e. **3.54 ms of CPU per channel-second**, 4.25× over
  (`docs/DECISIONS/2026-09-09-decode-core-v2-stage2-gate.md:277–288`, below `stage2-gate.md`) [F].
- Whole pipeline on the 15-minute B2 recording: hsmm 2501.6 s user CPU against 634.8 s for manta's legacy
  decoder (`stage2-gate.md:65–76`) [F].
- SPEC v2's estimate: about 430 candidate evaluations per anchor and about 100 anchors per second at 35 WPM,
  0.4 MFLOP/s per channel (`docs/SPEC-decode-core-v2.md:431–440`) [F].
- **These numbers, and the gate's accuracy numbers, predate the code studied.** The gate was measured at
  `70da368`, "Task 11's bench/determinism commit" (`stage2-gate.md:10–12`), which by its description is
  `bd492c36` in the merged PR's history (the branch was rewritten). After it, 61 more commits, among them 21
  rounds of review fixes, changed `hsmm/mod.rs` (+289 −20 lines), `hsmm/commit.rs` (+126 −13), `evidence.rs`
  (+266 −38) and `decoder.rs` (+200 −4) before the merge (GitHub compare `bd492c36...65169130`; much of it
  comments and tests), including the re-seed score shift, sealing by sample time, per-hop speed feedback to
  the evidence window and the anchor look-ahead [F]. No later measurement is published [F]. Reference numbers
  for this code have to come from running it.

### 6.2 Measured here

On this machine (a laptop with an Intel Core i7-12700H, Windows 11; Rust 1.98.0, release build), one decoder
at a time, the process pinned to one logical processor at high priority (`start /affinity 4 /high`) [M]:

| Workload | WPM | Channel time | CPU per channel-second |
|---|---|---|---|
| manta's bench workload: the clean dit-gap-dah-gap pattern, 300 decoders × 10 s (`build/manta-ref/src/bin/bench300.rs`) | 35 | 3000 s | 9.37 ms (the code studied); 9.09 ms (the gate's code, `bd492c36`) |
| "CQ TEST K1ABC K1ABC TEST" × 6, noise-free (`exp/cpu-clean.f32`) | 25 | 66.4 s | 16.1 ms |
| the same at S₅₀₀ +15 dB (`exp/cpu-noisy.f32`) | 25 | 66.4 s | 15.9 ms |
| the same at S₅₀₀ +15 dB (`exp/cpu-noisy-12.f32`) | 12 | 137.8 s | 31.5 ms |
| the same at S₅₀₀ +15 dB (`exp/cpu-noisy-40.f32`) | 40 | 41.7 s | 11.1 ms |
| noise only (`exp/cpu-noise.f32`) | — | 60 s | 6.3 ms |

Timed by `build/manta-ref/src/bin/timefile.rs` (best of 3, the whole `TrackDecoder` path including `finish`).
The three text streams at S₅₀₀ +15 dB decode without a single error (`exp/cpu-noisy*.jsonl`).

- The bench workload costs 2.6× manta's published 3.54 ms. The gate's code costs the same here as the code
  studied (9.09 against 9.37 ms), so the difference is the machine and platform, not the review changes [M; the
  cause I].
- The cost grows with the dit length, 31.5 ms at 12 WPM against 11.1 ms at 40 WPM: every anchor scans the
  stored anchors of the last 80 dits (`mod.rs:236–265`) [I].
- Noise alone is cheaper, 6.3 ms: the gate is closed most of the time, and anchors then come only every 8 hops
  [I].
- For scale, not like for like (another machine): the bank decoder costs about 42 ms and Matched about 0.6 ms
  per channel-second on the Linux test machine (`docs/signal-processing.md` §8c). The channel emulation of §4
  adds 48 000 multiply-adds per channel-second, well under 0.1 ms [I].
- **Caution:** unpinned, the same 300-decoder workload took 113 s instead of 28 s, and criterion's own run of
  manta's bench (`cargo bench -p manta-decode --bench hsmm_300_tracks`) estimated 12 732 s for its 100 samples
  and was stopped [M]; most likely Windows moved the long-running background process to its efficiency cores
  [I]. CPU measurements on this laptop need the process pinned.

### 6.3 Code structure and size

| File (`crates/manta-decode/src/`) | Lines | Production (with comments) | Code lines | Tests | Role |
|---|---|---|---|---|---|
| `hsmm/mod.rs` | 652 | 406 | 244 | 246 | anchors, candidates, merge, beam, seeding, finish |
| `hsmm/token.rs` | 324 | 129 | 114 | 195 | token, ordering, merge key, successor |
| `hsmm/segments.rs` | 119 | 85 | 68 | 34 | segment types and priors |
| `hsmm/commit.rs` | 264 | 195 | 86 | 69 | consensus and forced commit, margins |
| `evidence.rs` | 822 | 345 | 212 | 477 | EMA, mark level, gate, llr, anchors, prefix |
| `noise.rs` | 156 | 101 | 77 | 55 | minimum statistics |
| `tree.rs` | 280 | 192 | 157 | 88 | Morse table and tree |
| `lib.rs` | 42 | 27 | 18 | 15 | hop constants |
| `decoder.rs` (the functions on the hsmm path) | 247 of 1981 | 247 | 188 | ≈ 300 | glue: noise → evidence → hsmm → events, SNR, q |

"Code lines" exclude blank and comment lines [M, counted]. A third of the production lines are comments or
blank, many of the comments review history. The hsmm chain is about 1 160 lines of Rust code, about 1 000
without the Morse table and the glue's branches for manta's other decoders [M, counted; I for the second
figure]. With the 1500 → 375 emulation, a C++ port should come to about 1 200–1 500 lines with headers, and its
tests to about 1 500, against 4708 and 5181 lines for the bank decoder (`engine/src/bank/`,
`engine/include/kz4ap/bank/`, `engine/src/bank_decoder.cpp`; `engine/tests/bank/`) [I].

Things that cost CPU in manta's structure [I, from the code]: every anchor scans every stored anchor within
80 u_max (up to about 4500 hops back) × up to 12 tokens × 6 types; each candidate clones its token's hist
vector; the merge is quadratic in the number of candidates; `sealed` is searched linearly; where the gate is
open on noise, sign changes of llr are frequent, so anchors come more often than on clean keying (in noise
alone the gate is mostly closed, §6.2). A faithful port keeps all of this; optimizations come after the
reference check.

## 7. Licensing

- **The terms** [F]: `Cargo.toml:18` declares `license = "MIT OR Apache-2.0"` for the workspace;
  `crates/manta-decode/Cargo.toml` and `crates/manta-dsp/Cargo.toml` inherit it (`license.workspace = true`).
  `README.md:400–404`: "MIT OR Apache-2.0, at your option." `LICENSE-MIT`: "Copyright (c) 2026 Hagale
  Technologies, LLC" and the MIT permission notice. `LICENSE-APACHE`: Apache 2.0, with "Copyright 2026 Hagale
  Technologies, LLC" in its appendix (line 189). There is no NOTICE file.
- **Per-file notices** [F]: none. No source file in `manta-decode` or `manta-dsp` carries a copyright or SPDX
  line; the license is stated only at the repository level.
- **What our ported files should keep** [I; standard reading of the two licenses, not legal advice]: taking the
  code under MIT (the simpler of the two), every file that is a translation of manta's code keeps, in its
  header: the manta file(s) and commit it was translated from; "Copyright (c) 2026 Hagale Technologies, LLC";
  the MIT permission notice verbatim; and a line saying it was translated to C++ and modified for KZ4AP
  Skimmer, whose modifications are under the GPL-3.0 with the rest of the project. Ported tests are
  translations too and carry the same header. The full `LICENSE-MIT` text should also ship in the repository,
  e.g. `third_party/manta/LICENSE-MIT`, as `third_party/pocketfft/LICENSE.md` does for pocketfft. Under
  Apache-2.0 instead, the files would also need a prominent change notice and the recipients a copy of the
  Apache license; MIT avoids that.
- **Which files** [I]: `decode/hsmm/{mod,token,segments,commit}.rs`, `decode/evidence.rs`, `decode/noise.rs`,
  `decode/tree.rs` (the table), the hsmm path of `decode/decoder.rs` and the constants of `decode/lib.rs`; and,
  for the channel emulation, `crates/manta-dsp/src/proto.rs` (the filter design).
- **Golden data** written by running manta's code is program output, not a copy of the code; recording its
  provenance (manta commit, driver, input) is enough [I].

## 8. Open questions for the owner

1. **Port target.** manta at commit `1555009ce5c1` (hsmm code of 2026-09-10), in the oracle configuration
   (fixed channel, a = √P, no neighbor reference, refiner off), as manta's gate measured it: agreed?
2. **Faithful includes the defects.** Should the port reproduce, before any change, the behaviors of §3
   (cold-start seeding, noise decoded as text, the last character withheld during a pause and then committed
   twice, phantom marks after 80 dits, no re-seed after a silence although manta's specification asks for
   one), so that the first comparison is manta's behavior and each fix is a measured change?
3. **Numeric types.** Keep manta's f32/f64 split so that the port can be checked bit for bit (§5.4), although the
   engine otherwise computes in double?
4. **Units in the configuration.** manta states its constants in hops (2.667 ms). Store them in seconds and
   dits, converting to hops at the point of use with a test that the converted f32 values equal manta's
   literals exactly (they do for the defaults of §2 when the values in seconds are kept to double precision,
   since the conversion ends in a rounding to f32), or keep hops in the port's configuration?
5. **The channel emulation (§4, option A)** as the faithful stand-in for manta's filterbank, and whether to also
   run manta's own filterbank oracle on our I/Q recordings as an end-to-end cross-check.
6. **Output mapping.** manta's events never correct text. Map CharDecoded to an appended character with its
   confidence as `probability`, WordBoundary to `" "`, the best token's speed to `wpm`, and emit no
   corrections; and report speed per update, or smoothed? (manta reports about 30 speed changes a second,
   spread ±2.5 WPM around the true 25 WPM, §3.3.)
7. **Lifecycle.** Our channels stay open about 16–17 s after a strong station stops (the 1 s average's decay
   plus the 10 s timeout), manta's tracks 5 s; keep ours for the comparison?
8. **Tools.** Keep the Rust toolchain in `build/rust` (1.1 GB) for the duration of the port? Commit the
   reference driver and the golden-value writer (Rust sources under, e.g., `tools/manta-ref/`), or keep them
   in `build/`?
9. **License header wording** for the ported files (§7), including the line for our modifications.
10. **`docs/signal-processing.md`.** The port is a signal-processing change, so its section (the hsmm chain,
    the emulation, the parameter table of §2 with classes) goes into that document in the same commit; agreed
    that it describes manta's values with manta's stated bases, as in §2?
