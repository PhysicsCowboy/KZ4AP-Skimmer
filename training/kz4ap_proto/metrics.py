"""Measurements on the prototype's decoded files (proto/<name>/<result>.decoded.json) against their labels,
for the report (Task 12) and the experiments (Tasks 13-14). Intervals: bootstrap 95% over channels."""

from __future__ import annotations

import json
import math
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

from kz4ap_synth.suites import BOOTSTRAP_RESAMPLES, _interval, _rng_for

from .periodicity import Periodicity
from .power import disable_power_throttling
from .runner import oracle_scorings

PERIODICITY_GROUPS = ("A sensitivity", "B fading", "C fists", "G ragchew", "H two-station QSO, oracle (per station)",
                      "I Farnsworth")


def iter_channels(out_dir, name: str, only: str | None = None):
    """(job, label, decoded channel, decoded config) for every decoded channel."""
    out_dir = Path(out_dir)
    for job in oracle_scorings(out_dir, only):
        path = out_dir / "proto" / name / f"{job['result']}.decoded.json"
        if not path.exists():
            continue
        decoded = json.loads(path.read_text(encoding="utf-8"))
        labels = json.loads((out_dir / job["labels"]).read_text(encoding="utf-8"))["signals"]
        for ch in decoded["channels"]:
            yield job, labels[ch["label_index"]], ch, decoded["config"]


def true_dit_s(label: dict) -> float | None:
    """The label's dit, 1.2 s / WPM, if its speed is constant (no wpm_end; a QSO only if all senders share it)."""
    if label.get("wpm_end") is not None:
        return None
    senders = label.get("senders") or []
    if senders and len({s["wpm"] for s in senders}) > 1:
        return None
    return 1.2 / label["wpm"]


def transmissions(label: dict, pad_s: float = 0.0) -> list[tuple[float, float]]:
    txs = label.get("transmissions") or [{"start_s": label["start_s"], "end_s": label["end_s"]}]
    return [(t["start_s"] - pad_s, t["end_s"] + pad_s) for t in txs]


def bootstrap_ratio(units, key):
    """95% interval of sum(numerators) / sum(denominators), resampling units (channels)."""
    if len(units) < 2:
        return None
    num = np.array([u[0] for u in units], float)
    den = np.array([u[1] for u in units], float)
    picks = _rng_for(key).integers(len(units), size=(BOOTSTRAP_RESAMPLES, len(units)))
    return _interval([num[p].sum() / den[p].sum() if den[p].sum() > 0 else None for p in picks])


def speed_errors(out_dir, name, only=None, min_snr_db=6.0, factor=1.5, settle_s=3.0, run_s=3.0) -> dict:
    """Per group, scored constant-speed labels at S500 >= min_snr_db: the fraction of selection instants inside a
    transmission (from settle_s after its start) whose selected dit is off the label's by more than `factor`
    (no fit counts as off), and lock-ins: transmissions with such instants for run_s or longer in a row."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        truth = true_dit_s(label)
        if truth is None or not label.get("score", True) or label["snr_db"] < min_snr_db:
            continue
        g = groups.setdefault(job["group"], {"units": [], "lock_ins": 0, "transmissions": 0})
        bad = total = 0
        for a, b in transmissions(label):
            g["transmissions"] += 1
            run_start, locked = None, False
            for t, _, T in ch["selections"]:
                if not a + settle_s <= t <= b:
                    continue
                total += 1
                off = T is None or abs(math.log(T / truth)) > math.log(factor)
                bad += off
                if off:
                    run_start = t if run_start is None else run_start
                    locked = locked or t - run_start >= run_s
                else:
                    run_start = None
            g["lock_ins"] += locked
        g["units"].append((bad, total))
    out = {}
    for grp, g in groups.items():
        bad, total = sum(u[0] for u in g["units"]), sum(u[1] for u in g["units"])
        out[grp] = {"instants": total, "error_fraction": bad / total if total else None,
                    "interval": bootstrap_ratio(g["units"], ("speed", grp)), "lock_ins": g["lock_ins"],
                    "transmissions": g["transmissions"]}
    return out


def switch_stats(out_dir, name, only=None, back_within_s=5.0) -> dict:
    """Per group, scored labels: branch switches and alternations (a switch straight back within back_within_s),
    per minute of transmission time."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"switches": 0, "alternations": 0, "minutes": 0.0})
        g["minutes"] += sum(b - a for a, b in transmissions(label)) / 60.0
        last = None  # (t, from, to)
        sel = ch["selections"]
        for (_, k0, _), (t1, k1, _) in zip(sel, sel[1:]):
            if k1 == k0:
                continue
            g["switches"] += 1
            if last is not None and last[1] == k1 and last[2] == k0 and t1 - last[0] <= back_within_s:
                g["alternations"] += 1
            last = (t1, k0, k1)
    return {grp: {**g, "switches_per_min": g["switches"] / g["minutes"] if g["minutes"] else None,
                  "alternations_per_min": g["alternations"] / g["minutes"] if g["minutes"] else None}
            for grp, g in groups.items()}


