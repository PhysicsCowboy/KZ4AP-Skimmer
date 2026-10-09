# Bell 1977: the Fortran listing

This folder holds E. L. Bell's Fortran program, transcribed from the "Computer Programs" appendix of his thesis:

> E. L. Bell, "Optimal Bayesian Estimation of the State of a Probabilistically Mapped Memory-Conditional Markov
> Process with Application to Manual Morse Decoding", Doctor of Engineering thesis, Naval Postgraduate School,
> Monterey, California, September 1977.

| File | What it is |
|---|---|
| `listing.txt` | The listing, transcribed verbatim, page by page. |
| `transcription-log.md` | How the two readings were settled. It has one row per line that differed or carried a mark, one row for the agreed line both readers misread (p. 181, 30800), the reason for each decision, and the counts. |
| `bell1977.f` | The listing made to compile with gfortran (Bell stage-1 plan, Task 3): every change marked `C KZ4AP: change N`. See "The compiled program" below. |
| `build.sh` | Builds `bell1977` and `bell1977-fpe` with gfortran. |
| `v2.py` | Runs the program on settings files, turns its output into reference and decoded texts, the score table and the reproduction rule (plan §5.2). |
| `run/*.settings` | One settings file per target cell of plan §5.1 (Table XVI: one per reading A, B, C of decision D9); `smoke.settings` (keyboard Morse, 20 WPM, no noise); `golden-1`, `-2`, `-3.settings`, the golden inputs of `engine/tests/data/bell/`. |
| `run/kam-grid/*.settings` | Keyboard Morse at 20 and 50 WPM over 3–15 dB S₁₀₀: the curve from which the S₁₀₀ shift between his tables and this program is estimated (plan §5.2, "also reported"). |

The transcription keeps the original exactly, including its apparent errors. For example, it keeps the misspelling
MINIIMUM in a comment on p. 180 (27100), and the DATA row for C on p. 163 (05300), which repeats the row for B.

## Page map

"p. N" is the page number printed on the thesis page. In the scan used (404 PDF pages), printed page N is PDF page
2N + 3; the even PDF pages between the listing pages are blank versos.

| Printed pages | PDF pages | Content |
|---|---|---|
| 158 | 319 | Figure 18i, "Output Waveforms", printed rotated 90 degrees. It is not part of the listing; `listing.txt` gives a one-line description and the figure's labels. |
| 159-191 | 321-385 | The listing. p. 159 is headed "COMPUTER PROGRAMS". |

Bell's sequence numbers are the line numbers of the DEC editor, not part of the Fortran. They fall into eight numbering
groups. Every group after the first starts at 00100; on p. 159, in the first group, the first digit is cut off and is reconstructed from that as `{0}`. Within a
group the numbers step by 100, with the exceptions listed in `transcription-log.md`.

| Group | Pages | Sequence numbers | Routines: page and sequence number where each starts |
|---|---|---|---|
| 1 | 159 | `{0}0100`-`{0}3100` | main program (p. 159) |
| 2 | 160-162 | 00100-13500 | INPUTL (p. 160, 00100); INITL (p. 160, 04900) |
| 3 | 163-165 | `{0}0100`-12300 | SIMSG1 (p. 163, `{0}0100`); KEY (p. 163, `{0}4400`) |
| 4 | 166 | 00100-05600 | DISPLA (00100); STATS (01700); AUTOCR (03200) |
| 5 | 167-175 | 00100-51200 | PROCES (p. 167, 00100); TRPROB (p. 169, `{1}2300`); XTRANS, a function (p. 170, 18600); PTRANS (p. 171, 24600); SPDTR, a function (p. 172, 30800); PATH (p. 173, 36400); LIKHD (p. 174, 44100) |
| 6 | 176-183 | 00100-43200 | KALFIL (p. 176, 00100); MODEL (p. 177, 09500); PROBP (p. 178, 15100); SPROB (p. 179, 20900); SAVEP (p. 180, 26400) |
| 7 | 184-188 | 00100-29500 | TRELIS (p. 184, 00100); TRANSL (p. 186, 16800) |
| 8 | 189-191 | 00100-15000 | RCVR (p. 189, 00100); BPFDET (p. 189, 03700); NOISE (p. 190, 09600) |

## Method

