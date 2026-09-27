# Verification notes: Wang 2018 and YFDM 2023

Two papers read in full (PDF text extracted with `pdftotext -layout`; both are
digitally typeset and extracted cleanly, so no page images were needed).
Labels: **[fact, p. N]** stated in the paper on page N; **[inference]**
reasoning by this note; **[uncertain]** a reading that could not be pinned
down. American spelling throughout.

---

## (a) Wang, Zhao, Ma and Xiong 2018

**Citation.** Xianyu Wang, Qi Zhao, Cheng Ma, Jianping Xiong, "Automatic
Morse Code Recognition Under Low SNR," *2nd International Conference on
Mechanical, Electronic, Control and Automation Engineering* (MECAE 2018),
*Advances in Engineering Research*, vol. 149, pp. 219–224, Atlantis Press,
2018, open access (CC BY-NC 4.0). Department of Precision Instrument,
Tsinghua University, Beijing. No DOI given by the venue; publisher page:
https://www.atlantis-press.com/proceedings/mecae-18/25893679.

### Method

The system is a speech-recognition (ASR) pipeline applied to Morse code, not
a matched filter or an envelope detector [fact, p. 221, Fig. 5: "Preprocessing,
denoising → Feature extraction → Recognition (acoustic model + language
model) → Error correction → Output"]. There is no mention anywhere in the
paper of a matched filter, envelope detection, or an explicit dit/dah
duration model; "signal processing" in the abstract refers to the denoising
and feature-extraction stages of this pipeline, not to anything like the
project's channel filter or envelope decoder.

**Feature extraction** [fact, p. 220], two methods compared:
1. Standard MFCC (Mel-frequency cepstral coefficients): pre-emphasis →
   framing/windowing/FFT → Mel filter bank → log → DCT (Fig. 2).
2. An "improved" feature: the same chain but with pre-emphasis removed and
   the Mel filter bank replaced by a **linear** filter bank of 13 bands
   centered at 1 kHz (Fig. 3–4). The passband is quoted as "800~1200KHz"
   [fact, p. 220] — almost certainly a units error for **800–1200 Hz**, since
   a range in kHz around a 1 kHz center makes no physical sense and the paper
   never otherwise discusses megahertz-scale signals [uncertain; not
   correctable from the text alone].

**Recognition** [fact, p. 221, §2.3], three algorithms measured, all built on
an HMM (hidden Markov model) for time alignment, differing only in what
supplies each HMM state's observation likelihood:
- **Monophone GMM-HMM** — a single Gaussian-mixture model (GMM) per
  state, one acoustic unit ("single-factor").
- **Triphone GMM-HMM** — context-dependent GMM-HMM.
- **DNN-HMM** — a deep neural network supplies the state-emission
  likelihoods in place of the GMM; this hybrid is the paper's "deep
  learning" system, not a separate end-to-end network.

A deep belief network (DBN) is described in the background section [fact,
p. 221, §2.3.3] and Fig. 5 is captioned "Classic DBN network structure," but
no DBN row appears in any results table; whether the DNN-HMM's network was
DBN-pretrained is not stated [uncertain].

**Recognition unit** [fact, pp. 221–222, §3.2, Table 2]: the paper compares
labeling "by dot and dash" against labeling "by word," and finds by-word
recognition far more accurate (word error rate (WER) 35% vs. 110% at 8 dB;
definitions below). All further experiments use by-word labeling. This means
each word is effectively an isolated recognition unit, closer to closed-set
isolated-word ASR than to open-vocabulary, character-by-character decoding of
arbitrary text. The paper never states a vocabulary size or whether test
words appeared in training; given the total dataset is only 2020 clips (next
section), a small closed word set is likely [inference], which would make
even a 1.1% WER much easier to obtain than continuous decoding of arbitrary
ham traffic — the project's actual target.

### Data

Fully synthetic ("produced by software") [fact, p. 221, Table 1], 2020 clips
total, no real recordings and no stated keying-speed range, fading, or
hand-keying variability:

| Split | SNR | Count |
|---|---|---|
| Train | 8 dB | 1860 |
| Test | 8 dB | 80 |
| Testn | −3 dB | 80 |

**How "SNR" is defined: not stated.** No bandwidth, and no statement of
key-on vs. average power, appears anywhere in the paper. "8 dB" and "−3 dB"
cannot be converted to this project's S₅₀₀ convention (key-down carrier over
noise in 500 Hz; `docs/signal-processing.md` §5, §11) without knowing that
bandwidth. **What "low SNR" means numerically here** is exactly these two
values: 8 dB is the trained/nominal condition, −3 dB is the one out-of-
distribution stress test. There is no sweep and no lower bound beyond −3 dB.

