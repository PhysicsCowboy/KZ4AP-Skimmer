"""Pooled CER and pooled paired CER / first-word CER of bank-proto against Matched and Envelope over all comparable
signals, split by oracle channels and the detector path, for all seeds, the held-out seeds and seed 1 (not-comparable
rows left out). Observation/analysis only (no rule). Feeds results section 4.3 (the pooled table) and 4.2.

    PYTHONPATH=training python -m kz4ap_proto.analysis.overall"""
import sys
from pathlib import Path

from kz4ap_synth.suites import _with_interval
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import pooled_paired
from kz4ap_proto.report import DETECTOR_PATH_GROUPS, comparable_rows, not_comparable

OUT = Path("build/suite/full3")
NAME = "bank-proto"


def main():
    if wants_help(sys.argv):
        print(__doc__)
        return
    for label, only in (("all seeds", None), ("held-out seeds 2-3", r"-s[23](\.stations|\.oracle)?$"),
                        ("seed 1", r"-s1(\.stations|\.oracle)?$")):
        rows = [r for r in comparable_rows(OUT, NAME, only=only) if not not_comparable(r["group"], r["tag"])]
        for part, keep in (("every comparable signal", lambda g: True),
                           ("oracle channels", lambda g: g not in DETECTOR_PATH_GROUPS),
                           ("detector path", lambda g: g in DETECTOR_PATH_GROUPS)):
            sub = [dict(r, group="all-of-part") for r in rows if keep(r["group"])]
            pooled = {}
            for fe in (NAME, "matched", "baseline"):
                s = [r for r in sub if r["front_end"] == fe and r["scored"]]
                pooled[fe] = (sum(r["edits"] for r in s) / sum(r["symbols"] for r in s), len(s))
            out = [f"{label}, {part}: {pooled[NAME][1]} signals; pooled CER bank-proto {pooled[NAME][0]:.4f}, "
                   f"Matched {pooled['matched'][0]:.4f}, Envelope {pooled['baseline'][0]:.4f}"]
            for ref, ref_name in (("matched", "Matched"), ("baseline", "Envelope")):
                c = pooled_paired(sub, ref, NAME)["all"]
                f = pooled_paired(sub, ref, NAME, "first_word_edits", "first_word_symbols")["all"]
                out.append(f"  bank-proto - {ref_name}: paired CER {_with_interval(c['mean'], c['interval'], '+.4f')}; "
                           f"paired first-word CER {_with_interval(f['mean'], f['interval'], '+.4f')} "
                           f"({f['signals']} signals)")
            print("\n".join(out))


if __name__ == "__main__":
    main()
