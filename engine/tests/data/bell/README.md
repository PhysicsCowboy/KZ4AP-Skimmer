# Golden data from Bell's compiled program

Three inputs of 1 s, each run through Bell's program as compiled in `reference/bell1977/` (Bell stage-1 plan,
Task 3), with the trace at level 2. The C++ receiver chain (Task 4) and decoder (Task 5, V3) read them and must give
the same values. They were made at commit 0ea2b05 with GNU Fortran 16.2.0 (conda-forge gcc 16.2.0-7) on the Linux
machine:

```
python reference/bell1977/v2.py run --bin BIN/bell1977-fpe --tables build/linux/bench/kz4ap-bell-tables \
    --out OUT --runs 1 reference/bell1977/run/golden-{1,2,3}.settings
python reference/bell1977/v2.py golden --run OUT/golden-N/r01 --dest engine/tests/data/bell --name golden-N
```

The seed of each is its settings file's SEED plus 1 (the run number). gfortran's random numbers belong to its
version, so another gfortran may give another signal from the same seed; the files here are the reference.

## The inputs

| Name | Sender | Speed | Noise | Seed | What it holds | Letters printed |
|---|---|---|---|---|---|---|
| golden-1 | keyboard Morse (LAW 0, EDEV 0) | 20 WPM | NOISE 1.261915 AU² per 8 kHz sample: 12 dB S₁₀₀ (one-sided N₀ = NOISE/4000 Hz) | 101 | T, c-sp, G (--.) begun | `M` |
| golden-2 | the fair fist, reading C (LAW 1, EDEV 0.460249, 0.460249, 0.460249, 0.460249, 0.920497, 1.610870 dots) | 30 WPM | NOISE 5.023773 AU²: 6 dB S₁₀₀ | 201 | T, c-sp, 4 (....-), c-sp | `7` |
| golden-3 | keyboard Morse | 50 WPM, then 30 WPM | NOISE 1.261915 AU²: 12 dB S₁₀₀ | 328 | T (samples 1–575), a pause (576–3263, 336 ms at 50 WPM), then B (-...) and the start of U at 30 WPM: KEY's dot length is set to 40 ms at sample 1600 (0.2 s, change 10), inside the pause | `M`, then two blanks |

All three use FREQ 1000 Hz and the fading of reference sheet (e) (GAMMA 0.9992388097, BVAR 2.574960·10⁻⁶ AU²);
`*.settings` holds the exact values. The first letter, T, decodes as M in all three: the decoder starts every path in
state 5, the element space after T's dash (reference sheet (f)), so the first dash is read as the second of M.

## Files

Every `.f64` file is a flat array of IEEE 754 binary64 values in little-endian byte order, with no header. Counts
are integers stored exactly as float64. For each input `golden-N`:

| File | Size | Content |
|---|---|---|
| `golden-N.settings` | text | The SETTINGS the program read (`reference/bell1977/README.md`, "Running"). |
| `golden-N.signal.f64` | 8000 values (64 000 bytes) | s_n, AU, n = 1 … 8000: SIMSG1's output, the input of RCVR, sample n at t = n × 0.125 ms. |
| `golden-N.steps.f64` | 200 records × 8 values (12 800 bytes) | One record per decoder step k = 1 … 200 (NOISE and PROCES run on sample 40k, t = 5k ms), in this order: k; z_k (AU) and R_k (AU²), NOISE's outputs; ISAVE (paths saved), IMAX (the slot with the largest P), PX (posterior probability of key down, from SPROB), XHAT (1 if PX > 0.5, else 0), SPDHAT (WPM, SPROB's conditional mean speed): the values PROCES holds after TRELIS at step k. |
| `golden-N.paths.f64` | records × 9 values | The saved paths after each step: for step k, ISAVE(k) records in slot order i = 1 … ISAVE(k), steps in order. Each record: k; i; λ (letter-tree state, 0 for a dead path); DUR (ms in the current element); ILRATE (WPM); PATHSV (the parent's slot at step k − 1); P; ŷ = YKKIP (AU, the path's Kalman amplitude estimate); P_kk = PKKIP (AU², its variance). Records: golden-1 1 279, golden-2 1 672, golden-3 1 500. |
| `golden-N.events.txt` | text | The trace's event records: `E sample element index column` for each element KEY starts (element 1–6: dot, dash, e-sp, c-sp, w-sp, pause; index in the letter, 0 for the letter-ending space; MORSE column of the letter, or of the next letter for a letter-ending space); `D call step lambda` for each decided state passed to TRANSL (one call per decided trellis stage, in time order; step = the decoder step at which it was decided); `L call step code nblank` for each letter TRANSL prints (IALPH code; blanks that follow). |
| `golden-N.transl.txt` | text | TRANSL's display output (each letter with its leading blank). |

Notes for the comparison:
- P is as SAVEP leaves it: its renormalization leaves out P(1), so the saved P sum to more than 1 (reference sheet
  (b), SAVEP); PROBP renormalizes at the next step.
- λ is read after TRELIS: a forced decision sets λ = 0 for every path not descended from the best one.
- PATHSV of step 1 indexes the 25 initial paths (all in state 5, 1000 ms, speeds 10, 20, 30, 40, 50 WPM in groups of
  five, ŷ 0.5 AU, P_kk 0.1 AU²).
- The paths are those after step k; the z_k and R_k of the same record are the inputs that produced them.
- No step of these inputs reaches SAVEP's zero sum: the three runs used `bell1977-fpe` and finished.

The data files total 565 929 bytes (the plan's limit for this folder is 1 MB).
