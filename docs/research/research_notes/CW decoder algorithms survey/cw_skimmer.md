# CW Skimmer and CW Skimmer Server (Afreet Software, VE3NEA): decoding and spotting

Labels used on each claim: **[fact]** = stated by VE3NEA or official Afreet docs (including VE3NEA statements relayed verbatim by RBN admins); **[secondhand]** = reported by others; **[inference]** = my reasoning.

Primary sources used:
- CW Skimmer 2.1 user manual (PDF, ~98 pp., 2008-2018): http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf
- CW Skimmer product page: https://www.dxatlas.com/CwSkimmer/
- Skimmer Server product page: https://www.dxatlas.com/SkimServer/
- RBN blog, SNR definition (VE3NEA email quoted by N4ZR, 2014-03-05): http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html
- RBN blog, "How Skimmer Server Decides What to Spot" (N4ZR, 2021-09-28, after discussion with VE3NEA): http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html
- RBN page "How to get spotted by the RBN": https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44
- AG1LE blog, VE3NEA email advice (2013-01): http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html
- N6TV Contest University 2018 and Dayton 2014 slides: https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf, https://www.kkn.net/~n6tv/N6TV_Dayton_2014_CW_Skimmer_Update.pdf

## 1. What VE3NEA has said about the decoder's approach (Bayesian, speed/level/noise estimation, soft decisions, priors)

### Takeaway
The only official description of the algorithm is one phrase: a very sensitive decoder "based on the methods of Bayesian statistics." VE3NEA has never published the algorithm; his one known technical hint (private email to AG1LE) says to work in probabilities end to end: compute the probability that the signal is present instead of making hard per-sample decisions, and combine probabilities through the stages up to word recognition. Everything more specific (how speed, level and noise are tracked; whether a callsign/language model is used inside the decoder) is undocumented.

