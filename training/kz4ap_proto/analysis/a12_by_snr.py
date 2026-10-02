"""Group A CER per S500 point (dB, -6 to +6 dB) on the held-out seeds, for bank-proto, the two extras and Matched.
Observation/analysis only. Feeds results section 5.5 (the "Group A and the regressions, held-out" table).

    PYTHONPATH=training python -m kz4ap_proto.analysis.a12_by_snr"""
import re
import sys
from pathlib import Path

from kz4ap_synth.suites import aggregate, load_results
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import EXPERIMENT_RESULTS

OUT = Path("build/suite/full3")


def main():
    if wants_help(sys.argv):
        print(__doc__)
        return
    names = ["bank-proto", "bank-proto-comb0p2506", "bank-proto-comb0p1487", "matched"]
    rows, _ = load_results(OUT, [OUT / "results", OUT / EXPERIMENT_RESULTS], names)
    rows = [r for r in rows if re.search(r"-s[23]$", r["recording"]) and r["group"] == "A sensitivity"]
    agg = aggregate(rows)
    for tag in ("12 wpm", "25 wpm", "40 wpm"):
        for fe in names:
            pts = agg[(fe, "A sensitivity", tag)]["cer_by_snr"]
            print(f"{tag} {fe:22s} " + " ".join(f"{s:+.0f}:{c:.3f}" for s, c in pts if -6 <= s <= 6))


if __name__ == "__main__":
    main()
