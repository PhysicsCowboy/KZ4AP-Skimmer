# KZ4AP Skimmer

An open-source skimmer for CW (Morse code): it listens to a whole band at
once through a software-defined radio, decodes every CW signal it finds, and
picks out the callsigns. An alternative to Afreet Software's CW Skimmer and
CW Skimmer Server.

Created by Kenton Randolph Brown, KZ4AP.

**Status:** early development. The decoding engine and its benchmark run on
recorded I/Q files; there is no user interface or live radio support yet.
See `docs/design/` for the design.

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

Every push runs the same check on Windows and Linux (`bench/smoke.sh`) and fails
if decoding gets worse than `bench/baselines/smoke.json`.

## Known limitations

- The first character or two of a transmission may be lost or wrong while the
  station is being detected.
- Very strong signals (around 60 dB SNR in 500 Hz) can produce extra "ghost"
  tracks beside them.
- After a station stops, a few stray E's can be decoded from noise before its
  track is dropped.

## License

GPL-3.0; see `LICENSE`. An additional permission for linking closed-source
SDR driver libraries (such as the SDRplay API) will be added before SDRplay
support lands.
