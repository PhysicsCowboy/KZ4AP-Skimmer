# Milestone 1: Decoding Pipeline and Benchmark — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A C++ engine that finds CW signals in an I/Q recording and decodes each one with a classical decoder, plus a benchmark tool that scores the decoded text against synthetic recordings with known answers.

**Architecture:** A Qt-free C++20 library (`engine/`) runs a synchronous pipeline: WAV source → shared-FFT spectrum analyzer → signal detector (tracks) → fast-convolution channelizer (one narrow baseband stream per track) → classical decoder per track → event bus. `bench/` drives the engine over a recording and scores it. `training/` holds a Python generator that makes synthetic recordings and their label files.

**Tech Stack:** C++20, CMake ≥ 3.25, GoogleTest 1.17.0, pocketfft (vendored header), nlohmann/json 3.12.0, Python 3.12 with numpy and pytest, GitHub Actions.

**Spec:** `docs/design/2026-09-25-kz4ap-skimmer-design.md`

## Scope of this milestone

Implements, from the spec: the engine library layout (§4), the file source, channelizer, signal detector, Classical decoder, and event bus of the data flow (§4.1), the determinism requirement (§4.1), the `bench/` tool with character error rate, detection, and real-time factor (§8.2), the synthetic tier of the benchmark recordings (§8.3), unit tests (§8.1), continuous integration with a baseline check and determinism check (§8.4), and the generator checks of §8.6.

Deliberately **not** in this milestone (each gets its own plan later):

- Callsign search, spots, callsign recall and busted-spot rate — waits for the callsign-matching design session (spec §6).
- Telnet spot server — needs spots.
- SDRplay source, ring buffer, worker threads, status events — arrive with live input. This milestone's engine is single-threaded and synchronous, which is what the benchmark's determinism needs anyway.
- Neural and Hybrid decoders, ONNX Runtime, model training.
- The Qt app.

## Global Constraints

- C++20; CMake minimum 3.25; the engine must not depend on Qt.
- Namespace `kz4ap`; public headers in `engine/include/kz4ap/`, sources in `engine/src/`, tests in `engine/tests/`.
- Program file names are lowercase-hyphenated: the benchmark executable is `kz4ap-bench` (the app, later, is `kz4ap-skimmer`).
- License GPL-3.0 (`LICENSE` at the repository root). Do not add per-file license headers yet: "GPL-3.0-only" vs "-or-later" is an open question (spec §9.2).
- Determinism: the same recording and settings must produce identical output, regardless of how input is split into calls.
- American spelling in code, comments, and docs.
- Commits: all work happens on the `milestone-1` branch; commit at the end of each task without waiting for approval (the user reviews the whole branch before it is merged). Never commit to `main`, merge, or push without the user's explicit approval. No `Co-Authored-By` or other attribution lines.
- I/Q convention: left channel = I (real), right channel = Q (imaginary).

## Environment (Windows, run once per PowerShell session)

CMake and Ninja ship with Visual Studio Build Tools 2026 but are not on `PATH`:

```powershell
$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:Path"
```

Common commands used throughout (from the repository root):

- Configure: `cmake --preset windows`
- Build: `cmake --build --preset windows`
- Run all C++ tests: `ctest --preset windows`
- Run matching C++ tests: `ctest --preset windows -R <regex>`
- Python tests: `.venv\Scripts\python -m pytest training -q`

## Review Focus

Inputs the spec implies but does not spell out, most likely to bite first. Each has a test in the owning task.

1. **Recordings that are not 16-bit stereo PCM** (mono, 24-bit, float): must fail with a clear message, never decode garbage — Task 3.
2. **Truncated recordings** (header claims more data than the file holds, as happens when a recorder crashes): read what is present — Task 3.
3. **Signals near the edge of the SDR span** (±sample_rate/2): the channel's bins wrap around the FFT without crashing or reading out of range — Task 5.
4. **Operators far from the default speed** (12 wpm and 45 wpm): the decoder adapts within a few characters — Task 7.
5. **A station that pauses for several seconds and resumes**: no garbage decoded from noise during the pause, and decoding resumes afterward — Task 7.

## File map

| File | Responsibility |
|---|---|
| `CMakeLists.txt`, `CMakePresets.json` | Build definition and presets (Windows, Linux) |
| `.gitattributes`, `.gitignore`, `LICENSE`, `README.md` | Repository basics |
| `third_party/pocketfft/` | Vendored FFT header (BSD-3-Clause) and its license |
| `engine/include/kz4ap/types.hpp` | `Sample` type |
| `engine/include/kz4ap/morse.hpp`, `engine/src/morse.cpp` | Morse code table |
| `engine/include/kz4ap/wav_reader.hpp`, `engine/src/wav_reader.cpp` | I/Q WAV file source |
| `engine/src/fft.hpp` | Private wrapper around pocketfft |
| `engine/include/kz4ap/spectrum.hpp`, `engine/src/spectrum.cpp` | Windowed power-spectrum frames |
| `engine/include/kz4ap/channelizer.hpp`, `engine/src/channelizer.cpp` | Per-track narrow baseband streams |
| `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp` | Tracks: finding CW carriers |
| `engine/include/kz4ap/decoder.hpp` | Decoder interface and decode result |
| `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp` | Classical decoder |
| `engine/include/kz4ap/event_bus.hpp` | Events and the bus |
| `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp` | Pipeline glue |
| `engine/tests/*` | GoogleTest unit tests; `test_signals.hpp` makes keyed test signals |
| `bench/src/labels.*`, `bench/src/scoring.*`, `bench/src/main.cpp` | Label parsing, scoring, `kz4ap-bench` CLI |
| `bench/tests/*` | Tests for labels and scoring |
| `bench/baselines/smoke.json`, `bench/smoke.sh` | CI benchmark baseline and smoke script |
| `training/kz4ap_synth/*`, `training/tests/*` | Synthetic recording generator and its tests |
| `.github/workflows/ci.yml` | Continuous integration |

---

### Task 1: Repository skeleton, build, and Morse table

**Files:**
- Create: `.gitattributes`, `.gitignore`, `LICENSE`, `README.md`, `CMakeLists.txt`, `CMakePresets.json`
- Create: `engine/CMakeLists.txt`, `engine/include/kz4ap/types.hpp`, `engine/include/kz4ap/morse.hpp`, `engine/src/morse.cpp`
- Test: `engine/tests/morse_test.cpp`

**Interfaces:**
- Produces: `kz4ap::Sample` (= `std::complex<float>`); `std::string_view kz4ap::morse::encode(char)`; `char kz4ap::morse::decode(std::string_view)`. CMake target `kz4ap::engine`, test executable `kz4ap_engine_tests`.

- [ ] **Step 1: Repository basics**

`.gitattributes`:
```
* text=auto eol=lf
*.bat text eol=crlf
*.wav binary
```

`.gitignore`:
```
/build/
/.venv/
__pycache__/
.pytest_cache/
```

`LICENSE`: download the GPL-3.0 text verbatim:
```powershell
curl.exe -sSfL https://www.gnu.org/licenses/gpl-3.0.txt -o LICENSE
```

`README.md`:
```markdown
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

## License

GPL-3.0; see `LICENSE`. An additional permission for linking closed-source
SDR driver libraries (such as the SDRplay API) will be added before SDRplay
support lands.
```

