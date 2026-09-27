# morseformer: evaluation for direct integration as a KZ4AP Skimmer decoder

Evaluated 2026-09-27 against commit
[`cbfd141`](https://github.com/sderhy/morseformer/tree/cbfd1416a762e3c4f781fef6ea85f1e12512ca16)
(v0.6.4 code, last code change 2026-06-03) and the Hugging Face model repo
[sderhy/morseformer](https://huggingface.co/sderhy/morseformer) (last modified 2026-05-22).
Links below use the prefix `R/` for
`https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/`.

Labels: **[fact]** read in the source or its documents; **[measured]** measured here
(throwaway venv under %TEMP%, CPU-only PyTorch 2.14.0 and ONNX Runtime 1.30.0, Intel
i7-12700H laptop, deleted afterward); **[inference]** my reasoning.

Symbols used throughout:

| Symbol | Meaning | Unit |
|---|---|---|
| f_s | morseformer's audio sample rate, 8000 | samples/s |
| r | our channel sample rate, 1500 (complex) | samples/s |
| f_c | morseformer's assumed tone (carrier) frequency, 600 | Hz |
| B_fe | morseformer front-end band-pass width, 200 (100 in the `contest` preset) | Hz |
| F | feature frame rate, 500 | frames/s |
| T_w, T_h | sliding-window length 6 and hop 2 | s |
| ρ | morseformer's stated SNR (definition in §5) | dB |
| S₅₀₀ | this project's SNR: key-on signal power over noise power in 500 Hz | dB |
| d | key-down duty fraction of a 6 s clip (fraction of samples with the key down) | 1 |
| MAC | one multiply-accumulate | — |

## Summary

- **License:** Apache-2.0 for code and weights [fact]. Compatible with GPL-3.0 for both
  porting code into the engine and redistributing the weights, with attribution and
  license-text obligations [inference; standard FSF position].
- **Weights:** published, 33.2 MB PyTorch checkpoint (4.13 M parameters with an EMA copy).
  Loads with `torch.load(weights_only=True)` [measured]. Exports cleanly to a 15.9 MB ONNX
  graph with standard operators only. ONNX Runtime matches PyTorch to 2.5×10⁻⁶ in
  probability [measured].
- **Not streaming.** A bidirectional Conformer runs over a fixed 6 s window, re-run every
  2 s, with up to 4 s commit latency [fact].
- **Cost:** about 4.2 GMAC per 6 s window, i.e. **about 2.1 GMAC per channel-second**. That is
  roughly 100–140× VE3NEA's option-2 estimate [inference, analytic]. Measured ONNX Runtime
  fp32, one thread: 97–106 ms per window at best and about 280 ms sustained on a P-core;
  about 800 ms on an E-core or throttled core [measured]. That works out to **7–20 channels
  per P-core**, so hundreds of channels is out of reach, and a Raspberry Pi 5 would manage
  only about 10 [inference].
- **Input mapping:** its front end can be computed directly from our complex stream (a 100 Hz
  zero-phase low-pass, magnitude, log, a 3-sample mean and per-window z-score). It matches
  the 8 kHz audio path with correlation 0.994–0.999 and gives identical decodes on a
  synthetic test [measured].
- **Biggest content risk:** training speed is **16–28 WPM** only [fact], so contest speeds
  above about 30 WPM are out of distribution.
- **Recommendation:** add it early as an offline **reference decoder in the evaluation
  harness**. Later, possibly add it as an opt-in, channel-capped "second-opinion" decoder
  (arbitration, survey option 4). Do not make it a general per-station decoder, and do not
  let it displace option 2.

## 1. Provenance, license, activity

- **Author:** GitHub user `sderhy`, the only contributor (98 of 98 commits) [fact]. The README
  acknowledgements name "Sébastien Derhy", while the model card's BibTeX says "Derhy, Serge"
  ([R/README.md](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/README.md) "Acknowledgements"; [R/MODEL_CARD.md](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/MODEL_CARD.md) "Citation") [fact]. The given name is inconsistent between the two, so cite the handle.
- **License:** SPDX **`Apache-2.0`**. Sources: GitHub license detection; the full Apache 2.0
  text in `LICENSE`; `pyproject.toml` `license = { text = "Apache-2.0" }`; the Hugging Face
  card `license: apache-2.0`. The README says "The released model weights are distributed
  under the same license." There is **no NOTICE file** [fact].
  - (a) **Porting or linking code into the GPL-3.0 engine:** allowed. Apache-2.0 is one-way
    compatible with GPL-3.0, so the combined work is GPL-3.0. Obligations under Apache §4:
    keep the copyright and attribution, include the Apache license text, and mark modified
    files as changed. There are no NOTICE contents to carry [inference from license text].
  - (b) **Redistributing the weights** (as a .pt file or a converted .onnx) with the
    GPL-3.0 program: allowed under Apache-2.0 with the same attribution. One caveat: the
    weights cannot be fully regenerated from published material, because the real-audio
    fine-tune corpus is not public (§5). That affects reproducibility, not license
    compatibility [inference].
- **Dates and activity:** repo created 2026-04-17. Last model or code change 2026-06-03
  (contest preset, GUI). Last push 2026-09-19 (a project web page). The Hugging Face repo
  was last modified 2026-05-22. **1 star, 1 fork**, no GitHub Releases. Versions are
  tracked in `CHANGELOG.md`, PyPI (`pip install morseformer`) and Hugging Face. Status is
  "Development Status :: 3 - Alpha" [fact]. It is a single-maintainer research project,
  with bursts of activity through June and dormant since [inference].

## 2. Architecture

### 2.1 Front end ([R/morseformer/features/frontend.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/features/frontend.py))

This is deterministic DSP with a single feature channel. It is not a spectrogram and not a
filterbank [fact]:

1. Real audio at f_s = 8000 samples/s. The live and audit paths hard-code 8000, and f_s must
   be a multiple of F.
2. A 4th-order Butterworth band-pass, f_c ± B_fe/2 = 600 ± 100 Hz, applied **zero-phase**
   with `sosfiltfilt` over the whole window (L33–43). That makes it non-causal.
3. The Hilbert envelope `abs(hilbert(x))` over the whole window (L47).
4. `ln(env + 1e-6)` (L95).
5. Box-mean decimation by f_s/F = 16 samples, giving F = 500 frames/s, i.e. 2 ms frames.
6. **Per-window** zero-mean, unit-variance normalization (`_normalise`, L65). This makes it
   invariant to input scale.

The output shape is `[T, 1]`: a 6 s window gives T = 3000 frames. The checkpoint stores the
same front-end config: `tone_freq 600, bandwidth 200, frame_rate 500, log_floor 1e-6`
[measured, read from checkpoint]. The model card's "600 Hz ± 250 Hz" is wrong for the
shipped path; 250 Hz is the half-width of the 500 Hz receiver filter used in training
synthesis [fact]. The `contest` preset narrows B_fe to 100 Hz at inference only; the model
was trained at 200 Hz ([R/morseformer/cli/presets.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/cli/presets.py) L98–111) [fact].

### 2.2 Encoder, heads, parameters ([R/morseformer/models/conformer.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/models/conformer.py), [acoustic.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/models/acoustic.py), [rnnt.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/models/rnnt.py))

- **Subsampling:** two Conv1d layers (kernel 3, stride 2), 1→72→144 channels with SiLU. This
  is 4× in time, so encoder frames run at 125 frames/s (8 ms each). A 6 s window gives
  T′ = 750 frames (L282) [fact].
- **Conformer:** 8 blocks, d_model = 144, 4 heads (head dimension 36), macaron feed-forward
  networks with expansion 4, pre-norm, and RoPE (rotary position embedding) self-attention
  through `F.scaled_dot_product_attention` **with no causal or chunk mask** (L148). The
  conv module is LayerNorm, pointwise conv → GLU, then depthwise conv with kernel 31 and
  **symmetric padding** (L213; ±15 frames, i.e. ±120 ms of look-ahead per layer), then
  LayerNorm, SiLU and a pointwise conv. BatchNorm is not used [fact].
- **CTC head:** Linear(144→49) followed by log-softmax [fact].
- **RNN-T:** the prediction network is Embedding(49,128) plus a 1-layer LSTM(128). The joint
  network is Linear(144→256) + Linear(128→256), tanh, then Linear(256→49). Blank has
  index 0 [fact].
- **Parameters** [measured from `rnnt_phase11b.pt`]: total **4,128,802**. The encoder plus
  CTC head is 3,908,209, the prediction network 138,368 and the joint network 82,225.
- **Training loss:** 0.3·CTC + 0.7·RNN-T ([R/morseformer/train/rnnt_loop.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/train/rnnt_loop.py) L64–65; also in the checkpoint config) [fact]. **The shipped decode path is
  greedy RNN-T** (`greedy_rnnt_decode_aligned`, rnnt.py L289), not CTC. Beam search
  (L551), an ITU callsign-shape prior ([R/morseformer/decoding/callsign_prior.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/decoding/callsign_prior.py)) and LM fusion are "experimental" and off in every preset [fact].
- **Inference gates:** an emitted non-blank token must have joint softmax probability ≥ 0.6
  (`confidence_threshold`). Digits 0–9 need ≥ 0.90 (`digit_threshold`). These values are in
  the `live` and `prose` presets. `contest` uses 0.5/0.8 and `conservative` 0.75/0.95
  (presets.py L71–122) [fact].

### 2.3 Output alphabet ([R/morseformer/core/tokenizer.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/core/tokenizer.py) L35)

There are 49 tokens: blank, **space (word boundary, index 1)**, A–Z, 0–9, `. , ? ! / = + -`,
and `É À '` [fact]. **There are no prosign tokens** ("Prosigns are not given dedicated
tokens"):

- `<BT>` is emitted as `=` and `<AR>` as `+`.
- `<SK>`, `<KN>`, `<BK>` and run-on `UR` are *labels of two letters*. The synthesizer renders
  them with the inter-character gap collapsed, with probabilities 0.5, 0.3, 0.3 and 0.25
  (`operator_run_on_pairs`) [fact].

So the model outputs `SK` for both `<SK>` and a clean `S K` with a character gap. The
engine can only tell them apart from timing or context [inference].

### 2.4 Language models ([R/morseformer/decoding/](https://github.com/sderhy/morseformer/tree/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/decoding))

- **Character 3-gram** (`lm_ngram.py`): stupid backoff, pickled count dictionary, 482 KB,
  trained on 100k synthetic Phase 9 samples. It is **not integrated into decoding**. It only
  rescores candidate splits in a dictionary-based **word splitter** (`word_splitter.py`).
  That splitter is a post-processing step on the final text: regex fixes (detach `DE`,
  isolate `K`/`KN`/`SK`, normalize `=`/`+`), then dynamic-programming segmentation of
  run-together tokens against an amateur-radio and English word list. It runs only in the
  offline `prose` preset [fact]. It is fully separable [fact].
- **GPT character model** (4.76 M parameters, `lm_phase5_2`): shallow fusion into RNN-T
  greedy decoding. Offline only, and dropped from all presets since v0.6.3 "because it hurt
  amateur jargon". Streaming fusion is described as broken ([R/MODEL_CARD.md](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/MODEL_CARD.md) "Limitations" 6; presets.py docstring) [fact].
- For a skimmer, neither is needed: the word splitter is tuned for ragchew prose, and the
  project's callsign posterior would replace both [inference].

## 3. Streaming vs whole-utterance ([R/morseformer/decoding/streaming.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/decoding/streaming.py))

- **Not incrementally streaming.** Attention is full over the window, the conv kernels are
  non-causal, the front-end filter is zero-phase, and normalization is per window. None of
  this can be carried as state from chunk to chunk [fact from code].
- **What it does:** a sliding window. T_w = 6.0 s must equal the training clip length ("the
  model is not robust to other lengths", L54–55). It re-decodes every T_h = 2.0 s (L76–77).
  Each window commits only tokens whose emission time falls in its **central** T_h-wide zone
  [t_start + 2 s, t_start + 4 s). The first window also commits its left part and the final
  one its right part. Zones tile the stream without overlap [fact].
- **State carried** between windows: the audio ring buffer and a commit high-water mark.
  Nothing neural is carried [fact].
- **Latency:** worst case T_w/2 + T_h/2 = **4 s** from the audio to committed text (L25). The
  model card reports "about 4 s end-to-end" live [fact]. Look-ahead is effectively 2–4 s.
- **Compute redundancy:** each second of audio passes through the encoder T_w/T_h = 3 times
  [fact/arithmetic].
- A true streaming variant (chunked attention masks with cached keys and values,
  causal-padded convs, running normalization) would need **retraining**. The published
  weights would not transfer as-is [inference].

## 4. Weights and ONNX

- **Published:** Hugging Face [sderhy/morseformer](https://huggingface.co/sderhy/morseformer)
  [fact]. The recommended file is `rnnt_phase11b.pt`, **33,178,294 bytes**. It holds the
  model state_dict plus an EMA state_dict (the loader applies the EMA), the training config
  and metrics. Earlier acoustics (`rnnt_phase3_0` … `rnnt_phase5_8`) are about 33 MB each,
  the LMs are 38 MB (`lm_phase*.pt`), and the 3-gram is 494 KB. The registry names an
  `rnnt_phase5_10` that is not on the Hub [fact].
- **Format:** a PyTorch pickle, which is unsafe to load in general. **It loads with
  `weights_only=True`** [measured], so it can be converted once, offline and safely.
- **ONNX export** [measured]:
  - Encoder plus CTC head, via the TorchScript exporter at opset 17: **15.9 MB fp32**. It
    has dynamic batch and time axes. Operators: Conv 26, MatMul 65, LayerNormalization 48,
    Softmax 8, Sigmoid 34, LogSoftmax 1, plus shape plumbing (Transpose, Slice, Concat,
    Gather, Reshape, Split). There are **no custom ops and no LSTM** in the encoder.
  - ONNX Runtime vs PyTorch on a real window: max |Δp| = 2.5×10⁻⁶ and 100% frame-argmax
    agreement.
  - Blocker check: the RoPE cos/sin table is traced as a constant sized for 750 encoder
    frames. That is fine for the fixed 6 s window, but a longer window would need
    re-export [inference from code; export warning seen].
  - Prediction plus joint step: a separate 0.88 MB ONNX graph with one standard `LSTM` op,
    3 MatMul and Tanh. It is small enough to hand-code in C++ instead [measured/inference].
  - Dynamic int8 quantization (`quantize_dynamic`, weights QInt8): 4.4 MB, 99.2% frame
    argmax agreement, **but slower** in the single run tried (1.59 s vs 0.76 s per window
    in the same slow-core mode) [measured, one run]. Static quantization is untested.

## 5. Training data

**Synthetic generator** ([R/morseformer/data/synthetic.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morseformer/data/synthetic.py) `phase_9` L290–343, `_generate_one` L1462; [R/morse_synth/](https://github.com/sderhy/morseformer/tree/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morse_synth)). The values below are for the shipped checkpoint's config as stored in the `.pt` [measured, read from checkpoint], which match the code [fact]:

| Item | Value |
|---|---|
| Clip | 6.0 s at 8000 samples/s, tone f_c = 600 Hz plus a per-clip offset U(−50, +50) Hz |
| Speed | **U(16, 28) WPM**, constant within a clip (no speed change) |
| Timing ("fist") | PARIS unit; per-element additive Gaussian jitter σ_e ~ U(0, 0.30) dit units and gap jitter σ_g ~ U(0, 0.50) dit units, clipped at 0.1 ([R/morse_synth/operator.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morse_synth/operator.py) L108); dash:dot ratio U(2.5, 4.5); inter-element gap inflation U(0.8, 1.6); word-gap inflation U(3, 8)× the 7-dit space; run-on pairs UR/SK/KN/BK |
| Keying | raised-cosine edges, 5 ms rise; no chirp |
| Fading ("QSB") | **deterministic sinusoidal envelope**, rate U(0.05, 1) Hz, depth U(0, 15) dB. There is no Rayleigh or Rician fading and no Doppler spread |
| Drift | random-walk frequency, σ U(0, 1) Hz/√s |
| QRN | Poisson impulses, 0–1 /s, 1 ms decay |
| QRM | probability 0.25: a second CW signal at f_c + U(−300, 300) Hz, U(−18, −8) dB relative power. About ⅓ of these fall inside the ±100 Hz front end [inference] |
| Receiver filter | 6th-order Butterworth band-pass, 500 Hz, zero-phase |
| Empty clips | 20% noise-only (AWGN, AWGN+QRN, distant weak CW) with empty labels |
| Text | callsigns 10%, Q-codes 10%, QSO templates 30%, numerics 12%, words 4%, random 8%, prose 6%, French prose 16%, dense contest 4% |
| SNR | ρ ~ U(0, 30) dB |

**Real data** [fact]:
- Fine-tuning mixed 20% real-audio chunks from `data/real/g3ses_force_aligned.jsonl`
  (checkpoint config).
- These are hand-keyed ragchews by operator set "g3ses", from a directory outside the repo
  (`../testlive`), segmented and force-aligned with the Phase 5.5 CTC head
  ([R/scripts/prepare_real_qso.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/scripts/prepare_real_qso.py), [force_align_real_qso.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/scripts/force_align_real_qso.py)).
- Earlier phases used 42 chunks from one hand-keyed session, plus 9482 ebook2cw chunks of
  *Alice in Wonderland* (machine-keyed).
- **None of the real audio is published**; the repo ships only small LCWO bench clips under
  `data/bench/` [fact].

**SNR definition** ([R/morse_synth/channel.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/morse_synth/channel.py) L17–24, L70–90) [fact]:

- ρ = 10·log₁₀(S_m/σ²), where σ² is the variance of real white Gaussian noise added at
  f_s = 8000 samples/s. Its power spreads over 0–4000 Hz.
- S_m is the mean of x² over the samples whose |x| exceeds the 70th percentile of |x|,
  where x is the **raw real waveform** (not its envelope). That waveform is measured after
  QSB, with any QRM mixed in.
- If that percentile is 0, S_m falls back to the full-clip mean power.
- Noise is added before the 500 Hz receiver filter, which leaves in-band signal and noise
  unchanged [inference].

**Conversion to S₅₀₀ (key-on, 500 Hz):** noise in 500 Hz is σ²·500/4000, so
S₅₀₀ = ρ + 9.03 dB − δ(d). Here δ(d) = 10·log₁₀(S_m/P_on) and P_on = A²/2 is key-on
power. I measured δ(d) numerically for a steady tone and a duty fraction d [measured]:

| d | 0.15 | 0.20 | 0.25 | 0.29 | 0.30 | 0.35 | 0.45 | 0.60 |
|---|---|---|---|---|---|---|---|---|
| δ(d), dB | −8.2 | −7.0 | −6.0 | −5.4 | 0.0 | +0.7 | +1.5 | +2.1 |

The definition is **discontinuous at d = 0.30**. Above it, the percentile selects the peaks
of the sine, so S_m ≥ P_on. Below it, S_m is the average power. So:

- **S₅₀₀ ≈ ρ + 7 to +9 dB when d ≥ 0.3**
- **S₅₀₀ ≈ ρ + 14.4 to +17 dB when d < 0.3**

A 6 s clip of Morse text (key-down ≈ 45% while sending) that fills 60–90% of the window
has d ≈ 0.27–0.40, which straddles the step. So any single reported ρ converts to S₅₀₀
only within a band of about 7–17 dB above ρ, unless the per-clip d is known [inference].
QSB depth and in-band QRM shift S_m further.

## 6. Reported results

All results below are the author's [fact] unless tagged. The SNR conversions are
[inference] and use the §5 rule. The noise reference is white noise over 0–4000 Hz, and the
signal is S_m, not key-on power.

**Synthetic, from the model card's "historical" tables. These are not the shipped
checkpoint:**
- *Realistic-channel ladder* for v0.3 (Phase 3.3): 1200 clips at 16/20/22/25/28 WPM with
  QSB, QRN, drift and the receiver filter. CER is 0.0% at ρ ≥ +5 dB, 0.3% at 0 dB, 5.0% at
  −5 dB and 39.7% at −10 dB.
  - With d ≥ 0.3, −5 dB and −10 dB correspond to S₅₀₀ ≈ +2 to +4 dB and −3 to −1 dB.
  - With d < 0.3 they correspond to about +9 to +12 dB and +4 to +7 dB.
  - The per-clip duty was not reported, so the result **cannot be converted more tightly**.
- *AWGN-only ladder* for v0.3: 0.99% at 0 dB, 29.4% at −5 dB and 88% at −10 dB. That is
  worse than the "realistic" ladder at the same ρ, which is unexplained. A plausible cause
  is the absence of the receiver filter, or a different d in that set [inference].
- *French accent bench* for v0.4: 0% down to +5 dB, 5.0% at 0 dB and 33.8% at −5 dB.
- No SNR ladder was published for v0.5+ or for the shipped `rnnt_phase11b`. The author
  notes the synthetic validation "saturates at ~0% CER" because of a config-propagation bug
  ([R/MODEL_CARD.md](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/MODEL_CARD.md) v0.5.1 caveats).

**The 17.75% real result** ([R/CHANGELOG.md](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/CHANGELOG.md) "Release v0.6.4"; [R/scripts/audit_real_qso.py](https://github.com/sderhy/morseformer/blob/cbfd1416a762e3c4f781fef6ea85f1e12512ca16/scripts/audit_real_qso.py)):
- Data: 26 hand-keyed ragchew clips, 31 min in total, from operator directories g3ses and
  g6pz, with hand-written `.txt` transcripts. The audio is resampled to 8 kHz and assumed
  to have a 600 Hz carrier.
- Decode: the `prose` preset, i.e. sliding window, thresholds 0.6/0.9, plus the word
  splitter and 3-gram.
- Metric: CER is Levenshtein distance over uppercase characters **including spaces**,
  divided by reference length. "ALL" is the **unweighted mean of per-clip CERs** (L227),
  not a character-weighted total.
- Results: ALL 17.75% CER / 44.31% WER; g3ses 8.45%; g6pz (held out) 28.60%.
- **g3ses is not independent.** The training real-audio JSONL is built from the same
  `../testlive/g3ses` recordings by `prepare_real_qso.py` and `force_align_real_qso.py`. So
  8.45% is largely in-training-set; the author also calls it "speaker-specific overfitting"
  [inference from code paths; author's caveat is fact].
- **The held-out figure, 28.6% CER, is the honest real-audio number** [inference].
- The corpus is unpublished and its SNR, receiver and band are not stated, so it **cannot be
  converted to S₅₀₀**.
- Two g6pz clips with carriers at +138/+190 Hz were excluded from training prep. Whether
  they are in the 26-clip audit is not stated.

**False positives:** the v0.6.4 gate records `silence_fp` = 0.97 characters per 6 s of pure
AWGN, versus 0.10 for the previous model. The author says the 0.6 confidence gate "masks"
this in production [fact]. Whether 0.97 was measured with or without the gate is not
stated [gap].

## 7. CPU cost

**Analytic** (encoder, per 6 s window, T′ = 750) [inference, arithmetic]:

- Each block, per frame:
  - feed-forward: 2 × 2 × 144 × 576 = 331,776 MAC
  - QKV projection: 62,208; output projection: 20,736
  - attention, QKᵀ + AV: 2 × 750 × 144 = 216,000
  - conv module: 41,472 + 4,464 + 20,736
  - total ≈ 0.70 M MAC
- × 8 blocks × 750 frames ≈ **4.18 G MAC**, + 0.02 G for subsampling ≈ **4.2 GMAC per
  window**.
- At one window per 2 s hop, that is **≈ 2.1 GMAC per channel-second**.
- The RNN-T joint and prediction networks add about 0.1 G per window (the 144→256
  projection over all frames, plus about 82k MAC per joint evaluation). That is negligible
  in C++.
- VE3NEA's option 2 is 15–20 M MAC per channel-second, so morseformer is **about 100–140×
  more**.

**Measured, single thread, fp32, one 6 s window (`[B, 3000, 1]`)** [measured]. The i7-12700H is
a hybrid laptop CPU, and timings were strongly bimodal by core type and power state. The
PyTorch profiler showed matmul 33%, attention 31% and convolution 20% of time.

| Runtime / placement | ms per window | CPU-s per channel-s (2 s hop) | channels per core |
|---|---|---|---|
| ONNX Runtime, P-core, best | 97–106 | 0.05 | ~20 |
| ONNX Runtime, pinned P-core, median | ~280 | 0.14 | ~7 |
| ONNX Runtime, pinned E-core / throttled, median | 750–820 | 0.38–0.41 | ~2.5 |
| PyTorch eager (unpinned) | ~1000 | 0.5 | ~2 |
| Python greedy RNN-T loop (extra, over encoder) | ~300 | — | Python overhead only |
| Front end: scipy at 8 kHz / our complex version at 1500 S/s | 12 / 7 | <0.01 | — |

- Batching (B = 8, 32) did **not** reduce per-window time: the model is compute-bound
  [measured].
- The best case (4.2 GMAC in 0.1 s ≈ 40 GMAC/s) is near this core's measured sgemm rate of
  56 GMAC/s [measured], so there is little headroom from a better runtime [inference].
- Shorter inputs: 3 s took 40–310 ms and 1.5 s took 19–145 ms (same bimodality). But the
  model is only trained at 6 s.

**Implications** [inference]:
- **300 channels ≈ 15–42 desktop P-cores** at fp32. That is infeasible alongside the rest of
  the engine.
- **Raspberry Pi 5** (4× Cortex-A76 at 2.4 GHz; fp32 NEON peak about 19 GMAC/s per core,
  realistic 5–10): **2–5 channels per core, about 8–20 per Pi at full load.**
- Levers:
  - hop 3 s: ×0.67 cost, latency 4.5 s
  - static int8: maybe ×2, untested
  - skipping the decode for windows with no detected keying
  - running it only on a few stations chosen by the cheap decoder (option 4 arbitration)
  - distilling or retraining a smaller or streaming student
- None of these closes a 100× gap for all stations.

## 8. Integration plan

**Input mapping (recommended: compute the features directly from the complex stream).**
The front end is an envelope detector, and our channel is already the analytic signal
around the station. So band-pass at 600 ± 100 Hz followed by a Hilbert envelope is
equivalent to a **low-pass at ±100 Hz on the complex baseband followed by |·|**
[inference]. Per channel, per 6 s window of 9000 complex samples at r = 1500:

1. Zero-phase 4th-order Butterworth low-pass with cutoff B_fe/2 = 100 Hz (50 Hz for the
   `contest` setting), run forward-backward over the window buffer to match `sosfiltfilt`.
   Our ±150 Hz channel covers the ±100 Hz pass-band, so there is **no bandwidth shortfall**.
2. Envelope |y|, then ln(|y| + ε). Scale samples so the noise floor is ≫ ε = 10⁻⁶. The
   later z-score removes the absolute scale.
3. Box-mean of 3 samples, since r/F = 1500/500 = 3 exactly. That spans the same 2 ms as 16
   samples at 8 kHz, giving 3000 frames at 500 frames/s.
4. Per-window mean and standard-deviation normalization.

- **Verified** [measured]: synthetic CW ("CQ TEST DE KZ4AP…", 24 WPM, 500 Hz receiver
  filter) was mixed down and resampled by 3/16 to 1500 complex samples/s. The features
  matched the 8 kHz reference with correlation 0.9988 / 0.9976 / 0.9962 / 0.9935 at
  ρ = +20 / +5 / 0 / −5 dB. RMS difference was 0.05–0.11 on unit-variance features.
  **RNN-T and CTC text were identical** at every SNR.
- This is one message and one seed, so re-check on the project benchmark [inference].
- The 600 Hz "tone" never has to be synthesized. Mixing to an audio tone and resampling to
  8 kHz would also work, but it costs more and gains nothing [inference].
- Carrier offset: the model was trained with ±50 Hz offset and 0–1 Hz/√s drift. Our
  ±11.7 Hz bin rounding is well inside that, so re-centering is optional for this decoder
  [inference].

**Runtime:**
- (a) **Encoder plus CTC head via ONNX Runtime in C++**, using the 15.9 MB graph exported
  above. Ship the converted .onnx, never the pickle. Run windows as channels come due, and
  stagger each channel's window phase so load is spread evenly over the 2 s hop.
- (b) **Front end in C++**: a biquad cascade run forward and backward over 9000 samples,
  plus log, mean and z-score. About 100 lines, under 1 ms per window.
- (c) **Decoding in C++, no external library**:
  - *RNN-T greedy* (the shipped and benchmarked path): a hand-coded LSTM(128) step and
    joint (≈ 0.22 M parameters), with up to 5 emissions per frame and the 0.6/0.9 gates.
    About 200 lines. Alternatively, use the 0.88 MB pred+joint ONNX graph.
  - *CTC* is simpler: argmax and collapse, or a prefix beam search that could take the
    project's callsign prior later. In my test it gave the same text as RNN-T, but the
    author's numbers are all for RNN-T, so the CTC path needs its own evaluation.
  - The 3-gram word splitter is not worth porting for a skimmer.

**Output → `DecodedSymbol`:**
- *Text:*
  - A–Z, 0–9, `. , ? / -`: pass through.
  - `=` → `<BT>`, `+` → `<AR>`.
  - `SK`/`KN`/`BK` arrive as letter pairs. Optionally fuse them to `<SK>`/`<KN>` when the two
    tokens are close in time; this needs a rule that has not been validated.
  - `!` (`-.-.--`) passes through.
  - `É` (`..-..`) and `À` (`.--.-`) map through their Morse patterns to whatever the
    engine uses for non-English codes (e.g. `*` or a pattern token).
  - `'` passes through.
  - Space token → `" "` (word space). The model emits spaces explicitly [fact], so word
    spaces come from the network, not from engine timing.
- *Probability:* for RNN-T, the joint softmax probability of the emitted token (the value
  already compared with the threshold). For CTC, the peak posterior over the token's spike
  frames. Calibration is unknown; fit a mapping on the benchmark [inference].
- *Times:* each RNN-T emission carries its encoder frame index. Time is
  t = t_window_start + k·8 ms (streaming.py uses exactly this for commits).
  - In the test, emissions (and CTC spikes, 3–4 frames wide) landed near the **start** of
    each character. Q in "CQ" at 24 WPM appeared at 0.68 s, where Q starts at about 0.70 s
    [measured, one example].
  - So start_s = emission time, and end_s ≈ next emission time minus one inter-character
    gap. Alternatively, run a CTC forced alignment (Viterbi) per window for boundaries, as
    the author did with `torchaudio.functional.forced_align`. Resolution is 8 ms
    [inference].
- *`process`/`flush`/`reset`:*
  - `process` appends to a 6 s ring buffer and decodes each full window per 2 s of new
    input. It returns only the tokens that fall in that window's commit zone.
  - `flush` decodes the remaining partial window and commits to the end (the repo does the
    same; it has a shorter-than-trained input).
  - `reset` clears the buffer and the high-water mark.
  - `DecodeUpdate.wpm` has no source; estimate it from token spacing or leave it at 0.

**Effort estimate** [inference]:
- 1–2 developer-weeks: ONNX export script, C++ front end, RNN-T greedy, windowing, symbol
  mapping and unit tests against Python reference outputs.
- Another 1–2 weeks: scoring it on benchmark categories A–I.
- Retraining for 10–45 WPM, Rayleigh fading or native 1500 S/s input: weeks of work plus a
  GPU. Training code and generator are available, so this is feasible, but it starts from an
  unpublished real-audio set.

**Risks, most serious first:**
1. **CPU:** ≈ 2.1 GMAC per channel-second, ~100× option 2 [inference; measured runtime].
2. **Speed range 16–28 WPM** [fact]: contest CW at 30–40 WPM is out of distribution and
   untested.
3. **4 s latency and a fixed 6 s window** [fact].
4. **Weak real-audio evidence:** 28.6% CER on one held-out operator, an unpublished corpus,
   no shipped-model SNR curve, and an SNR definition that is discontinuous in duty cycle
   [fact/inference].
5. **False positives on noise** (~1 character per 6 s in the gate test) multiplied across
   many channels [fact/inference].
6. **Prosign ambiguity:** SK/KN as letter pairs [fact].
7. **Single maintainer, dormant since June:** the author reports RNN-T-head divergence
   during training, and the shipped file is a mid-run checkpoint [fact].

## 9. Comparison with survey option 2 (VE3NEA CNN+LSTM, ~375k parameters)

| | morseformer (v0.6.4) | VE3NEA DeepCW (option 2) |
|---|---|---|
| Parameters | 4.13 M [measured] | ~0.375 M [fact, survey] |
| Cost | ≈ 2.1 GMAC per channel-second; 7–20 channels per P-core measured-derived | 15–20 M MAC per channel-second estimated; hundreds per core [survey inference] |
| Streaming | no; 6 s window, 2 s hop, ≤ 4 s latency | yes; stateful LSTM, batch across channels |
| Input fit | exact from complex stream (envelope), verified | 128-point FFT at r = 1500 matches his bins exactly [survey] |
| Speeds trained | 16–28 WPM | 8–50 WPM |
| Fading model | sinusoidal QSB ≤ 15 dB, ≤ 1 Hz | Rayleigh, f_D = 0.1–3 Hz |
| Controlled evidence | no shipped-model SNR curve; old ladders only partly convertible | head-to-head vs CW Skimmer, ~30k characters per point, S₅₀₀-convertible |
| Real-audio evidence | 17.75% mean CER (28.6% held out) on 31 min, unpublished | none |
| Weights | published, Apache-2.0, ONNX-exportable (verified) | not recorded in survey |
| Word spaces / prosigns | explicit space token; `=`/`+` only | 43 classes [survey] |

**Verdict** [inference]:
- morseformer brings two things option 2 lacks: **ready weights** and **some real-audio
  evidence**.
- It loses decisively on cost, streaming, speed coverage and controlled evidence, and those
  are the constraints the survey ranked highest.
- It should **not** replace or precede option 2.
- It **is** worth adding as a separate, clearly experimental decoder option, in two steps:
  1. **Now (cheap):** wrap the ONNX model in the Python evaluation harness as a reference
     decoder for benchmark categories A–I, especially H (real recordings). Weights are
     already in hand, so this gives a neural baseline before any training.
  2. **After option 2 and the benchmark exist:** consider the C++ integration in §8 as an
     opt-in, channel-capped decoder, e.g. ≤ 10 stations on a desktop and none on a Pi. It
     would be used for arbitration or second opinions on low-confidence stations (survey
     option 4). Do this only if the benchmark shows it beats option 2 on the stations where
     it would run.
