# Hobbyist and Open-Source CW (Morse) Decoder Algorithms

Labels used on every claim: **[documented fact]** = stated in the project's own source code, README, or author's blog; **[secondhand]** = reported by a third party (reviewer, search summary, aggregator); **[inference]** = my reasoning from cited material.

Caveat on SNR numbers: the sources below define SNR in different noise bandwidths (2 kHz, 2.5 kHz, 3 kHz), so their dB figures are not directly comparable without adjustment (see Inferences in section 4).

## 1. fldigi's CW decoder: signal chain, speed tracking, SOM matching, strengths and weaknesses

### Takeaway
fldigi's CW receiver is a classic hard-decision chain: FFT low-pass filter (optionally a speed-matched bandwidth), magnitude envelope smoothed by a moving average, AGC/noise-floor normalization, a two-threshold hysteresis slicer, a single "two dots" timing reference updated by a moving average for speed, a dit/dah split at that reference, and a table lookup (optionally replaced by a SOM nearest-codebook match). It is cheap and simple but degrades sharply below about -10 dB SNR (3 kHz reference), with CER going from about 1% at -10 dB to about 50% at -15 dB in AG1LE's test.

### Cited Findings
- [documented fact] Incoming complex samples are low-pass filtered by an FFT filter (`cw_FFT_filter`, FFT size `CW_FFT_SIZE` = 2048); when the "matched filter" option is on, the bandwidth is set to 5 x CW speed / 1.2 (Hz), otherwise to the user's `CWbandwidth` setting. — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] After filtering, samples are decimated (`DEC_RATIO`) and the magnitude is smoothed by a moving-average `bitfilter` whose length is `symbollen / (2 * DEC_RATIO)`, i.e., about half a dot; this is the envelope detector (no explicit peak detector). — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] `decode_stream()` tracks a signal average (`sig_avg`, via `decayavg()` with roughly 200 ms attack / 1000 ms decay), a separate `noise_floor`, and an `agc_peak`; the envelope is normalized by `agc_peak`. A signal metric is computed as 2.5 x 20log10(sig_avg / noise_floor) and gates decoding when the squelch is on. — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] Tone on/off detection uses hysteresis with two adaptive thresholds: upper = norm_sig - 0.2 x diff and lower = noise_floor + 0.7 x diff; state goes to "in tone" above the upper threshold and "after tone" below the lower one. — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] Speed tracking (`update_tracking()`): only when adjacent marks form a dot-dash or dash-dot pair with a length ratio in the 2-4x window, the mean of the two durations is fed into a moving-average `trackingfilter` (size `TRACKING_FILTER_SIZE`) to produce `two_dots`; receive WPM = KWPM / (two_dots / 2). — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] Element classification is a single split: a mark no longer than `two_dots` is a dit, longer is a dah; marks shorter than a noise-spike threshold (half the current dot length) are discarded. — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] Characters are decoded by building a dit/dah string and looking it up (`morse->rx_lookup`). If `CWuseSOMdecoding` is enabled, `find_winner()` instead normalizes the element durations and picks the nearest entry in a codebook (`som_table`) by Euclidean distance. — [fldigi source, cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx)
- [documented fact] Changing the transmit WPM resets the receive WPM tracker to that speed, which the manual presents as a way to force the decoder into a new speed range. — [fldigi CW help page](https://www.w1hkj.org/FldigiHelp/cw_page.html)
- [documented fact] The SOM and matched-filter features were contributed by AG1LE in 2012. His SOM prototype was a 7x7 map trained on 14 characters, each represented as a 7-number vector (up to 6 dit/dah durations in ms plus a terminating zero), classifying by Best Matching Unit so that noisy/jittered timing still maps to the right codebook entry. — [AG1LE: Morse Code decoding with Self Organizing Maps](http://ag1le.blogspot.com/2012/05/morse-code-decoding-with-self.html); [AG1LE: Matched Filter and SOM decoder](http://ag1le.blogspot.com/2012/05/fldigi-matched-filter-and-som-decoder.html)
- [secondhand] The combination of matched filter plus SOM was reported to give "excellent results" in testing by fldigi's maintainer Dave W1HKJ (as relayed by AG1LE). — [AG1LE blog search summary / May 2012 posts](http://ag1le.blogspot.com/2012/05/fldigi-matched-filter-and-som-decoder.html)
- [documented fact] AG1LE's qualitative tests on -3 to +6 dB recordings: legacy decoder produced errors at -3 to 0 dB and improved from about +1 dB; the SOM always returns a best-matching character, so in noise (-3/-2 dB) most of its output was "garbage." Experimental builds crashed after 2-3 replays, so no statistics were collected. — [AG1LE: more test results on experimental features](http://ag1le.blogspot.com/2012/05/fldigi-more-test-results-on.html)
- [documented fact] Quantitative test (Jan 2013): 20 WPM machine-generated text (WinMorse), 8 kHz mono, 11 min 27 s, AWGN added with PathSim in a 3 kHz bandwidth. Legacy decoder with matched filter (35 Hz) CER: 1.2% at -10 dB, 19.5% at -13 dB, 37.5% at -14 dB, 50.7% at -15 dB, 74.2% at -20 dB. The post also compared FFT filters at 35 Hz and 68 Hz and the SOM decoder. — [AG1LE: Morse Decoder SNR vs CER Testing](http://ag1le.blogspot.com/2013/01/morse-decoder-snr-vs-cer-testing.html)
- [documented fact] AG1LE reported that on a real noisy bulletin the legacy decoder "made many errors" while his Bayesian decoder produced readable copy. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

### Inferences
- [inference] The fldigi chain makes hard decisions at every stage (threshold slicer, single dit/dah boundary, exact table lookup), so one flipped element or a split/merged mark corrupts a whole character; this is the structural reason for the steep CER cliff between -10 and -15 dB.
- [inference] Speed tracking only updates on dot-dash adjacent pairs within a 2-4x ratio, so it is robust against isolated noise but slow to follow abrupt speed changes and can be misled by fists whose dah:dit ratio is outside 2-4.
- [inference] Hand-keying variance beyond the single `two_dots` boundary (e.g., long dits, short dahs, uneven spacing) is handled only by the SOM option; the SOM helps timing jitter but not detection errors.
- [inference] QSB handling relies on the ~1 s decay AGC and noise-floor tracker; a deep fade longer than that would lower thresholds and let noise through, or drop marks.
- [inference] Per-channel cost is tiny (one FFT filter, a moving average, a few scalar updates per decimated sample), which is why fldigi can run it in its multi-channel "signal browser" view.

### Gaps
- I did not find a published fldigi CER-vs-SNR curve for the SOM decoder or for the 35/68 Hz FFT-filter configurations with exact numbers (the 2013 post compared them, but the fetch returned only the matched-filter row).
- No fldigi mailing-list discussion of known weaknesses was retrieved.
- Exact values of `TRACKING_FILTER_SIZE`, `DEC_RATIO`, and KWPM were not extracted.

## 2. Mauri Niininen AG1LE's work: Bayesian (Bell) decoder, Kalman filtering, neural-network experiments

### Takeaway
AG1LE progressed through (a) matched filter + SOM for fldigi (2012), (b) a simple per-element Bayesian classifier (2013), (c) a port of E. L. Bell's 1977 Fortran Bayesian/Kalman "correlator-estimator" decoder into fldigi with multi-channel spin-up (2013-2014), and (d) deep learning, ending with a CNN-LSTM-CTC model on spectrogram images (2019-2020). The Bell port showed large CER reductions above about 6 dB SNR (2 kHz reference) but had an unexplained ~3% error floor and alpha-quality stability; the neural models reported low CER on synthetic data with a cliff near -12 dB (in a noise bandwidth not stated) but were trained on narrow, synthetic speed ranges.

### Cited Findings
**Simple Bayesian classifier (Jan 2013)**
- [documented fact] Prompted by advice from Alex VE3NEA (author of CW Skimmer) to work "in the Bayesian framework," i.e., compute probabilities instead of hard decisions. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)
- [documented fact] Architecture: an edge recorder collects key-down/key-up durations into histograms; a Bayesian classifier computes P(dit|duration) with P(dit) = P(dah) = 0.5 (dit likelihood region 0.5-2 dit lengths, dah 2-4); a character classifier multiplies the per-element probability vectors against a codebook; a word classifier matched against a corpus was proposed but not built. It cites D. L. Mills' "Real-time recognition of manual Morse" for element prior probabilities. No CER numbers given. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

**Bell 1977 decoder port (2013-2014)**
- [documented fact] The decoder was originally written by Dr. E. L. Bell in 1977; AG1LE hand-entered it from Fortran listings in Bell's doctoral thesis and converted it to C/C++ (about 3,335 lines of C), releasing it with Bell's verbal permission in the `morse-wip` repository. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html); [ag1le/morse-wip](https://github.com/ag1le/morse-wip)
- [documented fact] AG1LE describes the thesis as covering the math of transcribing hand-keyed Morse, with tests against theoretically optimal results and human performance. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- [secondhand] The "MIT" attribution in the assignment was not confirmed by the sources I fetched; AG1LE's posts name Bell and 1977 but the fetched text did not name the institution or thesis title. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- [documented fact] Method: a correlator-estimator in which all possible keystate transition sequences are hypothesized, correlated with the incoming signal, and the most likely sequence is output. Six keystates are modeled: dit, dah, element space, character space, word space, and pause. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- [documented fact] Code modules: `kalfil.c` (Kalman filters estimating the likelihood of each keystate from the observed signal), `noise.c` (noise estimate), `probp.c` (Bayesian update of posterior probabilities across decoding paths), `spdtr.c` (speed tracking, limited to 8-80 WPM). — [AG1LE: New Morse Decoder Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html)
- [documented fact] CER vs SNR was tested from -10 to +20 dB SNR (2 kHz bandwidth); AG1LE reports a "deep reduction in CER" with SNR above 6 dB, and an unexplained ~3% base error rate at high SNR. He listed remaining work: optimization, testing under timing/speed variance and fading, and parameter tuning. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html); [Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html)
- [documented fact] Compared to the legacy/SOM decoder on real noisy signals, AG1LE reported "significant accuracy improvement," but the alpha software crashed occasionally and the noise estimator was unreliable. — [AG1LE: New Morse Decoder Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html)
- [documented fact] Multi-channel version (fldigi 3.21.83cw-a4 alpha): detects spectral peaks and starts a new Bayesian decoder instance per detected signal frequency (channels rounded to 100 Hz; peaks within 20 Hz merged to reduce splatter); a demo showed 9 CW signals decoded between 400 and 1200 Hz with a threshold slider in fldigi's Signal Browser. — [AG1LE: New Morse Decoder Part 6](http://ag1le.blogspot.com/2014/07/new-morse-decoder-part-6.html); [AG1LE blog July 2014](https://ag1le.blogspot.com/2014/07/)
- [documented fact] Known problems in that release: no speed-dependent (matched) prefilter, no averaging of frequency estimates (mistuning), poor accuracy when multiple stations at different speeds are present, and a speed estimator that can get "stuck" at the extremes. No CER or CPU numbers were published for it. — [AG1LE: New Morse Decoder Part 6](http://ag1le.blogspot.com/2014/07/new-morse-decoder-part-6.html)
- [secondhand] PE4BAS described the resulting build as an "ultimate CW decoder" (user report). — [PE4BAS blog](https://pe4bas.blogspot.com/2014/08/ultimate-cw-decoder.html)

**Neural networks (2015-2020)**
- [documented fact] 2015 experiment: an LSTM (300 hidden units, dropout, linear dense output) trained to mark dits on a synthetic 40 WPM waveform of the word "QUICK" with noise and QSB (1,950 samples, 8 dit examples). It fit the training signal (loss 0.0164 after 100 epochs) but failed on the reversed word "KCIUQ" (overfitting); AG1LE concluded millions of data points would be needed. — [AG1LE: Deep Learning algorithm for Morse decoder using LSTM RNN](http://ag1le.blogspot.com/2015/11/experiment-deep-learning-algorithm-for.html)
- [documented fact] 2019 CNN-LSTM-CTC (TensorFlow): synthetic audio, 8 kHz, Gaussian noise at SNRs from -22 to +30 dB, 5,000 samples per SNR level, 95/5 train/validation split, random "words" of at most 5 characters, only 25 and 30 WPM. Decoding was good down to about -12 dB SNR and fell off "fairly dramatically" below. Training took 15-45 minutes on a 2.2 GHz i7 MacBook Pro. Train and validation shared SNR levels; no comparison to fldigi. — [AG1LE: Performance characteristics of the ML Morse Decoder](http://ag1le.blogspot.com/2019/02/performance-characteristics-of-ml-morse.html)
- [secondhand] An earlier run (5.2 h audio, 5,000 files) reported 0.1% CER, 99.5% word accuracy, and decoding "relatively accurately" to -3 dB SNR. — [AG1LE: Training a Computer to Listen and Decode Morse Code](http://ag1le.blogspot.com/2019/02/training-computer-to-listen-and-decode.html) (via search summary)
- [documented fact] 2020 real-time version: audio to spectrogram (8 kHz, 256-point FFT, overlapping), auto-detects the tone by spectral peak, crops 32 Hz of bandwidth around it, and feeds 128x32 images to the CNN-LSTM-CTC model. Trained on 27.8 h (25,000 clips of 4 s) derived from ARRL practice files; reported 1.5% CER and 97.2% word accuracy after about 2.5 h training on an i7 MacBook Pro. — [AG1LE: New real-time deep learning Morse decoder](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html); code: [ag1le/deepmorse-decoder](https://github.com/ag1le/deepmorse-decoder)
- [documented fact] Weaknesses he listed: characters cut at frame boundaries are misread (fixed sliding windows with no character alignment), poor number recognition (numbers ~8.6% of the corpus), no word spaces learned, and a non-event-driven design that does not scale. Live 30 WPM decoding showed per-character probabilities ranging from 4% to over 90%. — [AG1LE: New real-time deep learning Morse decoder](https://ag1le.blogspot.com/2020/04/new-real-time-deep-learning-morse.html)

### Inferences
- [inference] Bell's decoder is effectively a hidden-Markov/multiple-hypothesis tracker: a discrete keystate + speed state space, a Kalman filter per hypothesis for amplitude (which is how it handles QSB), and Bayesian path probabilities with pruning. That structure naturally handles poor fists (duration likelihoods, not fixed thresholds) and speed drift (speed is a state), which is why it is the most relevant prior art for a probabilistic skimmer.
- [inference] The ~3% floor at high SNR and the "stuck speed" behavior suggest implementation or parameter issues (priors, speed transition model, noise estimator) rather than a limit of the method; this is a risk to budget for if porting Bell's code.
- [inference] Running a full hypothesis bank per channel is much more expensive than fldigi's slicer; AG1LE published no CPU figures, so per-channel cost must be measured.
- [inference] The neural results are on synthetic, machine-timed, narrow-speed data with the same SNR distribution in training and validation; they say little about poor fists, QRM, or real QSB.

### Gaps
- Bell's thesis title and institution, the number of hypotheses/paths retained, and the Kalman state definitions were not confirmed from primary text (the thesis itself was not retrieved).
- No exact CER-vs-SNR table for the Bell port was found in fetched pages (only the qualitative "deep reduction above 6 dB" and 3% floor).
- No CPU/latency measurement for the Bell decoder or the multi-channel build.
- The noise bandwidth used for the 2019 neural SNR figures was not stated in what I retrieved.

## 3. Other notable decoders (brief)

### Takeaway
Most hobbyist decoders (CwGet, MRP40, Arduino/Goertzel, simple Python tools) are threshold-plus-timing designs with proprietary or undocumented details; the interesting algorithmic outliers are RSCW (maximum-likelihood sequence search for machine-timed code), ggmorse (brute-force search over speed and threshold with a timing-fit cost), and recent deep-learning decoders, of which DeepCW (e04) publishes the strongest numeric claims (under 1.5% CER at -8 dB, under 8% at -10 dB in a 2.5 kHz bandwidth).

### Cited Findings
**CW Skimmer (for reference; closed source)**
- [secondhand] Uses a Bayesian-statistics decoding algorithm that decodes all CW signals in the passband simultaneously; reported up to 700 simultaneous signals on a 3 GHz Pentium 4 with a wideband receiver. VE3NEA said he spent eight years and tried hundreds of algorithms. — [Wikipedia: CW Skimmer](https://en.wikipedia.org/wiki/CW_Skimmer)
- [documented fact] VE3NEA's stated principle (quoted by AG1LE): express prior knowledge as probabilities and update them from data; compute the probability that the signal is present rather than making a hard decision per sample. — [AG1LE: Towards Bayesian Morse Decoder](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

**ggmorse (Georgi Gerganov, open source C++)**
- [documented fact] Automatic pitch detection 200-1200 Hz and automatic speed detection 5-55 WPM, real time from microphone audio; the author calls the implementation not very user-friendly. — [ggmorse README](https://github.com/ggerganov/ggmorse)
- [documented fact] Tone power is measured with a running Goertzel filter (recomputed if the pitch estimate moves more than 50 Hz); pitch is auto-detected from a short-time FFT. — [ggmorse source](https://raw.githubusercontent.com/ggerganov/ggmorse/master/src/ggmorse.cpp)
- [documented fact] Speed and threshold are found by exhaustive search: for each WPM candidate and each threshold level (10%-90% of mean power steps), the signal is sliced, intervals are fit to 1/3/7-unit timing, and a cost (average fit error for dots, dahs, spaces, with a penalty if the dah:dit ratio is outside 2.5-3.5) is scored; the best combination wins. Marks under 2 dot lengths are dots; letters come from a lookup table, '?' if unmatched. — [ggmorse source](https://raw.githubusercontent.com/ggerganov/ggmorse/master/src/ggmorse.cpp)

**RSCW (PA3FWM)**
- [documented fact] Designed for machine-sent code with perfect timing (e.g., beacons): I/Q mix to baseband, moving-average low-pass (48 samples, 8 kHz to 1 kHz), a second moving average of one bit length, and magnitude. Bit clock is recovered by exploiting that odd bits are mostly 1 and even bits mostly 0 and fitting a sine. Decoding searches message hypotheses by cross-correlation, keeping only the best hypothesis ending in an inter-character space, since all such hypotheses accept the same extensions (a Viterbi-like pruning). — [RSCW algorithm](https://www.pa3fwm.nl/software/rscw/algorithm.html)
- [documented fact] The author calls this theoretically optimal for on-off keying in AWGN but gives no SNR figures; limitations: FFT-peak frequency tracking can jump between signals, clock recovery fails when inter-character spacing isn't 3 units (e.g., DK0WCY beacon uses 4), no language model. — [RSCW algorithm](https://www.pa3fwm.nl/software/rscw/algorithm.html)

**MRP40 (commercial, closed)**
- [documented fact] Claims decoding to 60 WPM, automatic speed recognition, AGC, AFC tracking of drifting signals ("Smart AFC"), automatic spacing correction of run-together words, "very good decoding of weak, noisy and fading signals," and "almost 100% copy" in contest conditions; no algorithm description. — [MRP40 home page](http://www.polar-electric.com/Morse/MRP40-EN/)
- [secondhand] Built-in filter described as about 30 Hz, adapted to speed; has a selectable weak-signal mode; reported to beat CwGet, MixW, and CWDecoderXP on weak signals. — [search summary of MRP40 pages/DXZone](https://www.dxzone.com/dx6642/mrp40-morse-decoder.html); [MRP40 Tips](https://www.polar-electric.com/Morse/MRP40-EN/tips.html)
- [secondhand] VE9KK found MRP40 strong near the noise floor, but it prints random characters on static; the free "CW Decoder Logic" (LY3H) had poor weak-signal copy and "hunted" for other signals between letters at slow speeds. He notes decoders print exactly what a poor fist sends. — [AmateurRadio.com, VE9KK](https://www.amateurradio.com/comparing-two-cw-decoding-programs/)

**CwGet (DXSoft, closed)**
- [documented fact] Selectable FIR or IIR filters (FIR better but costlier), automatic speed detection with a speed-lock button, an adjustable or automatic ("AutoThr") detection threshold, a burst filter that drops short noise pulses, AFC, squelch; unrecognized dit/dah sequences (often run-together characters from poor spacing) are shown in curly brackets. — [CwGet product page](https://www.dxsoft.com/en/products/cwget/)

**Morse Expert, WSJT-style approaches**
- No sources found (see Gaps).

**Arduino/embedded (OZ1JHM and derivatives)**
- [documented fact] OZ1JHM's Arduino decoder uses a Goertzel filter at a single tone (typical parameters: ~8928 Hz sampling, 558 Hz target, 64-sample blocks) giving a magnitude, then a threshold and a noise blanker (initial 6 ms); widely copied (M5Stack, mbed, k3ng keyer). — [OZ1JHM](http://oz1jhm.dk/content/very-simpel-cw-decoder-easy-build); [fletch.scot Goertzel decoder](https://fletch.scot/radio/goertzel.html); [JJ1LFO M5Stack port](https://github.com/JJ1LFO/M5Unified_CW_Decoder); [k3ng keyer CW decoder](https://github.com/k3ng/k3ng_cw_keyer/wiki/385-Feature:-CW-Decoder)

**Simple Python "morse-audio-decoder" (mkouhia)**
- [documented fact] Moving-RMS envelope (0.01 s Hann), fixed threshold at 0.5 x max envelope, run-length durations, K-means clustering of on/off durations into dot/dash and gap classes, then table lookup. Assumes no noise, constant speed, constant pitch, one channel; poor on isolated characters. — [mkouhia/morse-audio-decoder README](https://github.com/mkouhia/morse-audio-decoder/blob/main/README.md)
- [documented fact] Other Python projects: Shervinfmz adds envelope extraction, adaptive WPM, and an n-gram character language model to correct ambiguous output; joseph-crowley uses Butterworth bandpass, Hilbert envelope, and adaptive thresholding. — [Shervinfmz/morse-code-decoder](https://github.com/Shervinfmz/morse-code-decoder); [joseph-crowley/morse-audio](https://github.com/joseph-crowley/morse-audio)

**Deep-learning decoders on GitHub**
- [documented fact] DeepCW (e04/web-deep-cw-decoder): browser-based real-time neural decoder (model distributed as ONNX in deepcw-engine, AGPL-3.0). Benchmark in AWGN with SNR from time-averaged signal power (~50% duty cycle) in a 2,500 Hz noise bandwidth: 0.00% error from 0 to -4 dB at all tested speeds, near error-free at -6 dB, under 1.5% at -8 dB, under 8% at -10 dB. Also compared against CW Skimmer, fldigi, and ggmorse on short YouTube QSO clips with reference transcripts, claiming better accuracy. Architecture and training data are not documented in the README. — [e04/web-deep-cw-decoder README](https://raw.githubusercontent.com/e04/web-deep-cw-decoder/main/README.md); [e04/deepcw-engine](https://github.com/e04/deepcw-engine)
- [documented fact] CW-LAB is a fork of DeepCW described as CRNN + CTC running in ONNX Runtime Web, adapted as a learning tool; no metrics. — [lucpaysan/CW-LAB](https://github.com/lucpaysan/CW-LAB)
- [documented fact] MorseAngel (F4EXB): two stacked LSTM layers plus a linear layer on envelope samples, outputting 7 streams (signal, separators, 5 element positions); needs manual WPM setting (22-27 WPM typical), limited to 5-element characters, degrades on weak signals; author says it "shows signs of working." — [f4exb/morseangel](https://github.com/f4exb/morseangel)
- [documented fact] MorseNet: research project using recurrent networks with CTC on raw audio. MaorAssayag's project uses object detection on spectrograms to find Morse in noisy, interference-laden data. — [netom/MorseNet](https://github.com/netom/MorseNet); [MaorAssayag/morse-deep-learning-detect-and-decode](https://github.com/MaorAssayag/morse-deep-learning-detect-and-decode)

### Inferences
- [inference] ggmorse's exhaustive speed/threshold search is a batch "fit the whole window" approach: robust for one steady signal, but it assumes one speed per window and a global threshold, so fading and speed changes within a window hurt it; cost scales with (speeds x thresholds) per window.
- [inference] RSCW shows the payoff of sequence-level maximum-likelihood decoding, but only when timing is known; Bell's decoder is the hand-keyed generalization of the same idea.
- [inference] DeepCW's published curve is the best numeric bar found among open decoders; converting to a 3 kHz reference (about -0.8 dB) puts it at roughly <8% CER at -10.8 dB, versus fldigi's 1.2% at -10 dB and 19.5% at -13 dB (3 kHz), so the two cannot be ranked closely without running both on identical files.

### Gaps
- Morse Expert: no algorithm or performance documentation found.
- WSJT-style (coherent/sequence) CW decoders: no open project found in this search.
- ggmorse: no published SNR/CER evaluation.
- DeepCW: architecture, training data (speed range, fist jitter, QSB, QRM), and model size/CPU cost undocumented.
- MRP40 and CwGet algorithms are proprietary; performance evidence is anecdotal.

## 4. Cross-cutting: weak signals, speed changes, poor fists, QSB, QRM, cost per channel, quantitative evaluation

### Takeaway
Only AG1LE (fldigi legacy, Bell port, neural nets) and DeepCW publish CER-vs-SNR data, all on synthetic AWGN and mostly machine-timed text; no source found evaluates poor fists, QSB, or QRM quantitatively. Hard-threshold decoders are cheap but cliff near -10 dB (3 kHz); probabilistic and neural decoders promise gains but have no published per-channel CPU figures.

### Cited Findings
- [documented fact] fldigi legacy + 35 Hz matched filter, 20 WPM machine text, AWGN in 3 kHz: CER 1.2% (-10 dB), 19.5% (-13), 37.5% (-14), 50.7% (-15), 74.2% (-20). — [AG1LE 2013 SNR vs CER](http://ag1le.blogspot.com/2013/01/morse-decoder-snr-vs-cer-testing.html)
- [documented fact] Bell port: CER falls sharply above ~6 dB SNR (2 kHz bandwidth), ~3% floor. — [AG1LE: New Morse Decoder Part 1](http://ag1le.blogspot.com/2013/09/new-morse-decoder-part-1.html)
- [documented fact] AG1LE CNN-LSTM-CTC: good to about -12 dB, sharp drop below (25/30 WPM synthetic). — [AG1LE 2019 performance post](http://ag1le.blogspot.com/2019/02/performance-characteristics-of-ml-morse.html)
- [documented fact] DeepCW: 0% to -4 dB, <1.5% at -8 dB, <8% at -10 dB (2.5 kHz). — [DeepCW README](https://raw.githubusercontent.com/e04/web-deep-cw-decoder/main/README.md)
- [documented fact] Speed handling: fldigi moving-average of dot-dash pairs; Bell 8-80 WPM speed state (can stick at extremes in the alpha port); ggmorse 5-55 WPM search; MRP40/CwGet automatic with speed lock (CwGet); MorseAngel manual. — sources as cited in sections 1-3: [fldigi cw.cxx](https://github.com/w1hkj/fldigi/blob/master/src/cw_rtty/cw.cxx); [AG1LE Part 2](http://ag1le.blogspot.com/2013/12/new-morse-decoder-part-2.html); [AG1LE Part 6](http://ag1le.blogspot.com/2014/07/new-morse-decoder-part-6.html); [ggmorse](https://github.com/ggerganov/ggmorse); [CwGet](https://www.dxsoft.com/en/products/cwget/); [MorseAngel](https://github.com/f4exb/morseangel)
- [documented fact] Multi-signal/QRM: AG1LE's multi-channel Bayesian build had worse accuracy with several stations at different speeds and merged peaks within 20 Hz; RSCW's frequency tracker jumps between signals; CW Decoder Logic "hunts" to other signals between letters. — [AG1LE Part 6](http://ag1le.blogspot.com/2014/07/new-morse-decoder-part-6.html); [RSCW](https://www.pa3fwm.nl/software/rscw/algorithm.html); [VE9KK review](https://www.amateurradio.com/comparing-two-cw-decoding-programs/)
- [secondhand] CW Skimmer's Bayesian decoder runs up to 700 channels on a 3 GHz P4, the only per-channel cost figure found for a probabilistic decoder. — [Wikipedia: CW Skimmer](https://en.wikipedia.org/wiki/CW_Skimmer)

### Inferences
- [inference] SNR bandwidth conversion: SNR(3 kHz) = SNR(2.5 kHz) - 0.8 dB = SNR(2 kHz) - 1.8 dB. Any benchmark for the skimmer should fix one reference bandwidth (and state duty cycle) so results can be compared to these published numbers.
- [inference] CW Skimmer's 700 channels on a 2000s-era CPU implies a probabilistic decoder can be cheap enough (roughly a few MIPS or less per channel) if implemented carefully; the Bell port's cost is unknown and deep-learning decoders likely cost far more per channel unless batched across channels.
- [inference] A useful evaluation set for choosing a decoder should add what no published source covers: hand-keyed timing jitter, speed ramps, Rayleigh-style QSB, and adjacent-signal QRM, not just AWGN on machine-timed text.

### Gaps
- No quantitative poor-fist, QSB, or QRM results were found for any open decoder.
- No per-channel CPU measurements found for fldigi, the Bell port, ggmorse, or the neural decoders.
- No independent (third-party) reproduction of DeepCW's or AG1LE's numbers was found.