- [ ] **Step 2: CMake files**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.25)
project(kz4ap_skimmer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_subdirectory(engine)
```

`CMakePresets.json`:
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "windows",
      "binaryDir": "${sourceDir}/build/windows",
      "architecture": { "value": "x64", "strategy": "set" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    },
    {
      "name": "linux",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" }
    }
  ],
  "buildPresets": [
    { "name": "windows", "configurePreset": "windows", "configuration": "Release" },
    { "name": "linux", "configurePreset": "linux" }
  ],
  "testPresets": [
    { "name": "windows", "configurePreset": "windows", "configuration": "Release",
      "output": { "outputOnFailure": true } },
    { "name": "linux", "configurePreset": "linux",
      "output": { "outputOnFailure": true } }
  ]
}
```
(The Windows preset leaves the generator unset so CMake picks the newest Visual Studio installed — 2026 locally, whatever GitHub's runner has in CI.)

`engine/CMakeLists.txt`:
```cmake
add_library(kz4ap_engine
  src/morse.cpp
)
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_include_directories(kz4ap_engine PUBLIC include PRIVATE src)
target_compile_features(kz4ap_engine PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(kz4ap_engine PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_engine_tests
  tests/morse_test.cpp
)
target_link_libraries(kz4ap_engine_tests PRIVATE kz4ap::engine GTest::gtest_main)
gtest_discover_tests(kz4ap_engine_tests)
```

- [ ] **Step 3: Write the failing test**

`engine/tests/morse_test.cpp`:
```cpp
#include "kz4ap/morse.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace kz4ap;

TEST(Morse, EncodesLettersCaseInsensitively) {
    EXPECT_EQ(morse::encode('A'), ".-");
    EXPECT_EQ(morse::encode('a'), ".-");
    EXPECT_EQ(morse::encode('K'), "-.-");
}

TEST(Morse, EncodesDigitsAndPunctuation) {
    EXPECT_EQ(morse::encode('5'), ".....");
    EXPECT_EQ(morse::encode('0'), "-----");
    EXPECT_EQ(morse::encode('/'), "-..-.");
    EXPECT_EQ(morse::encode('?'), "..--..");
}

TEST(Morse, CharacterWithoutCodeIsEmpty) {
    EXPECT_TRUE(morse::encode('#').empty());
    EXPECT_TRUE(morse::encode(' ').empty());
}

TEST(Morse, DecodesPatterns) {
    EXPECT_EQ(morse::decode("-.-"), 'K');
    EXPECT_EQ(morse::decode("...--"), '3');
    EXPECT_EQ(morse::decode("........"), '\0');
    EXPECT_EQ(morse::decode(""), '\0');
}

TEST(Morse, EveryCodeRoundTrips) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?.,=+-(";
    for (char c : chars) {
        const auto pattern = morse::encode(c);
        ASSERT_FALSE(pattern.empty()) << c;
        EXPECT_EQ(morse::decode(pattern), c) << c;
    }
}
```

`engine/include/kz4ap/types.hpp`:
```cpp
#pragma once

#include <complex>

namespace kz4ap {

// One complex baseband sample: the real part is I, the imaginary part is Q.
using Sample = std::complex<float>;

}  // namespace kz4ap
```

`engine/include/kz4ap/morse.hpp`:
```cpp
#pragma once

#include <string_view>

namespace kz4ap::morse {

// Dot/dash pattern for a character (case-insensitive), e.g. ".-" for 'A'.
// Returns an empty view for characters that have no Morse code.
std::string_view encode(char c);

// Character for a dot/dash pattern, or '\0' if the pattern is not a known code.
char decode(std::string_view pattern);

}  // namespace kz4ap::morse
```

`engine/src/morse.cpp` (stub so the build links):
```cpp
#include "kz4ap/morse.hpp"

namespace kz4ap::morse {

std::string_view encode(char) { return {}; }
char decode(std::string_view) { return '\0'; }

}  // namespace kz4ap::morse
```

- [ ] **Step 4: Configure, build, run — expect failures**

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -R Morse
```
Expected: configure downloads GoogleTest; build succeeds; `EncodesLettersCaseInsensitively`, `EncodesDigitsAndPunctuation`, `DecodesPatterns`, `EveryCodeRoundTrips` FAIL.

- [ ] **Step 5: Implement**

`engine/src/morse.cpp`:
```cpp
#include "kz4ap/morse.hpp"

#include <cctype>

namespace kz4ap::morse {
namespace {

struct Code {
    char ch;
    std::string_view pattern;
};

// '+' is the prosign AR, '=' is BT, '(' is KN.
constexpr Code kCodes[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
    {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
    {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
    {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'/', "-..-."}, {'?', "..--.."}, {'.', ".-.-.-"}, {',', "--..--"},
    {'=', "-...-"}, {'+', ".-.-."}, {'-', "-....-"}, {'(', "-.--."},
};

}  // namespace

std::string_view encode(char c) {
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& code : kCodes) {
        if (code.ch == upper) return code.pattern;
    }
    return {};
}

char decode(std::string_view pattern) {
    for (const auto& code : kCodes) {
        if (code.pattern == pattern) return code.ch;
    }
    return '\0';
}

}  // namespace kz4ap::morse
```

- [ ] **Step 6: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows -R Morse
```
Expected: 5 tests pass.

- [ ] **Step 7: Commit on the `milestone-1` branch**

```powershell
git add .gitattributes .gitignore LICENSE README.md CMakeLists.txt CMakePresets.json engine
```
Commit message: `Add build skeleton, license, and Morse code table`

---

### Task 2: Synthetic recording generator (Python)

**Files:**
- Create: `training/requirements.txt`, `training/pyproject.toml`, `training/kz4ap_synth/__init__.py`, `training/kz4ap_synth/morse.py`, `training/kz4ap_synth/generate.py`
- Test: `training/tests/test_morse.py`, `training/tests/test_generate.py`

**Interfaces:**
- Produces: CLI `python -m kz4ap_synth.generate --scenario {single,band} [--signals N] [--duration S] [--sample-rate R] [--seed K] --out FILE.wav`, which writes `FILE.wav` (16-bit PCM stereo, I left, Q right) and `FILE.json`:
  ```json
  {"sample_rate": 192000, "duration_s": 30.0, "snr_bandwidth_hz": 500.0,
   "signals": [{"text": "CQ TEST K1ABC K1ABC", "freq_offset_hz": 1000.0, "wpm": 25.0,
                "snr_db": 20.0, "start_s": 0.5, "end_s": 7.1}]}
  ```
  SNR is carrier power over noise power in 500 Hz. Task 9 parses this file.

- [ ] **Step 1: Environment**

`training/requirements.txt`:
```
numpy>=2.0
pytest>=8.0
```

`training/pyproject.toml`:
```toml
[tool.pytest.ini_options]
testpaths = ["tests"]
pythonpath = ["."]
```

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r training/requirements.txt
```

`training/kz4ap_synth/__init__.py`:
```python
"""Synthetic CW recordings with known answers."""
```

- [ ] **Step 2: Write the failing tests**

`training/tests/test_morse.py`:
```python
import pytest

from kz4ap_synth.morse import CODES, dit_seconds, keying_intervals


def flat(intervals):
    return [x for interval in intervals for x in interval]


def test_dit_length_follows_paris_standard():
    assert dit_seconds(20) == pytest.approx(0.06)


def test_single_dit():
    assert flat(keying_intervals("E", 20)) == pytest.approx([0.0, 0.06])


def test_dit_dah_with_element_gap():
    assert flat(keying_intervals("A", 20)) == pytest.approx([0.0, 0.06, 0.12, 0.30])


def test_character_gap_is_three_dits():
    assert flat(keying_intervals("EE", 20)) == pytest.approx([0.0, 0.06, 0.24, 0.30])


def test_word_gap_is_seven_dits():
    assert flat(keying_intervals("E E", 20)) == pytest.approx([0.0, 0.06, 0.48, 0.54])


def test_unknown_characters_are_skipped():
    assert keying_intervals("E#", 20) == keying_intervals("E", 20)


def test_table_matches_engine_table():
    assert len(CODES) == 44
    assert CODES["K"] == "-.-"
    assert CODES["("] == "-.--."
```

`training/tests/test_generate.py`:
```python
import json
import wave

import numpy as np
import pytest

from kz4ap_synth.generate import (
    DEFAULT_NOISE_SIGMA,
    SignalSpec,
    amplitude_for_snr,
    generate,
    main,
    write_wav,
)
from kz4ap_synth.morse import keying_intervals


def test_noise_power_matches_sigma():
    iq = generate([], 8000, 4.0, seed=1)
    assert np.mean(np.abs(iq) ** 2) == pytest.approx(DEFAULT_NOISE_SIGMA**2, rel=0.05)


def test_tone_lands_on_its_frequency():
    fs = 48000
    iq = generate([SignalSpec("TTTTT", 1500.0, 20.0, 30.0, 0.0)], fs, 3.0, seed=1, add_noise=False)
    spectrum = np.abs(np.fft.fft(iq))
    peak_hz = np.fft.fftfreq(len(iq), 1 / fs)[np.argmax(spectrum)]
    assert peak_hz == pytest.approx(1500.0, abs=fs / len(iq))


def test_keying_follows_intervals():
    fs = 8000
    spec = SignalSpec("PARIS", 1000.0, 20.0, 20.0, 0.5)
    iq = generate([spec], fs, 5.0, seed=1, add_noise=False)
    amplitude = amplitude_for_snr(20.0, fs)
    intervals = keying_intervals("PARIS", 20.0)
    for on, off in intervals:
        mid = int((spec.start_s + (on + off) / 2) * fs)
        assert abs(iq[mid]) == pytest.approx(amplitude, rel=1e-6)
    for (_, off), (next_on, _) in zip(intervals, intervals[1:]):
        mid = int((spec.start_s + (off + next_on) / 2) * fs)
        assert abs(iq[mid]) < amplitude * 1e-6


def test_same_seed_same_output():
    spec = [SignalSpec("CQ", 500.0, 25.0, 10.0, 0.1)]
    a = generate(spec, 8000, 2.0, seed=7)
    b = generate(spec, 8000, 2.0, seed=7)
    c = generate(spec, 8000, 2.0, seed=8)
    assert np.array_equal(a, b)
    assert not np.array_equal(a, c)


def test_wav_is_16_bit_stereo_iq(tmp_path):
    iq = np.array([0.5 + 0.25j, -0.5 - 0.25j, 0.0 + 0.0j])
    path = tmp_path / "x.wav"
    write_wav(path, iq, 48000)
    with wave.open(str(path), "rb") as w:
        assert w.getnchannels() == 2
        assert w.getsampwidth() == 2
        assert w.getframerate() == 48000
        raw = np.frombuffer(w.readframes(3), dtype="<i2").reshape(3, 2)
    assert raw[0, 0] == round(0.5 * 32767)
    assert raw[0, 1] == round(0.25 * 32767)
    assert raw[1, 0] == -round(0.5 * 32767)


def test_wav_scales_down_instead_of_clipping(tmp_path):
    path = tmp_path / "loud.wav"
    write_wav(path, np.array([2.0 + 0.0j, -1.0 + 0.0j]), 48000)
    with wave.open(str(path), "rb") as w:
        raw = np.frombuffer(w.readframes(2), dtype="<i2").reshape(2, 2)
    assert raw[0, 0] == round(0.95 * 32767)
    assert raw[1, 0] == -round(0.475 * 32767)


def test_band_scenario_writes_labels(tmp_path):
    out = tmp_path / "band.wav"
    main(["--scenario", "band", "--signals", "5", "--duration", "10",
          "--sample-rate", "48000", "--seed", "3", "--out", str(out)])
    labels = json.loads(out.with_suffix(".json").read_text())
    assert labels["sample_rate"] == 48000
    assert labels["snr_bandwidth_hz"] == 500.0
    signals = labels["signals"]
    assert len(signals) == 5
    freqs = sorted(s["freq_offset_hz"] for s in signals)
    assert all(b - a >= 1000 for a, b in zip(freqs, freqs[1:]))
    for s in signals:
        assert s["text"]
        assert 0 <= s["start_s"] < s["end_s"] <= 10.0
    with wave.open(str(out), "rb") as w:
        assert w.getnframes() == 480000
```

- [ ] **Step 3: Run — expect failure**

```powershell
.venv\Scripts\python -m pytest training -q
```
Expected: collection errors, `ModuleNotFoundError: No module named 'kz4ap_synth.morse'`.

- [ ] **Step 4: Implement**

`training/kz4ap_synth/morse.py`:
```python
"""Morse code table and PARIS-standard keying timing.

Must stay in step with engine/src/morse.cpp.
"""

CODES = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".", "F": "..-.",
    "G": "--.", "H": "....", "I": "..", "J": ".---", "K": "-.-", "L": ".-..",
    "M": "--", "N": "-.", "O": "---", "P": ".--.", "Q": "--.-", "R": ".-.",
    "S": "...", "T": "-", "U": "..-", "V": "...-", "W": ".--", "X": "-..-",
    "Y": "-.--", "Z": "--..",
    "0": "-----", "1": ".----", "2": "..---", "3": "...--", "4": "....-",
    "5": ".....", "6": "-....", "7": "--...", "8": "---..", "9": "----.",
    "/": "-..-.", "?": "..--..", ".": ".-.-.-", ",": "--..--",
    "=": "-...-", "+": ".-.-.", "-": "-....-", "(": "-.--.",
}


def dit_seconds(wpm: float) -> float:
    """Length of one dit at the given speed (PARIS standard: 50 dits per word)."""
    return 1.2 / wpm


def keying_intervals(text: str, wpm: float) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, starting at time 0.

    Gaps: 1 dit between elements, 3 between characters, 7 between words.
    Characters with no Morse code are skipped.
    """
    dit = dit_seconds(wpm)
    intervals = []
    t = 0.0
    words = text.upper().split()
    for wi, word in enumerate(words):
        patterns = [CODES[c] for c in word if c in CODES]
        for ci, pattern in enumerate(patterns):
            for ei, element in enumerate(pattern):
                length = dit if element == "." else 3 * dit
                intervals.append((t, t + length))
                t += length
                if ei < len(pattern) - 1:
                    t += dit
            if ci < len(patterns) - 1:
                t += 3 * dit
        if wi < len(words) - 1:
            t += 7 * dit
    return intervals
```

`training/kz4ap_synth/generate.py`:
```python
"""Synthetic CW I/Q recordings with known answers, for testing and benchmarking.

Writes a 16-bit stereo WAV file (left = I, right = Q) and, next to it, a JSON
labels file describing every signal in it.
"""

from __future__ import annotations

import argparse
import json
import wave
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np

from .morse import keying_intervals

SNR_BANDWIDTH_HZ = 500.0
DEFAULT_NOISE_SIGMA = 0.02
RISE_S = 0.005

CALL_PREFIXES = ["K", "W", "N", "AA", "KB", "DL", "G", "F", "JA", "VE",
                 "EA", "I", "OH", "SM", "UA", "PY", "VK", "ZL"]
MESSAGES = ["CQ TEST {c} {c}", "CQ CQ DE {c} {c} K", "TU {c}", "{c} 5NN 14", "CQ {c} {c} TEST"]


@dataclass
class SignalSpec:
    text: str
    freq_offset_hz: float
    wpm: float
    snr_db: float
    start_s: float


def amplitude_for_snr(snr_db: float, sample_rate: int) -> float:
    """Carrier amplitude giving snr_db against DEFAULT_NOISE_SIGMA noise, measured in 500 Hz."""
    noise_in_band = DEFAULT_NOISE_SIGMA**2 * SNR_BANDWIDTH_HZ / sample_rate
    return float(np.sqrt(10 ** (snr_db / 10) * noise_in_band))


def keying_envelope(intervals, offset_s: float, n: int, sample_rate: int) -> np.ndarray:
    """0..1 envelope with raised-cosine edges; intervals are shifted by offset_s."""
    env = np.zeros(n)
    ramp_len = max(1, int(round(RISE_S * sample_rate)))
    ramp = 0.5 - 0.5 * np.cos(np.pi * (np.arange(ramp_len) + 0.5) / ramp_len)
    for on, off in intervals:
        i0 = max(0, int(round((offset_s + on) * sample_rate)))
        i1 = min(n, int(round((offset_s + off) * sample_rate)))
        if i1 <= i0:
            continue
        env[i0:i1] = 1.0
        r = min(ramp_len, (i1 - i0) // 2)
        if r > 0:
            env[i0:i0 + r] *= ramp[:r]
            env[i1 - r:i1] *= ramp[:r][::-1]
    return env


def generate(signals, sample_rate: int, duration_s: float, seed: int, add_noise: bool = True) -> np.ndarray:
    """Complex I/Q samples containing the given signals plus white noise."""
    rng = np.random.default_rng(seed)
    n = int(round(duration_s * sample_rate))
    iq = np.zeros(n, dtype=np.complex128)
    if add_noise:
        iq += (rng.standard_normal(n) + 1j * rng.standard_normal(n)) * (DEFAULT_NOISE_SIGMA / np.sqrt(2))
    for s in signals:
        phase = rng.uniform(0, 2 * np.pi)
        intervals = keying_intervals(s.text, s.wpm)
        if not intervals:
            continue
        i0 = max(0, int(s.start_s * sample_rate))
        i1 = min(n, int(np.ceil((s.start_s + intervals[-1][1]) * sample_rate)) + 1)
        if i1 <= i0:
            continue
        env = keying_envelope(intervals, s.start_s - i0 / sample_rate, i1 - i0, sample_rate)
        t = np.arange(i0, i1) / sample_rate
        carrier = np.exp(1j * (2 * np.pi * s.freq_offset_hz * t + phase))
        iq[i0:i1] += amplitude_for_snr(s.snr_db, sample_rate) * env * carrier
    return iq


def write_wav(path, iq: np.ndarray, sample_rate: int) -> None:
    """Write I/Q as 16-bit stereo PCM, scaling everything down if needed to avoid clipping."""
    peak = max(float(np.max(np.abs(iq.real))), float(np.max(np.abs(iq.imag))), 1e-12)
    scale = min(1.0, 0.95 / peak)
    stereo = np.empty((len(iq), 2), dtype="<i2")
    stereo[:, 0] = np.round(iq.real * scale * 32767)
    stereo[:, 1] = np.round(iq.imag * scale * 32767)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(stereo.tobytes())


def labels(signals, sample_rate: int, duration_s: float) -> dict:
    entries = []
    for s in signals:
        intervals = keying_intervals(s.text, s.wpm)
        end = s.start_s + (intervals[-1][1] if intervals else 0.0)
        entries.append({**asdict(s), "end_s": round(end, 3)})
    return {
        "sample_rate": sample_rate,
        "duration_s": duration_s,
        "snr_bandwidth_hz": SNR_BANDWIDTH_HZ,
        "signals": entries,
    }


def random_callsign(rng) -> str:
    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    prefix = CALL_PREFIXES[rng.integers(len(CALL_PREFIXES))]
    suffix = "".join(letters[rng.integers(26)] for _ in range(int(rng.integers(1, 4))))
    return f"{prefix}{rng.integers(10)}{suffix}"


def fill_text(message: str, wpm: float, available_s: float) -> str:
    """Repeat message, space-separated, as many times as fits in available_s."""
    text = message
    while True:
        candidate = f"{text} {message}"
        if keying_intervals(candidate, wpm)[-1][1] > available_s:
            return text
        text = candidate


def scenario_single() -> list[SignalSpec]:
    return [SignalSpec("CQ TEST K1ABC K1ABC", 1000.0, 25.0, 20.0, 0.5)]


def scenario_band(rng, count: int, duration_s: float, sample_rate: int) -> list[SignalSpec]:
    """count signals spread over 80% of the span, at least 1 kHz apart."""
    span = 0.4 * sample_rate
    specs: list[SignalSpec] = []
    while len(specs) < count:
        freq = round(float(rng.uniform(-span, span)), 1)
        if any(abs(freq - s.freq_offset_hz) < 1000 for s in specs):
            continue
        wpm = round(float(rng.uniform(18, 36)), 1)
        snr = round(float(rng.uniform(10, 30)), 1)
        start = round(float(rng.uniform(0, 2)), 3)
        message = MESSAGES[rng.integers(len(MESSAGES))].format(c=random_callsign(rng))
        text = fill_text(message, wpm, duration_s - start - 1.0)
        specs.append(SignalSpec(text, freq, wpm, snr, start))
    return specs


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["single", "band"], default="single")
    parser.add_argument("--signals", type=int, default=10)
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--sample-rate", type=int, default=192000)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--out", type=Path, required=True,
                        help="output .wav file; the labels go next to it as .json")
    args = parser.parse_args(argv)

    rng = np.random.default_rng(args.seed)
    if args.scenario == "single":
        specs = scenario_single()
    else:
        specs = scenario_band(rng, args.signals, args.duration, args.sample_rate)
    iq = generate(specs, args.sample_rate, args.duration, seed=args.seed + 1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_wav(args.out, iq, args.sample_rate)
    args.out.with_suffix(".json").write_text(
        json.dumps(labels(specs, args.sample_rate, args.duration), indent=2) + "\n")


if __name__ == "__main__":
    main()
```

- [ ] **Step 5: Run — expect pass**

```powershell
.venv\Scripts\python -m pytest training -q
```
Expected: 15 passed.

- [ ] **Step 6: Commit on the `milestone-1` branch**

```powershell
git add training
```
Commit message: `Add synthetic CW recording generator`

---

### Task 3: I/Q WAV reader

**Files:**
- Create: `engine/include/kz4ap/wav_reader.hpp`, `engine/src/wav_reader.cpp`
- Modify: `engine/CMakeLists.txt` (add `src/wav_reader.cpp` and `tests/wav_reader_test.cpp`)
- Test: `engine/tests/wav_reader_test.cpp`

**Interfaces:**
- Consumes: `kz4ap::Sample`.
- Produces: `class kz4ap::WavIqReader { explicit WavIqReader(const std::filesystem::path&); int sample_rate() const; std::uint64_t total_samples() const; std::size_t read(std::span<Sample> out); }`. Constructor throws `std::runtime_error` with a readable message.

- [ ] **Step 1: Write the failing test**

`engine/tests/wav_reader_test.cpp`:
```cpp
#include "kz4ap/wav_reader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;

namespace {

void put_u16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}

void put_u32(std::string& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

struct WavSpec {
    int rate = 48000;
    int channels = 2;
    int bits = 16;
    std::vector<std::int16_t> samples;       // interleaved
    bool extra_chunk = false;                // odd-sized LIST chunk before data
    std::optional<std::uint32_t> data_size;  // override the data chunk's declared size
};

std::string wav_bytes(const WavSpec& s) {
    std::string fmt;
    put_u16(fmt, 1);
    put_u16(fmt, static_cast<std::uint16_t>(s.channels));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate));
    put_u32(fmt, static_cast<std::uint32_t>(s.rate * s.channels * s.bits / 8));
    put_u16(fmt, static_cast<std::uint16_t>(s.channels * s.bits / 8));
    put_u16(fmt, static_cast<std::uint16_t>(s.bits));
    std::string data;
    for (auto v : s.samples) put_u16(data, static_cast<std::uint16_t>(v));

    std::string body = "WAVE";
    body += "fmt ";
    put_u32(body, 16);
    body += fmt;
    if (s.extra_chunk) {
        body += "LIST";
        put_u32(body, 3);
        body += "abc";
        body.push_back('\0');  // pad byte for the odd size
    }
    body += "data";
    put_u32(body, s.data_size.value_or(static_cast<std::uint32_t>(data.size())));
    body += data;

    std::string file = "RIFF";
    put_u32(file, static_cast<std::uint32_t>(body.size()));
    return file + body;
}

class WavIqReaderTest : public ::testing::Test {
protected:
    std::filesystem::path path_;

    void SetUp() override {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        path_ = std::filesystem::temp_directory_path() /
                (std::string("kz4ap_") + info->name() + ".wav");
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    void write(const WavSpec& spec) {
        const auto bytes = wav_bytes(spec);
        std::ofstream(path_, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
};

}  // namespace

TEST_F(WavIqReaderTest, ReadsSampleRateAndLength) {
    write({.rate = 96000, .samples = {1, 2, 3, 4, 5, 6, 7, 8}});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.sample_rate(), 96000);
    EXPECT_EQ(reader.total_samples(), 4u);
}

TEST_F(WavIqReaderTest, ScalesLeftToIAndRightToQ) {
    write({.samples = {16384, -16384, 32767, -32768}});
    WavIqReader reader(path_);
    std::vector<Sample> out(2);
    ASSERT_EQ(reader.read(out), 2u);
    EXPECT_FLOAT_EQ(out[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(out[0].imag(), -0.5f);
    EXPECT_FLOAT_EQ(out[1].real(), 32767.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[1].imag(), -1.0f);
}

TEST_F(WavIqReaderTest, ReadsInChunksUntilEnd) {
    write({.samples = {1, 0, 2, 0, 3, 0, 4, 0, 5, 0}});
    WavIqReader reader(path_);
    std::vector<Sample> out(2);
    EXPECT_EQ(reader.read(out), 2u);
    EXPECT_EQ(reader.read(out), 2u);
    ASSERT_EQ(reader.read(out), 1u);
    EXPECT_FLOAT_EQ(out[0].real(), 5.0f / 32768.0f);
    EXPECT_EQ(reader.read(out), 0u);
}

TEST_F(WavIqReaderTest, SkipsUnknownChunksIncludingPadByte) {
    write({.samples = {100, 200}, .extra_chunk = true});
    WavIqReader reader(path_);
    std::vector<Sample> out(1);
    ASSERT_EQ(reader.read(out), 1u);
    EXPECT_FLOAT_EQ(out[0].real(), 100.0f / 32768.0f);
    EXPECT_FLOAT_EQ(out[0].imag(), 200.0f / 32768.0f);
}

TEST_F(WavIqReaderTest, RejectsMono) {
    write({.channels = 1, .samples = {1, 2}});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, Rejects24Bit) {
    write({.bits = 24, .samples = {1, 2, 3}});
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, RejectsMissingFile) {
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, RejectsNonWavFile) {
    std::ofstream(path_, std::ios::binary) << "this is not a wav file";
    EXPECT_THROW(WavIqReader{path_}, std::runtime_error);
}

TEST_F(WavIqReaderTest, TruncatedFileReadsWhatIsPresent) {
    write({.samples = {1, 2, 3, 4, 5, 6}, .data_size = 4000});
    WavIqReader reader(path_);
    EXPECT_EQ(reader.total_samples(), 3u);
    std::vector<Sample> out(10);
    EXPECT_EQ(reader.read(out), 3u);
}
```

`engine/include/kz4ap/wav_reader.hpp`:
```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace kz4ap {

// Reads I/Q recordings stored as 16-bit PCM stereo WAV files (left = I, right = Q).
class WavIqReader {
public:
    // Throws std::runtime_error if the file cannot be opened or is not 16-bit stereo PCM.
    explicit WavIqReader(const std::filesystem::path& path);

    int sample_rate() const { return sample_rate_; }

    // Number of I/Q samples in the file; fewer than the header claims if the file is truncated.
    std::uint64_t total_samples() const { return total_samples_; }

    // Fills out with the next samples, scaled to [-1, 1). Returns how many were read; 0 at the end.
    std::size_t read(std::span<Sample> out);

private:
    std::ifstream file_;
    int sample_rate_ = 0;
    std::uint64_t total_samples_ = 0;
    std::uint64_t samples_read_ = 0;
    std::vector<std::int16_t> raw_;
};

}  // namespace kz4ap
```

`engine/src/wav_reader.cpp` (stub):
```cpp
#include "kz4ap/wav_reader.hpp"

namespace kz4ap {

WavIqReader::WavIqReader(const std::filesystem::path&) {}
std::size_t WavIqReader::read(std::span<Sample>) { return 0; }

}  // namespace kz4ap
```

`engine/CMakeLists.txt`: change the two source lists to
```cmake
add_library(kz4ap_engine
  src/morse.cpp
  src/wav_reader.cpp
)
```
```cmake
add_executable(kz4ap_engine_tests
  tests/morse_test.cpp
  tests/wav_reader_test.cpp
)
```

- [ ] **Step 2: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R WavIqReader
```
Expected: all 9 tests FAIL.

- [ ] **Step 3: Implement**

`engine/src/wav_reader.cpp`:
```cpp
#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
#include <string>

static_assert(std::endian::native == std::endian::little, "WAV parsing assumes a little-endian host");

namespace kz4ap {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;

template <typename T>
T read_le(std::ifstream& f) {
    T value{};
    f.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!f) throw std::runtime_error("WAV file ended unexpectedly");
    return value;
}

std::string read_tag(std::ifstream& f) {
    std::array<char, 4> tag{};
    f.read(tag.data(), 4);
    if (!f) throw std::runtime_error("WAV file ended unexpectedly");
    return std::string(tag.data(), 4);
}

}  // namespace

WavIqReader::WavIqReader(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    const std::string name = path.string();
    if (!file_) throw std::runtime_error("cannot open " + name);
    try {
        if (read_tag(file_) != "RIFF") throw std::runtime_error("not a WAV file");
        read_le<std::uint32_t>(file_);
        if (read_tag(file_) != "WAVE") throw std::runtime_error("not a WAV file");

        bool have_format = false;
        while (true) {
            if (file_.peek() == std::char_traits<char>::eof()) throw std::runtime_error("no data chunk");
            const std::string id = read_tag(file_);
            const auto size = read_le<std::uint32_t>(file_);
            if (id == "fmt ") {
                if (size < 16) throw std::runtime_error("malformed format chunk");
                auto format = read_le<std::uint16_t>(file_);
                const auto channels = read_le<std::uint16_t>(file_);
                sample_rate_ = static_cast<int>(read_le<std::uint32_t>(file_));
                read_le<std::uint32_t>(file_);  // byte rate
                read_le<std::uint16_t>(file_);  // block align
                const auto bits = read_le<std::uint16_t>(file_);
                std::uint32_t consumed = 16;
                if (format == kFormatExtensible && size >= 26) {
                    read_le<std::uint16_t>(file_);  // extension size
                    read_le<std::uint16_t>(file_);  // valid bits
                    read_le<std::uint32_t>(file_);  // channel mask
                    format = read_le<std::uint16_t>(file_);  // first two bytes of the sub-format GUID
                    consumed = 26;
                }
                if (format != kFormatPcm) throw std::runtime_error("only integer PCM WAV files are supported");
                if (channels != 2)
                    throw std::runtime_error("needs 2 channels (I and Q), found " + std::to_string(channels));
                if (bits != 16) throw std::runtime_error("needs 16-bit samples, found " + std::to_string(bits) + "-bit");
                file_.seekg(static_cast<std::streamoff>(size - consumed + (size & 1)), std::ios::cur);
                have_format = true;
            } else if (id == "data") {
                if (!have_format) throw std::runtime_error("data chunk comes before format chunk");
                const auto data_start = file_.tellg();
                file_.seekg(0, std::ios::end);
                const auto available = static_cast<std::uint64_t>(file_.tellg() - data_start);
                file_.seekg(data_start);
                total_samples_ = std::min<std::uint64_t>(size, available) / 4;
                return;
            } else {
                file_.seekg(static_cast<std::streamoff>(size + (size & 1)), std::ios::cur);
            }
        }
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(name + ": " + e.what());
    }
}

std::size_t WavIqReader::read(std::span<Sample> out) {
    const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), total_samples_ - samples_read_));
    if (n == 0) return 0;
    raw_.resize(2 * n);
    file_.read(reinterpret_cast<char*>(raw_.data()), static_cast<std::streamsize>(raw_.size() * sizeof(std::int16_t)));
    const auto got = static_cast<std::size_t>(file_.gcount()) / 4;
    for (std::size_t i = 0; i < got; ++i) {
        out[i] = Sample(raw_[2 * i] / 32768.0f, raw_[2 * i + 1] / 32768.0f);
    }
    samples_read_ += got;
    return got;
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass (Morse and WavIqReader).

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add engine
```
Commit message: `Add I/Q WAV file reader`

---

### Task 4: Spectrum analyzer (with vendored pocketfft)

**Files:**
- Create: `third_party/pocketfft/pocketfft_hdronly.h`, `third_party/pocketfft/LICENSE.md`, `third_party/pocketfft/VERSION`
- Create: `engine/src/fft.hpp`, `engine/include/kz4ap/spectrum.hpp`, `engine/src/spectrum.cpp`
- Modify: `engine/CMakeLists.txt`
- Test: `engine/tests/spectrum_test.cpp`

**Interfaces:**
- Consumes: `kz4ap::Sample`.
- Produces: `struct kz4ap::SpectrumFrame { double time_s; std::vector<float> power_db; }` where `power_db[i]` is the power at `(i - fft_size/2) * sample_rate / fft_size` Hz, and a tone of amplitude A reads `20*log10(A)` dB at its bin. `class kz4ap::SpectrumAnalyzer { SpectrumAnalyzer(int sample_rate, int fft_size, int hop); std::vector<SpectrumFrame> push(std::span<const Sample>); double bin_to_hz(int index) const; }`. Private helpers `kz4ap::detail::fft_forward(const Sample*, Sample*, std::size_t)` and `fft_inverse(...)` (both unnormalized).

- [ ] **Step 1: Vendor pocketfft at a pinned commit**

```powershell
New-Item -ItemType Directory -Force third_party/pocketfft | Out-Null
$rev = "c90e55b3d529f8efa40ed01a20de22405f45fc65"
curl.exe -sSfL "https://raw.githubusercontent.com/mreineck/pocketfft/$rev/pocketfft_hdronly.h" -o third_party/pocketfft/pocketfft_hdronly.h
curl.exe -sSfL "https://raw.githubusercontent.com/mreineck/pocketfft/$rev/LICENSE.md" -o third_party/pocketfft/LICENSE.md
Set-Content -Encoding ascii third_party/pocketfft/VERSION "https://github.com/mreineck/pocketfft branch cpp, commit $rev"
```

- [ ] **Step 2: Write the failing test**

`engine/tests/spectrum_test.cpp`:
```cpp
#include "kz4ap/spectrum.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace kz4ap;

namespace {

std::vector<Sample> tone(double freq_hz, double amplitude, int rate, std::size_t n) {
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        x[i] = Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
    return x;
}

constexpr int kRate = 48000;
constexpr int kN = 1024;

}  // namespace

TEST(SpectrumAnalyzer, TonePeaksAtItsBinWithItsPower) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const double f = 100.0 * kRate / kN;
    const auto frames = sa.push(tone(f, 0.5, kRate, kN));
    ASSERT_EQ(frames.size(), 1u);
    const auto& p = frames[0].power_db;
    const auto peak = static_cast<int>(std::max_element(p.begin(), p.end()) - p.begin());
    EXPECT_EQ(peak, kN / 2 + 100);
    EXPECT_NEAR(p[peak], 20 * std::log10(0.5), 0.05);
    EXPECT_NEAR(sa.bin_to_hz(peak), f, 1e-9);
}

TEST(SpectrumAnalyzer, NegativeFrequencyIsBelowCenter) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const auto frames = sa.push(tone(-37.0 * kRate / kN, 0.1, kRate, kN));
    const auto& p = frames.at(0).power_db;
    EXPECT_EQ(std::max_element(p.begin(), p.end()) - p.begin(), kN / 2 - 37);
}

TEST(SpectrumAnalyzer, FrameCountAndTimes) {
    SpectrumAnalyzer sa(kRate, kN, kN / 2);
    const auto frames = sa.push(tone(1000, 0.1, kRate, 3 * kN));
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_DOUBLE_EQ(frames[0].time_s, double(kN) / kRate);
    EXPECT_DOUBLE_EQ(frames[1].time_s, double(kN + kN / 2) / kRate);
}

TEST(SpectrumAnalyzer, ChunkingDoesNotChangeFrames) {
    const auto x = tone(3000, 0.2, kRate, 5 * kN);
    SpectrumAnalyzer whole(kRate, kN, kN / 2);
    const auto expected = whole.push(x);

    SpectrumAnalyzer pieces(kRate, kN, kN / 2);
    std::vector<SpectrumFrame> got;
    for (std::size_t i = 0; i < x.size(); i += 100) {
        const auto n = std::min<std::size_t>(100, x.size() - i);
        for (auto& f : pieces.push(std::span(x).subspan(i, n))) got.push_back(std::move(f));
    }
    ASSERT_EQ(got.size(), expected.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        EXPECT_EQ(got[i].time_s, expected[i].time_s);
        EXPECT_EQ(got[i].power_db, expected[i].power_db);
    }
}
```

`engine/include/kz4ap/spectrum.hpp`:
```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace kz4ap {

struct SpectrumFrame {
    double time_s = 0;            // time just after the frame's last input sample
    std::vector<float> power_db;  // fft_size bins, lowest frequency first:
                                  // bin i is at (i - fft_size/2) * sample_rate / fft_size Hz
};

// Hann-windowed power spectrum over a sliding window. A tone of amplitude A
// reads 20*log10(A) dB in its bin.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(int sample_rate, int fft_size, int hop);

    // Consumes samples and returns every frame they complete.
    std::vector<SpectrumFrame> push(std::span<const Sample> in);

    double bin_to_hz(int index) const;

private:
    int sample_rate_;
    int fft_size_;
    int hop_;
    std::vector<float> window_;
    float power_scale_ = 1;
    std::vector<Sample> buffer_;
    std::vector<Sample> fft_in_;
    std::vector<Sample> fft_out_;
    std::uint64_t dropped_ = 0;  // samples already removed from the front of buffer_
};

}  // namespace kz4ap
```

`engine/src/spectrum.cpp` (stub):
```cpp
#include "kz4ap/spectrum.hpp"

namespace kz4ap {

SpectrumAnalyzer::SpectrumAnalyzer(int sample_rate, int fft_size, int hop)
    : sample_rate_(sample_rate), fft_size_(fft_size), hop_(hop) {}
std::vector<SpectrumFrame> SpectrumAnalyzer::push(std::span<const Sample>) { return {}; }
double SpectrumAnalyzer::bin_to_hz(int) const { return 0; }

}  // namespace kz4ap
```

`engine/CMakeLists.txt` — replace the whole file with:
```cmake
add_library(kz4ap_engine
  src/morse.cpp
  src/wav_reader.cpp
  src/spectrum.cpp
)
add_library(kz4ap::engine ALIAS kz4ap_engine)
target_include_directories(kz4ap_engine PUBLIC include PRIVATE src)
target_include_directories(kz4ap_engine SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/pocketfft)
target_compile_definitions(kz4ap_engine PRIVATE POCKETFFT_NO_MULTITHREADING)
target_compile_features(kz4ap_engine PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(kz4ap_engine PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap_engine_tests
  tests/morse_test.cpp
  tests/wav_reader_test.cpp
  tests/spectrum_test.cpp
)
target_link_libraries(kz4ap_engine_tests PRIVATE kz4ap::engine GTest::gtest_main)
gtest_discover_tests(kz4ap_engine_tests)
```

- [ ] **Step 3: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R SpectrumAnalyzer
```
Expected: all 4 tests FAIL.

- [ ] **Step 4: Implement**

`engine/src/fft.hpp`:
```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <cstddef>

#include <pocketfft_hdronly.h>

namespace kz4ap::detail {

// Unnormalized forward DFT: out[k] = sum_n in[n] * exp(-2*pi*i*k*n/N).
inline void fft_forward(const Sample* in, Sample* out, std::size_t n) {
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(Sample))};
    pocketfft::c2c<float>({n}, stride, stride, {0}, pocketfft::FORWARD, in, out, 1.0f);
}

// Unnormalized inverse DFT: out[n] = sum_k in[k] * exp(+2*pi*i*k*n/N).
inline void fft_inverse(const Sample* in, Sample* out, std::size_t n) {
    const pocketfft::stride_t stride{static_cast<std::ptrdiff_t>(sizeof(Sample))};
    pocketfft::c2c<float>({n}, stride, stride, {0}, pocketfft::BACKWARD, in, out, 1.0f);
}

}  // namespace kz4ap::detail
```

`engine/src/spectrum.cpp`:
```cpp
#include "kz4ap/spectrum.hpp"

#include "fft.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {

SpectrumAnalyzer::SpectrumAnalyzer(int sample_rate, int fft_size, int hop)
    : sample_rate_(sample_rate), fft_size_(fft_size), hop_(hop),
      window_(static_cast<std::size_t>(fft_size)),
      fft_in_(static_cast<std::size_t>(fft_size)),
      fft_out_(static_cast<std::size_t>(fft_size)) {
    if (sample_rate <= 0 || fft_size <= 0 || hop <= 0 || hop > fft_size)
        throw std::invalid_argument("invalid spectrum analyzer parameters");
    double sum = 0;
    for (int n = 0; n < fft_size; ++n) {
        window_[n] = static_cast<float>(0.5 - 0.5 * std::cos(2 * std::numbers::pi * n / fft_size));
        sum += window_[n];
    }
    power_scale_ = static_cast<float>(1.0 / (sum * sum));
}

std::vector<SpectrumFrame> SpectrumAnalyzer::push(std::span<const Sample> in) {
    buffer_.insert(buffer_.end(), in.begin(), in.end());
    std::vector<SpectrumFrame> frames;
    const auto n = static_cast<std::size_t>(fft_size_);
    const std::size_t half = n / 2;
    std::size_t start = 0;
    while (buffer_.size() - start >= n) {
        for (std::size_t i = 0; i < n; ++i) fft_in_[i] = buffer_[start + i] * window_[i];
        detail::fft_forward(fft_in_.data(), fft_out_.data(), n);
        SpectrumFrame frame;
        frame.time_s = static_cast<double>(dropped_ + start + n) / sample_rate_;
        frame.power_db.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const float power = std::norm(fft_out_[(i + half) % n]) * power_scale_;
            frame.power_db[i] = 10.0f * std::log10(power + 1e-20f);
        }
        frames.push_back(std::move(frame));
        start += static_cast<std::size_t>(hop_);
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(start));
    dropped_ += start;
    return frames;
}

double SpectrumAnalyzer::bin_to_hz(int index) const {
    return static_cast<double>(index - fft_size_ / 2) * sample_rate_ / fft_size_;
}

}  // namespace kz4ap
```

- [ ] **Step 5: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass.

- [ ] **Step 6: Commit on the `milestone-1` branch**

```powershell
git add third_party engine
```
Commit message: `Add spectrum analyzer and vendor pocketfft`

---

### Task 5: Channelizer

A fast-convolution channelizer: one shared FFT per block, then for each channel a small inverse FFT over the bins around its center. This gives each tracked signal its own narrow, decimated baseband stream.

Parameters used everywhere in this milestone: 192 kHz input, FFT size N = 8192 (23.4 Hz bins), hop N/2 = 4096, 64 bins per channel, so decimation 128 and a channel output rate of 1500 Hz. Each block yields 32 output samples per channel.

**Files:**
- Create: `engine/include/kz4ap/channelizer.hpp`, `engine/src/channelizer.cpp`
- Modify: `engine/CMakeLists.txt` (add `src/channelizer.cpp`, `tests/channelizer_test.cpp`)
- Test: `engine/tests/channelizer_test.cpp`

**Interfaces:**
- Consumes: `kz4ap::Sample`, `detail::fft_forward`, `detail::fft_inverse`.
- Produces:
  ```cpp
  struct ChannelizerConfig { int sample_rate = 192000; int fft_size = 8192; int channel_bins = 64; double cutoff_hz = 150.0; };
  class Channelizer {
      using Sink = std::function<void(std::uint32_t channel_id, std::uint64_t first_index, std::span<const Sample> samples)>;
      explicit Channelizer(const ChannelizerConfig&);
      double output_rate() const;
      int hz_to_bin(double hz) const;   // nearest bin, clamped to [-fft_size/2, fft_size/2)
      double bin_to_hz(int bin) const;
      void add_channel(std::uint32_t id, int center_bin);
      void remove_channel(std::uint32_t id);
      std::size_t channel_count() const;
      void push(std::span<const Sample> in, const Sink& sink);
  };
  ```
  `first_index` counts output samples from the start of the stream, so `first_index / output_rate()` is the time in seconds. Sink calls within a block are in ascending channel ID order.

- [ ] **Step 1: Write the failing test**

`engine/tests/channelizer_test.cpp`:
```cpp
#include "kz4ap/channelizer.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <numbers>
#include <vector>

using namespace kz4ap;

namespace {

constexpr int kRate = 192000;
constexpr int kN = 8192;
constexpr double kBinHz = double(kRate) / kN;
constexpr std::size_t kWarmup = 96;  // output samples to skip while the filter fills

std::vector<Sample> tone(double freq_hz, double amplitude, std::size_t n) {
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / kRate;
        x[i] = Sample(static_cast<float>(amplitude * std::cos(ph)), static_cast<float>(amplitude * std::sin(ph)));
    }
    return x;
}

std::map<std::uint32_t, std::vector<Sample>> run(Channelizer& ch, const std::vector<Sample>& x,
                                                 std::vector<std::uint64_t>* first_indices = nullptr) {
    std::map<std::uint32_t, std::vector<Sample>> out;
    ch.push(x, [&](std::uint32_t id, std::uint64_t first, std::span<const Sample> s) {
        out[id].insert(out[id].end(), s.begin(), s.end());
        if (first_indices && id == 1) first_indices->push_back(first);
    });
    return out;
}

}  // namespace

TEST(Channelizer, OutputRateAndBinConversions) {
    Channelizer ch({});
    EXPECT_DOUBLE_EQ(ch.output_rate(), 1500.0);
    EXPECT_EQ(ch.hz_to_bin(1000.0), 43);
    EXPECT_EQ(ch.hz_to_bin(-96000.0), -4096);
    EXPECT_EQ(ch.hz_to_bin(96000.0), 4095);
    EXPECT_DOUBLE_EQ(ch.bin_to_hz(43), 43 * kBinHz);
}

TEST(Channelizer, ToneOnCenterComesOutSteadyAtItsAmplitude) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    std::vector<std::uint64_t> firsts;
    const auto out = run(ch, tone(201 * kBinHz, 0.3, kRate), &firsts);
    const auto& y = out.at(1);
    ASSERT_EQ(y.size(), 1500u - 1500u % 32u);
    for (std::size_t i = kWarmup; i < y.size(); ++i) {
        EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.005) << i;
        if (i + 1 < y.size()) EXPECT_NEAR(std::arg(y[i + 1] / y[i]), 0.0, 1e-3) << i;
    }
    ASSERT_GE(firsts.size(), 2u);
    EXPECT_EQ(firsts[0], 0u);
    EXPECT_EQ(firsts[1], 32u);
}

TEST(Channelizer, OffsetToneRotatesContinuouslyAcrossBlocks) {
    Channelizer ch({});
    ch.add_channel(1, 201);  // odd bin: exercises the per-block phase correction
    const auto y = run(ch, tone(201 * kBinHz + 5.0, 0.3, kRate)).at(1);
    const double step = 2 * std::numbers::pi * 5.0 / 1500.0;
    for (std::size_t i = kWarmup; i + 1 < y.size(); ++i) {
        EXPECT_NEAR(std::arg(y[i + 1] / y[i]), step, 1e-3) << i;
    }
}

TEST(Channelizer, PassbandIsFlatWithinHalfABin) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    const auto y = run(ch, tone(201 * kBinHz + 11.0, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.012) << i;
}

TEST(Channelizer, RejectsToneOutsidePassband) {
    Channelizer ch({});
    ch.add_channel(1, 201);
    const auto y = run(ch, tone(201 * kBinHz + 400.0, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_LT(std::abs(y[i]), 0.3 * 1e-3) << i;
}

TEST(Channelizer, SeparatesTwoChannels) {
    Channelizer ch({});
    ch.add_channel(1, 100);
    ch.add_channel(2, -300);
    auto x = tone(100 * kBinHz, 0.2, kRate);
    const auto x2 = tone(-300 * kBinHz, 0.05, kRate);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += x2[i];
    const auto out = run(ch, x);
    for (std::size_t i = kWarmup; i < out.at(1).size(); ++i) {
        EXPECT_NEAR(std::abs(out.at(1)[i]), 0.2, 0.2 * 0.005);
        EXPECT_NEAR(std::abs(out.at(2)[i]), 0.05, 0.05 * 0.005);
    }
}

TEST(Channelizer, ChannelNearBandEdgeWrapsSafely) {
    Channelizer ch({});
    const int bin = kN / 2 - 5;
    ch.add_channel(1, bin);
    const auto y = run(ch, tone(bin * kBinHz, 0.3, kRate)).at(1);
    for (std::size_t i = kWarmup; i < y.size(); ++i) EXPECT_NEAR(std::abs(y[i]), 0.3, 0.3 * 0.005) << i;
}

TEST(Channelizer, RemovedChannelStopsReceiving) {
    Channelizer ch({});
    ch.add_channel(1, 10);
    ch.add_channel(2, 20);
    ch.remove_channel(1);
    EXPECT_EQ(ch.channel_count(), 1u);
    const auto out = run(ch, tone(10 * kBinHz, 0.1, kRate / 10));
    EXPECT_EQ(out.count(1), 0u);
    EXPECT_EQ(out.count(2), 1u);
}
```

`engine/include/kz4ap/channelizer.hpp`:
```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

namespace kz4ap {

struct ChannelizerConfig {
    int sample_rate = 192000;
    int fft_size = 8192;       // input FFT size; the hop is fft_size / 2
    int channel_bins = 64;     // bins kept per channel; output rate = sample_rate * channel_bins / fft_size
    double cutoff_hz = 150.0;  // channel low-pass cutoff (-6 dB point)
};

// Splits a wideband I/Q stream into narrow, decimated baseband streams, one per
// channel, by fast convolution: one shared FFT per block, then a small inverse
// FFT per channel over the bins around its center.
class Channelizer {
public:
    // first_index counts output samples from the start of the stream.
    using Sink = std::function<void(std::uint32_t channel_id, std::uint64_t first_index,
                                    std::span<const Sample> samples)>;

    explicit Channelizer(const ChannelizerConfig& config);

    double output_rate() const;
    int hz_to_bin(double hz) const;  // nearest bin, clamped to [-fft_size/2, fft_size/2)
    double bin_to_hz(int bin) const;

    void add_channel(std::uint32_t id, int center_bin);
    void remove_channel(std::uint32_t id);
    std::size_t channel_count() const { return channels_.size(); }

    // Consumes samples; calls sink once per channel for every block they complete.
    void push(std::span<const Sample> in, const Sink& sink);

private:
    void process_block(const Sink& sink);

    ChannelizerConfig config_;
    int hop_;
    int decimation_;
    std::vector<Sample> filter_;  // channel_bins frequency-response values, indexed by bin offset mod channel_bins
    std::vector<Sample> history_;
    std::vector<Sample> pending_;
    std::vector<Sample> spectrum_;
    std::vector<Sample> channel_in_;
    std::vector<Sample> channel_out_;
    std::uint64_t block_index_ = 0;
    std::map<std::uint32_t, int> channels_;  // id -> center bin; ordered for deterministic sink order
};

}  // namespace kz4ap
```

`engine/src/channelizer.cpp` (stub):
```cpp
#include "kz4ap/channelizer.hpp"

namespace kz4ap {

Channelizer::Channelizer(const ChannelizerConfig& config) : config_(config), hop_(0), decimation_(1) {}
double Channelizer::output_rate() const { return 0; }
int Channelizer::hz_to_bin(double) const { return 0; }
double Channelizer::bin_to_hz(int) const { return 0; }
void Channelizer::add_channel(std::uint32_t, int) {}
void Channelizer::remove_channel(std::uint32_t) {}
void Channelizer::push(std::span<const Sample>, const Sink&) {}
void Channelizer::process_block(const Sink&) {}

}  // namespace kz4ap
```

`engine/CMakeLists.txt`: add `src/channelizer.cpp` to `kz4ap_engine` and `tests/channelizer_test.cpp` to `kz4ap_engine_tests`.

- [ ] **Step 2: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R Channelizer
```
Expected: all 8 tests FAIL.

- [ ] **Step 3: Implement**

`engine/src/channelizer.cpp`:
```cpp
#include "kz4ap/channelizer.hpp"

#include "fft.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace kz4ap {

Channelizer::Channelizer(const ChannelizerConfig& config) : config_(config) {
    const int n = config.fft_size;
    const int m = config.channel_bins;
    if (n <= 0 || n % 2 != 0 || m <= 0 || m % 2 != 0 || n % m != 0 || config.sample_rate <= 0)
        throw std::invalid_argument("invalid channelizer parameters");
    hop_ = n / 2;
    decimation_ = n / m;

    // Windowed-sinc low-pass, as long as overlap-save allows (fft_size - hop + 1 taps).
    const int taps = n - hop_ + 1;
    const double fc = config.cutoff_hz / config.sample_rate;
    const double mid = (taps - 1) / 2.0;
    std::vector<double> h(static_cast<std::size_t>(taps));
    double sum = 0;
    for (int i = 0; i < taps; ++i) {
        const double x = i - mid;
        const double sinc = x == 0 ? 2 * fc : std::sin(2 * std::numbers::pi * fc * x) / (std::numbers::pi * x);
        const double w = 0.42 - 0.5 * std::cos(2 * std::numbers::pi * i / (taps - 1)) +
                         0.08 * std::cos(4 * std::numbers::pi * i / (taps - 1));  // Blackman
        h[i] = sinc * w;
        sum += h[i];
    }
    std::vector<Sample> padded(static_cast<std::size_t>(n));
    for (int i = 0; i < taps; ++i) padded[i] = Sample(static_cast<float>(h[i] / sum), 0.0f);
    std::vector<Sample> response(static_cast<std::size_t>(n));
    detail::fft_forward(padded.data(), response.data(), static_cast<std::size_t>(n));
    filter_.resize(static_cast<std::size_t>(m));
    for (int k = -m / 2; k < m / 2; ++k) filter_[(k + m) % m] = response[(k + n) % n];

    history_.assign(static_cast<std::size_t>(n), Sample{});
    spectrum_.resize(static_cast<std::size_t>(n));
    channel_in_.resize(static_cast<std::size_t>(m));
    channel_out_.resize(static_cast<std::size_t>(m));
    pending_.reserve(static_cast<std::size_t>(hop_));
}

double Channelizer::output_rate() const {
    return static_cast<double>(config_.sample_rate) / decimation_;
}

int Channelizer::hz_to_bin(double hz) const {
    const auto bin = static_cast<int>(std::lround(hz * config_.fft_size / config_.sample_rate));
    return std::clamp(bin, -config_.fft_size / 2, config_.fft_size / 2 - 1);
}

double Channelizer::bin_to_hz(int bin) const {
    return static_cast<double>(bin) * config_.sample_rate / config_.fft_size;
}

void Channelizer::add_channel(std::uint32_t id, int center_bin) { channels_[id] = center_bin; }

void Channelizer::remove_channel(std::uint32_t id) { channels_.erase(id); }

void Channelizer::push(std::span<const Sample> in, const Sink& sink) {
    for (const Sample& s : in) {
        pending_.push_back(s);
        if (static_cast<int>(pending_.size()) == hop_) process_block(sink);
    }
}

void Channelizer::process_block(const Sink& sink) {
    const int n = config_.fft_size;
    const int m = config_.channel_bins;
    std::move(history_.begin() + hop_, history_.end(), history_.begin());
    std::copy(pending_.begin(), pending_.end(), history_.end() - hop_);
    pending_.clear();
    detail::fft_forward(history_.data(), spectrum_.data(), static_cast<std::size_t>(n));

    // Block b holds input samples [b*hop - (n - hop), b*hop + hop). Only the last
    // hop/decimation outputs are free of circular wrap-around; they start at input b*hop.
    const std::int64_t block_start = static_cast<std::int64_t>(block_index_) * hop_ - (n - hop_);
    const int keep = hop_ / decimation_;
    const std::uint64_t first_index = block_index_ * static_cast<std::uint64_t>(keep);

    for (const auto& [id, center] : channels_) {
        for (int k = -m / 2; k < m / 2; ++k) {
            const int src = ((center + k) % n + n) % n;
            channel_in_[(k + m) % m] = spectrum_[src] * filter_[(k + m) % m];
        }
        detail::fft_inverse(channel_in_.data(), channel_out_.data(), static_cast<std::size_t>(m));
        // The inverse FFT mixes relative to this block's start; re-reference the mixer
        // to the start of the stream so phase is continuous from block to block.
        const std::int64_t turns = ((static_cast<std::int64_t>(center) * block_start) % n + n) % n;
        const double angle = -2 * std::numbers::pi * static_cast<double>(turns) / n;
        const Sample rotate = Sample(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))) /
                              static_cast<float>(n);
        for (int i = m - keep; i < m; ++i) channel_out_[i] *= rotate;
        sink(id, first_index, std::span<const Sample>(channel_out_.data() + (m - keep), static_cast<std::size_t>(keep)));
    }
    ++block_index_;
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass. If `OffsetToneRotatesContinuouslyAcrossBlocks` fails only at every 32nd sample, the per-block phase correction is wrong; use superpowers:systematic-debugging rather than loosening the tolerance.

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add engine
```
Commit message: `Add fast-convolution channelizer`

---

### Task 6: Signal detector

Finds CW carriers in spectrum frames and keeps a list of tracks. It averages power per bin over time (CW keys on and off, so single frames are misleading), estimates the noise floor as the median of the averaged spectrum, and reports a track once a peak has stood above the floor long enough. A track dies after a long silence.

**Files:**
- Create: `engine/include/kz4ap/signal_detector.hpp`, `engine/src/signal_detector.cpp`
- Modify: `engine/CMakeLists.txt` (add `src/signal_detector.cpp`, `tests/signal_detector_test.cpp`)
- Test: `engine/tests/signal_detector_test.cpp`

**Interfaces:**
- Consumes: `kz4ap::SpectrumFrame` (Task 4).
- Produces:
  ```cpp
  struct Track { std::uint32_t id = 0; double freq_hz = 0; float snr_db = 0; double start_time_s = 0; double last_active_s = 0; };
  struct DetectorConfig { int sample_rate = 192000; int fft_size = 8192; int hop = 4096; float threshold_db = 6.0f;
      float hysteresis_db = 3.0f; double average_s = 1.0; double birth_s = 0.5; double death_s = 10.0;
      std::size_t max_tracks = 200; int min_separation_bins = 3; };
  struct DetectorUpdate { std::vector<Track> born; std::vector<std::uint32_t> died; };
  class SignalDetector { explicit SignalDetector(const DetectorConfig&); DetectorUpdate process(const SpectrumFrame&); std::vector<Track> tracks() const; };
  ```
  Track IDs start at 1 and are never reused. `process` throws `std::invalid_argument` if the frame size differs from `fft_size`.

- [ ] **Step 1: Write the failing test**

`engine/tests/signal_detector_test.cpp`:
```cpp
#include "kz4ap/signal_detector.hpp"

#include <gtest/gtest.h>

#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace kz4ap;

namespace {

constexpr int kN = 256;
constexpr int kRate = 25600;  // 100 Hz bins
constexpr int kHop = 128;     // 5 ms frames

DetectorConfig config() {
    DetectorConfig c;
    c.sample_rate = kRate;
    c.fft_size = kN;
    c.hop = kHop;
    c.average_s = 0.05;
    c.birth_s = 0.5;
    c.death_s = 2.0;
    return c;
}

double frame_time(int i) { return (i + 1) * double(kHop) / kRate; }

SpectrumFrame frame(double t, const std::vector<std::pair<int, float>>& peaks = {}) {
    SpectrumFrame f;
    f.time_s = t;
    f.power_db.assign(kN, -100.0f);
    for (auto [bin, db] : peaks) f.power_db[bin] = db;
    return f;
}

}  // namespace

TEST(SignalDetector, TrackIsBornOnlyAfterSignalPersists) {
    SignalDetector d(config());
    std::vector<Track> born;
    double born_at = -1;
    for (int i = 0; i < 200; ++i) {
        const auto u = d.process(frame(frame_time(i), {{200, -70.0f}}));
        if (!u.born.empty() && born_at < 0) born_at = frame_time(i);
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(born.size(), 1u);
    EXPECT_GE(born_at, 0.55);  // 0.05 s of averaging warm-up, then 0.5 s of persistence
    EXPECT_LT(born_at, 0.58);
    EXPECT_EQ(born[0].id, 1u);
    EXPECT_NEAR(born[0].freq_hz, (200 - kN / 2) * 100.0, 1e-6);
    EXPECT_NEAR(born[0].snr_db, 30.0f, 0.5f);
    EXPECT_EQ(d.tracks().size(), 1u);
}

TEST(SignalDetector, NoiseAloneMakesNoTracks) {
    SignalDetector d(config());
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> jitter(-2.0f, 2.0f);
    for (int i = 0; i < 400; ++i) {
        auto f = frame(frame_time(i));
        for (auto& p : f.power_db) p += jitter(rng);
        EXPECT_TRUE(d.process(f).born.empty()) << "frame " << i;
    }
}

TEST(SignalDetector, TrackDiesAfterSilence) {
    SignalDetector d(config());
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{200, -70.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    double died_at = -1;
    for (; i < 1000 && died_at < 0; ++i) {
        if (!d.process(frame(frame_time(i))).died.empty()) died_at = frame_time(i);
    }
    EXPECT_GT(died_at, 1.0 + 2.0);
    EXPECT_LT(died_at, 1.0 + 2.0 + 0.5);
    EXPECT_TRUE(d.tracks().empty());
}

TEST(SignalDetector, WideSignalMakesOneTrack) {
    SignalDetector d(config());
    std::size_t born = 0;
    for (int i = 0; i < 200; ++i)
        born += d.process(frame(frame_time(i), {{199, -72.0f}, {200, -70.0f}, {201, -72.0f}})).born.size();
    EXPECT_EQ(born, 1u);
}

TEST(SignalDetector, TwoSignalsMakeTwoTracks) {
    SignalDetector d(config());
    std::size_t born = 0;
    for (int i = 0; i < 200; ++i)
        born += d.process(frame(frame_time(i), {{100, -70.0f}, {150, -75.0f}})).born.size();
    EXPECT_EQ(born, 2u);
}

TEST(SignalDetector, CapKeepsStrongestSignals) {
    auto c = config();
    c.max_tracks = 2;
    SignalDetector d(c);
    std::vector<Track> born;
    for (int i = 0; i < 400; ++i) {
        const auto u = d.process(frame(frame_time(i), {{50, -70.0f}, {100, -80.0f}, {150, -90.0f}}));
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(born.size(), 2u);
    EXPECT_NEAR(born[0].freq_hz, (50 - kN / 2) * 100.0, 1e-6);
    EXPECT_NEAR(born[1].freq_hz, (100 - kN / 2) * 100.0, 1e-6);
}

TEST(SignalDetector, StrongerNewSignalReplacesWeakestAtCap) {
    auto c = config();
    c.max_tracks = 1;
    SignalDetector d(c);
    int i = 0;
    for (; i < 200; ++i) d.process(frame(frame_time(i), {{50, -90.0f}}));
    ASSERT_EQ(d.tracks().size(), 1u);
    const auto weak_id = d.tracks()[0].id;
    std::vector<std::uint32_t> died;
    std::vector<Track> born;
    for (; i < 400; ++i) {
        const auto u = d.process(frame(frame_time(i), {{50, -90.0f}, {150, -70.0f}}));
        died.insert(died.end(), u.died.begin(), u.died.end());
        born.insert(born.end(), u.born.begin(), u.born.end());
    }
    ASSERT_EQ(died.size(), 1u);
    EXPECT_EQ(died[0], weak_id);
    ASSERT_EQ(born.size(), 1u);
    EXPECT_NEAR(born[0].freq_hz, (150 - kN / 2) * 100.0, 1e-6);
}

TEST(SignalDetector, RejectsFrameOfWrongSize) {
    SignalDetector d(config());
    SpectrumFrame f;
    f.power_db.assign(100, -100.0f);
    EXPECT_THROW(d.process(f), std::invalid_argument);
}
```

`engine/include/kz4ap/signal_detector.hpp`:
```cpp
#pragma once

#include "kz4ap/spectrum.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kz4ap {

struct Track {
    std::uint32_t id = 0;       // starts at 1, never reused
    double freq_hz = 0;         // offset from the center of the SDR span
    float snr_db = 0;           // averaged power above the noise floor, per FFT bin
    double start_time_s = 0;    // when the signal was first seen
    double last_active_s = 0;   // last time it stood above the floor
};

struct DetectorConfig {
    int sample_rate = 192000;
    int fft_size = 8192;
    int hop = 4096;                  // samples between frames
    float threshold_db = 6.0f;       // a new track needs this much above the noise floor
    float hysteresis_db = 3.0f;      // an existing track stays active down to threshold - hysteresis
    double average_s = 1.0;          // time constant of the per-bin power average; also the warm-up
    double birth_s = 0.5;            // how long a peak must persist to become a track
    double death_s = 10.0;           // how long a track may stay inactive before it dies
    std::size_t max_tracks = 200;
    int min_separation_bins = 3;     // peaks closer than this to a track belong to it
};

struct DetectorUpdate {
    std::vector<Track> born;
    std::vector<std::uint32_t> died;
};

// Finds CW carriers in spectrum frames and keeps a list of tracks.
class SignalDetector {
public:
    explicit SignalDetector(const DetectorConfig& config);

    // Throws std::invalid_argument if the frame does not have fft_size bins.
    DetectorUpdate process(const SpectrumFrame& frame);

    std::vector<Track> tracks() const;

private:
    struct Active {
        Track track;
        int bin;
    };
    struct Candidate {
        int bin;
        double first_seen_s;
        float snr_db;
        bool seen;
    };

    double refined_freq(const std::vector<float>& avg_db, int bin) const;

    DetectorConfig config_;
    double alpha_;
    std::uint64_t frames_seen_ = 0;
    double first_frame_s_ = 0;
    std::vector<double> average_;  // linear power per bin
    std::vector<Active> active_;
    std::vector<Candidate> candidates_;
    std::uint32_t next_id_ = 1;
};

}  // namespace kz4ap
```

`engine/src/signal_detector.cpp` (stub):
```cpp
#include "kz4ap/signal_detector.hpp"

namespace kz4ap {

SignalDetector::SignalDetector(const DetectorConfig& config) : config_(config), alpha_(0) {}
DetectorUpdate SignalDetector::process(const SpectrumFrame&) { return {}; }
std::vector<Track> SignalDetector::tracks() const { return {}; }
double SignalDetector::refined_freq(const std::vector<float>&, int) const { return 0; }

}  // namespace kz4ap
```

`engine/CMakeLists.txt`: add `src/signal_detector.cpp` to `kz4ap_engine` and `tests/signal_detector_test.cpp` to `kz4ap_engine_tests`.

- [ ] **Step 2: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R SignalDetector
```
Expected: all tests except `NoiseAloneMakesNoTracks` FAIL (the stub never creates tracks).

- [ ] **Step 3: Implement**

`engine/src/signal_detector.cpp`:
```cpp
#include "kz4ap/signal_detector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kz4ap {

SignalDetector::SignalDetector(const DetectorConfig& config)
    : config_(config),
      alpha_(1.0 - std::exp(-(static_cast<double>(config.hop) / config.sample_rate) / config.average_s)),
      average_(static_cast<std::size_t>(config.fft_size), 0.0) {}

DetectorUpdate SignalDetector::process(const SpectrumFrame& frame) {
    const auto n = frame.power_db.size();
    if (n != static_cast<std::size_t>(config_.fft_size))
        throw std::invalid_argument("spectrum frame size does not match detector fft_size");
    const double now = frame.time_s;
    if (frames_seen_ == 0) first_frame_s_ = now;
    ++frames_seen_;

    // Running mean at first, then an exponential average.
    const double a = std::max(alpha_, 1.0 / static_cast<double>(frames_seen_));
    std::vector<float> avg_db(n);
    for (std::size_t i = 0; i < n; ++i) {
        average_[i] += a * (std::pow(10.0, frame.power_db[i] / 10.0) - average_[i]);
        avg_db[i] = static_cast<float>(10.0 * std::log10(average_[i] + 1e-30));
    }
    std::vector<float> sorted = avg_db;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n / 2), sorted.end());
    const float floor_db = sorted[n / 2];

    DetectorUpdate update;
    const int last = static_cast<int>(n) - 1;

    // Refresh existing tracks; expire the ones that have been quiet too long.
    for (auto it = active_.begin(); it != active_.end();) {
        float level = avg_db[it->bin];
        if (it->bin > 0) level = std::max(level, avg_db[it->bin - 1]);
        if (it->bin < last) level = std::max(level, avg_db[it->bin + 1]);
        it->track.snr_db = level - floor_db;
        if (it->track.snr_db >= config_.threshold_db - config_.hysteresis_db) it->track.last_active_s = now;
        if (now - it->track.last_active_s >= config_.death_s) {
            update.died.push_back(it->track.id);
            it = active_.erase(it);
        } else {
            ++it;
        }
    }

    // Until the averages settle, a single noisy frame can look like a signal.
    if (now - first_frame_s_ < config_.average_s) return update;

    // Local peaks above threshold that do not belong to an existing track.
    for (auto& c : candidates_) c.seen = false;
    constexpr int kRadius = 2;
    for (int i = kRadius; i <= last - kRadius; ++i) {
        const float snr = avg_db[i] - floor_db;
        if (snr < config_.threshold_db) continue;
        bool peak = true;
        for (int d = -kRadius; d <= kRadius && peak; ++d) {
            if (d < 0 && avg_db[i + d] >= avg_db[i]) peak = false;  // ties go to the lower bin
            if (d > 0 && avg_db[i + d] > avg_db[i]) peak = false;
        }
        if (!peak) continue;
        const bool near_track = std::any_of(active_.begin(), active_.end(), [&](const Active& t) {
            return std::abs(t.bin - i) < config_.min_separation_bins;
        });
        if (near_track) continue;
        auto c = std::find_if(candidates_.begin(), candidates_.end(),
                              [&](const Candidate& k) { return std::abs(k.bin - i) <= 1; });
        if (c != candidates_.end()) {
            c->bin = i;
            c->snr_db = snr;
            c->seen = true;
        } else {
            candidates_.push_back({i, now, snr, true});
        }
    }
    std::erase_if(candidates_, [](const Candidate& c) { return !c.seen; });

    // Promote candidates that have persisted long enough, strongest first.
    std::vector<Candidate> ready;
    std::erase_if(candidates_, [&](const Candidate& c) {
        if (now - c.first_seen_s < config_.birth_s) return false;
        ready.push_back(c);
        return true;
    });
    std::stable_sort(ready.begin(), ready.end(),
                     [](const Candidate& x, const Candidate& y) { return x.snr_db > y.snr_db; });
    for (const auto& c : ready) {
        if (active_.size() >= config_.max_tracks) {
            auto weakest = std::min_element(active_.begin(), active_.end(), [](const Active& x, const Active& y) {
                return x.track.snr_db < y.track.snr_db;
            });
            if (weakest == active_.end() || weakest->track.snr_db >= c.snr_db) continue;
            update.died.push_back(weakest->track.id);
            active_.erase(weakest);
        }
        Track t;
        t.id = next_id_++;
        t.freq_hz = refined_freq(avg_db, c.bin);
        t.snr_db = c.snr_db;
        t.start_time_s = c.first_seen_s;
        t.last_active_s = now;
        active_.push_back({t, c.bin});
        update.born.push_back(t);
    }
    return update;
}

std::vector<Track> SignalDetector::tracks() const {
    std::vector<Track> out;
    out.reserve(active_.size());
    for (const auto& a : active_) out.push_back(a.track);
    return out;
}

double SignalDetector::refined_freq(const std::vector<float>& avg_db, int bin) const {
    // Parabolic interpolation over the peak and its neighbors.
    double delta = 0;
    if (bin > 0 && bin + 1 < static_cast<int>(avg_db.size())) {
        const double a = avg_db[bin - 1], b = avg_db[bin], c = avg_db[bin + 1];
        const double denom = a - 2 * b + c;
        if (denom != 0) delta = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    }
    return (bin + delta - config_.fft_size / 2) * static_cast<double>(config_.sample_rate) / config_.fft_size;
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass.

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add engine
```
Commit message: `Add signal detector`

---

### Task 7: Decoder interface and Classical decoder

The baseline decoder. From a channel's baseband it takes the magnitude, smooths it, and keys on and off against adaptive mark and space levels. It measures mark and space durations, estimates speed from the dit/dah split of recent marks, and turns elements into characters. Each character gets a probability from how cleanly its elements fell on either side of the dit/dah boundary.

**Files:**
- Create: `engine/include/kz4ap/decoder.hpp`, `engine/include/kz4ap/classical_decoder.hpp`, `engine/src/classical_decoder.cpp`
- Create: `engine/tests/test_signals.hpp` (shared test helper; Task 8 uses it too)
- Modify: `engine/CMakeLists.txt` (add `src/classical_decoder.cpp`, `tests/classical_decoder_test.cpp`)
- Test: `engine/tests/classical_decoder_test.cpp`

**Interfaces:**
- Consumes: `kz4ap::Sample`, `kz4ap::morse::decode`, `kz4ap::morse::encode` (test helper).
- Produces:
  ```cpp
  struct DecodedChar { char ch; float probability; double start_s; double end_s; };   // ' ' = word space, '*' = unknown pattern
  struct DecodeUpdate { std::vector<DecodedChar> chars; float wpm = 0; float confidence = 0; };
  class Decoder { virtual DecodeUpdate process(std::span<const Sample> samples, double t0_s) = 0;
                  virtual DecodeUpdate flush() = 0; virtual void reset() = 0; };
  struct ClassicalDecoderConfig { double initial_wpm = 25; double min_wpm = 5; double max_wpm = 60; double attack_s = 0.004;
      double decay_s = 3.0; float squelch_ratio = 3.0f; double smoothing_dits = 0.25; double glitch_dits = 0.3; };
  class ClassicalDecoder final : public Decoder { ClassicalDecoder(double sample_rate, ClassicalDecoderConfig = {}); double wpm() const; };
  ```
  Test helper `kz4ap::test::keying(text, wpm, start_s)` and `kz4ap::test::keyed_signal(text, wpm, rate, duration_s, freq_hz, amplitude, noise_sigma, seed, start_s)`.

- [ ] **Step 1: Write the test helper and the failing test**

`engine/tests/test_signals.hpp`:
```cpp
#pragma once

#include "kz4ap/morse.hpp"
#include "kz4ap/types.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kz4ap::test {

// Key-down intervals (start_s, end_s) for text at wpm, PARIS timing, beginning at start_s.
inline std::vector<std::pair<double, double>> keying(const std::string& text, double wpm, double start_s) {
    const double dit = 1.2 / wpm;
    std::vector<std::string> words;
    std::string word;
    for (char c : text) {
        if (c == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(word);

    std::vector<std::pair<double, double>> out;
    double t = start_s;
    for (std::size_t wi = 0; wi < words.size(); ++wi) {
        std::vector<std::string_view> codes;
        for (char c : words[wi]) {
            const auto p = morse::encode(c);
            if (!p.empty()) codes.push_back(p);
        }
        for (std::size_t ci = 0; ci < codes.size(); ++ci) {
            for (std::size_t ei = 0; ei < codes[ci].size(); ++ei) {
                const double len = codes[ci][ei] == '.' ? dit : 3 * dit;
                out.emplace_back(t, t + len);
                t += len;
                if (ei + 1 < codes[ci].size()) t += dit;
            }
            if (ci + 1 < codes.size()) t += 3 * dit;
        }
        if (wi + 1 < words.size()) t += 7 * dit;
    }
    return out;
}

// A keyed carrier at freq_hz (0 = baseband) with 5 ms raised-cosine edges, plus
// complex white noise of total power noise_sigma^2.
inline std::vector<Sample> keyed_signal(const std::string& text, double wpm, double rate, double duration_s,
                                        double freq_hz = 0, double amplitude = 1.0, double noise_sigma = 0.0,
                                        unsigned seed = 1, double start_s = 0.5) {
    const auto n = static_cast<std::size_t>(duration_s * rate);
    std::vector<double> env(n, 0.0);
    constexpr double kRise = 0.005;
    for (auto [on, off] : keying(text, wpm, start_s)) {
        const auto i0 = static_cast<std::size_t>(on * rate);
        const auto i1 = std::min(n, static_cast<std::size_t>(off * rate));
        for (std::size_t i = i0; i < i1; ++i) {
            const double t = i / rate;
            double e = 1.0;
            if (t - on < kRise) e = 0.5 - 0.5 * std::cos(std::numbers::pi * (t - on) / kRise);
            if (off - t < kRise) e = std::min(e, 0.5 - 0.5 * std::cos(std::numbers::pi * (off - t) / kRise));
            env[i] = e;
        }
    }
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, std::max(noise_sigma, 1e-12) / std::sqrt(2.0));
    std::vector<Sample> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double ph = 2 * std::numbers::pi * freq_hz * static_cast<double>(i) / rate;
        const double a = amplitude * env[i];
        Sample s(static_cast<float>(a * std::cos(ph)), static_cast<float>(a * std::sin(ph)));
        if (noise_sigma > 0) s += Sample(static_cast<float>(gauss(rng)), static_cast<float>(gauss(rng)));
        x[i] = s;
    }
    return x;
}

// Duration that fits text at wpm starting at 0.5 s, plus trailing silence.
inline double duration_for(const std::string& text, double wpm, double tail_s = 1.5) {
    return keying(text, wpm, 0.5).back().second + tail_s;
}

}  // namespace kz4ap::test
```

`engine/tests/classical_decoder_test.cpp`:
```cpp
#include "kz4ap/classical_decoder.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace kz4ap;
using kz4ap::test::duration_for;
using kz4ap::test::keyed_signal;

namespace {

constexpr double kRate = 1500.0;

std::vector<DecodedChar> decode_all(ClassicalDecoder& d, const std::vector<Sample>& x, std::size_t chunk = 256) {
    std::vector<DecodedChar> chars;
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        const auto n = std::min(chunk, x.size() - i);
        auto u = d.process(std::span<const Sample>(x).subspan(i, n), static_cast<double>(i) / kRate);
        chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    }
    auto u = d.flush();
    chars.insert(chars.end(), u.chars.begin(), u.chars.end());
    return chars;
}

// Uppercase text with runs of spaces collapsed and ends trimmed.
std::string text(const std::vector<DecodedChar>& chars) {
    std::string out;
    for (const auto& c : chars) {
        if (c.ch == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += c.ch;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

TEST(ClassicalDecoder, DecodesCleanSignal) {
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    EXPECT_EQ(text(decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))), msg);
    EXPECT_NEAR(d.wpm(), 25.0, 2.5);
}

TEST(ClassicalDecoder, CleanSignalHasHighProbabilities) {
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    for (const auto& c : decode_all(d, keyed_signal(msg, 25, kRate, duration_for(msg, 25)))) {
        if (c.ch != ' ') EXPECT_GT(c.probability, 0.9f) << c.ch;
    }
}

TEST(ClassicalDecoder, ReportsCharacterTimes) {
    ClassicalDecoder d(kRate);
    const auto chars = decode_all(d, keyed_signal("E", 25, kRate, 2.0));
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].ch, 'E');
    EXPECT_NEAR(chars[0].start_s, 0.5, 0.02);
    EXPECT_NEAR(chars[0].end_s, 0.5 + 0.048, 0.02);
}

TEST(ClassicalDecoder, DecodesWithModerateNoise) {
    // 15 dB SNR in 500 Hz: white noise over 1500 Hz with total power 3 * 10^(-1.5).
    ClassicalDecoder d(kRate);
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 0.31, 7);
    EXPECT_EQ(text(decode_all(d, x)), msg);
}

TEST(ClassicalDecoder, NoiseAloneDecodesNothing) {
    ClassicalDecoder d(kRate);
    const auto chars = decode_all(d, keyed_signal("", 25, kRate, 10.0, 0, 0.0, 0.3, 3));
    EXPECT_EQ(text(chars), "");
}

TEST(ClassicalDecoder, AdaptsToSlowSpeed) {
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 12, kRate, duration_for(msg, 12))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 12.0, 1.2);
}

TEST(ClassicalDecoder, AdaptsToFastSpeed) {
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS CQ K1ABC";
    const auto got = text(decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45))));
    EXPECT_TRUE(ends_with(got, "CQ K1ABC")) << got;
    EXPECT_NEAR(d.wpm(), 45.0, 4.5);
}

TEST(ClassicalDecoder, PauseDecodesNothingThenResumes) {
    ClassicalDecoder d(kRate);
    auto x = keyed_signal("CQ K1ABC", 25, kRate, 14.5, 0, 1.0, 0.2, 5, 0.5);
    const auto later = keyed_signal("TU", 25, kRate, 14.5, 0, 1.0, 0.0, 1, 12.0);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += later[i];
    EXPECT_EQ(text(decode_all(d, x)), "CQ K1ABC TU");
}

TEST(ClassicalDecoder, UnknownPatternBecomesAsterisk) {
    ClassicalDecoder d(kRate);
    // Eight dits in a row ("HH" with no character gap) is not a Morse character.
    std::vector<Sample> x(static_cast<std::size_t>(3.0 * kRate));
    const double dit = 1.2 / 25;
    for (int k = 0; k < 8; ++k) {
        const auto i0 = static_cast<std::size_t>((0.5 + 2 * k * dit) * kRate);
        const auto i1 = static_cast<std::size_t>((0.5 + (2 * k + 1) * dit) * kRate);
        for (std::size_t i = i0; i < i1; ++i) x[i] = Sample(1.0f, 0.0f);
    }
    const auto chars = decode_all(d, x);
    ASSERT_FALSE(chars.empty());
    EXPECT_EQ(chars[0].ch, '*');
    EXPECT_EQ(chars[0].probability, 0.0f);
}

TEST(ClassicalDecoder, ChunkSizeDoesNotChangeOutput) {
    const std::string msg = "CQ TEST K1ABC";
    const auto x = keyed_signal(msg, 25, kRate, duration_for(msg, 25), 0, 1.0, 0.31, 7);
    ClassicalDecoder a(kRate), b(kRate);
    const auto ca = decode_all(a, x, 7);
    const auto cb = decode_all(b, x, 1000);
    ASSERT_EQ(ca.size(), cb.size());
    for (std::size_t i = 0; i < ca.size(); ++i) {
        EXPECT_EQ(ca[i].ch, cb[i].ch);
        EXPECT_EQ(ca[i].probability, cb[i].probability);
        EXPECT_EQ(ca[i].start_s, cb[i].start_s);
    }
}

TEST(ClassicalDecoder, ResetRestoresInitialSpeed) {
    ClassicalDecoder d(kRate);
    const std::string msg = "PARIS PARIS";
    decode_all(d, keyed_signal(msg, 45, kRate, duration_for(msg, 45)));
    d.reset();
    EXPECT_DOUBLE_EQ(d.wpm(), 25.0);
}
```

Note on `ChunkSizeDoesNotChangeOutput`: `t0_s` for each chunk is computed as `i / kRate`, so sample times are identical however the input is split.

`engine/include/kz4ap/decoder.hpp`:
```cpp
#pragma once

#include "kz4ap/types.hpp"

#include <span>
#include <vector>

namespace kz4ap {

struct DecodedChar {
    char ch;            // ' ' marks a word space, '*' a pattern that is not a Morse character
    float probability;  // the decoder's belief that ch is right, 0..1
    double start_s;
    double end_s;
};

struct DecodeUpdate {
    std::vector<DecodedChar> chars;
    float wpm = 0;
    float confidence = 0;  // running average of recent character probabilities
};

// Decodes one channel's baseband stream into characters.
class Decoder {
public:
    virtual ~Decoder() = default;
    // samples: channel baseband; t0_s: time of samples[0].
    virtual DecodeUpdate process(std::span<const Sample> samples, double t0_s) = 0;
    // Emits any partly received character (end of input, or the track died).
    virtual DecodeUpdate flush() = 0;
    // Forgets all state (e.g. after a gap in the input).
    virtual void reset() = 0;
};

}  // namespace kz4ap
```

`engine/include/kz4ap/classical_decoder.hpp`:
```cpp
#pragma once

#include "kz4ap/decoder.hpp"

#include <deque>
#include <vector>

namespace kz4ap {

struct ClassicalDecoderConfig {
    double initial_wpm = 25.0;
    double min_wpm = 5.0;
    double max_wpm = 60.0;
    double attack_s = 0.004;       // how fast mark/space levels follow a new extreme
    double decay_s = 3.0;          // how fast they relax back
    float squelch_ratio = 3.0f;    // mark level must exceed space level by this factor to key
    double smoothing_dits = 0.25;  // envelope smoothing time constant, in dits
    double glitch_dits = 0.3;      // marks and dropouts shorter than this are ignored
};

// Baseline statistical decoder: envelope keying against adaptive levels, speed
// from the dit/dah split of recent marks, probabilities from timing margins.
class ClassicalDecoder final : public Decoder {
public:
    explicit ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config = {});

    DecodeUpdate process(std::span<const Sample> samples, double t0_s) override;
    DecodeUpdate flush() override;
    void reset() override;

    double wpm() const { return 1.2 / dit_s_; }

private:
    struct Element {
        bool dah;
        float confidence;
        double start_s;
        double end_s;
    };

    void step(float magnitude, double t, DecodeUpdate& out);
    void key_down(double t);
    void key_up(double t);
    void finish_char(DecodeUpdate& out);
    void emit(DecodeUpdate& out, char ch, float probability, double start_s, double end_s);
    void update_speed();

    double rate_;
    ClassicalDecoderConfig config_;
    float attack_alpha_;
    float decay_alpha_;
    float smooth_alpha_ = 1;
    double dit_s_ = 0;
    float smoothed_ = 0;
    float mark_ = 0;
    float space_ = 0;
    bool primed_ = false;
    bool key_ = false;
    double down_t_ = 0;
    double prev_down_t_ = 0;
    double up_t_ = 0;
    bool char_open_ = false;  // elements received since the last character boundary
    bool word_open_ = false;  // characters received since the last word space
    std::vector<Element> elements_;
    std::deque<double> recent_marks_;
    float confidence_ = 0;
};

}  // namespace kz4ap
```

`engine/src/classical_decoder.cpp` (stub):
```cpp
#include "kz4ap/classical_decoder.hpp"

namespace kz4ap {

ClassicalDecoder::ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config)
    : rate_(sample_rate), config_(config), attack_alpha_(0), decay_alpha_(0), dit_s_(1.2 / config.initial_wpm) {}
DecodeUpdate ClassicalDecoder::process(std::span<const Sample>, double) { return {}; }
DecodeUpdate ClassicalDecoder::flush() { return {}; }
void ClassicalDecoder::reset() {}
void ClassicalDecoder::step(float, double, DecodeUpdate&) {}
void ClassicalDecoder::key_down(double) {}
void ClassicalDecoder::key_up(double) {}
void ClassicalDecoder::finish_char(DecodeUpdate&) {}
void ClassicalDecoder::emit(DecodeUpdate&, char, float, double, double) {}
void ClassicalDecoder::update_speed() {}

}  // namespace kz4ap
```

`engine/CMakeLists.txt`: add `src/classical_decoder.cpp` to `kz4ap_engine` and `tests/classical_decoder_test.cpp` to `kz4ap_engine_tests`.

- [ ] **Step 2: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R ClassicalDecoder
```
Expected: the decoding tests FAIL. `NoiseAloneDecodesNothing` and `ChunkSizeDoesNotChangeOutput` may pass against the stub; that's fine.

- [ ] **Step 3: Implement**

`engine/src/classical_decoder.cpp`:
```cpp
#include "kz4ap/classical_decoder.hpp"

#include "kz4ap/morse.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

namespace kz4ap {
namespace {

constexpr double kLog2 = 0.6931471805599453;
constexpr double kDahSlope = 0.08;  // width of the dit/dah decision, in log-duration units
constexpr std::size_t kSpeedWindow = 24;

float alpha_for(double tau_s, double rate) {
    return static_cast<float>(1.0 - std::exp(-1.0 / (tau_s * rate)));
}

float logistic(double x) { return static_cast<float>(1.0 / (1.0 + std::exp(-x))); }

}  // namespace

ClassicalDecoder::ClassicalDecoder(double sample_rate, ClassicalDecoderConfig config)
    : rate_(sample_rate),
      config_(config),
      attack_alpha_(alpha_for(config.attack_s, sample_rate)),
      decay_alpha_(alpha_for(config.decay_s, sample_rate)) {
    reset();
}

void ClassicalDecoder::reset() {
    dit_s_ = 1.2 / config_.initial_wpm;
    smooth_alpha_ = alpha_for(std::max(config_.smoothing_dits * dit_s_, 1.0 / rate_), rate_);
    smoothed_ = mark_ = space_ = 0;
    primed_ = key_ = false;
    down_t_ = prev_down_t_ = up_t_ = 0;
    char_open_ = word_open_ = false;
    elements_.clear();
    recent_marks_.clear();
    confidence_ = 0;
}

DecodeUpdate ClassicalDecoder::process(std::span<const Sample> samples, double t0_s) {
    DecodeUpdate out;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        step(std::abs(samples[i]), t0_s + static_cast<double>(i) / rate_, out);
    }
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    return out;
}

DecodeUpdate ClassicalDecoder::flush() {
    DecodeUpdate out;
    if (char_open_) finish_char(out);
    out.wpm = static_cast<float>(wpm());
    out.confidence = confidence_;
    return out;
}

void ClassicalDecoder::step(float magnitude, double t, DecodeUpdate& out) {
    smoothed_ += smooth_alpha_ * (magnitude - smoothed_);
    const float env = smoothed_;
    if (!primed_) {
        mark_ = space_ = env;
        primed_ = true;
    }
    mark_ += (env > mark_ ? attack_alpha_ : decay_alpha_) * (env - mark_);
    space_ += (env < space_ ? attack_alpha_ : decay_alpha_) * (env - space_);

    const float span = mark_ - space_;
    const bool squelched = span <= 0 || mark_ < config_.squelch_ratio * space_;
    if (!key_ && !squelched && env > space_ + 0.6f * span) {
        key_down(t);
    } else if (key_ && (squelched || env < space_ + 0.4f * span)) {
        key_up(t);
    }

    if (!key_) {
        const double gap = t - up_t_;
        if (char_open_ && gap > 2.0 * dit_s_) finish_char(out);
        if (word_open_ && !char_open_ && gap > 5.0 * dit_s_) {
            emit(out, ' ', 1.0f, up_t_, t);
            word_open_ = false;
        }
    }
}

void ClassicalDecoder::key_down(double t) {
    key_ = true;
    if (char_open_ && !elements_.empty() && t - up_t_ < config_.glitch_dits * dit_s_) {
        // The key-up was a dropout inside one element: merge it back into that element.
        elements_.pop_back();
        if (!recent_marks_.empty()) recent_marks_.pop_back();
        down_t_ = prev_down_t_;
        return;
    }
    down_t_ = t;
}

void ClassicalDecoder::key_up(double t) {
    key_ = false;
    const double duration = t - down_t_;
    if (duration < config_.glitch_dits * dit_s_) return;  // too short to be an element
    const float p_dah = logistic((std::log(duration / dit_s_) - kLog2) / kDahSlope);
    const bool dah = p_dah >= 0.5f;
    elements_.push_back({dah, dah ? p_dah : 1.0f - p_dah, down_t_, t});
    prev_down_t_ = down_t_;
    up_t_ = t;
    char_open_ = true;
    recent_marks_.push_back(duration);
    if (recent_marks_.size() > kSpeedWindow) recent_marks_.pop_front();
    update_speed();
}

void ClassicalDecoder::finish_char(DecodeUpdate& out) {
    std::string pattern;
    float probability = 1.0f;
    for (const auto& e : elements_) {
        pattern += e.dah ? '-' : '.';
        probability *= e.confidence;
    }
    const char ch = morse::decode(pattern);
    emit(out, ch != '\0' ? ch : '*', ch != '\0' ? probability : 0.0f, elements_.front().start_s,
         elements_.back().end_s);
    elements_.clear();
    char_open_ = false;
    word_open_ = true;
}

void ClassicalDecoder::emit(DecodeUpdate& out, char ch, float probability, double start_s, double end_s) {
    out.chars.push_back({ch, probability, start_s, end_s});
    if (ch != ' ') confidence_ += 0.2f * (probability - confidence_);
}

void ClassicalDecoder::update_speed() {
    std::vector<double> d(recent_marks_.begin(), recent_marks_.end());
    if (d.size() < 2) return;
    std::sort(d.begin(), d.end());
    std::size_t split = 0;
    double best = 0;
    for (std::size_t i = 0; i + 1 < d.size(); ++i) {
        const double ratio = d[i + 1] / d[i];
        if (ratio > best) {
            best = ratio;
            split = i + 1;
        }
    }
    double dit = dit_s_;
    if (best >= 1.8) {
        // Two clusters: dits below the split, dahs (three dits each) above it.
        const double dits = std::accumulate(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(split), 0.0);
        const double dahs = std::accumulate(d.begin() + static_cast<std::ptrdiff_t>(split), d.end(), 0.0);
        dit = (dits + dahs / 3.0) / static_cast<double>(d.size());
    } else {
        // All recent marks look alike: they are dits or dahs by the current estimate.
        const double mean = std::accumulate(d.begin(), d.end(), 0.0) / static_cast<double>(d.size());
        dit = mean > 2.0 * dit_s_ ? mean / 3.0 : mean;
    }
    dit_s_ = std::clamp(dit, 1.2 / config_.max_wpm, 1.2 / config_.min_wpm);
    smooth_alpha_ = alpha_for(std::max(config_.smoothing_dits * dit_s_, 1.0 / rate_), rate_);
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass. The two tests that depend on tuned constants are `NoiseAloneDecodesNothing` and `PauseDecodesNothingThenResumes`. If either fails, debug with superpowers:systematic-debugging, printing `mark_`, `space_`, and the key state over time. The expected knobs are `decay_s` (2–4 s) and `squelch_ratio` (2.5–4). Change the defaults only if the rest of the suite still passes afterward.

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add engine
```
Commit message: `Add decoder interface and classical decoder`

---

### Task 8: Event bus and engine pipeline

**Files:**
- Create: `engine/include/kz4ap/event_bus.hpp`, `engine/include/kz4ap/engine.hpp`, `engine/src/engine.cpp`
- Modify: `engine/CMakeLists.txt` (add `src/engine.cpp`, `tests/engine_test.cpp`)
- Test: `engine/tests/engine_test.cpp`

**Interfaces:**
- Consumes: `SpectrumAnalyzer`, `SignalDetector`/`Track`/`DetectorConfig`, `Channelizer`/`ChannelizerConfig`, `Decoder`/`DecodeUpdate`/`DecodedChar`, `ClassicalDecoder`/`ClassicalDecoderConfig`.
- Produces:
  ```cpp
  struct TrackEvent { enum class Kind { Born, Died }; Kind kind; Track track; };
  struct DecodedTextEvent { std::uint32_t track_id; double freq_hz; std::vector<DecodedChar> chars; float wpm; float confidence; };
  using Event = std::variant<SpectrumFrame, TrackEvent, DecodedTextEvent>;
  class EventBus { using Handler = std::function<void(const Event&)>; void subscribe(Handler); void publish(const Event&) const; };
  struct EngineConfig { int sample_rate = 192000; int fft_size = 8192; int channel_bins = 64; double channel_cutoff_hz = 150.0;
                        DetectorConfig detector; ClassicalDecoderConfig decoder; };
  class Engine { Engine(const EngineConfig&, EventBus&); void process(std::span<const Sample>); void finish(); };
  ```
  Every character appears in exactly one `DecodedTextEvent`. For each track, a `Born` event precedes its text and a `Died` event, if any, follows it.

- [ ] **Step 1: Write the failing test**

`engine/tests/engine_test.cpp`:
```cpp
#include "kz4ap/engine.hpp"

#include "test_signals.hpp"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using namespace kz4ap;
using kz4ap::test::keyed_signal;

namespace {

constexpr double kRate = 192000;
constexpr double kNoiseSigma = 0.02;
constexpr double kAmplitude20dB = 0.0102;  // 20 dB SNR in 500 Hz against kNoiseSigma

struct Result {
    std::vector<Track> born;
    std::map<std::uint32_t, std::string> text;
};

Result run(const std::vector<Sample>& x, std::size_t chunk) {
    EventBus bus;
    Result r;
    bus.subscribe([&](const Event& e) {
        if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) r.born.push_back(t->track);
        if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
            for (const auto& c : d->chars) r.text[d->track_id] += c.ch;
        }
    });
    Engine engine(EngineConfig{}, bus);
    for (std::size_t i = 0; i < x.size(); i += chunk) {
        engine.process(std::span<const Sample>(x).subspan(i, std::min(chunk, x.size() - i)));
    }
    engine.finish();
    return r;
}

std::vector<Sample> band(double duration_s) {
    auto x = keyed_signal("VVV CQ K1ABC", 25, kRate, duration_s, 12000.0, kAmplitude20dB, kNoiseSigma, 11);
    const auto y = keyed_signal("VVV CQ W9XYZ", 22, kRate, duration_s, -30000.0, kAmplitude20dB, 0.0, 12);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    return x;
}

}  // namespace

TEST(Engine, FindsAndDecodesTwoSignals) {
    const auto r = run(band(9.0), 65536);
    ASSERT_EQ(r.born.size(), 2u);
    std::map<double, std::string> by_freq;
    for (const auto& t : r.born) by_freq[t.freq_hz] = r.text.count(t.id) ? r.text.at(t.id) : "";
    const auto low = by_freq.begin();
    const auto high = std::next(low);
    EXPECT_NEAR(low->first, -30000.0, 25.0);
    EXPECT_NEAR(high->first, 12000.0, 25.0);
    EXPECT_NE(low->second.find("CQ W9XYZ"), std::string::npos) << low->second;
    EXPECT_NE(high->second.find("CQ K1ABC"), std::string::npos) << high->second;
}

TEST(Engine, ChunkingDoesNotChangeResults) {
    const auto x = band(9.0);
    const auto a = run(x, 1000);
    const auto b = run(x, 77777);
    ASSERT_EQ(a.born.size(), b.born.size());
    for (std::size_t i = 0; i < a.born.size(); ++i) EXPECT_EQ(a.born[i].freq_hz, b.born[i].freq_hz);
    EXPECT_EQ(a.text, b.text);
}

TEST(Engine, NoiseAloneMakesNoTracks) {
    const auto r = run(keyed_signal("", 25, kRate, 4.0, 0, 0.0, kNoiseSigma, 13), 65536);
    EXPECT_TRUE(r.born.empty());
}
```

`engine/include/kz4ap/event_bus.hpp`:
```cpp
#pragma once

#include "kz4ap/decoder.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"

#include <cstdint>
#include <functional>
#include <utility>
#include <variant>
#include <vector>

namespace kz4ap {

struct TrackEvent {
    enum class Kind { Born, Died };
    Kind kind;
    Track track;
};

struct DecodedTextEvent {
    std::uint32_t track_id;
    double freq_hz;
    std::vector<DecodedChar> chars;
    float wpm;
    float confidence;
};

using Event = std::variant<SpectrumFrame, TrackEvent, DecodedTextEvent>;

// Delivers engine events to every subscriber, synchronously, in subscription order.
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler handler) { handlers_.push_back(std::move(handler)); }

    void publish(const Event& event) const {
        for (const auto& h : handlers_) h(event);
    }

private:
    std::vector<Handler> handlers_;
};

}  // namespace kz4ap
```

`engine/include/kz4ap/engine.hpp`:
```cpp
#pragma once

#include "kz4ap/channelizer.hpp"
#include "kz4ap/classical_decoder.hpp"
#include "kz4ap/event_bus.hpp"
#include "kz4ap/signal_detector.hpp"
#include "kz4ap/spectrum.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace kz4ap {

struct EngineConfig {
    int sample_rate = 192000;
    int fft_size = 8192;
    int channel_bins = 64;
    double channel_cutoff_hz = 150.0;
    DetectorConfig detector;  // sample_rate, fft_size and hop are overwritten by the engine
    ClassicalDecoderConfig decoder;
};

// Runs the decoding pipeline synchronously on the caller's thread. Output is
// identical however the input is split across calls to process().
class Engine {
public:
    Engine(const EngineConfig& config, EventBus& bus);
    ~Engine();

    void process(std::span<const Sample> samples);

    // End of input: processes buffered samples and flushes partly decoded characters.
    void finish();

private:
    struct Channel {
        Track track;
        std::unique_ptr<Decoder> decoder;
    };

    void process_hop(std::span<const Sample> hop);
    void close_channel(std::uint32_t id);
    void publish_update(std::uint32_t id, DecodeUpdate&& update);

    EngineConfig config_;
    EventBus& bus_;
    int hop_;
    SpectrumAnalyzer spectrum_;
    SignalDetector detector_;
    Channelizer channelizer_;
    std::vector<Sample> pending_;
    std::map<std::uint32_t, Channel> channels_;
};

}  // namespace kz4ap
```

`engine/src/engine.cpp` (stub):
```cpp
#include "kz4ap/engine.hpp"

namespace kz4ap {

Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(config), bus_(bus), hop_(config.fft_size / 2),
      spectrum_(config.sample_rate, config.fft_size, config.fft_size / 2),
      detector_(config.detector),
      channelizer_(ChannelizerConfig{config.sample_rate, config.fft_size, config.channel_bins, config.channel_cutoff_hz}) {}
Engine::~Engine() = default;
void Engine::process(std::span<const Sample>) {}
void Engine::finish() {}
void Engine::process_hop(std::span<const Sample>) {}
void Engine::close_channel(std::uint32_t) {}
void Engine::publish_update(std::uint32_t, DecodeUpdate&&) {}

}  // namespace kz4ap
```

`engine/CMakeLists.txt`: add `src/engine.cpp` to `kz4ap_engine` and `tests/engine_test.cpp` to `kz4ap_engine_tests`.

- [ ] **Step 2: Build and run — expect failures**

```powershell
cmake --build --preset windows
ctest --preset windows -R Engine
```
Expected: `FindsAndDecodesTwoSignals` FAILS (no tracks born). The other two may pass against the stub.

- [ ] **Step 3: Implement**

`engine/src/engine.cpp`:
```cpp
#include "kz4ap/engine.hpp"

#include <utility>

namespace kz4ap {
namespace {

DetectorConfig detector_config(const EngineConfig& c) {
    DetectorConfig d = c.detector;
    d.sample_rate = c.sample_rate;
    d.fft_size = c.fft_size;
    d.hop = c.fft_size / 2;
    return d;
}

}  // namespace

Engine::Engine(const EngineConfig& config, EventBus& bus)
    : config_(config),
      bus_(bus),
      hop_(config.fft_size / 2),
      spectrum_(config.sample_rate, config.fft_size, config.fft_size / 2),
      detector_(detector_config(config)),
      channelizer_(ChannelizerConfig{config.sample_rate, config.fft_size, config.channel_bins,
                                     config.channel_cutoff_hz}) {
    pending_.reserve(static_cast<std::size_t>(hop_));
}

Engine::~Engine() = default;

void Engine::process(std::span<const Sample> samples) {
    // Work in whole hops so the spectrum analyzer and channelizer stay in lockstep,
    // whatever size the caller's chunks are.
    for (const Sample& s : samples) {
        pending_.push_back(s);
        if (static_cast<int>(pending_.size()) == hop_) {
            process_hop(pending_);
            pending_.clear();
        }
    }
}

void Engine::finish() {
    if (!pending_.empty()) {
        pending_.resize(static_cast<std::size_t>(hop_), Sample{});
        process_hop(pending_);
        pending_.clear();
    }
    for (auto& [id, channel] : channels_) publish_update(id, channel.decoder->flush());
}

void Engine::process_hop(std::span<const Sample> hop) {
    for (auto& frame : spectrum_.push(hop)) {
        const auto update = detector_.process(frame);
        bus_.publish(Event{std::move(frame)});
        for (const auto id : update.died) close_channel(id);
        for (const auto& track : update.born) {
            channelizer_.add_channel(track.id, channelizer_.hz_to_bin(track.freq_hz));
            channels_.emplace(track.id, Channel{track, std::make_unique<ClassicalDecoder>(
                                                           channelizer_.output_rate(), config_.decoder)});
            bus_.publish(Event{TrackEvent{TrackEvent::Kind::Born, track}});
        }
    }
    channelizer_.push(hop, [this](std::uint32_t id, std::uint64_t first_index, std::span<const Sample> s) {
        const auto it = channels_.find(id);
        if (it == channels_.end()) return;
        const double t0 = static_cast<double>(first_index) / channelizer_.output_rate();
        publish_update(id, it->second.decoder->process(s, t0));
    });
}

void Engine::close_channel(std::uint32_t id) {
    const auto it = channels_.find(id);
    if (it == channels_.end()) return;
    publish_update(id, it->second.decoder->flush());
    bus_.publish(Event{TrackEvent{TrackEvent::Kind::Died, it->second.track}});
    channelizer_.remove_channel(id);
    channels_.erase(it);
}

void Engine::publish_update(std::uint32_t id, DecodeUpdate&& update) {
    if (update.chars.empty()) return;
    const auto& channel = channels_.at(id);
    bus_.publish(Event{DecodedTextEvent{id, channel.track.freq_hz, std::move(update.chars), update.wpm,
                                        update.confidence}});
}

}  // namespace kz4ap
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass.

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add engine
```
Commit message: `Add event bus and engine pipeline`

---

### Task 9: Benchmark tool (`kz4ap-bench`)

**Files:**
- Modify: `CMakeLists.txt` (fetch nlohmann/json; `add_subdirectory(bench)`)
- Create: `bench/CMakeLists.txt`, `bench/src/labels.hpp`, `bench/src/labels.cpp`, `bench/src/scoring.hpp`, `bench/src/scoring.cpp`, `bench/src/main.cpp`
- Test: `bench/tests/labels_test.cpp`, `bench/tests/scoring_test.cpp`

**Interfaces:**
- Consumes: `WavIqReader`, `Engine`, `EngineConfig`, `EventBus`, `TrackEvent`, `DecodedTextEvent`; the labels JSON format from Task 2.
- Produces:
  ```cpp
  struct LabeledSignal { std::string text; double freq_offset_hz; double wpm; double snr_db; double start_s; double end_s; };
  struct Labels { int sample_rate; double duration_s; std::vector<LabeledSignal> signals; };
  Labels parse_labels(const std::string& json_text);        // throws std::runtime_error
  Labels load_labels(const std::filesystem::path& path);    // throws std::runtime_error
  struct DecodedTrack { std::uint32_t id; double freq_hz; std::string text; };
  struct SignalScore { LabeledSignal label; std::optional<std::uint32_t> track_id; std::string decoded; std::size_t edits; double cer; };
  struct Score { std::vector<SignalScore> signals; double cer; std::size_t detected; std::size_t false_tracks; };
  std::string normalize_text(std::string_view);
  std::size_t edit_distance(std::string_view, std::string_view);
  Score score(const std::vector<LabeledSignal>&, const std::vector<DecodedTrack>&, double match_tolerance_hz = 50.0);
  ```
  CLI: `kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json] [--no-timing] [--baseline BASELINE.json]`. Exit code 0 = ok, 1 = worse than the baseline, 2 = error. The baseline file is `{"max_cer": <number>, "min_detection_recall": <number>}`.

- [ ] **Step 1: Write the failing tests**

`bench/tests/scoring_test.cpp`:
```cpp
#include "scoring.hpp"

#include <gtest/gtest.h>

using namespace kz4ap::bench;

TEST(Scoring, NormalizeUppercasesAndCollapsesSpaces) {
    EXPECT_EQ(normalize_text("  cq   k1abc \t"), "CQ K1ABC");
    EXPECT_EQ(normalize_text(""), "");
}

TEST(Scoring, EditDistance) {
    EXPECT_EQ(edit_distance("", ""), 0u);
    EXPECT_EQ(edit_distance("ABC", "ABD"), 1u);
    EXPECT_EQ(edit_distance("K1ABC", "K1AB"), 1u);
    EXPECT_EQ(edit_distance("", "ABC"), 3u);
    EXPECT_EQ(edit_distance("KITTEN", "SITTING"), 3u);
}

TEST(Scoring, MatchesNearestTrackWithinTolerance) {
    const std::vector<LabeledSignal> labels{{"CQ K1ABC", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 1010.0, "CQ K1ABC"}, {2, 3000.0, "TU"}};
    const auto s = score(labels, tracks);
    ASSERT_EQ(s.signals.size(), 1u);
    EXPECT_EQ(s.signals[0].track_id, 1u);
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 0.0);
    EXPECT_EQ(s.detected, 1u);
    EXPECT_EQ(s.false_tracks, 1u);
}

TEST(Scoring, MissedSignalCountsEveryCharacter) {
    const std::vector<LabeledSignal> labels{{"CQ", 1000.0, 25, 20, 0, 5}};
    const auto s = score(labels, {});
    EXPECT_FALSE(s.signals[0].track_id.has_value());
    EXPECT_DOUBLE_EQ(s.signals[0].cer, 1.0);
    EXPECT_EQ(s.detected, 0u);
}

TEST(Scoring, AggregateCerWeightsByLength) {
    const std::vector<LabeledSignal> labels{{"ABCDEFGHIJ", 0.0, 25, 20, 0, 5}, {"AB", 5000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 0.0, "ABCDEFGHIJ"}, {2, 5000.0, "XY"}};
    EXPECT_DOUBLE_EQ(score(labels, tracks).cer, 2.0 / 12.0);
}

TEST(Scoring, SplitTrackUsesTheLongestText) {
    const std::vector<LabeledSignal> labels{{"CQ K1ABC", 1000.0, 25, 20, 0, 5}};
    const std::vector<DecodedTrack> tracks{{1, 990.0, "E"}, {2, 1020.0, "CQ K1ABC"}};
    const auto s = score(labels, tracks);
    EXPECT_EQ(s.signals[0].track_id, 2u);
    EXPECT_EQ(s.false_tracks, 1u);
}
```

`bench/tests/labels_test.cpp`:
```cpp
#include "labels.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace kz4ap::bench;

TEST(Labels, ParsesGeneratorOutput) {
    const auto labels = parse_labels(R"({
      "sample_rate": 192000, "duration_s": 30.0, "snr_bandwidth_hz": 500.0,
      "signals": [{"text": "CQ K1ABC", "freq_offset_hz": -1234.5, "wpm": 25.0,
                   "snr_db": 15.0, "start_s": 0.5, "end_s": 6.25}]})");
    EXPECT_EQ(labels.sample_rate, 192000);
    EXPECT_DOUBLE_EQ(labels.duration_s, 30.0);
    ASSERT_EQ(labels.signals.size(), 1u);
    EXPECT_EQ(labels.signals[0].text, "CQ K1ABC");
    EXPECT_DOUBLE_EQ(labels.signals[0].freq_offset_hz, -1234.5);
    EXPECT_DOUBLE_EQ(labels.signals[0].end_s, 6.25);
}

TEST(Labels, RejectsMissingField) {
    EXPECT_THROW(parse_labels(R"({"sample_rate": 192000, "signals": []})"), std::runtime_error);
}

TEST(Labels, RejectsMalformedJson) {
    EXPECT_THROW(parse_labels("{not json"), std::runtime_error);
}
```

Add the declarations `bench/src/labels.hpp`:
```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace kz4ap::bench {

struct LabeledSignal {
    std::string text;
    double freq_offset_hz;
    double wpm;
    double snr_db;
    double start_s;
    double end_s;
};

struct Labels {
    int sample_rate = 0;
    double duration_s = 0;
    std::vector<LabeledSignal> signals;
};

// Parses a labels file written by training/kz4ap_synth. Throws std::runtime_error on bad input.
Labels parse_labels(const std::string& json_text);
Labels load_labels(const std::filesystem::path& path);

}  // namespace kz4ap::bench
```

`bench/src/scoring.hpp`:
```cpp
#pragma once

#include "labels.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kz4ap::bench {

struct DecodedTrack {
    std::uint32_t id;
    double freq_hz;
    std::string text;
};

struct SignalScore {
    LabeledSignal label;
    std::optional<std::uint32_t> track_id;  // empty if no track matched
    std::string decoded;                    // normalized
    std::size_t edits;
    double cer;
};

struct Score {
    std::vector<SignalScore> signals;
    double cer = 0;                // total edits / total reference characters
    std::size_t detected = 0;      // labeled signals matched to a track
    std::size_t false_tracks = 0;  // unmatched tracks that decoded some text
};

// Uppercase, whitespace runs collapsed to one space, ends trimmed.
std::string normalize_text(std::string_view s);

// Levenshtein distance.
std::size_t edit_distance(std::string_view a, std::string_view b);

// Matches each labeled signal to the track within match_tolerance_hz that
// decoded the most text, and scores the character error rate.
Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz = 50.0);

}  // namespace kz4ap::bench
```

Stubs `bench/src/labels.cpp`:
```cpp
#include "labels.hpp"

namespace kz4ap::bench {

Labels parse_labels(const std::string&) { return {}; }
Labels load_labels(const std::filesystem::path&) { return {}; }

}  // namespace kz4ap::bench
```
and `bench/src/scoring.cpp`:
```cpp
#include "scoring.hpp"

namespace kz4ap::bench {

std::string normalize_text(std::string_view) { return {}; }
std::size_t edit_distance(std::string_view, std::string_view) { return 0; }
Score score(const std::vector<LabeledSignal>&, const std::vector<DecodedTrack>&, double) { return {}; }

}  // namespace kz4ap::bench
```
and a placeholder `bench/src/main.cpp`:
```cpp
int main() { return 2; }
```

`bench/CMakeLists.txt`:
```cmake
add_library(kz4ap_bench_lib
  src/labels.cpp
  src/scoring.cpp
)
target_include_directories(kz4ap_bench_lib PUBLIC src)
target_link_libraries(kz4ap_bench_lib PUBLIC kz4ap::engine nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(kz4ap_bench_lib PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(kz4ap_bench_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(kz4ap-bench src/main.cpp)
target_link_libraries(kz4ap-bench PRIVATE kz4ap_bench_lib)

add_executable(kz4ap_bench_tests
  tests/labels_test.cpp
  tests/scoring_test.cpp
)
target_link_libraries(kz4ap_bench_tests PRIVATE kz4ap_bench_lib GTest::gtest_main)
gtest_discover_tests(kz4ap_bench_tests)
```

Root `CMakeLists.txt`: after the GoogleTest `FetchContent_MakeAvailable(googletest)` line add
```cmake
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)
```
and after `add_subdirectory(engine)` add
```cmake
add_subdirectory(bench)
```

- [ ] **Step 2: Configure, build, run — expect failures**

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows -R "Scoring|Labels"
```
Expected: Scoring and Labels tests FAIL.

- [ ] **Step 3: Implement labels, scoring, and the CLI**

`bench/src/labels.cpp`:
```cpp
#include "labels.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kz4ap::bench {

Labels parse_labels(const std::string& json_text) {
    try {
        const auto j = nlohmann::json::parse(json_text);
        Labels labels;
        labels.sample_rate = j.at("sample_rate").get<int>();
        labels.duration_s = j.at("duration_s").get<double>();
        for (const auto& s : j.at("signals")) {
            labels.signals.push_back({s.at("text").get<std::string>(), s.at("freq_offset_hz").get<double>(),
                                      s.at("wpm").get<double>(), s.at("snr_db").get<double>(),
                                      s.at("start_s").get<double>(), s.at("end_s").get<double>()});
        }
        return labels;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("bad labels file: ") + e.what());
    }
}

Labels load_labels(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    std::ostringstream text;
    text << f.rdbuf();
    return parse_labels(text.str());
}

}  // namespace kz4ap::bench
```

`bench/src/scoring.cpp`:
```cpp
#include "scoring.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace kz4ap::bench {

std::string normalize_text(std::string_view s) {
    std::string out;
    for (const char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::size_t edit_distance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t substitute = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, substitute});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

Score score(const std::vector<LabeledSignal>& labels, const std::vector<DecodedTrack>& tracks,
            double match_tolerance_hz) {
    Score result;
    std::set<std::uint32_t> used;
    std::size_t total_edits = 0;
    std::size_t total_chars = 0;
    for (const auto& label : labels) {
        const std::string reference = normalize_text(label.text);
        const DecodedTrack* best = nullptr;
        std::size_t best_len = 0;
        for (const auto& t : tracks) {
            if (used.count(t.id) || std::abs(t.freq_hz - label.freq_offset_hz) > match_tolerance_hz) continue;
            const std::size_t len = normalize_text(t.text).size();
            if (!best || len > best_len ||
                (len == best_len && std::abs(t.freq_hz - label.freq_offset_hz) <
                                        std::abs(best->freq_hz - label.freq_offset_hz))) {
                best = &t;
                best_len = len;
            }
        }
        SignalScore s{label, std::nullopt, "", 0, 0.0};
        if (best) {
            used.insert(best->id);
            s.track_id = best->id;
            s.decoded = normalize_text(best->text);
            ++result.detected;
        }
        s.edits = edit_distance(reference, s.decoded);
        s.cer = reference.empty() ? 0.0 : static_cast<double>(s.edits) / static_cast<double>(reference.size());
        total_edits += s.edits;
        total_chars += reference.size();
        result.signals.push_back(std::move(s));
    }
    result.cer = total_chars == 0 ? 0.0 : static_cast<double>(total_edits) / static_cast<double>(total_chars);
    for (const auto& t : tracks) {
        if (!used.count(t.id) && !normalize_text(t.text).empty()) ++result.false_tracks;
    }
    return result;
}

}  // namespace kz4ap::bench
```

`bench/src/main.cpp`:
```cpp
// kz4ap-bench: runs the engine over an I/Q recording and scores the result.

#include "labels.hpp"
#include "scoring.hpp"

#include "kz4ap/engine.hpp"
#include "kz4ap/wav_reader.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace kz4ap;
using namespace kz4ap::bench;

namespace {

struct Args {
    std::filesystem::path recording;
    std::optional<std::filesystem::path> labels;
    std::optional<std::filesystem::path> json;
    std::optional<std::filesystem::path> baseline;
    bool timing = true;
};

constexpr const char* kUsage =
    "usage: kz4ap-bench RECORDING.wav [--labels LABELS.json] [--json OUT.json]\n"
    "                   [--no-timing] [--baseline BASELINE.json]\n";

Args parse_args(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::filesystem::path {
            if (i + 1 >= argc) throw std::runtime_error(a + " needs a value\n" + kUsage);
            return argv[++i];
        };
        if (a == "--labels") args.labels = value();
        else if (a == "--json") args.json = value();
        else if (a == "--baseline") args.baseline = value();
        else if (a == "--no-timing") args.timing = false;
        else if (!a.empty() && a[0] == '-') throw std::runtime_error("unknown option " + a + "\n" + kUsage);
        else if (args.recording.empty()) args.recording = a;
        else throw std::runtime_error(std::string("more than one recording given\n") + kUsage);
    }
    if (args.recording.empty()) throw std::runtime_error(kUsage);
    if (args.baseline && !args.labels) throw std::runtime_error("--baseline needs --labels");
    return args;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args args = parse_args(argc, argv);
        WavIqReader reader(args.recording);

        EventBus bus;
        std::map<std::uint32_t, DecodedTrack> tracks;
        bus.subscribe([&](const Event& e) {
            if (const auto* t = std::get_if<TrackEvent>(&e); t && t->kind == TrackEvent::Kind::Born) {
                tracks[t->track.id] = {t->track.id, t->track.freq_hz, ""};
            } else if (const auto* d = std::get_if<DecodedTextEvent>(&e)) {
                for (const auto& c : d->chars) tracks[d->track_id].text += c.ch;
            }
        });

        EngineConfig config;
        config.sample_rate = reader.sample_rate();
        Engine engine(config, bus);
        std::vector<Sample> block(65536);
        const auto started = std::chrono::steady_clock::now();
        while (const auto n = reader.read(block)) engine.process(std::span<const Sample>(block).first(n));
        engine.finish();
        const double wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const double duration_s = static_cast<double>(reader.total_samples()) / reader.sample_rate();

        nlohmann::json out;
        out["recording"] = args.recording.filename().string();
        out["duration_s"] = duration_s;
        out["tracks"] = nlohmann::json::array();
        std::vector<DecodedTrack> track_list;
        for (const auto& [id, t] : tracks) {
            track_list.push_back(t);
            out["tracks"].push_back({{"id", id}, {"freq_hz", t.freq_hz}, {"text", normalize_text(t.text)}});
            std::printf("track %4u  %+10.1f Hz  %s\n", id, t.freq_hz, normalize_text(t.text).c_str());
        }
        if (args.timing) {
            out["realtime_factor"] = duration_s / wall_s;
            std::printf("processed %.1f s of audio in %.2f s (%.1fx real time)\n", duration_s, wall_s,
                        duration_s / wall_s);
        }

        int exit_code = 0;
        if (args.labels) {
            const Labels labels = load_labels(*args.labels);
            if (labels.sample_rate != reader.sample_rate())
                throw std::runtime_error("labels sample rate does not match the recording");
            const Score s = score(labels.signals, track_list);
            const double recall = labels.signals.empty()
                                      ? 1.0
                                      : static_cast<double>(s.detected) / static_cast<double>(labels.signals.size());
            nlohmann::json signals = nlohmann::json::array();
            for (const auto& sig : s.signals) {
                signals.push_back({{"freq_offset_hz", sig.label.freq_offset_hz},
                                   {"reference", normalize_text(sig.label.text)},
                                   {"decoded", sig.decoded},
                                   {"track_id", sig.track_id ? nlohmann::json(*sig.track_id) : nlohmann::json()},
                                   {"cer", sig.cer}});
                std::printf("label %+10.1f Hz  CER %5.3f  %s\n", sig.label.freq_offset_hz, sig.cer,
                            sig.track_id ? "" : "(not detected)");
            }
            out["score"] = {{"cer", s.cer},
                            {"detected", s.detected},
                            {"labels", labels.signals.size()},
                            {"detection_recall", recall},
                            {"false_tracks", s.false_tracks},
                            {"signals", signals}};
            std::printf("CER %.4f, detected %zu of %zu, %zu false tracks\n", s.cer, s.detected,
                        labels.signals.size(), s.false_tracks);

            if (args.baseline) {
                std::ifstream f(*args.baseline);
                if (!f) throw std::runtime_error("cannot open " + args.baseline->string());
                const auto b = nlohmann::json::parse(f);
                const double max_cer = b.at("max_cer").get<double>();
                const double min_recall = b.at("min_detection_recall").get<double>();
                if (s.cer > max_cer || recall < min_recall) {
                    std::fprintf(stderr, "FAIL: CER %.4f (max %.4f), detection recall %.3f (min %.3f)\n", s.cer,
                                 max_cer, recall, min_recall);
                    exit_code = 1;
                }
            }
        }
        if (args.json) std::ofstream(*args.json) << out.dump(2) << '\n';
        return exit_code;
    } catch (const std::exception& e) {
        std::cerr << "kz4ap-bench: " << e.what() << '\n';
        return 2;
    }
}
```

- [ ] **Step 4: Build and run — expect pass**

```powershell
cmake --build --preset windows
ctest --preset windows
```
Expected: all tests pass.

- [ ] **Step 5: End-to-end run on a synthetic recording**

```powershell
$env:PYTHONPATH = "training"
.venv\Scripts\python -m kz4ap_synth.generate --scenario single --duration 10 --out build/synth/single.wav
build\windows\bench\Release\kz4ap-bench.exe build/synth/single.wav --labels build/synth/single.json
```
Expected: one track near +1000 Hz whose text contains `CQ TEST K1ABC K1ABC` apart from the first second or so, which is lost while the track is being found. The label line shows the signal as detected and the CER is below 0.2. Also check that a bad path prints an error and exits with code 2:
```powershell
build\windows\bench\Release\kz4ap-bench.exe nope.wav; $LASTEXITCODE
```
Expected output: `kz4ap-bench: cannot open nope.wav`, then `2`.

- [ ] **Step 6: Commit on the `milestone-1` branch**

```powershell
git add CMakeLists.txt bench
```
Commit message: `Add kz4ap-bench benchmark tool`

---

### Task 10: Continuous integration with benchmark smoke test

**Files:**
- Create: `bench/smoke.sh`, `bench/baselines/smoke.json`, `.github/workflows/ci.yml`
- Modify: `README.md` (add a "Benchmark" section)

**Interfaces:**
- Consumes: the `kz4ap-bench` CLI and exit codes (Task 9), the generator CLI (Task 2), the presets (Task 1).

- [ ] **Step 1: Smoke script**

`bench/smoke.sh`:
```bash
#!/usr/bin/env bash
# Benchmark smoke test: generate a synthetic band, score it against the stored
# baseline, and check that two runs produce identical results.
# Usage: bench/smoke.sh BUILD_DIR   (run from the repository root)
set -euo pipefail

BUILD_DIR="$1"
PYTHON="${PYTHON:-python}"
BENCH=$(find "$BUILD_DIR" -type f \( -name kz4ap-bench -o -name kz4ap-bench.exe \) | head -n 1)
if [ -z "$BENCH" ]; then
    echo "kz4ap-bench not found under $BUILD_DIR" >&2
    exit 2
fi

WORK="$BUILD_DIR/smoke"
mkdir -p "$WORK"
PYTHONPATH=training "$PYTHON" -m kz4ap_synth.generate --scenario band --signals 8 \
    --duration 30 --seed 1 --out "$WORK/band.wav"

"$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/run1.json" \
    --no-timing --baseline bench/baselines/smoke.json
"$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/run2.json" --no-timing
if ! cmp -s "$WORK/run1.json" "$WORK/run2.json"; then
    echo "FAIL: two runs over the same recording produced different results" >&2
    exit 1
fi
echo "smoke test passed"
```

- [ ] **Step 2: Measure and record the baseline**

Start from a baseline that cannot fail, so the first run only measures. `bench/baselines/smoke.json`:
```json
{"max_cer": 1.0, "min_detection_recall": 0.0}
```

Run (from Git Bash, since the script is bash):
```bash
PYTHON=.venv/Scripts/python bash bench/smoke.sh build/windows
```
Expected: `smoke test passed`, with a line such as `CER 0.0xxx, detected 8 of 8, 0 false tracks`.

Then set the baseline from the measurement. `max_cer` is the observed CER plus 0.05, rounded up to two decimals. `min_detection_recall` is the observed recall minus 0.125 (one signal of eight). For example, an observed CER of 0.061 with 8 of 8 detected gives:
```json
{"max_cer": 0.12, "min_detection_recall": 0.875}
```
Rerun the script to confirm it still passes against the new baseline. If the observed CER is above 0.3, stop and investigate with superpowers:systematic-debugging before recording a baseline, because that means the pipeline is broken, not merely weak.

- [ ] **Step 3: Workflow**

`.github/workflows/ci.yml`:
```yaml
name: CI

on:
  push:
  pull_request:

jobs:
  build-and-test:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: windows-latest
            preset: windows
          - os: ubuntu-latest
            preset: linux
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v7

      - uses: actions/setup-python@v7
        with:
          python-version: "3.12"

      - name: Install Ninja
        if: runner.os == 'Linux'
        run: sudo apt-get update && sudo apt-get install -y ninja-build

      - name: Python dependencies
        run: python -m pip install -r training/requirements.txt

      - name: Python tests
        run: python -m pytest training -q

      - name: Configure
        run: cmake --preset ${{ matrix.preset }}

      - name: Build
        run: cmake --build --preset ${{ matrix.preset }}

      - name: C++ tests
        run: ctest --preset ${{ matrix.preset }}

      - name: Benchmark smoke test
        shell: bash
        run: bash bench/smoke.sh build/${{ matrix.preset }}
```

- [ ] **Step 4: README section**

Append to `README.md`:
```markdown
## Benchmark

`kz4ap-bench` runs the engine over an I/Q recording and, given the labels file
the synthetic generator writes, scores the decoded text:

    python -m venv .venv
    .venv\Scripts\python -m pip install -r training/requirements.txt
    $env:PYTHONPATH = "training"
    .venv\Scripts\python -m kz4ap_synth.generate --scenario band --signals 8 --out build/synth/band.wav
    build\windows\bench\Release\kz4ap-bench.exe build/synth/band.wav --labels build/synth/band.json

Every push runs the same check on Windows and Linux (`bench/smoke.sh`) and fails
if decoding gets worse than `bench/baselines/smoke.json`.
```

- [ ] **Step 5: Commit on the `milestone-1` branch**

```powershell
git add bench/smoke.sh bench/baselines/smoke.json .github README.md
```
Commit message: `Add CI workflow with benchmark smoke test`

- [ ] **Step 6: Push and watch CI (only with the user's approval to push)**

After the user approves the push, run `git push`. Then watch with `gh run watch` or on the repository's Actions tab. If the Linux job fails where Windows passed, suspect compiler differences (warnings treated differently, a missing `#include`) before suspecting the algorithm.
