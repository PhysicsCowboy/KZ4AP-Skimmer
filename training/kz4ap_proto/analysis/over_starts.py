"""Over starts inside transmissions per transmission, per group with counts and pooled over all groups
(metrics.spurious_over_starts on the development set). Observation/analysis only. Feeds results section 3.10 (E7).

    PYTHONPATH=training python -m kz4ap_proto.analysis.over_starts RUN [RUN ...]"""
import sys

from kz4ap_proto import metrics
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import DEV


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    for name in sys.argv[1:]:
        s = metrics.spurious_over_starts("build/suite/full3", name, DEV)
        n = sum(v["over_starts"] for v in s.values())
        t = sum(v["transmissions"] for v in s.values())
        print(f"{name}: all groups {n} / {t} = {n / t:.4f}; " + "; ".join(
            f"{g} {v['over_starts']}/{v['transmissions']}" for g, v in sorted(s.items())))


if __name__ == "__main__":
    main()
