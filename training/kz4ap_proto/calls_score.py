"""Scores a run's call list: against another program's list, or against a generated recording's truth.

    python -m kz4ap_proto.calls_score compare RUN.csv OTHER [--name-a NAME] [--name-b NAME] [--out OUT.csv]
        [--spotter CALL]... [--continent NA] [--from UTC] [--to UTC] [--khz LOW HIGH] [--tx-mode CW]
    python -m kz4ap_proto.calls_score truth RUN.csv LABELS.json [--attribution-hz HZ] [--out OUT.csv]

RUN.csv is what kz4ap-calls --out or kz4ap-skimmer --calls writes (one row per call), or any file the reader below accepts.

**The Reverse Beacon Network's day files** (`compare`'s filters; SpotFilter in read_calls_file): when OTHER is one, `--spotter` keeps the spots of
those skimmers (its `callsign` column; repeatable), `--continent` those of skimmers on one continent (`de_cont`), `--from` and `--to` a
UTC window (`date`, `YYYY-MM-DD HH:MM:SS`; ISO 8601 with `T`, `Z` or fractions is accepted and cut to the second; from inclusive, to
exclusive), `--khz` a frequency span (`freq`, kHz, inclusive), `--tx-mode` one mode (`tx_mode`). A row that lacks a filtered column is
dropped; the dropped rows are counted.

**What the reader accepts** (read_calls_file). CW Skimmer's exact export format is not known here, so the input is forgiving, and the
adapter used is printed so that a wrong guess shows:

- `adif`: `<CALL:5>K1ABC` tags. The frequency from `<FREQ:n>`, MHz.
- `spots`: DX-cluster and CW Skimmer telnet lines, `DX de W3LPL-#: 7025.1 K1ABC 12 dB 25 WPM CQ 1234Z`. Each line is one spot. As CW Skimmer 2.1
  really sends them: the spotter is `-#:` when its Operator page is empty; playing a WAV with no QRG tag, the frequency is the offset from the
  center, kHz, 0.1 kHz steps, and may be negative; the clock is 0000Z with no QTR tag. A leading local time stamp, HH:MM:SS[.f], is allowed.
- `csv`: a comma-separated file with a header naming the call's column: `dx`, `dx_call`, `call`, `callsign`, or `station`, in that order of
  preference, since in the Reverse Beacon Network's files `callsign` is the skimmer and `dx` the station spotted. A frequency from a
  column `freq_khz`, `freq`, `frequency` or `qrg` (kHz; values under 100 are taken as MHz) or `freq_hz` (Hz, relative frequencies kept).
  A column `count` gives the readings of a row; otherwise each row counts one.
- `list`: anything else, a plain call list or an unknown log: on each line, the first word with a call's form is the call; a number
  between 1 800 and 30 000 with a decimal point is a frequency in kHz. Lines with no such word are counted and reported as skipped.

Calls are compared by base call: K1ABC/P, DL/K1ABC and K1ABC are one station. Results are the analysis of a run (CLAUDE.md, Languages): the
grammar here is a reader's check, and the engine's (engine/src/callsign.cpp) is what decides what a run reports.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

_DIGIT_SECOND = {
    "A": "23456789", "C": "23456789", "D": "23456789", "E": "234567", "H": "2346789", "J": "2345678", "L": "23456789",
    "P": "23456789", "S": "0235678" + "9", "T": "2345678", "V": "2345678", "Y": "23456789", "Z": "2368",
}
_WHOLE_LETTER = "BFGIKMNRW"
_BASE = re.compile(r"^([A-Z0-9]{1,3}?)([0-9]{1,4})([A-Z]{1,4})$")
_SPOT = re.compile(r"^\s*(?:\d\d:\d\d:\d\d(?:\.\d+)?\s+)?DX\s+DE\s+\S*\s+(-?\d+(?:\.\d+)?)\s+([A-Z0-9/]+)", re.IGNORECASE)
_ADIF_CALL = re.compile(r"<CALL:(\d+)(?::[A-Z])?>", re.IGNORECASE)
_ADIF_FREQ = re.compile(r"<FREQ:(\d+)(?::[A-Z])?>", re.IGNORECASE)
_MODIFIERS = {"P", "M", "MM", "AM", "QRP", "QRPP", "A"}


def _prefix_ok(prefix: str) -> bool:
    """The ITU's series, as the engine's allocated_pair reads them; only the parts a reader's check needs."""
    if len(prefix) == 1:
        return prefix in _WHOLE_LETTER
    if prefix[0] == "Q" or prefix == "DE" or prefix == "YZ" or prefix == "4N":
        return False
    if prefix[0].isdigit():
        return prefix[0] in "23456789" or prefix[:2] == "1A"
    if len(prefix) == 2 and prefix[1].isdigit():
        return prefix[1] in _DIGIT_SECOND.get(prefix[0], "")
    return len(prefix) == 2


