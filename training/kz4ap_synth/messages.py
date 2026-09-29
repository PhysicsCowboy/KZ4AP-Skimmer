"""Message text for synthetic CW: CQ calls, contest exchanges, ragchew QSOs,
and statistical filler text.

The templates, callsigns, names, places and the order of a QSO are this
project's own. The filler text (random_text) draws characters and word
lengths from VE3NEA's on-air tables, reproduced below under the MIT license.
Every function takes a numpy Generator and draws only from it, so the same
generator state always gives the same text.
"""

from __future__ import annotations

import re
from dataclasses import dataclass

import numpy as np

# VE3NEA_CHAR_WEIGHTS and VE3NEA_WORD_LENGTH_PROBS are copied from VE3NEA's
# DeepCW (https://github.com/VE3NEA/DeepCW, commit
# 2c8fdac01bb2bf07d80989b0e5aabfe8cb87d76b, data_generation.ipynb cell 3,
# morse_code.py), where they are said to be "collected from a large number of
# CW messages decoded with CW Skimmer on the Ham bands". His "=" (-...-) is
# written "<BT>" here. Notes: docs/research/deepcw-generator-notes.md §4.
#
# MIT License
#
# Copyright (c) 2024 Alex Shovkoplyas
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
VE3NEA_CHAR_WEIGHTS = {
    "1": 13, "2": 14, "3": 33, "4": 43, "5": 41, "6": 8, "7": 14, "8": 10, "9": 14, "0": 11,
    "A": 127, "B": 62, "C": 69, "D": 84, "E": 321, "F": 55, "G": 43, "H": 68, "I": 130,
    "J": 8, "K": 117, "L": 100, "M": 76, "N": 168, "O": 126, "P": 57, "Q": 68, "R": 95,
    "S": 159, "T": 236, "U": 61, "V": 23, "W": 95, "X": 16, "Y": 40, "Z": 12,
    "/": 19, ".": 12, ",": 9, "?": 16, "<BT>": 15,
}
VE3NEA_WORD_LENGTH_PROBS = [0.0, 0.1672, 0.2569, 0.1939, 0.1745, 0.0921, 0.025, 0.008, 0.006,
                            0.004, 0.003, 0.003, 0.002, 0.002, 0.002, 0.001, 0.001]  # index = characters
# End of the material copied from DeepCW.

LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
US_PREFIXES_1 = ("K", "N", "W")
US_PREFIXES_2 = ("AA", "AB", "AC", "AD", "AE", "AF", "AG", "AI", "AJ", "AK", "KA", "KB", "KC", "KD",
                 "KE", "KF", "KG", "KI", "KJ", "KK", "KN", "KO", "NA", "NB", "NC", "WA", "WB", "WD")
DX_QTHS = {"DL": "MUNICH", "G": "LEEDS", "F": "LYON", "JA": "OSAKA", "VE": "OTTAWA", "EA": "MADRID",
           "I": "ROME", "OH": "HELSINKI", "SM": "UPPSALA", "UA": "MOSCOW", "PY": "RIO", "VK": "PERTH",
           "ZL": "AUCKLAND", "LU": "CORDOBA", "ON": "GHENT", "PA": "UTRECHT", "OK": "BRNO",
           "SP": "KRAKOW", "HA": "BUDAPEST", "YO": "CLUJ"}
US_QTHS = ("BOSTON MA", "AUSTIN TX", "DENVER CO", "TULSA OK", "OMAHA NE", "RENO NV", "MACON GA",
           "BANGOR ME", "FRESNO CA", "DAYTON OH", "BOISE ID", "SALEM OR")
NAMES = ("JOHN", "BOB", "JIM", "TOM", "BILL", "MIKE", "DAVE", "STEVE", "ED", "AL", "DON", "JOE",
         "KEN", "RON", "PAT", "SUE", "ANN", "MARY", "HANS", "PETE")