**Metric.** WER = Levenshtein edit distance (insertions + deletions +
substitutions) over reference length, at the labeling unit in use. Confirmed
by the text itself: the "by dot and dash" condition reports WER **above
100%** (110%, 143%) "due to a lot of insertion errors" [fact, p. 221–222],
which only makes sense for an edit-distance rate, not a bounded accuracy.

### Results (Tables 2–5, pp. 222)

| Experiment | Condition | WER (8 dB) | WER (−3 dB) |
|---|---|---|---|
| Labeling unit | by dot and dash | 110% | 143% |
| Labeling unit | by word | 35% | 56% |
| HMM state count (monophone GMM-HMM, by word) | 4 states | 40% | — |
| | 5 states | 37% | — |
| | **6 states (best)** | **35%** | — |
| | 7 states | 56% (overfit) | — |
| Feature (6-state monophone GMM-HMM) | MFCC | 35% | 56% |
| | **Improved linear-filter feature** | **12%** | **22%** |
| Recognition algorithm (improved feature) | Monophone GMM-HMM | 12% | 22% |
| | Triphone GMM-HMM | **3.4%** | 26% |
| | **DNN-HMM (best overall)** | **1.1%** | **18%** |

Two things worth flagging for the record [inference]:
- Triphone GMM-HMM is much better than monophone at 8 dB (3.4% vs. 12%) but
  *worse* than monophone at −3 dB (26% vs. 22%) — a sign of overfitting to
  the training SNR that the authors do not discuss.
- The DNN-HMM's advantage over GMM-HMM shrinks sharply off the training SNR:
  about 11× relative WER reduction at 8 dB (12% → 1.1%) but only about 20%
  relative reduction at −3 dB (22% → 18%). Nothing in the paper says whether
  the −3 dB test used a model retrained at −3 dB or just the 8 dB model
  evaluated on lower-SNR audio; the latter is more consistent with "Testn"
  (test-new) as a name and with Table 1 listing no −3 dB training split
  [uncertain].

**Compute cost: not reported.** No parameter counts, FLOPs, or timing figures
for the DNN, DBN or GMM-HMM systems appear anywhere in the paper.

### Limitations (this note's assessment) [inference]

- Entirely synthetic data, 2020 clips total — small even for 2018.
- SNR bandwidth and convention undefined; the 8 dB/−3 dB numbers are not
  comparable to any other source in the project's survey.
- Vocabulary size and open- vs. closed-set testing are unstated; likely a
  small closed word set given the dataset size and the "by word" unit choice.
- No comparison to a matched-filter or threshold/envelope baseline — the
  "classical" comparison point here is GMM-HMM, not anything resembling the
  project's classical decoder (`docs/signal-processing.md` §8).
- No compute or latency numbers, so real-time per-channel feasibility cannot
  be assessed.

### Verification against the project's current text

