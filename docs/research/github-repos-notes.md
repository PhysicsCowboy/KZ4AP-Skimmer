# Notes on three GitHub repositories: nn-morse, cwlab, morse-dataset

Checked 2026-09-27 through the GitHub API (`gh api`) and raw file reads. Nothing was cloned, installed or run except two read-only helpers: a WAV-header reader and a numpy-free unpickler for `Codebook.npy`. Their scratch files lived in `%TEMP%` and were deleted afterward.

**Labels** follow the survey's convention. **[fact]** means read directly in the repository's code, README, issues or git metadata, or in the author's own blog. **[secondhand]** means a third party's report. **[inference]** means reasoning in these notes. Where these notes compute a number from code, such as a parameter count or an SNR range, the label is [inference: arithmetic from code]. The arithmetic is shown so it can be checked.

**Symbols.** These are the same as in [decoder-survey.md](decoder-survey.md), plus the generator variables used below.

| Symbol | Meaning | Units |
|---|---|---|
| S₅₀₀ | key-on (key-down) carrier power over noise power in 500 Hz; the project convention | dB |
| N₀ | one-sided noise power spectral density | (full-scale units)²/Hz |
| f_s | generator or decoder sample rate | samples/s |
| n | the integer `noise_power` argument of the nn-morse/cwlab generator, drawn uniformly from 0–199 | dimensionless |
| σ² | variance of the additive Gaussian noise that n sets | (full-scale units)² |
| a | the `amplitude` argument divided by 100, drawn uniformly from 0.10–1.49 | dimensionless |
| T | dit duration, 1.2/WPM | s |
| MAC | one multiply-accumulate | — |

Permalinks below point at each repository's HEAD commit as of this check.

---

## 1. pd0wm/nn-morse

**Summary.** nn-morse is a compact, fully readable CTC decoder (Dense×4 → LSTM(256) → softmax, about 740,000 parameters) with a synthetic audio generator and one shipped weight file. It is MIT licensed. Its training noise never goes below about S₅₀₀ = +1 dB, and it has no fading, no QRM and no evaluation. It is the ancestor of bg4xsd/cwlab.

### Identity and status

- **Author:** Willem Melching, callsign PD0WM (GitHub `pd0wm`; "I CAN Hack", the Netherlands; blog icanhack.nl) [fact: GitHub profile].
- **License:** MIT (SPDX `MIT`), "Copyright (c) 2020" in [LICENSE](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/LICENSE) [fact]. MIT is GPL-3.0-compatible: MIT code and weights can be incorporated into a GPL-3.0 work if the copyright and permission notice are kept [inference; the standard FSF compatibility position].
- **Dates:** created 2020-05-10. The last substantive commit, "add model file for epoch 1750," landed 2020-05-10 (f6a504a). Later commits are Dependabot dependency bumps; the last merged one is from 2022-03-28 (e1f7bdd). The last push was 2023-02-16 [fact: API].
- **Maintenance:** dormant since May 2020. Two issues are open: #2, "Future work?" (2022), which went unanswered, and #13, on Cyrillic support (2022). Neither has a maintainer reply [fact]. Issue #2 mentions a blog post by the author about the project. It was not located on icanhack.nl in this check [gap].
- **Popularity:** 44 stars and 15 forks. The forks include `ag1le/nn-morse` (Mauri Niininen, AG1LE, whose work the survey cites) and `bg4xsd/nn-morse` [fact: API].
- **Lineage:** original work, not a fork. The README cites no prior work [fact].

### What it is

A training script plus a file decoder: [main.py](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/main.py), [morse.py](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/morse.py), [decode_audio.py](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/decode_audio.py), and the weights `models/001750.pt`. It is not a real-time application [fact].

### Input representation

