# Keying envelopes of real amateur transmitters

Research for D17 (the test set's keying edges) and D16 (the 80 Hz filter's shape) of
`docs/plans/2026-10-20-new-decoder-prototype-1.md`. Date: 2026-10-23. Read-only research: no repository file
other than this one was changed. Committed 2026-10-25 (owner: "yes (a)") as the evidence the design notes cite; the
text is as written on 2026-10-23, except that references to scratch files are marked as such. Its 80 Hz filter is
superseded by plan D17's 76 Hz filter.

**How each claim was checked.** Every claim carries one of these tags:

- **[S]** read in the source itself: the manufacturer's manual, the ARRL Lab report or *QST* review text, the
  program's source code, the standard, the author's own article.
- **[2]** read in a secondary source: a forum post, a third-party guide, a repost, or a search-engine summary
  of a page I could not open. Named each time.
- **[R]** recollection, unchecked.
- **[D]** derived here: my own calculation from sourced numbers. The scratch scripts are `edges.awk`
  (rise-time fractions of edge shapes) and `mf.awk` (matched-filter numbers). Both are temporary and outside the
  repository, as are the downloaded manuals and reviews.

## Terms used here

- **Envelope.** The RF amplitude as a function of time, in FS (fraction of key-down amplitude).
- **Edge length E.** The time an edge takes from 0 to 100% amplitude, in ms.
- **10–90% rise time t_r.** The time from 10% to 90% amplitude, in ms. This is how the ARRL Lab defines it [S:
  ARRL expanded test reports, 1996–1998]. For a raised-cosine edge, t_r = 0.590·E [D]. Manufacturers' menu
  values do not say which definition they use. Kenwood's does: it defines rise time as key-down to peak, i.e. E [S].
- **Raised cosine (RC).** An edge of the form ½ − ½·cos(π t/E).
- **On delay / off delay.** The time from key closure to the RF rising, and from key opening to the RF falling,
  in ms. The ARRL Lab's procedure measures transmit delays between 50% points (stated for the PTT test, section
  4.8.3.5 [S]). The CW keying captions do not restate the reference point, so treating them as 50%-point delays
  is an assumption.
- **Mark-length bias Δ.** The RF mark length at half amplitude minus the keyed length, in ms. If the delays are
  50%-point delays, Δ = off delay − on delay [D]. Δ < 0 means the transmitter shortens marks.
- **Weighting.** ARRL's measure of mark length: the fraction of time the carrier is on during a string of dots.
  Perfect keying gives 50% [S]. At the ARRL's 60 WPM test (20 ms on, 20 ms off), Δ = (weighting − 0.5)·40 ms [D].
- **QSK.** Full break-in: the receiver works between elements. **VOX/semi break-in:** the transmitter stays on
  for a hang time after the last element.
- **WPM.** PARIS speed; dot length T = 1.2 s / WPM. 100 WPM: T = 12 ms; 60 WPM: 20 ms; 25 WPM: 48 ms.

## Summary

1. **Rise times.** Current transceivers offer menu edge times of 1 to 8 ms. Defaults are 4 ms (Icom IC-7300,
   IC-9700 and IC-7760; Yaesu FT-991A and FT-710), 6 ms (Kenwood TS-890S, Yaesu FTDX10) and 2 ms (Icom IC-7610,
   from one secondary source). Elecraft (K3, KX3, K4) and FlexRadio offer no menu setting. In ARRL Lab
   measurements at 40–60 WPM, 10–90% rise and fall times run 0.8 to 4.7 ms. The three current mainstream rigs
   with published numbers measure 2.5 to 4.7 ms at their default settings. The ARRL Lab's 1997 boilerplate says
   "most click-free transmitters have a rise and fall time between 1 ms and 5 ms" (10–90%).
2. **Shape.** Shaping is usually a raised cosine or something close to it: the K3, the QRP Labs shaper, the
   Flex, VE3NEA's DeepCW generator. Smoother integrated-window ramps also occur: VE3NEA's Morse Runner uses an
   integrated Blackman-Harris ramp, and piHPSDR an optimized series of sines. One older DSP rig, the TS-950SDX,
   used a Gaussian. Measured edges are often asymmetric: rise and fall differ by up to a factor of 5 (0.8 ms rise
   and 4 ms fall in one rig).
3. **Timing.** Every shaper whose timing I could read starts its rise at key-down and its fall at key-up. Its
   half-amplitude mark length therefore equals the keyed length, delayed by E/2. This is the generator's
   "centered" convention up to a constant delay, not its default "inside" convention. Real transmitters add
   unequal on and off delays, though. Measured biases Δ run from −7.0 ms to +4.7 ms, with both signs in current
   equipment: an IC-7760 shortens a 40 WPM first dit by 5.1 ms, an FT-710 lengthens a 60 WPM dit by 4.7 ms. The
   first element often differs from later ones by 1 to 3 ms.
4. **High speed.** Kenwood's TS-890S manual is the only manufacturer source tying rise time to speed: its 6 ms
   default "is suitable for low and medium speed keying; for high-speed keying set 4, 2 or 1 ms". The ITU's
   necessary bandwidth for Morse is B_N = K·B, with B the dot rate in Bd and K = 3 (no fading) or 5 (fading).
   That is 62.5/104 Hz at 25 WPM, 150/250 Hz at 60 WPM and 250/417 Hz at 100 WPM. Combined with Grammer's
   (*QST* 1966) pulse-bandwidth rule, it implies t_r ≈ T/K: 2.4–4.0 ms at 100 WPM [D, heuristic]. No source
   gives measured envelopes of real 60–100 WPM signals. ARRL tests stop at 60 WPM, and most internal keyers stop
   at 48–60 WPM; the K3 in QRQ mode and the Flex reach 100 WPM.