def base_call(word: str) -> str | None:
    """The base call of a word with a call's form, or None. K1ABC/P -> K1ABC, DL/K1ABC -> K1ABC, K1ABC/DL -> K1ABC."""
    word = word.strip().upper()
    if not word or not re.fullmatch(r"[A-Z0-9/]+", word):
        return None
    parts = word.split("/")
    if len(parts) == 1:
        return word if _is_base(word) else None
    if len(parts) != 2 or not parts[0] or not parts[1]:
        return None
    left, right = parts
    # As the engine's parse_form (call-form-2): either side may be the base; when both can, the one that is not a location form
    # (VP2V, VK9X, 3D2R) when exactly one is, else the left (W1AW/VP2V and VP2V/W1AW -> W1AW, K1A/W1AW -> K1A, N4A/K4SV -> N4A).
    modifier = right in _MODIFIERS or (len(right) == 1 and right.isdigit())
    left_base = _is_base(left) and (modifier or len(right) <= 4)
    right_base = not modifier and _is_base(right) and len(left) <= 4
    if left_base and right_base:
        return right if _location_form(left) and not _location_form(right) else left
    if left_base:
        return left
    return right if right_base else None


def _location_form(word: str) -> bool:
    """Two characters of an allocated pair, a digit and one letter, outside the United States' series: VP2V, VK9X, 3D2R."""
    if len(word) != 4 or not (word[2].isdigit() and word[3].isalpha()) or not _prefix_ok(word[:2]):
        return False
    us = word[0] in "KNW" and word[1].isalpha() or word[0] == "A" and "A" <= word[1] <= "L"
    return not us


def _is_base(word: str) -> bool:
    # Prefixes of one, two, three characters in turn: the first that leaves one run of digits and then letters.
    for length in (1, 2, 3):
        if len(word) <= length:
            return False
        prefix, rest = word[:length], word[length:]
        if length == 3 and not prefix[0].isdigit():
            continue
        match = re.fullmatch(r"([0-9]{1,4})([A-Z]{1,4})", rest)
        if match and _prefix_ok(prefix) and (prefix[-1].isalpha() or len(prefix) == 2):
            return True
    return False


@dataclass
class Listed:
    count: int = 0
    freqs_khz: list[float] = field(default_factory=list)
    forms: set[str] = field(default_factory=set)
    row: dict[str, str] = field(default_factory=dict)


@dataclass
class CallFile:
    adapter: str
    calls: dict[str, Listed]
    skipped_lines: int = 0
    skipped_examples: list[str] = field(default_factory=list)
    filtered: int = 0  # rows a SpotFilter dropped


def _utc_second(text: str) -> str:
    """A UTC time cut to `YYYY-MM-DD HH:MM:SS`, the RBN's form, so that times compare as text."""
    return text.strip().replace("T", " ").rstrip("Z")[:19]


@dataclass
class SpotFilter:
    """Which rows of a Reverse Beacon Network day file to keep; see the module's description. None keeps every row."""
    spotters: set[str] | None = None
    continent: str | None = None
    start_utc: str | None = None  # inclusive
    end_utc: str | None = None  # exclusive
    low_khz: float | None = None
    high_khz: float | None = None
    tx_mode: str | None = None

    def keeps(self, row: dict[str, str]) -> bool:
        if self.spotters is not None and (row.get("callsign") or "").strip().upper() not in self.spotters:
            return False
        if self.continent is not None and (row.get("de_cont") or "").strip().upper() != self.continent.upper():
            return False
        if self.tx_mode is not None and (row.get("tx_mode") or "").strip().upper() != self.tx_mode.upper():
            return False
        if self.start_utc is not None or self.end_utc is not None:
            when = row.get("date")
            if not when:
                return False
            when = _utc_second(when)
            if self.start_utc is not None and when < _utc_second(self.start_utc):
                return False
            if self.end_utc is not None and when >= _utc_second(self.end_utc):
                return False
        if self.low_khz is not None or self.high_khz is not None:
            khz = _to_float(row.get("freq"))
            if khz is None:
                return False
            if self.low_khz is not None and khz < self.low_khz:
                return False
            if self.high_khz is not None and khz > self.high_khz:
                return False
        return True