- Real audio at f_s = 2000 samples/s ([morse.py L7](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/morse.py#L7)) [fact].
- `scipy.signal.spectrogram(samples, nperseg=40, noverlap=0)` ([L29–32](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/morse.py#L29-L32)) [fact]. With scipy's defaults, that means a Tukey(0.25) window, constant detrend, 'density' scaling and a one-sided spectrum. The result is **21 bins spaced 50 Hz from 0 to 1000 Hz**, with **20 ms frames and no overlap: 50 frames/s** [inference: scipy defaults]. `fs` is not passed, so the density scaling uses fs = 1. That only rescales the input [inference].
- The features are linear power, not log power, and the code applies no per-frame normalization [fact]. At decode time the whole file is resampled to 2000 samples/s and divided by its peak absolute value ([decode_audio.py L19–26](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/decode_audio.py#L19-L26)) [fact]. That is a whole-file operation, so it is not streaming as written. A streaming port would need an automatic gain control (AGC) in its place [inference].

### Architecture, loss and cost

- `Linear(21→256)`, then three `Linear(256→256)` layers, each followed by ReLU. Then `LSTM(256→256)`, one layer, unidirectional. Then `Linear(256→43)` and log-softmax ([main.py L50–74](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/main.py#L50-L74)) [fact]. There is no time convolution and no bidirectional layer. The README notes that this makes the network usable in a streaming fashion [fact].
- **Output classes:** 43, being CTC blank, space, A–Z, 0–9 and `. , ? = +` [fact: code; confirmed by the checkpoint shape `[43, 256]` quoted in issue #13].
- **Parameter count ≈ 740,400** [inference: arithmetic from code]:

  | Layer | Parameters |
  |---|---|
  | dense1 (21 × 256 + 256) | 5,632 |
  | dense2–4 (3 × 65,792) | 197,376 |
  | LSTM (4 × (256·256 + 256·256 + 2·256)) | 526,336 |
  | dense5 (256 × 43 + 43) | 11,051 |
  | **Total** | **740,395** |

  The checkpoint's 2,963,596 bytes ≈ 740,395 × 4 bytes plus about 2 kB of container overhead, which is consistent [inference].
- **Loss:** `nn.CTCLoss()`, with Adam at a learning rate of 10⁻³. A comment says to lower it to 10⁻⁴ after about 1,500 epochs ([L118–119](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/main.py#L118-L119)) [fact]. Decoding is greedy best-path; the code carries a "TODO: proper beam search" [fact].
- **Inference cost:** about 737,000 MAC per frame. That is 5,400 for dense1, 196,600 for dense2–4, 524,300 for the LSTM and 11,000 for dense5. At 50 frames/s this comes to **about 37 million MAC/s per channel**, roughly twice the survey's 15–20 million MAC/s estimate for VE3NEA's DeepCW [inference: arithmetic]. Every layer is a per-frame matrix-vector product, so the network batches across channels as well as DeepCW does [inference].

### Training data (synthetic generator in morse.py)

All of the following is [fact] from [morse.py L35–97](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/morse.py#L35-L97) and [main.py L81–90](https://github.com/pd0wm/nn-morse/blob/e1f7bdddfa6ea9d161fe14b5d4e639675a7a4ba8/main.py#L81-L90) unless labeled otherwise.

- **Text:** 10–19 characters drawn uniformly from the 42-symbol alphabet, space included. Consecutive spaces are possible. The first and last characters are never spaces. There is no language model or ham-text model.
- **Speed:** 10–39 WPM, uniform, constant within a sample. The PARIS dit is `(60/wpm)/50` s.
- **Timing jitter:** every mark and every gap is scaled independently by a factor from N(1, 0.2), clipped to [0.5, 2.0]. Dits and dahs share the same distribution. Element gaps are 1 dit. Character gaps are 1 + 2 = 3 dits. A word space appends another 7 dits after the 3-dit character gap, so the **word gap is about 10 dits, not 7** [inference: reading the code; the comments say "seven"]. There is no per-operator fist model and no dah/dit ratio variation separate from this jitter.
- **Carrier:** one sine at 100–949 Hz, uniform. Keying is rectangular: no rise-time shaping, no chirp, no key clicks.
- **Noise and level:** `σ² = 1e-6 · n · f_s/2 = 10⁻³·n`, with n uniform over 0–199. The output is `a·(0.5·keyed_sine + N(0, σ²))`, clipped to ±1.
- **Absent:** fading (QSB), QRM, impulse noise (QRN), frequency drift, and real recordings.

**SNR in the project convention** [inference: arithmetic from code]. The key-down signal power is P = 0.5²/2 = 0.125. The noise is real white Gaussian over 0 to f_s/2 = 1000 Hz, so N₀ = σ²/1000 Hz = 10⁻⁶·n per Hz. The common factor a cancels, apart from clipping. Hence

  **S₅₀₀ = P / (N₀ · 500 Hz) = 250 / n**, or in decibels **S₅₀₀ = 24.0 dB − 10·log₁₀ n**.

  | n | S₅₀₀ (key-on, 500 Hz) | E_s/N₀ per dit at 25 WPM (T = 48 ms) |
  |---|---|---|
  | 199 (worst) | +1.0 dB | +14.8 dB |
  | 100 | +4.0 dB | +17.8 dB |
  | 25 | +10.0 dB | +23.8 dB |
  | 1 | +24.0 dB | +37.8 dB |
  | 0 | noiseless | — |

  Because n is uniform, about 87% of training samples have S₅₀₀ < +10 dB, but **none are below +1.0 dB**. The survey places the real competition between decoders at S₅₀₀ ≈ 0 dB and below, so the shipped model has never seen the regime that matters [inference]. At large a with large n, the ±1 clip bites. The peak is about 0.75 plus noise with σ·a up to 0.66, which adds a nonlinearity that slightly lowers the effective SNR [inference].

- **Training volume:** each "epoch" is 2,048 freshly generated samples in batches of 64. The shipped checkpoint is epoch 1,750, or about 3.6 million samples [inference: arithmetic]. The README says the network "will converge after a few thousand epochs" [fact].

### Weights, results, evaluation

- The weights ship as `models/001750.pt`, a PyTorch state_dict, MIT [fact].
- **Results:** none. The repository has no CER, WER or SNR sweep and no held-out test. The only demonstration is `hello_world.png`, which shows one synthetic "HELLO WORLD" file (2000 samples/s; a noisy variant is included) with per-frame posteriors [fact]. The SNR of those demo files is not stated [fact].

### Reuse for KZ4AP Skimmer

- **Code and weights:** reusable under MIT with notice retention; compatible with GPL-3.0 [inference].
- **Input fit:** the project's per-station stream is 1500 samples/s complex, band-limited to about ±150 Hz. There are two ways to feed nn-morse [inference]:
  - Frequency-shift the baseband to a real audio tone, for example +600 Hz, and resample to 2000 samples/s. The existing weights would then see the signal within about 450–750 Hz, inside their 100–950 Hz training range.
  - Retrain on a native complex spectrogram, such as a 30-sample FFT at 1500 samples/s (20 ms frames, 50 Hz bins). Only about 7 of its 30 bins fall within ±150 Hz.

  Retraining is needed anyway (see below), so the weights are useful only as a smoke test.
- **Generator:** a clean, minimal reference for the "text → keyed tone → noise → spectrogram → CTC label" pipeline. It is too simple for the benchmark as it stands. It lacks fading, QRM, fists, speed drift and shaped keying, and it does not reach S₅₀₀ < +1 dB. The project generator should instead parameterize directly in S₅₀₀ [inference].
- **Architecture as a baseline:** it is useful as the "no convolution" control against DeepCW's CNN+LSTM. It has twice the MACs, and its dense front end cannot exploit frequency-shift invariance the way a convolution can [inference].

---

## 2. bg4xsd/cwlab

**Summary.** "CW Lab" is a lightly modified copy of pd0wm/nn-morse. The network is unchanged; the character set grows to 59 symbols, with prosigns, and the generator timing changes slightly. It ships four retrained checkpoints and a few recorded WAV files, with no quantitative evaluation. It is MIT licensed. It is **not related** to lucpaysan/CW-LAB.

### Identity and status

- **Author:** "Dr. Cat Lu / BFcat," callsign BG4XSD, of Nanjing, Jiangsu, China. The GitHub profile bio reads "nonlinear time series analysis and complex system"; the author's file headers give the contact bfcat@live.cn [fact: profile and file headers]. Some commits are authored as `bfcatlu` [fact].
- **License:** MIT (SPDX `MIT`), "Copyright (c) 2022 BG4XSD" ([LICENSE](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/LICENSE)) [fact]. It is GPL-3.0-compatible [inference]. The files derived from nn-morse do not reproduce pd0wm's copyright notice [fact: file headers credit only BFcat]. Any reuse should carry both MIT notices [inference].
- **Dates:** created 2022-12-10. The first commit in the history is dated 2022-12-04, before creation, presumably a local history pushed later. There are 86 commits; the last commit and push were on 2023-01-30 [fact: API].
- **Maintenance:** abandoned since January 2023 [inference]. The README's plans, "realtime audio input … realtime text message," and a MATLAB path were never implemented. `decoder_realtime()` is a `print("TODO")` stub ([test_audio.py L42–43](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/test_audio.py#L42-L43)) [fact].
- **Popularity:** 1 star, 0 forks [fact].
- **Related repositories by the same author:** `bg4xsd/deep_decoder` (MIT, created and last pushed 2023-07-03) holds only an environment README and a `morseNet.ipynb` notebook; its contents were not reviewed in this check. The author also keeps forks of nn-morse and morse-dataset [fact: API].

### Lineage: derived from pd0wm/nn-morse

- The README says so [fact]: "nn_morse is implemented by pd0wm … It's very useful …", and "The decoder.py, for the Dense-LSTM-Dense (DLD) network structure is kept unchanged … pd0wm's work is very GOOD."
- The git blob hashes prove it [fact]. `models_lib/original_NNmodel_001750_DO_NOT_USE.pt` and pd0wm's `models/001750.pt` are the same blob (`67bb5ab…`), as are the two `hello_world.png` files (`c55b3e2…`).
- The code is structurally identical. `NetDLD` in [decoder.py L167–196](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/decoder.py#L167-L196) is pd0wm's `Net` renamed, and `morse.py` and the training loop follow pd0wm line for line with the edits listed below [fact].
- The README also credits souryadey/morse-dataset as inspiration, but no code from it is used [fact].

### Relation to lucpaysan/CW-LAB (cited in the survey)

**Unrelated; the names merely resemble each other** [fact for the metadata; inference for "unrelated"]. lucpaysan/CW-LAB is a GitHub fork of `e04/web-deep-cw-decoder`, GPL-3.0, created 2026-03-23. It is a browser app running e04's ONNX model plus a Bayesian decoder. bg4xsd/cwlab is not a fork of anything, is MIT, dates from 2022–23, is written in PyTorch, and descends from pd0wm/nn-morse. No code, weights or authorship are shared. A search for repositories named like "cw-lab" turned up no other Morse project [fact: GitHub search].

### Changes relative to nn-morse

All of these are [fact] from [morse.py](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/morse.py) and the training script, unless labeled otherwise.

- **Alphabet:** 58 dictionary keys plus space, giving 59 symbols and 60 network outputs with the CTC blank. The additions include `' ! / ( ) & : ; - _ " @ $` and prosign stand-ins `#` = BK, `%` = CL, `^` = BT and `*` = SK ([L60–86](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/morse.py#L60-L86)).
- **Label bugs in that alphabet** [fact for the code; inference for the consequence]:
  - `'='` and `'^'` are both `-...-`, so BT gets two labels.
  - `'$'` is defined twice. The second definition, `...-.-`, wins, which makes `$` identical to `*` (SK).

  Identical audio therefore carries two different labels, which puts an irreducible confusion into CTC training. The repo's own test log shows it: models A emit `^` and models B emit `=` for the same BT separators ([CWLab_readme_R1.0.outpu.txt](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/docs/CWLab_readme_R1.0.outpu.txt)).
- **Spectrogram:** `noverlap = 40 // 8 = 5`, so the hop is 35 samples, or 17.5 ms: **57.1 frames/s** instead of 50. There are still 21 bins spaced 50 Hz ([L90–108](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/morse.py#L90-L108)).
- **Timing jitter:** the clip narrows to [0.75, 1.5] from [0.5, 2.0], with the same N(1, 0.2). The author's comment says wider jitter made dits and dahs unrecognizable ([L125–134](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/morse.py#L125-L134)).
- **Silences:** leading silence is a random 1–7 dits and trailing silence a random 3–5 dits, in place of a fixed 5 each. The word space is built from four separate draws, 1 + 2 + 3 + 1 dits. Added to the preceding character gap, that is still about 10 dits; the comment's "total 7 unit" counts only the added part [inference: reading the code].
- **Text length:** configurable. The author trained on lengths 10–19. The script mentions 1–9 characters for short real-audio buffers.
- **Unchanged:** noise model, amplitude range, pitch range of 100–949 Hz, speed range of 10–39 WPM, and rectangular keying. **S₅₀₀ therefore spans the same +1.0 to +24 dB, plus noiseless, as nn-morse**, and there is still no fading, QRM or chirp [inference: the same arithmetic as §1].
- **Parameters:** 740,395 + 17 × 257 = **744,764**. The checkpoints' 2,982,327 bytes are consistent with that [inference: arithmetic]. Inference cost is about 737,000 MAC per frame × 57.1 frames/s ≈ **42 million MAC/s per channel** [inference].
- **Training script defects** [fact]:
  - [train_tensorboard.py L121](https://github.com/bg4xsd/cwlab/blob/664ac052d1b7fe1a989861f972a6b833d65c7d2d/morse_decoder/train_tensorboard.py#L121) sets `myConfig = myConfigFix`. That overrides every command-line argument with a 10-epoch debug configuration.
  - The script imports `wandb` but never uses it.
  - `run.sh` calls a `main_pro.py` that does not exist.

### Weights and results

- **Checkpoints** (MIT) [fact: filenames and log]: `readme.R1.0_A.{001800,005000}.pt` and `readme.R1.0_B.{001880,005090}.pt`. The log names their training runs `models_Len10-20_Batch64_Lr_e3-e5_3stage_5k` and `…_5.5k`, meaning 10–20 characters, batch 64, and learning rates stepped from 10⁻³ to 10⁻⁵ in three stages. The loss plot for run A shows a plateau near 4.2 until about epoch 300. The loss then falls to about 0.6 by epoch 1,500 and stays at about 0.5 through epoch 5,000 [fact: `readme.R1.0_A._Loss.png`].
- **Sound library** [fact: WAV headers read by range request]:

  | Files | Format | Duration | Transcript |
  |---|---|---|---|
  | `CallCQ_*` (5 files, with generator parameters in the name) | synthetic, 2000 samples/s, float32 | — | yes |
  | `CQ_DE_BG4XSD.wav` | synthetic, 2000 samples/s | — | yes |
  | `realQSO_001–003.wav` | real, 6000 samples/s, float32 | about 94 s, 124 s and 263 s [inference: size / (4 bytes × 6000)] | only `realQSQ_001.txt`, a BH4FCD–VR2GM contact, and it is not stated which WAV it matches |
  | `100MostCommonEnglishWords{12,24}.wav` | 6000 samples/s | — | none shipped |
  | `demo_with_mobile_reord_sound.wav` | phone recording | — | none; the author notes the true text is 5-character groups but does not give it |

- **Results:** qualitative only; no CER table [fact]. What the one log shows:
  - On the two 100-word files (12 and 24 WPM, SNR not stated), model A.005000's output matches the word list except for the BT labels. Model B.005090 merges many word spaces ("THISTHESE").
  - On synthetic CQ calls at n = 128 and n = 189, which are S₅₀₀ ≈ +2.9 dB and +1.2 dB by §1's formula [inference], the models produce 0–3 character errors in about 35 characters, one trial each.
  - On the phone recording the output is garbage.
  - The README says real QSOs "seem somewhat difficult to the models," and no real-QSO decode appears in the log.

### Reuse for KZ4AP Skimmer

- Nothing beyond what nn-morse already offers [inference]. The prosign-extended alphabet is a useful list, but its duplicate codes must be fixed before use.
- The three real QSO WAVs at 6000 samples/s are very small (about 8 minutes in total), have no aligned transcripts and no stated SNR, and their redistribution rights are unclear. The files are in an MIT repository, but the on-air audio of third parties (BH4FCD and VR2GM) is not obviously the committer's to license. They could serve as informal smoke tests at most [inference].

---

## 3. souryadey/morse-dataset

**Summary.** A generator and two pre-built datasets for **single-character Morse symbol classification** on abstract 64-sample intensity vectors. The data is not audio and has no sample rate, speed, tone or CTC sequence labels. The project is MIT licensed and has an ICCCNT 2018 paper. It is not usable as a training or benchmark set for this project.

### Identity and status

- **Author:** Sourya Dey, then at the University of Southern California (USC), with K. M. Chugg and P. A. Beerel as paper coauthors. His GitHub profile now lists Galois, Arlington, VA. No callsign is given [fact: profile and README].
- **Paper:** S. Dey, K. M. Chugg, P. A. Beerel, "Morse Code Datasets for Machine Learning," *ICCCNT 2018*, pp. 1–7 ([arXiv:1807.04239](https://arxiv.org/abs/1807.04239); [IEEE 8494011](https://ieeexplore.ieee.org/document/8494011)) [fact: README]. The paper's full text could not be read in this check, because the PDF did not render. Only the abstract was read [gap].
- **License:** MIT (SPDX `MIT`), with the LICENSE file added 2019-11-03 [fact]. It is GPL-3.0-compatible [inference]. The paper itself is IEEE-copyrighted [fact: README]. The README also points to an IEEE DataPort page and a 2020 competition [fact].
- **Dates:** created 2017-10-13; last commit 2020-09-05, which added an image. The last substantive code change predates that [fact: API].
- **Maintenance:** dormant [inference].
- **Popularity:** 26 stars and 3 forks, one of them `bg4xsd/morse-dataset` [fact].

### Content and format

All of the following is [fact] from [generate_morse_dataset.py](https://github.com/souryadey/morse-dataset/blob/da296f40eb931337612b2fc48f89e4e880ebed13/generate_morse_dataset.py) unless labeled otherwise.

- **Classes:** 64 symbols from `Codebook.npy`, a pickled dict. It holds A–Z, 0–9 and punctuation, plus non-English letters written as `J^`, `H^`, `N~`, `CH`, `G^`, `C,`, `U..`, `D-`, `O..`, `S^`, `A..` and `E\``. The longest code has 6 elements [fact: decoded in this check]. **`(` and `H^` are both `-.--.`** [fact], so two classes are indistinguishable. With equal priors, that alone forces at least 1/128 ≈ 0.8% test error [inference].
- **Example:** one character per 64-sample frame; no words, no inter-character timing and no sequences. "Samples" are abstract time steps with no physical sample rate or WPM [fact].
- **Element lengths:** in samples, dit 1–3, dah 4–9 and intra-character gap 1–3, each uniform and independent. There is no 1:3 constraint within a character, so dit and dah durations vary freely inside one symbol [fact]. Frames are left-aligned (`leadingsp_rand=0`) unless a random leading offset is enabled.
- **GRAY style** (used for both shipped files): marks are Gaussian-valued with mean 12 and σ = 1.34, and gaps are 0. Additive Gaussian noise with mean `noisemean` and σ `noisesd` is added everywhere. Values are clipped to [0, 16], divided by 16 and rounded to 3 decimals. Clipping at 0 half-rectifies the noise in the gaps. A BW style instead uses 0/1 values with random bit flips.
- **Shipped files:**

  | File | Size | Generator settings | Split per class (train/val/test) | Total |
  |---|---|---|---|---|
  | `baseline.npz` | 22.0 MB | defaults, **`noisesd = 0`**: only mark-level jitter, no additive noise | 5000/1000/1000 | 448,000 × 64 float64 |
  | `difficult.npz` | 45.6 MB | `noisesd = 4`, `minlendash = 3` (dah can equal the longest dit), random leading offset | 5000/1000/1000 | 448,000 × 64 float64 |

  Labels are one-hot vectors of length 64, stored as `xtr/ytr/xva/yva/xte/yte` in the npz files [fact].
- **Discrepancy:** the author's blog describes the baseline as having noise with σ = 1 added to every value, but the code's default is `noisesd = 0` ([blog](https://cobaltfolly.wordpress.com/2017/10/15/morse-code-dataset-for-artificial-neural-networks)) [fact for both statements]. Which one produced the shipped `baseline.npz` cannot be told without loading it [gap].
- **"SNR":** there is no physical SNR and no bandwidth. For `difficult`, the per-sample ratio of mean key-on power to noise variance is (12² + 1.34²)/4² ≈ 9.1, or about +9.6 dB per abstract sample. It is referenced to nothing physical and is not convertible to S₅₀₀ [inference: arithmetic].
- **Reported accuracy:** the author reported 97.64% on baseline and 26.22% on difficult for his network, in blog comments. A commenter, "londumas," reported 98.43% and 48.04% [secondhand: blog comments]. The network architectures are not described in those comments.

### Code defects

- The generator is Python 2 code. `Codebook.values()[n]` ([L60](https://github.com/souryadey/morse-dataset/blob/da296f40eb931337612b2fc48f89e4e880ebed13/generate_morse_dataset.py#L60)) fails in Python 3, and `np.load(...).item()` ([L38](https://github.com/souryadey/morse-dataset/blob/da296f40eb931337612b2fc48f89e4e880ebed13/generate_morse_dataset.py#L38)) needs `allow_pickle=True` on numpy ≥ 1.16.3. The README nonetheless says "Python 3" [fact for the code; inference that it errors].
- The npz files store label indices only. The index-to-character map is the iteration order of the pickled dict in whatever interpreter generated them, and it is not stored. Recovering which one-hot column is which character requires assuming that order [inference].
- The upper error bound U in [dataset_metrics.py L111](https://github.com/souryadey/morse-dataset/blob/da296f40eb931337612b2fc48f89e4e880ebed13/dataset_metrics.py#L111) indexes `label_d[m,c]`, where `c` is a stale loop variable, instead of `label_d[m,j]`. Both L and U also divide by `label_coll_variances**2`, a variance squared, where σ² is meant [fact for the code; inference that these are bugs].

### Suitability for this project

**Not suitable as either a training set or a benchmark set** [inference]:

- it contains no audio or IQ data, so no filter, envelope, fading or QRM can be modeled;
- it contains no timing between characters or words, so it cannot exercise CTC or segmentation;
- it has no speed or physical SNR axis to map to S₅₀₀.

At most, its generator idea could unit-test an isolated element-sequence classifier, but the project's own generator would do that better. **Nothing to reuse**, beyond noting that its difficulty metrics exist.

---

## Effect on the survey's ranking and benchmark plan

- **Ranking: no change** [inference]. None of the three repositories publishes a quantitative CER result, so none adds evidence for or against options 1–4. nn-morse and cwlab are a dense-front-end LSTM+CTC design, a weaker relative of option 2 at about twice DeepCW's per-channel cost. DeepCW remains the better template.
- **Benchmark plan: no change** [inference]. One possible addition: nn-morse's 740,000-parameter Dense+LSTM could serve as a cheap "no convolution" ablation of option 2 once the project's generator exists. It would have to be retrained on the project's data, down to S₅₀₀ ≤ 0 dB and with fading. That addition is optional and not a revision of the plan.
- **Survey text:** the CW-LAB citation in decoder-survey.md refers correctly to lucpaysan/CW-LAB, the e04 fork. bg4xsd/cwlab is a different project and should not be conflated with it [fact/inference].
