# Developing KZ4AP Skimmer

The engine, the benchmark and the tools are C++20 (`engine/`, `bench/`); the
desktop app is Qt 6 (`app/`). Python is used only for the test-signal
generator and the analysis of results (`training/`). How the signal
processing works is in [signal-processing.md](signal-processing.md).

## Building

Windows: Visual Studio Build Tools 2022 or later, C++ workload. Its CMake is
not on PATH; in PowerShell:

    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
    $env:PATH += ";$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"

Linux: GCC 11.4 or later or Clang 17 or later, CMake 3.25 or later, Ninja.

    cmake --preset windows      # or: linux
    cmake --build --preset windows
    ctest --preset windows

The `windows` preset builds in `build/windows` (Release configuration), the
`linux` preset in `build/linux` (Ninja, Release).

**Dependencies.** The first configure downloads GoogleTest, nlohmann/json and
SLEEF (vectorized math for the bank decoder) with CMake's FetchContent, so it
needs network access. Configure through the presets: with a
single-configuration generator and no `CMAKE_BUILD_TYPE`, SLEEF's CMake code
sets `CMAKE_BUILD_TYPE` to Release in the cache, which then applies to the
whole project from the next configure on.

**Qt.** The app needs Qt 6 Widgets and Network and is built only where CMake
finds them; everything else builds and tests the same without Qt. Point
`CMAKE_PREFIX_PATH` at your Qt, for example
`cmake --preset windows -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`. Without
it the configure step prints "Qt 6 Widgets and Network not found:
kz4ap-skimmer is not built". If you have no Qt, `python -m pip install
aqtinstall` then `python -m aqt install-qt windows desktop 6.8.3
win64_msvc2022_64 -O C:\Qt` installs one. On Windows the build copies Qt's
DLLs next to the program.

**SDRplay API.** Radio support is compiled when the API's headers are found
(`C:\Program Files\SDRplay\API\inc` on Windows, `/usr/local/include` on
Linux, or set `KZ4AP_SDRPLAY_INCLUDE_DIR`); turn it off with
`-DKZ4AP_WITH_SDRPLAY=OFF`. The library is loaded at run time, never linked;
the environment variable `KZ4AP_SDRPLAY_DLL` names another one. Without the
headers everything builds, and the app says so when asked for the radio.

## Tests

`ctest --preset windows` (or `linux`) runs the C++ tests (GoogleTest) except
the long ones; the presets `windows-full-sweep` / `linux-full-sweep` and
`windows-measurement` / `linux-measurement` run those labeled `full-sweep` and
`measurement`.

The Python tests need Python 3.12 (the version CI uses):

    python -m venv .venv
    .venv\Scripts\python -m pip install -r training/requirements.txt    # Linux: .venv/bin/python
    .venv\Scripts\python -m pytest training -q

The figure tests skip unless `training/requirements-analysis.txt` (matplotlib)
is installed too.

CI (`.github/workflows/ci.yml`) runs on Windows and Linux: the Python tests,
the build, ctest, and `bench/smoke.sh`, which decodes one generated recording
with the Envelope and Matched decoders, fails if either is worse than
`bench/baselines/smoke.json` or `bench/baselines/smoke-matched.json` (CER and
stations detected), and fails if two runs of the same decoder differ.

## Test-signal generator

`kz4ap_synth.generate` writes a synthetic I/Q recording and, beside it, a
labels file (`.json`) with each station's true text, frequency, speed and
level:

    $env:PYTHONPATH = "training"    # Linux: PYTHONPATH=training before the command
    .venv\Scripts\python -m kz4ap_synth.generate --scenario band --signals 8 --duration 30 --seed 1 --out build/synth/band.wav

`--scenario single|band`, `--signals N`, `--duration S` (s), `--sample-rate HZ`
(default 192000), `--seed N`, `--wpm-min`/`--wpm-max` (WPM), and
`--snr-min`/`--snr-max` (S₅₀₀: key-down carrier power over noise power in
500 Hz, dB; defaults 10 and 30). `--help` lists the rest.

## kz4ap-bench

`kz4ap-bench` runs the engine over an I/Q recording and, given the generator's
labels, scores the decoded text (character error rate, CER, and detection) and reports CPU time in ms per
channel-second:

    build\windows\bench\Release\kz4ap-bench.exe build/synth/band.wav --labels build/synth/band.json
    build/linux/bench/kz4ap-bench build/synth/band.wav --labels build/synth/band.json

`--decoder envelope|matched|bank|viterbi|segmental` selects the decoder; the
bench's default is `matched` (the app's is `segmental`). `--oracle` gives each
labeled station its own channel instead of the detector's; `--json FILE`
writes the results; `--set NAME=VALUE` (repeatable) sets a field of the
segmental decoder's configuration (`engine/include/kz4ap/segmental/config.hpp`)
or the viterbi decoder's (`engine/include/kz4ap/viterbi/config.hpp`), in the
field's own unit. Run it with no arguments for the full usage. Each track's
final text (`text`) and its text as first published (`text_immediate`) are
both scored (`cer`, `cer_immediate`).

`kz4ap-calls` runs the callsign stage over a recording without the app and
writes the call list as CSV; see the comment at the top of
`bench/src/calls_main.cpp`.

## Named suites

`kz4ap_synth.suites` generates many recordings at once, scores them with
`kz4ap-bench`, and summarizes CER, character and word-space errors,
first-word errors, the S₅₀₀ where CER crosses 0.10 and 0.05 (each with a
bootstrap 95% interval), and CPU time per channel-second:

    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.suites generate --suite full --seeds 3 --out build/suite/full
    .venv\Scripts\python -m kz4ap_synth.suites run --out build/suite/full --bench build\windows\bench\Release\kz4ap-bench.exe --decoder segmental --decoder matched
    .venv\Scripts\python -m kz4ap_synth.suites summarize --out build/suite/full

The suites are `smoke` (the CI recording), `full` (sized for `--seeds 3`),
`dev2` (the jittered development set) and `n2` (its noise-only group). A run
writes into `results/DECODER`, or `results/NAME` with `--name NAME`, and
refuses a folder that already holds results: give a new `--name`, or pass
`--overwrite` to replace the test cases it runs, or `--skip-existing` to
resume. `--jobs N` runs N bench processes at once. The module's docstring
(`training/kz4ap_synth/suites.py`) describes the groups.
