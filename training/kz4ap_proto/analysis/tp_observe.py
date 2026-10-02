"""The speed estimate T_P offline on exp-ref's stored branch-1 posteriors: the comb's thresholds for precision 0.85
and 0.90 and the rule at each; the rule inside groups D, E, F; confident T_P outside transmissions per group.
Observation/analysis only (not a decision rule; owner's request 2026-10-01). Feeds results section 3.6.1.

Exact offline, because T_P never feeds back into the branch-1 posteriors:
  A. the comb's thresholds for precision 0.85 and 0.90 (between the placeholder 0.03 and the 0.95-calibrated
     0.2506), and the rule's precision / coverage / median time to confident at each comb threshold;
  B. the same rule inside transmissions of groups D, E and F (excluded from E1's points);
  C. confident T_P estimates OUTSIDE transmissions (padded by 0.5 s), per group, for every configuration.

    PYTHONPATH=training python -m kz4ap_proto.analysis.tp_observe   (writes build/suite/full3/experiments/tp-observe.md)
"""
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

from kz4ap_proto import metrics
from kz4ap_proto.experiments import DEV
from kz4ap_proto.params import ProtoConfig
from kz4ap_proto.periodicity import Periodicity
from kz4ap_proto.power import disable_power_throttling
from kz4ap_proto.analysis import wants_help

OUT = Path("build/suite/full3")
LOGGED = [1.0, 2.0, 3.0, 5.0, 10.0]
JOBS = 6
PAD_S = 0.5


def cfg_for(method):
    return ProtoConfig().with_values(periodicity_method=method, periodicity_windows_s=LOGGED)


def confident(per, subset, threshold):
    order = [LOGGED.index(w) for w in sorted(subset)]
    return next((per[i] for i in order if per[i][0] is not None and per[i][1] >= threshold), None)


def _outside_of(work):
    p1_path, cfg, rate, label, group = work
    p1 = np.load(p1_path)
    per = Periodicity(cfg, rate)
    txs = metrics.transmissions(label, PAD_S)
    pts = []
    for i in range(0, len(p1), per.update_every):
        per.push(p1[i:i + per.update_every])
        t = min(i + per.update_every, len(p1)) / rate
        if any(a <= t <= b for a, b in txs):
            continue
        per.update(force=True)
        pts.append((group, list(per.per_window)))
    return pts, per.update_every / rate


def outside_points(method):
    work = []
    for job, label, ch, _ in metrics.iter_channels(OUT, "exp-ref", DEV):
        if not label.get("score", True):
            continue
        p1 = OUT / "proto" / "exp-ref" / "p1" / job["result"] / f"{ch['label_index']}.npy"
        work.append((str(p1), cfg_for(method), ch["rate_hz"], label, job["group"]))
    with ProcessPoolExecutor(max_workers=JOBS, initializer=disable_power_throttling) as pool:
        res = list(pool.map(_outside_of, work, chunksize=1))
    step = res[0][1] if res else 0.25
    return [p for pts, _ in res for p in pts], step


def fmt(r):
    if r is None or r["precision"] is None:
        return "no confident estimate"
    iv = lambda x: f" ({x[0]:.3f}–{x[1]:.3f})" if x else ""
    med = r["median_time_to_confident_s"]
    return (f"{r['precision']:.3f}{iv(r['precision_interval'])} | {r['coverage']:.3f}{iv(r['coverage_interval'])} | "
            + (f"{med:.2f} s" if med is not None else "—"))


def main():
    if wants_help(sys.argv):
        print(__doc__)
        return
    disable_power_throttling()
    lines = ["# T_P observations (offline, exp-ref's branch-1 posteriors; observation only)", ""]
    comb_pts = metrics.periodicity_points(OUT, "exp-ref", cfg_for("comb"), only=DEV, jobs=JOBS)
    edge_pts = metrics.periodicity_points(OUT, "exp-ref", cfg_for("edge"), only=DEV, jobs=JOBS)
    sub = (2.0, 5.0, 10.0)
    t85, _ = metrics.calibrate(comb_pts, LOGGED, sub, 0.85)
    t90, _ = metrics.calibrate(comb_pts, LOGGED, sub, 0.90)
    configs = [("comb, (2, 5, 10) s, 0.03 (exp-ref)", "comb", sub, 0.03),
               (f"comb, (2, 5, 10) s, {t85:.4g} (precision 0.85)", "comb", sub, t85),
               (f"comb, (2, 5, 10) s, {t90:.4g} (precision 0.90)", "comb", sub, t90),
               ("comb, (2, 5, 10) s, 0.2506 (precision 0.95; exp-diag-comb-cal)", "comb", sub, 0.2506),
               ("edge comb, (2, 5, 10) s, 0.03232 (exp-diag-edge-cal)", "edge", sub, 0.03232),
               ("edge comb, (5, 10) s, 0.02214 (exp-E1-3)", "edge", (5.0, 10.0), 0.022138968788232207)]
    pts = {"comb": comb_pts, "edge": edge_pts}
    lines += [f"Comb thresholds: precision 0.85 at {t85!r}, precision 0.90 at {t90!r} (dimensionless score).", "",
              "## A. E1's points (inside transmissions; groups A, B mixed, C, G, H per station, I; S500 >= 0 dB)", "",
              f"{len(comb_pts)} points.", "",
              "| configuration | precision | coverage | median time to first confident |", "|---|---|---|---|"]
    for name, m, s, th in configs:
        lines.append(f"| {name} | {fmt(metrics.evaluate_rule(pts[m], LOGGED, s, th))} |")
    lines += ["", "## B. Inside transmissions of groups D, E, F (not in E1's points; constant-speed labels only)", ""]
    groups_b = ("D speed", "E interference", "F tuning")
    pts_b = {m: metrics.periodicity_points(OUT, "exp-ref", cfg_for(m), only=DEV, groups=groups_b,
                                           min_snr_db=-100.0, jobs=JOBS) for m in ("comb", "edge")}
    for g in groups_b:
        gp = {m: [p for p in pts_b[m] if p["group"] == g] for m in pts_b}
        lines += [f"### {g}: {len(gp['comb'])} points", "",
                  "| configuration | precision | coverage | median time to first confident |", "|---|---|---|---|"]
        for name, m, s, th in configs:
            r = metrics.evaluate_rule(gp[m], LOGGED, s, th) if gp[m] else None
            lines.append(f"| {name} | {fmt(r)} |")
        lines.append("")
    lines += ["## C. Confident T_P outside transmissions (padded by 0.5 s), per group",
              "", "Fraction of update points outside every transmission with a confident estimate, and the same "
              "as confident updates per minute.", ""]
    outs = {m: outside_points(m) for m in ("comb", "edge")}
    groups = sorted({g for g, _ in outs["comb"][0]})
    header = "| configuration | " + " | ".join(groups) + " |"
    lines += [header, "|" + "---|" * (len(groups) + 1)]
    for name, m, s, th in configs:
        o, step = outs[m]
        cells = []
        for g in groups:
            gp = [per for gg, per in o if gg == g]
            c = sum(confident(per, s, th) is not None for per in gp)
            cells.append(f"{c / len(gp):.3f} ({c / (len(gp) * step / 60):.1f}/min)" if gp else "—")
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines += ["", "Outside points per group: " + ", ".join(
        f"{g} {sum(1 for gg, _ in outs['comb'][0] if gg == g)}" for g in groups) +
        f"; update step {outs['comb'][1]:.3f} s."]
    target = OUT / "experiments" / "tp-observe.md"
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {target}")


if __name__ == "__main__":
    main()