def spurious_over_starts(out_dir, name, only=None, settle_s=1.0) -> dict:
    """Per group, scored labels: over starts of the selected branch inside a transmission (from settle_s after
    its start; a real over starts in the silence before it), per transmission."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"over_starts": 0, "transmissions": 0})
        txs = transmissions(label)
        g["transmissions"] += len(txs)
        g["over_starts"] += sum(1 for t in ch["over_starts"] if any(a + settle_s <= t <= b for a, b in txs))
    return {grp: {**g, "per_transmission": g["over_starts"] / g["transmissions"] if g["transmissions"] else None}
            for grp, g in groups.items()}


def false_characters(out_dir, name, only=None, pad_s=0.5) -> dict:
    """Per group, scored labels: final characters (not word spaces) that start outside every transmission
    (padded by pad_s), per minute outside transmissions."""
    groups: dict = {}
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if not label.get("score", True):
            continue
        g = groups.setdefault(job["group"], {"characters": 0, "minutes": 0.0})
        txs = [(max(0.0, a), min(ch["channel_s"], b)) for a, b in transmissions(label, pad_s)]
        g["minutes"] += (ch["channel_s"] - sum(max(0.0, b - a) for a, b in txs)) / 60.0
        g["characters"] += sum(1 for text, start, _ in ch["chars"]
                               if text != " " and not any(a <= start <= b for a, b in txs))
    return {grp: {**g, "per_min": g["characters"] / g["minutes"] if g["minutes"] else None}
            for grp, g in groups.items()}


def cpu_per_channel_second(out_dir, name, only=None) -> float:
    """Decoding CPU time per channel-second, s/s."""
    cpu = seconds = 0.0
    for _, _, ch, _ in iter_channels(out_dir, name, only):
        cpu += ch["cpu_s"]
        seconds += ch["channel_s"]
    return cpu / seconds if seconds else 0.0


def _points_of(work) -> list[dict]:
    p1_path, cfg, rate, label, unit, group = work
    truth = true_dit_s(label)
    p1 = np.load(p1_path)
    per = Periodicity(cfg, rate)
    txs = transmissions(label)
    points = []
    for i in range(0, len(p1), per.update_every):
        per.push(p1[i:i + per.update_every])
        t = min(i + per.update_every, len(p1)) / rate
        inside = [k for k, (a, b) in enumerate(txs) if a <= t <= b]
        if not inside:
            continue
        per.update(force=True)
        points.append({"group": group, "unit": unit, "tx": inside[0], "truth": truth,
                       "since_start": t - txs[inside[0]][0], "per": list(per.per_window)})
    return points


def periodicity_points(out_dir, name, cfg, only=None, groups=PERIODICITY_GROUPS, min_snr_db=0.0, jobs=None) -> list:
    """Runs cfg's periodicity estimator offline on the stored branch-1 posteriors (decode --keep-p1), every
    window logged: one point per update inside a transmission of a scored, constant-speed label at
    S500 >= min_snr_db in the given groups. Exact: T_P never feeds back into branch 1's posterior."""
    out_dir = Path(out_dir)
    work = []
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if job["group"] not in groups or true_dit_s(label) is None or not label.get("score", True):
            continue
        if label["snr_db"] < min_snr_db:
            continue
        p1 = out_dir / "proto" / name / "p1" / job["result"] / f"{ch['label_index']}.npy"
        work.append((str(p1), cfg, ch["rate_hz"], label, (job["result"], ch["label_index"]), job["group"]))
    disable_power_throttling()
    with ProcessPoolExecutor(max_workers=jobs, initializer=disable_power_throttling) as pool:
        return [p for points in pool.map(_points_of, work, chunksize=1) for p in points]