5. **Skimmer sources.** VE3NEA documents nothing about the keying envelopes of real signals in CW Skimmer or
   on the Reverse Beacon Network. His simulators use a 2 ms raised cosine (DeepCW) and a 5 ms (10–90%)
   integrated Blackman-Harris ramp (Morse Runner). Both preserve the keyed length at half amplitude.
6. **For the filter and the generator (findings, section 6).** For a 100 WPM dot, the edge shape barely
   matters: a 12 ms boxcar filter (noise bandwidth 83.3 Hz) loses at most 0.33 dB against a filter matched to a
   centered raised-cosine dot with any edge from 2 to 8 ms [D]. The mark-length bias matters more: a −5 ms bias
   costs 4.7 dB against the ideal dot. The generator's 5 ms raised cosine (t_r = 2.95 ms) lies inside the
   measured range of real rise times. Its default placement inside the element gives every transmitter
   Δ = −5 ms. That is at the shortening end of the measured range, and the data show both signs.

## 1. Rise and fall times

### 1.1 Settings in current transceivers

- **Kenwood TS-890S.** Menu 6-13 "CW Rise Time": 1/2/4/6 ms, default 6 ms. "CW rise time is the time from key-down to
  the point where the output waveform reaches its highest peak" [S: TS-890S instruction manual B5A-2217-10, Spanish
  edition, translated by me; the text cites the menu as [5-15], the table as 6-13]. *QST* confirms the default:
  "the waveform shown in Figure 5 is with the default 6-millisecond setting" [S: *QST* June 2019 review].
- **Kenwood TS-590S.** Maximum setting 6 ms [S: an aside in a *QST* July 2020 amplifier review]. A FlexRadio
  forum post says the TS-590 "sounds really harsh and wide with the 2 ms setting" [2: N4LQ, 2015].
- **Icom IC-7300.** CW-KEY SET → Rise Time: 2, 4, 6 or 8 ms, default 4 ms [2: ManualsLib copy of the Full
  Manual p. 55; the official PDF refused download (HTTP 403)]. DJ0IP cites "Full Manual (page 4-21)" for the 4 ms
  default and recommends at least 6 ms against clicks [S: DJ0IP, "Operating CW with the IC-7300"].
- **Icom IC-9700.** Default 4 ms [S: *QST* January 2020 review caption].
- **Icom IC-7760.** Default 4 ms, choices 2, 6 and 8 ms; the reviewer used 8 ms "for the cleanest CW waveform and
  keying sidebands" [S: *QST* April 2026 review].
- **Icom IC-7610.** "The default CW rise time is 2ms, which is a key-click special. Choose 6ms or maybe even 8ms,
  depending on what CW speed you operate at." [S: Sherwood Engineering test-result sheet, test date
  2017-12-22, hosted by K0PIR]. This 2 ms default conflicts with the 4 ms of Icom's other current rigs, and I did
  not check it in Icom's manual.
- **Yaesu FTDX10.** "CW WAVE SHAPE: Sets the rise and fall times of the keying envelope": 4/6/8 ms, default 6 ms
  [S: FTDX10 Operation Manual, 2021]. A third-party guide (cw599.com) gives the start value as 4 ms [2]. That is
  wrong against the manual.
- **Yaesu FTDX101D/MP.** ARRL tested them at "the default rise time setting" but did not state its value [S: *QST*
  November 2019 and December 2020]. Earlier Yaesu rigs offered "4, 6, and 8 ms" [S: *QST* August 2023, on "some
  prior Yaesu rigs"].
- **Yaesu FT-710.** "CW Wave Shape … now 2, 3, and 4 ms … When the Lab measured the rise and fall times, the
  measured times were much faster than the menu settings showed. The new times in the latest version of Yaesu's
  firmware … reflect more closely the actual rise times … leave the default setting of 4 ms" [S: *QST* August 2023].
  So at least one manufacturer's menu numbers did not match measured times until a firmware change.
- **Yaesu FT-991A.** CW WAVE SHAPE 2/4 ms, default 4 ms [2: cw599.com guide].
- **Elecraft K3/K3S, KX3, K4.** The owner's manuals have no rise-time or wave-shape menu entry. I searched the
  K3 rev. D10, K3S, KX3 rev. C5 and K4 rev. D6 texts for "rise", "shape" and "wave" [S, negative result]. The K3
  is described as having "a DSP-generated 'raised-cosine' waveform with 5 ms 'rise time'" [2: "CW Spectral
  Waveforms", reposted by 9M2PJU in 2012, original author not named]. In a scope comparison, W9AC measured a
  K3 in QRQ mode reaching full power about 13 ms after key closure and falling to zero about 10 ms after key-up,
  at 40 WPM [2: FlexRadio forum, 2014]. The 13 ms includes the key-to-RF delay.
- **FlexRadio 6000 series.** No menu setting. A Flex community manager wrote: "under v1.3.8 we had the rise time
  set to 2-5ms and it is now 6-8ms", and the WPM slider "also affects the rise time for both internally and
  externally keyed CW" [2: FlexRadio forum, N5AC, 2015]. W9AC measured about 5 ms (6700), about 4 ms (6500,
  "appears to be a … raised cosine") and full power 6–7 ms after key closure (6700, 2014) [2: forum]. Adjustable
  rise time was "likely" in a future release (2015). In 2025 users were still asking for it [2]. Current state
  unknown.
- **Apache Labs ANAN / Thetis.** Not found in any source.
- **piHPSDR (DL1YCF; software for HPSDR/ANAN radios).** Default RF ramp width 9 ms (`int cw_ramp_width = 9;
  // default value (in ms)`) [S: `src/radio.c`, master branch, read 2026-10-23]. I did not find the menu range.
- **Hermes-Lite 2.** The radio "generates a shaped CW signal". The protocol documentation gives no rise time
  [S: Hermes-Lite2 wiki, Protocol].
- **SunSDR2 (Expert Electronics).** Software-set rise and fall time. W9AC compared 5 ms with 2 ms: "more than a 20 dB
  difference at ±1 kHz from Fc" [2: FlexRadio forum, 2015].
- **QRP Labs shaper (AN005).** Raised cosine, 2 to 113 ms, default 5 ms. "Practically, 5 to 10ms is most commonly
  used." [S: QRP Labs AN005].

### 1.2 Measurements

All ARRL Lab measurements below are 10–90% times on the first two dits of a dot string, with external keying, at
14 MHz unless stated.

- **ARRL test conditions.** "The keying rate is 20 ms on and 20 ms off, a rate that corresponds to 60 WPM"; the
  test captures the first and second dits in VOX and QSK modes; "Some transmitters will lose or chop the first dit
  of a word" [S: ARRL Lab Test Procedures Manual, 2010, section 4.7]. Several 2025–2026 reviews used 40 WPM instead
  [S: captions].
- **ARRL boilerplate (1996–1998).** "If the risetime or falltime become too short, the transmitter will generate
  key clicks. Most click-free transmitters have a rise and fall time between 1 ms and 5 ms." [S: ARRL Laboratory
  Expanded Test-Result Reports, TS-570D/FT-920/IC-746].
