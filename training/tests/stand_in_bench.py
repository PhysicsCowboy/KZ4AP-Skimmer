"""A stand-in for kz4ap-bench, for the tests of the suite runner (kz4ap_synth.suites.run_suite) and of the search errors
(kz4ap_proto.experiments.search_errors). It takes the options those hand the bench and writes the files they read. It
decodes nothing and it does not align: the edits it reports for --score-decoded are the symbols that differ at the same
position plus the difference in length, which is the edit distance for substitutions and for a changed tail (all the
tests use); the real bench's alignment is tested in bench/tests/scoring_test.cpp.

Environment (all optional):
  STAND_IN_BENCH_DIR       a folder: each run leaves started-<pid> when it starts, done-<pid> when it ends, and
                           events-<pid>.json with its start and end times, s
  STAND_IN_BENCH_WAIT_FOR  a number n: a run does not finish until n runs have started (10 s at most, then it exits
                           with status 3), which can only happen if n runs are alive at once
  STAND_IN_BENCH_FAIL      text: a run on a recording whose name contains it exits with status 2
"""

import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

WAIT_LIMIT_S = 10.0
COUNTS = ("symbols", "edits", "chars", "char_edits", "spaces", "space_edits", "first_word_symbols",
          "first_word_edits", "nospace_symbols", "nospace_edits")


def launcher(directory) -> Path:
    """An executable named kz4ap-bench in `directory` that runs this script with the Python that runs the tests (a
    .cmd file on Windows, which cannot run a script directly)."""
    directory = Path(directory)
    script = Path(__file__).resolve()
    if os.name == "nt":
        path = directory / "kz4ap-bench.cmd"
        path.write_text(f'@"{sys.executable}" "{script}" %*\n')
    else:
        path = directory / "kz4ap-bench"
        path.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{script}" "$@"\n')
        path.chmod(0o755)
    return path


def symbols(text: str) -> list[str]:
    """A text's symbols as the scorer counts them: a prosign token such as <BT> is one, a word space is one."""
    return re.findall(r"<[^<>\s]+>|.", text)


def score_decoded(args) -> dict:
    labels = json.loads(Path(args.labels).read_text())["signals"]
    texts = json.loads(Path(args.score_decoded).read_text())["texts"]
    signals = []
    for i, (label, text) in enumerate(zip(labels, texts)):
        reference, decoded = symbols(label["text"]), symbols(text)
        edits = sum(a != b for a, b in zip(reference, decoded)) + abs(len(reference) - len(decoded))
        spaces = reference.count(" ")
        signals.append({"index": i, "scored": label.get("score", True), "reference": label["text"], "decoded": text,
                        "symbols": len(reference), "spaces": spaces, "chars": len(reference) - spaces,
                        "edits": edits})
    return {"score": {"signals": signals}}


def run(args) -> dict:
    labels = json.loads(Path(args.labels).read_text())["signals"]
    signals = [{"index": i, "snr_db": label["snr_db"], "scored": label.get("score", True), "track_id": 1,
                "tracked_freq_hz": None, "decoded": "", **{k: 0 for k in COUNTS}} for i, label in enumerate(labels)]
    if args.stats:
        Path(args.stats).write_text(json.dumps({"stand_in": True}) + "\n")
    return {"recording": Path(args.recording).name, "decoder": args.decoder, "front_end": args.decoder,
            "oracle": args.oracle, "settings": args.set, "stats": args.stats, "channel_seconds": 1.0 * len(labels),
            "score": {"signals": signals}}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("recording", nargs="?")
    parser.add_argument("--labels")
    parser.add_argument("--json")
    parser.add_argument("--oracle", action="store_true")
    parser.add_argument("--no-timing", action="store_true")
    parser.add_argument("--decoder", default="matched")
    parser.add_argument("--set", action="append", default=[])
    parser.add_argument("--stats")
    parser.add_argument("--score-decoded")
    args = parser.parse_args(argv)
    folder = Path(os.environ["STAND_IN_BENCH_DIR"]) if "STAND_IN_BENCH_DIR" in os.environ else None
    started = time.time()
    if folder is not None:
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"started-{os.getpid()}").write_text("")
    fail = os.environ.get("STAND_IN_BENCH_FAIL")
    if fail and args.recording and fail in Path(args.recording).name:
        print(f"stand-in bench: told to fail on {args.recording}", file=sys.stderr)
        return 2
    wait_for = int(os.environ.get("STAND_IN_BENCH_WAIT_FOR", "0"))
    while folder is not None and len(list(folder.glob("started-*"))) < wait_for:
        if time.time() - started > WAIT_LIMIT_S:
            print(f"stand-in bench: {wait_for} runs never ran at once", file=sys.stderr)
            return 3
        time.sleep(0.01)
    out = score_decoded(args) if args.score_decoded else run(args)
    Path(args.json).write_text(json.dumps(out) + "\n")
    if folder is not None:
        (folder / f"events-{os.getpid()}.json").write_text(json.dumps({"start": started, "end": time.time()}))
        (folder / f"done-{os.getpid()}").write_text("")
    print("CER 0.0000")
    return 0


if __name__ == "__main__":
    sys.exit(main())