def _note(calls: dict[str, Listed], word: str, count: int = 1, freq: float | None = None, row: dict[str, str] | None = None) -> bool:
    base = base_call(word)
    if base is None:
        return False
    entry = calls.setdefault(base, Listed())
    entry.count += count
    entry.forms.add(word.strip().upper())
    if freq is not None:
        entry.freqs_khz.append(freq)
    if row:
        entry.row = row
    return True


def _to_float(text: str | None) -> float | None:
    try:
        return float(text) if text not in (None, "") else None
    except ValueError:
        return None


def _read_adif(text: str) -> dict[str, Listed]:
    calls: dict[str, Listed] = {}
    for record in re.split(r"<EOR>", text, flags=re.IGNORECASE):
        match = _ADIF_CALL.search(record)
        if not match:
            continue
        word = record[match.end():match.end() + int(match.group(1))]
        freq_match = _ADIF_FREQ.search(record)
        freq = None
        if freq_match:
            value = _to_float(record[freq_match.end():freq_match.end() + int(freq_match.group(1))])
            freq = value * 1000.0 if value is not None else None
        _note(calls, word, freq=freq)
    return calls


def _call_column(header: list[str]) -> str | None:
    names = [h.strip().lower() for h in header]
    return next((c for c in ("dx", "dx_call", "call", "callsign", "station") if c in names), None)


def _row_khz(row: dict[str, str]) -> float | None:
    """The first of the frequency columns that holds a value, kHz."""
    for column in ("freq_khz", "freq", "frequency", "qrg"):
        value = _to_float(row.get(column))
        if value is not None:
            return value * 1000.0 if 0 < value < 100 else value
    value = _to_float(row.get("freq_hz"))
    return value / 1000.0 if value is not None else None


def _read_csv(text: str, call_column: str, spot_filter: SpotFilter | None = None) -> tuple[dict[str, Listed], int, list[str], int]:
    calls: dict[str, Listed] = {}
    skipped, examples, filtered = 0, [], 0
    reader = csv.DictReader(io.StringIO(text), skipinitialspace=True)
    reader.fieldnames = [name.strip().lower() for name in (reader.fieldnames or [])]
    for row in reader:
        if spot_filter is not None and not spot_filter.keeps(row):
            filtered += 1
            continue
        count = _to_float(row.get("count"))
        if not _note(calls, row.get(call_column) or "", count=int(count) if count else 1, freq=_row_khz(row), row=row):
            skipped += 1
            if len(examples) < 5:
                examples.append(str(row.get(call_column)))
    return calls, skipped, examples, filtered


def _read_lines(text: str) -> tuple[dict[str, Listed], int, list[str], bool]:
    calls: dict[str, Listed] = {}
    skipped, examples, spots, lines = 0, [], 0, 0
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith(("#", ";")):
            continue
        lines += 1
        match = _SPOT.match(line)
        if match:
            spots += 1
            word = match.group(2)
            if not _note(calls, word, freq=float(match.group(1))):
                # A spot's word is a station's call by the spotter's own test, whatever this reader's grammar makes of it (D4C, E7DX, LZ1SPACE).
                entry = calls.setdefault(word.upper(), Listed())
                entry.count += 1
                entry.forms.add(word.upper())
                entry.freqs_khz.append(float(match.group(1)))
            continue
        freq = None
        call = None
        for word in re.split(r"[\s,;|\t]+", line.strip()):
            value = _to_float(word) if "." in word else None
            if value is not None and 1800.0 <= value <= 30000.0 and freq is None:
                freq = value
            elif call is None and base_call(word) is not None:
                call = word
        if call is None or not _note(calls, call, freq=freq):
            skipped += 1
            if len(examples) < 5:
                examples.append(line.strip()[:60])
    return calls, skipped, examples, lines > 0 and spots * 2 > lines


def read_calls_text(text: str, spot_filter: SpotFilter | None = None) -> CallFile:
    """Reads a call list of any accepted form; see the module's description. spot_filter applies to a CSV only (an RBN day file):
    ValueError for any other form."""
    text = text.lstrip("﻿")
    if _ADIF_CALL.search(text):
        if spot_filter is not None:
            raise ValueError("spot filters apply to a Reverse Beacon Network CSV, not to ADIF")
        return CallFile("adif", _read_adif(text))
    first = next((line for line in text.splitlines() if line.strip()), "")
    if "," in first:
        column = _call_column(next(csv.reader([first], skipinitialspace=True)))
        if column:
            calls, skipped, examples, filtered = _read_csv(text, column, spot_filter)
            return CallFile("csv", calls, skipped, examples, filtered)
    if spot_filter is not None:
        raise ValueError("spot filters apply to a Reverse Beacon Network CSV, not to spots or a plain list")
    calls, skipped, examples, spots = _read_lines(text)
    return CallFile("spots" if spots else "list", calls, skipped, examples)