- **Numbers.** In the table in section 7.
- **Mainstream current rigs with numbers.** FT-710: first-dit rise 2.5 ms, fall 3.9 ms; second dit 3.6/4.7 ms, at the
  4 ms default [S: *QST* August 2023]. IC-7760: 3.0/2.9 ms first dit, 3.1/2.9 ms second, at 40 WPM, default
  setting [S: *QST* April 2026]. If the 4 ms setting means a raised cosine 4 ms long (0–100%), the expected 10–90% time is
  2.36 ms [D]. The measurements, 2.5 to 4.7 ms, run longer: the menu number is not exactly either definition.
- **Mainstream current rigs without numbers.** For the IC-9700, TS-890S, FTDX101D/MP and FLEX-6600M the reviews
  print only scope figures (10 ms per division) [S]. I could not render the figures to read them.
- **Older rigs (1996–1998).** TS-570D rise 1.0–1.1 ms, fall 2.2 ms (subsequent dits rise 2 ms). FT-920 rise
  2.5–4.0 ms, fall 1.0–2.0 ms. IC-746 rise 1.0–2.0 ms, fall 1.5 ms [S: ARRL expanded reports].

### 1.3 Guidance not tied to speed

- "The ARRL recommends a 5 mS rise and 5 mS fall time for CW … based on data in section 2.202 of FCC rules and
  CCIR Radio regulations. But this is only a simplified version." [2: W8JI quoted on LA8AK's page]. "ARRL typically
  recommends 5 ms for keying speeds up to at least 40 WPM" [2: NA6O, no citation]. I did not find either statement
  in an ARRL publication.

## 2. Shape

- **Theory.** For a pulse whose rising and falling edges have the same form, the envelope is a square pulse of
  the keyed width convolved with the edge's slope. Its spectrum is the square pulse's spectrum times the
  slope's transform, so "the rise time dominates the bandwidth". The edge with the best product of time and
  bandwidth spreads has a Gaussian slope, i.e. an error-function edge [S: W9CF, "Spectral Analysis of a CW keying
  pulse", 2001]. KE3HO compared 0.5 ms and 5 ms trapezoids with a 5 ms "raised SIN" edge. Pulse length (30 ms against
  300 ms) leaves the spectrum's envelope unchanged; the raised sine falls fastest beyond about 250 Hz [S: KE3HO, "CW
  Bandwidth Analysis"].
- **Raised cosine.** The K3 [2]. QRP Labs AN005 [S]. The Flex 6500 "appears to be a … raised cosine" [2].
  "Around 6–8 ms, and … raised-cosine shaping" was offered as typical [2: N7AIG, FlexRadio forum]. piHPSDR's
  sidetone (the RF ramp is different, below) [S]. DeepCW's generator: 2 ms, the two halves of a Hann window [S via
  `docs/research/deepcw-generator-notes.md` §1.3]. One soundcard keying program defines its rise time as 0–100%,
  with 10–90% ≈ 0.6 of it [2: search summary of a TechSpot download page]. That matches the raised cosine's 0.590 [D].
- **Integrated windows (smoother than a raised cosine at the corners).** Morse Runner (VE3NEA) uses the integral
  of a 4-term Blackman-Harris window. Its length is 2.7 × RiseTime, with RiseTime = 5 ms by default, so 13.5 ms
  from 0 to 100% [S: `VCL/MorseKey.pas`]. Its 10–90% time is 0.973 × RiseTime = 4.87 ms [D]. VE3NEA's "RiseTime"
  is therefore a 10–90% figure. piHPSDR's RF ramp is y − Σ aₖ sin(2πky), with five coefficients "optimiz[ed]
  … against the spectral pollution of a string of dots". 10–90% = 0.549 × width, so 4.9 ms at the 9 ms default
  [S code, D fraction]. Its source also keeps a disabled "VE3NEA ramp (integrated Blackman-Harris-Window)" [S].
