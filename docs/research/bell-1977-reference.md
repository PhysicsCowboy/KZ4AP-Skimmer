# Bell 1977: reference sheet for the stage-1 port

This sheet is the reference from which stage 1 of the Bell decoder plan
(`docs/plans/2026-10-07-bell-decoder-stage-1.md`) builds three things: the C++ port of E. L. Bell's decoder, the
compiled Fortran reference, and the reproduction of his published tables. It gives every constant, formula and
table those tasks need, each with its source, its unit and its status.

- **Source.** E. L. Bell, *Optimal Bayesian Estimation of the State of a Probabilistically Mapped Memory-Conditional
  Markov Process with Application to Manual Morse Decoding*, Doctor of Engineering thesis, Naval Postgraduate School,
  1977. "p. N" is the printed page; in the local scan printed page N is PDF page 2N + 3.
- **The program** is the settled transcription `reference/bell1977/listing.txt` (with `README.md` and
  `transcription-log.md`). Where the listing and the thesis text disagree, the listing is used (owner's decision D1).
  The text decides only where the listing is unreadable or silent. Section (g) lists every disagreement.
- **The earlier summary** `docs/research/bell-1977-notes.md` was used as a guide only. Every statement here rests on
  the listing or on a thesis page checked on its image.

## Labels, units and conventions

- **Labels.** Every statement carries one:
  - [fact, code p. N L]: the listing, page N, sequence number L. On pp. 159, 163, 169, 177 and 178 the first digit of
    the sequence numbers is reconstructed (`{d}` in `listing.txt`); it is cited here without braces.
  - [fact, p. N]: the thesis text or a table or figure, checked on the page image.
  - [derived]: our calculation from the listing or the text; the method is given. "Derived numerically" means a
    simulation of the listing's own equations in double precision (scripts kept outside the repository; see the
    method notes in section (c)).
  - [inference]: our reasoning, with its grounds and how certain it is.
  - [our choice]: a value or rule the program or the thesis does not give, chosen here, with its reason.
- **Units.**
  - Time in s or ms. Bell's simulator and receiver chain run at 8000 samples/s, one sample every τ₈ = 0.125 ms
    [fact, code p. 160 00600: `TAU/.000125/`]. His decoder runs on every 40th sample, at 200 samples/s, one step
    every τ = 5 ms [fact, code p. 159 01800; p. 112].
  - Speed in WPM. The dot (baud) length is Δ = 1200/r ms at r WPM [fact, code p. 160 02900, p. 170 21200].
    "Bauds" and "dots" both mean multiples of Δ.
  - **AU** is the amplitude unit of Bell's simulator: its key-down carrier has amplitude 1 AU nominally
    [fact, code p. 163 00700: `AMP/1./`], and its noise variance RNOISE is in AU² per 8 kHz sample. The receiver
    chain is linear up to its rectifier, and the rectifier and the envelope low-pass are homogeneous of degree one,
    so the envelope Z, the decoder's measurement z and its variance R are also in AU and AU² [derived].
  - Per-step quantities state their step: "per 8 kHz sample" (0.125 ms) or "per 5 ms step".
  - Every dB names its reference: dB S₁₀₀ and dB S₅₀₀ (key-down carrier power over the noise power in 100 Hz or
    500 Hz, section (d)), E/N₀ in dB (energy of one dot over the one-sided noise density), dB relative to a stated
    gain (for filters), dB of amplitude ratio relative to a stated level.