RIGS = ("K3", "IC7300", "FT991", "TS590", "KX3", "FT710", "IC705", "HOMEBREW")
ANTENNAS = ("DIPOLE", "VERTICAL", "3 EL YAGI", "EFHW", "LOOP", "INV VEE", "LONG WIRE")
POWERS_W = (5, 10, 50, 100, 500)
WEATHER = ("SUNNY", "CLOUDY", "RAIN", "SNOW", "WINDY", "FOGGY", "CLEAR")
STATES = ("MA", "TX", "CO", "OK", "NE", "NV", "GA", "ME", "CA", "OH", "ID", "OR")
CHAT_LINES = ("BEEN LICENSED {years} YRS", "UR SIGS FB HR", "SOME QSB ON UR SIG", "BAND CONDX GUD TDY",
              "RETIRED NW ES ENJOY CW", "AGE HR {age}", "QRM HR BUT UR OK")

_CALL = re.compile(r"^([A-Z]+)\d[A-Z]{1,3}(/P)?$")


@dataclass(frozen=True)
class Operator:
    """Who sends: the facts a ragchew exchanges. Speed and keying belong to the generator."""
    call: str
    name: str
    qth: str
    rig: str
    antenna: str     # includes the height where one is sent, e.g. "DIPOLE UP 40 FT"
    power_w: int     # transmitter output power, W


@dataclass(frozen=True)
class Over:
    """One station's turn in a QSO, from its first key-down to the hand-over."""
    sender: int      # 0: the station that called CQ; 1: the station that answered
    text: str


def _pick(rng, options):
    return options[int(rng.integers(len(options)))]


def _letters(rng, count: int) -> str:
    return "".join(LETTERS[int(i)] for i in rng.integers(26, size=count))


def callsign(rng) -> str:
    """A plausible amateur callsign: a US 1x2, 1x3, 2x1, 2x2 or 2x3 call, or a DX prefix,
    digit and 1-3 letters; one call in 20 is portable (/P)."""
    kind = int(rng.integers(6))
    digit = str(int(rng.integers(10)))
    if kind == 0:
        call = _pick(rng, US_PREFIXES_1) + digit + _letters(rng, 2)
    elif kind == 1:
        call = _pick(rng, US_PREFIXES_1) + digit + _letters(rng, 3)
    elif kind == 2:
        call = _pick(rng, US_PREFIXES_2) + digit + _letters(rng, int(rng.integers(1, 4)))
    else:
        call = _pick(rng, tuple(DX_QTHS)) + digit + _letters(rng, int(rng.integers(1, 4)))
    return call + ("/P" if rng.random() < 0.05 else "")


def prefix(call: str) -> str:
    """The letters before the call's digit."""
    match = _CALL.match(call)
    if not match:
        raise ValueError(f"not a callsign: {call!r}")
    return match.group(1)


def random_operator(rng) -> Operator:
    call = callsign(rng)
    qth = DX_QTHS.get(prefix(call)) or _pick(rng, US_QTHS)
    antenna = _pick(rng, ANTENNAS)
    if rng.random() < 0.5:
        antenna += f" UP {int(rng.integers(3, 16)) * 5} FT"
    return Operator(call, _pick(rng, NAMES), qth, _pick(rng, RIGS), antenna, int(_pick(rng, POWERS_W)))


def cq_call(rng, call: str) -> str:
    """A general call: CQ two or three times, DE, the call two or three times, K."""
    cq = " ".join(["CQ"] * int(rng.integers(2, 4)))
    calls = " ".join([call] * int(rng.integers(2, 4)))
    return f"{cq} DE {calls} K"


def contest_exchange(rng, call: str, other: str) -> str:
    """One contest transmission by call, working other: a CQ, a report with a serial
    number, zone or state, or a TU."""
    kind = int(rng.integers(5))
    if kind == 0:
        return f"CQ TEST {call} {call}"
    if kind == 1:
        return f"{other} 5NN {int(rng.integers(1, 1000))}"
    if kind == 2:
        return f"{other} 5NN {int(rng.integers(1, 41)):02d}"
    if kind == 3:
        return f"{other} 5NN {_pick(rng, STATES)}"
    return f"TU {call}"


def _rst(rng) -> str:
    return f"{int(rng.integers(3, 6))}{int(rng.integers(3, 10))}9"


def _greeting(rng) -> str:
    return _pick(rng, ("GM", "GA", "GE"))


def _temperature(rng, qth: str) -> str:
    if qth in DX_QTHS.values():
        return f"TEMP {int(rng.integers(0, 36))}C"
    return f"TEMP {int(rng.integers(32, 96))}F"