def read_calls_file(path: str | Path, spot_filter: SpotFilter | None = None) -> CallFile:
    return read_calls_text(Path(path).read_text(encoding="utf-8", errors="replace"), spot_filter)


def edit_distance_one(a: str, b: str) -> bool:
    """Whether a and b differ by exactly one letter changed, added or dropped."""
    if a == b or abs(len(a) - len(b)) > 1:
        return False
    if len(a) == len(b):
        return sum(x != y for x, y in zip(a, b)) == 1
    short, long = (a, b) if len(a) < len(b) else (b, a)
    return any(long[:i] + long[i + 1:] == short for i in range(len(long)))


@dataclass
class Comparison:
    both: list[str]
    only_a: list[str]
    only_b: list[str]
    near_pairs: list[tuple[str, str]]  # a call in A alone, one letter from a call in B alone: perhaps one station


def compare_lists(a: CallFile, b: CallFile) -> Comparison:
    set_a, set_b = set(a.calls), set(b.calls)
    only_a, only_b = sorted(set_a - set_b), sorted(set_b - set_a)
    near = [(x, y) for x in only_a for y in only_b if edit_distance_one(x, y)]
    return Comparison(sorted(set_a & set_b), only_a, only_b, near)


def truth_calls(labels: dict) -> dict[str, list[float]]:
    """Base call -> the frequencies it was sent at, Hz from the center. Handles the labels the repository's recordings have: `stations`
    with `call` (or the call read from the first burst's text) and the generator's `signals`, with `senders` where one signal holds the
    two stations of a QSO on one frequency, with text otherwise."""
    truth: dict[str, list[float]] = {}

    def add(call: str | None, freq: float) -> None:
        base = base_call(call) if call else None
        if base:
            truth.setdefault(base, []).append(freq)

    for station in labels.get("stations", []):
        freq = float(station["freq_hz"])
        if station.get("call"):
            add(station["call"], freq)
            continue
        for burst in station.get("bursts", [])[:1]:
            add(_sender_in(burst.get("text", "")), freq)
    for signal in labels.get("signals", []):
        freq = float(signal.get("freq_offset_hz", 0.0))
        senders = signal.get("senders") or []
        for sender in senders:
            add(sender.get("call"), freq + float(sender.get("freq_offset_hz", 0.0)))
        if not senders:
            add(_sender_in(signal.get("text", "")), freq)
    return truth


def _sender_in(text: str) -> str | None:
    """The sender of a generated text: the word after DE, else the first word with a call's form that is not named by a following DE."""
    words = text.upper().split()
    for i, word in enumerate(words[:-1]):
        if word == "DE" and base_call(words[i + 1]):
            return words[i + 1]
    for i, word in enumerate(words):
        if base_call(word) and not (i + 1 < len(words) and words[i + 1] == "DE"):
            return word
    return None


@dataclass
class TruthScore:
    stations: int
    reported: int
    found: list[str]
    false_calls: list[str]
    missed: list[str]
    misreadings: list[tuple[str, str]]  # a false call and the true call one letter from it
    attributed: int  # found calls reported within attribution_hz of where the station is
    misattributed: list[tuple[str, float, float]]  # call, reported Hz, truth's nearest Hz

    @property
    def precision(self) -> float:
        return len(self.found) / self.reported if self.reported else 0.0

    @property
    def recall(self) -> float:
        return len(self.found) / self.stations if self.stations else 0.0


def score_truth(run: CallFile, truth: dict[str, list[float]], attribution_hz: float = 100.0) -> TruthScore:
    found = sorted(set(run.calls) & set(truth))
    false_calls = sorted(set(run.calls) - set(truth))
    missed = sorted(set(truth) - set(run.calls))
    misreadings = [(f, t) for f in false_calls for t in truth if edit_distance_one(f, t)]
    attributed = 0
    wrong = []
    for call in found:
        entry = run.calls[call]
        if not entry.freqs_khz:
            continue
        reported_hz = entry.freqs_khz[-1] * 1000.0
        nearest = min(truth[call], key=lambda f: abs(f - reported_hz))
        if abs(nearest - reported_hz) <= attribution_hz:
            attributed += 1
        else:
            wrong.append((call, reported_hz, nearest))
    return TruthScore(len(truth), len(run.calls), found, false_calls, missed, misreadings, attributed, wrong)