- **Elements** are numbered as Bell numbers them: 1 dot, 2 dash, 3 element space (e-sp), 4 character space (c-sp),
  5 word space (w-sp), 6 pause [fact, code p. 160 00600 (mean lengths 1, 3, 1, 3, 7, 14 dots); p. 161 06300 (key
  state 1, 1, 0, 0, 0, 0); p. 164 07100 (printed as `.`, `_`, blank, `/`, `;`, `\`)].
- **Terms.** KAM is keyboard-sent Morse, exact timing; HKM is hand-keyed Morse [fact, p. 11]. "Fist" means a
  simulated hand sender (good, fair, poor). A "path" is one saved hypothesis of the decoder. Bell's RCVR, BPFDET and
  NOISE are his **receiver chain**.

## Read first: findings that change what the plan assumed

Each is detailed in the section named.

1. **The simulator's element durations are uniform, not Gaussian.** KEY draws each duration as
   mean ± U·EDEV·Δ with U uniform on (−1, 1), floored at 20 ms, with no ceiling. The text says a truncated Gaussian
   between 16 and 360 ms (p. 125). The fist settings follow from this (sections (d), (e)).
2. **The receiver chain is narrower than the text says.** The two resonators and RCVR's quadrature low-pass give a
   pre-detection band 64.4 Hz wide at −3 dB relative to its gain at the carrier, with a noise bandwidth of
   78.5 Hz. The envelope low-pass is not a 100 Hz Chebyshev: its response is monotone, −3 dB relative to its DC gain
   at 31.1 Hz, and its DC gain is 7.99 (section (c)).
3. **NOISE does not compute R = 2μ².** It sets R = 0.30·YMAVG, floored at 0.005 AU², and
   z = 1.1·(Z − 2.4·YMAVG − 0.05), where YMAVG is a smoothed 1.0 s running minimum of the envelope. R never reaches
   0, so the plan's §9 item 1 (division by zero) cannot occur through R (sections (b), (c)).
4. **The letter tree file is `MORSEM`, and INITL reads 300 rows of it,** not 400. Its 16 rows in DATA are the
   second-order element model of Table XII, and no letter tree can contain three of them (rows 5, 9 and 10). The
   plan's test "the 16 DATA rows are reproduced" is replaced by an exact one (section (f)).
5. **The simulator sends random letters only.** The text-file branch in KEY is unreachable code. The random draw
   sends A with probability 0.001 instead of 1/36, and the MORSE table sends C with B's code (section (d)).
6. **The decoder's longest decision delay is 199 samples (995 ms)**, not 200; SAVEP keeps at least 2 paths, not 7;
   it deletes duplicate paths; and its final renormalization leaves out the first path's probability (section (b)).
7. **Bell's error marks differ from our character CER.** In his printed samples a merged pair of letters is one
   error; the character CER counts two edits. The reproduction rule should take this into account (sections (i),
   (j)).
8. **The compiled reference needs one guard the plan does not list:** LIKHD reads `IELMST(0)` for dead paths
   (section (h)).

## (a) Program structure

### Call graph

The listing has a main program and 24 subprograms (22 subroutines, 2 functions) [fact, code pp. 159–191]:

```
MAIN (p. 159)
├── INITL            once: tables in DATA; reads MORSEM (300 rows) and TEXT (105 values)
├── INPUTL           once: run-time settings typed at the terminal
└── loop forever (label 1)
    ├── for each 8 kHz sample:
    │   ├── SIMSG1   the simulated received signal (one sample)
    │   │   ├── KEY      the keying: element sequence and durations
    │   │   │   └── RAN  (library: uniform random number)
    │   │   └── RANDN    (library: Gaussian random number), 4 calls per sample
    │   ├── RCVR     quadrature down-conversion, low-pass, up-conversion to 1000 Hz
    │   ├── BPFDET   two resonators, rectifier, envelope low-pass
    │   └── on every 40th sample:
    │       ├── NOISE    noise-floor estimate; z and R for the decoder
    │       └── PROCES   one decoder step
    │           ├── for each saved path: TRPROB (→ XTRANS, PTRANS → SPDTR), PATH, LIKHD (→ KALFIL → MODEL)
    │           ├── PROBP, SPROB, SAVEP
    │           └── TRELIS (→ TRANSL)
    ├── every 18 samples: STATS        (display buffer)
    └── every 512 × 18 samples: DISPLA (→ ERASE, PLOTR, VIEW: display library; then waits for the operator)
```

- AUTOCR (p. 166) is never called [fact: no `CALL AUTOCR` in the listing].
- ERASE, PLOTR, VIEW, RAN and RANDN are not in the listing (section (h)).

### Order of operations per 8 kHz sample

The main loop is `DO 2 N1=1,512` around `DO 3 N2=1,18` [fact, code p. 159 01000–02600]. For each sample k = 1, 2, …
(time t = k·0.125 ms):

1. `SIMSG1(X,ZSIG)`: KEY decides the key state x_k; the amplitude, fading, phase and chirp processes advance; the
   noisy sample ZSIG = s_k is formed [fact, code p. 159 01200; p. 163].
2. `RCVR(ZSIG,ZRCV)` [fact, code p. 159 01400].
3. `BPFDET(ZRCV,ZDET)` [fact, code p. 159 01500].
4. `NP=NP+1`; if NP = 40: NP = 0, then `NOISE(ZDET,RN,Z)` and `PROCES(Z,RN,XHAT,PX,ELMHAT,LTRHAT)` [fact, code
   p. 159 01700–02100]. NP starts at 0 [fact, code p. 159 00500] and is not reset between display frames, so
   NOISE and PROCES run on samples k = 40n, n = 1, 2, …, that is at t = 5n ms exactly [derived].
5. After every 18 samples: `STATS(ZDET,Z,PX,XHAT,…,N1)` stores one display point; after 512 of them (9216 samples,
   1.152 s of signal) `DISPLA` plots four traces and waits for the operator (`ACCEPT`) [fact, code p. 159
   02500–02700; p. 166 00900]. The 18 sets only the display rate; it does not touch the decoding [derived].

### Order of operations per 5 ms step (PROCES)

[fact, code pp. 167–168]

1. For each saved path i = 1 … ISAVE (at most 25):
   - `TRPROB`: the probabilities of its 30 successors (6 elements × 5 speed branches), normalized to sum to 1;
   - `PATH`: each successor's letter-tree state, element duration and speed;
   - `LIKHD`: for each successor, one Kalman step and the likelihood of z (KALFIL, with MODEL's Φ, Q, h).
2. `PROBP`: posterior of each of the 30·ISAVE successors, normalized.
3. `SPROB`: posterior that the key is down (PX), the most probable element, the conditional-mean speed;
   then XHAT = 1 if PX > 0.5 (the "demodulated signal").
4. `SAVEP`: prune to at most 25 paths, re-index them and their Kalman states, delete duplicates, renormalize.
5. `TRELIS`: store the saved nodes in a 200-stage circular trellis, find the node all paths share or force a
   decision at 199 stages, and pass each decided node to `TRANSL`, which prints letters.

The debug printout in PROCES is skipped by `GO TO 1` [fact, code p. 168 09750]. PROCES never assigns its outputs
ELMHAT and LTRHAT [fact, code pp. 167–168].

### Runs and operator pauses

- KEY counts elements. After 300 elements it types the sent element string, sets IEND = 1 and waits for the
  operator; then it continues with the next element [fact, code p. 164 09700–11100]. At its next call TRELIS types
  the average number of saved paths and the average decision delay, and waits [fact, code p. 184 02900–03900].
  The decoder's state is never reset between runs [fact: no code resets it].
- A run of 300 elements holds about 40.4 letters under KEY's random text (section (d)) [derived]. Bell's
  experiments had "approximately 200 letters" [fact, p. 139], so about 5 runs each [inference].
- On the PDP-10, one minute of signal took about 20 minutes [fact, p. 140].

## (b) Each subroutine

Order: setup (MAIN, INPUTL, INITL), simulator (SIMSG1, KEY), receiver chain (RCVR, BPFDET, NOISE), decoder (PROCES
and the routines it calls), display (DISPLA, STATS, AUTOCR). Every routine's local variables keep their values
between calls, and several rely on starting at zero (section (h)). Line numbers are Bell's sequence numbers.

### MAIN (p. 159)

- **Purpose:** drives the simulation, the receiver chain and the decoder, sample by sample (section (a)).
- **Locals:** `INTEGER ELMHAT,XHAT`; display buffers S1–S4(512).
- **Constants:**

  | Constant | Value | Line | Meaning |
  |---|---|---|---|
  | RN | 0.1 | 00400 | initial R; overwritten by NOISE before PROCES first uses it [derived] |
  | NP | 0 | 00500 | sample counter for the decimation |
  | inner loop | 18 | 01100 | 8 kHz samples per display point (display only) |
  | outer loop | 512 | 01000 | display points per frame |
  | decimation | 40 | 01800 | 8 kHz samples per decoder step: τ = 40 × 0.125 ms = 5 ms |

- **Notes:** XHAT (INTEGER) is passed to STATS's REAL argument XIN4, a type mismatch that affects only the display
  trace [fact, code p. 159 02500, p. 166 01700; derived]. `STOP` (03000) is unreachable.

### INPUTL (p. 160, 00100–04400)

- **Purpose:** reads the run settings typed at the terminal and converts them.
- **COMMON:** /BLK1/ TAU; /BLK6/ DMEAN, XDUR, ESEP(6), EDEV(6); /BLK2/ WC, WCHIRP, ASIGMA, BSIGMA, PHISGM, RSIGM,
  TCHIRP, GAMMA.
- **DATA:** TAU = 0.000125 s (00600); ESEP = 1, 3, 1, 3, 7, 14 dots (00600); EDEV = 6×0 (00600); XDUR = 0 ms (00700).
- **Inputs** (free-format `FORMAT(7F)`, prompts at 01100, 01400, 01800, 02100):

  | Prompt order | Variable | Unit (from its use) |
  |---|---|---|
  | 1 | RATE | WPM |
  | 2–7 | ESEP(1..6) | dots: mean length of dot, dash, e-sp, c-sp, w-sp, pause |
  | 8–13 | EDEV(1..6) | dots: half-width of the uniform spread of each element (prompt: "STD DEVIATIONS"; see KEY) |
  | 14 | AVAR | AU² per key-down: variance of the amplitude step |
  | 15 | BVAR | AU² per 8 kHz sample: variance of the fading innovation |
  | 16 | FCHIRP | Hz: chirp frequency offset at key-down |
  | 17 | TCHIRP | ms: chirp decay time constant |
  | 18 | PHIVAR | rad² per key-down: variance of the phase step |
  | 19 | GAMMA | per 8 kHz sample: fading AR(1) coefficient |
  | 20 | FC ("FREQ") | Hz: carrier frequency |
  | 21 | RNOISE ("NOISE") | AU² per 8 kHz sample: noise variance |

- **Computation:** ASIGMA = √AVAR, BSIGMA = √BVAR, PHISGM = √PHIVAR, RSIGM = √RNOISE (02400–02700);
  DMEAN = 1200/RATE ms, the dot length Δ (02900); WC = 6.28319·FC rad/s (03000); WCHIRP = 6.28319·FCHIRP rad/s
  (03100). If ESEP(1) = 0, ESEP is set to 1, 3, 1, 3, 7, 14 (03400–04000).
- **Notes:** 6.28319 stands for 2π throughout the listing (relative error +7.5·10⁻⁷) [derived].

### INITL (pp. 160–162, 04900–13500)

- **Purpose:** the decoder's tables; reads the letter tree and the text file.
- **COMMON:** /BLKLAM/ IELMST(400), ILAM1(16), ILAMX(6); /BLKRAT/ MEMDEL(6,6); /BLKELM/ ELEMTR(16,6);
  /BLKSPD/ RTRANS(5,2), MEMPR(6,6); /BLKMEM/ MEMFCN(400,6); /BLKS/ ISX(6); /BLKTRN/ LTRMAP(400), IALPH(70),
  IBLANK(400); /BLKTXT/ ITEXT(200).
- **DATA** (each table below; the letter-tree rows and IALPH are in section (f)):
  - ISX = 1, 1, 0, 0, 0, 0: key state of elements 1–6 (06300).
  - ILAMX = 1, 1, 0, 0, 0, 0: the same, indexed by element (07500).
  - ILAM1 = 3, 4, 5, 6, 3, 4, 5, 6, 1, 2, 1, 2, 1, 2, 1, 2: the element of each element-state 1–16 (07400).
  - MEMFCN rows 1–16 (06400–07000), IELMST 1–16 (07200–07300), LTRMAP rows 1–16 (07700): section (f).
  - IALPH(70) (07800–08300): section (f). IBLANK = 400 × 0 (08400).
  - ELEMTR(16,6) (08700–09200), RTRANS(5,2) (09500), MEMDEL(6,6) (09600–09800), MEMPR(6,6) (09900–10000): below.
- **File input** [fact, code pp. 161–162 10300–13200]:
  - `OPEN(UNIT=20,FILE='MORSEM')`; for I = 1 … 300: `READ(20,30)` eight integers, `FORMAT(8I3)`:
    LTRMAP(I), IELMST(I), MEMFCN(I,1..6). IBLANK(I) = 1 if IELMST(I) is 3 or 7 (word-space states), 2 if it is 4
    or 8 (pause states), else unchanged (0). Then `ENDFILE 20`.
  - Rows 1–300 of LTRMAP, IELMST and MEMFCN are thus overwritten from the file; rows 301–400 keep their DATA value 0.
  - `OPEN(UNIT=20,FILE='OUTPUT')`: writes MEMFCN rows 1–300, `FORMAT(10X,6(I3,2X))` (a check listing); `ENDFILE`.
  - `OPEN(UNIT=20,FILE='TEXT')`: reads ITEXT(1..105), one `I2` value per line; `ENDFILE`. KEY never uses ITEXT
    (its text branch is unreachable, section (d)).

**ELEMTR(m, k):** probability that element k follows element-state m [fact, code p. 161 08700–09200]. The
element-states are Bell's second-order labels (previous element, current element) of Table XII [fact, p. 105]:

| m | Label | Current element ILAM1(m) | dot | dash | e-sp | c-sp | w-sp | pause | Row sum |
|---|---|---|---|---|---|---|---|---|---|
| 1 | e-sp after dot (·^) | 3 | .55 | .45 | 0 | 0 | 0 | 0 | 1 |
| 2 | c-sp after dot (·~) | 4 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 3 | w-sp after dot | 5 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 4 | pause after dot | 6 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 5 | e-sp after dash (−^) | 3 | .55 | .45 | 0 | 0 | 0 | 0 | 1 |
| 6 | c-sp after dash | 4 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 7 | w-sp after dash | 5 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 8 | pause after dash | 6 | .5 | .5 | 0 | 0 | 0 | 0 | 1 |
| 9 | dot after e-sp (^·) | 1 | 0 | 0 | .581 | .335 | .067 | .015 | 0.998 |
| 10 | dash after e-sp | 2 | 0 | 0 | .54 | .376 | .069 | .015 | 1 |
| 11 | dot after c-sp | 1 | 0 | 0 | .923 | .062 | .012 | .003 | 1 |
| 12 | dash after c-sp | 2 | 0 | 0 | .923 | .062 | .012 | .003 | 1 |
| 13 | dot after w-sp | 1 | 0 | 0 | .923 | .062 | .012 | .003 | 1 |
| 14 | dash after w-sp | 2 | 0 | 0 | .923 | .062 | .012 | .003 | 1 |
| 15 | dot after pause | 1 | 0 | 0 | .95 | .04 | .009 | .001 | 1 |
| 16 | dash after pause | 2 | 0 | 0 | .95 | .04 | .009 | .001 | 1 |

Only element changes that change the key state have nonzero probability (marks go to spaces, spaces to marks).
Staying in the current element is handled separately by XTRANS [derived].

**MEMDEL(from, to):** speed step in WPM when the element changes from `from` to `to` [fact, code p. 161
09600–09800]. **MEMPR(from, to):** which column of RTRANS gives the probabilities (0: no transition)
[fact, code p. 161 09900–10000]. **RTRANS(i, c):** probability of speed branch i [fact, code p. 161 09500].

| from \ to | dot | dash | e-sp | c-sp | w-sp | pause |
|---|---|---|---|---|---|---|
| dot | 0 / 0 | 0 / 0 | 2 / 1 | 2 / 1 | 2 / 1 | 2 / 1 |
| dash | 0 / 0 | 0 / 0 | 2 / 1 | 2 / 1 | 2 / 1 | 2 / 1 |
| e-sp | 2 / 1 | 2 / 1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| c-sp | 2 / 2 | 2 / 2 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| w-sp | 5 / 1 | 5 / 1 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| pause | 10 / 2 | 10 / 2 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

(Each cell: MEMDEL / MEMPR.)

| Speed branch i | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| speed change | −2·MEMDEL | −MEMDEL | 0 | +MEMDEL | +2·MEMDEL |
| RTRANS(i, 1) | .1 | .2 | .4 | .2 | .1 |
| RTRANS(i, 2) | .15 | .2 | .3 | .2 | .15 |

So after a dot, dash or e-sp the speed changes by 0, ±2 or ±4 WPM with probabilities .4, .2, .1; after a c-sp by
the same steps with .3, .2, .15; after a w-sp by 0, ±5, ±10 WPM (.4, .2, .1); after a pause by 0, ±10, ±20 WPM
(.3, .2, .15). This is Table X exactly [fact, p. 104; derived].

### SIMSG1(X, SIG) (p. 163, 00100–04200)

- **Purpose:** one 8 kHz sample of the received signal: keyed carrier with amplitude steps, fading, phase steps,
  chirp, plus white Gaussian noise.
- **Arguments:** X (out: key state x_n, 1 or 0, set by KEY); SIG (out: s_n, AU).
- **COMMON:** /BLK1/ TAU; /BLK2/ WC, WCHIRP, ASIGMA, BSIGMA, PHISGM, RSIGM, TCHIRP, GAMMA.
- **DATA:** XLAST = 1, BETA = 1 (00600); AMP = 1 AU, BFADE = 0 AU, THETA = 0 rad, PHI = 0 rad (00700).
- **Computation,** per sample n, in this order [fact, code p. 163 00900–03700]:
  1. KEY is called with DUR = β (samples since the last key change) and returns x_n.
  2. β ← β·(1 − x_n − x_{n−1} + 2·x_n·x_{n−1}) + 1: it continues when x is unchanged and restarts at 1 when x
     changes. TK = x_n·(1 − x_{n−1}) is 1 at a key-down, else 0.
  3. Amplitude step: AMP ← AMP + TK·w_A, w_A ~ N(0, AVAR); then AMP ← max(AMP, 0.01 AU).
  4. Fading: BFADE ← GAMMA·BFADE + w_B, w_B ~ N(0, BVAR). If AMP + BFADE < 0.001 AU, BFADE ← 0.001 − AMP (the
     fading state itself is clamped). The amplitude is a_n = AMP + BFADE ≥ 0.001 AU (AMPB).
  5. Chirp: t_el = 1000·TAU·β ms (time since the key change); ω_ch = x_n·WCHIRP·exp(−t_el/TCHIRP) rad/s.
  6. Phase: θ ← (θ + (WC + ω_ch)·TAU) mod 6.28319; φ ← (φ + TK·w_φ) mod 6.28319, w_φ ~ N(0, PHIVAR).
  7. s_n = x_n·a_n·sin(θ + φ) + w_n, w_n ~ N(0, RNOISE).
- **Notes:**
  - RANDN is called four times per sample, in the order w_A, w_B, w_φ, w_n, whatever TK is [fact, code p. 163
    01500, 01900, 03000, 03600].
  - θ is advanced before use, so sample n has carrier phase n·WC·TAU when there is no chirp; RCVR's oscillator
    starts and advances identically (section (c)) [derived].
  - XLAST starts at 1 and the first element is a mark, so the first element is one sample shorter than the rule
    in KEY gives, and no amplitude or phase step happens at the first key-down [derived].
  - TCHIRP must not be 0 even when FCHIRP = 0 (division in step 5) [derived].

### KEY(DUR, X) (pp. 163–165, 04400–12300)

- **Purpose:** the sender: which element is keyed now, and how long it lasts.
- **Arguments:** DUR (in: SIMSG1's β, samples at 8 kHz); X (out: key state; unchanged unless an element ends).
- **COMMON:** /BLKEND/ IEND; /BLK1/ TAU; /BLK6/ DMEAN, XDUR, ESEP, EDEV; /BLKTXT/ ITEXT.
- **DATA:**

  | Constant | Value | Line | Meaning |
  |---|---|---|---|
  | IK | octal 001000000000 = 2²⁷ = 134 217 728 | 05000 | argument (seed) of the library function RAN |
  | LTR | 20 | 05100 | first letter: column 20 of MORSE, T |
  | NELM, N, NLTR | 0, 0, 1 | 05100 | element index in the letter; element count of the run; text index |
  | MORSE(10,40) | element codes of 36 symbols, then 4 empty columns | 05200–07000 | below |
  | ISYMBL | `.`, `_`, blank, `/`, `;`, `\` | 07100 | printed symbol of elements 1–6 |
  | thresholds | 0.9, 0.8 | 08100–08200 | choice of the letter-ending space |
  | letter draw | 35, 0.001 | 08400 | the next letter's column, step 3 below |
  | run length | 300 elements | 09900 | elements per run |
  | duration floor | 20 ms | 11800 | shortest element |

- **MORSE table.** Column c holds the element codes of symbol c, alternating marks with 3 (e-sp) and padded with 0
  [fact, code pp. 163–164 05200–07000]. Columns 1–36 are A–Z, 1–9, 0 in the order of IALPH(1..36) (section (f)),
  in correct International Morse, with one exception: **column 3 (C) is printed 2,3,1,3,1,3,1, the code of B
  (−···)**; C is −·−· [fact, code p. 163 05300; checked on the image, `transcription-log.md`]. Columns 37–40 are
  zero.
- **Computation** [fact, code pp. 164–165 07300–12000]:
  1. Elapsed time in the element: 1000·TAU·DUR ms = DUR/8 ms. If it is below XDUR, return (X unchanged).
  2. Otherwise the element ends: NELM ← NELM + 1, IELM = MORSE(NELM, LTR).
  3. If IELM = 0 (the letter's code is exhausted): NELM ← 0; draw Y = RAN; the letter-ending space is a w-sp if
     Y > 0.9, a pause if 0.8 < Y ≤ 0.9, else a c-sp. Draw Y = RAN again; the next letter is
     LTR = int(35·(Y − 0.001) + 1) + 1. Then go to step 4. (Lines 08900–09500, which would read the next letter and
     space from ITEXT, follow the unconditional `GO TO 100` at 08800 and carry no label: they are never executed.)
  4. Count the element: N ← N + 1, IOUT(N) = ISYMBL(IELM). At N = 300: N ← 0, NLTR ← 0, IEND ← 1, type the run's
     element string (10 lines of 50 symbols) and wait for the operator.
  5. Duration: mean XM = ESEP(IELM)·DMEAN ms, half-width XSIGM = EDEV(IELM)·DMEAN ms; Y = 2·(RAN − 0.5), uniform on
     (−1, 1); XDUR = XM + Y·XSIGM, floored at 20 ms. X = 1 for a dot or dash, 0 for a space.
- **What it produces** [derived]:
  - **Durations** are uniform on (m − e, m + e)·Δ for an element of mean m dots and EDEV e, floored at 20 ms, with
    no ceiling. The prompt calls EDEV a "standard deviation"; under this law the standard deviation is e/√3 dots.
  - **Quantization.** An element ends at the first sample at which DUR/8 ms ≥ XDUR, so it lasts ⌈8·XDUR/ms⌉
    samples of 0.125 ms (the first element of a session one sample less).
  - **Letters.** With RAN uniform on (0, 1): LTR = 1 (A) needs Y < 0.001, probability 0.001; LTR = 2 … 35 have
    probability 1/35 = 0.028571 each; LTR = 36 (0) has 1/35 − 0.001 = 0.027571. C (LTR = 3) is keyed as B, so the
    keyed symbols are B with probability 2/35, C never, A almost never.
  - **Spaces.** Each letter is followed by a c-sp with probability 0.8, a w-sp 0.1, a pause 0.1.
  - **Averages** over this distribution: 3.711 marks and 7.42 elements per letter, 14.40 dots per letter including
    its ending space (mean 4.5 dots); 40.4 letters per 300-element run; 200 letters last 173 s at 20 WPM, 115 s at
    30 WPM, 69 s at 50 WPM.
- **Notes:**
  - IALPH(43) = 'X' repeats IALPH(24) (section (f)); KEY has no column for code 43, so it never sends it.
  - The 20 ms floor is the dot length at 60 WPM, the top of the decoder's speed range [derived; inference that it
    was meant so].
  - The printed "input data" of a run is this element string (`.`, `_`, blank, `/`, `;`, `\`), not letters
    [fact, code p. 164 09800, 10500–10900].

### RCVR(ZIN, ZOUT) (p. 189, 00100–02800)

- **Purpose:** moves the carrier from WC to 1000 Hz through a complex baseband with a one-pole low-pass
  ("converts the input signal at radian freq WC to 1000 Hz", comment 00500–00600).
- **Arguments:** ZIN (in: s_n, AU); ZOUT (out, AU).
- **COMMON:** /BLK1/ TAU; /BLK2/ WC (the first word of the block only).
- **DATA:** THETA = 0, THETLO = 0 (01200). ZILP and ZQLP have no DATA and start at 0 (section (h)).
- **Computation** [fact, code p. 189 01400–02500]:
  - θ ← (θ + WC·TAU) mod 6.28319; i = ZIN·cos θ, q = ZIN·sin θ;
  - i_LP ← i_LP + 0.070·(i − i_LP), q_LP ← q_LP + 0.070·(q − q_LP), i.e. y_n = 0.93·y_{n−1} + 0.07·x_n;
  - θ_LO ← (θ_LO + 6283.2·TAU) mod 6.28319; ZOUT = i_LP·cos θ_LO + q_LP·sin θ_LO.
- **Constants:** 0.070 (01900, 02000); 6283.2 rad/s (02200), i.e. 1000.0023 Hz; 6.28319 (01500, 02300).
- Derived responses: section (c).

### BPFDET(ZIN, Z) (pp. 189–190, 03700–09100)

- **Purpose:** band-pass filter, rectifier and envelope low-pass ("a simple cascade of two 2-pole digital resonators
  at a center freq of 1000 Hz … a three-pole Chebyschev 100 Hz LPF", comment 04100–04500).
- **Arguments:** ZIN (in: RCVR output, AU); Z (out: envelope, AU). Z also holds the filter's previous output
  between calls.
- **DATA** [fact, code p. 189 05200–05400]:

  | Constant | Value | Role |
  |---|---|---|
  | C1, C2, C | 1.2726, 0.8100, 0.1900 | first resonator (input ZIN) |
  | CK1, CK2, CG | 1.37158, 0.9409, 0.0150 | second resonator (input: first resonator's output) |
  | A(1) | 0.000030051 | low-pass numerator gain |
  | A(2), A(3), A(4) | 2.9507982, 2.90396345, −0.953135172 | low-pass feedback |

- **Computation** [fact, code pp. 189–190 05900–08700], with all states starting at 0:
  - first resonator: w_n = 1.2726·w_{n−1} − 0.8100·w_{n−2} + 0.1900·ZIN_n;
  - second resonator: v_n = 1.37158·v_{n−1} − 0.9409·v_{n−2} + 0.0150·w_n (ZBPF = v_n);
  - rectifier: XDET = SQRT(ZBPF²) = |v_n|, a full-wave rectifier (the comment says "square-law");
  - low-pass: Z_n = A₁·(d_n + 3d_{n−1} + 3d_{n−2} + d_{n−3}) + A₂·Z_{n−1} − A₃·Z_{n−2} − A₄·Z_{n−3}, with
    d = XDET. With A₄ < 0 the last term is +0.953135172·Z_{n−3}.
- **Order in the signal path:** the wide resonator (pole radius 0.90) comes first, the narrow one (0.97) second
  [fact, code p. 190 06100, 06500]. Derived responses: section (c).

### NOISE(ZIN, RN, Z) (pp. 190–191, 09600–15000)

- **Purpose:** estimates the noise level of the envelope, removes it, and supplies the measurement variance R.
- **Arguments:** ZIN (in: the envelope Z at the decimated sample, AU); RN (out: R, AU²); Z (out: z, AU).
- **DATA** [fact, code p. 190 11100–11300]: YLONG = 200 × 1.0, YSHORT = 50 × 1.0 (AU); KL = KKL = KS = KKS = 1;
  YMIN1 = YMIN2 = 1.0; YMAVG = 0.05 (AU).
- **Computation,** per 5 ms step [fact, code pp. 190–191 11500–14700]:
  1. Advance the circular indices: KL cycles 2 … 200, 1, …; KS cycles 2 … 50, 1, …; the scan lengths KKL and KKS
     grow by one per step up to 200 and 50.
  2. If KKS > 2 (from the second call on): store ZIN at YLONG(KL) and YSHORT(KS), and set YMIN1 = YMIN2 = ZIN.
  3. YMIN1 = min(YMIN1, YLONG(1..KKL)); YMIN2 = min(YMIN2, YSHORT(1..KKS)); YMIN = min(YMIN1, YMIN2).
  4. YMAVG ← YMAVG + 0.004·(YMIN − YMAVG).
  5. R = 0.30·YMAVG, floored at 0.005 AU².
  6. z = 1.1·(ZIN − 2.4·YMAVG − 0.05).
- **What it computes** [derived]:
  - YSHORT holds the last 50 values, which are also among YLONG's last 200, so YMIN2 ≥ YMIN1 always and YSHORT
    never changes the result. YMIN is the minimum of the envelope over the last 200 decoder samples (1.0 s),
    current sample included. (During the first 200 steps the initial values 1.0 AU take part, harmlessly unless
    the envelope exceeds 1.0 AU throughout.)
  - YMAVG is an exponential average of that minimum with coefficient 0.004 per 5 ms step: time constant
    5 ms/0.004 = 1.25 s.
  - The subtracted level 2.4·YMAVG + 0.05 is the estimate of the noise-only mean of the envelope, μ_n; R is
    proportional to YMAVG, not to μ_n²; z is scaled by 1.1 but R is not.
  - The floor makes R ≥ 0.005 AU², so the Kalman innovation variance R + h·P⁻ is never 0.
- **Constants:** 200 and 50 samples (10900); 0.004 (14300); 0.30 and 0.005 (14500–14600); 1.1, 2.4, 0.05 (14700).
  Bell calls the scale "selected empirically" [fact, p. 124]; their class is heuristic.
- Calibration against S₁₀₀: section (c).

### PROCES(Z, RN, XHAT, PX, ELMHAT, LTRHAT) (pp. 167–168, 00100–11300)

- **Purpose:** one decoder step per 5 ms (section (a)).
- **Arguments:** Z (in: z, AU); RN (in: R, AU²); XHAT (out: 1 if PX > 0.5, else 0); PX (out: posterior probability
  that the key is down); ELMHAT, LTRHAT (outputs never assigned).
- **Locals** (kept between calls; they are the decoder's state):

  | Array | Size | Meaning | Unit | Initial value (DATA, lines 05500–06100) |
  |---|---|---|---|---|
  | ISAVE | 1 | number of saved paths | | 25 |
  | LAMBDA | 25 | letter-tree state λ of each saved path | | 25 × 5 |
  | DUR | 25 | time in the current element | ms | 25 × 1000 |
  | ILRATE | 25 | speed of each saved path | WPM | 5 × 10, 5 × 20, 5 × 30, 5 × 40, 5 × 50 |
  | P | 750 | posterior of saved paths (1..ISAVE) or of new nodes | | 750 × 1 |
  | PIN | 25 × 30 | transition probabilities, path × successor | | |
  | LAMSAV, DURSAV, ILRSAV | 750 | state, duration (ms), speed (WPM) of each new node | | 750 × 5, –, 750 × 20 |
  | LKHD | 750 | likelihood of each new node | AU⁻¹ | |
  | PATHSV | 25 | parent path of each saved node | | 25 × 5 |
  | SORT | 25 | node index of each saved node | | |

- **Indexing:** successor n = (i_r − 1)·6 + k of a path, for element k = 1 … 6 and speed branch i_r = 1 … 5; new
  node j = (path − 1)·30 + n, j ≤ 750 [fact, code p. 169 17200, p. 173 40100–40500].
- **Line 07400** is cut off at the right edge of the reproduced printout after `…,DURSAV,ILR` [fact,
  `listing.txt`]. It reads `CALL PATH(IPATH,LAMBDA(I),DUR(I),ILRATE(I),LAMSAV,DURSAV,ILRSAV)` [inference]. Grounds:
  PATH has exactly seven dummy arguments, the seventh being ILRSAV(750), "new data rates for each path
  extension" [fact, code p. 173 36400, 37800, 38500]; the six printed arguments match PATH's first six in order;
  PROCES's only 750-element array beginning with ILR is ILRSAV (05100); line 07500 starts a new statement (no
  continuation mark), so the statement and its closing parenthesis end on 07400. The printed statement is 60
  characters long and begins at Fortran column 7 to 9 (depending on how the line was indented), so the missing
  `SAV)` ends at column 70 to 72, inside the 72-column statement field, and a further argument would not fit
  [derived]. Passing ILRATE instead would let PATH write 750 elements into a 25-element
  array. Certainty: as high as an inference can be; the program cannot be correct otherwise.
- **Notes:** the debug printout (09800–10400) is skipped by `GO TO 1` (09750).

### TRPROB(IP, LAMBDA, DUR, ILRATE, P) (pp. 169–170, 12300–18300)

- **Purpose:** transition probabilities from saved path IP to its 30 successors.
- **Arguments:** IP (path index); LAMBDA (its state); DUR (ms); ILRATE (WPM); P (out: PROCES's PIN(25,30)).
- **COMMON:** /BLKLAM/.
- **Computation** [fact, code p. 169 15400–17900]:
  - If λ = 0 (a dead path): P(IP, n) = 0 for all n.
  - Else e = ILAM1(IELMST(λ)) (the path's current element), p_stay = XTRANS(e, DUR, ILRATE); for k = 1 … 6 and
    i_r = 1 … 5, PTRANS gives the unnormalized π_n and their sum Σ; P(IP, n) = π_n/Σ.

### XTRANS(IELEM, D0, IRATE) (function; pp. 170–171, 18600–24100)

- **Purpose:** the probability that the current element lasts at least one more 5 ms step.
- **Arguments:** IELEM (element e); D0 (ms in the element so far); IRATE (speed, WPM).
- **Constants:** KIMAP = 1, 3, 1, 3, 7, 14 (20500): nominal length m of each element in dots; APARM = 3.000, 1.500,
  1.000 (20600); 1200 (21200); 5 ms (21400).
- **Computation** [fact, code p. 170 21100–23800]: m = KIMAP(e); Δ = 1200/IRATE ms; b₀ = D₀/(m·Δ),
  b₁ = (D₀ + 5 ms)/(m·Δ); α_m = 3.0·m for e = 1 … 4, 7·1.5 = 10.5 for e = 5, 14·1.0 = 14 for e = 6. Then

  | Case | p_stay |
  |---|---|
  | b₁ ≤ 1 | (1 − ½e^{α_m(b₁−1)}) / (1 − ½e^{α_m(b₀−1)}) |
  | b₀ < 1 < b₁ | ½e^{−α_m(b₁−1)} / (1 − ½e^{α_m(b₀−1)}) |
  | b₀ ≥ 1 | e^{−α_m(b₁−b₀)} |

- **Meaning** [derived]: this is the survival ratio Pr[L ≥ D₀ + 5 ms | L ≥ D₀] for an element length L whose
  density, in bauds, is Laplacian with mode m and rate α per baud, α = α_m/m: **3.0 per baud** for dot, dash, e-sp
  and c-sp, **1.5** for the w-sp, **1.0** for the pause. The text derives 3.61, 1.81 and 0.90 per baud (p. 98);
  section (g). With α = 3.0 per baud, a dot outlasts 2 dots with probability ½e^{−3} = 0.0249.

### PTRANS(KELEM, IRATE, LAMBDA, ILRATE, PTRX, PSUM, PIN, N) (pp. 171–172, 24600–30200)

- **Purpose:** one unnormalized successor probability.
- **Arguments:** KELEM (successor's element k); IRATE (its speed branch i_r); LAMBDA (path state); ILRATE (path
  speed, WPM); PTRX (p_stay); PSUM (in/out: running sum); PIN (out, 30); N (successor index).
- **COMMON:** /BLKLAM/, /BLKELM/.
- **Computation** [fact, code p. 171 27800–29900], with e = ILAM1(IELMST(λ)):
  - k = e (stay in the element): π_n = p_stay if i_r = 3, else 0.
  - k ≠ e: π_n = (1 − p_stay)·ELEMTR(IELMST(λ), k)·SPDTR(i_r, ILRATE, k, e).
  - PSUM ← PSUM + π_n.
- **Note:** PTRANS does not consult MEMFCN. A successor the letter tree does not allow (MEMFCN = 0) still gets its
  probability; PATH then gives it state 0 (section (f)) [derived].

### SPDTR(ISRT, ILRT, ISELM, ILELM) (function; p. 172, 30800–35500)

- **Purpose:** speed-transition probability.
- **Arguments:** ISRT (speed branch i_r of the successor); ILRT (path speed, WPM); ISELM (successor element);
  ILELM (path element).
- **COMMON:** /BLKSPD/ RTRANS, MEMPR; /BLKRAT/ MEMDEL.
- **Computation** [fact, code p. 172 33300–35200]: if ILELM = ISELM: 1 for i_r = 3, else 0. Otherwise: if
  MEMPR(ILELM, ISELM) = 0, 0; else RTRANS(i_r, MEMPR(ILELM, ISELM)), set to 0 if the new speed
  ILRT + (i_r − 3)·MEMDEL(ILELM, ISELM) is above 60 or below 10 WPM.
- **Note:** the zeroed out-of-range branches are not redistributed here; TRPROB's normalization over all 30
  successors rescales the rest [derived].

### PATH(IP, LAMBDA, DUR, ILRATE, LAMSAV, DURSAV, ILRSAV) (pp. 173–174, 36400–43600)

- **Purpose:** the labels of the 30 successors of path IP.
- **COMMON:** /BLKLAM/, /BLKMEM/ MEMFCN, /BLKS/ ISX, /BLKRAT/ MEMDEL.
- **Computation** [fact, code pp. 173–174 39600–43300], for i_r = 1 … 5, k = 1 … 6, node j:
  - if λ = 0: LAMSAV(j) = 0;
  - else LAMSAV(j) = MEMFCN(λ, k); if that is 0, nothing else is written for j (DURSAV(j) and ILRSAV(j) keep their
    old values);
  - else, with x_old = ILAMX(e) and x_new = ISX(k): DURSAV(j) = DUR·(1 − x_new − x_old + 2·x_new·x_old) + 5 ms,
    i.e. DUR + 5 ms if the key state is unchanged and 5 ms if it changes; ILRSAV(j) = ILRATE + (i_r − 3)·MEMDEL(e, k)
    WPM (not clipped to 10–60; out-of-range nodes have probability 0).

### LIKHD(Z, RN, IP, LAMBDA, DUR, ILRATE, P, LKHD) (pp. 174–175, 44100–50900)

- **Purpose:** the likelihood of z for each of path IP's 30 successors.
- **COMMON:** /BLKLAM/, /BLKS/.
- **Computation** [fact, code pp. 174–175 47800–50000]: e = ILAM1(IELMST(λ)), x_old = ILAMX(e); for k = 1 … 6,
  i_r = 1 … 5: x_new = ISX(k), p = P(IP, n), `KALFIL(Z,IP,RN,x_old,x_new,e,j,i_r,DUR,ILRATE,p,L)`; LKHD(j) = L.
- **Notes:**
  - KALFIL's KELEM argument is the saved path's element e, though KALFIL's comment calls it the "elem state of new
    node" [fact, code p. 174 47800, p. 176 01500]; MODEL uses it as the element being left. The rate passed is the
    branch index i_r, which MODEL ignores [fact, code p. 175 49000, p. 177 09500–p. 178 14500].
  - For λ = 0, `IELMST(0)` is read outside the array (section (h)); the value is not used, because every p is 0.
  - The `TYPE` at 50300 follows `GO TO 100` and is never executed.

### KALFIL(Z, IP, RN, ILX, IXS, KELEM, JNODE, ISRATE, DUR, ILRATE, PIN, LKHDJ) (pp. 176–177, 00100–08800)

- **Purpose:** one scalar Kalman step on the amplitude for one successor, and its likelihood.
- **COMMON:** /BLKSV1/ YKKIP(25), PKKIP(25), YKKSV(750), PKKSV(750): the saved paths' estimates and the new nodes'.
- **DATA:** YKKIP = 25 × 0.5 AU; PKKIP = 25 × 0.10 AU² (03700); PINMIN = 0.00010 (03800).
- **Computation** [fact, code pp. 176–177 04400–08300]:
  - If PIN ≤ 10⁻⁴: L = 0, and YKKSV, PKKSV of the node are not written.
  - Else MODEL gives Φ, Q, h. With ŷ = YKKIP(IP), P = PKKIP(IP):
    ŷ⁻ = Φ·ŷ; P⁻ = Φ²·P + Q; S = h·P⁻ + R; G = P⁻·h/S; P⁺ = (1 − G·h)·P⁻; ν = z − h·ŷ⁻;
    ŷ⁺ = ŷ⁻ + G·ν, raised to 0.01 AU if it is ≤ 0.01; a = ν²/(2S); L = 0 if a > 1000, else L = S^{−1/2}·e^{−a}.
  - Units: ŷ AU, P and S AU², L in AU⁻¹ (a Gaussian density without its constant 1/√(2π)).
- **Notes:** h is 0 or 1, so h·P⁻ equals h²·P⁻. ILX is passed but not used. The `TYPE` at 08500 is never executed.

### MODEL(DUR, IELM, ILR, ISR, IXS, PHI, QA, HZ) (pp. 177–178, 09500–14500)

- **Purpose:** the amplitude model of a transition: Φ, Q per 5 ms step, and the measurement coefficient h.
- **Arguments:** DUR (ms in the element being left); IELM (that element); ILR (path speed, WPM); ISR (unused);
  IXS (key state of the successor); PHI, QA, HZ (out).
- **Constants:** 1200 (12400); 14 bauds (12600, 14100); 0.0001 (12900); 0.15, 0.6, 14 (13500); 0.01, 0.2
  (13600); 22.4, −2 (13900–14000).
- **Computation** [fact, code pp. 177–178 11900–14200]: h = IXS. Δ = R1 = 1200/ILR ms; B = min(DUR/Δ, 14)
  bauds.
  - Leaving a mark (IELM ≤ 2), to any successor: Φ = 1, Q = 10⁻⁴ AU².
  - Leaving a space for a mark (IELM ≥ 3, IXS = 1): Φ = 1, Q(B) = 0.15·e^{0.6(B−14)} + 0.01·B·e^{0.2(1−B)} AU².
  - Staying in a space (IELM ≥ 3, IXS = 0): Φ = 10^{−2/(22.4·Δ/ms)} per step, Φ = 1 once B ≥ 14; Q = 0.
- **Values** [derived]:

  | B (bauds) | 0.5 | 1 | 2 | 3 | 5 | 7 | 10 | ≥ 14 |
  |---|---|---|---|---|---|---|---|---|
  | Q(B), AU² per 5 ms step | 0.00557 | 0.01006 | 0.01649 | 0.02031 | 0.02314 | 0.02333 | 0.03014 | 0.16040 |

  The key-up decay: over a space of B bauds (B ≤ 14) the amplitude estimate is multiplied by
  Φ^{BΔ/5 ms} = 10^{−B/56}, that is −0.357 dB of amplitude per baud, independent of speed, and at most
  10^{−0.25} (−5.0 dB of amplitude relative to the space's start) at B = 14. The 1/e time is 24.3 dots. Per 5 ms
  step, Φ = 0.99829 at 10 WPM, 0.99658 at 20 WPM, 0.99487 at 30 WPM, 0.99147 at 50 WPM, 0.98977 at 60 WPM.
- **Notes:** B uses the duration before this step and the saved path's speed. MODEL does not distinguish an e-sp
  from the other spaces (the text does, section (g)).

### PROBP(P, PIN, ISAVE, LKHD) (pp. 178–179, 15100–20300)

- **Purpose:** posterior probability of every new node (the thesis's equation 26, p. 121).
- **Computation** [fact, code pp. 178–179 17200–20000]: for saved path i and successor n, j = (i − 1)·30 + n:
  p_j = P(i)·PIN(i, n)·LKHD(j); then P(j) = p_j/Σ p over j = 1 … 30·ISAVE. (PMAX is computed but not used.)
- **Note:** P(1 … ISAVE) are overwritten only after all p_j are formed, so the input probabilities are intact
  during the loop [derived].

### SPROB(P, ISAVE, ILRSAV, PELM, KHAT, SPDHAT, PX) (pp. 179–180, 20900–26100)

- **Purpose:** marginal posteriors (p. 121).
- **Computation** [fact, code pp. 179–180 23300–25800]: PSELEM(k) = Σ over paths and speed branches of P(j) for
  element k; PX = Σ over k ≤ 2 (key down); SPDHAT = Σ ILRSAV(j)·P(j) (WPM); PELM = max_k PSELEM(k) and KHAT its
  element (the last one on ties).
- **Notes:** only PX is used afterwards (XHAT in PROCES). SPDHAT includes nodes of state 0 whose ILRSAV was not
  rewritten (PATH), so it is a display value only [derived].

### SAVEP(P, PATHSV, ISAVE, IMAX, LAMSAV, DURSAV, ILRSAV, LAMBDA, DUR, ILRATE, SORT) (pp. 180–183, 26400–43100)

- **Purpose:** keeps the most probable new nodes as the next step's paths (pp. 121–122).
- **COMMON:** /BLKSV1/ (moves the Kalman estimates of the kept nodes into the saved slots).
- **Constants:** POPT = 0.90 (30800); 0.000001 (32500); 25 paths (36200).
- **Computation** [fact, code pp. 181–183 31000–42800]:
  1. **One per element.** For k = 1 … 6: the node of element k with the largest P over all paths and speed
     branches (on ties the last in index order, test `.LT.`); it is kept if P ≥ 10⁻⁶.
  2. **Fill.** Repeat: among nodes not yet kept, the one with the largest P (on ties the first, test `.LE.`) is kept;
     stop when the kept total ≥ 0.90 or 25 nodes are kept. At least one node is added here even if step 1 already
     reached 0.90. If no unkept node has P > 0, the node found last is added again with P = 0 (it is removed in
     step 4).
  3. **Re-index.** ISAVE = number kept; P(i) = P(kept i)/Σ kept; LAMBDA, DUR, ILRATE, YKKIP, PKKIP of slot i are
     copied from the kept node; PATHSV(i) is its parent path. The order is the order of selection: the
     element-best nodes in element order, then the rest by decreasing probability.
  4. **Duplicates.** A kept node with the same (ILRATE, DUR, LAMBDA) as an earlier kept node is deleted. Its
     probability is dropped, not added to the survivor, and the survivor keeps its own Kalman estimate.
  5. **Renormalize.** The survivors are moved up; PSUM is summed over slots 2 … ISAVE only, and every P(i), including
     P(1), is divided by it. IMAX is the slot with the largest P (the first on ties).
- **Notes** [derived]:
  - Bounds: step 1 keeps at least one node (some node has P ≥ 1/750), step 2 at least one more, so at least 2 nodes
    before step 4; at most 25. The comment's "minimum of 7 paths" (27100) is not what the code does.
  - Step 5 omits P(1) from the sum, so the saved probabilities sum to more than 1. PROBP renormalizes at the next
    step, and the ratios between paths are unchanged, so decoding is unaffected. If step 4 leaves a single path,
    PSUM = 0 and the division fails (a latent defect; not observed to matter).
  - The "duplicates" are equal in state, duration and speed but may differ in their amplitude estimates.

### TRELIS(ISAVE, PATHSV, LAMBDA, IMAX) (pp. 184–186, 00100–16200)

- **Purpose:** the trellis of saved nodes, the decision, and the call of TRANSL (p. 123).
- **COMMON:** /BLKEND/ IEND.
- **DATA:** PTHTRL = 5000 × 5 and LMDSAV = 5000 × 5 (02100): a circular store of 200 stages × 25 nodes, holding each
  node's parent index and state; N = 0, NDELAY = 200 stages (02200); IPNOD = 25 × 1, NCALL = NMAX = MMAX = 0 (02300).
- **Computation** [fact, code pp. 184–186 02800–16100]:
  1. Statistics for the display: running means of ISAVE and of the decision delay; at IEND = 1 they are typed and
     the operator is awaited.
  2. **Store:** N ← N + 1, wrapping 200 → 1; stage N holds PTHTRL(N, i) = PATHSV(i) and LMDSAV(N, i) = λ_i.
  3. **Trace back:** IPNOD(i) = i. For K = 1, 2, …: if K = 200, go to the forced decision. Otherwise replace each
     IPNOD(i) by its parent, PTHTRL(stage N − K + 1, IPNOD(i)), an index into stage N − K; IPMAX is the value for
     the best path IMAX. If all IPNOD are equal, the paths converge at stage N − K: NDEL = K + 1.
  4. **Converged:**
     - NDEL = NDELST + 1: the same node as at the last call; nothing to translate.
     - NDEL = NDELST: one new stage; translate the node at stage N − K.
     - otherwise: translate, oldest first, the nodes on the convergent path from stage N − NDELST + 1 to stage
       N − NDEL + 1 (`DO 400 K=NDEL,NDELST`, then reversed).
  5. **Forced decision** (no convergence within 199 stages): NDEL = 200; translate the node at stage N − 199 on the
     best path (LMDSAV(N − 199, IPMAX)); set λ = 0 for every saved path whose ancestor at that stage is not IPMAX.
  6. NDELST = NDEL.
- **Notes** [derived]:
  - A converged decision has a delay of K = 1 … 198 samples; the forced decision reads the node **199 samples
    (995 ms) back**. The text says 200 samples, 1 s (p. 123).
  - Paths set to λ = 0 die at the next step (TRPROB gives them no successors).
  - At the first call NDELST = 0 and the trace-back reaches the initial stages (all state 5), so NDEL > NDELST + 1
    and `DO 400 K=NDEL,NDELST` has a negative trip count: it runs once under FORTRAN-66 rules, not at all under
    FORTRAN-77 rules (section (h)). The node it would translate is in state 5, which prints nothing.
  - The decided sequence is always a valid path of the state graph, so TRANSL never receives state 0.

### TRANSL(LTR) (pp. 186–188, 16800–29400)

- **Purpose:** prints letters ("simple mapping of the decoded Morse symbols into the proper letters and numerals",
  p. 123).
- **COMMON:** /BLKTRN/ LTRMAP, IALPH, IBLANK; /BLKLAM/.
- **DATA:** ISPACE = blank; SPFLAG = 0; NCHAR = 0 (18500). IXLAST, LSTELM, LSTLTR start at 0.
- **Computation,** one decided state λ per call [fact, code pp. 187–188 19100–29100]: e = ILAM1(IELMST(λ)),
  x = ILAMX(e).
  - A letter is printed when the key state changes to 1 (x = 1, previous x = 0) and the previous decided element
    was a c-sp, w-sp or pause (≥ 4). The letter is that of the previous decided state λ_s (a letter-ending space
    state): code c = LTRMAP(λ_s), text IALPH(c), followed by NBLANK = IBLANK(λ_s) blanks (0 after a c-sp, 1 after
    a w-sp, 2 after a pause).
  - The print format depends on c:

    | Code c | Output |
    |---|---|
    | 43 | if the previous output ended without a blank: as below for ≤ 44; else two blanks, the character, a new line |
    | 1–42, 44 | the character (A1), then NBLANK blanks |
    | 45, 46 | two characters (A2), then NBLANK blanks |
    | 47–60 | two blanks, two characters, two blanks, then NBLANK blanks |
    | 61 | two blanks, three characters, two blanks |
    | 66 | new line, 10 blanks, two characters, new line, 10 blanks |
    | any other | two blanks, three characters, two blanks, then NBLANK blanks |

    A new line with 10 blanks follows when NCHAR reaches 52. Then x, e and λ are remembered.
- **Notes:**
  - A letter is printed only when the next letter's first mark is decided, so the last letter of a transmission is
    printed only if another mark follows [derived].
  - `IF(NBLANK.EQ.0) GO TO 110` jumps to the terminal statement of `DO 110` from outside the loop (also 210, 310,
    560); its effect is to skip the blanks (section (h)).
  - Lines 20300–20500 (printing the element number instead of the letter) are skipped by `GO TO 40` (20200).

### DISPLA, STATS, AUTOCR (p. 166)

- **DISPLA(S1, S2, S3, S4):** clears the screen, plots the four 512-point traces with the library routines PLOTR
  and VIEW, then waits for the operator (`ACCEPT`, 00900). XM is never assigned. Display only.
- **STATS(XIN1, …, XIN4, S1, …, S4, N):** stores four values at index N. Display only.
- **AUTOCR(S5, RS):** a running average of the autocorrelation of 500-point blocks. Never called.
- The traces are, top to bottom, the 8 kHz envelope Z, the decoder's z, PX and XHAT [fact, code p. 159 02500,
  p. 166 00400–00700]. Figure 18 shows the keying, Z, z and XHAT [fact, p. 149], so it was drawn with another
  choice of traces [inference].

## (c) The receiver chain, derived

**Method.** Each filter's transfer function is written from the listing's difference equation; responses, poles,
half-power points (−3 dB relative to the peak or DC gain), noise bandwidths and group delays are evaluated on the
unit circle in double precision (frequency grid 0.01 Hz or finer; noise bandwidths as Σh² of the impulse response,
20 000 samples, or as the integral of |H|²).
Time-domain values come from a sample-by-sample simulation of SIMSG1's carrier and noise, RCVR, BPFDET and NOISE
exactly as listed (240 s of carrier, 120 s of noise, 100 s of keyed signal). The scripts are working files and are
not committed; every number below is reproducible from the formulas given. f_s = 8000 Hz throughout.

### Signal path

```
s (AU, carrier at f_c) → RCVR: mix by e^{jθ}, one-pole low-pass on I and Q, remix at 1000.0023 Hz
  → resonator 1 (r = 0.90) → resonator 2 (r = 0.97) → |·| → 3-pole low-pass (DC gain 7.99) → Z (8 kHz)
  → every 40th sample → NOISE → z, R (200 samples/s)
```

### RCVR

- **Low-pass on I and Q:** H_L(z) = 0.070/(1 − 0.930·z⁻¹), DC gain 1 [fact, code p. 189 01900–02000].
  - Half power where cos ω = (1 + 0.93² − 2·0.07²)/(2·0.93) = 0.997366, i.e. **±92.44 Hz from the carrier**
    (184.9 Hz wide as a band-pass) [derived]. The text says "a single-pole 500 Hz BW band-pass filter" (p. 123).
  - Σh² = 0.07²/(1 − 0.93²) = 0.036269; noise bandwidth of the low-pass 145.08 Hz (one-sided), i.e. **290.2 Hz** as a
    band-pass around the carrier [derived].
  - Group delay at 0 Hz: 1.661 ms (13.29 samples) [derived].
- **Oscillators:** the down-mixer advances by WC·TAU per sample from 0, exactly as the simulator's carrier phase does
  without chirp; the up-mixer runs at 6283.2 rad/s = **1000.0023 Hz** [derived from code p. 189 01400, 02200].
- **Carrier gain:** with c = (i + j·q) = s·e^{jθ}, ZOUT = Re{c_LP·e^{−jθ_LO}}. A carrier a·sin(θ + φ) at f_c gives
  ZOUT = (a/2)·sin(θ_LO + φ): amplitude gain **1/2**, translated to 1000.0023 Hz, spectrum not inverted
  [derived]. The carrier phase θ cancels.
- **Image term:** the mixer also produces a component at 2f_c, attenuated by H_L(2f_c), which the up-mixer moves to
  2f_c − 1000.0023 Hz [derived].
  - For f_c far from 1000 Hz it falls outside the resonators (for f_c = 1500 Hz at 2000 Hz) and is negligible.
  - **For f_c = 1000 Hz it lands on 999.998 Hz** with relative amplitude |H_L(2000 Hz)| = 0.05126 (−25.8 dB
    relative to the DC gain): the output carrier amplitude is (a/2)·|1 + 0.05126·e^{jψ}|, between 0.474a and 0.526a,
    with ψ drifting through 2π every 1/(2 × 0.0023 Hz) = **213.8 s** [derived].
  - The same coherence makes the output noise variance at f_c = 1000 Hz vary slowly between 0.01628 and 0.01999
    AU² per AU² of input noise (period 213.8 s); for f_c away from 1000 Hz it is 0.018134 = Σh²/2 [derived].

### BPFDET resonators

H₁(z) = 0.19/(1 − 1.2726·z⁻¹ + 0.81·z⁻²), first in the path; H₂(z) = 0.015/(1 − 1.37158·z⁻¹ + 0.9409·z⁻²), second
[fact, code pp. 189–190 05300–06600]. Both pole pairs have cos θ = 0.7070000 (2r·cos θ = C1 and CK1), so both sit at
1000.19 Hz; the radii are √0.81 = 0.90 and √0.9409 = 0.97 [derived].

| Quantity | Resonator 1 (r = 0.90) | Resonator 2 (r = 0.97) | Cascade 1 × 2 |
|---|---|---|---|
| peak gain, at | 1.4140 at 993.10 Hz | 0.35888 at 999.60 Hz | 0.50692 at 999.10 Hz |
| gain at 1000 Hz | 1.41215 | 0.35886 | 0.50677 |
| −3 dB band (relative to the peak) | 849.47–1121.89 Hz (272.4 Hz) | 960.18–1037.83 Hz (77.6 Hz) | 962.10–1034.43 Hz (72.3 Hz) |
| (1 − r)·f_s/π | 254.6 Hz | 76.4 Hz | |
| noise bandwidth, one-sided, relative to the peak gain | 830.6 Hz | 243.4 Hz | 190.7 Hz |
| group delay at 1000 Hz | 1.069 ms | 3.981 ms | 5.050 ms |

[derived] The resonators have no zeros, so their skirts stay high (resonator 2 is −22.7 dB relative to its peak at
0 Hz); this is why their noise bandwidths are three times their half-power widths. The text gives 1.22 for the
ratio of two single-tuned circuits (p. 111); that holds for identical analog circuits, not for these.

### The whole pre-detection chain

From the input carrier to ZBPF, as a function of the offset δf of a component from the carrier (RCVR × 1/2 ×
cascade at 1000 + δf Hz) [derived]:

- gain at the carrier: 0.5 × 0.50677 = **0.25339**;
- −3 dB band, relative to the gain at the carrier: **δf = −33.47 … +30.93 Hz, 64.40 Hz wide** (slightly
  asymmetric: the cascade peaks at 999.10 Hz);
- noise bandwidth, referred to the gain at the carrier: **78.49 Hz**. A white input noise of RNOISE AU² per sample
  (one-sided density RNOISE/4000 Hz) gives at ZBPF a variance of RNOISE/4000 × 78.49 × 0.25339² =
  0.0012599·RNOISE AU² (simulated at f_c = 1000 Hz: 0.001216);
- envelope (group) delay at the carrier: 1.661 + 5.050 = **6.71 ms**.

The text says a 100 Hz band-pass filter, with noise samples independent at 5 ms (pp. 111–112, 123). With this chain
the noise in the detector is that of 78.5 Hz, so its SNR is 10·log₁₀(100/78.49) = **1.05 dB above S₁₀₀** [derived].

### Rectifier and envelope low-pass

- **Rectifier:** XDET = SQRT(ZBPF²) = |ZBPF|, full-wave [fact, code p. 190 07200]. For a carrier of amplitude A at
  ZBPF its mean is (2/π)·A; for Gaussian noise of standard deviation σ it is √(2/π)·σ = 0.7979σ [derived].
- **Low-pass:** H(z) = A₁(1 + 3z⁻¹ + 3z⁻² + z⁻³)/(1 − 2.9507982z⁻¹ + 2.90396345z⁻² − 0.953135172z⁻³)
  [fact, code p. 189 05200, p. 190 07800–08700].

| Property | Value [derived] |
|---|---|
| DC gain | 8A₁/(1 − A₂ + A₃ + A₄) = 0.000240408/0.000030078 = **7.99282** (A₁ = 0.000430051 would give 114.4) |
| poles (z-plane) | 0.9835482; 0.9844177·e^{±j2π·51.10 Hz/f_s} |
| analog equivalents (s = f_s·ln z) | −132.7 rad/s (21.1 Hz); −125.6 ± j321.1 rad/s |
| magnitude relative to the DC gain | −0.67 dB at 10 Hz, −1.93 dB at 20 Hz, **−3.01 dB at 31.08 Hz**, −3.62 dB at 40 Hz, −4.92 dB at 50 Hz, −7.86 dB at 60 Hz, −11.79 dB at 70 Hz, −15.66 dB at 80 Hz, −22.25 dB at 100 Hz, −41.6 dB at 200 Hz, −85.3 dB at 1000 Hz, −108.3 dB at 2000 Hz |
| shape | monotone decreasing from 0 Hz (no passband ripple) |
| noise bandwidth, one-sided, relative to the DC gain | 35.48 Hz |
| group delay at 0 Hz | 9.650 ms (77.2 samples) |
| step response | 10 %, 50 %, 90 % of its final value at 3.875, 8.250, 14.625 ms; no overshoot |

The text and the comment call it a "3-pole Chebyshev 100 Hz" low-pass (p. 124; code p. 189 04500). The
coefficients give neither: the response has no ripple, its −3 dB point (relative to the DC gain) is at 31.1 Hz,
and for a Chebyshev type I response of order 3 the real pole would lie at twice the real part of the complex pair,
not at 1.06 times it [derived]. The DC gain of 8 comes from the numerator 1 + 3 + 3 + 1 = 8 with A₁ ≈ 1 − A₂ + A₃ + A₄ (to 0.09 %): A₁
appears to have been set for a gain of 1 on a numerator normalized to 1 [inference]. The transcription review
confirmed all four coefficients on the image [fact, `transcription-log.md`, p. 189 05200].

### Envelope of a key-down carrier of 1 AU

[derived; simulation and analysis]

- Steady state, carrier away from 1000 Hz: Z = 7.99282 × 0.25339 × m, where m is the mean of |sin| over the 8 samples
  per cycle of the 1000.0023 Hz tone. m depends on the sampling phase: 0.60355 … 0.65328, 2/π = 0.63662 on average.
  The phase drifts slowly, because the rectified tone's 8 kHz harmonic aliases to 8 × 0.0023 Hz: period **53.5 s**.
  So **Z = 1.2893 AU on average, between 1.2224 and 1.3231 AU** (−5.2 % … +2.6 %).
- Carrier at 1000 Hz: in addition the image term of RCVR multiplies the amplitude by 0.9487 … 1.0513 with period
  213.8 s: Z between 1.160 and 1.391 AU. Simulated over 240 s (1 s means): 1.171 … 1.337 AU, mean 1.270 AU.
- Keying: after a key-down the 8 kHz envelope reaches 10 %, 50 %, 90 % of its final value in 8.1, 14.5, 26.8 ms
  (carrier at 1000 Hz; 8.3, 14.6, 26.9 ms at 1500 Hz); after a key-up it falls to 50 % in 14.5 ms. A mark is
  therefore seen about **14.5 ms late** at its 50 % points, with its length preserved.
- After NOISE with no noise (YMAVG → 0 once the 1 s minimum falls in spaces): z = 1.1 × (1.2893 − 0.05) = 1.363 AU.

### Decimation phase

PROCES step n uses the envelope of 8 kHz sample k = 40n, at t = 5n ms (sample k at t = k/8000 s), n = 1, 2, …
[derived from code p. 159]. With the 14.5 ms envelope delay, step n reflects the keying at about t = 5n − 14.5 ms.
No sample between the 40th ones is used.

### Noise statistics

[derived numerically; one 120 s noise realization at f_c = 1000 Hz, scaled by √RNOISE]

- ZBPF is Gaussian (mean |ZBPF| = 0.7979σ to within 0.05 %).
- Noise-only envelope: E[Z] = 7.99282 × 0.7979 × √(0.0012599·RNOISE) = **0.2264·√RNOISE AU** in the long run
  (0.2223·√RNOISE over the 120 s realization); standard deviation 0.0783·√RNOISE AU.
- Relative to a 1 AU carrier's envelope (1.2893 AU), the noise-only mean is 0.1756·√RNOISE, i.e.
  0.785/√(S₁₀₀ as a power ratio) with S₁₀₀ as defined in section (d).
- **The 5 ms samples are correlated:** correlation 0.71 at lag 1 (5 ms), 0.27 at lag 2 (10 ms), a consequence of
  the 31 Hz envelope low-pass and the 64 Hz band-pass. The decoder treats the samples as independent, which the
  text justifies for a 100 Hz band-pass sampled every 5 ms (pp. 110–112).

### NOISE's calibration

Noise only, carrier at 1000 Hz [derived numerically]:

| S₁₀₀ (dB S₁₀₀) | RNOISE (AU²) | mean Z (AU) | YMAVG (AU) | subtracted 2.4·YMAVG + 0.05 (AU) | mean z (AU) | var z (AU²) | R (AU²) |
|---|---|---|---|---|---|---|---|
| 3 | 10.02 | 0.700 | 0.253 | 0.656 | +0.049 | 0.0753 | 0.0758 |
| 4 | 7.96 | 0.624 | 0.225 | 0.590 | +0.037 | 0.0598 | 0.0675 |
| 6 | 5.02 | 0.496 | 0.179 | 0.479 | +0.018 | 0.0377 | 0.0537 |
| 9 | 2.52 | 0.351 | 0.127 | 0.354 | −0.003 | 0.0189 | 0.0380 |
| 12 | 1.26 | 0.249 | 0.090 | 0.265 | −0.018 | 0.0095 | 0.0269 |

Keyed carrier of 1 AU, 400 ms on, 400 ms off, carrier at 1500 Hz, samples more than 40 ms from a transition
[derived numerically]:

| S₁₀₀ (dB S₁₀₀) | z on marks: mean, var (AU, AU²) | z on spaces: mean, var (AU, AU²) | R (AU²) |
|---|---|---|---|
| 3 | 0.773, 0.196 | −0.025, 0.083 | 0.085 |
| 6 | 0.910, 0.108 | −0.035, 0.042 | 0.061 |
| 9 | 1.026, 0.056 | −0.042, 0.021 | 0.043 |
| 12 | 1.116, 0.029 | −0.046, 0.011 | 0.031 |
| 20 | 1.261, 0.005 | −0.052, 0.002 | 0.012 |

Reading [derived]:
- The constants put a 1 AU key-down carrier near z = 1 AU at 6–12 dB S₁₀₀, where the Kalman filter starts
  (ŷ = 0.5 AU, P = 0.1 AU²); the subtraction makes z nearly zero-mean in noise around 9 dB S₁₀₀.
- R equals the noise-only variance of z near 3 dB S₁₀₀ and exceeds it at higher SNR (2.0 times at 9 dB S₁₀₀,
  2.8 times at 12 dB S₁₀₀), since R grows with the noise amplitude and the variance with its square. The variance
  of z on marks is 2.4–2.8 times that on spaces (the signal-times-noise term), which the decoder's model, with one
  R for both, ignores.
- YMAVG settles with a 1.25 s time constant from its start value 0.05 AU, so the first seconds of a session are
  decoded with a biased offset and R.

## (d) The simulator

### Processes, per 8 kHz sample

[fact, code pp. 160, 163–165; units derived from use]

| Process | Rule | Parameter (unit) | Step |
|---|---|---|---|
| keying | elements in sequence: the letter's marks with e-sps between them, then one letter-ending space | — | per element |
| durations | uniform on (m − e, m + e)·Δ, floored at 20 ms, no ceiling; lasts ⌈8·XDUR/ms⌉ samples | m = ESEP(k), e = EDEV(k) (dots); Δ = 1200/RATE ms | per element |
| text | random letters (KEY, section (b)); the text-file branch is unreachable | — | per letter |
| letter-ending space | c-sp 0.8, w-sp 0.1, pause 0.1 | — | per letter |
| amplitude steps | AMP ← max(AMP + N(0, AVAR), 0.01 AU) at each key-down | AVAR (AU² per key-down) | per key-down |
| fading | BFADE ← GAMMA·BFADE + N(0, BVAR); clamp so that AMP + BFADE ≥ 0.001 AU | GAMMA (per sample), BVAR (AU² per sample) | per sample |
| amplitude | a_n = AMP + BFADE | AU | per sample |
| carrier phase | θ ← (θ + (2π·f_c + ω_ch)·τ₈) mod 6.28319 | f_c = FREQ (Hz) | per sample |
| chirp | ω_ch = x·2π·FCHIRP·exp(−t_el/TCHIRP), t_el = time since the key change | FCHIRP (Hz), TCHIRP (ms, must be > 0) | per sample |
| phase steps | φ ← (φ + N(0, PHIVAR)) mod 6.28319 at each key-down | PHIVAR (rad² per key-down) | per key-down |
| signal | s_n = x_n·a_n·sin(θ + φ) + w_n | — | per sample |
| noise | w_n ~ N(0, RNOISE), white | RNOISE (AU² per sample) | per sample |

(2π here is the listing's 6.28319.) The stationary standard deviation of BFADE is √(BVAR/(1 − GAMMA²)) AU and its
correlation time −τ₈/ln(GAMMA) [derived]. AMP is a random walk over key-downs (variance growing by AVAR per mark)
with a floor; nothing pulls it back to 1 AU [derived].

### Letters and spaces

- KEY draws letters from the 36 columns of its MORSE table (A–Z, 1–9, 0), with the probabilities derived in
  section (b): A 0.001, B–Z and 1–9 1/35 each, 0 1/35 − 0.001.
- **C is keyed with B's code** (−···) [fact, code p. 163 05300]. As printed, the simulator therefore never sends C
  (−·−·) and sends B with probability 2/35 [derived].
- **Effect on Bell's letter-error counts** [inference]: KEY prints the sent *elements*, not the letters (section
  (b)), so an operator comparing the decoded text with that printout reads −··· as B, and a decoded B counts as
  correct. Table XV reports 0 % at 12 dB S₁₀₀ (50 WPM) and 9 dB S₁₀₀ (20 WPM) [fact, p. 132]; had a "C" keyed as B
  been counted against a decoded B, those cells would show about 2.9 % (1/35 of the letters). Either the printed
  C-row was not in the version that made the tables, or it was not counted as an error. Either way, for the
  reproduction the sent symbol must be labeled by the code actually keyed (B), not by the column drawn (section
  (j)).
- **IALPH(43) = 'X'** repeats IALPH(24) = 'X' [fact, code p. 161 08100; `transcription-log.md`]. It is in the
  decoder's print table, not in KEY, so it does not affect what the simulator sends; it affects only how a decoded
  X is printed (section (f)) [derived].

### S₁₀₀ as the program implements it

- **Definition used.** S₁₀₀ is the key-down carrier power over the noise power in 100 Hz, using the one-sided noise
  density [inference]. Grounds:
  - the tables label it "SNR (dB) (100 Hz)" [fact, pp. 130–134];
  - Table IV gives E/N₀ = S₁₀₀ + 10·log₁₀(100 Hz × Δ) to within 0.1 dB at 50, 30 and 20 WPM: E/N₀ exceeds S₁₀₀ by
    3.80, 6.02 and 7.78 dB, and the table's E/N₀ column is 3.8, 6.0 and 7.8 dB above its S₁₀₀ column, except 7.7 at
    0 dB S₁₀₀, 20 WPM [fact, p. 35; derived]; E is the energy of one dot at key-down power and N₀ the one-sided
    density [fact, p. 33];
  - Figure 14's two SNR scales show the same offsets: E/N₀ 4 dB above S₁₀₀ at 50 WPM, 8 dB above at 20 WPM
    [fact, p. 128].
- **In the simulator** [derived]: the key-down power is a²/2 (a = a_n in AU); the noise has variance RNOISE per
  8 kHz sample, white over 0–4000 Hz, so its one-sided density is N₀ = RNOISE/(4000 Hz) and its power in 100 Hz is
  RNOISE/40. Hence

  **S₁₀₀ = (a²/2)/(RNOISE/40) = 20·a²/RNOISE**, i.e. S₁₀₀ (dB S₁₀₀) = 13.01 + 20·log₁₀(a/AU) − 10·log₁₀(RNOISE/AU²),

  and E/N₀ = (a²/2)·Δ/N₀ = 2000·a²·(Δ/s)/RNOISE = S₁₀₀ × 100 Hz × Δ, as Table IV requires.
- **S₅₀₀ = 4·a²/RNOISE, so S₅₀₀ = S₁₀₀ − 10·log₁₀ 5 = S₁₀₀ − 6.99 dB** for this definition and white noise
  [derived]. The plan's conversion holds.
- With a = 1 AU, RNOISE = 20·10^(−S₁₀₀/10) AU² [derived]:

  | S₁₀₀ (dB S₁₀₀) | 3 | 4 | 6 | 7 | 8 | 9 | 12 |
  |---|---|---|---|---|---|---|---|
  | RNOISE (AU² per 8 kHz sample) | 10.024 | 7.962 | 5.024 | 3.991 | 3.170 | 2.518 | 1.262 |
  | S₅₀₀ (dB S₅₀₀) | −3.99 | −2.99 | −0.99 | 0.01 | 1.01 | 2.01 | 5.01 |

- With fading, a varies; the nominal a = 1 AU is meant [inference]. With the fading proposed in section (e) the mean
  key-down power is (1 + 0.0017)/2, a difference of 0.007 dB in S₁₀₀ [derived].
- How Bell set RNOISE for a given S₁₀₀ is not stated. A two-sided convention would shift every S₁₀₀ by 3.01 dB; the
  Table IV relation supports the one-sided one [inference].

## (e) The settings behind Tables XV and XVI

Bell typed his settings at run time (INPUTL), so they are not in the listing; the thesis gives some of them. Each
value below carries its page, or "not given" with our proposal.

### Stated or fixed by the listing

| Setting | Table XV (p. 132) | Table XVI (p. 134) | Source |
|---|---|---|---|
| signal | KAM: keyboard Morse | HKM: simulated hand-keyed | [fact, pp. 11, 132, 134] |
| decoder model | first-order, second-order, independent letters (three columns); the stage-1 targets use the independent-letter column | independent letters | XV [fact, p. 132]. XVI [inference]: p. 134 says "as a function of model complexity" but tabulates one column per speed, and Figure 17's KAM curve (about 30, 6, 3 % at 3, 4, 6 dB S₁₀₀, read from the graph) is XV's independent-letter column (31, 6, 3 %) while its fair-fist curve follows XVI's 20 WPM fair row (about 37, 8, 5.5, 4 % against 34, 8, 6, 4 % at 3, 4, 6, 9 dB S₁₀₀) [fact, p. 135] |
| P_opt | 0.90 | 0.90 | [fact, pp. 131–132 ("90% optimal estimator"), 134; code p. 181 30800] |
| decision delay | 200 samples (1 s) | 200 samples | [fact, pp. 132, 134; code p. 184 02200]; effective maximum 199 samples (section (b)) |
| speeds | 50, 20 WPM | 30, 20 WPM | [fact, pp. 132, 134] |
| S₁₀₀ | 50 WPM: 12, 9, 8, 7, 6 dB S₁₀₀; 20 WPM: 9, 6, 4, 3 dB S₁₀₀ | 9, 6, 4, 3 dB S₁₀₀ | [fact, pp. 132, 134] |
| fists | — | good, fair, poor: P_es = 0.00143, 0.0149, 0.0403 per element, i.e. letters with at least one wrong element E_s = 1 %, 10 %, 25 % | [fact, pp. 33, 134]; E_s = 1 − (1 − P_es)⁷ with 7 symbols per letter (p. 32 prints the relation without the "1 −") [derived] |
| text | random letters | random letters | [inference]: p. 125 offers random letters or a text file; the decoder models independent letters (pp. 106, 123) and Figure 17 compares with human operators on independent letters (p. 135); the printed KEY can only send random letters |
| letters per experiment | about 200 | about 200 | [fact, p. 139] |
| channel | "subjected to the fading model (VII.C) and white gaussian noise" | same | [fact, p. 125]; values not given |
| element mean lengths ESEP | not given | not given | [our choice] the listing's defaults 1, 3, 1, 3, 7, 14 dots (p. 160 00600, 03400–04000), the nominal lengths of p. 95 |
| decoder start | 25 paths in state 5 with 1000 ms in the element, 5 paths at each of 10, 20, 30, 40, 50 WPM, path probability 1, Kalman estimate 0.5 AU with variance 0.1 AU² | same | [fact, code p. 167 05500–06100, p. 176 03700] |

### Not given: our proposals

| Setting | Proposal | Reason |
|---|---|---|
| EDEV, KAM | 0 for all six elements | [inference] keyboard Morse has exact timing (p. 11); p. 125 randomizes durations to model the hand senders. Durations are then exactly m·Δ (at 20, 30, 50 WPM: 480, 320, 192 samples per dot). |
| EDEV, fists (dot, dash, e-sp, c-sp) | good 0.3353, fair 0.4602, poor 0.5723 dots | [our choice] the Gaussian standard deviation σ for which an element crosses the midpoint threshold 1 dot away with probability P_es: σ = 1/Q⁻¹(P_es), Q⁻¹ = 2.9824, 2.1727, 1.7472. Used as KEY's half-width (reading B below). |
| EDEV, fists (w-sp) | 0.6706, 0.9205, 1.1447 dots | [our choice] the same criterion at the w-sp/c-sp threshold, 2 dots from the mean (5 dots): 2σ |
| EDEV, fists (pause) | 1.1736, 1.6109, 2.0032 dots | [our choice] the same at the pause/w-sp threshold, 3.5 dots from the mean (10.5 dots): 3.5σ |
| FREQ (f_c) | 1000 Hz | [our choice] not given (p. 125: "ω₀ ≤ 4 KHZ"); RCVR's output frequency and the plan's default in §4.1. It brings in RCVR's image beat (section (c)), ±5 % of amplitude over 214 s, negligible for decoding. |
| RNOISE | 20·10^(−S₁₀₀/10) AU² | [derived] section (d), with the nominal key-down amplitude 1 AU |
| GAMMA | 0.99923881 per 8 kHz sample | [our choice] VII.C's γ = 0.97 per 5 ms (1 Hz fading, p. 115) as 0.97^(1/40) per sample |
| BVAR | 2.57496·10⁻⁶ AU² per 8 kHz sample | [our choice] VII.C's Var(v) = 10⁻⁴ per 5 ms (p. 116): BVAR·(1 − GAMMA⁸⁰)/(1 − GAMMA²) = 10⁻⁴. Stationary σ of BFADE 0.0411 AU, correlation time 164 ms. |
| AVAR | 0 | [our choice] VII.D's power steps model a new net station after a pause (p. 116), not one sender; in KEY's process AMP would random-walk with no restoring force over a run, drifting S₁₀₀ uncontrollably |
| PHIVAR | 0 | [our choice] the envelope chain does not see the carrier phase, except through ringing across a key-down; no value given |
| FCHIRP, TCHIRP | 0 Hz, 10 ms | [our choice] no chirp is stated for the simulations (the 50 Hz of p. 112 motivates the filter width); TCHIRP > 0 only to avoid a division by zero |
| runs | 20 runs of 200 letters per cell, each with its own seed | the plan's §5.2 (Bell used about 200 letters, about 5 runs of 300 elements, per cell) |

**The fist half-widths: three readings.** KEY's law is uniform (section (b)); the text's is a Gaussian truncated to
16–360 ms (p. 125). The P_es criterion can be applied three ways:

| Reading | Law | Dot, dash, e-sp, c-sp half-width or σ (good / fair / poor), dots | Status |
|---|---|---|---|
| A | uniform (listing), half-width set so that the uniform law crosses with probability P_es: 1/(1 − 2P_es) | 1.0029 / 1.0307 / 1.0877 (w-sp ×2, pause ×3.5) | alternative |
| **B** | **uniform (listing), half-width = the Gaussian σ above** | **0.3353 / 0.4602 / 0.5723** | **proposed** |
| C | Gaussian (text), σ as above, truncated to 16–360 ms | 0.3353 / 0.4602 / 0.5723 | contradicts the listing (D1); a §5.3 fallback only |

Reasons for B [our choice]:
1. The law must be the listing's (D1); only the typed values are open.
2. Bell's prompt asks for "STD DEVIATIONS" (p. 160 01400) and his text sets "the variance … to give the error
   crossover probabilities" for a Gaussian (p. 125), so he most likely typed Gaussian σ's [inference].
3. The good fist's results equal the keyboard results at 20 WPM: 1, 4, 6, 31 % against 0, 3, 6, 31 % at 9, 6, 4,
   3 dB S₁₀₀ [fact, pp. 132, 134]. Under B the good fist's dots span 40–80 ms at 20 WPM, a mild irregularity. Under A
   they span 0–120 ms, 17 % of them clipped to 20 ms, which would cost energy at low SNR; equal results are not
   plausible [derived; inference].

Consequence of B [derived]: under the uniform law no element reaches its midpoint threshold (the poor dot stays
below 1.573 dots, the poor dash above 2.428 dots; the w-sp above 5.86, the pause above 12.0 dots), so the simulated
fists make no sending errors in Bell's sense. The fists differ by spread alone, and the P_es values are nominal. At
30 WPM, 6.3 % of the poor fist's dots and e-sps are clipped at 20 ms; none at 20 WPM, and none for the good and fair
fists at either speed. If V2 fails on Table XVI, A and then C are the readings to test (§5.3 of the plan: "correcting
our reading of his settings … fist parameters").

### Tables XIV and XVII

- **Table XIV** (p. 131): the first-order Markov decoder, 50 WPM KAM, P_opt 0.98 … 0.8, delays 0, 40 and 200
  samples, 12, 9 and 6 dB S₁₀₀ [fact]. The listing holds only the second-order element model in DATA and a fixed
  NDELAY of 200; the first-order model's state table is not printed (Table XI gives its probabilities, p. 105).
  Not reproducible from the listing as printed [fact; inference].
- **Table XVII** (p. 136): an abrupt speed change after every tenth letter, KAM at 50 WPM and the fair fist at 30
  and 20 WPM, 9, 8 and 6 dB S₁₀₀ [fact]. KEY reads RATE once and has no speed change [fact, code pp. 160, 163–165], so
  the version that made Table XVII is not printed. Not reproducible as printed [inference].

## (f) The letter tree

### What the tables mean, from the code that reads them

| Table | Meaning | Read by |
|---|---|---|
| λ (LAMBDA) | a state of the letter tree: a node of the Morse code tree together with the element being keyed and the one before it; 0 means "no state" (a dead path) | all decoder routines |
| IELMST(λ) | the element-state of λ, 1 … 16: the (previous element, current element) pair of Table XII | TRPROB, PTRANS, PATH, LIKHD, TRANSL [fact, code pp. 169–171, 174, 187] |
| ILAM1(m) | the current element of element-state m | same |
| ELEMTR(m, k) | probability of element k next, from element-state m (section (b)) | PTRANS [fact, code p. 171 28800] |
| MEMFCN(λ, k) | the state reached from λ when element k is keyed; MEMFCN(λ, own element) = λ (staying); 0: not allowed | PATH [fact, code p. 173 41300] |
| LTRMAP(λ) | the IALPH code printed for λ; used only for letter-ending space states | TRANSL [fact, code p. 187 19900] |
| IBLANK(λ) | blanks printed after the letter: 1 for w-sp states (IELMST 3, 7), 2 for pause states (4, 8), else 0 | INITL, TRANSL [fact, code p. 161 11100–11400, p. 187 20000] |

How they combine [derived from the code]:
- The **probabilities** of the next element come from ELEMTR through IELMST alone; the tree does not change them.
- The **tree** decides only which successors exist: PATH sets the new state to MEMFCN(λ, k), and a successor with
  MEMFCN = 0 becomes state 0. PTRANS does not check MEMFCN, so such a successor still takes its share of the
  probability for one step; it is likelihood-weighted and normalized with the others, may be saved, and dies at
  the next step (TRPROB gives state 0 no successors). In effect the probability of a disallowed continuation is
  removed one step late, and the dead node may occupy a path slot for that step. The checks for λ = 0 in TRPROB
  and PATH show that Bell expected such states [inference].
- A **letter is printed** when the decided state moves from a c-sp, w-sp or pause state to a mark: the letter is
  LTRMAP of that space state (TRANSL). So each letter needs its own letter-ending space states; the e-sp and mark
  states never print.

### The 16 rows in DATA

[fact, code p. 161 06400–07700]

| λ | IELMST | Element-state | LTRMAP | MEMFCN(λ, dot, dash, e-sp, c-sp, w-sp, pause) |
|---|---|---|---|---|
| 1 | 1 | e-sp after dot | 3 | 9, 10, 1, 0, 0, 0 |
| 2 | 2 | c-sp after dot | 4 | 11, 12, 0, 2, 0, 0 |
| 3 | 3 | w-sp after dot | 5 | 13, 14, 0, 0, 3, 0 |
| 4 | 4 | pause after dot | 6 | 15, 16, 0, 0, 0, 4 |
| 5 | 5 | e-sp after dash | 3 | 9, 10, 5, 0, 0, 0 |
| 6 | 6 | c-sp after dash | 4 | 11, 12, 0, 6, 0, 0 |
| 7 | 7 | w-sp after dash | 5 | 13, 14, 0, 0, 7, 0 |
| 8 | 8 | pause after dash | 6 | 15, 16, 0, 0, 0, 8 |
| 9 | 9 | dot after e-sp | 1 | 9, 0, 1, 2, 3, 4 |
| 10 | 10 | dash after e-sp | 2 | 0, 10, 5, 6, 7, 8 |
| 11 | 11 | dot after c-sp | 1 | 11, 0, 1, 2, 3, 4 |
| 12 | 12 | dash after c-sp | 2 | 0, 12, 5, 6, 7, 8 |
| 13 | 13 | dot after w-sp | 1 | 13, 0, 1, 2, 3, 4 |
| 14 | 14 | dash after w-sp | 2 | 0, 14, 5, 6, 7, 8 |
| 15 | 15 | dot after pause | 1 | 15, 0, 1, 2, 3, 4 |
| 16 | 16 | dash after pause | 2 | 0, 16, 5, 6, 7, 8 |

- These rows are the **second-order element model** of Table XII: 16 states, one per element-state, with no letters
  [derived]. Their LTRMAP is the element number, which only the skipped element printout of TRANSL (20300–20500)
  could use [inference]. They are what the decoder would run if MORSEM were not read; Table XV's "second order"
  column is plausibly this model [inference].
- INITL overwrites rows 1–300 from MORSEM, these 16 included [fact, code p. 161 10400–11000].
- **No letter tree can contain rows 5, 9 and 10** [derived]. In a tree, the element space after T must lead to the
  marks of N (−·) and M (−−), and the one after E to I (··) and A (·−). Rows 1 and 5 both lead to 9 and 10, which
  would have to be I and N at once. Rows 9 and 10 lead back to rows 1–8, so the c-sp after I would be E's letter
  state and I would print as E. So the plan's test "the 16 DATA rows are reproduced" cannot be met by any file that
  decodes letters. The other 13 rows are exactly the root of a letter tree numbered as below, which is the evidence
  for that numbering [inference].

### His alphabet (IALPH)

IALPH(70) holds the printed text of each code [fact, code p. 161 07800–08300]. The Morse code of each entry is not
in the listing; KEY's MORSE table gives A–Z, 1–9, 0 in IALPH order (section (b)), the rest is inference or choice.

| Code | Text | TRANSL format | Morse code (proposal) | Status |
|---|---|---|---|---|
| 1–26 | A … Z | character | International Morse | [inference, strong]: KEY lists these codes in the same order |
| 27–36 | 1 … 9, 0 | character | International Morse | same |
| 24 | X | character | not used: the letter X's node is given code 43 | [our choice], see 43 |
| 37 | ; | character | (−·−·−·) | excluded [our choice]: in KEY's text format 37 and 38 mean w-sp and pause (p. 164 09200–09300), and KEY prints the w-sp as `;` (07100), so 37 and 38 may be display codes for spaces [inference, weak]; variant: include |
| 38 | : | character | (−−−···) | excluded, as 37 |
| 39 | % | character | unknown | excluded [our choice]: no Morse code for %; it was in Bell's tree, since his decoder printed it (p. 148, sample H, CWA6 → C%A6) [fact] |
| 40 | & | character | (·−···, the same code as AS) | excluded [our choice]: one code cannot print two entries; AS has the dedicated format |
| 41, 42 | 0 | — | — | unused |
| 43 | X | after a word space: X and a new line; inside a word: as a character | −··− | [inference, moderate]: X written as a word stands for a full stop in message text, and TRANSL's special case for 43 makes exactly that distinction; [our choice]: map the X node to 43. The printed character is X either way. |
| 44 | . | character | ·−·−·− | [inference]: in p. 148 sample G, "TEST." is decoded "NSCK"; T followed by the period with the letter space lost is −·−·−·−, read as C (−·−·) K (−·−), which holds only for ·−·−·− |
| 45 | AS | two characters | ·−··· | [our choice] standard prosign (wait); AS appears in decoded output (p. 148, sample H) [fact] |
| 46 | SN | two characters | ···−· | [our choice] standard prosign (understood) |
| 47–50 | 0 | — | — | unused |
| 51 | NR | spaced prosign | −··−· | [our choice, weak]: N and R run together; NR appears in decoded output (p. 148, samples G, H) [fact] |
| 52 | NO | spaced prosign | −·−−− | [our choice, weak]: run together |
| 53 | GA | spaced prosign | −−··− | [our choice, weak]: run together |
| 54 | OK | spaced prosign | −−−−·− | [our choice, weak]: run together |
| 55 | AR | spaced prosign | ·−·−· | [our choice] standard (end of message) |
| 56 | SK | spaced prosign | ···−·− | [our choice] standard (end of work) |
| 57–60 | 0 | — | — | unused |
| 61 | IMI | spaced, three characters | ··−−·· | [our choice] standard (question); IMI appears in decoded output (p. 148, sample E) [fact] |
| 62–65 | 0 | — | — | unused |
| 66 | BT | on its own line | −···− | [our choice] standard (break) |
| 67–69 | 0 | — | — | unused |
| 70 | EEE | spaced, three characters | unknown (an error sign; the eight-dot form would need eight levels) | excluded [our choice] |

**Proposed alphabet** [our choice]: the 36 letters and digits (X under code 43), the period, and AS, SN, NR, NO,
GA, OK, AR, SK, IMI, BT: **47 symbols**, all Morse codes distinct, longest 6 elements. Reason: every entry with a
known or standard code, without the four whose code is unknown or conflicting. Variants for V2 if needed: "36
only" (letters and digits) and "+ ; :" (49 symbols).

### The rule that reconstructs the file

The numbering is ours; the meaning is his. Let the alphabet give each code c a Morse string w_c. The trie nodes v
are all non-empty prefixes of the w_c; v ends in a dot or a dash; v is a symbol if v = w_c.

**States** (IELMST in brackets: first value if v ends in a dot, second if in a dash):
- M(v): the mark of v after an element space, for nodes of two or more elements [9, 10].
- M(v, s): the mark of a one-element node (E or T) after a letter-ending space s = c-sp, w-sp, pause
  [11, 12], [13, 14], [15, 16].
- E(v): the element space after v, for every node with a child [1, 5].
- S(v, s): the letter-ending space s after a symbol v: c-sp [2, 6], w-sp [3, 7], pause [4, 8].

**MEMFCN** (0 where not listed):
- M(v) and M(v, s): own element → itself; e-sp → E(v) if it exists; c-sp, w-sp, pause → S(v, c-sp), S(v, w-sp),
  S(v, pause) if v is a symbol.
- E(v): e-sp → itself; dot → M(v·) and dash → M(v−) if those nodes exist.
- S(v, s): s → itself; dot → M(·, s); dash → M(−, s).

**LTRMAP:** c for S(v, s) with v = w_c; for mark and e-sp states, c if v is a symbol, else 0 (never printed).

**Numbering:**
- 1 E(·), 2 S(·, c-sp), 3 S(·, w-sp), 4 S(·, pause), 5 E(−), 6 S(−, c-sp), 7 S(−, w-sp), 8 S(−, pause), 9 M(··),
  10 M(·−), 11 M(·, c-sp), 12 M(−, c-sp), 13 M(·, w-sp), 14 M(−, w-sp), 15 M(·, pause), 16 M(−, pause): so that
  IELMST(λ) = λ for λ = 1 … 16, as in DATA.
- From 17: the remaining states, nodes in breadth-first order (shorter first, then dot before dash), each node's
  states in the order M, E, S(c-sp), S(w-sp), S(pause).
- Rows after the last state, up to 300: all zeros. Format: `FORMAT(8I3)`, LTRMAP, IELMST, MEMFCN(1..6).

**The start state.** PROCES starts all paths in λ = 5 with 1000 ms elapsed [fact, code p. 167 05600, 06000].
Under this numbering λ = 5 is E(−), the element space after T; its element-state is 5 ("e-sp after a dash"), as
is state 5's in the DATA model [derived]. The first decided letter of a session can therefore come out as a letter that begins with
T (the simulator's first letter is always T [fact, code p. 163 05100]) [inference]. Apart from state 5, the state
numbers are only labels: the decoder compares states for equality (SAVEP) and looks them up, so any other
numbering decodes identically [derived].

**Size** [derived]: 36 symbols: 39 nodes, 173 states; the proposed 47 symbols: 52 nodes, 227 states; 49 symbols
(with ; and :): 55 nodes, 239 states. All fit in the 300 rows INITL reads.

### Tests the reconstructed file must pass

1. **Every symbol decodes:** from a letter-ending state (for example λ = 2), following MEMFCN through the symbol's
   elements with an e-sp between marks, then a c-sp (or w-sp, or pause), reaches a state whose LTRMAP is the
   symbol's code.
2. **Every state is reachable** from state 5.
3. **The DATA rows:** IELMST(λ) = λ for λ = 1 … 16; MEMFCN rows 1–4, 6–8 and 11–16 equal the DATA rows exactly; rows
   5, 9 and 10 differ only in their links into the tree (5: dot → N's mark, dash → M's mark; 9, 10: the spaces of
   I and of A). This replaces the plan's "the 16 DATA rows are reproduced", which no letter tree can meet.
4. **Staying:** MEMFCN(λ, ILAM1(IELMST(λ))) = λ for every state.
5. **Consistency with ELEMTR:** every nonzero MEMFCN(λ, k), k not λ's own element, has ELEMTR(IELMST(λ), k) > 0,
   and the successor's IELMST is the pair (λ's element, k).
6. **Zeros:** where ELEMTR(IELMST(λ), k) > 0 (k not λ's element), MEMFCN(λ, k) = 0 exactly where the trie has no
   such continuation: a mark of a node without children has no e-sp successor, a mark of a node that is not a
   symbol has no letter-ending spaces, an e-sp has no successor for a missing child. Where ELEMTR = 0 (k not λ's
   element), MEMFCN = 0.
7. **Size and format:** at most 300 states; values ≤ 999 (`I3`); rows beyond the last state zero.

A check of the rule (scratch scripts, not committed) passes all seven tests for the three alphabets above. The
proposed alphabet has 47 transitions that ELEMTR allows and the tree does not (each leads to state 0 for one step).

## (g) Text against listing

Under D1 the listing's value is the one used. "Derived" in the listing column means a property computed from the
listed code (section (c)).

| Item | Text value (page) | Listing value (line) | Used here |
|---|---|---|---|
| duration density rate α | 3.61 per baud (dot, dash, e-sp, c-sp), 1.81 (w-sp), 0.90 (pause) (p. 98) | 3.0, 1.5, 1.0 per baud (p. 170 20600, APARM with KIMAP) | listing |
| stay probability when D₀ ≤ m ≤ D₀ + ε | (1 − ½e^{α(D₀+ε−m)}) / (½e^{−α(D₀−m)}) (p. 100) | ½e^{−α(D₀+ε−m)} / (1 − ½e^{α(D₀−m)}) (p. 170 23600–23800); the printed formula takes, in numerator and denominator, the expressions p. 100 gives for the other range; the listing's is the quotient of the two survival functions for this range (p. 100, upper half) [derived] | listing |
| Φ on marks | γ = 0.97 per 5 ms (p. 117 (a)) | 1 (p. 178 13000) | listing |
| Q on marks | 10⁻⁴ (p. 116 (a)) | 10⁻⁴ AU² (p. 178 12900) | same |
| Q, e-sp → mark | 0.01 (p. 117 (c)) | Q(B) = 0.15e^{0.6(B−14)} + 0.01·B·e^{0.2(1−B)}; 0.01006 at B = 1 (p. 178 13500–13600) | listing |
| Q, other space → mark | 0.25 (p. 117 (e)) | the same formula: 0.0203 at 3 bauds, 0.0233 at 7, 0.160 at ≥ 14 | listing |
| Φ during an e-sp | 1 (p. 117 (b)) | 10^{−2/(22.4·Δ/ms)} per step, 1 from 14 bauds on (p. 178 13900–14100) | listing |
| Φ during other spaces | 0.98 (p. 117 (d)) | the same as during an e-sp: no distinction | listing |
| noise floor window | minimum over 240 samples, 1.2 s (p. 124) | minimum over 200 samples, 1.0 s; a 50-sample buffer has no effect (pp. 190–191 10900–13800) | listing |
| R | 2μ̂ₙ² (pp. 114, 124) | 0.30·YMAVG, floor 0.005 AU² (p. 191 14500–14600) | listing |
| removed mean μ̂ₙ | the averaged minimum, scaled "empirically" (p. 124) | 2.4·YMAVG + 0.05, and the result × 1.1 (p. 191 14700) | listing |
| detector | square law (Figure 13, p. 113; comment p. 190 06900) | full-wave rectifier |x| (p. 190 07200) | listing |
| envelope low-pass | "100 Hz bandwidth 3-pole Chebyshev" (p. 124; comment p. 189 04500) | monotone, −3 dB relative to the DC gain at 31.1 Hz, DC gain 7.99 (p. 189 05200; p. 190 08100–08700) [derived] | listing |
| RCVR filter | "single-pole 500 Hz BW band-pass" (p. 123) | one-pole low-pass on I and Q, −3 dB relative to its DC gain at ±92.4 Hz, noise bandwidth 290 Hz (p. 189 01900–02000) [derived] | listing |
| band-pass | 100 Hz, two single-tuned resonators, noise bandwidth 1.22 times the half-power bandwidth (pp. 111–112, 123) | resonators of radius 0.90 and 0.97 at 1000.2 Hz; with RCVR 64.4 Hz wide at −3 dB relative to the gain at the carrier, noise bandwidth 78.5 Hz (p. 189 05300–05400) [derived] | listing |
| element durations of the simulated fists | truncated Gaussian, 16–360 ms (p. 125) | uniform, mean ± EDEV·Δ, floor 20 ms, no ceiling (p. 164 11300–11800) | listing |
| text of the simulator | random letters or a text file (p. 125) | random letters; the text-file branch is unreachable (p. 164 08800–09500) | listing |
| letter C in the simulator | — (not stated) | keyed as −··· (p. 163 05300) | listing, labeled as B (section (j)) |
| second-order element probabilities | Table XII: ·~ .5/.45; −^ .55/.5; −~ .5/.45; ^· .5 to dash and .069 to w-sp (p. 105) | .5/.5; .55/.45; .5/.5; 0 to dash and .067 to w-sp (p. 161 08700–09100) | listing |
| paths saved | at least one per element state (p. 122); the comment says at least 7 (p. 180 27100) | at least one per element only if its probability ≥ 10⁻⁶; at least 2 in all (pp. 181–182 31500–36300) [derived] | listing |
| saved paths' order | "renumbered in order of decreasing probability" (p. 122) | element-best first, then decreasing probability (p. 181) | listing |
| equal paths | (no merging described) | duplicates in (state, duration, speed) deleted (p. 182 38800–40600) | listing |
| decision delay | at most 200 samples, 1 s (p. 123) | at most 199 samples, 995 ms (p. 185 07300–07400, p. 186 14500–14700) [derived] | listing |
| letter tree file | — | `MORSEM`, 300 rows of `8I3` (p. 161 10300–10600) | listing |
| speed range and steps | 10–60 WPM; Table X (pp. 102–104) | the same (p. 161 09500–10000; p. 172 35100–35200) | same |
| speed changes only at a key-state change | p. 103 | only when the element changes (p. 172 33300–33500), the same thing since only key-state changes have nonzero probability [derived] | same |
| P_opt | 0.9 (pp. 131–132) | 0.90 (p. 181 30800) | same |
| sample rate | 200 Hz, 5 ms (p. 112) | every 40th 8 kHz sample (p. 159 01800) | same |

## (h) Library routines and compiler semantics

### Routines called but not in the listing

| Routine | Calls | What it must do (from its use) | Standard equivalent proposed |
|---|---|---|---|
| RAN(IK) | KEY: one per element (p. 164 11500) and two more at the end of each letter (07900, 08300) | a uniform pseudo-random number on (0, 1); IK = octal 001000000000 = 2²⁷ is passed as its argument [fact]; whether it is a seed that RAN updates is not visible in the listing | gfortran `RANDOM_NUMBER` with a fixed seed (the plan's change 2); in C++ and Python any uniform (0, 1) generator. Bell's exact stream cannot be reproduced. |
| RANDN(W, 1, 0., SIGMA) | SIMSG1, four per sample (p. 163 01500, 01900, 03000, 03600) | one Gaussian number with mean 0 and standard deviation SIGMA (the fourth argument is √(variance) from INPUTL) [inference from the calls] | Box–Muller from two uniforms, scaled by SIGMA; called also when SIGMA = 0, to keep the stream's order |
| ERASE, PLOTR(S, 512, flag, scale, y), VIEW('1') | DISPLA (p. 166) | graphics terminal output | none: display only, omitted |
| SQRT, EXP, SIN, COS, AMOD, `**` | throughout | Fortran intrinsics | the same; AMOD(x, y) = x − y·trunc(x/y), so a negative phase stays negative (harmless under sin) |

### What the listing assumes of its compiler

These matter for the compiled reference (Task 3) and for the C++ port, which must behave like each other (V3):

1. **Local variables keep their values between calls,** and those without DATA start at 0 (for example RCVR's
   ZILP and ZQLP, BPFDET's filter states, TRANSL's IXLAST, LSTELM, LSTLTR, TRELIS's NDELST, and PROCES's DURSAV,
   whose stale values PATH leaves in place for state-0 nodes). The PDP-10 allocates locals statically [inference].
   Proposed: gfortran `-fno-automatic -finit-local-zero`; in C++ the same state in members initialized to 0.
2. **DO loops with a negative trip count.** FORTRAN-66 compilers ran such a loop once; FORTRAN-77 runs it zero
   times. Which rule Bell's compiler used is not stated. The listing reaches such a loop only in TRELIS's
   first call (`DO 400 K=NDEL,NDELST` with NDEL > NDELST, section (b)), where the difference is invisible (state 5
   prints nothing) [derived]. Proposed: FORTRAN-77 rules in both (gfortran's), recorded as an assumption.
3. **Jumps to a DO loop's terminal statement from outside the loop** (TRANSL: `GO TO 110`, 210, 310, 560 when
   NBLANK = 0). The intended effect, skipping the blanks, is what the C++ implements; gfortran must be checked to
   compile it so (a warning at most).
4. **Out-of-range subscripts.** LIKHD evaluates `ILAM1(IELMST(LAMBDA))` and `ILAMX(...)` with LAMBDA = 0 for dead
   paths; the values are unused because every transition probability of a dead path is 0 [derived]. On the PDP-10
   this read a word outside the array. Compiled with gfortran it may read anything, and the second subscript may
   fault. **Task 3 needs a guard here (skip LIKHD's body, or set its outputs to 0, when LAMBDA = 0), which does not
   change any result; the plan's list of allowed changes does not include it yet.**
5. **Named COMMON blocks of different lengths:** RCVR declares /BLK2/ with WC only; DATA for COMMON variables outside
   BLOCK DATA (INITL, INPUTL, KALFIL). gfortran accepts both in legacy mode (Task 3 to confirm).
6. **Character data in integer arrays:** IALPH holds `'A'` … `'IMI'`, `'EEE'` (up to five 7-bit characters per
   36-bit PDP-10 word); ISYMBL uses Hollerith `1H.` etc. In the port these become strings.
7. **DEC input/output:** `TYPE`, `ACCEPT`, `FORMAT(7F)` without widths, `$` (no new line), octal constants
   (`"001000000000`), `ENDFILE` on files opened for reading (INITL). The plan's change 1 replaces the terminal input
   and output. Note that `ENDFILE` after reading may truncate the file under gfortran; MORSEM must hold exactly 300
   rows and TEXT exactly 105.
8. **Precision.** The PDP-10's single precision has a 27-bit fraction (about 8 decimal digits); the plan compiles
   and ports in double precision (§4.3). Bell's results carry that difference [inference: small against the
   statistical spread of 200-letter experiments].

