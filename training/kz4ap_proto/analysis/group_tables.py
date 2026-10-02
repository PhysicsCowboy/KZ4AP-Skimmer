"""Every (group, tag) row of the prototype against Matched and Envelope, all seeds, as compact markdown (from
summary.json and report-bank-proto.json). Observation/analysis only. Feeds results section 4.3.1.

    PYTHONPATH=training python -m kz4ap_proto.analysis.group_tables > build/suite/full3/experiments/group-tables.md"""
import json
import sys
from pathlib import Path

from kz4ap_proto.analysis import wants_help

OUT = Path("build/suite/full3")


def num(x, fmt):
    return format(x, fmt).replace("-", "−")


def cer(v):
    i = v.get("cer_interval")
    return f"{num(v['cer'], '.3f')} ({num(i[0], '.3f')}–{num(i[1], '.3f')})" if i else num(v["cer"], ".3f")


def paired(p):
    if not p:
        return "—"
    i = p["interval"]
    mark = {"better": " B", "worse": " W", "unchanged": ""}.get(p["verdict"], "")
    return f"{num(p['mean'], '+.3f')} ({num(i[0], '+.3f')} to {num(i[1], '+.3f')}){mark}"


def main():
    if wants_help(sys.argv):
        print(__doc__)
        return
    sys.stdout.reconfigure(encoding="utf-8")  # the tables use U+2212 and U+2013
    summary = json.loads((OUT / "summary.json").read_text(encoding="utf-8"))
    regimes = {(e["group"], e["tag"]): e
               for e in json.loads((OUT / "report-bank-proto.json").read_text(encoding="utf-8"))["comparison"]["regimes"]}
    by: dict = {}
    for g in summary["groups"]:
        by.setdefault((g["group"], g["tag"]), {})[g["front_end"]] = g
    groups = sorted({g for g, _ in by})
    lines = []
    for group in groups:
        lines += [f"**{group}**", "",
                  "| tag | signals | CER: prototype | Matched | Envelope | prototype − Matched | prototype − Envelope "
                  "| first-word CER P / M / E |",
                  "|---|---|---|---|---|---|---|---|"]
        for (g, tag) in sorted(k for k in by if k[0] == group):
            v = by[(g, tag)]
            e = regimes.get((g, tag), {})
            star = " *" if e.get("not_comparable") else ""
            fw = " / ".join(num(v[fe]["first_word_cer"], ".2f") for fe in ("bank-proto", "matched", "baseline"))
            lines.append(f"| {tag}{star} | {v['bank-proto']['signals']} | {cer(v['bank-proto'])} | {cer(v['matched'])} "
                         f"| {cer(v['baseline'])} | {paired(e.get('matched'))} | {paired(e.get('baseline'))} | {fw} |")
        lines.append("")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
