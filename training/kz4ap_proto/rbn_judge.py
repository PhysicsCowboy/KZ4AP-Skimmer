"""The RBN judge of checkpoints 3 and 4: sorts the calls a skimmer reported from a recording by what the Reverse Beacon Network spotted.

    python -m kz4ap_proto.rbn_judge RBN_DAY.csv --recording NAME --role ROLE --start UTC --seconds S --center-khz KHZ
        --run LABEL=FILE [--run LABEL=FILE ...] [--na-window] [--known MASTER.SCP] [--out CALLS.csv] [--append]

A heuristic, and results analysis only (CLAUDE.md, Languages); the product never matches calls against the RBN (design spec, section 6).
Each reported base call (calls_score.base_call: /P and prefixes dropped) gets one class:

- `rbn_window`: the RBN holds a CW spot of it inside the recording's UTC window [start, start + seconds] (both ends inclusive; the RBN's
  times are whole seconds, so a start with a fraction leaves its first whole second out) and within +/-96 kHz of the recording's
  center (its 192 kHz span; SPAN_KHZ / 2): *confirmed*;
- `rbn_day`: else spotted (CW) anywhere that day;
- `misread_window`: else one edit (a letter changed, added or dropped; calls_score.edit_distance_one) from a call spotted in the window;
- `misread_day`: else one edit from a call spotted that day;
- `unknown`: none of these.

The two misreading classes are a proxy with systematic error both ways, not a count of errors: a real station the RBN did not spot that
day lands in them when a spotted call is one edit away (about two times in three on the 2017 recordings, checkpoint 3), and a garble that
equals a spotted call escapes them.

The table (one row per base call per recording; `write_rows`): recording, role, call, class, rbn_skimmers_in_window (the number of
distinct RBN skimmers spotting it in the window, for `rbn_window`), one_edit_from (for a misreading, the spotted calls it is one edit
from, in the window for `misread_window` and that day for `misread_day`, joined by "/"), then one 0/1 column per run: whether that run
reported the call. With `--na-window` the calls North American skimmers (`de_cont` NA) spotted in the window are rows too, reported or
not, so that a call no run reported can be counted.

With `--known FILE` (a list of calls, one per line, in MASTER.SCP's form: lines starting with `#` or `!!` skipped, each entry by its
base call) the table gains the column `in_known_list` (1 or 0, every row) after one_edit_from, and the counts split the calls the RBN did
not spot at all: `unknown_listed` and `unknown_unlisted`, and `misread_listed` (a misreading class, but in the list). The evidence is
one-sided (owner, 2026-11-19): a call in the list is likely a real station, since MASTER.SCP is built from contest logs; a call not in
it is undecided, never a misreading, since many hams never contest. Membership leaves every class as it was.

A spotted word with no base call's form (the RBN's own busts, W6SSEQP; S5S) is matched as spotted. With the windows of checkpoint 4's
`windows.csv` this reproduces every class, skimmer count and one-edit list of checkpoint 3's `calls.csv` (1 020 rows).

A run's FILE is our call list (kz4ap-calls --out or kz4ap-skimmer --calls: its `base` column) or CW Skimmer's telnet spots
(`DX de ...` lines, an optional leading local time; the call is the word after the frequency). Its kind is read from its first lines.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import re
from dataclasses import dataclass, field
from pathlib import Path

from kz4ap_proto.calls_score import base_call, edit_distance_one

SPAN_KHZ = 192.0
ONE_EDIT_SHOWN = 3   # the table's one_edit_from holds the first three partners in alphabetical order, as checkpoint 3's did
CLASSES = ("rbn_window", "rbn_day", "misread_window", "misread_day", "unknown")
COLUMNS = ("recording", "role", "call", "class", "rbn_skimmers_in_window", "one_edit_from")
KNOWN_COLUMN = "in_known_list"
MISREADS = ("misread_window", "misread_day")
_SPOT_LINE = re.compile(r"^\s*(?:\d\d:\d\d:\d\d(?:\.\d+)?\s+)?DX\s+de\s+\S*\s+(-?\d+(?:\.\d+)?)\s+(\S+)", re.IGNORECASE)


@dataclass(frozen=True)
class Spot:
    base: str
    khz: float
    when: dt.datetime
    skimmer: str
    continent: str


@dataclass(frozen=True)
class Window:
    start: dt.datetime
    seconds: float
    center_khz: float

    def holds(self, spot: Spot) -> bool:
        end = self.start + dt.timedelta(seconds=self.seconds)
        return self.start <= spot.when <= end and abs(spot.khz - self.center_khz) <= SPAN_KHZ / 2


@dataclass
class Verdict:
    kind: str
    skimmers: int = 0
    one_edit_from: list[str] = field(default_factory=list)


def parse_utc(text: str) -> dt.datetime:
    """`YYYY-MM-DD HH:MM:SS` or ISO 8601 (`T`, `Z`, fractions), as a naive UTC datetime."""
    text = text.strip().replace("T", " ").rstrip("Z")
    for form in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S"):
        try:
            return dt.datetime.strptime(text, form)
        except ValueError:
            pass
    raise ValueError(f"not a UTC time: {text!r}")


def read_rbn(path: str | Path) -> list[Spot]:
    """The CW spots of an RBN day file (its `tx_mode` CW), each with its base call; rows without a base call, a frequency or a time are
    left out."""
    spots = []
    with Path(path).open(encoding="utf-8", errors="replace", newline="") as f:
        for r in csv.DictReader(f):
            if (r.get("tx_mode") or "").strip().upper() != "CW":
                continue
            dx = (r.get("dx") or "").strip().upper()
            if not dx:
                continue
            b = base_call(dx) or dx
            try:
                khz = float(r["freq"])
                when = dt.datetime.strptime(r["date"][:19], "%Y-%m-%d %H:%M:%S")
            except (ValueError, KeyError, TypeError):
                continue
            spots.append(Spot(b, khz, when, (r.get("callsign") or "").strip(), (r.get("de_cont") or "").strip()))
    return spots


def judge(bases, spots: list[Spot], window: Window) -> dict[str, Verdict]:
    """Each base call's verdict against the day's spots and the recording's window."""
    in_window: dict[str, set[str]] = {}
    for s in spots:
        if window.holds(s):
            in_window.setdefault(s.base, set()).add(s.skimmer)
    day = {s.base for s in spots}
    out = {}
    for b in sorted(set(bases)):
        if b in in_window:
            out[b] = Verdict("rbn_window", len(in_window[b]))
        elif b in day:
            out[b] = Verdict("rbn_day")
        else:
            near = sorted(x for x in in_window if edit_distance_one(b, x))
            if near:
                out[b] = Verdict("misread_window", one_edit_from=near)
            else:
                near = sorted(x for x in day if edit_distance_one(b, x))
                out[b] = Verdict("misread_day", one_edit_from=near) if near else Verdict("unknown")
    return out