## (i) How Bell counted letter errors

**Stated:**
- Each simulation experiment used "approximately 200 letters", and its outcome is the "proportion of letter
  errors" [fact, p. 139].
- Table XX gives 90 % confidence intervals for measured rates of 5–30 %, assuming the proportion is Gaussian
  [fact, p. 139]. They are close to the normal approximation p ± 1.645·√(p(1 − p)/200) (10 % → 6.5–13.5 %, printed
  7–14 %), with printed upper limits up to 1.4 points higher (20 % → 15.3–24.7 %, printed 15–26 %) [derived].
- In the appendix "Samples of output data", "errors are indicated by an underline" [fact, p. 147]. Eight decoded
  texts are printed with their conditions (A–H, pp. 147–148): 50 WPM KAM at 12 dB S₁₀₀; 20 WPM fair at 9, 6, 6
  and 4 dB S₁₀₀; 15 WPM KAM at 12 dB S₁₀₀; 15 WPM fair at 12 and 6 dB S₁₀₀.

**What the underlines show** [fact: the marks on pp. 147–148; inference: that the tables were counted the same way]:
- a wrong letter is one mark (A → I, F → M, R → G);
- two sent letters decoded as one are **one** mark: OVER → OVF (E and R merged into F), AFTERNOON → AFDRNOON
  (T and E into D), JUMPED → JU.ED, THIS → 6IS (T and H into 6);