- **Gaussian.** The TS-950SDX: "a DSP-generated 'gaussian' time-domain waveform with four choices of 'rise time'",
  8 ms among them [2: "CW Spectral Waveforms"]. W9AC said Flex's ramp change keeps "the Gaussian shape" [2].
- **Exponential (RC networks).** Analog keying circuits. W9CF shows its abrupt slope change widens the
  spectrum [S]. NA6O found "a simple RC exponential shape produced much higher side lobes" [2: search summary].
- **Asymmetric edges are common in measurements.** MMX 0.8 ms rise, 4 ms fall; CFT1 3.2/1.6 ms; FT-710
  2.5/3.9 ms; TS-570D 1.0/2.2 ms; FT-920 QSK 2.5/1.0 ms [S: ARRL]. The Ten-Tec Omni VI Plus had "a sharp 1-2 ms
  leading edge" [2: W9AC].

## 3. Timing: where the edges sit relative to the keyed timing

### 3.1 What a shaper does by itself

- **QRP Labs AN005.** Key OUT's "leading edge is synchronous with the keying IN signal, but [its] trailing edge
  'over-hangs', or 'lags' the keying signal, while the raised cosine shaping occurs" [S]. The rise starts at
  key-down and the fall starts at key-up. So the half-amplitude mark length equals the keyed length, delayed by
  E/2 [D].
- **KE3HO's model** defines pulse width "from the beginning of the rising edge (key down) to the beginning of the
  falling edge (key up)" [S]. Same convention.
- **Morse Runner (VE3NEA).** A dot is RampOn, then on for (unit − RampLen), RampOff, then off for (unit − RampLen)
  [S: `GetEnvelope`]. The fall begins exactly one unit after the rise began, so the half-amplitude length is
  exactly one unit [D]. At 100 WPM the 13.5 ms ramp exceeds the 12 ms unit, and the on-time would be negative
  [D]. I did not check Morse Runner's speed limit.
- **DeepCW (VE3NEA).** Mark = front edge (n_e) + ones(L − n_e) + rear edge (n_e): half-amplitude length L
  [S via project notes, which mark the 50% length as inference].
- **Causality** [D]. A transmitter shaping in real time cannot start its fall before key-up, unless it delays
  the whole keying stream. So shaping alone cannot shorten a mark at half amplitude; a causal shaper keeps it or
  lengthens it. Shortening comes from unequal delays in the transmit path (T/R switching, the first element's
  hold-off, ALC).
- **The generator's conventions.** `keying_envelope(..., centered=True)` puts each edge's midpoint on the
  interval's end. Its shape is that of a causal shaper delayed by E/2, so the 50% length equals the keyed length.
  The default `centered=False` puts both edges inside, so the 50% length is the keyed length − E.

### 3.2 What real transmitters do: measured mark-length bias

Δ at half amplitude, in ms (negative = marks shortened). Old reports: from weighting at 60 WPM, Δ = (w − 0.5)·40 ms.
New reports: off delay − on delay [D from S numbers].

| Rig (source) | Speed | Mode | First dit Δ | Later dit Δ |
|---|---|---|---|---|
| Kenwood TS-570D (ARRL 1997) | 60 WPM | QSK / VOX | +1.6 (54%) | 0.0 (50%) |
| Yaesu FT-920 (ARRL 1996–97) | 60 WPM | QSK | −4.0 (40%) | −5.0 (37.5%) |
| Yaesu FT-920 | 60 WPM | VOX | −1.0 (47.5%) | −0.4 (48.9%) |
| Icom IC-746 (ARRL 1998) | 60 WPM | semi-QSK | −3.3 (41.7%) | −2.2 (44.4%) |
| Icom IC-746 | 60 WPM | QSK | −3.3 (41.7%) | −6.1 (34.7%) |
| Yaesu FT-710 (*QST* 08/2023) | 60 WPM | QSK delay 15 ms | +4.7 (on 18, off 22.7) | +3.8 (18.8, 22.6) |
| Icom IC-7760 (*QST* 04/2026) | 40 WPM | "QSK set to the default 4 ms" | −5.1 (10.5, 5.4) | −3.5 (9.3, 5.8) |
| Xiegu G106 (*QST* 05/2023) | 60 WPM | QSK on | +3.1 (5.7, 8.8) | −0.9 (10.4, 9.5) |
| Xiegu X6200 (*QST* 07/2025) | 40 WPM | QSK 100 ms | −1.7 (10.6, 8.9) | −1.6 (10.6, 9.0) |
| Penntek TR-45L (*QST* 12/2023) | 60 WPM | — | −3.2 (10.7, 7.5) | −4.6 (12.1, 7.5) |
| WA3RNC TR-35 (*QST* 12/2022) | 60 WPM | — | −4.2 (12, 7.8) | −7.0 (14.9, 7.9) |
| WA3RNC TR-25 (*QST* 12/2021) | 60 WPM | — | −5.6 (12.8, 7.2) | −5.6 (12.8, 7.2) |
| KM4CFT CFT1 (*QST* 04/2025) | 40 WPM | — | −3.2 (4.8, 1.6) | −3.1 (4.8, 1.7) |
| QRP Labs QCX-mini (*QST* 11/2021) | 60 WPM | — | +0.6 (1.1, 1.7) | +0.8 (0.8, 1.6) |
| PreppComm MMX (*QST* 03/2024) | 60 WPM | — | +2.1 (1.5, 3.6) | +1.1 (1.5, 2.6) |