def periodicity_points_decoded(out_dir, name, only=None, groups=PERIODICITY_GROUPS, min_snr_db=0.0) -> list:
    """The decoder's own periodicity records (Plan B, B4a), one point per recorded recomputation inside a
    transmission of a scored, constant-speed label at S500 >= min_snr_db in the given groups, with every window's
    (T, score) as the decoded file records it (scores rounded to 4 decimals). The same points as
    periodicity_points, but read from what the decoder computed (its windows, whatever their form) rather than
    recomputed from stored posteriors; "per" lists the windows shortest first."""
    points = []
    for job, label, ch, _ in iter_channels(out_dir, name, only):
        if job["group"] not in groups or true_dit_s(label) is None or not label.get("score", True):
            continue
        if label["snr_db"] < min_snr_db:
            continue
        txs = transmissions(label)
        unit = (job["result"], ch["label_index"])
        for t, _, _, _, per in ch["periodicity"]:
            inside = [k for k, (a, b) in enumerate(txs) if a <= t <= b]
            if inside:
                points.append({"group": job["group"], "unit": unit, "tx": inside[0], "truth": true_dit_s(label),
                               "since_start": t - txs[inside[0]][0], "per": [tuple(w) for w in per]})
    return points


def evaluate_rule(points, windows_s, subset, threshold: float, tolerance: float = math.log(1.05)) -> dict:
    """The rule "the confident estimate (score >= threshold) with the shortest window in subset": precision
    (confident estimates within 5% of the true dit), coverage (points with a confident estimate), and the median
    time from a transmission's start to its first confident estimate, s."""
    order = [list(windows_s).index(w) for w in sorted(subset)]
    units: dict = {}
    firsts: dict = {}
    for p in points:
        u = units.setdefault(p["unit"], [0, 0, 0])  # points, confident, correct
        u[0] += 1
        est = next((p["per"][i] for i in order if p["per"][i][0] is not None and p["per"][i][1] >= threshold), None)
        if est is None:
            continue
        u[1] += 1
        u[2] += abs(math.log(est[0] / p["truth"])) <= tolerance
        firsts.setdefault((p["unit"], p["tx"]), p["since_start"])
    total = sum(u[0] for u in units.values())
    confident = sum(u[1] for u in units.values())
    correct = sum(u[2] for u in units.values())
    key = (tuple(sorted(subset)), round(threshold, 6))
    return {"points": total, "confident": confident,
            "precision": correct / confident if confident else None,
            "precision_interval": bootstrap_ratio([(u[2], u[1]) for u in units.values()], ("precision",) + key),
            "coverage": confident / total if total else None,
            "coverage_interval": bootstrap_ratio([(u[1], u[0]) for u in units.values()], ("coverage",) + key),
            "median_time_to_confident_s": float(np.median(list(firsts.values()))) if firsts else None}


def calibrate(points, windows_s, subset, target: float = 0.95):
    """(threshold, evaluate_rule result): the lowest of 100 quantiles of the scores seen at which the rule's
    precision reaches target; (None, None) if none does."""
    scores = [s for p in points for t, s in p["per"] if t is not None]
    if not scores:
        return None, None
    for threshold in np.unique(np.quantile(scores, np.linspace(0.0, 0.99, 100))):
        r = evaluate_rule(points, windows_s, subset, float(threshold))
        if r["precision"] is not None and r["precision"] >= target:
            return float(threshold), r
    return None, None
