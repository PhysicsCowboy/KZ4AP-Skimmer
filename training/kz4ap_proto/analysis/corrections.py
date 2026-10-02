"""The prototype's corrections in a run's decoded files (oracle and detector path): count by reason, per
channel-minute, and the reach distribution (s); every reach must be <= the correction reach, 20 s (owner).
Observation/analysis only. Feeds results section 4.4 ("Corrections").

    PYTHONPATH=training python -m kz4ap_proto.analysis.corrections NAME [REGEX]"""
import json
import re
import sys
from pathlib import Path

import numpy as np

from kz4ap_proto.analysis import wants_help

OUT = Path("build/suite/full3")


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    name = sys.argv[1]
    only = sys.argv[2] if len(sys.argv) > 2 else None
    reach, by_reason, channels, channel_s, files = [], {}, 0, 0.0, 0
    for path in sorted((OUT / "proto" / name).glob("*.decoded.json")):
        result = path.name[:-len(".decoded.json")]
        if only and not re.search(only, result):
            continue
        files += 1
        for ch in json.loads(path.read_text(encoding="utf-8"))["channels"]:
            channels += 1
            channel_s += ch["channel_s"]
            for c in ch["corrections"]:
                reach.append(c["reach_s"])
                by_reason[c["reason"]] = by_reason.get(c["reason"], 0) + 1
    r = np.array(reach)
    print(f"{name}{' (' + only + ')' if only else ''}: {files} decoded files, {channels} channels, "
          f"{channel_s:.0f} channel-seconds; {len(r)} corrections ({len(r) / (channel_s / 60.0):.3f} per "
          f"channel-minute); by reason {by_reason}")
    if len(r):
        q = np.percentile(r, [50, 90, 99])
        print(f"reach, s: median {q[0]:.3f}, 90% {q[1]:.3f}, 99% {q[2]:.3f}, max {r.max():.3f}; "
              f"over 20 s: {int((r > 20.0 + 1e-9).sum())}")


if __name__ == "__main__":
    main()