**`research_notes/CW decoder algorithms survey/classical_literature.md`, line
105** ("Search snippets say it combines signal processing with deep
learning, and mention HMM and GMM ... as common audio-recognition baselines
and training at 8 dB. The PDF did not extract...") — **CORRECTED.**
- "Combines signal processing with deep learning": **CONFIRMED** [fact,
  p. 219, abstract].
- "Training at 8 dB": **CONFIRMED** [fact, p. 221, Table 1].
- "HMM and GMM ... as common audio-recognition baselines" understates their
  role. **Correct statement:** every recognizer tested is an HMM; GMM
  (monophone and triphone) and a DNN are the two kinds of per-state
  observation model compared head-to-head as the paper's own experiments
  (Table 5), not background-cited baselines from elsewhere. The paper's
  "deep learning" *is* the DNN-HMM hybrid — not a CNN-LSTM-CTC network like
  the other entries surveyed in `neural_decoders.md`.

**`neural_decoders.md`, line 22** ("combines signal processing with deep
learning [secondhand; the PDF would not parse]") — **CONFIRMED**, and the
"PDF would not parse" caveat no longer applies; drop it. Add: the "deep
learning" is specifically a DNN-HMM hybrid, and results are WER, not
accuracy.

**`neural_decoders.md`, line 73** (Gaps: "I could not read the full text of
the 2018 Atlantis Press paper... Their SNR definitions and figures are
unverified.") — **RESOLVED.** The SNR definition is confirmed to be **not
stated** in the paper itself (not merely unread); the figures above are the
verified numbers.

### [inference] Effect on the decoder ranking

**No change.** Wang 2018 is a small, fully synthetic, likely closed-vocabulary
ASR-style word recognizer (GMM/DNN-HMM) with an undefined SNR convention and
no compute-cost data. It supplies no evidence comparable to the project's
SNR-vs-CER table (`decoder-survey.md`, "One SNR yardstick") and no basis for
choosing among the dit-matched filter, CNN+LSTM+CTC, or Bell-style HMM
candidates. Its one methodological lesson: a low WER at a single labeling
granularity (whole word, likely closed vocabulary) is not evidence about
open-vocabulary, continuous character decoding, which is what this project
needs.

---

## (b) Wei, Li and Han 2023 (YFDM)

**Citation.** Zhenhua Wei, Zijun Li, Siming Han, "YFDM: YOLO for detecting
Morse code," *Scientific Reports* 13, 20614 (2023).
https://doi.org/10.1038/s41598-023-48030-7. Open access (CC BY 4.0). Academy
of Operational Support, Rocket Force Engineering University, Xi'an, China.
PMC10667528. Received 5 Sep 2023, accepted 21 Nov 2023.

### Method

**Task: detection, not decoding.** The output is a set of bounding boxes
(frequency × time extent) around Morse-code bursts in a spectrogram image —
the same *kind* of job as the project's spectrum analyzer + signal detector
(`docs/signal-processing.md` §4, §6), not a character decoder.

**Input** [fact, p. 3, p. 6]: an RGB short-time-Fourier-transform (STFT)
spectrogram image, resized to 640×640 pixels, of a real bandwidth of
**2 MHz**, with the Morse carrier's center frequency placed anywhere in
**5–12 MHz** and each labeled burst lasting **3–8 s**. That is roughly
3 kHz per pixel column in frequency — about 130× coarser than this project's
23.4 Hz FFT bin (`docs/signal-processing.md` §2) — and a per-image time span
(3–8 s) far longer than the project's 21.3 ms processing hop.

**Model (YFDM = "YOLO for Detecting Morse")** [fact, pp. 2–6]: a modified
YOLOv5s (a one-stage CNN object detector), chosen over Faster-RCNN, SSD and
YOLOv8s (Table 5, p. 8) for its much smaller memory footprint at comparable
accuracy. Three changes from stock YOLOv5s:
1. **Backbone:** standard convolutions replaced by deformable convolution v2
   (DCNv2) combined with C3 blocks, meant to track Morse code's irregular
   geometry in the image (frequency offset, speed variation) better than a
   fixed convolution kernel (eq. 1–2, p. 3–4).
2. **Neck:** the FPN+PAN multi-scale fusion structure rebuilt with
   lightweight GSConv and VoV-GSCSP modules to cut parameters and
   floating-point operations (eq. 3–4, p. 4–5).
3. **Post-processing:** standard non-max suppression (NMS) replaced by
   "Confidence Propagation Cluster" (CP-Cluster), which passes messages
   between overlapping candidate boxes (by IoU, intersection over union) to
   boost or suppress each box's confidence before final selection (eq. 5–10,
   p. 5–6), because NMS "is more sensitive to the IoU threshold" and is not
   guaranteed to keep the best box.
4. **Loss:** five IoU-based bounding-box losses compared (CIoU, DIoU, EIoU,
   SIoU, WIoUv1); WIoUv1 chosen as best (Table 9, p. 10).

### Data

Entirely synthetic [fact, p. 6, "Datasets"], no real Morse or real receiver
noise:
- Broadband candidate signals generated by simulation, converted to
  time-frequency images by STFT.
- Background clutter deliberately includes **other modulation types**: FSK,
  PSK and AM-modulated signals, plus actually-recorded human voice and music
  placed near the Morse signals, plus white-noise interference — a busier,
  more heterogeneous spectral environment than this project's AWGN-only
  synthetic benchmark.
- Simulated frequency drift and code-rate (speed) fluctuation.
- **"−5 to 0 dB random white noise" added to the background** [fact, p. 6].
  This is the paper's only numeric SNR statement, offered as the "low SNR"
  condition (their Fig. 5(d) example).
- Training set: 2072 images; validation set: 350 images; every image has at
  least one Morse signal.

**How "SNR" is defined: not stated.** No bandwidth is given, no statement of
key-on vs. average power, and no statement of what the noise is referenced
against (whole-image power, per-signal band, or something else). "−5 to
0 dB" cannot be related to this project's S₅₀₀ convention. There is also no
result broken out by SNR: despite training across a 5 dB range, no plot or
table gives detection performance as a function of SNR — every reported
number below is an aggregate over the whole (mixed-SNR, mixed-interferer)
validation set.

### Evaluation metrics and compute setup

Metrics [fact, p. 7]: Precision, Recall, AP0.5 (average precision at
IoU ≥ 0.5), AP0.5:0.95 (COCO-style, averaged over IoU 0.5–0.95), an F2 score
(β = 2, weighting recall over precision, "because missing detection is more
serious than error detection" for a front end feeding later decoding),
parameter count, GFLOPs (giga floating-point operations per second — as
printed in the paper; this is really a per-inference operation count, GFLOP,
not a rate) and FPS (frames per second, full image-in to boxes-out latency).

Hardware [fact, p. 7]: Windows 10, Python 3.8, PyTorch 1.10.0, CUDA 11.3, one
**NVIDIA RTX 3080Ti GPU**; 100 epochs, batch size 16, 640×640 images. Every
FPS number in the paper is GPU throughput on this desktop card, not CPU or
embedded-device throughput. The paper's claim that the smaller model
"provides favorable conditions for deployment on the mobile device side"
(conclusion, p. 9) is not backed by any actual mobile/CPU/embedded
benchmark — it is aspirational [uncertain — presented as fact by the paper,
but unmeasured].

### Results

**Model choice** (Table 5, p. 8): SSD AP0.5 = 0.936, AP0.5:0.95 = 0.584
(worst; "prone to incomplete detection" of long, narrow Morse marks);
Faster-RCNN AP0.5 = 0.994, AP0.5:0.95 = 0.667, 314.2 MB (best accuracy, too
slow/heavy); YOLOv5s AP0.5 = 0.993, AP0.5:0.95 = 0.665, 13.7 MB; YOLOv8s
AP0.5 = 0.992, AP0.5:0.95 = 0.673, 85.4 MB. YOLOv5s was chosen as the base
model for its much smaller footprint at near-equal accuracy and about 9%
higher FPS than YOLOv8s.

**Ablation** (Table 6, p. 8):

| Configuration | Precision | Recall | F2 | AP0.5:0.95 | Params (M) | GFLOPs |
|---|---|---|---|---|---|---|
| YOLOv5s (baseline) | 0.992 | 0.987 | 0.988 | 0.665 | 7.022 | 15.94 |
| + DCNv2/C3 backbone | 0.993 | 0.993 | 0.993 | 0.670 | 7.138 | 12.88 |
| + GSConv/VoV-GSCSP neck | 0.991 | 0.994 | 0.993 | 0.664 | 5.845 | 12.80 |
| **YFDM (both, = full model)** | 0.989 | 0.995 | 0.994 | 0.670 | **5.961** | **9.74** |

Relative to YOLOv5s, the full model has **15.1% fewer parameters and 38.9%
fewer GFLOPs** [fact, p. 1 abstract and p. 8, matching table arithmetic], the
smallest of every YOLO version they tabulate (v3, v4, v5s, v6s, v7, v8s;
Table 8, p. 9).

**Loss and post-processing** (Table 9, p. 10; CP-Cluster section, p. 11):
with WIoUv1 loss and plain NMS, AP0.5:0.95 = 0.673 at FPS ≈ 104. Replacing
NMS with CP-Cluster raises **AP0.5:0.95 to 0.68** (the headline number, a
further +2.26% over the 0.665 ablation baseline) but drops **FPS to 72.4**
(abstract) / 71.76 (body text, p. 11) — roughly a 30% throughput cost for
that accuracy gain, all on the RTX 3080Ti.

**False alarms: not reported as a rate.** Only Precision (≈ 0.989–0.995
across the configurations above) and Recall (≈ 0.987–0.995) on the synthetic
validation set are given. These bound the false-positive/false-negative
*fraction* on that dataset but do not give a per-hour, per-MHz, or
per-image false-alarm rate usable for comparison with a running skimmer.

### Verification against the project's current text

**`neural_decoders.md`, line 37** ("applies YOLO object detection to Morse
in spectrograms. This is detection, not decoding [secondhand; the page was
behind a CAPTCHA, so title only]") — **CONFIRMED**, and can be upgraded from
[secondhand] to [fact]: it is a modified YOLOv5s (DCNv2/C3 backbone,
GSConv/VoV-GSCSP neck, CP-Cluster post-processing, WIoUv1 loss) applied to
STFT spectrogram images, and the paper explicitly frames this as detection
only.

**`neural_decoders.md`, line 73** (Gaps: "...the YFDM paper (CAPTCHA). Their
SNR definitions and figures are unverified.") — **RESOLVED.** SNR is
confirmed **not stated** beyond the bare "−5 to 0 dB" range with no
bandwidth or convention; the figures above are the verified numbers.

**`neural_decoders.md`, line 297** ("'DeepMorse' (2019) and YFDM (2023)
address wideband detection, which could act as a neural front end for a
classical or neural per-channel decoder [secondhand]") — **CORRECTED (add
caveat, do not overturn).** YFDM is now confirmed [fact] to do wideband
CNN-based detection, but the paper gives no evidence that it would be a
good — or even workable — front end for *this* project specifically: no
SNR-vs-detection curve, no false-alarm rate, no real-signal test, and GPU-
bound throughput (72 FPS on an RTX 3080Ti for one 2 MHz/640×640 image) with
no CPU or embedded numbers at all. The recommended wording: "could in
principle act as a neural front end, but this is unproven for a real-time,
CPU-based, narrowband-per-station design; adopting it would require a
GPU/NPU inference path and a batch-image rather than streaming-per-hop
architecture."

### [inference] Relevance as a detector front end, vs. `docs/signal-processing.md` §6

- **Different problem, different scale.** YFDM detects Morse bursts among
  *other modulation types* (FSK/PSK/AM/voice/music) across a 2 MHz HF span
  at ~3 kHz/pixel and 3–8 s per detection; the project's detector finds CW
  carriers in up to ±96 kHz at 23.4 Hz/bin, updating every 21.3 ms hop, with
  no non-CW interferers in its current synthetic benchmark. YFDM solves a
  harder classification problem (CW vs. other signal types) at far coarser
  frequency/time resolution than the project needs.
- **Compute.** The project's detector is one shared FFT per hop (already
  computed for the spectrum display) plus O(N) median/peak-picking on a CPU,
  serving up to 200 simultaneous tracks in real time. YFDM needs a full CNN
  forward pass per spectrogram image on a desktop GPU to reach 72 FPS.
  Adopting anything like it as a per-hop front end would be an architecture
  change (CPU → GPU/NPU, streaming → batched images), not a drop-in swap,
  and nothing in this paper justifies that cost.
- **No SNR-resolved or false-alarm-rate result** exists to compare against
  the project's own 6 dB/3 dB median-threshold detector
  (`docs/signal-processing.md` §6, "New tracks"/"Existing tracks") at any
  matched operating point.
- **Entirely synthetic training and evaluation**, same caveat as Wang 2018:
  no real HF recordings, no real receiver noise floor or band roll-off, only
  simulated drift and speed variation.

**Bottom line [inference]:** this paper neither confirms nor refutes the
survey's speculation that a wideband CNN detector could serve as a neural
front end; it only establishes that such detectors exist and can be made
smaller than stock YOLO variants. It does not change the decoder ranking
(it is not a decoder), and it does not currently justify replacing or
augmenting the project's signal detector — a GPU-bound, 2 MHz/frame batch
detector solving a different classification problem is not evidently a
better fit than the existing CPU, per-hop, per-bin detector for this
project's ±150 Hz-per-station design.
