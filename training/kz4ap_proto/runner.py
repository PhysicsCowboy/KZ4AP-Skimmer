"""Stage 1 runner (plan Task 12): record the suite's oracle channel streams with kz4ap-bench, decode them with
the prototype, score the decoded text with kz4ap-bench --score-decoded, and report against the engine's
results in the suite's results/ directory.

    python -m kz4ap_proto.runner record --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX]
    python -m kz4ap_proto.runner engine-copies --out build/suite/full3 --bench PATH/kz4ap-bench [--only REGEX]
    python -m kz4ap_proto.runner decode --out build/suite/full3 [--name bank-proto] [--set KEY=VALUE ...] [--only REGEX] [--jobs N]
    python -m kz4ap_proto.runner score --out build/suite/full3 --bench PATH/kz4ap-bench [--name bank-proto] [--only REGEX]
    python -m kz4ap_proto.runner report --out build/suite/full3 [--name bank-proto] [--only REGEX] [--suffix TEXT]
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import os
import re
import subprocess
import time
import traceback
from dataclasses import asdict
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import BENCH_FRONT_END, _scorings, oracle_copies

from .channel import ChannelDecoder
from .params import ProtoConfig
from .power import disable_power_throttling
from .streams import detector_channel, load_channel, read_manifest

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
    (the Matched path's detector: --front-end matched, no --oracle)."""
    out_dir = Path(out_dir)
    for job in detector_jobs(out_dir, only):
        target = out_dir / "channels" / job["result"]
        if (target / "channels.json").exists():
            continue
        cmd = [str(bench), str(out_dir / job["wav"]), "--labels", str(out_dir / job["labels"]),
               "--front-end", "matched", "--no-timing", "--record-channels", str(target),
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
               "--front-end", "envelope", "--no-timing", "--record-channels", str(target),
               "--json", str(target / "bench.json")]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            raise RuntimeError(f"kz4ap-bench failed recording {job['result']}:\n{done.stderr}")
        print(f"recorded {job['result']}")


def score_engine_copies(out_dir: Path, bench: Path, front_ends=("baseline", "matched"), only: str | None = None) -> None:
    """Scores the oracle copies of the detector-only groups with the engine's own front ends
    (kz4ap-bench --oracle --front-end envelope|matched) into results/<front end>/<name>.oracle.json, so the
    prototype's rows there compare like for like. Existing results are kept."""
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
                       "--front-end", BENCH_FRONT_END[fe], "--json", str(target)]
                done = subprocess.run(cmd, capture_output=True, text=True)
                if done.returncode != 0:
                    raise RuntimeError(f"kz4ap-bench failed on {result} ({fe}):\n{done.stderr}")
                lines = done.stdout.strip().splitlines()
                print(f"{fe:9s} {result}: {lines[-1] if lines else ''}")


class ChannelFailed(RuntimeError):
    """A channel's decode failed in a worker; the message names the recording directory and channel position."""


def _decode_one(work) -> tuple:
    key, record_dir, labels_path, position, values, p1_path, detector = work
    try:
        cfg = ProtoConfig().with_values(**values)
        # detector (rate Hz, channel entry of the manifest parsed once): a detector channel, decoded from its
        # opening, mixed by the detector's frequency (Task 11b)
        ch = (load_channel(record_dir, labels_path, position) if detector is None
              else detector_channel(record_dir, *detector))
        started = time.process_time()
        result = ChannelDecoder(cfg, ch.rate_hz).run(ch.baseband(), keep_p1=p1_path is not None)
        out = result.to_json()
        out.update(label_index=ch.label_index, rate_hz=ch.rate_hz, cpu_s=time.process_time() - started,
                   channel_s=len(ch.y) / ch.rate_hz)
        if detector is not None:
            out.update(track_id=ch.track_id, birth_freq_hz=ch.birth_freq_hz, last_freq_hz=ch.anchors[-1][1],
                       open_s=ch.open_s, close_s=ch.close_s)
        if p1_path is not None:
            Path(p1_path).parent.mkdir(parents=True, exist_ok=True)
            np.save(p1_path, result.p1)
    except Exception as e:  # the worker's traceback does not survive the trip to the main process: keep its text
        raise ChannelFailed(f"decoding {record_dir}, channel position {position} failed: {e!r}\n"
                            f"{traceback.format_exc()}") from None
    return key, out


def _config_json(cfg) -> dict:
    """The config as a decoded file stores it (JSON: tuples become lists)."""
    return json.loads(json.dumps(asdict(cfg)))


def _is_done(path: Path, cfg_json: dict, p1_paths) -> bool:
    """A decoded file exists, was decoded with this config, and (with keep_p1) its posteriors exist."""
    if not path.exists():
        return False
    try:
        same = json.loads(path.read_text()).get("config") == cfg_json
    except json.JSONDecodeError:  # unreadable: decode it again
        return False
    return same and all(Path(p).exists() for p in p1_paths)


