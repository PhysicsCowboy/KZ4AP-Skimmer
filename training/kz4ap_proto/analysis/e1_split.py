"""E1's periodicity tables split by group (C fists apart) and, for every method, by the true dit (fast: T <= 31.5 ms,
i.e. >= 38.1 WPM; slow: above), windows (2, 5, 10) s. Observation/analysis only (not a decision rule). Feeds
results section 3.2.1.

    PYTHONPATH=training python -m kz4ap_proto.analysis.e1_split METHOD [METHOD ...]   (comb, edge, spectrum)"""
import sys
from pathlib import Path

from kz4ap_proto import metrics
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import DEV
from kz4ap_proto.params import ProtoConfig


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    out = Path("build/suite/full3")
    sub = (2.0, 5.0, 10.0)
    for method in sys.argv[1:]:
        cfg = ProtoConfig().with_values(periodicity_method=method, periodicity_windows_s=[1, 2, 3, 5, 10])
        w = sorted(cfg.periodicity_windows_s)
        pts = metrics.periodicity_points(out, "exp-ref", cfg, only=DEV, jobs=10)
        th, _ = metrics.calibrate(pts, w, sub)
        print(f"## {method}: pooled calibrated threshold {th}")
        parts = {"C fists": [p for p in pts if p["group"] == "C fists"],
                 "all but C": [p for p in pts if p["group"] != "C fists"],
                 "fast (T <= 31.5 ms)": [p for p in pts if p["truth"] <= 0.0315],
                 "slow (T > 31.5 ms)": [p for p in pts if p["truth"] > 0.0315]}
        for name, ps in parts.items():
            at = metrics.evaluate_rule(ps, w, sub, th) if th is not None else None
            own, r = metrics.calibrate(ps, w, sub)
            f = lambda r: ("none" if r is None or r["precision"] is None else
                           f"precision {r['precision']:.3f} {r['precision_interval']}, coverage {r['coverage']:.3f} "
                           f"{r['coverage_interval']}, median {r['median_time_to_confident_s']}")
            print(f"- {name}: {len(ps)} points, {len({p['unit'] for p in ps})} channels; at the pooled threshold: "
                  f"{f(at)}; own calibrated threshold {own}: {f(r)}")
        print(flush=True)


if __name__ == "__main__":
    main()
