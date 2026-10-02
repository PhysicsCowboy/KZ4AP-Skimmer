"""Paired CER and paired first-word CER (variant minus base, per signal) pooled over a chosen set of groups, with
the same bootstrap as experiments.pooled_paired (95% over signals). Observation/analysis only. Feeds results
sections 3.8 (E5, group I), 3.9 (E9), 3.10 (E7), 3.12 (E8) and section 6's group I step table.

    PYTHONPATH=training python -m kz4ap_proto.analysis.pooled_groups BASE VARIANT LETTERS

LETTERS: group initials, e.g. AGHI (H means both oracle views)."""
import sys
from pathlib import Path

from kz4ap_synth.suites import _with_interval, load_results
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import EXPERIMENT_RESULTS, pooled_paired


def main():
    if wants_help(sys.argv) or len(sys.argv) < 4:
        print(__doc__)
        return
    out = Path("build/suite/full3")
    base, variant, letters = sys.argv[1], sys.argv[2], sys.argv[3]
    rows, _ = load_results(out, [out / EXPERIMENT_RESULTS], [base, variant])
    rows = [r for r in rows if r["group"][0] in letters]
    groups = sorted({r["group"] for r in rows})
    for label, e, s in (("paired CER", "edits", "symbols"),
                        ("paired first-word CER", "first_word_edits", "first_word_symbols")):
        p = pooled_paired(rows, base, variant, e, s)["all"]
        print(f"{variant} - {base}, {label}, pooled over {', '.join(groups)}: {p['signals']} signals, "
              f"{_with_interval(p['mean'], p['interval'], '+.4f')}")


if __name__ == "__main__":
    main()
