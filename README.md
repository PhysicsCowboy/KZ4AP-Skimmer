# KZ4AP Skimmer

An open-source CW (Morse code) skimmer and decoder. It listens to a whole band
at once through a software-defined radio, decodes every CW signal it finds,
and picks out the callsigns. It also works as a plain CW decoder that shows
the text alone.

Created by KZ4AP.

**Status:** early development. The desktop app runs on Windows and Linux, from
an I/Q recording or a live SDRplay radio (receive only).

## Features

- **Skimmer mode:** a waterfall up to 768 kHz wide, every CW signal decoded at
  once, and a call list with each call's frequency and when it was last heard.
  An optional list of known calls can be loaded or downloaded.
- **Decoder-only mode:** the waterfall and the decoded text of the signal you
  click, with no call search.
- Playback of I/Q recordings in WAV files.

## Building

Windows: Visual Studio Build Tools 2022 or later (C++ workload, with its
CMake) and Qt 6. Linux: GCC 11.4+ or Clang 17+, CMake 3.25+, Ninja, and Qt 6
for the app. The first configure downloads its other dependencies, so it needs
network access.

On Windows, run these from a Developer PowerShell, or put Build Tools' CMake on
PATH first ([developing.md](docs/developing.md)).

    cmake --preset windows -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64    # or: linux
    cmake --build --preset windows
    ctest --preset windows

## Running

    build\windows\app\Release\kz4ap-skimmer.exe RECORDING.wav

With no argument the app opens idle: press Open..., or enter your radio's
serial number and press Start radio. The SDRplay API (3.x) must be installed
for the radio. `--help` lists the options.

## More

- [User guide](docs/user-guide.md): the app's windows, the call list, settings,
  recordings and the radio.
- [Developing](docs/developing.md): building, tests and the benchmark.
- [Design](docs/design/2026-09-25-kz4ap-skimmer-design.md) and
  [signal processing](docs/signal-processing.md).

## License

GPL-3.0-or-later; see `LICENSE`. An additional permission under its section 7
allows linking closed-source radio driver libraries; see `LICENSE-EXCEPTION.md`.