- one sent letter decoded as two is a mark per output letter: SUNDAY → IUTSANO (Y → NO);
- a spurious word space (LOGS → L OGS, the space underlined) and a missing one (A SUNNY → ASUNNY) are marked.

**Not stated:** the denominator (letters sent, presumably), the rule for lining the decoded text up with the sent
text, and whether space errors entered the tables' percentages. The simulator printed the sent *elements*, not
letters (section (b)), so the comparison was made against that element printout, by eye [inference].

**Against this project's measure.** The plan compares Bell's numbers with the character CER (plan §0;
`docs/signal-processing.md` A.11: minimum-edit alignment; edits that involve a word space are counted apart). The
two differ [derived]:

| Event | Bell's marks | Character edits |
|---|---|---|
| wrong letter | 1 | 1 |
| two letters merged into one | 1 | 2 (substitution and deletion) |
| one letter split into two | 2 | 2 (substitution and insertion) |
| spurious or missing word space | 1 | 0 (a space edit) |

Worked example, sample B (20 WPM, fair, 9 dB S₁₀₀; 51 letters sent): Bell marks 4 errors (7.8 %); the character
edits are 6 (11.8 %): JUMPED → JU.ED 2, OVER → OVF 2, A → I 1, AFTERNOON → AMTERNOON 1 [derived]. Merges are
common at low SNR and with sloppy fists (a lost letter space), so the character CER runs above Bell's count.
Section (j) proposes how to handle this.