def _chat(rng) -> str:
    """Two different lines of chat, separated by <BT>."""
    first, second = rng.choice(len(CHAT_LINES), size=2, replace=False)
    return " <BT> ".join(CHAT_LINES[int(i)].format(years=int(rng.integers(2, 60)), age=int(rng.integers(16, 90)))
                         for i in (first, second))


def ragchew(rng, a: Operator, b: Operator, chat_rounds: int = 0) -> list[Over]:
    """A whole ragchew QSO: a calls CQ, b answers, then they alternate overs: reports
    and names and QTHs, rigs, antennas, power and weather, chat_rounds rounds of chat,
    and the closing. <BT> separates thoughts inside an over; every over before the
    closing ends by handing over with <AR> or <KN>; each closing over ends with <SK>."""
    ga, gb = _greeting(rng), _greeting(rng)
    rst_a, rst_b = _rst(rng), _rst(rng)
    overs = [
        Over(0, cq_call(rng, a.call)),
        Over(1, f"{a.call} DE {b.call} {b.call} <AR>"),
        Over(0, f"{b.call} DE {a.call} <BT> {ga} OM ES TNX FER CALL <BT> UR RST {rst_b} {rst_b} "
                f"<BT> NAME HR {a.name} {a.name} <BT> QTH {a.qth} {a.qth} <BT> HW CPY? "
                f"<AR> {b.call} DE {a.call} <KN>"),
        Over(1, f"{a.call} DE {b.call} <BT> R R {gb} {a.name} TNX FER RPT <BT> UR RST {rst_a} {rst_a} "
                f"<BT> NAME HR {b.name} {b.name} <BT> QTH {b.qth} {b.qth} <BT> HW? "
                f"<AR> {a.call} DE {b.call} <KN>"),
        Over(0, f"{b.call} DE {a.call} <BT> R FB {b.name} TNX FER RPT ES INFO <BT> RIG HR {a.rig} "
                f"ES PWR {a.power_w}W <BT> ANT {a.antenna} <BT> WX HR {_pick(rng, WEATHER)} ES "
                f"{_temperature(rng, a.qth)} <AR> {b.call} DE {a.call} <KN>"),
        Over(1, f"{a.call} DE {b.call} <BT> FB OM {a.name} <BT> RIG HR {b.rig} ES PWR {b.power_w}W "
                f"<BT> ANT {b.antenna} <BT> WX HR {_pick(rng, WEATHER)} {_temperature(rng, b.qth)} "
                f"<AR> {a.call} DE {b.call} <KN>"),
    ]
    for _ in range(chat_rounds):
        for sender, me, other in ((0, a, b), (1, b, a)):
            overs.append(Over(sender, f"{other.call} DE {me.call} <BT> {_chat(rng)} "
                                      f"<AR> {other.call} DE {me.call} <KN>"))
    overs += [
        Over(0, f"{b.call} DE {a.call} <BT> OK {b.name} TNX FER FB QSO ES HPE CUAGN <BT> 73 ES GL "
                f"{b.call} DE {a.call} <SK>"),
        Over(1, f"{a.call} DE {b.call} <BT> TNX {a.name} FER QSO <BT> 73 GL OM {a.call} DE {b.call} <SK>"),
    ]
    return overs


def random_text(rng, words: int) -> str:
    """words words of i.i.d. characters from VE3NEA's on-air character frequencies, with
    word lengths from his word-length distribution (mean 3.06 characters)."""
    lengths_p = np.array(VE3NEA_WORD_LENGTH_PROBS) / np.sum(VE3NEA_WORD_LENGTH_PROBS)
    lengths = rng.choice(len(lengths_p), size=words, p=lengths_p)
    chars = list(VE3NEA_CHAR_WEIGHTS)
    weights = np.array([VE3NEA_CHAR_WEIGHTS[c] for c in chars], dtype=float)
    drawn = rng.choice(len(chars), size=int(np.sum(lengths)), p=weights / np.sum(weights))
    out, pos = [], 0
    for n in lengths:
        out.append("".join(chars[int(i)] for i in drawn[pos:pos + n]))
        pos += n
    return " ".join(out)
