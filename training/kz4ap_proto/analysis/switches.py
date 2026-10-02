"""Branch switches and alternations per minute of transmission time, pooled over all groups (counts / minutes, from
metrics.switch_stats on the development set). Observation/analysis only. Feeds results section 3.11 (E6).

    PYTHONPATH=training python -m kz4ap_proto.analysis.switches RUN [RUN ...]"""
import sys

from kz4ap_proto import metrics
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import DEV


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    for name in sys.argv[1:]:
        s = metrics.switch_stats("build/suite/full3", name, DEV)
        sw = sum(v["switches"] for v in s.values())
        al = sum(v["alternations"] for v in s.values())
        m = sum(v["minutes"] for v in s.values())
        print(f"{name}: {m:.1f} min of transmissions; switches {sw} ({sw / m:.3f} /min); "
              f"alternations {al} ({al / m:.3f} /min)")


if __name__ == "__main__":
    main()
