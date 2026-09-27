# Two neural-Morse documents — verification notes

Read in full from the PDFs in `C:\KZ4APSkimmer-papers\` on 2026-09-27, per the
process in the project's paper-reading rules. Evidence labels follow the
project convention: **[fact, p. N]** = stated in the document at that page;
**[inference]** = reasoning from what the document states; **[uncertain]** =
a real ambiguity in the source. All SNR/dB values name their reference and
bandwidth, or say "not stated" if the source gives none.

---

## 1. "Morse Code Audio Recognition using LSTM-CTC Model" (IEEE, 2023)

### Citation

Arkar Win and Kyawt Kyawt San (University of Information Technology, Yangon,
Myanmar), "Morse Code Audio Recognition using LSTM-CTC Model," *2023 IEEE
Conference on Computer Applications (ICCA)*, pp. 393-398.
DOI: [10.1109/ICCA51723.2023.10181830](https://ieeexplore.ieee.org/document/10181830/).
6 pages. Saved as
`C:\KZ4APSkimmer-papers\Morse_Code_Audio_Recognition_using_LSTM-CTC_Model.pdf`.

### Input features

MFCC (Mel-frequency cepstral coefficients), computed by the standard speech
pipeline: framing/windowing, FFT to get the power spectrum (periodogram), a
Mel-filter bank, log of the filter-bank energies, then DCT to produce the
cepstral coefficients [fact, p. 395, Fig. 2]. The paper states no numeric
MFCC parameters anywhere (no frame length, hop length, number of Mel filters,
or number of cepstral coefficients kept) — all **not stated** [fact, absence
noted after reading the full text]. The paper argues MFCC over a CNN front
end because "the performance of MFCC feature extraction is better than CNN
in audio recognitions" and because CNN is "aim[ed] to classify short
sequences of audio" while "MFCC can able to extract feature from long
audios" [fact, p. 395] — this is asserted, not measured; no ablation is
shown.

### Architecture and size

Pipeline (Fig. 3, p. 395): input `.wav`/`.mp3` file → dataset preparation →
MFCC feature extraction → **one** Bidirectional LSTM layer → Softmax → CTC →
result text [fact, p. 395]. Despite the plural "Bidirectional LSTM layers"
in the running text (p. 395), the architecture diagram shows a single BiLSTM
block, and Table I's "Unit" column (100 or 200) is the only size parameter
given, most plausibly the BiLSTM's hidden-unit count [fact for the table
value, p. 397; inference for calling it hidden units — the paper never
labels it]. No MFCC feature dimension, output-alphabet size, or total
parameter count is given, so a parameter count cannot be computed from the
paper [gap]. This is a much smaller/simpler network than the CNN+LSTM+CTC
architectures already in the project's survey (VE3NEA ~375k parameters,
5-conv-layer AG1LE 2019/2020, MaorAssayag) — it has no convolutional front
end at all, just MFCC → one BiLSTM → Softmax → CTC.

### CTC training

Standard CTC loss on the BiLSTM/Softmax output, decoded with "Keras backend
evaluation of CTC decode functions" (i.e., Keras's built-in CTC decode,
presumably greedy) [fact, p. 396]. Three training configurations were tried,
differing only in epochs, batch size, and unit count (Table I, p. 397):

| Model | Epochs | Batch size | Units | Training loss | Validation loss | Training acc. | Validation acc. |
|---|---|---|---|---|---|---|---|
| 1 | 100 | 10 | 100 | 10 | 7.5 | 10% | 7% |
| 2 | 200 | 5 | 100 | 5 | 20 | 60% | 7% |
| 3 | 350 | 10 | 200 | 1 | 25 | **95%** | **4%** |

[fact, p. 397, Table I; cross-checked against the loss/accuracy curve
figures on p. 397, which match — e.g., Fig. 11's validation-accuracy curve
for model 3 stays under about 0.05, consistent with the table's 4%]. The
paper calls model 3 the best result because of its 95% training accuracy
and presents it as the paper's headline number, but its validation accuracy
(4%) is the *worst* of the three configurations, i.e., **more training made
generalization worse**, a textbook overfitting signature the paper does not
discuss or explain [inference from the table].

### Data and SNR definition

Synthetic only: 2,300 training `.wav` files and 500 validation/test `.wav`
files, generated from an online "English alphabet text to Morse code audio
generator website" (morsecode.world), each file containing one or two
English words [fact, p. 395]. Fixed format: 16 kHz, 16-bit, **5 WPM**
(words per minute) — a single, very slow, beginner-level speed with no
stated speed variation [fact, p. 395]. **SNR is not stated anywhere in the
paper.** There is no noise-injection step described in the preprocessing or
dataset sections, and no SNR value, noise bandwidth, or noise model appears
in the text, figures, or table — the dataset reads as clean synthesized
tone audio [fact, absence noted after reading the full text]. The only
noise-related language is a general motivating remark in the introduction
("when the background noise is high and the signal-to-noise is very low,
the traditional way is difficult") [fact, p. 393], not a description of
what was actually tested.

### Results

No character error rate (CER) or word error rate (WER) is reported. The
conclusion explicitly says WER calculation is future work: "the system is
still in progress on calculation of word error rate and calculation the
performance of same dataset using similar approaches" [fact, p. 397]. The
only reported metrics are the Keras training/validation loss and
classification accuracy in Table I above. Given the 5 WPM/clean-audio/tiny
two-word-sample setup and the overfitting shown by model 3, this paper
provides **no usable CER-vs-SNR evidence** for the project's benchmark.

### Compute cost / limitations

No training or inference time, FLOP count, or hardware is stated anywhere
[gap]. Limitations, from what is and is not in the paper: single fixed
speed (5 WPM only); no noise/SNR modeling; a very small dataset of 1-2-word
clips; no CER/WER metric; the best-training-accuracy configuration
generalizes worst; single BiLSTM layer with no stated feature dimension or
parameter count; greedy (not beam) CTC decode implied but not confirmed.

### Verification against the project's current text

Checked `decoder-survey.md` and every file in
`research_notes\CW decoder algorithms survey\` for statements about this
document (searched "LSTM-CTC", "10181830", "MFCC").

- **`neural_decoders.md`, line 32**: "2023: 'Morse Code Audio Recognition
  using LSTM-CTC Model,' an IEEE conference paper, uses MFCC features (a
  speech-style spectral summary) with an LSTM and CTC [secondhand; abstract
  via search]." **CONFIRMED**, and can be upgraded from [secondhand] to
  [fact] — the paper does use MFCC → (Bi)LSTM → CTC, as described. Minor
  precision gain: it is specifically a *single bidirectional* LSTM layer
  (Fig. 3), not a generic "LSTM," and there is no CNN front end.
- **`neural_decoders.md`, line 62**: "MFCCs [secondhand] — IEEE 10181830."
  **CONFIRMED**, upgrade to [fact].
- **`neural_decoders.md`, line 73 (Gaps)**: "I could not read the full text
  of ... the IEEE 2023 LSTM-CTC paper... Their SNR definitions and figures
  are unverified." **CORRECTED.** The full text has now been read (it is
  short and open-access via a mirrored PDF). There is no SNR definition to
  verify — the paper never defines or reports an SNR at all, and reports no
  CER/WER figures of any kind, only training/validation loss and accuracy
  on clean 5 WPM synthetic audio. This gap can be closed, but the
  corrected content is "this paper contributes nothing to a CER-vs-SNR
  comparison," not a numeric result to add to the project's conversion
  table.
- **`decoder-survey.md`**: **NOT IN DOCUMENT.** No line in `decoder-survey.md`
  cites this specific paper (DOI 10181830) or its authors. The "AG1LE
  CNN-LSTM-CTC" row (line 55) and MaorAssayag row (line 56) in the SNR table
  cite different sources (AG1LE's 2019 blog and the MaorAssayag repo), not
  this IEEE paper, so there is nothing to correct there.

### [inference] Effect on ranking or benchmark plan

No change. This paper is weaker evidence than what the survey already uses
for the same architecture family: no SNR sweep, no CER/WER, a single fixed
5 WPM speed, a tiny two-word-clip synthetic dataset, and a headline result
(95% training accuracy) that is actually the most overfit of its three
configurations (4% validation accuracy, worse than the other two models'
7%). It does not add or contradict any figure in the survey's SNR
conversion table or in the ranked-candidates table, and it gives no
architecture detail (parameter count, MFCC dimension) precise enough to
compare against VE3NEA's DeepCW or morseformer. It is best read as a
confirming data point that the "MFCC/spectrogram → recurrent net → CTC"
family is a natural fit for Morse (consistent with the rest of the
literature already surveyed), not as new quantitative evidence.

---

## 2. "CW Decoding Using Neural Networks" (eHam.net article 28435)

### Citation

Mauri Niininen (call sign **AG1LE**), "CW Decoding Using Neural Networks,"
*eHam.net*, article #28435, posted 2012-07-14.
URL: [https://www.eham.net/article/28435](https://www.eham.net/article/28435).
Saved as `C:\KZ4APSkimmer-papers\CW Decoding Using Neural Networks.pdf`
(a print of the live page, 35 numbered "pages" per the page's own footer
counter — most of that length is a long reader-comment thread, not article
body). Signed "73, Mauri AG1LE" at the end of the body text [fact].

### What it describes

This is **not** the LSTM-CTC/deep-learning family covered elsewhere in the
project's survey, and it is not VE3NEA's DeepCW. It is Mauri Niininen's
(AG1LE's) own **2012 Self-Organizing Map (SOM)** experiment — an
unsupervised clustering method, years before his first LSTM work (which the
project's `neural_decoders.md` chronology already dates to November 2015)
[fact, body text]. The article itself says it is a write-up of "the
experimental version of FLDIGI setup explained in my blog," linking to
`ag1le.blogspot.com/2012/05/fldigi-matched-filter-and-som-decoder.html`
[fact] — the same May-2012 blog post the project's own
`hobbyist_decoders.md` already cites (line 21) for the fldigi SOM/matched-filter
feature. This eHam article is best understood as a companion piece or
republication of that blog-based work, not a separate, independent source.

Method, as described: dit/dah element durations were extracted from text
that fldigi's experimental matched-filter+SOM decoder had already decoded
from noisy audio. A **7×7 SOM** (49 nodes) was trained, using the SOM
Toolbox, to cluster **14 different Morse characters**, each represented as a
7-number vector (up to 6 dit/dah durations in milliseconds, zero-padded,
plus a terminating zero) [fact]. Three visualizations are shown: a hit
histogram with Ward's-linkage clustering, a nearest-neighbor topology graph,
and a U-matrix (average inter-node distance, colored) [fact]. The stated
purpose is explicitly a demonstration, not a decoder evaluation: "to find
out how well a particular neural network algorithm ... would work in
learning Morse code characters purely from noisy data" [fact, author's own
reply to a comment].

The article also mentions, as prior work by the same author, "much larger"
unpublished testing: **almost 40,000 characters** from real CW QSOs, rag-chews
and bulletins, recorded on the 7 MHz, 14 MHz and 18 MHz amateur bands, with
SOM sizes of 20×20, 10×10 and 7×7 tested [fact]. The article says this
larger test was done "last year," which, if the eHam posting date
(2012-07-14) is also roughly the composition date, would place it in 2011 —
**earlier than any AG1LE Morse-ML work currently dated in the project's own
research notes** (which start their AG1LE chronology at the May 2012 blog
posts) [uncertain: the article gives no exact date for this earlier test,
and "last year" is the author's loose phrasing, not a citation].

### Measurements

None in the quantitative sense the project's rules ask for: **no CER, no
WER, no accuracy percentage, and no SNR value** are reported anywhere in
the article [fact, absence noted after reading the full body text]. All
claims are qualitative ("SOM was able to learn... despite noise and jitter,"
"clustered similar Morse code patterns together") [fact]. The article notes
generically that "as the signal-to-noise ratio decreases the noise and
jitter makes it more difficult to produce accurate timing information" and
that "some CW stations don't comply with dit/dah timing standards" [fact],
but gives no numbers for either effect. The lengthy reader-comment thread
that follows the article (roughly 30 of the "35 pages") is off-topic — it
is almost entirely an unrelated argument between commenters about learning
CW versus using decoders, plus one tangential later comment (unrelated
commenter) mentioning "-12.2 dB SNR" wavelet experiments on NCDXF/IARU
beacons — not part of this article and not attributed to AG1LE, so it is
excluded here as out of scope for this document.

### Verification against the project's current text

Checked `decoder-survey.md` and every file in
`research_notes\CW decoder algorithms survey\` for "eHam" and related SOM
material.

- **`neural_decoders.md`, line 75 (Gaps)**: "An eHam article, 'CW Decoding
  Using Neural Networks' (eHam 28435), exists but returned HTTP 403, so its
  content is unknown." **CORRECTED.** The content is now known (summarized
  above): it is AG1LE's July 2012 SOM clustering demonstration, not a new
  or unknown project, and not LSTM/CTC/deep-learning work. It adds no
  CER/SNR figures, so it does not close any of `neural_decoders.md`'s
  quantitative gaps — it only identifies what the document is.
- **`hobbyist_decoders.md`, line 21**: "The SOM and matched-filter features
  were contributed by AG1LE in 2012. His SOM prototype was a 7x7 map trained
  on 14 characters, each represented as a 7-number vector (up to 6 dit/dah
  durations in ms plus a terminating zero), classifying by Best Matching
  Unit..." **CONFIRMED**, now directly from the primary source rather than
  a blog fetch — the eHam article independently states the same 7×7 grid,
  14 characters, 7-number vectors, and BMU classification, matching exactly.
  This line can be upgraded in confidence (it already carried [documented
  fact]).
- **`hobbyist_decoders.md`, line 22**: "The combination of matched filter
  plus SOM was reported to give 'excellent results' in testing by fldigi's
  maintainer Dave W1HKJ (as relayed by AG1LE) [secondhand]." **NOT IN this
  document** — the eHam article does not mention W1HKJ or repeat that
  claim; it neither confirms nor contradicts it. No change.
- **`hobbyist_decoders.md`, line 23-24 and `decoder-survey.md` line 93**
  (AG1LE's qualitative -3 to +6 dB tests, the Jan 2013 quantitative CER-vs-SNR
  test, and the survey's "AG1LE's self-organizing-map (SOM) codebook match"
  sentence): **NOT IN DOCUMENT.** None of those later, dated, quantitative
  tests are in this July 2012 article; it predates them. No correction
  needed — those citations point to different (later) AG1LE posts.
- New information not previously in either file: the ~40,000-character,
  three-band (7/14/18 MHz), multi-SOM-size (20×20/10×10/7×7) follow-on test
  that AG1LE says he ran "last year" (i.e., possibly 2011). This is a
  candidate addition to `hobbyist_decoders.md`'s AG1LE chronology (section
  2), noting it only as a mention with no numbers, and flagging the "last
  year" dating as [uncertain] since it would push AG1LE's earliest Morse-ML
  experimentation about a year earlier than the project's notes currently
  show. This document's own rules restrict this task to producing notes,
  not editing other files, so the addition is left here for that later
  edit.

### [inference] Effect on ranking or benchmark plan

No change. This is a 2012 unsupervised-clustering demonstration with no
CER, WER, or SNR numbers, already effectively represented in the project's
survey through the fldigi SOM-codebook citations it is a companion piece
to. It confirms detail (7×7/14-character/BMU) already in
`hobbyist_decoders.md` rather than adding new quantitative evidence, and it
predates and is far weaker than the CNN+LSTM+CTC and Bayesian/HMM work the
current ranking (dit-matched filter → CNN+LSTM+CTC → Bell-style HMM →
hybrids) is actually based on.