The samples' sent texts are fixed sentences ("A LAZY BROWN DOG …"), which KEY as printed cannot send (its
text-file branch is unreachable), so they come from a different version of the simulator [inference]. Their error
rates (for example sample B, 7.8 % by Bell's marks, against 4 % in Table XVI for the same cell) are illustrations,
not table cells.

## (j) Gaps: what the plan must fill by its own choice

Each item: the gap, the choice proposed, its reason, and the section with the detail. "Plan change" marks the ones
that change something the plan states.

| # | Gap | Choice proposed | Reason |
|---|---|---|---|
| 1 | The letter tree file MORSEM is not printed | Rule N1 of section (f), alphabet of 47 symbols, rows up to 300 zero; variants "36 only" and "+ ; :" for V2 diagnosis | The rule follows from the code that reads the file; N1 keeps 13 of the 16 DATA rows and the start state's meaning; the alphabet takes every IALPH entry with a known or standard code |
| 2 | The plan's tree test "the 16 DATA rows are reproduced" cannot be met (plan change) | Test 3 of section (f): IELMST(1..16) = 1..16, MEMFCN rows 1–4, 6–8, 11–16 equal to DATA, rows 5, 9, 10 differing only in their links into the tree | Rows 5, 9 and 10 cannot belong to any letter tree that prints the right letters (section (f)) |
| 3 | The TEXT file is not printed | 105 lines of `0` (`I2`) | INITL reads 105 values; KEY never uses them. (Bell's was probably the 51-letter sentence of samples A–E: 102 entries in KEY's space-and-letter pairs [inference].) |
| 4 | Settings of Tables XV and XVI | Section (e): FREQ 1000 Hz; RNOISE = 20·10^(−S₁₀₀/10) AU²; GAMMA 0.99923881; BVAR 2.57496·10⁻⁶ AU²; AVAR 0; PHIVAR 0; FCHIRP 0 Hz; TCHIRP 10 ms; ESEP 1, 3, 1, 3, 7, 14; EDEV 0 (KAM) and reading B for the fists | Each reason in section (e) |
| 5 | The simulator's duration law is uniform, not the plan's Gaussian (plan change: Task 7, "Gaussian durations per KEY") | Task 7's style "bell" draws mean ± U·EDEV·Δ, U uniform on (−1, 1), floor 20 ms, quantized to ⌈8·d/ms⌉ samples at 8 kHz; its test checks this law and the half-widths of reading B, not P_es | D1; KEY p. 164 11300–11800 |
| 6 | The text the simulator sends (plan change: Task 7's test "letter frequencies uniform over his alphabet") | Reproduce KEY's draw: A 0.001, B 2/35 (B and the C column), C never, D–Z and 1–9 1/35 each, 0 1/35 − 0.001; letter-ending space c-sp 0.8, w-sp 0.1, pause 0.1. **Label each sent letter by the code keyed** (the C column as B). | D1; and Table XV's 0 % cells show that a "C" keyed as B was not counted as an error (section (d)) |
| 7 | Bell's error marks count a merge once; the character CER counts it twice (plan change: §5.2) | Report both. Apply §5.2's rule to a **Bell-style count**: align decoded and sent letters, word spaces removed, by minimum edits; count substitutions and insertions, plus deletions not adjacent to a substitution or insertion; divide by the letters sent. Keep the character CER as the project's measure and report it beside. | It reproduces the marks of pp. 147–148 (merge 1, split 2, wrong letter 1); without it the comparison is biased against our decoder (sample B: 4 against 6). Space errors are left out of both, since Bell's tables do not say and the random text's word spaces are random. |
| 8 | RAN and RANDN | gfortran `RANDOM_NUMBER`, fixed seed per run; Box–Muller for RANDN, called four times per sample in the listing's order | Plan change 2; section (h) |
| 9 | Compiler semantics (section (h)) | gfortran `-std=legacy -fdec -fno-automatic -finit-local-zero` (with the plan's real-8 flags); FORTRAN-77 DO trip counts; the C++ keeps the same state and initial zeros | So that V3 compares like with like; the only FORTRAN-66-dependent loop is invisible |
| 10 | LIKHD reads `IELMST(0)` for dead paths (plan change: Task 3's list of allowed changes) | Add a guard, marked `C KZ4AP:`, that skips LIKHD's loop body for LAMBDA = 0 and sets LKHD to 0 for its 30 nodes; the C++ does the same | It changes no result (every transition probability of a dead path is 0) and prevents an out-of-range read |
| 11 | What the C++ must keep from the listing | XTRANS's three cases; PTRANS without the MEMFCN check; state-0 successors and their one-step life; SAVEP's selection order, tie rules, duplicate deletion and its normalization without P(1); TRELIS's 199-stage forced decision and pruning; MODEL's Φ and Q exactly; NOISE's constants | V3 requires identical letters and paths; each is how the listing behaves (section (b)) |
| 12 | The plan's description of NOISE (plan change: §4.1 step 5, §9 item 1) | Replace "μₙ and R = 2μₙ²" by the listing: z = 1.1·(Z − 2.4·YMAVG − 0.05), R = 0.30·YMAVG ≥ 0.005 AU² | D1; section (b). R cannot reach 0, so §9 item 1's division by zero cannot come from R; exact-zero guards are needed only where a sum of probabilities can be 0 (PROBP; SAVEP when one path survives), and only if a test shows it |
| 13 | The plan's file name and size (plan change: §2) | `MORSEM`, 300 rows read of the 400 dimensioned | p. 161 10300–10600 |
| 14 | Output symbols for scoring | Each IALPH entry is one symbol; the two- and three-letter entries (AS, SN, NR, NO, GA, OK, AR, SK, IMI, BT) become prosign tokens; NBLANK ≥ 1 becomes one word space | A.11 counts a prosign as one symbol; the plan's §4.1 makes a word space or a pause one space |
| 15 | The end of input: TRANSL prints a letter only when the next letter's first mark is decided, and Bell's program never ends | The plan's `finish()`: at the end, decide the best path's remaining nodes and print the pending letter, as an addition recorded in the results | Without it the last letter of every transmission is lost |
| 16 | The decoder's first letter of a session | Keep the listing's start (state 5 with 1000 ms elapsed, N1's e-sp after T) | D1; its effect is at most one letter per session |
| 17 | Operator waits and displays | Removed (DISPLA, KEY's and TRELIS's `ACCEPT`, PLOTR) | Plan change 1; they do not touch the decoding |
| 18 | Precision | Double precision (plan §4.3) | Plan's choice; the PDP-10 used a 27-bit fraction |
| 19 | Tables XIV and XVII | Out of stage 1's reproduction | Not reproducible from the listing as printed (section (e)) |
| 20 | Receiver-chain tests at f_c = 1000 Hz (Task 4) | Test the steady envelope against the ranges of section (c) (1.160–1.391 AU at 1000 Hz; 1.222–1.323 AU elsewhere) or at a chosen sampling phase, not against one value | The 8-samples-per-cycle rectifier and RCVR's image make the envelope vary slowly by several percent |
