"""Morse code table and PARIS-standard keying timing.

Must stay in step with engine/src/morse.cpp.
"""

CODES = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".", "F": "..-.",
    "G": "--.", "H": "....", "I": "..", "J": ".---", "K": "-.-", "L": ".-..",
    "M": "--", "N": "-.", "O": "---", "P": ".--.", "Q": "--.-", "R": ".-.",
    "S": "...", "T": "-", "U": "..-", "V": "...-", "W": ".--", "X": "-..-",
    "Y": "-.--", "Z": "--..",
    "0": "-----", "1": ".----", "2": "..---", "3": "...--", "4": "....-",
    "5": ".....", "6": "-....", "7": "--...", "8": "---..", "9": "----.",
    "/": "-..-.", "?": "..--..", ".": ".-.-.-", ",": "--..--",
    "=": "-...-", "+": ".-.-.", "-": "-....-", "(": "-.--.",
}


def dit_seconds(wpm: float) -> float:
    """Length of one dit at the given speed (PARIS standard: 50 dits per word)."""
    return 1.2 / wpm


def keying_intervals(text: str, wpm: float) -> list[tuple[float, float]]:
    """Key-down intervals (start_s, end_s) for text, starting at time 0.

    Gaps: 1 dit between elements, 3 between characters, 7 between words.
    Characters with no Morse code are skipped.
    """
    dit = dit_seconds(wpm)
    intervals = []
    t = 0.0
    words = text.upper().split()
    for wi, word in enumerate(words):
        patterns = [CODES[c] for c in word if c in CODES]
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
