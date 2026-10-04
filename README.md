# KZ4AP Skimmer

An open-source skimmer for CW (Morse code): it listens to a whole band at
once through a software-defined radio, decodes every CW signal it finds, and
picks out the callsigns. An alternative to Afreet Software's CW Skimmer and
CW Skimmer Server.

Created by Kenton Randolph Brown, KZ4AP.

**Status:** early development. The decoding engine and its benchmark run on
recorded I/Q files; there is no user interface or live radio support yet.
See `docs/design/` for the design.

## Project documents

| Document | Role |
|---|---|
| `docs/design/2026-09-25-kz4ap-skimmer-design.md` | What we are building and why: the decisions, including the development order (§3.1) and the decoder plan (§5). The authority; changes need the owner's approval. |
| `docs/plans/` | One implementation plan per milestone: how to build it, task by task. Written from the approved design before the milestone starts, and approved before any code is written. |
| `docs/backlog.md` | Deferred work and to-dos, in milestone order. Items move into plans as milestones start. |
| `docs/signal-processing.md` | What the engine's signal processing actually does now, with every parameter and whether it was derived, measured or chosen heuristically. Updated in the same commit as any signal-processing change. |
| `docs/research/` | The evidence behind decisions. `decoder-survey.md` is the synthesis (glossary, decoder ranking, benchmark scenarios); verification notes for each source sit beside it; `research_notes/` holds the original research record, annotated rather than rewritten. |

Work happens on feature branches; merging into `main` and pushing require the
owner's approval.

## Building

Windows: Visual Studio Build Tools 2026 (C++ workload). Linux: GCC 13+ or
Clang 17+, CMake 3.25+, Ninja.

    cmake --preset windows      # or: linux
    cmake --build --preset windows
    ctest --preset windows

## Benchmark

`kz4ap-bench` runs the engine over an I/Q recording and, given the labels file
the synthetic generator writes, scores the decoded text. On Windows
(PowerShell):

    python -m venv .venv
    .venv\Scripts\python -m pip install -r training/requirements.txt
    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.generate --scenario band --signals 8 --out build/synth/band.wav
    build\windows\bench\Release\kz4ap-bench.exe build/synth/band.wav --labels build/synth/band.json

On Linux:

    python3 -m venv .venv
    .venv/bin/python -m pip install -r training/requirements.txt
    PYTHONPATH=training .venv/bin/python -m kz4ap_synth.generate --scenario band --signals 8 --out build/synth/band.wav
    build/linux/bench/kz4ap-bench build/synth/band.wav --labels build/synth/band.json

The generator's Python tests run with `python -m pytest training -q` (using the
venv's Python).

Every push runs the same check on Windows and Linux (`bench/smoke.sh`). It
decodes one synthetic recording on both paths and fails if the milestone-1
(Envelope) path gets worse than `bench/baselines/smoke.json` (CER above 0.09,
or fewer than 7 of 8 stations detected), if the default (Matched) path gets
worse than `bench/baselines/smoke-matched.json` (CER above 0.07, or fewer than
7 of 8 detected), or if two runs of the same path differ. Measured on Windows:
Envelope CER 0.0353 (34 edits in 964 symbols), Matched 0.0436 (42 edits;
0.0622, 60 edits, before the growth-bound and late-opening fixes). The Matched
limit was set from the 0.0622 and fails from 68 edits; it is tightened only
once the Linux CI value is measured too; how both limits were chosen is in
`docs/signal-processing.md`, section 11, "Smoke check".

Named suites generate many recordings at once, score them, and summarize
CER, character and word-space errors, first-word errors, VE3NEA's no-space
CER, the S₅₀₀ where CER crosses 0.10 and 0.05 (each with a bootstrap 95%
interval), and CPU time per channel-second. The full suite is sized for
three seeds: 117 recordings, 4.55 h of audio and 3.20 GB (measured). On this
project's development desktop (12th Gen Intel Core i7-12700H) generating them
took 60 min, with another job sharing the machine for part of it, and scoring
them with both front ends took 5.0 min (measured; `docs/signal-processing.md`,
section 8b):

    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full
    .venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build\windows\bench\Release\kz4ap-bench.exe --decoder baseline --decoder matched
    .venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full

`kz4ap-bench --decoder` selects the decoder (`--front-end` is its old name and
still works). `--decoder matched` (the default) selects the dit-matched decoder
with frequency re-centering, each channel following the station the signal
detector assigns it (see `docs/signal-processing.md`, sections 6 and 8b);
`--decoder envelope` (also `baseline`) selects the milestone-1 pipeline;
`--decoder bank` selects the filter-bank decoder (section 8c), whose results
the suites write to `results/bank`. The bench writes each track's final text
(`text`, corrections applied) and its immediate text (`text_immediate`, the
characters as first published) and scores both (`cer`, `cer_immediate`).

## Known limitations

- The first character or two of a transmission may be lost or wrong while the
  station is being detected.
- Very strong signals (around 60 dB SNR in 500 Hz) can produce extra "ghost"
  tracks beside them.
- After a station stops, a few stray E's can be decoded from noise before its
  track is dropped.
- Default (Matched) path, measured on synthetic recordings
  (`docs/signal-processing.md`, section 8b, "Limits measured"):
  - an unkeyed tune-up carrier of 1 s or more before the first sending can
    stop the station from being decoded at all (CER 0.542 after 1 s and 0.909
    after 2 s, against 0.000 and 0.029 on the Envelope path);
  - a channel that opens in the middle of a transmission loses or garbles
    the first word, sent partly before the opening; a fragment of a mark no
    longer misleads the speed estimate (the smoke-test station that read at
    CER 0.234 now reads 0.043), but a station slower than 12.5 WPM often
    has its first characters read as a string of T's (12 WPM, 6–20 dB SNR
    in 500 Hz: first-word CER 0.985, against 0.591 before that fix);
  - the start-up runaways measured before the growth-bound fix are gone
    (CER 0.464 and 0.981 before, 0.000 and 0.015 after), but a speed-up
    from 20 to 35 WPM now loses the text after the step in 5 of 6 signals
    (CER 0.394, against 0.113 before that fix; not yet diagnosed);
  - it is worse than the Envelope path in several conditions: slow fading
    with paddle or hand keying at high SNR, some fists (bug, hand, positive
    imbalance), a strong interferer 20 Hz away, stations at 10 WPM, and a
    QSO's answering station 50 Hz from the caller when the two are split
    into two tracks.

## License

GPL-3.0; see `LICENSE`. An additional permission for linking closed-source
SDR driver libraries (such as the SDRplay API) will be added before SDRplay
support lands.
