# manta (HagaleTechnologies) — evaluation notes

Repository: https://github.com/HagaleTechnologies/manta
Evaluated: 2026-09-27

## 1. Summary and metadata

- **What:** a headless Rust daemon that channelizes a whole SDR passband with a
  polyphase filterbank (PFB), runs one classical CW decoder per detected
  signal, validates callsigns and emits RBN-style `DX de` spots over telnet
  plus a JSON Lines/WebSocket stream ([README](https://github.com/HagaleTechnologies/manta/blob/main/README.md)). [fact]
- **Author:** Tony Hagale (GitHub `thagale`, Austin, TX), the only human
  committer (107 commits); the rest are bots (dependabot, a
  "catalyst-cloud-connector" bot). [fact] The repository is heavily
  agent-driven: `CLAUDE.md`, `AGENTS.md`, `docs/superpowers/plans/*`, and CI
  workflows that wait for Codex review (`wait-for-codex.yml`). [fact] The
  docs are voluminous and very detailed, and parts read as
  machine-written. [inference]
- **License:** the GitHub API reports Apache-2.0, but the workspace manifest
  says `license = "MIT OR Apache-2.0"` ([Cargo.toml](https://github.com/HagaleTechnologies/manta/blob/main/Cargo.toml)) and both
  `LICENSE-APACHE` and `LICENSE-MIT` are present. [fact] Dual-licensed at the
  recipient's option.
  - **Copying or porting into GPL-3.0 KZ4AP Skimmer:** allowed. MIT is
    GPL-compatible, and Apache-2.0 is compatible with GPL-3.0 (one-way:
    Apache/MIT code may go into GPL-3.0 work, not the reverse). Choosing MIT
    is simplest: keep the copyright notice and permission text with any
    copied or substantially ported code (e.g. a header comment plus a
    `THIRD_PARTY` notice). If taken under Apache-2.0 instead, also keep the
    NOTICE file (none exists here) and mark modified files. [fact, standard
    license terms; not legal advice]
  - **Reverse direction / future merge:** KZ4AP Skimmer's GPL-3.0 code cannot
    be contributed into manta under MIT/Apache terms unless the owner (as
    copyright holder of his own code) relicenses or dual-licenses it. A
    merge into manta therefore means relicensing KZ4AP code to
    MIT/Apache; a fork of manta that absorbs KZ4AP code would become GPL-3.0
    as a whole (allowed, since manta is permissive). [inference]
  - Dependency `coppa` (same author, git-pinned) supplies FFT, audio and
    AWGN/Watterson channel models; its license was not checked. [fact]
- **Activity:** created 2026-07-07; last push 2026-09-21; 203 issues+PRs
  numbered; roughly 100+ commits in about 11 weeks; recent commits are CI
  plumbing, not DSP. [fact] 0 stars, 0 forks. [fact]
- **Maturity:** version 0.1.0, pre-1.0, **no GitHub releases and no tags**
  despite the README's "download a prebuilt binary" text. [fact] Extensive
  tests: golden-vector tests (v1..v17, VR), property tests, CPU-budget
  criterion benches, determinism tests, telnet/JSON/uplink acceptance tests;
  `ci-full.yml` passing on `main` as of 2026-09-21. [fact] The README states
  the default (`legacy`) decoder loses copy under HF fading and the new
  `hsmm` engine failed its own recall gate (see section 4). [fact] RBN
  uplink unverified, dry-run by default. [fact]

## 2. What it does

- **Inputs** ([ARCHITECTURE.md §3](https://github.com/HagaleTechnologies/manta/blob/main/ARCHITECTURE.md), [manta-input/src](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-input/src)): [fact]
  - SoapySDR (feature `soapy`, not in default builds): RTL-SDR, Airspy HF+
    (named the reference device), **SDRplay via SoapySDRPlay**; marked
    "needs hardware soak". A live RSP1B run is recorded in
    [docs/DECISIONS/2026-09-08-first-live-rsp1b-run.md](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-08-first-live-rsp1b-run.md).
    No native SDRplay API code. [fact]
  - OpenHPSDR/Hermes protocol over UDP (`hpsdr.rs`, 89 kB), KiwiSDR
    WebSocket IQ (12 kHz complex per channel), WAV/raw IQ files, and 48 kHz
    sound-card audio (Hilbert-transformed to analytic). [fact]
- **Bandwidth:** design center 96–192 kS/s complex; ceiling 768 kS/s;
  optional power-of-two halfband decimation (`--capture-rate-hz`). One
  band per daemon instance; multi-band means multiple instances. [fact]
- **Platforms:** Linux x86-64/ARM (Raspberry Pi 4 target: a 192 kS/s
  passband in one Pi 4 core), macOS, Windows (MAN-212). [fact]
- **Outputs:** text/JSON Lines on stdout; DX-cluster telnet server
  (port 7300, RBN `DX de` format); JSON Lines/WebSocket (port 7301, aimed at
  cqdx.app); Prometheus metrics; outbound RBN uplink client (dry-run by
  default, unverified against real RBN). [fact]
- **UI:** none. "Not an interactive receiver or panadapter. Use SDR++ or
  similar for a waterfall" (README, Non-goals). [fact]
- **Dependencies:** Rust 1.85+, tokio, rtrb lock-free rings, cpal (ALSA on
  Linux), optional SoapySDR C library, and the author's `coppa` crates
  (FFT, audio, channel models). [fact]

## 3. Signal chain compared with KZ4AP Skimmer

Symbols: f_s input complex sample rate (S/s); N FFT/filterbank size; Δ
channel spacing (Hz); f_o per-channel output rate (S/s); u dit length; WPM
words per minute (PARIS, dit = 1.2 s / WPM). KZ4AP references are to
`docs/signal-processing.md` (SP) and `docs/research/decoder-survey.md` (DS).

| Stage | manta | KZ4AP Skimmer |
|---|---|---|
| Spectrum for detection | The PFB output itself (squared magnitude per channel, Δ = 93.75 Hz, 375 frames/s); no separate windowed FFT ([SPEC-decode-core §1](https://github.com/HagaleTechnologies/manta/blob/main/docs/SPEC-decode-core.md)) [fact] | Separate Hann-windowed FFT, Δf ≈ 23 Hz, ENBW 35.2 Hz, hop N/2 (SP §2–4) [fact] |
| Channelizer | 4×-oversampled WOLA polyphase filterbank: N = f_s/93.75 (2048 at 192 kS/s), 8 taps/branch, Kaiser windowed-sinc prototype with −6 dB at ±46.875 Hz, 80 dB stopband; every channel computed every hop; f_o = 375 S/s complex. Measured prototype ENBW 82.2 Hz, group delay 42.7 ms at N = 1024 ([decoder-recall-research](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-09-decoder-recall-research.md), "Confirmed engineering observations") [fact] | Fast-convolution (shared FFT) channelizer, one stream per track only, ±150 Hz Blackman-sinc channel, decimated to r = 1500 S/s (SP §7) [fact] |
| Per-station selection | Track owns center channel ±1; per hop takes the max-power channel of the three, passes only sqrt(power) (phase discarded) to the decoder [fact]. Optional 11-tap, 30 Hz narrowband "refiner" that mixes the fractional-channel offset to 0 Hz is implemented but **not wired** into the engine (SPEC v2 §3) [fact] | Channel centered on the detected bin; residual offset up to about ±11.7 Hz; envelope (magnitude) of the complex stream (SP §7–8) [fact] |
| Noise floor / detection | Per channel: 25th percentile of dB power over 10 s (histogram), min with block-of-32 median + 3 dB; gate: 40 ms EMA ≥ floor + 6 dB (SPEC text; the recall research says the actual code default is 12 dB) for 19 hops (≈ 50 ms); drop at +3 dB after 5 s hang; track cap 1200 with lowest-SNR eviction ([SPEC-decode-core §2](https://github.com/HagaleTechnologies/manta/blob/main/docs/SPEC-decode-core.md)) [fact]. All SNRs in the 93.75 Hz channel ENBW nominal, not the 82.2 Hz measured one [fact] | Median of all bins; 1 s average; 6 dB / 3 dB per 35.2 Hz bin; 0.5 s birth; 10 s death; cap 200 (SP §6, §10) [fact] |
| Envelope / keying (legacy, default) | Magnitude of the channel sample with fixed reference scale; dual-EMA noise/signal rails, threshold at their geometric mean, hysteresis and debounce ([ARCHITECTURE §5](https://github.com/HagaleTechnologies/manta/blob/main/ARCHITECTURE.md)) [fact] | Asymmetric mark/space followers, 60%/40% hysteresis, squelch M ≥ 3S (SP §8) [fact]. Close cousins [inference] |
| Speed (legacy) | Online 2-means clustering of marks into dit/dah, WPM EMA; waits for 5 marks [fact] | 24-mark window, 2-dit boundary, 5–60 WPM (SP A.10) [fact] |
| Character decode (legacy) | Per-element likelihoods then **beam search width 4 over the Morse tree**, per-character confidence (SPEC-decode-core §4.3–4.5) [fact] | Hard lookup with per-symbol probability (SP §8) [fact] |
| Character decode (`hsmm`, opt-in) | **Explicit-duration (semi-Markov) segment decoder with token passing** ([SPEC-decode-core-v2 §1, §4](https://github.com/HagaleTechnologies/manta/blob/main/docs/SPEC-decode-core-v2.md), [hsmm/](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-decode/src/hsmm)): (1) evidence: a_s = 8 ms EMA of amplitude; mark level M = centered sliding max over ±4 dits (fixed delay); noise n from a track-local minimum-statistics floor (1.5 s window, +2.16 dB bias correction, measured); normalized amplitude û = (a−n)/(M−n); per-hop LLR = (û − 0.5)/σ_u², σ_u = 0.30, clipped to ±10 — i.e. a **Gaussian, not Rician/Rayleigh** likelihood; (2) segments Dit/Dah/EGap/CGap/WGap/Silence start and end only at "anchors" (edge triggers), evidence summed by prefix sums; (3) duration prior log-normal, ln-σ = 0.22, admissible 0.6–1.5 × nominal; type priors (Dit 0.6, Dah 0.4, EGap 0.62, CGap 0.28, WGap 0.10) plus a −1.5 mark-insertion penalty against noise "E"s; (4) each token carries its own dit length u (EMA α = 0.2, 8–60 WPM), seeded at 5 speeds (≈ 12–50 WPM); merge on (node, phase, round(2u)); beam 12; (5) commit on consensus prefix or forced after 25 dits; confidence = logistic(score margin / 6) × SNR factor [fact]. No language/callsign prior inside the decoder [fact] | Not yet built; this is DS option 3 ("explicit-duration HMM with beam search") almost exactly, minus Bell-style Kalman amplitude tracking and minus the Rician LLR of DS option 1 [inference] |
| Neural | None. "M4 ML fusion" (small CTC model fused by confidence weighting, from the author's earlier `dit` project) is planned, gated on beating classical under simulated fading; no model or weights exist [fact] | DS option 2 (VE3NEA-style CNN+LSTM-CTC) is the ranked primary challenger [fact] |
| Frequency estimate | Quadratic interpolation on dB powers of 3 channels, key-down hops only, power-weighted centroid (SPEC-decode-core §1.4) [fact] | Bin-level, ±1 bin tracking (SP §6) [fact] |
| Callsign extraction and spotting | `manta-spot`: CQ/DE/TEST/beacon context parse; grammar prefilter; cty.dat allocation check (rejects); master.scp only raises confidence, never gates; call must repeat as a distinct message within 90 s before first spot (beacons and an operator allowlist exempt); variant arbitration withholds a confusable weaker rival; dedupe on (call, frequency bucket), re-spot 10 min unless SNR improves or type changes ([wiki/pages/spot-validation.md](https://github.com/HagaleTechnologies/manta/blob/main/wiki/pages/spot-validation.md), [crates/manta-spot/src](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-spot/src)) [fact] | Callsign matching deferred (project memory) [fact] |
| SNR reported | Internal S−F in the channel, peak-held per 1 s, converted to 2500 Hz by −14.3 dB; telnet/RBN lines add +6.99 dB to express 500 Hz. Measured slope 0.974, bias ≤ +2.2 dB vs true SNR in 2500 Hz on synthetic AWGN ([man102](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-07-man102-snr-reference-and-estimator.md)) [fact] | Detector SNR per 35.2 Hz bin; benchmark S₅₀₀ key-down (SP §5) [fact] |

Stage-by-stage observations:

- manta's channel is much narrower than KZ4AP's (82 Hz ENBW vs a ±150 Hz
  channel), so its envelope starts about 10·log₁₀(252/82) ≈ 5 dB closer to
  matched-filter SNR, before any smoothing [inference; 252 Hz is SP's
  stated channel bandwidth]. The cost is fixed 94 Hz frequency quantization
  and the loss for a signal between channels (−6 dB at the edge in each
  channel; the decoder uses only one). KZ4AP's DS option 1 (re-center, then
  a dit-matched filter a few tens of Hz wide) would beat both [inference].
- manta's `hsmm` sums per-hop LLRs at 375 S/s from a channel whose noise
  correlation time is about 1/82 Hz ≈ 12 ms ≈ 4.6 hops, and does not
  decimate or rescale; this is exactly the overcounting caveat in DS option
  1, so its evidence scores are overconfident by a factor of about 4–5
  relative to the duration and type priors [inference]. Its fixed
  σ_u = 0.30 also ignores that the envelope spread depends on SNR.
- manta's recall research (2026-09-09, 20 "investigations") independently
  reaches KZ4AP's DS conclusions: keep complex samples and re-center before
  decoding, decode key state/durations/Morse jointly, treat fading as
  observation uncertainty, keep several speed hypotheses, add learned models
  only after measuring the classical gap
  ([decoder-recall-research.md](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-09-decoder-recall-research.md), §2–7, §20) [fact].

## 4. Performance evidence

**No CER-versus-SNR curve is published.** [fact] What exists:

- **Golden vectors** (synthetic, `manta-testkit`, AWGN and Watterson fading
  from `coppa`; SNR defined as signal over noise in **2500 Hz** by the
  generator, e.g. V1 = 20 WPM at +20 dB re 2500 Hz, equivalent to +27 dB re
  500 Hz): pass/fail criteria on exact text and "0 bogus callsigns", not a
  CER sweep. The README says the legacy engine fails some fading vectors
  (issues #25, #28); `hsmm` passes only 3 of 9 "real-conditions" VR tests
  and a minority of V tests, with three VR vectors decoding to nothing and
  V8w (a strong pileup) decoding 0/34 signals
  ([stage-2 gate](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-09-decode-core-v2-stage2-gate.md) §7–8) [fact].
- **Oracle on a real recording** (B2: 15 min, 192 kS/s IQ, 2025-11-29 CQ WW
  CW, 40 m near 7.080 MHz; 221 windows of 40 s around RBN spots by the
  skimmer K5TR). Metrics: `as_word` = the call appears as a whole decoded
  word; `framed` = it follows CQ, DE or TEST
  ([oracle.rs](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-testkit/src/oracle.rs) `score_text`) [fact].
  Results (stage-2 gate §1) [fact]:

  | engine | as_word | framed | substring |
  |---|---|---|---|
  | legacy | 26.7 % | 13.1 % | 48.0 % |
  | hsmm | 56.1 % | 31.7 % | 58.8 % |

  By SNR bucket (< 15, 15–25, ≥ 25 dB; n = 29, 99, 93), hsmm as_word =
  24 %, 58 %, 65 %. The bucket SNR is the RBN-reported value for the K5TR
  spot, i.e. CW Skimmer's estimate re **500 Hz** [inference from the RBN
  CSV column; the doc does not state the reference]. Since the reference
  skimmer copied every one of these calls, manta `hsmm` recovers about half
  of what CW Skimmer copied from the same band at the same time, though at a
  different receiver [inference].
- **Spot recall vs RBN** on B2: legacy 13.4 % (K5TR only) / 4.1 % (all
  RBN), hsmm 24.9 % / 8.7 %; precision lower bounds 10–30 %. The doc marks
  these recall figures as upper bounds (a scorer bug double-counted
  re-spots), and the recall research explains why a union of all RBN
  skimmers is not the set audible at this receiver [fact].
- **Live SDRplay RSP1B field test** (20 m, synchronized with RBN telnet,
  90 s): two signals RBN reported at up to 38 dB and 20 dB (re 500 Hz, per
  RBN convention) from 5–6 skimmers produced **zero** manta tracks;
  documented as a manta-side detection gap
  ([2026-09-10 decision](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-10-synchronized-rbn-capture-proves-detection-gap-is-manta-side.md))
  [fact]. A same-day follow-up (MAN-171) attributes a "dead zone" to the RF
  path rather than manta code [fact; not read in detail].
- **CPU:** legacy whole pipeline, 300 synthetic tracks, 15 s audio: 7.08 s
  CPU (user+sys) = 0.47 core on an Apple M4 Pro, about 1.6 ms of CPU per
  track-second including the PFB
  ([man18](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-02-man18-pi4-cpu-budget-gate.md))
  [fact; per-track figure is arithmetic]. `hsmm` decoder alone: 10.6 s for
  300 tracks × 10 s, about 3.5 ms per track-second, 4.25× over its own
  budget (stage-2 gate §6) [fact]. On the real B2 file `hsmm` used 3.9× the
  CPU time of legacy [fact]. **Not measured on a Raspberry Pi** (no
  hardware available to the author) [fact].
- **SNR calibration:** reported vs true SNR (2500 Hz), single AWGN signal:
  slope 0.974, bias +2.1 dB at 0 dB falling to +1.5 dB at 25 dB
  ([man102](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-07-man102-snr-reference-and-estimator.md)) [fact].
- **No head-to-head with CW Skimmer on controlled data** and no comparison
  with VE3NEA's published grid [fact].

## 5. Reusable or instructive for KZ4AP Skimmer

License: all of manta is MIT OR Apache-2.0, so any of it can be ported into
GPL-3.0 KZ4AP Skimmer with the MIT notice kept (section 1). Porting is Rust
to C++20, so "reuse" means reimplementing from the code and specs; a close
line-by-line port is still a derived work, so keep attribution [inference].

Most instructive, in order:

1. **The `hsmm` decoder as a worked reference for DS option 3**
   ([SPEC-decode-core-v2 §4](https://github.com/HagaleTechnologies/manta/blob/main/docs/SPEC-decode-core-v2.md),
   [hsmm/mod.rs](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-decode/src/hsmm/mod.rs),
   [token.rs](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-decode/src/hsmm/token.rs),
   [commit.rs](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-decode/src/hsmm/commit.rs)):
   anchor-based segmentation with prefix-summed evidence (cost scales with
   edges, not samples), per-token speed, seeding at several speeds, a merge
   key of (node, phase, quantized u), consensus-prefix commit, and a
   margin-based confidence. It is a concrete, tested design to start from,
   and its measured failures (collapse on fast clean signals, element/gap
   misclassification, fading) are a list of what to test first
   [inference]. KZ4AP should differ where DS already says so: Rician LLR
   decimated to the filter's correlation time, a pre-detection matched
   filter, and per-path amplitude tracking [inference].
2. **Measurement discipline:** the oracle (decode a window around each
   RBN-spotted station in a real IQ recording, score as_word/framed) is a
   cheap real-signal benchmark that isolates the decoder from the detector;
   KZ4AP has no equivalent yet [inference]. The recall research's critique
   of RBN scoring (no time matching, mismatched counting units, a union of
   receivers as truth) should be adopted before KZ4AP scores against RBN
   ([decoder-recall-research.md](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-09-decoder-recall-research.md),
   "What the approximately 30% number means") [inference].
3. **Noise reference by minimum statistics** with a measured bias
   correction (+2.16 dB for the 1.5 s minimum of 40 ms-smoothed chi-square
   (2 degrees of freedom) power) and a guard-banded spectral reference
   (SPEC v2 §2, [floor.rs](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-dsp/src/floor.rs)):
   directly applicable to a per-station noise estimate in KZ4AP [inference].
4. **Detector floor:** 25th percentile per channel instead of a median,
   bounded by a 32-channel block median + 3 dB, because a CW duty cycle of
   50–60 % inflates a median (SPEC-decode-core §2.1–2.2) [fact for their
   reasoning; inference that it matters for KZ4AP's median-of-all-bins
   floor, which is less exposed on a sparse band].
5. **SNR reporting:** peak-hold over each report interval rather than a
   mean or an instantaneous sample (measured slope 0.97), and explicit
   conversion between 2500 Hz internal and 500 Hz on the wire; relevant to
   KZ4AP's SNR calibration backlog item [inference].
6. **Spot validation rules** (`manta-spot`: context parse, cty.dat
   rejection, SCP as confidence only, repetition counted as distinct
   messages, variant arbitration, dedupe) plus bundled cty.dat, master.scp
   and DXCC table (check
   [SOURCES.md](https://github.com/HagaleTechnologies/manta/blob/main/crates/manta-spot/data/SOURCES.md)
   for the data files' own terms): a ready spec for KZ4AP's deferred
   callsign matching [inference]. The BEACON-exemption false spots (29
   garbage spots in one overnight run) are a cautionary example [fact].
7. **SoapySDR RSP1B notes:** AGC must be disabled before setting gain, and
   SoapySDRPlay3 rejects overall gain 46–48 dB on the RSP1B while the named
   IFGR/RFGR elements work
   ([first-live-rsp1b-run](https://github.com/HagaleTechnologies/manta/blob/main/docs/DECISIONS/2026-09-08-first-live-rsp1b-run.md))
   [fact]. Relevant only if KZ4AP uses SoapySDR instead of the native API.

Less useful: the PFB itself (KZ4AP's fast-convolution channelizer already
centers each station; fixed 94 Hz channels would give that up) and the
server, metrics and uplink code (out of scope for a live display)
[inference].

## 6. Fit with the owner's focus and a later merge/fork

**Single-band live display (current focus):** little direct overlap.
manta is headless by design, explicitly excludes a waterfall or panadapter,
and optimizes deterministic spot output [fact]. Its per-track events
(glyph, timestamp, confidence, alternatives, speed updates, periodic SNR
and frequency) are the same shape of stream a CW Skimmer-style display
needs, and its JSON Lines stream could feed a separate display, but it
would not replace KZ4AP Skimmer's engine for this purpose [inference].

| | manta has, KZ4AP lacks | KZ4AP has or plans, manta lacks |
|---|---|---|
| Display | — | Qt waterfall, live operator view (planned) |
| SDR | SoapySDR (RTL-SDR, Airspy, SDRplay via SoapySDRPlay3), OpenHPSDR, KiwiSDR, sound card | Native SDRplay API (planned) |
| Decoder | Tested beam search; a full HSMM with a real-signal oracle | Evidence-graded decoder survey; neural challenger plan (DS option 2) |
| Front end | Narrow (82 Hz ENBW) channels; minimum-statistics noise reference; interpolated frequency | Per-station centered channel; planned re-centering and matched filter (DS option 1) |
| Spotting | Callsign validation, telnet/RBN/JSON outputs, dedupe, metrics, Docker, cross-platform CI | Callsign matching deferred |
| Evidence | Golden vectors, determinism tests, CPU benches, a real contest IQ recording (not redistributable) | Physically defined SNR conventions and benchmark definitions (SP §5, A.11) |

**Merge or fork for RBN spotting (later):** manta already has what an RBN
node needs around the decoder (validation, cluster protocol, uplink,
dedupe, packaging), and its decoder is its stated weak point (`hsmm`
doubles legacy but fails its own gate; a live detection gap on the RSP1B)
[fact]. The natural combination is therefore **KZ4AP's decoder inside
manta's spotting shell**, provided KZ4AP's decoder measurably wins on
manta's own oracle and golden vectors [inference]. Obstacles: language
(Rust vs C++20: FFI or a port); channel format (375 S/s magnitude from a
94 Hz PFB channel vs 1500 S/s complex from a ±150 Hz channel); and license
direction: contributing upstream means releasing the decoder under
MIT/Apache, which the owner can do for his own code, while a GPL-3.0 fork
of manta is permitted but diverges from upstream [inference]. manta is an
11-week-old, one-developer, agent-driven project with no releases, so
upstream longevity is unproven [inference].
