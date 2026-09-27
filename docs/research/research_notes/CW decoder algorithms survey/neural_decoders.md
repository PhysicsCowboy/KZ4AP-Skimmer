# Neural-network approaches to automatic Morse (CW) decoding, 2015–2026

Research date: 2026-09-26. Every claim is labeled **[documented fact]** (stated in the primary source cited), **[secondhand]** (reported by a third party or a search-engine summary of a source I could not open), or **[inference]** (my own reasoning from the cited facts). Measurements I made myself are labeled **[measured here]**, with the method given.

Target system for the inferences: per-station complex baseband at 1500 sample/s, band-limited to about ±150 Hz, with hundreds of stations at once on a desktop CPU or Raspberry Pi.

---

## 1. Architectures, notable papers, and open-source projects

### Takeaway
Almost every neural Morse decoder since 2015 uses the same design borrowed from speech and handwriting recognition. A time–frequency (spectrogram) input feeds a small CNN front end, then a recurrent layer (LSTM) or, since about 2025, a Conformer/transformer encoder, and the network is trained with CTC loss (a training method that lets it learn character sequences without being told exactly when each character starts). The two projects that matter most for a skimmer are both recent:
- **VE3NEA's DeepCW** (2024), a CNN+LSTM+CTC model by the author of CW Skimmer, benchmarked directly against CW Skimmer.
- **e04's DeepCW** (2025–2026), a roughly 3.6M-parameter attention-based model published as ONNX.

### Cited findings

