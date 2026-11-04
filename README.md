# KZ4AP Skimmer

An open-source skimmer for CW (Morse code): it listens to a whole band at
once through a software-defined radio, decodes every CW signal it finds, and
picks out the callsigns. An alternative to Afreet Software's CW Skimmer and
CW Skimmer Server.

Created by Kenton Randolph Brown, KZ4AP.

**Status:** early development. The decoding engine and its benchmark run on
recorded I/Q files, and a desktop app runs them on a recording or an SDRplay
radio, receive only. See `docs/design/` for the design.

## Try the app (Windows)

In PowerShell, from the repository's folder. It needs Visual Studio's C++ build
tools (2022 or later, with their CMake) and Qt 6 in `C:\Qt` (below if you have none).
Network the first time. About 1.5 min to configure and 2.5 min to build on an 8-core
desktop (measured).

    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    $env:PATH += ";$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    cmake --preset windows -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
    cmake --build --preset windows --target kz4ap-skimmer

**A recording to open.** Recordings are not committed. If a recording of the real band
has been left in the git-ignored `build\recordings\` (`real30.wav` is 30 min of 40 m at
192 kHz, 1.4 GB, with no truth), open it, and you need no Python:

    build\windows\app\Release\kz4ap-skimmer.exe build\recordings\real30.wav

With no argument the program opens idle: press Open... or Start radio.

Otherwise generate 60 s of 30 stations at 192 kHz (46 MB; 20 s for the environment and
20 s to generate, measured). It takes Python 3.12 (numpy 2.5.3 needs it). If `python
--version` says anything else, or "Python was not found" (Windows' Store stub), set `$py`
below to the full path of a Python 3.12 `python.exe`: `conda create -n kz4ap-py312 -c
conda-forge python=3.12` makes `%USERPROFILE%\anaconda3\envs\kz4ap-py312\python.exe`, where
Anaconda's own `Scripts\conda.exe` is the `conda` if that is not on PATH either. Skip the
two lines after `$py` if `.venv` exists:

    $py = "python"
    & $py -m venv .venv
    .venv\Scripts\python -m pip install -r training\requirements.txt
    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.generate --scenario band --signals 30 --duration 60 --sample-rate 192000 --seed 1 --out build\recordings\band30.wav
    build\windows\app\Release\kz4ap-skimmer.exe build\recordings\band30.wav

**No Qt in `C:\Qt`?** With any Python: `python -m pip install aqtinstall`, then
`python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt` (33 s,
measured). Without Qt the configure step says so in one line and the build reports that
`kz4ap-skimmer.vcxproj` does not exist.

**What you will see.** The left table has one row per track; on real 40 m 35% to 63% of
those rows are not callsigns. The **call list** is the table to its right: one row per call
that has been read well enough to report, with its frequency, when it was last heard, how
many times it has been read and how (*signed*: with DE or CQ, or before K or TEST; *bare*:
alone; *named*: only by another station, so the frequency is that station's). Click a heading
to sort; drag the bar between the tables to widen it. The status bar's "Calls" counts it. A
call shows at least 10 s after it is read a second time: on `band30.wav` all 30 calls are
there by about 24 s of stream (derived: their second readings end by 13.5 s, measured).

To keep the list, add `--calls calls.csv` (it is written when the stream ends; add
`--quit-at-end` to leave after that). To give it a list of calls in MASTER.SCP's format, add
`--known-calls MASTER.SCP`: a listed call needs one signed, named or doubled reading. A list does
not help a call with no digit after a letter-digit prefix (D4C, E7DX, C6AGU): the stage has no
form for those, and does not read them (`docs/backlog.md`).

    build\windows\app\Release\kz4ap-skimmer.exe build\recordings\band30.wav --speed max --calls calls.csv --quit-at-end

The radio needs the SDRplay API (below).

## Project documents

| Document | Role |
|---|---|
| `docs/design/2026-09-25-kz4ap-skimmer-design.md` | What we are building and why: the decisions, including the development order (§3.1) and the decoder plan (§5). The authority; changes need the owner's approval. |
| `docs/plans/` | The results records for checkpoints, and working plans of a page at most. Older plans stay as history. |
| `docs/decision-log.md` | One line for each engineering decision: what, why, and what it costs if wrong. |
| `docs/backlog.md` | Deferred work and to-dos, in milestone order. Items move into plans as milestones start. |
| `docs/signal-processing.md` | What the engine's signal processing actually does now: a body readable in about 30 minutes, which covers in full the chains under development or discussion (each stage, its formulas, every value with its unit and whether it was derived, measured or chosen heuristically) and gives every other chain a short summary pointing to the appendix; and an appendix (derivations, provenance with validity conditions, known limitations, the full parameter table, the benchmark's definitions). Brought up to date at each checkpoint and before any merge into `main`. Results and evidence are in the results records under `docs/plans/`. |
| `docs/research/` | The evidence behind decisions. `decoder-survey.md` is the synthesis (glossary, decoder ranking, benchmark scenarios); verification notes for each source sit beside it; `research_notes/` holds the original research record, annotated rather than rewritten. |

Work happens on feature branches. Pushing requires the owner's approval, each time.

## Building

Windows: Visual Studio Build Tools 2026 or 2022 (C++ workload), whose CMake is not on PATH until
you add it ("Try the app" above). Linux: GCC 11.4 or later, or Clang 17+, CMake 3.25+, Ninja.

    cmake --preset windows      # or: linux
    cmake --build --preset windows
    ctest --preset windows

The first configure downloads GoogleTest, nlohmann/json and SLEEF (the
vectorized math library of the bank decoder's duration fit) with CMake's
FetchContent, so it needs network access. Configure through the presets: with
a single-configuration generator and no `CMAKE_BUILD_TYPE`, SLEEF's own CMake
code sets it to Release in the cache, which then applies to the whole project
from the next configure on.

## Desktop app

`kz4ap-skimmer` (in `app/`) shows a band as a waterfall with each signal's
decoded text and speed, from an I/Q recording or a live SDRplay radio. It needs
Qt 6 Widgets and is built only where CMake finds them; everything else builds
and tests the same without Qt. "Try the app" above has the steps; elsewhere,
point `CMAKE_PREFIX_PATH` at your Qt's folder.

On Windows the build copies Qt's DLLs next to the program. `--decoder` picks the
decoder (default `segmental`), `--speed` the pace (a factor, or `max`) and
`--threads` the threads for the channels (default half the machine's); `--help`
lists the rest. The Position slider seeks in a recording: the decoders start
afresh from there.

**The radio.** The SDRplay API (3.x) is used if it is there, and nothing else
needs it: the program builds and runs without it and says so when asked for the
radio. Building its support needs the API's headers (found in
`C:\Program Files\SDRplay\API\inc`, or set `KZ4AP_SDRPLAY_INCLUDE_DIR`; turn it
off with `-DKZ4AP_WITH_SDRPLAY=OFF`); the library is loaded at run time, never
linked (`KZ4AP_SDRPLAY_DLL` names another one).

To record a band: set Radio center, Rate (96, 192, 384 or 768 kHz), IF gain
reduction and LNA state; press Start radio; check the waterfall, with no "ADC
OVERLOAD" beside the buttons (raise the gain reduction or the LNA state if there
is); press Record and choose a file; press Stop recording. The recording goes to
the project's WAV form, 0.77 MB/s at 192 kHz, up to its limit of 4 GiB (93 min at
192 kHz), where it stops by itself. The file name holds the center frequency,
since the form does not; center and rate are locked while recording. Its writer
has its own queue, so a pipeline falling behind ("pipeline lost" beside the
buttons) leaves no gap in the file. `kz4ap-skimmer --radio --center-khz 7030 --rate
192000` starts the radio at once.

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
`docs/signal-processing.md`, appendix A.11, "Smoke check".

Named suites generate many recordings at once, score them, and summarize
CER, character and word-space errors, first-word errors, VE3NEA's no-space
CER, the S₅₀₀ where CER crosses 0.10 and 0.05 (each with a bootstrap 95%
interval), and CPU time per channel-second. The full suite is sized for
three seeds: 117 recordings, 4.55 h of audio and 3.20 GB (measured). On this
project's development desktop (12th Gen Intel Core i7-12700H) generating them
took 60 min, with another job sharing the machine for part of it, and scoring
them with both decoders took 5.0 min (measured; `docs/plans/2026-09-27-milestone-2a-results.md`,
section 3.1):

    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full
    .venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build\windows\bench\Release\kz4ap-bench.exe --decoder baseline --decoder matched
    .venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full

The second `run` into the same `--out` is refused: results cost hours, so a run
does not write into a results folder (`results/baseline`, `results/matched`, or
`results/NAME` for a run given `--name NAME`) that already holds result files.
Give a run a new `--name`, or pass `--overwrite` to replace the test cases it
runs, or `--skip-existing` to resume a run where it stopped (the test cases that
are done must have been made with the same decoder, settings, genie and
statistics form, or the resume is refused before anything runs).

`kz4ap-bench --decoder` selects the decoder (`--front-end` is its old name and
still works). `--decoder matched` (the default) selects the dit-matched decoder
with frequency re-centering, each channel following the station the signal
detector assigns it (see `docs/signal-processing.md`, sections 6 and 8b);
`--decoder envelope` (also `baseline`) selects the milestone-1 pipeline;
`--decoder bank` selects the filter-bank decoder (section 8c), whose results
the suites write to `results/bank`; `--decoder viterbi` selects the new decoder
(per envelope sample, every 2.667 ms; section 8d is brought up to date at the
next checkpoint; selectable, not the default), which prints
its best story's text when the recording ends. Its settings are `--set
NAME=VALUE` (repeatable; NAME is a field of `ViterbiConfig` in
`engine/include/kz4ap/viterbi/config.hpp`, the value in the field's own
physical unit, and the result file's `config` block records every field), for
genie runs on oracle channels `--genie fist|fading` (repeatable; it
needs `--oracle` and takes the true timing, carrier or fading spread from the
labels), and `--stats FILE` (with `--oracle`), which writes the search's
statistics for each channel as JSON: a summary (live stories, how often the
ceiling decided, what became of each newborn by kind and end, and the newborns
that ever led), about 1.15 kB for a channel, or with `--stats-detail` every
newborn line, 45 kB per channel-second (the format, versions 2 and 1, is
documented in `bench/src/viterbi_json.hpp`, the reductions in
`training/kz4ap_proto/search_stats.py`). The result file is written first,
under a name of its own that is renamed when it is complete, then the
statistics file (`--json` and `--stats` may not name one file; a finished file
whose rename fails stays under its own name, `FILE.partial`, and the message
says where). For example, the wide-beam reference of the
first prototype's plan: `kz4ap-bench band.wav --labels band.json --oracle
--decoder viterbi --set beam_nats=80 --set ceiling=17000`. The bench writes
each track's final text (`text`, corrections applied) and its immediate text
(`text_immediate`, the characters as first published) and scores both (`cer`,
`cer_immediate`).

## Known limitations

- On the air, wide and digital signals (FT8, SSB) still open tracks: each is judged
  on its own evidence and retired within 4 to 14 s, and its text is not listed unless
  it reads as plausible, but about a third to two thirds of the rows listed on real 40 m
  are still junk, data signals most of all: 35% on a 62 s recording of an evening with a
  lot of CW, 63% on a 30 min one (`docs/backlog.md`, "The app on the air").
  The detector holds at most 400 tracks (`DetectorConfig::max_tracks`); at the cap a
  stronger new peak replaces the weakest track, and a pipeline behind a live radio
  gives up the tracks with the least evidence first.
- A station beside a neighbor at its own level or below is read from about 80 Hz away at any
  level, the neighbor's leak being counted as noise; at 60 Hz partly, at 40 Hz largely not. A
  neighbor +10 dB re the station needs about 100 Hz, and much stronger ones more
  (`docs/signal-processing.md`, A.8e).
- An I/Q radio's DC offset at the center frequency is filtered out (a notch about
  0.2 Hz wide), and a station beside it is read; the radio's raised noise near its center
  can open short tracks there that are judged and dropped. The default center stays
  7031.5 kHz, off the 7030 kHz calling frequency.
- Two stations closer than 30 Hz share one channel, and a station within about 45 Hz of a
  stronger one is read badly (`docs/signal-processing.md`, section 6 and A.6).
- Very strong signals (around 60 dB SNR in 500 Hz) can produce extra "ghost"
  tracks beside them.
- After a station stops, a few stray E's can be decoded from noise before its
  track is dropped.
- Default (Matched) path, measured on synthetic recordings
  (`docs/plans/2026-09-27-milestone-2a-results.md`, section 8.3, "Limits measured"):
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