def _write_decoded(target: Path, name: str, cfg_json: dict, job: dict, channels: list) -> None:
    common = {"front_end": name, "recording": job["wav"], "labels": job["labels"], "config": cfg_json}
    if "scorings" in job:  # detector channels: the "tracks" form, scored by frequency (Task 11b)
        channels.sort(key=lambda c: c["track_id"])
        tracks = [{"id": c["track_id"], "freq_hz": c["birth_freq_hz"], "last_freq_hz": c["last_freq_hz"],
                   "text": c["text"]} for c in channels]
        body = {**common, "tracks": tracks, "channels": channels}
    else:
        channels.sort(key=lambda c: c["label_index"])
        body = {**common, "texts": [c["text"] for c in channels], "channels": channels}
    path = target / f"{job['result']}.decoded.json"
    partial = path.with_name(path.name + ".partial")
    partial.write_text(json.dumps(body) + "\n")
    os.replace(partial, path)  # a crash mid-write leaves no complete-looking decoded file
    print(f"decoded {job['result']}", flush=True)


def decode(out_dir: Path, name: str = DEFAULT_NAME, values: dict | None = None, only: str | None = None,
           jobs: int | None = None, keep_p1: bool = False) -> None:
    """Decodes every recorded channel of the matching oracle scorings and detector jobs with
    ProtoConfig().with_values(**values), in worker processes (each opted out of Windows power throttling).

    Built for multi-hour runs: results are taken as they complete; each recording's decoded file is written as soon
    as its last channel arrives, and its channels are then dropped from memory; a recording whose decoded file
    already exists with an equal config (and, with keep_p1, its posteriors) is skipped, so an interrupted run
    resumes. A failing channel raises ChannelFailed naming its recording directory and position; recordings
    finished before it keep their files."""
    out_dir = Path(out_dir)
    values = values or {}
    cfg = ProtoConfig().with_values(**values)  # validates the names before any work starts
    cfg_json = _config_json(cfg)
    target = out_dir / "proto" / name
    target.mkdir(parents=True, exist_ok=True)
    work, pending, skipped = [], {}, 0
    for job in oracle_scorings(out_dir, only):
        record_dir = out_dir / "channels" / job["result"]
        count = len(read_manifest(record_dir)["channels"])
        p1s = [str(target / "p1" / job["result"] / f"{i}.npy") if keep_p1 else None for i in range(count)]
        if _is_done(target / f"{job['result']}.decoded.json", cfg_json, [p for p in p1s if p]):
            skipped += 1
            continue
        pending[job["result"]] = [job, count, []]
        work += [(job["result"], str(record_dir), str(out_dir / job["labels"]), i, values, p1s[i], None)
                 for i in range(count)]
    for job in detector_jobs(out_dir, only):
        record_dir = out_dir / "channels" / job["result"]
        if _is_done(target / f"{job['result']}.decoded.json", cfg_json, []):
            skipped += 1
            continue
        manifest = read_manifest(record_dir)  # once per recording; each worker gets its channel's entry
        rate_hz = float(manifest["sample_rate_hz"])
        pending[job["result"]] = [job, len(manifest["channels"]), []]
        work += [(job["result"], str(record_dir), None, i, values, None, (rate_hz, entry))
                 for i, entry in enumerate(manifest["channels"])]
    if skipped:
        print(f"skipped {skipped} recordings already decoded with this config", flush=True)
    # Every job gets a decoded file, even one with no channel (a detector that opened none): the bench then scores
    # its labels as not detected, as it does the engine's.
    for key in [k for k, (_, count, _) in pending.items() if count == 0]:
        job, _, channels = pending.pop(key)
        _write_decoded(target, name, cfg_json, job, channels)
    if not work:
        return
    disable_power_throttling()
    workers = jobs or max(1, (os.cpu_count() or 2) - 2)
    with multiprocessing.get_context("spawn").Pool(workers, initializer=disable_power_throttling) as pool:
        for key, channel in pool.imap_unordered(_decode_one, work, chunksize=1):
            entry = pending[key]
            entry[2].append(channel)
            if len(entry[2]) == entry[1]:
                job, _, channels = pending.pop(key)
                _write_decoded(target, name, cfg_json, job, channels)


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


def parse_values(pairs) -> dict:
    """--set KEY=VALUE pairs; VALUE is JSON if it parses (numbers, lists), else a string."""
    values = {}
    for pair in pairs or []:
        key, _, text = pair.partition("=")
        try:
            values[key] = json.loads(text)
        except json.JSONDecodeError:
            values[key] = text
    return values


def main(argv=None) -> None:
    disable_power_throttling()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("record", "engine-copies", "decode", "score", "report"):
        p = sub.add_parser(command)
        p.add_argument("--out", type=Path, required=True)
        p.add_argument("--only", default=None, help="regular expression on result names")
        if command in ("record", "engine-copies", "score"):
            p.add_argument("--bench", type=Path, required=True)
        if command not in ("record", "engine-copies"):
            p.add_argument("--name", default=DEFAULT_NAME)
        if command == "decode":
            p.add_argument("--set", dest="values", action="append", help="KEY=VALUE: a ProtoConfig value")
            p.add_argument("--jobs", type=int, default=None)
            p.add_argument("--keep-p1", action="store_true")
        if command == "report":
            p.add_argument("--suffix", default="")
    args = parser.parse_args(argv)
    if args.command == "record":
        record(args.out, args.bench, args.only)
    elif args.command == "engine-copies":
        score_engine_copies(args.out, args.bench, only=args.only)
    elif args.command == "decode":
        decode(args.out, args.name, parse_values(args.values), args.only, args.jobs, args.keep_p1)
    elif args.command == "score":
        score(args.out, args.bench, args.name, args.only)
    else:
        from .report import write_report
        print(f"wrote {write_report(args.out, args.name, only=args.only, suffix=args.suffix)}")


if __name__ == "__main__":
    main()