**Chronology of notable work**
- 2015 (Nov–Dec): AG1LE (Mauri Niininen) published early experiments on an LSTM RNN Morse decoder in TensorFlow [documented fact] — [AG1LE 2015-11](http://ag1le.blogspot.com/2015/11/experiment-deep-learning-algorithm-for.html), [AG1LE 2015-12](http://ag1le.blogspot.com/2015/12/tensorflow-new-lstm-rnn-based-morse.html); code in [ag1le/LSTM_morse](https://github.com/ag1le/LSTM_morse). I read only the post titles and the search summaries, not the details [secondhand].
- 2018: netom/MorseNet ("cwdecode") is a research repo that trains an LSTM with CTC loss on chunked *raw audio*, not a spectrogram, using synthetic WAV generation [documented fact] — [netom/MorseNet README](https://github.com/netom/MorseNet). The repo was created 2018-04 and last pushed 2026-06 (GitHub API).
- 2018: Dey, Chugg and Beerel (USC), "Morse Code Datasets for Machine Learning," arXiv 1807.04239. This is a synthetic dataset generator of tunable difficulty for classifying *isolated* Morse symbols, used to study network-complexity reduction. It is not a streaming decoder [documented fact] — [arXiv 1807.04239](https://arxiv.org/abs/1807.04239). The search summary describes a single 1024-neuron hidden-layer MLP (multilayer perceptron) with 64 outputs [secondhand] — [arXiv PDF](https://arxiv.org/pdf/1807.04239).
- 2018: "Automatic Morse Code Recognition Under Low SNR" (MECAE 2018, Atlantis Press) combines signal processing with deep learning [secondhand; the PDF would not parse] — [Atlantis Press](https://www.atlantis-press.com/proceedings/mecae-18/25893679). [Corrected 2026-09-27: full text read; the "deep learning" is a DNN-HMM hybrid compared against GMM-HMMs on whole-word recognition, results are word error rate (best 1.1% at 8 dB, 18% at −3 dB; SNR bandwidth and definition not stated) — see ../../wang-yfdm-notes.md §a]
- 2019: "DeepMorse: A Deep Convolutional Learning Method for Blind Morse Signal Detection in Wideband Wireless Spectrum" is a CNN for *detecting* Morse signals in wideband spectrum, not decoding them [secondhand; title only] — [ResearchGate](https://www.researchgate.net/publication/333793763_DeepMorse_A_Deep_Convolutional_Learning_Method_for_Blind_Morse_Signal_Detection_in_Wideband_Wireless_Spectrum).
- 2019-02: AG1LE, "Training a Computer to Listen and Decode Morse Code" [documented fact] — [AG1LE 2019-02](http://ag1le.blogspot.com/2019/02/training-computer-to-listen-and-decode.html).
  - Architecture: CNN-LSTM-CTC with 5 conv layers, 2 LSTM layers of 256 units, and CTC. It was adapted from a handwritten-text-recognition model.
  - Input: 8 kHz audio, demodulated and decimated, then shaped into 128×32 "images."
- 2020-04: AG1LE, "New real-time deep learning Morse decoder" [documented fact] — [AG1LE 2020-04](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html); code in [ag1le/deepmorse-decoder](https://github.com/ag1le/deepmorse-decoder).
  - Same CNN-LSTM-CTC design, with a live microphone input.
  - Front end: real-time FFT spectrogram with automatic peak-picking to find the CW tone, cut into 128×32 images for the model.
- 2021–2022: 1-800-BAD-CODE/MorseCodeToolkit trains NVIDIA NeMo **QuartzNet 10x5** CTC models (a 1-D convolutional speech-recognition architecture) end-to-end on Morse audio [documented fact] — [MorseCodeToolkit README](https://github.com/1-800-BAD-CODE/MorseCodeToolkit).
  - It ships pretrained English and Russian models. The Russian model was fine-tuned from the English one.
- 2023: "Morse Code Audio Recognition using LSTM-CTC Model," an IEEE conference paper, uses MFCC features (a speech-style spectral summary) with an LSTM and CTC [secondhand; abstract via search] [Confirmed 2026-09-27: MFCC → a single bidirectional LSTM → CTC, no CNN; clean synthetic 5 WPM clips, no SNR defined, no CER/WER reported, only training/validation accuracy (95% / 4% for the best-trained model, i.e., overfit) — see ../../neural-papers-notes.md §1] — [IEEE Xplore 10181830](https://ieeexplore.ieee.org/document/10181830/), [UCSY PDF](https://onlineresource.ucsy.edu.mm/bitstream/handle/123456789/2648/ICCA19064.pdf?sequence=1&isAllowed=y).
- Chinese-language journal (无线电工程 / *Radio Engineering*, date not confirmed) [secondhand; abstract via search] — [CNKI](https://wxdg.cbpt.cnki.net/portal/journal/portal/client/paper/dff1f1b161b2350f2d652bb650ed00dd).
  - Front end: a wavelet time–frequency image.
  - Model: 3D-CNN plus bidirectional ConvLSTM, with a CTC output layer.
  - Claim: more than 98% word accuracy on "unstable" Morse across SNRs, with relatively few training samples.
- 2023: "YFDM: YOLO for detecting Morse code" (PMC 10667528) applies YOLO object detection to Morse in spectrograms. This is detection, not decoding [secondhand; the page was behind a CAPTCHA, so title only] [Confirmed 2026-09-27: a modified YOLOv5s detecting Morse bursts in 2 MHz-wide, 640×640 STFT spectrogram images; synthetic data with "−5 to 0 dB" noise (reference and bandwidth not stated) — see ../../wang-yfdm-notes.md §b] — [PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC10667528/).
- Undated (Technion EE Deep Learning course 046211 final project): MaorAssayag/morse-deep-learning-detect-and-decode [documented fact] — [repo](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode).
  - Decoder: dense layers, one LSTM and CTC, 740,395 parameters, input a spectrogram with 21 frequency bins.
  - Detector: a separate Faster R-CNN (ResNet-50), about 41.3M parameters, to find Morse bursts.
- **2024-10: VE3NEA/DeepCW** by Alex Shovkoplyas, the author of CW Skimmer. MIT license; repo created 2024-10-21 and last pushed 2024-10-27 (GitHub API) [documented fact] — [VE3NEA/DeepCW](https://github.com/VE3NEA/DeepCW), `model.ipynb`.
  - Layers: Keras model with Conv2D(32, 3×3) → MaxPool 2×2 → Conv2D(64, 3×3) → MaxPool 2×2 → Dense(64) → **stateful LSTM(256)** → softmax over 43 classes (41 characters, space and blank), trained with CTC.
  - Size: the parameter summary lists 320 + 18,496 + 16,448 + 328,704 + 11,051, which is **about 375k parameters**.
  - Inference: a model rebuilt with batch 1 and a 4-frame-wide input (plus overlap margin), so it runs as a streaming decoder with a greedy CTC decode.
- **2025-08 to 2026-08: e04/web-deep-cw-decoder ("DeepCW", deepcw.cc)** [documented fact] — [e04/web-deep-cw-decoder](https://github.com/e04/web-deep-cw-decoder); [e04/deepcw-engine](https://github.com/e04/deepcw-engine).
  - Browser, desktop and mobile real-time decoder with "multi-channel decoding" and a separate neural noise-reduction model.
  - The model is released separately as `deepcw-engine` (`model.onnx`, 2026-06) with Python and Node.js ONNX Runtime examples.
  - Model metadata (`model.onnx.json`): 3200 Hz sample rate, FFT 256, hop 48 samples, spectrogram covering 400–1200 Hz in 65 bins, log1p normalization. Output is per-frame log-probabilities over 42 classes (41 characters plus blank), which is a CTC-style output.
- [measured here] The deepcw-engine ONNX graph (15.1 MB) has **3,614,481 parameters**. Its operators include 21 Conv, 44 MatMul, 32 LayerNormalization, 6 Softmax and 27 Sigmoid, with no LSTM or GRU. That pattern is consistent with a Conv plus self-attention (Conformer-like) encoder. Method: `onnx` Python package, operator count and initializer sum. — [deepcw-engine](https://github.com/e04/deepcw-engine)
- A 2026 fork, CW-LAB / "CW Master" by BY4CWY, based on e04's decoder, describes the neural decoder as "CRNN + CTC" and also offers a traditional Bayesian decoder [documented fact] — [lucpaysan/CW-LAB](https://github.com/lucpaysan/CW-LAB). The "CRNN" wording may describe an earlier e04 model than the current attention-based ONNX [inference].
- **2026-04 to 2026-09: sderhy/morseformer** (v0.6.4, Apache-2.0) [documented fact] — [morseformer README](https://github.com/sderhy/morseformer).
  - Pipeline: DSP front end (complex bandpass filter at the carrier) → 8-layer Conformer (d=144, rotary position embeddings, 4× time subsampling, about 3.9M parameters) → dual CTC and RNN-T heads (the RNN-T head is about 0.2M parameters) → optional character 3-gram language model (482 KB) that re-splits run-together words.
  - A 4.8M-parameter GPT-style language model exists but is off by default because "it hurt amateur jargon."
  - The README claims that, as of April 2026, no transformer-based CW decoder had been published. That is contradicted by e04's attention-based model (released 2026-06, possibly earlier in the web app), so treat the claim as dated [inference].
- The VE3NEA Morse Expert Android app uses "the same algorithms as used in CW Skimmer." There is no sign of a neural network in it [documented fact] — [Morse Expert](https://ve3nea.github.io/MorseExpert/).

**Input representations seen**
- Magnitude spectrogram crop, narrow: VE3NEA DeepCW uses 22 bins at 11.7 Hz spacing (about 258 Hz span), stepped at 93.75 frames/s. The settings imply 6000 Hz sampling (FFT 512, hop 64, 300-sample window) [documented fact for the settings; the 6 kHz rate is inference from "512 ⇒ 11.7 Hz"] — [VE3NEA/DeepCW model.ipynb](https://github.com/VE3NEA/DeepCW).
- Wide spectrogram: e04 uses 400–1200 Hz in 65 bins at 66.7 frames/s [documented fact] — [deepcw-engine](https://github.com/e04/deepcw-engine).
- Spectrogram "images" of 128×32 (AG1LE) [documented fact] — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).
- Raw audio chunks (MorseNet) [documented fact] — [MorseNet](https://github.com/netom/MorseNet).
- MFCCs [secondhand] — [IEEE 10181830](https://ieeexplore.ieee.org/document/10181830/). [Confirmed 2026-09-27: MFCC, frame and coefficient parameters not stated — see ../../neural-papers-notes.md §1]
- Wavelet time–frequency images [secondhand] — [CNKI](https://wxdg.cbpt.cnki.net/portal/journal/portal/client/paper/dff1f1b161b2350f2d652bb650ed00dd).
- Complex bandpass at the carrier, then an encoder (morseformer) [documented fact] — [morseformer](https://github.com/sderhy/morseformer).

### Inferences
- The field has converged on the pattern "time–frequency features → small conv → sequence model → CTC." Newer projects move from LSTM to Conformer/attention and add an external language model. This mirrors automatic speech recognition (ASR) circa 2018–2022.
- VE3NEA's design fits a per-station skimmer channel most closely: a narrow roughly 258 Hz, 22-bin input around one signal and a *stateful* streaming LSTM. The ±150 Hz, 1500 sample/s channel could feed an equivalent crop directly.
- e04's model takes an 800 Hz-wide audio passband with a positional attention encoder. It is built for "audio from a transceiver," not for per-channel baseband.
- The large published academic literature on neural Morse work (detection, isolated symbols, assistive devices) is mostly irrelevant to HF skimming. The practically important work sits in GitHub repos and blogs, not in peer-reviewed venues.

### Gaps
- I could not read the full text of the 2018 Atlantis Press paper (the PDF was unparseable), the CNKI 3DCNN-BiConvLSTM paper, the IEEE 2023 LSTM-CTC paper, or the YFDM paper (CAPTCHA). Their SNR definitions and figures are unverified. [Resolved 2026-09-27 for three of the four: Wang 2018 and YFDM (see ../../wang-yfdm-notes.md) and the IEEE 2023 paper (see ../../neural-papers-notes.md §1) are now read in full; none states an SNR bandwidth or key-on/average definition, and the IEEE paper states no SNR at all. The CNKI (Radio Engineering) paper was deliberately skipped by the project owner and remains unread.]
- The e04 repos do not document the architecture. The "Conformer-like" description is my inference from the ONNX operators.
- I found no QEX or QST article on neural CW decoders. An eHam article, "CW Decoding Using Neural Networks" ([eHam 28435](https://www.eham.net/article/28435)), exists but returned HTTP 403, so its content is unknown. [Resolved 2026-09-27: it is AG1LE's July 2012 self-organizing-map (7×7 map, 14 characters) demonstration, with no CER, WER or SNR figures; it also mentions an earlier unpublished test on about 40,000 characters from the 7, 14 and 18 MHz bands — see ../../neural-papers-notes.md §2]

---

## 2. Training data: synthetic generation, real recordings, and generalization

### Takeaway
Nearly all models are trained on synthetic data. The strongest generators model operator timing statistics (per keying style), Rayleigh fading with Doppler spread, pitch error, and character frequencies drawn from real traffic. Synthetic-only models look excellent on synthetic tests and much worse on real on-air audio: morseformer reports about 18% CER on real hand-keyed ragchews after a real-audio fine-tune. The gap between synthetic and real signals is the main open problem.

### Cited findings

**VE3NEA DeepCW (2024)** [documented fact] — [VE3NEA/DeepCW data_generation.ipynb, model.ipynb](https://github.com/VE3NEA/DeepCW)
- Data source and fading:
  - Data are generated as spectrograms on the fly.
  - Character frequencies and word-length distributions were collected from "a large number of CW messages decoded with CW Skimmer on the Ham bands."
  - Channel model: Rayleigh fading with a Doppler spread of 0.1–3 Hz.
- Training ranges:
  - SNR from −16 to +50 dB and speed from 8 to 50 WPM.
  - Pitch error ±30 Hz and noise floor ±20 dB.
  - Keying style mix: hand key 25%, paddle 50%, computer 25%.
- Keying model:
  - Element lengths are log-normal.
  - Each keying style (hand key, Vibroplex, paddle, computer) has its own mean and standard deviation for dot, dash, intra-character, character and word spaces. For example, a hand key has dash mean exp(1.5) dots with a σ of 0.3 in log space.
  - A random on/off imbalance of about 0.1 dot per transmitter models keying circuits that switch on and off at different speeds.
- Training volume:
  - 329 batches per epoch of 4 spectrograms × 512 frames, for 50 epochs.
  - The `batch_count` formula corresponds to about 30 minutes of audio per epoch, with signal parameters re-randomized every 5 batches.

**e04 DeepCW** [documented fact] — [e04/web-deep-cw-decoder](https://github.com/e04/web-deep-cw-decoder)
- The benchmark is in AWGN. The training-data recipe is not published.

**morseformer** [documented fact] — [morseformer](https://github.com/sderhy/morseformer)
- Synthetic "HF pipeline":
  - Operator model: element/gap jitter, dash:dot ratio changes, gap inflation and inter-word silence inflation.
  - Channel model: AWGN, QSB, QRN, carrier jitter and drift, receiver bandpass, and QRM.
  - Text: callsigns, Q-codes, QSO templates and prose.
- Real-audio fine-tune (v0.6.4):
  - Fine-tuned on hand-keyed ragchew audio, using forced alignment (`torchaudio.functional.forced_align`) to get per-token timestamps for targeted word-gap augmentation.
  - The README says a "silent-truncation bug" had blocked five earlier training phases.
  - It lists broader real-audio coverage (multiple operators, W1AW transcripts) as a data gap.

**AG1LE** [documented fact]
- 2019: 5,000 synthetic single-word samples (5.2 h), 20–30 WPM, SNR of only 20/30/40 dB — [AG1LE 2019](http://ag1le.blogspot.com/2019/02/training-computer-to-listen-and-decode.html).
- 2020: 27.8 h of ARRL code-practice audio (25,000 four-second WAVs), with digits only about 8.6% of the corpus, which caused number errors — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).

**Other projects** [documented fact]
- MaorAssayag: synthetic data with variable pitch, speed and amplitude, SNR from −10 to +10 dB, and about 50M characters for LSTM training — [repo](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode).
- MorseCodeToolkit: generates corpora from text files and mixes in real background-noise recordings — [MorseCodeToolkit](https://github.com/1-800-BAD-CODE/MorseCodeToolkit).

**Generalization evidence**
- AG1LE 2020 [documented fact] — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).
  - On live 30 WPM audio, confidence swung from 4% to over 90%.
  - Failures came from characters split at frame boundaries, numbers, and the lack of word spacing.
  - The author attributes the gap partly to having about 1000× less training material than commercial ASR.
- morseformer v0.6.4 on real hand-keyed ragchews (26 clips, 31 min) [documented fact] — [morseformer](https://github.com/sderhy/morseformer).
  - Aggregate: **CER 17.75%** and WER 44.31%.
  - By speaker: 8.45% CER on one speaker set (g3ses) and 28.60% on a held-out operator (g6pz).
  - The previous version scored 26.98% CER. Only the synthetic example ("CQ DE F4HYY K" at 20 WPM, +20 dB) is shown decoding cleanly.
- e04 on three short real YouTube QSO clips [documented fact] — [e04 README](https://github.com/e04/web-deep-cw-decoder).
  - DeepCW's output had mostly word-spacing errors plus a few character errors.
  - The author frames this as a non-general comparison.

### Inferences
- The largest real-world failure mode is word and character spacing on hand-sent Morse, not tone detection. Every real-audio report (AG1LE, morseformer, e04's clips) shows run-together or split words. A classical decoder with an explicit timing model, or a language-model word splitter, may be needed after a neural character recognizer.
- VE3NEA's use of CW Skimmer-derived on-air character and word statistics, together with per-keying-style log-normal timing, is the most principled synthetic generator found. It is a good template for a training and benchmark generator in this project.
- A synthetic-only model should be judged on real recordings with known transcripts. A synthetic AWGN CER curve (as in e04's README) says little about hand-keyed QSOs.

### Gaps
- I found no published large labeled corpus of real HF CW with ground-truth text, SNR and timing. morseformer names this as a gap too.
- There is no quantitative study of how synthetic QRM (overlapping signals) affects neural decoders.

---

## 3. Reported performance: CER versus SNR, speed, poor fists, QSB, QRM, and classical comparisons

### Takeaway
VE3NEA's own 2024 controlled benchmark, run on identical synthetic data, is the only apples-to-apples neural-versus-CW-Skimmer result found.
- At or below about 0 dB, the small CNN+LSTM is about equal to CW Skimmer (slightly worse at the lowest SNRs).
- Under fast fading (1–3 Hz Doppler), it is substantially better.
- It is also better at high SNR with hand keying. There, CW Skimmer plateaus at 8–20% CER and DeepCW at 6%.

e04 reports near-zero CER down to −6 dB (average power in 2500 Hz, AWGN). SNR definitions differ between sources by several dB, so headline numbers are not directly comparable.

### Cited findings

**VE3NEA DeepCW versus CW Skimmer (2024)** [documented fact] — [VE3NEA/DeepCW accuracy_charts.ipynb](https://github.com/VE3NEA/DeepCW)
- Test conditions:
  - Both decoders ran on the same generated data, with Rayleigh fading at 0.1, 0.3, 1 and 3 Hz Doppler spread.
  - Speeds were 8–48 WPM and keying was hand key or paddle.
  - Each point used about 30,000 characters.
- SNR definition:
  - The chart axis is "Key-on SNR in 3 kHz at 24 WPM, dB," with a secondary Eb/N0 axis.
  - The conversion constant is `EB_N0_TO_3KHZ_KEYON_SNR = 10·log10(info_bit_rate / NOISE_BW / duty_cycle)`, in `error_rate.ipynb`.
- The SNR points were [−16, −12, −6, −3, 0, 6, 10, 20, 30, 50] dB. Selected CER values follow (Skimmer / DeepCW).

| Condition (24 WPM unless noted) | −6 dB | 0 dB | +6 dB | +10 dB | +20 dB | +50 dB |
|---|---|---|---|---|---|---|
| Paddle, Doppler 0.1 Hz | 0.364 / 0.373 | 0.101 / 0.137 | 0.039 / 0.048 | 0.022 / 0.025 | 0.011 / 0.009 | 0.011 / 0.005 |
| Paddle, Doppler 1 Hz | 0.457 / 0.452 | 0.221 / 0.195 | 0.135 / 0.074 | 0.111 / 0.035 | 0.101 / 0.009 | 0.109 / 0.005 |
| Paddle, Doppler 3 Hz | 0.654 / 0.533 | 0.459 / 0.241 | 0.419 / 0.085 | 0.396 / 0.039 | 0.390 / 0.010 | 0.405 / 0.009 |
| Hand key, Doppler 0.1 Hz | 0.429 / 0.412 | 0.188 / 0.186 | 0.106 / 0.110 | 0.091 / 0.082 | 0.076 / 0.069 | 0.083 / 0.063 |
| Hand key, Doppler 1 Hz | 0.552 / 0.509 | 0.329 / 0.261 | 0.237 / 0.137 | 0.213 / 0.093 | 0.197 / 0.060 | 0.203 / 0.060 |
| Hand key, Doppler 3 Hz | 0.790 / 0.586 | 0.633 / 0.304 | 0.566 / 0.147 | 0.550 / 0.098 | 0.533 / 0.066 | 0.548 / 0.065 |
| Hand key, 12 WPM, Doppler 3 Hz | 0.933 / 0.695 | 0.775 / 0.386 | 0.764 / 0.208 | 0.781 / 0.169 | 0.868 / 0.128 | 0.952 / 0.125 |
| Paddle, 12 WPM, Doppler 0.1 Hz | 0.352 / 0.445 | 0.129 / 0.184 | 0.050 / 0.081 | 0.032 / 0.052 | 0.016 / 0.027 | 0.017 / 0.027 |

- At −16 and −12 dB, both decoders are at about 0.74–0.93 CER, i.e. essentially failing.
- The same repo computes a theoretical lower-bound CER for on/off keying in AWGN and in Rayleigh fading (Rician versus Rayleigh amplitude distributions, with a CER derived from BER using Morse code lengths and on-air character statistics) — `error_rate.ipynb` [documented fact].

**e04 DeepCW (README, 2026)** [documented fact] — [e04/web-deep-cw-decoder](https://github.com/e04/web-deep-cw-decoder)
- Test conditions: "HIGH" mode, AWGN only.
- SNR definition: time-averaged signal power over the whole record at about 50% keying duty cycle, referenced to a **2500 Hz** noise bandwidth.
- CER is Levenshtein-based (it counts insertions, deletions and substitutions).
- Results:
  - 0.00% CER from 0 to −4 dB at all tested speeds.
  - "Nearly error-free" at −6 dB.
  - Below 1.5% at −8 dB and below 8% at −10 dB across "the full speed range."
  - The specific WPM values appear only in a heatmap image, not in the text.
- Real-clip comparison against CW Skimmer, fldigi and ggmorse (all at their latest versions as of 2026-06-03, three YouTube QSO shorts):
  - DeepCW's transcripts had the fewest character errors, with its main faults being run-together words.
  - CW Skimmer showed character substitutions and deletions, some duplicate decodes and "TTTK"-style garbage.
  - fldigi and ggmorse were worse.
  - This is a small, uncontrolled sample and the author explicitly disclaims it as a general ranking.

**Other reported numbers**
- AG1LE 2019: CER 0.1% and 99.5% word accuracy on held-out *clean* synthetic data. Decoding stayed "relatively good" to −3 dB when trained on +40 dB data (low confidence) [documented fact] — [AG1LE 2019](http://ag1le.blogspot.com/2019/02/training-computer-to-listen-and-decode.html).
- AG1LE 2020: CER 1.5% and 97.2% word accuracy on ARRL practice audio (clean) [documented fact] — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).
- MaorAssayag: average CER below 2% above −5 dB SNR, with SNR bandwidth not specified [documented fact] — [repo](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode).
- MorseCodeToolkit QuartzNet: "~1% WER" on its own synthetic data [documented fact] — [MorseCodeToolkit](https://github.com/1-800-BAD-CODE/MorseCodeToolkit).
- 3DCNN-BiConvLSTM: more than 98% word accuracy across SNRs [secondhand] — [CNKI](https://wxdg.cbpt.cnki.net/portal/journal/portal/client/paper/dff1f1b161b2350f2d652bb650ed00dd).
- morseformer: 17.75% CER on real ragchews. It publishes no CER-versus-SNR curve [documented fact] — [morseformer](https://github.com/sderhy/morseformer).

### Inferences
**Converting SNR definitions**
- e04's average-power SNR in 2500 Hz sits about 3 dB below key-on SNR in 2500 Hz, and about 0.8 dB above key-on SNR in 3 kHz (−3 dB + 10·log10(3000/2500) ≈ +0.8 dB).
- So e04's "−8 dB, below 1.5% CER" is roughly −7 dB key-on SNR in 3 kHz. VE3NEA's model at −6 dB, even in the slowest fading case, shows about 37% CER.
- The difference is too large to be definitional alone. Likely causes are:
  - AWGN versus Rayleigh fading. Even a 0.1 Hz Doppler spread gives deep fades within a record.
  - A larger model (3.6M versus 0.375M parameters).
  - Possibly different CER accounting: VE3NEA's figure excludes spaces.
- Neither result is directly comparable to CW Skimmer's field sensitivity.

**Translating to this project's channel**
- For a ±150 Hz baseband (300 Hz noise bandwidth), SNR in 300 Hz = SNR in 3 kHz + 10 dB.
- VE3NEA's "0 dB key-on in 3 kHz" is therefore about +10 dB key-on SNR in the skimmer's channel bandwidth.

**Where neural decoders help**
- In VE3NEA's data, the neural gains are largest where a classical decoder's model assumptions break: fast QSB (1–3 Hz Doppler) and hand-keyed timing at high SNR, where CW Skimmer is limited by timing and segmentation errors rather than noise.
- At threshold SNR in slow fading, the two are about equal. This suggests the neural model's benefit is mostly robustness to the channel and the operator, not raw weak-signal sensitivity.

**Benchmark implication**
- A fair benchmark against a classical probabilistic decoder should report CER against a stated SNR definition (key-on or average, and noise bandwidth).
- It should sweep Doppler spread and keying style, as VE3NEA did, and include a real-recording set scored for spacing separately from characters.

### Gaps
- No source reports neural-decoder performance with strong overlapping-signal QRM (two stations within tens of Hz), even though morseformer includes QRM in its training generator.
- No controlled neural-versus-fldigi CER-versus-SNR curve was found.
- I found no independent evaluation of e04's model on fading channels, or of VE3NEA's model on real recordings.

---

## 4. Inference cost per channel, model size, and runtimes (ONNX)

### Takeaway
Models range from about 0.375M parameters (VE3NEA's CNN+LSTM, on the order of 10–20 million multiply-accumulates per second of audio) to about 3.6–4M (e04's attention model, morseformer's Conformer).
- The small streaming LSTM class looks feasible for hundreds of channels on a desktop CPU and plausibly for dozens on a Raspberry Pi.
- The attention models run on the order of 10–100× real time per CPU thread on a laptop, i.e. tens of channels per core, with cost growing faster than linearly in window length.
- ONNX Runtime is already the deployment path for e04's model.

### Cited findings
- VE3NEA DeepCW [documented fact] — [VE3NEA/DeepCW model.ipynb](https://github.com/VE3NEA/DeepCW):
  - About 375k parameters; the LSTM accounts for 328,704 of them.
  - A streaming configuration: batch 1, a 4-frame input slice and a stateful LSTM.
  - Training ran at about 80–100 ms per batch on a GPU.
- [inference] Rough compute for VE3NEA's model at 93.75 input frames/s:
  - After two 2× poolings, the LSTM (4·256·(64+256) ≈ 328k multiply-accumulates per step) runs at about 23.4 steps/s, or about 7.7M multiply-accumulates per second (MAC/s).
  - The second conv layer is about 147k MACs per pooled frame at about 47 frames/s, or about 7M MAC/s.
  - Total is roughly 15–20M MAC/s per channel.
  - 300 channels would need about 5–6 GMAC/s. One modern desktop core can do this with SIMD; a Raspberry Pi 4/5 can do a fraction of it.
  - The FFT front end is extra, but it could reuse the skimmer's existing channelizer output.
- e04 deepcw-engine: an ONNX model (15.1 MB), with Python and Node.js ONNX Runtime examples, 3200 Hz input and a 400–1200 Hz spectrogram [documented fact] — [deepcw-engine](https://github.com/e04/deepcw-engine). The web app runs it in-browser; the README mentions ONNX Runtime Web — [CW-LAB README](https://github.com/lucpaysan/CW-LAB).
- [measured here] deepcw-engine `model.onnx`, timed with onnxruntime (CPUExecutionProvider) on one intra-op thread.
  - Setup: an Intel Alder Lake mobile CPU (Family 6 Model 154, 20 logical CPUs) on Windows 11, random inputs, 3 warmups, 15 runs, median.
  - A 10 s window (667 frames) took 97–474 ms, so about 21–102× real time on one thread. The spread likely reflects hybrid P-core/E-core scheduling.
  - A 30 s window (2000 frames) took about 3.2 s, about 9× real time.
  - A batch of 8 × 10 s took about 5.5 s, so batching did not help on one thread.
  - A 3 s window (200 frames) took about 165 ms, about 18× real time.
  - Superlinear growth with window length is consistent with self-attention.
- morseformer: "CPU-real-time at inference," with no GPU needed. It runs under PyTorch, with no ONNX export mentioned and no throughput figures [documented fact] — [morseformer](https://github.com/sderhy/morseformer).
- AG1LE 2020 ran in real time for a single channel in Python/TensorFlow on a 2.2 GHz quad-core MacBook Pro [documented fact] — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).
- MaorAssayag's LSTM decoder has 740k parameters, and its Faster R-CNN detector has 41.3M parameters, far too heavy for per-channel use [documented fact] — [repo](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode).

### Inferences
- **Per-channel feasibility:**
  - With e04-class models on sliding 10 s windows re-run every, say, 1 s for streaming output, cost rises about 10×. That gives roughly 2–10 channels per core on the measured laptop.
  - Hundreds of channels would need a streaming or cached-state design, a much smaller model, or GPU/NPU batching.
- **Recommended candidate for this project:** a VE3NEA-style stateful CNN+LSTM (or GRU), roughly 0.1–0.4M parameters, on a narrow spectrogram crop, exported to ONNX and batched across channels.
  - One batched LSTM step for N channels is a single [N × 320] × [320 × 1024] GEMM (general matrix multiply).
  - This is efficient on CPU and should scale to hundreds of channels on a desktop.
  - A Pi 5 is plausible for tens of channels. This needs measurement.
- **Wideband alternative:** e04's 400–1200 Hz input suggests one wideband model could cover several stations per inference. That conflicts with this project's per-station channel design, and a trained model's behavior on off-center or multiple tones would need validation.
- **Quantization:** int8 via ONNX Runtime would likely give a further 2–4× on CPU. I found no source that reports quantized Morse models.

### Gaps
- No source publishes per-channel throughput, latency, or Raspberry Pi results for any neural Morse decoder.
- My ONNX timing is a single, noisy laptop measurement with random input. It excludes spectrogram computation and CTC decoding, and it does not use a streaming configuration.
- End-to-end decode latency (key-up to character output) is undocumented for every project.

---

## 5. Hybrid approaches (neural front end plus classical decoding, or classical front end plus neural rescoring)

### Takeaway
Most working "neural" decoders are already hybrids in practice. They use a classical DSP front end (bandpass or spectrogram, carrier finding) and increasingly a classical or n-gram back end (greedy or beam CTC decoding, language-model word splitting). A neural network that replaces only the element or character recognizer, feeding a probabilistic timing/text model, has not been published as such.

### Cited findings
- Front ends:
  - morseformer runs a classical DSP front end (complex bandpass at the carrier) before its neural encoder [documented fact] — [morseformer](https://github.com/sderhy/morseformer).
  - AG1LE 2020 finds the CW tone classically (spectrum peak-picking) before neural decoding [documented fact] — [AG1LE 2020](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html).
- Back end: morseformer's default "prose" preset rescores candidate word splits with a character 3-gram language model trained on synthetic amateur text. A neural GPT language model with shallow fusion was tried and disabled because it hurt ham jargon [documented fact] — [morseformer](https://github.com/sderhy/morseformer).
- Two-stage neural systems:
  - MaorAssayag pairs a detector that finds Morse bursts (Faster R-CNN) with a separate LSTM-CTC decoder [documented fact] — [repo](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode).
  - "DeepMorse" (2019) and YFDM (2023) address wideband *detection*, which could act as a neural front end for a classical or neural per-channel decoder [secondhand] [Refined 2026-09-27: YFDM is confirmed as a wideband detector, but its suitability as a front end here is unproven — no SNR-resolved results, no false-alarm rate, no real-signal test, and 72 images/s on a desktop GPU with no CPU figures; adopting it would need GPU/NPU inference and batched images — see ../../wang-yfdm-notes.md §b] — [DeepMorse](https://www.researchgate.net/publication/333793763_DeepMorse_A_Deep_Convolutional_Learning_Method_for_Blind_Morse_Signal_Detection_in_Wideband_Wireless_Spectrum), [YFDM](https://pmc.ncbi.nlm.nih.gov/articles/PMC10667528/).
- CW-LAB ships both a neural CRNN+CTC decoder and a "traditional Bayesian" decoder side by side, advertising the Bayesian one for adaptive speed and low latency. They are alternatives, not fused [documented fact] — [CW-LAB](https://github.com/lucpaysan/CW-LAB).
- e04 includes a separate neural noise-reduction model for listening, which is distinct from the decoder [documented fact] — [e04](https://github.com/e04/web-deep-cw-decoder).
- In the 2018 dataset paper, Dey et al. study classification of *isolated* Morse symbols by small networks [documented fact] — [arXiv 1807.04239](https://arxiv.org/abs/1807.04239). That is the building block for a "neural symbol classifier inside a classical segmenter" hybrid [inference].

### Inferences
Promising hybrid options for a skimmer that already has a classical probabilistic decoder:

1. **Neural per-frame posteriors as emission probabilities.** Use a small streaming network to produce per-frame key-on/off (or element) probabilities in place of the envelope detector's likelihood. The existing probabilistic timing/text decoder (HMM/Bayesian) then does the segmentation. This addresses the fast-fading weakness VE3NEA's data shows for CW Skimmer, while keeping the classical decoder's timing model, which is strong on spacing.
2. **Neural CTC recognizer plus a classical/n-gram word and callsign model.** CTC character posteriors feed a beam search with a ham-specific language model (callsigns, Q-codes, RST/serial exchanges). This is morseformer's direction, and callsign constraints matter most for a skimmer.
3. **Dual-decoder arbitration.** Run the classical decoder everywhere, and invoke the neural decoder only on channels where classical confidence is low or QSB is detected. This caps CPU cost for hundreds of channels.

Whatever the design, benchmark neural and classical decoders on the same generator (VE3NEA's approach) with the SNR convention written down, and also on a real-recording set.

### Gaps
- There is no published quantitative result for any neural-plus-classical fusion (for example, neural emissions in an HMM) for Morse.
- I did not find how CW Skimmer's current algorithm compares with its author's own DeepCW in production, or whether VE3NEA incorporated neural methods into any shipping product. The Morse Expert page states it uses CW Skimmer's algorithms — [Morse Expert](https://ve3nea.github.io/MorseExpert/).
