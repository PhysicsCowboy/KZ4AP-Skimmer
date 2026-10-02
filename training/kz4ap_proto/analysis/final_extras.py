"""The owner's two extra configurations on the held-out seeds 2-3: scored, reported, and each paired against
bank-proto on the same held-out signals. Observation/analysis only (no rule). Feeds results section 5.5.

    PYTHONPATH=training python -m kz4ap_proto.analysis.final_extras score BENCH NAME...
    PYTHONPATH=training python -m kz4ap_proto.analysis.final_extras report NAME...
    PYTHONPATH=training python -m kz4ap_proto.analysis.final_extras paired NAME...

score: runner.score of NAME's decoded files (seeds 2-3) into experiments/results/NAME, so that the suite's results/
(and its summary.md) hold only baseline, matched and bank-proto.
report: report.write_report(NAME) on the held-out test cases, reading results/ and experiments/results/
(-> report-NAME-held-out.md).
paired: NAME minus bank-proto, signal by signal on the held-out test cases, pooled over all groups and per group,
CER and first-word CER (experiments.pooled_paired: the same per-signal differences and bootstrap as
suites.paired_differences, grouped by group instead of (group, tag)) -> experiments/extras-paired.md."""
import re
import sys
from pathlib import Path

from kz4ap_synth.suites import _with_interval, load_results
from kz4ap_proto import runner
from kz4ap_proto.analysis import wants_help
from kz4ap_proto.experiments import EXPERIMENT_RESULTS, pooled_paired
from kz4ap_proto.report import write_report

OUT = Path("build/suite/full3")
HELD_OUT = r"-s[23](\.stations|\.oracle)?$"
BASE = "bank-proto"
ROOTS = [OUT / "results", OUT / EXPERIMENT_RESULTS]


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    cmd = sys.argv[1]
    if cmd == "score":
        bench = Path(sys.argv[2])
        for name in sys.argv[3:]:
            runner.score(OUT, bench, name, HELD_OUT, results_root=OUT / EXPERIMENT_RESULTS)
    elif cmd == "report":
        for name in sys.argv[2:]:
            print(f"wrote {write_report(OUT, name, results_dirs=ROOTS, only=HELD_OUT, suffix='held-out')}")
    elif cmd == "paired":
        lines = ["# The owner's extra configurations against bank-proto, held-out seeds 2-3 (observation only)", "",
                 f"Rows: test cases matching `{HELD_OUT}`. Paired: extra minus bank-proto, signal by signal on the "
                 "same labels; mean over signals; bootstrap 95% interval over signals (1000 resamples). "
                 "Detector-path groups: both decode the same detector channels. Not a decision rule; neither extra "
                 "is adopted by any rule.", ""]
        for name in sys.argv[2:]:
            rows, _ = load_results(OUT, ROOTS, [BASE, name])
            rows = [r for r in rows if re.search(HELD_OUT, r["recording"])]
            cer = pooled_paired(rows, BASE, name)
            fw = pooled_paired(rows, BASE, name, "first_word_edits", "first_word_symbols")
            scored = {fe: [r for r in rows if r["front_end"] == fe and r["scored"]] for fe in (BASE, name)}
            pooled = {fe: sum(r["edits"] for r in v) / sum(r["symbols"] for r in v) for fe, v in scored.items()}
            lines += [f"## {name} − {BASE}", "",
                      f"Pooled CER (summed edits / summed symbols): {BASE} {pooled[BASE]:.4f}, {name} "
                      f"{pooled[name]:.4f} ({len(scored[name])} scored signals).", "",
                      "| group | signals | paired CER | paired first-word CER (signals) |", "|---|---|---|---|"]
            for group in sorted(cer, key=lambda g: (g != "all", g)):
                f = fw.get(group, {})
                lines.append(f"| {group} | {cer[group]['signals']} | "
                             f"{_with_interval(cer[group]['mean'], cer[group]['interval'], '+.4f')} | "
                             f"{_with_interval(f.get('mean'), f.get('interval'), '+.4f')} ({f.get('signals', 0)}) |")
            lines.append("")
        path = OUT / "experiments" / "extras-paired.md"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"wrote {path}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
