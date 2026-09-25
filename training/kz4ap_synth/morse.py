"""Morse code table and PARIS-standard keying timing.

Symbols are single characters ("A", "?") or prosign tokens ("<SK>"). Must stay
identical to engine/src/morse.cpp; tests/test_morse.py compares them.
"""

import re

CODES = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".", "F": "..-.",
    "G": "--.", "H": "....", "I": "..", "J": ".---", "K": "-.-", "L": ".-..",
    "M": "--", "N": "-.", "O": "---", "P": ".--.", "Q": "--.-", "R": ".-.",
    "S": "...", "T": "-", "U": "..-", "V": "...-", "W": ".--", "X": "-..-",
    "Y": "-.--", "Z": "--..",
    "0": "-----", "1": ".----", "2": "..---", "3": "...--", "4": "....-",
    "5": ".....", "6": "-....", "7": "--...", "8": "---..", "9": "----.",
    ".": ".-.-.-", ",": "--..--", ";": "-.-.-.", ":": "---...", "?": "..--..",
    "!": "-.-.--", "'": ".----.", '"': ".-..-.", ")": "-.--.-", "/": "-..-.",
    "-": "-....-", "$": "...-..-", "@": ".--.-.", "_": "..--.-",
    "<AA>": ".-.-", "<AR>": ".-.-.", "<AS>": ".-...", "<BK>": "-...-.-",
    "<BT>": "-...-", "<CL>": "-.-..-..", "<HH>": "........", "<KA>": "-.-.-",
    "<KN>": "-.--.", "<SK>": "...-.-", "<SN>": "...-.", "<SOS>": "...---...",
}

_SYMBOL = re.compile(r"<[^<>]*>|.")


def symbols(word: str) -> list[str]:
    """Split a word into symbols, keeping "<...>" prosign tokens whole."""
    return _SYMBOL.findall(word)


def dit_seconds(wpm: float) -> float:
    """Length of one dit at the given speed (PARIS standard: 50 dits per word)."""
    return 1.2 / wpm


def keying_intervals(text: str, wpm: float) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, starting at time 0.

    Gaps: 1 dit between elements, 3 between characters, 7 between words.
    Symbols with no Morse code are skipped.
    """
    dit = dit_seconds(wpm)
    intervals = []
    t = 0.0
    words = text.upper().split()
    for wi, word in enumerate(words):
        patterns = [CODES[s] for s in symbols(word) if s in CODES]
        for ci, pattern in enumerate(patterns):
            for ei, element in enumerate(pattern):
                length = dit if element == "." else 3 * dit
                intervals.append((t, t + length))
                t += length
                if ei < len(pattern) - 1:
                    t += dit
            if ci < len(patterns) - 1:
                t += 3 * dit
        if wi < len(words) - 1:
            t += 7 * dit
    return intervals