### Cited Findings
- [fact] The manual and product page describe the decoder as "a very sensitive CW decoding algorithm based on the methods of Bayesian statistics"; no further algorithmic detail is given anywhere in the manual. — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf); [Product page](https://www.dxatlas.com/CwSkimmer/)
- [fact, relayed] In a private email to AG1LE (who asked for advice on improving the fldigi CW decoder), VE3NEA advised approaching every problem in a Bayesian framework: express prior knowledge as probabilities and use observed data to update them. For example, instead of deciding at every input sample whether the signal is present, compute the probability that it is present. He advised applying Bayes' rule at each stage of the decoder and carrying probabilities "all the way to the word recognition unit." — [AG1LE blog, Jan 2013](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)
- [fact, relayed] VE3NEA told AG1LE he worked on the problem for about 8 years and tried hundreds of algorithms before getting a version that worked as he wanted. The manual's credits also mention "the long 8 years that I spent on this software." — [AG1LE blog](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html); [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] The decoder shows confidence information to the user: characters decoded at very low SNR are printed in gray or pale colors to flag possible decoding errors. — [Manual, "CW decoder"](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] Version 1.1 notes say "the accuracy of decoding and word segmentation improved," so word segmentation is an explicit step in the pipeline. — [Manual, Version history](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] The SNR measurement (see section 4) discards key-off samples and on/off transition samples, so the software classifies each sample as key-on, key-off or transition, at least for measurement. — [RBN blog, VE3NEA email](http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html)
- [fact] Noise density is estimated from the flat part of the power spectrum over the whole receiver bandwidth (48/96/192 kHz), not locally around each signal. — [RBN blog, VE3NEA email](http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html)
- [fact] Keying speed (WPM) is estimated per signal and reported in each spot (added in v1.3). — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] Senders are advised to keep speed constant within a message (no speed changes mid-message), use 3:1 dash/dot weighting and clean letter spacing, and avoid half-spaces next to the callsign. — [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html); [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf)
- [secondhand, speculation] AG1LE and a commenter speculated that Skimmer may use real-time dictionary lookups (a language prior). VE3NEA did not confirm this. — [AG1LE blog](http://ag1le.blogspot.com/2013/01/towards-bayesian-morse-decoder.html)

### Inferences
- [inference] The "word recognition unit" wording, the word-segmentation release note, and the confidence-shaded output together suggest a soft-decision pipeline: per-sample key-on probabilities, then element/character hypotheses, then word-level recognition that uses the soft scores. Whether that stage uses a dictionary or callsign prior is not public.
- [inference] Asking senders to keep speed and weighting constant within a message suggests the timing model assumes a fairly stable speed (and perhaps weighting) per transmission, estimated from a window of history. Hand-sent CW with irregular timing is its known weak point.
- [inference] Estimating noise once across the whole band is cheap and robust but will overstate SNR where the local noise floor is raised by QRM or splatter. A per-signal decoder may need a local noise estimate as well.
- [inference] For our design, "Bayesian" here most plausibly means a probabilistic (likely HMM-like or particle-style) key-state and timing model with soft outputs, similar in spirit to the Bell/AG1LE Bayesian Morse decoder. That is an informed guess, not something documented.

### Gaps
- No published description of the state model, the speed or weighting tracker, the signal-level tracker (AGC in the decoder), the decision stage, or whether a language model is used. I found no paper, patent, talk or slide deck by VE3NEA on the internals.
- The documented speed range of the decoder is not stated. The only WPM figure in the manual (45 WPM) is for reading Morse by eye on the waterfall, not for the decoder.

## 2. Signal detection and separation across a wide bandwidth

### Takeaway
Skimmer creates a separate decoder for each CW signal it finds in the passband. Each decoder is about 50 Hz wide (secondhand, from RBN admins) and keeps a 256-character history. The number of decoders adapts to available CPU: up to 700 parallel decoders on a 3-GHz Pentium 4. Skimmer Server covers up to 7 (N6TV says 8) bands of up to 192 kHz each.

### Cited Findings
- [fact] Decodes all CW signals in the receiver passband at once; up to 700 signals can be decoded in parallel on a 3-GHz P4 with a wideband receiver. — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] CW Skimmer creates a new CW decoder for every CW signal in the passband. Each decoder's decoded text is analyzed and shown as a label on the band map. — [Manual, "Band map"](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] "Max Number of CW Decoders": by default the program picks the optimal number of decoders based on available CPU ("Adaptive"). The user can instead set a fixed number. — [Manual, Settings/Misc](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] CPU minimums: 1 GHz for a 3-kHz (soundcard) radio, Pentium 4 2.5 GHz for a wideband radio. — [Product page](https://www.dxatlas.com/CwSkimmer/)
- [fact] Skimmer Server: all CW signals on up to 7 bands, up to 192 kHz per band, using the same decoding algorithms as CW Skimmer. Minimum CPU is a 3 GHz processor with SSE3; an Intel Core i7 is recommended. — [SkimServer page](https://www.dxatlas.com/SkimServer/)
- [secondhand] N6TV says Skimmer Server monitors up to 8 bands at once with a single SDR. This conflicts with the official "up to 7 bands." — [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf) vs [SkimServer page](https://www.dxatlas.com/SkimServer/)
- [secondhand] Skimmer works by watching hundreds of decoders, each 50 Hz wide, across a band. Each decoder is 256 characters "deep": new characters are added and the oldest dropped. — [RBN "How to get spotted"](https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44); [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html)
- [fact] Signal extraction for SNR uses a 50 Hz filter, which is consistent with the 50-Hz channel description. — [RBN blog, VE3NEA email](http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html)
- [fact] The power spectrum computed "as part of Morse Code decoding" can be sent over UDP to other programs (v2.0), which means decoding starts from an FFT power spectrum. — [Manual, Spectrum via UDP](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] A noise blanker is included, aimed at impulsive noise (static crashes, powerline noise). The manual says it works best with no strong signals in the passband and should stay off unless impulse noise is present. — [Manual, DSP](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [secondhand] RBN "QSY" flags "may occasionally be image spots," meaning I/Q image signals can create spurious decoders. — [RBN tutorial 2013](http://reversebeacon.blogspot.com/2013/12/a-new-tutorial-on-using-rbn.html)

### Inferences
- [inference] The architecture is probably: wideband FFT, then peak/trace detection on the spectrum, then one narrow (~50 Hz) channel per detected trace, then a per-channel Bayesian decoder, then a per-channel text buffer (256 chars), then a message analyzer. "Adaptive" decoder count suggests the per-decoder cost is significant and signals are prioritized when CPU is short.
- [inference] A 50-Hz channel implies a limit on how close two signals can be before they merge into one decoder. The docs give no figure for minimum resolvable spacing.

### Gaps
- No documentation of the signal-detection threshold, how traces are tracked when a signal drifts, how decoders are created or retired, or the FFT resolution.

## 3. Callsign extraction and the spotting decision

### Takeaway
A message analyzer scans each decoder's text for callsigns and for keywords (CQ, TEST, DE, and others) that show whether the station is running or S&P and whether the call is the sender's. A call is spotted only after it passes pattern and list validation and repeats enough times within the 256-character buffer. The number of repeats needed depends on how plausible the call is (watch list, then common pattern in the patt3ch.lst file, then rarer listed pattern, then unlisted pattern) and on the user's validation level. The standalone Skimmer can also check against Master.dta (the SCP file); Skimmer Server does not use MASTER.DTA. Busted calls are mostly handled downstream, by the RBN/CT1BOH cross-skimmer statistics, not inside Skimmer.

### Cited Findings
- [fact] The message analyzer pulls callsigns from the text and looks for keywords such as "CQ", "DE", "TEST" to decide whether the call belongs to the sender or to the other station, and whether the station is running or S&P. Labels get a "CQ" prefix (running) or "DE" (sender's own call). A call that appears two or more times in the message is shown in bold. — [Manual, "Band map"](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] A callsign is sent to Telnet clients, listed, and shown in bold only if it passes the validation tests: Watch list (user list), Master.dta (SCP), typical/unusual/suspicious callsign pattern, and ITU block (the prefix must come from an ITU-allocated block). — [Manual, "Callsign Validation"](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] There are four validation levels (Minimal, Normal, Aggressive, Paranoid); the higher the level, the more calls are rejected. The Telnet SKIMMER/SETT command reports e.g. "vlNormal". — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] Release notes: V1.2 added Callsign Validation. V1.6 and V1.9 improved CQ/DE detection. V2.0 improved prefix identification. V2.1 fixed a bug that caused only calls in master.dta and watch.lst to be spotted, which shows list membership and pattern validation are separate paths. — [Manual, Version history](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [fact] A Telnet option, "Do not send callsigns without CQ", restricts spots to CQing stations. A station first spotted without CQ is re-spotted with "CQ" once it is found to be running. A station that is not running but is sending its own call is marked "DE". — [Manual, Telnet](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [secondhand, from VE3NEA discussion] Skimmer Server uses a pattern file, patt3ch.lst. Patterns keep three explicit characters plus wildcards (@ = letter, # = digit), e.g. VE3@@@, N4@@, HA2###@@@. A "+" marks commonly heard patterns, and the file is updated about twice a year. — [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html); [RBN "How to get spotted"](https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44)
- [secondhand, from VE3NEA discussion] Repeats needed within the 256-character buffer, by validation level (Minimal / Normal / Aggressive / Paranoid):
  - Watch list: 1 / 1 / 2 / 2
  - "+" pattern: 2 / 2 / 4 / not spotted
  - Listed pattern without "+": 2 / 3 / 4 / not spotted
  - Pattern not in file: 2 / 5 / not spotted / not spotted

  — [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html). The RBN "How to get spotted" page gives a simplified version (2 / 3 / 4 repeats for common / standard / unlisted), which roughly matches the Normal column except for the unlisted count (4 vs 5). — [RBN page](https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44)
- [secondhand] The transmission must include a keyword (TEST or CQ; FD, SS, NA, UP in the right context). Calls with a /B suffix (beacons), taken from beacon lists downloaded nightly, are spotted without keywords. — [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html); [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf)
- [secondhand] The original keywords were CQ, TEST and QRZ; VE3NEA later added FD, SS, NA and UP. N6TV advises sending short calls (e.g., W1F) twice. — [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf)
- [secondhand] Skimmer Server will not re-spot the same call on or near the same frequency more than once every 10 minutes; a QSY of about 0.5 kHz resets this. — [RBN 2021 blog](http://reversebeacon.blogspot.com/2021/09/how-skimmer-server-decides-what-to-spot.html)
- [secondhand] Skimmer Server does not use MASTER.DTA, while CW Skimmer does. — [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf)
- [secondhand] Half-spaces next to a call can cause mis-decodes, e.g. a stray "T" from "CWT" merging with a call. — [RBN "How to get spotted"](https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44)
- [secondhand] Busted-call handling happens at the network level. RBN applies CT1BOH's "skimquality" algorithms, which mark a spot "verified" when more than 2 skimmers heard it, flag QSY spots, and flag likely busts using "a complex statistical algorithm" that also suggests the correct call (example: K7FL flagged as a bust of OK7FL). CC Cluster nodes drop "unique" spots (heard by only one skimmer) as likely busts. — [RBN tutorial 2013](http://reversebeacon.blogspot.com/2013/12/a-new-tutorial-on-using-rbn.html); [N6TV Dayton 2014](https://www.kkn.net/~n6tv/N6TV_Dayton_2014_CW_Skimmer_Update.pdf)

### Inferences
- [inference] The spotting rule works as a crude posterior threshold: the prior plausibility of a call (watch list, then common pattern, then rare pattern, then unknown) trades off against evidence (number of independent decodes in the buffer). An open-source design could replace this table with an explicit posterior: callsign prior from pattern or SCP frequency, times per-decode likelihoods from soft decoder scores.
- [inference] Inside one skimmer, there is no documented cross-decode voting beyond repeat counting. Correcting busts (e.g., OK7FL/K7FL, where a leading character is lost) relies on multi-skimmer statistics.

### Gaps
- There is no official description of the analyzer grammar (how it tokenizes, how "DE"/"CQ" context is scored), or of how the Master.dta check combines with the pattern levels in standalone Skimmer.
- The CT1BOH algorithm is not published in detail.

## 4. SNR reporting: exact definition

### Takeaway
According to VE3NEA's own description, the reported value is key-down signal power (signal taken through a 50-Hz filter, only key-on samples kept, fading handled with a Rayleigh model) divided by the noise power in 500 Hz. The 500-Hz noise figure is computed as noise power spectral density × 500 Hz, with the density taken from the flat part of the whole receiver's power spectrum. It is a nominal 500-Hz bandwidth, not an equivalent noise bandwidth of any real filter, and the signal is key-down, not averaged over keying. The measurement builds up over roughly 45 s until the spot is validated.

### Cited Findings
- [fact, VE3NEA email quoted verbatim by N4ZR, 2014-03-05] The signal is extracted from the I/Q stream with a 50-Hz filter. Samples in the key-off state and on/off transitions are discarded. Signal strength is computed assuming a Rayleigh fading model, to account for QSB. Noise density is estimated from the flat part of the power spectrum. Noise power in 500 Hz is density × bandwidth. The SNR is signal power divided by 500-Hz noise power. — [RBN blog 2014](http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html)
- [fact] Manual: the SNR in the spot comment is "the key-on signal-to-noise ratio in the 500 Hz bandwidth averaged over QSB." — [Manual, Telnet spots](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [secondhand, N4ZR in the same post] Data are collected over about 45 s, up to the point the spot is validated (correcting an earlier belief that SNR came from a short interval). Noise is estimated across the whole receiver bandwidth (48/96/192 kHz), not locally. The SNR was checked and found accurate even at 10 stations/kHz with an average SNR of 60+ dB. Values depend on each node's antenna, receiver and noise. — [RBN blog 2014](http://reversebeacon.blogspot.com/2014/03/understanding-signal-to-noise-ratio-snr.html)
- [fact] The spot format is "DX de CALL-#: freq CALL NN dB NN WPM [CQ|DE] time". The "-#" suffix marks an automatic spot. — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)

### Inferences
- [inference] Because the noise figure is density × 500 Hz, it is effectively an ideal rectangular (brick-wall) 500-Hz noise bandwidth. Converting to other conventions: SNR in 2500 Hz (WSJT-style) = RBN SNR − 10·log10(5) ≈ RBN SNR − 7.0 dB. SNR in 50 Hz ≈ RBN SNR + 10 dB. SNR per 1 Hz (dB-Hz, C/N0) = RBN SNR + 27.0 dB.
- [inference] Because signal power is key-down (not averaged with key-up time), an on-off keyed signal's average power is about 3-4 dB lower (roughly a 40-50% duty cycle) than the reported key-down value.
- [inference] "Rayleigh model" plus "averaged over QSB" suggests the estimator targets the mean power of a fading signal rather than a peak or median. For a Rayleigh-faded signal the median is about 1.6 dB below the mean. The exact estimator is not given.
- [inference] Estimating noise across the whole receiver (not locally) means spots in crowded or splattered segments will read high compared with a local-noise SNR.

### Gaps
- The exact Rayleigh-model estimator is not documented, nor is the precise "flat part of the spectrum" method (a percentile or a mode, for example). It is also not stated whether the 50-Hz filter's own noise is subtracted from the key-on power.

## 5. Evidence on weak-signal performance, speed range, poor fists, QSB, QRM, and comparisons

### Takeaway
Hard numbers are scarce. The official claims are only qualitative ("very sensitive," "super-sensitive"). Anecdotes say it excels on weak signals and in contest pileups, but struggles with poorly timed hand-sent CW, mid-message speed changes and bad spacing, and has some word-spacing and lag problems compared with MRP40. I found no controlled published benchmark (for example, character error rate vs SNR) for CW Skimmer.

### Cited Findings
- [fact] Official wording: "very sensitive" (CW Skimmer) and "super-sensitive" (Skimmer Server). No threshold SNR is stated. — [Manual](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf); [SkimServer page](https://www.dxatlas.com/SkimServer/)
- [fact] The manual's noise-blanker demonstration uses a weak EME signal in high noise; the noise blanker is meant for impulse noise. — [Manual, DSP](http://dxatlas.com/CWSkimmer/Files/CwSkimmer.pdf)
- [secondhand] RBN guidance: make sure the CW is good, since errors show up as mis-spots. Avoid speed changes (no >/< or +++/--- speed shifts in messages), keep 3:1 weighting, and let the computer send rather than rushing with paddles. — [RBN "How to get spotted"](https://www.reversebeacon.net/pages/How+to+get+spotted+by+the+RBN+44); [N6TV CTU 2018](https://www.contestuniversity.com/wp-content/uploads/2018/05/4.-N6TV-CW-and-RTTY-Skimmers-and-the-Reverse-Beacon-Network.pdf)
- [secondhand] Average speeds of RBN spots: 30.6 WPM in CQ WW CW 2013 and 29.6 WPM in ARRL DX CW 2014, which shows routine decoding around 30 WPM in contests. — [N6TV Dayton 2014](https://www.kkn.net/~n6tv/N6TV_Dayton_2014_CW_Skimmer_Update.pdf)
- [secondhand] Search-result summaries of G4ILO's "Best Morse Decoder" post (2012) say it found CW Skimmer excels on weak signals, but that MRP40 had better word spacing (Skimmer sometimes ran words together or split them) and less lag. I could not fetch the page to verify (the connection failed), so treat this as unverified. — [G4ILO blog](http://blog.g4ilo.com/2012/12/best-morse-decoder.html)
- [secondhand] VE9KK: Skimmer "works great" for contests, but it keeps only about 10 words of history, which makes it awkward for ragchew QSOs. — [VE9KK via AmateurRadio.com, 2016](https://www.amateurradio.com/comparing-two-cw-decoding-programs/)
- [secondhand, low-quality aggregator] A 2020s guide claims a "20-28 WPM sweet spot" and says the decoder expects consistent element timing; no source is given. — [HamPost](https://www.hampost.com/guides/cw-skimmer-reverse-beacon-network)
- [secondhand] RBN volume: 2013 CQ WW CW produced about 5.7 M spots. Even at >99% accuracy, that volume yields many busts, attributed to local QRM, weak signals and similar causes. — [N6TV Dayton 2014](https://www.kkn.net/~n6tv/N6TV_Dayton_2014_CW_Skimmer_Update.pdf); search summary of [N1MM Telnet docs](https://n1mmwp.hamdocs.com/manual-windows/telnet-window/) (not fetched)

### Inferences
- [inference] The practical sensitivity floor is not published. RBN spots commonly report single-digit dB values (the manual's examples include 4-7 dB), i.e., decodes with key-down SNR around 5 dB in 500 Hz (about +15 dB in the 50-Hz channel). That sets a rough known-working point for comparison, not a threshold.
- [inference] Its known weaknesses (irregular hand timing, speed changes within a message, half-spaces) point to a timing model with fairly rigid speed and weighting assumptions and a word segmenter that relies on standard spacing. A decoder with explicit per-operator timing variance (as in Bell/AG1LE-style models) might handle poor fists better.

### Gaps
- There is no controlled weak-signal benchmark (error rate vs SNR), no documented minimum or maximum WPM, and no quantitative QSB or QRM tests for CW Skimmer. There are also no published head-to-head results against fldigi, MRP40 or the AG1LE decoder under identical conditions. The G4ILO comparison could not be verified directly.
- I found no VE3NEA talk, slides or article (QST/NCJ/CQ) describing the internals. The N6TV Dayton and CTU decks are operational, not algorithmic.