def read_known(path: str | Path) -> set[str]:
    """The base calls of a known-call list in MASTER.SCP's form; an entry with no base call's form is kept as written."""
    calls = set()
    for line in Path(path).read_text(encoding="utf-8", errors="replace").lstrip("﻿").splitlines():
        word = line.strip().upper()
        if not word or word.startswith("#") or word.startswith("!!"):
            continue
        calls.add(base_call(word) or word)
    return calls


def columns(run_labels: list[str], known: bool = False) -> list[str]:
    """The table's header: with a known-call list, `in_known_list` after one_edit_from."""
    return list(COLUMNS) + ([KNOWN_COLUMN] if known else []) + list(run_labels)


def north_american_window_calls(spots: list[Spot], window: Window) -> set[str]:
    return {s.base for s in spots if s.continent == "NA" and window.holds(s)}


def read_run(path: str | Path) -> set[str]:
    """The base calls of a run: our call list's `base` column, or CW Skimmer's telnet spots."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    first = text.lstrip("\ufeff").split("\n", 1)[0]
    if "," in first and "base" in [c.strip() for c in first.split(",")]:
        return {r["base"].strip() for r in csv.DictReader(text.lstrip("\ufeff").splitlines()) if r.get("base", "").strip()}
    calls = set()
    for line in text.splitlines():
        m = _SPOT_LINE.match(line)
        if m:
            word = m.group(2).upper()
            calls.add(base_call(word) or word)
    return calls


def rows(recording: str, role: str, runs: dict[str, set[str]], spots: list[Spot], window: Window,
         na_window: bool = False, known: set[str] | None = None) -> list[list]:
    """The table's rows for one recording, sorted by call; with `known`, each row's membership after one_edit_from."""
    bases = set().union(*runs.values()) if runs else set()
    if na_window:
        bases |= north_american_window_calls(spots, window)
    verdicts = judge(bases, spots, window)
    out = []
    for b, v in verdicts.items():
        out.append([recording, role, b, v.kind, v.skimmers if v.kind == "rbn_window" else "",
                    "/".join(v.one_edit_from[:ONE_EDIT_SHOWN])]
                   + ([int(b in known)] if known is not None else [])
                   + [int(b in calls) for calls in runs.values()])
    return out


def write_rows(path: str | Path, run_labels: list[str], table: list[list], append: bool = False, known: bool = False) -> None:
    path = Path(path)
    new = not (append and path.exists())
    with path.open("w" if new else "a", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        if new:
            w.writerow(columns(run_labels, known))
        w.writerows(table)


def counts(table_rows: list[dict], column: str) -> dict[str, int]:
    """calls, confirmed (rbn_window), unconfirmed and each class, over the rows whose `column` is 1; with an `in_known_list` column
    also unknown_listed, unknown_unlisted and misread_listed."""
    listed = bool(table_rows) and KNOWN_COLUMN in table_rows[0]
    c = {"calls": 0, "confirmed": 0, "unconfirmed": 0, **{k: 0 for k in CLASSES}}
    if listed:
        c.update(unknown_listed=0, unknown_unlisted=0, misread_listed=0)
    for r in table_rows:
        if str(r[column]) != "1":
            continue
        c["calls"] += 1
        c[r["class"]] += 1
        c["confirmed" if r["class"] == "rbn_window" else "unconfirmed"] += 1
        if listed:
            in_list = str(r[KNOWN_COLUMN]) == "1"
            if r["class"] == "unknown":
                c["unknown_listed" if in_list else "unknown_unlisted"] += 1
            elif r["class"] in MISREADS and in_list:
                c["misread_listed"] += 1
    return c


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="python -m kz4ap_proto.rbn_judge", description=__doc__.split("\n\n")[0])
    p.add_argument("rbn", type=Path, help="the RBN day file (CSV)")
    p.add_argument("--recording", required=True)
    p.add_argument("--role", default="")
    p.add_argument("--start", required=True, help="the recording's start, UTC")
    p.add_argument("--seconds", type=float, required=True, help="the recording's length, s")
    p.add_argument("--center-khz", type=float, required=True, help="the recording's center, kHz")
    p.add_argument("--run", action="append", default=[], metavar="LABEL=FILE", help="a run's calls (repeatable, in column order)")
    p.add_argument("--na-window", action="store_true", help="add North American skimmers' window calls as rows")
    p.add_argument("--known", type=Path, help="a known-call list (MASTER.SCP's form): adds in_known_list and splits the counts")
    p.add_argument("--out", type=Path)
    p.add_argument("--append", action="store_true", help="append to --out without a header if it exists")
    a = p.parse_args(argv)
    runs = {}
    for item in a.run:
        label, sep, path = item.partition("=")
        if not sep:
            p.error(f"--run {item!r}: expected LABEL=FILE")
        runs[label] = read_run(path)
    window = Window(parse_utc(a.start), a.seconds, a.center_khz)
    known = read_known(a.known) if a.known else None
    table = rows(a.recording, a.role, runs, read_rbn(a.rbn), window, a.na_window, known)
    if a.out:
        write_rows(a.out, list(runs), table, a.append, known is not None)
    named = [dict(zip(columns(list(runs), known is not None), map(str, r))) for r in table]
    for label in runs:
        c = counts(named, label)
        print(f"{a.recording} {label}: " + ", ".join(f"{k} {v}" for k, v in c.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
