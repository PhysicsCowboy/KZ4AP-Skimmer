"""(1) The exact calibrated edge-comb threshold for windows (5, 10) s at the default teeth and width; (2) where the
spectrum fit's estimates fall relative to the true dit, and its precision at its top score quantiles.
Observation/analysis only. Feeds results sections 3.2.1 (the spectrum fit) and 3.4 (E1-E3's threshold).

    PYTHONPATH=training python -m kz4ap_proto.analysis.e3_extra

(2) uses windows 2, 5, 10 s and the shortest-window estimate per point regardless of score."""
import math
import sys
from pathlib import Path

import numpy as np

from kz4ap_proto import metrics
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import DEV
from kz4ap_proto.params import ProtoConfig


def main():
    if wants_help(sys.argv):
        print(__doc__)
        return
    out = Path("build/suite/full3")
    cfg = ProtoConfig().with_values(periodicity_method="edge", periodicity_windows_s=[1, 2, 3, 5, 10])
    pts = metrics.periodicity_points(out, "exp-ref", cfg, only=DEV, jobs=10)
    th, r = metrics.calibrate(pts, sorted(cfg.periodicity_windows_s), (5.0, 10.0))
    print(f"edge (5, 10): threshold {th!r}; precision {r['precision']:.4f}; coverage {r['coverage']:.4f}")
    cfg = ProtoConfig().with_values(periodicity_method="spectrum", periodicity_windows_s=[1, 2, 3, 5, 10])
    pts = metrics.periodicity_points(out, "exp-ref", cfg, only=DEV, jobs=10)
    w = sorted(cfg.periodicity_windows_s)
    idx = [w.index(x) for x in (2.0, 5.0, 10.0)]
    ratios, scores, ok = [], [], []
    for p in pts:
        est = next((p["per"][i] for i in idx if p["per"][i][0] is not None), None)
        if est is None:
            continue
        q = est[0] / p["truth"]
        ratios.append(q)
        scores.append(est[1])
        ok.append(abs(math.log(q)) <= math.log(1.05))
    ratios, scores, ok = np.array(ratios), np.array(scores), np.array(ok)
    bins = [0, 0.4, 0.6, 0.95, 1.05, 1.4, 1.8, 2.2, 2.8, 3.3, 1e9]
    hist = np.histogram(ratios, bins)[0]
    print("spectrum: estimate / true dit, shortest window with an estimate, all scores:")
    for a, b, n in zip(bins, bins[1:], hist):
        print(f"  [{a}, {b}): {n / len(ratios):.3f}")
    for qtl in (0.5, 0.9, 0.99, 0.999):
        s = np.quantile(scores, qtl)
        sel = scores >= s
        print(f"  score >= {s:.3f} nats (quantile {qtl}): {sel.sum()} estimates, within 5%: {ok[sel].mean():.3f}; "
              f"median ratio {np.median(ratios[sel]):.3f}")
    # Populations, stated (Task 13 review M5): the rule's own precision and coverage at those score levels, and the
    # levels of metrics.calibrate's search (quantiles over every logged window's scores, 1, 2, 3, 5 and 10 s).
    allscores = np.array([s for p in pts for t, s in p["per"] if t is not None])
    print(f"calibrate's population: {len(allscores)} scores (every logged window); its highest searched level, the "
          f"0.99 quantile: {np.quantile(allscores, 0.99):.3f} nats")
    for qtl in (0.5, 0.9, 0.99, 0.999):
        s = float(np.quantile(scores, qtl))
        r = metrics.evaluate_rule(pts, w, (2.0, 5.0, 10.0), s)
        print(f"  rule at {s:.3f} nats: precision {r['precision']:.3f} {r['precision_interval']}, "
              f"coverage {r['coverage']:.4f}, confident {r['confident']} of {r['points']} points")
    print(f"spectrum points: {len(pts)}; with an estimate in a window of (2, 5, 10) s: {len(ratios)}")


if __name__ == "__main__":
    main()
