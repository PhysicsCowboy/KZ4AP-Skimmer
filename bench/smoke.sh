#!/usr/bin/env bash
# Benchmark smoke test: generate a synthetic band, score it against the stored
# baselines on the milestone-1 (Envelope) path, which must stay bit-identical, and on the
# Matched path (the default), and check that two runs of each produce identical results.
# Usage: bench/smoke.sh BUILD_DIR   (run from the repository root)
set -euo pipefail

BUILD_DIR="$1"
PYTHON="${PYTHON:-python}"

# Multi-config generators (Windows) put both a Debug and a Release binary
# under BUILD_DIR; single-config generators (the Linux CI layout, Ninja with
# CMAKE_BUILD_TYPE=Release) put just one. Prefer a uniquely-identified
# Release binary; otherwise, if there is exactly one candidate at all, use
# it; otherwise the choice is ambiguous and this fails rather than silently
# picking a stale binary (`find | head -n 1` used to pick whichever sorted
# first, which could be an old Debug build).
CANDIDATES=$(find "$BUILD_DIR" -type f \( -name kz4ap-bench -o -name kz4ap-bench.exe \))
if [ -z "$CANDIDATES" ]; then
    echo "kz4ap-bench not found under $BUILD_DIR" >&2
    exit 2
fi
RELEASE_CANDIDATES=$(printf '%s\n' "$CANDIDATES" | grep '/Release/' || true)
TOTAL_COUNT=$(printf '%s\n' "$CANDIDATES" | wc -l | tr -d ' ')
if [ -z "$RELEASE_CANDIDATES" ]; then
    RELEASE_COUNT=0
else
    RELEASE_COUNT=$(printf '%s\n' "$RELEASE_CANDIDATES" | wc -l | tr -d ' ')
fi
if [ "$RELEASE_COUNT" -eq 1 ]; then
    BENCH="$RELEASE_CANDIDATES"
elif [ "$TOTAL_COUNT" -eq 1 ]; then
    BENCH="$CANDIDATES"
else
    echo "kz4ap-bench: ambiguous binary under $BUILD_DIR (no single Release build, and more than one candidate):" >&2
    printf '%s\n' "$CANDIDATES" >&2
    exit 2
fi

WORK="$BUILD_DIR/smoke"
mkdir -p "$WORK"
PYTHONPATH=training "$PYTHON" -m kz4ap_synth.generate --scenario band --signals 8 \
    --duration 30 --seed 1 --out "$WORK/band.wav"

check() {  # check NAME BASELINE [bench options...]
    local name="$1" baseline="$2"
    shift 2
    "$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/$name-1.json" \
        --no-timing --baseline "$baseline" "$@"
    "$BENCH" "$WORK/band.wav" --labels "$WORK/band.json" --json "$WORK/$name-2.json" --no-timing "$@"
    if ! cmp -s "$WORK/$name-1.json" "$WORK/$name-2.json"; then
        echo "FAIL: two $name runs over the same recording produced different results" >&2
        exit 1
    fi
}

check baseline bench/baselines/smoke.json --front-end envelope
check matched bench/baselines/smoke-matched.json --front-end matched
echo "smoke test passed"
