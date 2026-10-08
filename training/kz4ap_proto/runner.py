"""Record a suite's channel streams with kz4ap-bench, score decoded files (proto/<name>/, as kz4ap-bank-replay
writes them for the oracle test cases) with kz4ap-bench --score-decoded, and report against the engine's results in
the suite's results/ directory. The stage-1 Python prototype's `decode` was removed with the prototype on 2026-10-07
(git history; engine/tests/data/bank/README.md).

    python -m kz4ap_proto.runner record --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX]
    python -m kz4ap_proto.runner engine-copies --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX] [--decoder baseline|matched|bank ...]
    python -m kz4ap_proto.runner score --out build/suite/full3 --bench PATH/kz4ap-bench [--name bank-proto] [--only REGEX]
    python -m kz4ap_proto.runner report --out build/suite/full3 [--name bank-proto] [--only REGEX] [--suffix TEXT]
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

from kz4ap_synth.suites import BENCH_FRONT_END, FRONT_ENDS, _scorings, oracle_copies

from .power import disable_power_throttling

DEFAULT_NAME = "bank-proto"


def oracle_scorings(out_dir: Path, only: str | None = None) -> list[dict]:
    """Every (recording, labels file) scored with oracle channels: the oracle recordings' scorings, and the oracle
    copies of the detector-only groups (suites.oracle_copies)."""
    manifest = json.loads((Path(out_dir) / "manifest.json").read_text())
    jobs = []
    for rec in manifest["recordings"]:
        for label_file, result, group in (_scorings(rec) if rec["oracle"] else oracle_copies(rec)):
            if only is None or re.search(only, result):
                jobs.append({"result": result, "group": group, "wav": rec["wav"], "labels": label_file})
    return jobs


def detector_jobs(out_dir: Path, only: str | None = None) -> list[dict]:
    """One job per non-oracle recording: its channels are the ones the Matched path's detector opens
    (kz4ap-bench --record-channels without --oracle; Task 11b), recorded once and scored under each of the
    recording's scorings (labels, and station labels where given), whose result names match the engine's own
    detector-path results."""
    manifest = json.loads((Path(out_dir) / "manifest.json").read_text())
    jobs = []
    for rec in manifest["recordings"]:
        if rec["oracle"]:
            continue
        scorings = [s for s in _scorings(rec) if only is None or re.search(only, s[1])]
        if scorings:
            jobs.append({"result": f"{rec['name']}.detector", "wav": rec["wav"], "labels": rec["labels"],
                         "scorings": scorings})
    return jobs


def record(out_dir: Path, bench: Path, only: str | None = None) -> None:
    """Records the oracle scorings' channels (--oracle) and the detector's channels of every non-oracle recording
    (the Matched path's detector: --decoder matched, no --oracle)."""
    out_dir = Path(out_dir)
    for job in detector_jobs(out_dir, only):
        target = out_dir / "channels" / job["result"]
        if (target / "channels.json").exists():
            continue
        cmd = [str(bench), str(out_dir / job["wav"]), "--labels", str(out_dir / job["labels"]),
               "--decoder", "matched", "--no-timing", "--record-channels", str(target),
               "--json", str(target / "bench.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed recording {job['result']}:\n{done.stderr}")
        print(f"recorded {job['result']}")
    for job in oracle_scorings(out_dir, only):
        target = out_dir / "channels" / job["result"]
        if (target / "channels.json").exists():
            continue
        cmd = [str(bench), str(out_dir / job["wav"]), "--labels", str(out_dir / job["labels"]), "--oracle",
               "--decoder", "envelope", "--no-timing", "--record-channels", str(target),
               "--json", str(target / "bench.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed recording {job['result']}:\n{done.stderr}")
        print(f"recorded {job['result']}")


def score_engine_copies(out_dir: Path, bench: Path, front_ends=("baseline", "matched"), only: str | None = None) -> None:
    """Scores the oracle copies of the detector-path groups with the engine's own decoders
    (kz4ap-bench --oracle --decoder envelope|matched|bank; front_ends names them by results folder: baseline,
    matched, bank) into results/<folder>/<name>.oracle.json, so the prototype's rows there compare like for like.
    Existing results are kept."""
    out_dir = Path(out_dir)
    manifest = json.loads((out_dir / "manifest.json").read_text())
    for rec in manifest["recordings"]:
        for label_file, result, _ in oracle_copies(rec):
            if only is not None and not re.search(only, result):
                continue
            for fe in front_ends:
                target = out_dir / "results" / fe / f"{result}.json"
                if target.exists():
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                cmd = [str(bench), str(out_dir / rec["wav"]), "--labels", str(out_dir / label_file), "--oracle",
                       "--decoder", BENCH_FRONT_END[fe], "--json", str(target)]
                done = subprocess.run(cmd, capture_output=True, text=True)
                if done.returncode != 0:
                    raise RuntimeError(f"kz4ap-bench failed on {result} ({fe}):\n{done.stderr}")
                lines = done.stdout.strip().splitlines()
                print(f"{fe:9s} {result}: {lines[-1] if lines else ''}")


def score(out_dir: Path, bench: Path, name: str = DEFAULT_NAME, only: str | None = None,
          results_root: Path | None = None) -> None:
    """Scores every decoded file with kz4ap-bench --score-decoded into results_root/name (default
    out_dir/results/name), and prints how many scorings and labels had a decoded file out of those available."""
    out_dir = Path(out_dir)
    results = Path(results_root or out_dir / "results") / name
    results.mkdir(parents=True, exist_ok=True)
    scorings = [(job["result"], job["labels"], job["result"]) for job in oracle_scorings(out_dir, only)]
    scorings += [(job["result"], label_file, result) for job in detector_jobs(out_dir, only)
                 for label_file, result, _ in job["scorings"]]
    scored = labels_scored = labels_available = 0
    for decoded_name, label_file, result in scorings:
        decoded = out_dir / "proto" / name / f"{decoded_name}.decoded.json"
        labels = _label_count(out_dir / label_file)
        labels_available += labels
        if not decoded.exists():
            continue
        cmd = [str(bench), "--labels", str(out_dir / label_file), "--score-decoded", str(decoded),
               "--json", str(results / f"{result}.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed scoring {result} ({name}):\n{done.stderr}")
        scored += 1
        labels_scored += labels
        lines = done.stdout.strip().splitlines()
        print(f"{name} {result}: {lines[-1] if lines else ''}")
    print(f"{name}: scored {scored} of {len(scorings)} test cases ({labels_scored} of {labels_available} labels) "
          "that had a decoded file")


def _label_count(path: Path) -> int:
    """Labels in a labels file (0 if it is missing)."""
    return len(json.loads(Path(path).read_text())["signals"]) if Path(path).exists() else 0


def main(argv=None) -> None:
    disable_power_throttling()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("record", "engine-copies", "score", "report"):
        p = sub.add_parser(command)
        p.add_argument("--out", type=Path, required=True)
        p.add_argument("--only", default=None, help="regular expression on result names")
        if command in ("record", "engine-copies", "score"):
            p.add_argument("--bench", type=Path, required=True)
        if command not in ("record", "engine-copies"):
            p.add_argument("--name", default=DEFAULT_NAME)
        if command == "report":
            p.add_argument("--suffix", default="")
        if command == "engine-copies":
            p.add_argument("--decoder", dest="front_ends", action="append", choices=FRONT_ENDS,
                           help="the engine's decoder by results folder (repeatable; default baseline and matched)")
    args = parser.parse_args(argv)
    if args.command == "record":
        record(args.out, args.bench, args.only)
    elif args.command == "engine-copies":
        score_engine_copies(args.out, args.bench, front_ends=tuple(args.front_ends or ("baseline", "matched")),
                            only=args.only)
    elif args.command == "score":
        score(args.out, args.bench, args.name, args.only)
    else:
        from .report import write_report
        print(f"wrote {write_report(args.out, args.name, only=args.only, suffix=args.suffix)}")


if __name__ == "__main__":
    main()