Caveats. In the old reports, the on and off delay columns do not change between modes, while weighting does.
For the IC-746, the listed 9 ms on and 3 ms off match QSK's 34.7% (Δ = −6 ms) but not semi-QSK's 44.4%. For the
TS-570D, the delays give Δ ≈ −1 ms in QSK against 50–54% weighting. I take weighting as the more direct measure
there. The table has no bias numbers for the mainstream rigs whose reviews print only figures: IC-7300,
IC-7610, IC-9700, TS-890S, FTDX10, FTDX101, K3, K4, Flex.

Findings [D unless marked]:

- Biases of a few ms in both directions are normal. Over this table they run from −7.0 to +4.7 ms. The WA3RNC and
  Penntek kits and the CFT1 shorten marks by 3 to 7 ms; the MMX, QCX-mini and G106 lengthen them by up to 3 ms; one
  current 100 W rig (FT-710) lengthens them by about 4 ms and another (IC-7760) shortens them by 3.5 to 5 ms.
- The bias is a time offset in ms. Nothing in the sources says it scales with speed. If it stays fixed (an
  assumption), its share of a dot grows with speed: ±5 ms is ±10% of a 25 WPM dot, ±25% at 60 WPM and ±42% at
  100 WPM.
- **First element.** The first dit can differ from later ones by up to 4 ms (G106: +3.1 against −0.9 ms; IC-746
  QSK: −3.3 against −6.1 ms). ARRL's procedure warns that some transmitters "lose or chop the first dit" [S]. Sherwood
  on the IC-7610: "For a clean first 'dit' on CW, the ALC needs to be backed off" [S]. ALC is the automatic level
  control, which can clip the start of the first element. Elecraft K3: "In QRQ mode, the delay from key down to first RF out may be as little
  as 4 to 5 milliseconds" (8 ms minimum otherwise). With a longer TX DLY, "your CW keying may exhibit excessive
  jitter (per-element variation)" [S: K3 manual]. Yaesu FTDX10: QSK DELAY TIME 15/20/25/30 ms before transmitting,
  forced to 15 ms at 45 WPM and above [S: FTDX10 manual].
- **Weighting set on purpose.** Keyers and rigs let operators change mark length. Icom's dot/dash ratio runs
  1:1:2.8 to 1:1:4.5, default 1:1:3.0 [2: ManualsLib copy]. The K3's CW WGHT is an "element/space timing
  ratio", listed at 1.15 [S]. K1EL keyers offer 25–75% weighting and 0–31 ms "keying compensation" plus a
  first-dit/dah compensation [2: search summary of K1EL K16/K40 pages]. Keying compensation exists to lengthen
  elements that a transmitter shortens. So the on-air mark length is the keyer's choice plus the rig's bias.

## 4. High speed (60 to 100 WPM)

### 4.1 Guidance that ties rise time to speed

- **Kenwood TS-890S** [S]: "The default setting (6 ms) is suitable for keying at low and medium speed. For
  high-speed keying, set '4 ms', '2 ms' or '1 ms'." The only manufacturer statement I found.
- **Sherwood on the IC-7610** [S]: "Choose 6ms or maybe even 8ms, depending on what CW speed you operate at."
- **FlexRadio**: the rise time follows the keyer speed slider. W9AC saw the change at 19–20 WPM [2].
- **piHPSDR** [S]: the ramp coefficients were optimized for "ramp width 7 msec for CW speed 5 - 15 wpm …
  8 msec for … 16 - 32 wpm … 9 msec for … 33 - 40 wpm", to meet a spectral mask on a dot string: −20 dBc beyond
  90 Hz, −40 dBc beyond 150 Hz, −60 dBc beyond 338 Hz. The source calls these ARRL "clean signal initiative"
  limits; I did not check that attribution. Here the ramp gets *longer* as speed rises, because a faster dot
  string puts more energy into the sidebands.
- **QRP Labs** [S]: "the rise/fall speed must never be more than the symbol rate!" That is, an edge must be
  shorter than a dot (12 ms at 100 WPM).
- **Grammer, W1DF, "Why Key Clicks?", *QST* October 1966** [S: RF Cafe transcription]. "The bandwidth of a pulse
  is equal to 1 divided by twice its rise or decay time". The RF bandwidth is twice the keying bandwidth. "A rise
  or decay time of 5 to 7 milliseconds is short enough for the fastest hand keying speeds". "A speed of 50 bauds
  is about as fast as any amateur will go with hand keying".
- **ARRL (secondary only).** One ARRL committee document is said to tie "rise and fall times of 5 ms" to about
  150 Hz, enough for about 36 WPM on fading circuits and 60 WPM on non-fading ones [2: search summary of an
  ARRL board-minutes attachment; the file refused download (HTTP 403)].

### 4.2 ITU-R SM.1138 necessary bandwidth

From the source [S: Rec. ITU-R SM.1138-3 (10/2019), Annex 1]: "Continuous wave telegraphy, Morse code: B_n = BK;
K = 5 for fading circuits, K = 3 for non-fading circuits. 25 words per minute: B = 20, K = 5, bandwidth 100 Hz
(100HA1AAN)". B is "modulation rate (Bd)". The ITU example's B = 20 Bd at 25 WPM is 0.80 Bd per WPM. The PARIS
dot rate 1/T = WPM/1.2 s is 0.833 Bd per WPM, 4% higher [D]. With the PARIS rate [D]:

| Speed | Dot T | B = 1/T | B_N, K = 3 | B_N, K = 5 | t_r ≈ T/K (K = 3 / K = 5) |
|---|---|---|---|---|---|
| 25 WPM | 48 ms | 20.8 Bd | 62.5 Hz | 104 Hz | 16.0 ms / 9.6 ms |
| 60 WPM | 20 ms | 50.0 Bd | 150 Hz | 250 Hz | 6.7 ms / 4.0 ms |
| 100 WPM | 12 ms | 83.3 Bd | 250 Hz | 417 Hz | 4.0 ms / 2.4 ms |

The last column is my own heuristic, not a standard [D]. I set Grammer's RF bandwidth of an edge, 1/t_r, equal to
B_N = K/T, which gives t_r = T/K: a rise time that allows the K-th harmonic of the dot rate. It shows that the
ITU's "necessary" bandwidth at 100 WPM permits edges of 2.4 to 4 ms. Grammer did not say whether his t_r is
10–90% or 0–100%, so the factor is uncertain to about ×1.7 (the raised cosine's 0–100%/10–90% ratio).

### 4.3 What real high-speed signals use

- I found no measurement of a real 60–100 WPM signal's envelope. The ARRL Lab tests at 60 WPM at most [S].
- Most current internal keyers stop below 100 WPM [S: *QST* specification tables]: IC-9700 and IC-7760 6–48 WPM,
  TS-890S 4–60 WPM, FTDX101D 4.8–60 WPM, FTDX101MP 4–56 WPM, FT-710 4–60 WPM. The FLEX-6600M reaches 4.2–100 WPM
  [S] and the K3 reaches 8–100 WPM in QRQ mode [S: K3 manual: "For ultra-fast CW full break-in at up to 100 WPM,
  set CONFIG:CW QRQ to ON"]. At 100 WPM the other rigs are keyed by an external keyer or a computer, with their
  rise-time menus as set; Kenwood's manual tells high-speed users to shorten the edge.
- A 0–100% edge of E inside a 12 ms dot leaves a flat top of 12 − E ms if the edges straddle the keyed timing
  [D]: 10 ms at E = 2 ms, 8 ms at 4 ms, 6 ms at 6 ms, 4 ms at 8 ms, 3 ms at piHPSDR's 9 ms. Any setting
  up to 12 ms still reaches full amplitude.

## 5. Skimmer and RBN sources

- **Already recorded in the project.** `docs/research/deepcw-generator-notes.md`: DeepCW's generator uses 2 ms
  raised-cosine edges (0–100%), with the 50% mark length equal to the drawn length L, and has "no chirp, clicks"
  beyond them. `docs/research/research_notes/CW decoder algorithms survey/cw_skimmer.md`: RBN and N6TV advise
  senders to keep "3:1 weighting" and a constant speed. Neither file has data on real signals' envelopes.
- **New here.** VE3NEA's Morse Runner keys its simulated stations with an integrated Blackman-Harris ramp,
  5 ms at 10–90% (13.5 ms from 0 to 100%), preserving the keyed length [S: code]. A disabled "VE3NEA ramp" in piHPSDR
  is the same shape [S].
- **Searched, nothing found.** CW Skimmer's documentation, VE3NEA's pages and RBN pages say nothing about keying
  envelopes, rise times, key clicks or mark-length bias of real signals. RBN spots carry a signal-quality field
  but no keying measure [2: DXSpider wiki]. HamSCI asked skimmer operators for raw recordings for later analysis
  [2]; I found no result on keying.

## 6. What this implies for a filter matched to a 100 WPM dot and for the generator's default edges

Findings, not recommendations. The numbers are [D] from `mf.awk`: one 100 WPM dot (T = 12 ms keyed), amplitude 1
FS, white noise, a linear filter on the complex baseband before the detector. "Boxcar" is the filter matched to the
ideal 12 ms rectangle. Its noise bandwidth is 1/T = 83.3 Hz, which is the plan's "about 80 Hz". Its power
response is 3 dB down at ±36.9 Hz, with the first nulls at ±83.3 Hz. Gains are relative to an ideal 12 ms
rectangle through the boxcar.

| Dot shape (RC = raised cosine, E = 0–100% edge) | 50% length | Energy | Own matched filter | Boxcar | Boxcar vs own | Own filter's noise bandwidth |
|---|---|---|---|---|---|---|
| Rectangle, no edges | 12.0 ms | 12.0 ms·FS² | 0 dB | 0 dB | 0 dB | 83.3 Hz |
| RC 2 ms centered | 12.0 ms | 11.5 | −0.18 dB | −0.27 dB | −0.08 dB | 79.9 Hz |
| RC 4 ms centered | 12.0 ms | 11.0 | −0.38 dB | −0.54 dB | −0.16 dB | 76.4 Hz |
| RC 5 ms centered | 12.0 ms | 10.75 | −0.48 dB | −0.68 dB | −0.21 dB | 74.7 Hz |
| RC 6 ms centered | 12.0 ms | 10.5 | −0.58 dB | −0.83 dB | −0.25 dB | 72.9 Hz |
| RC 8 ms centered | 12.0 ms | 10.0 | −0.79 dB | −1.12 dB | −0.33 dB | 69.4 Hz |
| **RC 5 ms inside (generator default)** | 7.0 ms | 5.75 | −3.20 dB | −4.68 dB | −1.49 dB | 117.3 Hz |
| RC 4 ms centered, Δ = −3.5 ms | 8.5 ms | 7.5 | −2.04 dB | −3.00 dB | −0.96 dB | 103.8 Hz |
| RC 4 ms centered, Δ = −5 ms (IC-7760 first dit) | 7.0 ms | 6.0 | −3.01 dB | −4.68 dB | −1.67 dB | 122.4 Hz |
| RC 4 ms centered, Δ = +4.7 ms (FT-710) | 16.7 ms | 15.7 | +1.17 dB | 0.00 dB | −1.17 dB | 56.3 Hz |
| RC 8 ms centered, Δ = −5 ms | 7.0 ms (peak 0.98 FS) | 5.0 | −3.80 dB | −4.79 dB | −0.99 dB | 102.0 Hz |

F1. **For the filter, edge shape is a small effect.** Across the measured and menu range of edges (centered
raised cosines, E = 2 to 8 ms, i.e. t_r = 1.2 to 4.7 ms), the boxcar loses 0.08 to 0.33 dB against the filter
matched to the true dot. The best filter's noise bandwidth moves only from 80 to 69 Hz. The shapes between raised
cosine and integrated Blackman-Harris differ by less than the edge-length range does (not computed separately).
Matching the edge exactly is worth about 0.1–0.3 dB at 100 WPM.

F2. **Mark-length bias is the larger effect.** A real Δ of −3.5 to −5 ms at 100 WPM costs 3.0 to 4.7 dB through
the boxcar. Of that, 2.0 to 3.0 dB is energy the transmitter never sent; the rest, 1.0 to 1.7 dB, is filter
mismatch. A +4.7 ms bias leaves the boxcar's output unchanged: it captures a full 12 ms of carrier. The longer
mark's own matched filter would gain 1.17 dB. No single filter matches both signs; the boxcar sits at the
lengthening end's floor and the shortening end's loss.

F3. **The generator's default 100 WPM dot** (5 ms raised cosine inside the element) has 7 ms at half amplitude,
3.2 dB less energy than the keyed dot, and needs a 117 Hz filter to be matched. Against the measurements it
behaves like a transmitter with Δ = −5 ms and 5 ms edges. That lies within the measured biases (IC-7760 first dit
−5.1 ms, FT-920 QSK −5.0 ms, TR-25 −5.6 ms). But it applies the same shortening to every transmitter, while the
measured biases spread from −7.0 to +4.7 ms, with both signs in current equipment.

F4. **The generator's edge length is not long for real transmitters.** RISE_S = 5 ms is a 0–100% raised
cosine, so t_r = 2.95 ms. Measured 10–90% times of current mainstream rigs at default settings are 2.5 to 4.7 ms;
menu defaults are 4 to 6 ms, and the maximum is 8 ms. Kenwood advises 1–4 ms for high speed, and the ITU heuristic in
§4.2 allows 2.4–4 ms at 100 WPM. With centered edges, the 5 ms raised cosine at 100 WPM keeps the full 12 ms at half
amplitude, with a 7 ms flat top. The 5 ms edges shorten marks only because of where the default puts them, inside
the element.

F5. **The timing convention has two separable parts.** (a) Shaping by itself, in every source with readable timing
(QRP Labs, KE3HO, Morse Runner, DeepCW), keeps the keyed length at half amplitude, i.e. the generator's
"centered" convention up to a constant delay of E/2. (b) Real transmit paths add a bias Δ of a few ms whose sign
and size vary by rig, by mode (QSK against VOX) and between the first and later elements. The sources support (a)
as the shaping convention. They support (b) as a spread across transmitters; no source makes it a fixed offset.

F6. **Other measured properties of real edges that the generator does not have:** rise and fall that differ (up to
5× apart); a first element that differs from later ones by up to about 4 ms; a weighting chosen by the keyer
(25–75% in K1EL keyers, 2.8–4.5 dash/dot in Icom rigs) on top of the rig's bias.

## 7. Rise times by rig and setting

10–90% times are the ARRL's measurements; menu values are the manufacturer's (definition unstated unless noted).

| Rig | Menu setting (default in bold) | Measured 10–90% rise / fall | Source |
|---|---|---|---|
| Kenwood TS-890S | 1, 2, 4, **6** ms (key-down to peak) | figure only | manual [S]; *QST* 06/2019 [S] |
| Kenwood TS-590S | up to 6 ms | — | *QST* 07/2020 [S] |
| Icom IC-7300 | 2, **4**, 6, 8 ms | — | Full Manual via ManualsLib [2]; DJ0IP [S] |
| Icom IC-9700 | **4** ms | figure only | *QST* 01/2020 [S] |
| Icom IC-7610 | **2** ms (per Sherwood) | — | Sherwood sheet [S]; not checked in Icom manual |
| Icom IC-7760 | 2, **4**, 6, 8 ms | 3.0/2.9 ms (first dit), 3.1/2.9 ms, at 40 WPM | *QST* 04/2026 [S] |
| Yaesu FTDX10 | 4, **6**, 8 ms | — | manual [S] |
| Yaesu FTDX101D/MP | default not stated | figure only | *QST* 11/2019, 12/2020 [S] |
| Yaesu FT-710 | 2, 3, **4** ms | 2.5/3.9 ms (first), 3.6/4.7 ms | *QST* 08/2023 [S] |
| Yaesu FT-991A | 2, **4** ms | — | cw599.com [2] |
| Elecraft K3/K3S/KX3/K4 | no menu | K3: "5 ms raised cosine" | manuals [S, negative]; repost [2] |
| FlexRadio 6000 | no menu; tied to WPM slider | ~4–5 ms (forum scope), "6–8 ms" (Flex staff, 2015) | forum [2] |
| piHPSDR (HPSDR/ANAN) | **9** ms ramp (t_r 4.9 ms) | — | source code [S, D] |
| QRP Labs AN005 | 2–113 ms, **5** ms raised cosine | — | app note [S] |
| Kenwood TS-570D (1997) | — | 1.0–1.1/2.2 ms (first), 2/2.2 ms | ARRL report [S] |
| Yaesu FT-920 (1996–97) | — | QSK 2.5/1.0 ms, then 4.0/1.0; VOX 3.0/2.0, then 4.0/2.0 | ARRL report [S] |
| Icom IC-746 (1998) | — | 1.0/1.5 ms (first), 2.0/1.5 ms | ARRL report [S] |
| QRP rigs and kits, 2021–2025 (*QST*: G106, X6200, TR-25/35/45L, CFT1, QCX-mini, MMX) | — | 0.8–4.5 ms rise, 1.5–4.3 ms fall | captions [S] |
| Morse Runner (VE3NEA, simulator) | RiseTime **5** ms (≈ 10–90%) | — | source code [S] |
| DeepCW generator (VE3NEA, simulator) | **2** ms raised cosine | — | project notes [S] |
| KZ4AP generator | RISE_S **5** ms raised cosine, inside | t_r = 2.95 ms | `generate.py` [S] |

## 8. Gaps

- No numbers for the mainstream rigs whose *QST* reviews print only figures: IC-7300, IC-7610, IC-9700,
  TS-890S, FTDX10/101, K3, K4, FLEX-6600M. Reading their bias would need the figures rendered, or the ARRL Lab's
  expanded test reports for those rigs.
- No measurement anywhere of real signals at 60–100 WPM, or of the bias at speeds other than 40–60 WPM.
- Not checked: Icom's own manual for the IC-7300/IC-7610 values; the ARRL Handbook's current text; the ARRL "5 ms up
  to 40 WPM" statement; Morse Runner's speed limit; piHPSDR's ramp-width menu range; whether ARRL's CW on/off
  delays are 50%-point delays.

## Sources

Manuals and standards (downloaded and read):
- Elecraft K3 Owner's Manual rev. D10 (2011); K3S, KX3 rev. C5, K4 Operating Manual rev. D6 —
  ftp.elecraft.com/K3, /K3S, /KX3, /K4 "Manuals Downloads".
- Yaesu FTDX10 Operation Manual (2021) — https://www.rcqsl.com/manuals/19.pdf
- Kenwood TS-890S Manual de instrucciones B5A-2217-10 (Spanish) — https://www.rcqsl.com/manuals/162.pdf
- ITU-R SM.1138-3 (10/2019) — https://www.itu.int/dms_pubrec/itu-r/rec/sm/R-REC-SM.1138-3-201910-I!!PDF-E.pdf
- ARRL Laboratory Test Procedures Manual (2010) — https://www.arrl.org/files/file/Technology/tis/info/pdf/Procedure%20Manual%202010%20with%20page%20breaks.pdf
- ARRL Laboratory Expanded Test-Result Reports, TS-570D, FT-920, IC-746 — https://stuff.mit.edu/afs/sipb/user/jhawk/tdunn/
- *QST* Product Reviews 2019–2026 (68 PDFs) — https://www.arrl.org/files/file/ProductReviewsForDeb/ (e.g. 2023/pr082023.pdf
  FT-710; 2026/pr042026.pdf IC-7760; 2020/0619.pdf TS-890S; 2020/0120.pdf IC-9700)
- Sherwood Engineering, IC-7610 test results (2017) — https://k0pir.us/wp-content/uploads/2017/12/Icom-IC-7610-h.pdf
- DJ0IP, "Operating CW with the IC-7300" — https://www.dj0ip.de/app/download/5815650643/IC-7300+Amp+Keying.pdf
- QRP Labs AN005 — https://www.qrp-labs.com/images/appnotes/AN005_A4.pdf
- Source code: VE3NEA Morse Runner `VCL/MorseKey.pas` (https://github.com/VE3NEA/MorseRunner); DL1YCF piHPSDR
  `src/transmitter.c`, `src/radio.c` (https://github.com/dl1ycf/pihpsdr)
- W9CF, "Spectral Analysis of a CW keying pulse" (2001) — https://hellschreiber.com/pdf-hell/article-spectrum-cw-9cf.pdf
- KE3HO, "CW Bandwidth Analysis" — https://hellschreiber.com/pdf-hell/article-spectrum-cw-3ho.pdf
- G. Grammer, W1DF, "Why Key Clicks?", *QST* October 1966 — https://rfcafe.com/references/qst/why-key-clicks-october-1966-qst.htm

Secondary:
- ManualsLib IC-7300 Full Manual p. 55 — https://www.manualslib.com/manual/1106166/Icom-Ic-7300.html?page=55
- cw599.com guides (FT-991A, FTDX10) — https://cw599.com/radio-guides/yaesu-ft-991a/
- "CW Spectral Waveforms" (9M2PJU repost) — https://hamradio.my/cw-spectral-waveforms/
- FlexRadio community threads — https://community.flexradio.com/discussion/comment/15494021 , /13747713 , /13749716
- LA8AK quoting W8JI — https://noding.com/la8ak/b9.htm ; NA6O — https://na6o.com/amateur_radio/eperimental_xmtr.html
- Hermes-Lite 2 protocol — https://github.com/softerhardware/Hermes-Lite2/wiki/Protocol
- K1EL keyer pages (search summary) — https://www.k1elsystems.com/K40info.html
