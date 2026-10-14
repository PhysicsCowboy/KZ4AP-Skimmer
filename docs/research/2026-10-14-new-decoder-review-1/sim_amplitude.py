"""Bias of topic 3's amplitude estimate, as the notes specify it, at its fixed point.

Notes, topic 3 item 2: A^2 = the weighted average of the tap's power minus the noise power, each sample weighted by
its probability of being key-down, judged on the tap itself (carrier plus noise against noise alone, at the current
estimates). The tap: 5 pieces of 3 samples (10 ms) at 1500 samples/s, one output per piece (sliding).

Not specified in the notes, chosen here: the prior probability of key-down in the weights (0.44, the PARIS key-down
fraction, and 0.2 as a sensitivity check); the noise level is taken as known exactly; the estimate is iterated to its
fixed point over the whole record (an infinitely long average), so what remains is bias, not scatter.

Signal: random letters and digits in 5-letter words, exact PARIS timing, hard keying, through the real channel
filter (Blackman-windowed sinc, 4097 taps at 192 kHz, -6 dB at 150 Hz), at 1500 samples/s. Noise: complex white
noise through the same filter. S500 = key-down carrier power over the noise power in 500 Hz.

Run: .venv/Scripts/python docs/research/2026-10-14-new-decoder-review-1/sim_amplitude.py
"""

import math

import numpy as np

FS_IN = 192_000.0
DEC = 128
R = FS_IN / DEC
PIECE = 3
TAP = 5
CODES = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".", "F": "..-.", "G": "--.", "H": "....", "I": "..",
    "J": ".---", "K": "-.-", "L": ".-..", "M": "--", "N": "-.", "O": "---", "P": ".--.", "Q": "--.-", "R": ".-.",
    "S": "...", "T": "-", "U": "..-", "V": "...-", "W": ".--", "X": "-..-", "Y": "-.--", "Z": "--..",
    "0": "-----", "1": ".----", "2": "..---", "3": "...--", "4": "....-", "5": ".....", "6": "-....",
    "7": "--...", "8": "---..", "9": "----.",
}


def taps_h():
    n = np.arange(4097) - 2048
    h = np.sinc(2 * 150.0 * n / FS_IN) * np.blackman(4097)
    return h / h.sum()


def keying(wpm, seconds, rng):
    """Key-down intervals (s) of random 5-letter words, exact PARIS timing."""
    dit = 1.2 / wpm
    t, out = 0.2, []
    letters = list(CODES)
    while t < seconds:
        for li in range(5):
            code = CODES[letters[rng.integers(len(letters))]]
            for ei, el in enumerate(code):
                d = dit if el == "." else 3 * dit
                out.append((t, t + d))
                t += d + (dit if ei < len(code) - 1 else 0)
            t += 3 * dit if li < 4 else 0
        t += 7 * dit
    return out


def log_i0(x):
    x = np.asarray(x, dtype=float)
    small = x < 500
    out = np.empty_like(x)
    out[small] = np.log(np.i0(x[small]))
    xl = x[~small]
    out[~small] = xl - 0.5 * np.log(2 * math.pi * xl) + np.log1p(1 / (8 * xl))
    return out


def main():
    rng = np.random.default_rng(7)
    h = taps_h()
    step_t = (np.arange(len(h)) - 2048) / FS_IN       # step response of the centered (zero-delay) filter
    step_v = np.cumsum(h)

    # noise at 1500 samples/s with the filter's spectrum: white noise at 1500/s through h sampled every 128 taps
    hd = h[np.arange(0, 4097, DEC)]
    hd_var = float(np.sum(h ** 2))                    # per-sample variance of the true channel noise (N0 = 1 per 1/fs)
    n0 = 1 / 500.0                                    # noise density so that the noise power in 500 Hz is 1 FS^2
    sigma_ch2 = n0 * FS_IN * hd_var                   # channel noise variance per sample, FS^2

    # exact tap noise variance from the autocovariance of the filtered noise
    full = np.correlate(h, h, mode="full")
    mid = len(h) - 1
    rk = np.array([full[mid + DEC * k] for k in range(0, 33)]) / full[mid]   # zero beyond lag 32
    n = PIECE * TAP
    vt = sigma_ch2 * (n * rk[0] + 2 * sum((n - k) * rk[k] for k in range(1, n)))

    print(f"channel noise variance per sample {sigma_ch2:.4f} FS^2 (noise power in 500 Hz = 1 FS^2); tap noise "
          f"variance {vt:.3f} FS^2; tap SNR gain over one sample {10 * math.log10(n * n * sigma_ch2 / vt):.2f} dB")
    print()
    print("bias of A^2 at the estimator's fixed point, dB relative to the true A^2 (negative = too low)")
    header = "  speed   " + "".join(f"{s:>9d}" for s in (0, 5, 10, 20, 30)) + "   (S500, dB SNR in 500 Hz)"
    for prior in (0.44, 0.2):
        print(f" prior P(key-down) = {prior}")
        print(header)
        for wpm in (100, 50, 25, 12):
            seconds = max(20.0, 600.0 / wpm)
            iv = keying(wpm, seconds, rng)
            nsamp = int((iv[-1][1] + 0.3) * R)
            t = np.arange(nsamp) / R
            env = np.zeros(nsamp)
            for (a, b) in iv:
                lo, hi = int(max(0, (a - 0.012) * R)), int(min(nsamp, (b + 0.012) * R) + 1)
                tt = t[lo:hi]
                env[lo:hi] += np.interp(tt - a, step_t, step_v, left=0, right=1) - \
                    np.interp(tt - b, step_t, step_v, left=0, right=1)
            white = (rng.standard_normal(nsamp + len(hd)) + 1j * rng.standard_normal(nsamp + len(hd))) / math.sqrt(2)
            noise = np.convolve(white, hd, mode="valid")[:nsamp]
            noise *= math.sqrt(sigma_ch2 / np.var(noise))
            phase = np.exp(1j * 2 * math.pi * rng.random())
            row = []
            for s500 in (0, 5, 10, 20, 30):
                a_true = math.sqrt(10 ** (s500 / 10))       # A^2 = S500 x (noise power in 500 Hz = 1)
                y = a_true * env * phase + noise
                m = len(y) // PIECE * PIECE
                pcs = y[:m].reshape(-1, PIECE).sum(axis=1)
                tap = np.convolve(pcs, np.ones(TAP), mode="valid")
                p2 = np.abs(tap) ** 2
                a2 = a_true ** 2
                for _ in range(200):
                    s = n * math.sqrt(max(a2, 1e-12))
                    llr = -s * s / vt + log_i0(2 * s * np.sqrt(p2) / vt)
                    w = 1 / (1 + (1 - prior) / prior * np.exp(-np.clip(llr, -700, 700)))
                    new = float(np.sum(w * (p2 - vt)) / (n * n * np.sum(w)))
                    if abs(new - a2) < 1e-9 * max(a2, 1e-12):
                        a2 = new
                        break
                    a2 = max(new, 1e-12)
                row.append(10 * math.log10(max(a2, 1e-12) / a_true ** 2))
            print(f"  {wpm:3d} WPM " + "".join(f"{v:9.2f}" for v in row))
        print()


if __name__ == "__main__":
    main()
