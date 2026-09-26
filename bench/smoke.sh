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