def _write_rows(path: str | None, header: list[str], rows: list[list[str]]) -> None:
    if not path:
        return
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def _describe(name: str, listed: CallFile) -> str:
    text = f"{name}: adapter {listed.adapter}, {len(listed.calls)} calls, {sum(e.count for e in listed.calls.values())} readings or spots"
    if listed.skipped_lines:
        text += f", {listed.skipped_lines} lines skipped, for example {listed.skipped_examples[:3]}"
    if listed.filtered:
        text += f", {listed.filtered} rows outside the filters"
    return text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    compare = commands.add_parser("compare", help="two call lists: calls in both, in each only, and counts")
    compare.add_argument("run")
    compare.add_argument("other")
    compare.add_argument("--name-a", default="run")
    compare.add_argument("--name-b", default="other")
    compare.add_argument("--out", help="one row per call: call, in which list, counts")
    compare.add_argument("--spotter", action="append", help="OTHER, an RBN day file: keep this skimmer's spots (repeatable)")
    compare.add_argument("--continent", help="OTHER, an RBN day file: keep the spots of skimmers on this continent (de_cont), e.g. NA")
    compare.add_argument("--from", dest="start_utc", help="OTHER, an RBN day file: spots at or after this UTC time")
    compare.add_argument("--to", dest="end_utc", help="OTHER, an RBN day file: spots before this UTC time")
    compare.add_argument("--khz", nargs=2, type=float, metavar=("LOW", "HIGH"), help="OTHER, an RBN day file: spots in this span, kHz")
    compare.add_argument("--tx-mode", help="OTHER, an RBN day file: spots of this mode (tx_mode), e.g. CW")
    truth = commands.add_parser("truth", help="precision and recall on a generated recording")
    truth.add_argument("run")
    truth.add_argument("labels")
    truth.add_argument("--attribution-hz", type=float, default=100.0, help="Hz: a call found counts as attributed within this of its station")
    truth.add_argument("--out", help="one row per call: call, true, false or missed")
    args = parser.parse_args(argv)

    if args.command == "compare":
        spot_filter = None
        if args.spotter or args.continent or args.start_utc or args.end_utc or args.khz or args.tx_mode:
            spot_filter = SpotFilter(
                spotters={s.strip().upper() for s in args.spotter} if args.spotter else None, continent=args.continent,
                start_utc=args.start_utc, end_utc=args.end_utc, low_khz=args.khz[0] if args.khz else None,
                high_khz=args.khz[1] if args.khz else None, tx_mode=args.tx_mode)
        a, b = read_calls_file(args.run), read_calls_file(args.other, spot_filter)
        c = compare_lists(a, b)
        print(_describe(args.name_a, a))
        print(_describe(args.name_b, b))
        print(f"in both {len(c.both)}; in {args.name_a} only {len(c.only_a)}; in {args.name_b} only {len(c.only_b)}")
        print(f"one letter apart, one in each list only (perhaps one station, one misread): {len(c.near_pairs)}")
        for x, y in c.near_pairs:
            print(f"  {x} / {y}")
        print(f"both: {' '.join(c.both)}")
        print(f"{args.name_a} only: {' '.join(c.only_a)}")
        print(f"{args.name_b} only: {' '.join(c.only_b)}")
        rows = [[call, "both", a.calls[call].count, b.calls[call].count] for call in c.both]
        rows += [[call, "a", a.calls[call].count, 0] for call in c.only_a]
        rows += [[call, "b", 0, b.calls[call].count] for call in c.only_b]
        _write_rows(args.out, ["call", "in", f"count_{args.name_a}", f"count_{args.name_b}"], rows)
        return 0

    run = read_calls_file(args.run)
    labels = json.loads(Path(args.labels).read_text(encoding="utf-8"))
    score = score_truth(run, truth_calls(labels), args.attribution_hz)
    print(_describe("run", run))
    print(f"stations {score.stations}; reported {score.reported}; true calls found {len(score.found)}; false calls {len(score.false_calls)}; "
          f"precision {score.precision:.3f}; recall {score.recall:.3f}")
    print(f"found calls within {args.attribution_hz:.0f} Hz of their station: {score.attributed} of {len(score.found)}")
    print(f"false calls one letter from a true call: {len(score.misreadings)}")
    print(f"false: {' '.join(score.false_calls)}")
    for call, reported, nearest in score.misattributed[:40]:
        print(f"  misattributed {call}: reported at {reported:.0f} Hz, station at {nearest:.0f} Hz")
    rows = [[call, "true", run.calls[call].count] for call in score.found]
    rows += [[call, "false", run.calls[call].count] for call in score.false_calls]
    rows += [[call, "missed", 0] for call in score.missed]
    _write_rows(args.out, ["call", "kind", "count"], rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