- **Two readings.** Two readers, separate agents, transcribed every page from images of the scan independently.
  Neither saw the other's work. Neither used the PDF's text layer, which garbles code.
  - The images were rendered with Ghostscript, for example
    `gswin64c -q -dNOPAUSE -dBATCH -sDEVICE=pnggray -r170 -dFirstPage=M -dLastPage=M -sOutputFile=pM.png <thesis PDF>`.
  - The readers worked at 170 dpi and zoomed at 300 dpi and above.
- **Agreement.** The two readings agree on 1 715 of the 1 814 lines. Both readers misread one of those lines alike: p. 181
  30800 prints `DATA POPT/.90/`, and both read .99. It is corrected.
- **Settling.** Every difference, and every character that either reader marked as uncertain, was settled on the page
  image.
  - Each page was rendered at the scan's native resolution, about 350 dpi, at which the rendering reproduces the scan's
    text bitmap pixel for pixel.
  - Each doubtful glyph was compared with the same printer's glyphs on the same pages.
  - Where the image could not decide, the Fortran context decided: a statement label that must exist, a variable name
    used elsewhere, or the syntax.
- **Review.** A review checked five random pages character by character, every numeric constant, every
  context settlement and every image settlement that changes a value. It found two wrong constants:
  - p. 163 02300, `BFADE=0.001-AMP`, which had first been settled as 0.021;
  - p. 181 30800, `.90`.

  Both are corrected and recorded in the log.
- **The log.** `transcription-log.md` gives each decision with its reason, and the counts.
- **Kept out of the repository.** The page images and the two readings are working files and are not committed. The
  log quotes both readings for every line it lists.

## Conventions

- **Lines.** One line of `listing.txt` is one printed line. `=== p. N (PDF page M) ===` starts each page.
  `--- page number: N ---` is the page number printed at its foot.
- **Columns.**
  - Each line keeps the printed columns, at 10 characters per inch: first the sequence number, then the label,
    continuation mark and statement in their printed positions.
  - The file has no tabs and no trailing spaces.
- **Zeros.** Bell's slashed zero is written `0`. The letter O is written `O`.
- **Marks.**
  - `[?]`: a character that cannot be read. None is left.
  - `[?x|y]`: a character that is x or y. None is left.
  - `{d}`: a digit cut off by the scan's left edge and reconstructed from the sequence numbering. There are 171, all
    the first digit of a sequence number:
    - 140 on pp. 163, 169, 177 and 178, fixed by the legible numbers around them;
    - 31 on p. 159, where the main program is alone in its numbering group. These come from the group's start at
      00100, as in every other group.
  - `[cut off at the right edge of the reproduced printout]`: the rest of the line is not on the thesis page. The
    reproduction of the printout ends there, inside the scan. This occurs once, on p. 168 at line 07400, after
    `...,ILR`.
  - `[?]`, `[?x|y]` and `{d}` each stand for one printed column. The text after one of them is shifted to the right by
    its extra characters.
- **Descriptions.** A line in brackets on its own, on p. 158, is a description, not a transcription.
- **Sequence numbers** are kept as printed, including the irregular ones (02900 twice on p. 167; 06900 where 08900
  would follow on p. 168; 04120 on p. 184; others listed in the log).
- **Unusual glyph.** The character set is uppercase ASCII. One glyph is unusual: an underscore printed with a central
  tick, in `1H_` on p. 164 (07100). See the log.

## The compiled program

`bell1977.f` is `listing.txt` as fixed-form Fortran: statements from column 7, labels in columns 1–5, the continuation
mark `2` in column 6, and Bell's sequence number of each line in columns 73–80, which the compiler ignores. Line 07400
(p. 168), cut off in the reproduced printout after `...,DURSAV,ILR`, is completed as `ILRSAV)` (reference sheet (b),
PROCES); that is a reading, not a change. Apart from the marked changes below, the statements equal the listing's
character for character (blanks aside); this was checked by comparing the two statement by statement.

### The changes

