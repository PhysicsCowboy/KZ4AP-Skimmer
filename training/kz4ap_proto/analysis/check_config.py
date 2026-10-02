"""The settled values stored in a run's decoded files, and whether every decoded file of the run holds the same
config. Observation/analysis only. Feeds results section 4.1 ("Every decoded file stores the same configuration").

    PYTHONPATH=training python -m kz4ap_proto.analysis.check_config NAME"""
import json
import sys
from pathlib import Path

from kz4ap_proto.analysis import wants_help


def main():
    if wants_help(sys.argv) or len(sys.argv) < 2:
        print(__doc__)
        return
    name = sys.argv[1]
    files = sorted((Path("build/suite/full3/proto") / name).glob("*.decoded.json"))
    configs = [json.loads(p.read_text(encoding="utf-8"))["config"] for p in files]
    c = configs[0]
    keys = ["noise_method", "periodicity_method", "comb_confidence_min", "periodicity_windows_s", "fit_memory",
            "t_grid_step", "q_grid", "w_grid", "tg_grid", "false_marks_per_s", "rekey_after_s", "new_over_min_s",
            "new_over_gaps", "switch_persistence", "quality_tie_nats", "text_window_chars"]
    print(f"{name}: {len(files)} decoded files; all configs equal: {all(x == c for x in configs)}")
    for k in keys:
        print(f"  {k} = {c.get(k)}")
    print(f"  x_on_values = {c['x_on_values'][0]} ... {c['x_on_values'][-1]} ({len(c['x_on_values'])} values)")


if __name__ == "__main__":
    main()