Each is marked `C KZ4AP: change N` in the code, with its reason. Changes 1–9 are the plan's list (Task 3). Change 10 is
an addition used only by golden input 3, and change 11 the guard of plan §9 item 6 (controller's ruling R1, Task 3);
both were accepted by the controller.

| # | Change | Reason |
|---|---|---|
| 1 | INPUTL reads its 21 values from the file `SETTINGS` (list-directed, its first four lines in the order of Bell's four prompts) and one more line of ours (below); `TYPE` becomes `PRINT` (standard output); the operator's waits (`ACCEPT ... WAIT` in KEY, DISPLA, TRELIS) are removed; MAIN ends the session after `TSTOP` seconds of signal and closes the output files. | Bell typed his settings at a terminal and stopped the program by hand. |
| 2 | Every `RAN(IK)` (KEY 07900, 08300, 11500) becomes `CALL RANDOM_NUMBER(Y)` and `DATA IK` is removed; RANDN, a library routine missing from the listing, is written as Box–Muller from two uniform numbers; one stream, seeded from `SETTINGS`. | RAN and RANDN are not in the listing. gfortran's own RAN intrinsic would compile silently and return the same number at every call (reference sheet (h)). |
| 3 | TRANSL's `TYPE` statements write to `TRANSL.TXT`, with Bell's formats. | His display output, kept as text. |
| 4 | A trace, output only: `EVENTS.TXT` (trace level 1), `SIGNAL.BIN` and `STEPS.BIN` (level 2), below. | Scoring needs the sent letters and the time of each decoded letter; the C++ tests need the signal and the paths. |
| 5 | `MORSEM` is written by `kz4ap-bell-tables morsem MORSEM` (rule N1, 47 symbols; reference sheet (f)). | The file is not printed. No code change. |
| 6 | LIKHD: before 47800, a dead path (λ = 0) gets likelihood 0 for its 30 nodes and no Kalman state is written. | The listing reads `IELMST(0)`, outside the array. Every transition probability of a dead path is 0, so KALFIL would do the same: the guard changes no result. |
| 7 | The five jumps to a DO loop's terminal statement from outside the loop (PROCES 09750; TRANSL 22900, 24100, 25300, 27600) go to a new label just after the loop. | FORTRAN 77 forbids them and gfortran's result is undefined; the evident intent is to skip the loop (reference sheet (h)). |
| 8 | Stubs that do nothing for ERASE, PLOTR and VIEW. | Display library routines, not in gfortran. |
| 9 | KEY's duration law is chosen by `LAW`: 0, the printed uniform law; 1, reading C of decision D9: a Gaussian of mean ESEP·Δ and standard deviation EDEV·Δ, redrawn while below 16 ms and, for dot, dash, e-sp and c-sp, while above 360 ms. | Decision D9 (a): the text's truncated Gaussian (p. 125) for Table XVI, readings A and B run with the printed law (reference sheet (e)). |
| 10 | MAIN sets KEY's dot length to 1200/`RATE2` ms at sample `TSW`·8000 (`TSW` = 0: never). | Golden input 3 needs a speed change, which the printed KEY cannot make. Off in every V2 run. |
| 11 | SAVEP: when the sum PSUM of slots 2 … ISAVE is 0, P(1) is added to it before the division at 42700, so the single survivor gets P = 1; otherwise nothing changes. Each use writes a record `G step isave` to EVENTS.TXT. | The listing's sum leaves out P(1), and when every slot after the first holds P = 0 it divides by 0: +∞ and NaN, from which the decoder does not recover (reference sheet (b)). V2 reached it in 270 of 1 440 runs without the guard (results record §3.4). A documented deviation; the C++ does the same (plan §9 item 6). |

Compiler semantics (reference sheet (h)): `-fno-automatic -finit-local-zero` give static locals that start at 0, as
on the PDP-10; DO loops follow FORTRAN 77 (no trip when the count is 0 or negative); every REAL is double precision
(plan §4.3). gfortran warns about three things in the listing itself, none a change: STATS receives the INTEGER XHAT
for a REAL argument (display only); RCVR declares COMMON /BLK2/ with WC only; and the padding of our COMMON /KZ4AP/ is
avoided by its order.

### Building

`build.sh [OUTDIR]` compiles with `$FC` (default `gfortran`):
`-std=legacy -fdec -fno-automatic -finit-local-zero -fdefault-real-8 -fdefault-double-8 -O2`, and the same with
`-ffpe-trap=invalid,zero` as `bell1977-fpe`, which stops with SIGFPE at a division by zero or an invalid operation
(SAVEP's zero sum before change 11, or any other) and otherwise gives identical output. On the Linux machine gfortran comes from
conda-forge, installed with micromamba inside the repository's `build/` folder:

```
mkdir -p build/micromamba
curl -sSL https://micro.mamba.pm/api/micromamba/linux-64/latest | tar -xj -C build/micromamba bin/micromamba
MAMBA_ROOT_PREFIX=$PWD/build/micromamba HOME=$PWD/build/micromamba \
  build/micromamba/bin/micromamba create -y -p $PWD/build/gfortran -c conda-forge --override-channels gfortran
FC=build/gfortran/bin/gfortran sh reference/bell1977/build.sh build/bell1977/bin
```

Used for Task 3: micromamba 2.9.0 and GNU Fortran 16.2.0 (conda-forge gcc 16.2.0-7). gfortran's `RANDOM_NUMBER`
(xoshiro256** since GCC 9) belongs to the compiler's version, so the same seed gives the same runs only with the same
gfortran.

### Running

The program runs in a folder holding `SETTINGS`, `MORSEM` (300 rows) and `TEXT` (105 lines `0`, which KEY never
reads); INITL also writes `OUTPUT`, its check listing of MEMFCN. `v2.py run` prepares one folder per run and runs it.

`SETTINGS`: five lines, list-directed, text after the values ignored (the files here comment with `!`):

| Line | Values | Units |
|---|---|---|
| 1 | RATE, ESEP(1..6) | WPM; dots (dot, dash, e-sp, c-sp, w-sp, pause) |
| 2 | EDEV(1..6) | dots: the uniform law's half-width (LAW 0) or the Gaussian's standard deviation (LAW 1) |
| 3 | AVAR, BVAR, FCHIRP, TCHIRP, PHIVAR | AU² per key-down; AU² per 8 kHz sample; Hz; ms; rad² per key-down |
| 4 | GAMMA, FREQ, NOISE | per 8 kHz sample; Hz; AU² per 8 kHz sample |
| 5 | SEED, LAW, TSTOP, TRACE, TSW, RATE2 | integer; 0 or 1; s; 0, 1 or 2; s; WPM |

The values of lines 1–4 for the target cells are those of reference sheet (e) and (d): FREQ 1000 Hz, NOISE =
20·10^(−S₁₀₀/10) AU² (one-sided N₀ = NOISE/4000 Hz), GAMMA 0.9992388097, BVAR 2.574960·10⁻⁶ AU², AVAR 0, PHIVAR 0,
FCHIRP 0 Hz, TCHIRP 10 ms, ESEP 1, 3, 1, 3, 7, 14 dots; EDEV 0 for keyboard Morse, and for the fists the spreads of
readings A, B and C (sheet (e)), from P_es = 0.00143, 0.0149, 0.0403 to six decimals. `v2.py run` replaces SEED by the
file's SEED plus the run number r = 1 … 20. Cell k of the 30 (in the order of plan §5.1) has SEED 1000·k, the same
for its readings A, B and C, so the three readings of a run share their seed; the S₁₀₀ grid has 900000 + 1000·WPM +
100·S₁₀₀. The seed array given to `RANDOM_SEED` is built from SEED, and the first 1000 numbers are discarded (change
2), because gfortran's generator gives correlated first numbers for neighboring seeds. TSTOP gives 202 letters with a
margin: (1.25 × 3490.6/RATE + 5) s (14.40 dots per letter on average, sheet (b)); `v2.py texts` refuses a run that
sent too few letters.

Outputs:
- standard output: the prompts and the values read, KEY's element printout after every 300 elements, TRELIS's
  averages;
- `TRANSL.TXT`: TRANSL's display text;
- `EVENTS.TXT` (TRACE ≥ 1), one record per line:
  - `E sample element index letter`: KEY starts an element at this 8 kHz sample (1 = the first): element 1–6 (dot,
    dash, e-sp, c-sp, w-sp, pause), its index in the letter (0 for the letter-ending space), the MORSE column of the
    letter (for a letter-ending space, the next letter's);
  - `L call step code nblank`: TRANSL prints a letter: its call count (one call per decided trellis stage, in time
    order), the decoder step (5 ms) at which it prints, the IALPH code and the blanks that follow (0, 1 or 2);
  - `D call step lambda` (TRACE = 2): every decided state passed to TRANSL;
  - `G step isave` (TRACE ≥ 1): change 11 acted in SAVEP at this decoder step, leaving ISAVE slots;
- `SIGNAL.BIN` (TRACE = 2): per 8 kHz sample, three little-endian float64: s_n (AU), the key state x_n, the envelope
  Z (AU);
- `STEPS.BIN` (TRACE = 2): per decoder step, eight float64 (step, z in AU, R in AU², ISAVE, IMAX, PX, XHAT, SPDHAT in
  WPM), then ISAVE records of seven float64 (λ, DUR in ms, ILRATE in WPM, PATHSV, P, ŷ in AU, P_kk in AU²): the
  saved paths as PROCES holds them after TRELIS.
