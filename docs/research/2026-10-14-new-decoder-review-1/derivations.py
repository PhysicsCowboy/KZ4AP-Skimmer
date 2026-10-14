"""Numerical checks for review 1 of docs/design/2026-10-09-new-decoder-design-notes.md.

Run: .venv/Scripts/python docs/research/2026-10-14-new-decoder-review-1/derivations.py  (numpy only; about a minute)

Units: time in s or ms as named, frequency in Hz, amplitudes in FS (arbitrary here), powers in FS^2.
Every ratio printed in dB says what it is a ratio of.
"""

import math

import numpy as np

FS_IN = 192_000.0      # input rate, samples/s (signal-processing.md section 0)
DEC = 128              # channelizer decimation
R = FS_IN / DEC        # channel rate, 1500 samples/s
PIECE = 3              # samples per 2 ms piece (notes, topic 4 item 1)
TAP_PIECES = 5         # the tap: 5 pieces, 10 ms (notes, topic 3 item 1)


def section(title):
    print()
    print("=" * 100)
    print(title)
    print("=" * 100)


# ---------------------------------------------------------------------------------------------------------
# The two channel models
# ---------------------------------------------------------------------------------------------------------

def ideal_autocov(kmax, bw=300.0):
    """Ideal brick-wall channel of total width bw Hz (+-bw/2), white input: R[k]/R[0] = sinc(bw*k/R)."""
    k = np.arange(-kmax, kmax + 1)
    return k, np.sinc(bw * k / R)


def real_filter_taps():
    """Blackman-windowed sinc, 4097 taps at 192 kHz, cutoff 150 Hz (-6 dB), unity DC gain (A.7)."""
    ntaps = 4097
    n = np.arange(ntaps) - (ntaps - 1) / 2
    fc = 150.0
    h = np.sinc(2 * fc * n / FS_IN) * np.blackman(ntaps)
    return h / h.sum()


def real_autocov(h, kmax):
    """Autocovariance of white noise through h, decimated by DEC, normalized to R[0] = 1."""
    full = np.correlate(h, h, mode="full")          # lags -(L-1)..(L-1) at 192 kHz
    mid = len(h) - 1
    k = np.arange(-kmax, kmax + 1)
    lags = mid + DEC * k
    vals = np.where((lags >= 0) & (lags < len(full)), full[np.clip(lags, 0, len(full) - 1)], 0.0)
    return k, vals / full[mid]


def var_of_sum(rk, kk, n):
    """Variance of a sum of n consecutive samples, given normalized autocovariance rk at lags kk."""
    m = np.abs(kk)
    w = np.clip(n - m, 0, None)
    return float(np.sum(w * rk))


# ---------------------------------------------------------------------------------------------------------
section("0. The real channel filter, reproduced from A.7 (check against the documented values)")
h = real_filter_taps()
nfft = 1 << 22
H = np.fft.rfft(h, nfft)
f = np.fft.rfftfreq(nfft, 1 / FS_IN)
mag_db = 20 * np.log10(np.maximum(np.abs(H), 1e-12))
for f0 in (30, 50, 75, 100, 125, 150, 200, 250, 280):
    i = int(round(f0 / (f[1] - f[0])))
    print(f"  response at {f0:4d} Hz: {mag_db[i]:8.2f} dB relative to 0 Hz")
nbw = float(np.sum(h ** 2) * FS_IN)
print(f"  noise bandwidth (two-sided, complex), sum(h^2)*fs = {nbw:.1f} Hz  (A.7 says 252 Hz)")

KMAX = 2000
k_id, r_id = ideal_autocov(KMAX)
k_re, r_re = real_autocov(h, KMAX)

# ---------------------------------------------------------------------------------------------------------
section("1. Correlation time of the channel noise (notes L132: 'correlated for about 3.3 ms')")
print("  ideal 300 Hz channel: first zero of sinc(300 Hz * tau) at 1/300 Hz = 3.33 ms (exact)")
tau = np.arange(0, 0.02, 1 / FS_IN)
full = np.correlate(h, h, mode="full")
mid = len(h) - 1
rr = full[mid:mid + len(tau)] / full[mid]
z = np.argmax(rr < 0)
print(f"  real filter: first zero of the noise autocorrelation at {tau[z] * 1e3:.2f} ms")
print("  normalized autocovariance at channel-sample lags (1 sample = 0.667 ms):")
for k in range(0, 8):
    print(f"    lag {k} ({k / R * 1e3:5.2f} ms): ideal {r_id[KMAX + k]:+.4f}   real {r_re[KMAX + k]:+.4f}")

# ---------------------------------------------------------------------------------------------------------
section("2. Pieces treated as independent: understatement of a mark's noise variance (notes L150-152)")
print("  factor = var(sum of the mark's 3n samples) / (n * var(one 3-sample piece))")
print(f"  {'mark':>8} {'pieces':>6} | {'ideal 300 Hz':>12} {'dB':>6} | {'real filter':>11} {'dB':>6}")
vp_id = var_of_sum(r_id, k_id, PIECE)
vp_re = var_of_sum(r_re, k_re, PIECE)
for ms in (8, 12, 16, 24, 36, 48, 100, 144, 240, 720):
    n = int(round(ms / 2))
    a = var_of_sum(r_id, k_id, PIECE * n) / (n * vp_id)
    b = var_of_sum(r_re, k_re, PIECE * n) / (n * vp_re)
    print(f"  {ms:6d} ms {n:6d} | {a:12.3f} {10 * math.log10(a):6.2f} | {b:11.3f} {10 * math.log10(b):6.2f}")
print("  long-mark limit (ratio of DC density x 3 to piece variance):")
lim_id = 3 * float(np.sum(r_id)) / vp_id
lim_re = 3 * float(np.sum(r_re)) / vp_re
print(f"    ideal {lim_id:.3f} ({10 * math.log10(lim_id):.2f} dB), real {lim_re:.3f} ({10 * math.log10(lim_re):.2f} dB)")

# ---------------------------------------------------------------------------------------------------------
section("3. 'Variance proportional to T' (notes L150-151): exact variance / (N0 * T) for short sums")
print("  ratio = var(sum over T) / (T * r * sum_k R[k]);  1.000 means proportional to T is exact")
for ms in (2, 4, 6, 8, 10, 12, 16, 24, 48):
    n = int(round(ms * 1e-3 * R))
    a = var_of_sum(r_id, k_id, n) / (n * float(np.sum(r_id)))
    b = var_of_sum(r_re, k_re, n) / (n * float(np.sum(r_re)))
    print(f"  T = {ms:3d} ms ({n:3d} samples): ideal {a:.3f} ({10 * math.log10(a):+.2f} dB)   real {b:.3f} ({10 * math.log10(b):+.2f} dB)")

# ---------------------------------------------------------------------------------------------------------
section("4. The 10 ms tap's noise relative to one raw channel sample (notes L111: 'about 4.6 dB less')")
print("  SNR gain of the 15-sample sum over one sample = 15^2 * R[0] / var(sum of 15 samples)")
for name, rk, kk, bw in (("ideal 300 Hz", r_id, k_id, 300.0), ("real filter", r_re, k_re, nbw)):
    g = 225.0 / var_of_sum(rk, kk, 15)
    print(f"  {name:13s}: {g:.3f} = {10 * math.log10(g):.2f} dB   (simple estimate 10*log10({bw:.0f} Hz / 100 Hz) = "
          f"{10 * math.log10(bw / 100.0):.2f} dB)")

# ---------------------------------------------------------------------------------------------------------
section("5. Phase rotation: loss of a coherent whole-mark sum, |sinc(df*T)|^2, dB relative to df = 0")
marks = (("dash 100 WPM", 0.036), ("dash 25 WPM", 0.144), ("dash 20 WPM", 0.180), ("dash 13 WPM", 0.277),
         ("dash 8 WPM", 0.450), ("dash 5 WPM", 0.720))
dfs = (0.1, 0.2, 0.5, 1.0, 2.0, 5.9, 11.7)
print("  " + " " * 14 + "".join(f"{d:>9.1f}" for d in dfs) + "   (residual offset, Hz)")
for name, T in marks:
    row = []
    for d in dfs:
        s = np.sinc(d * T)
        row.append(10 * math.log10(max(s * s, 1e-12)))
    print(f"  {name:14s}" + "".join(f"{v:9.1f}" for v in row))
print("  (0.5 Hz on a 720 ms dash: df*T = 0.36 cycles, the notes' 'about a third of a cycle'.)")

# ---------------------------------------------------------------------------------------------------------
section("6. Farnsworth gap factor F = T_g / dit = (60c/s - 37.2) / 22.8 (ARRL/Bloom 1990; generator)")


def farn(c, s):
    return (60.0 * c / s - 37.2) / 22.8


print(f"  letters 18 WPM, overall 5 WPM: F = {farn(18, 5):.3f}   (notes L86: about 7.8)")
print(f"  letters 25 WPM, overall 5 WPM: F = {farn(25, 5):.3f}")
print(f"  letters 33 WPM, overall 5 WPM: F = {farn(33, 5):.3f}")
print(f"  ratio 8^(1/4) = {8 ** 0.25:.4f}; grid {[round(8 ** (i / 4), 3) for i in range(5)]}")
rng = np.random.default_rng(1)
tot, over8 = 0, 0
cells_c = ((18, 25), (25, 33))
cells_s = ((5, 8), (8, 12), (12, 16))
print("  development set I2 cells (uniform draws within each cell; the generator's own draw law may differ):")
for c0, c1 in cells_c:
    for s0, s1 in cells_s:
        c = rng.uniform(c0, c1, 200_000)
        s = rng.uniform(s0, s1, 200_000)
        F = farn(c, s)
        frac = float(np.mean(F > 8.0))
        tot += 1
        print(f"    letters {c0}-{c1} WPM x overall {s0}-{s1} WPM: F {F.min():5.2f}-{F.max():5.2f}, share above 8: {frac:5.1%}")

# ---------------------------------------------------------------------------------------------------------
section("7. Speed ladder (notes L77-79) and the fist margins")
d = [9.6e-3 * 1.1 ** (k - 1) / 0.8 for k in range(1, 33)]
print(f"  dits {d[0] * 1e3:.2f} ms ({1.2 / d[0]:.2f} WPM) to {d[-1] * 1e3:.2f} ms ({1.2 / d[-1]:.2f} WPM)")
print(f"  largest speed error on the grid (half a step): {100 * (math.sqrt(1.1) - 1):.2f} %  (ln: {0.5 * math.log(1.1):.4f})")
for name, ratio in (("dot/dash and element/letter gap (x3)", 3.0), ("letter/word gap (x7/3)", 7 / 3)):
    half = 0.5 * math.log(ratio)
    p = 0.5 * math.erfc(half / 0.3 / math.sqrt(2))
    print(f"  {name}: half margin {half:.3f} in ln; with sigma 0.3, timing-only confusion {p:.1%} per element")


def phi(x):
    return 0.5 * math.erfc(-x / math.sqrt(2))


print("  share of dots shorter than the 10 ms tap if the sender's scatter were sigma = 0.3 (ln):")
for wpm in (100, 80, 60):
    dit = 1.2 / wpm
    print(f"    {wpm} WPM (dit {dit * 1e3:.0f} ms): {phi(math.log(0.010 / dit) / 0.3):.1%}")

# ---------------------------------------------------------------------------------------------------------
section("8. CPU arithmetic (notes L202-206)")
pieces_per_s = 500
n_hyp = 42e-3 / (pieces_per_s * 50e-9)
print(f"  hypotheses allowed by 42 ms per channel-second at 50 ns per hypothesis-piece: {n_hyp:.0f}")
for ops in (10e6, 25e6):
    print(f"  {ops / 1e6:.0f} M operations per channel-second at {n_hyp:.0f} hypotheses: "
          f"{ops / (n_hyp * pieces_per_s):.1f} operations per hypothesis-piece")
for nh in (100, 300):
    print(f"  {nh} hypotheses at 12-30 operations per hypothesis-piece: "
          f"{nh * 500 * 12 / 1e6:.1f}-{nh * 500 * 30 / 1e6:.1f} M operations per channel-second")

# ---------------------------------------------------------------------------------------------------------
section("9. Edge placement to whole 2 ms pieces: SNR loss of the best piece-aligned sum (real-filter noise)")
print("  rectangular mark, edges at a uniformly random position inside a piece; loss relative to a sum over")
print("  exactly the mark's samples; the best (first, last) piece pair chosen for each position; no edge smoothing")


def piece_loss(T_ms, rk, kk, steps=60):
    nsamp = T_ms * 1e-3 * R
    losses = []
    for u in np.linspace(0, PIECE, steps, endpoint=False):     # mark start, in samples, inside piece 0
        start, end = u, u + nsamp
        ideal_n = int(round(nsamp))
        snr_ideal = ideal_n ** 2 / var_of_sum(rk, kk, ideal_n)
        best = 0.0
        p0 = int(start // PIECE)
        p1 = int(math.ceil(end / PIECE))
        for a in range(max(0, p0 - 1), p0 + 2):
            for b in range(max(a + 1, p1 - 2), p1 + 2):
                lo, hi = a * PIECE, b * PIECE
                overlap = max(0.0, min(hi, end) - max(lo, start))
                n = hi - lo
                snr = overlap ** 2 / var_of_sum(rk, kk, n)
                best = max(best, snr)
        losses.append(10 * math.log10(snr_ideal / best))
    return float(np.mean(losses)), float(np.max(losses))


for T_ms in (12, 15, 24, 36, 48, 144):
    m, w = piece_loss(T_ms, r_re, k_re)
    print(f"  {T_ms:4d} ms mark: mean loss {m:.2f} dB, worst {w:.2f} dB")

# ---------------------------------------------------------------------------------------------------------
section("10. The tap through the real channel: largest tap output on one mark, relative to 15*A")


def burst_through_channel(L_s, edge_s, offset_in):
    """A carrier burst at 0 Hz (50%-amplitude length L_s, raised-cosine edges of edge_s), through h, decimated."""
    pad = 0.03
    n = int((L_s + 2 * pad + 2 * edge_s) * FS_IN) + 1
    t = np.arange(n) / FS_IN - pad
    x = np.zeros(n)
    on = (t >= 0) & (t <= L_s)
    x[on] = 1.0
    if edge_s > 0:
        for t0, sgn in ((0.0, 1), (L_s, -1)):
            m = np.abs(t - t0) < edge_s / 2
            x[m] = 0.5 + 0.5 * sgn * np.sin(math.pi * (t[m] - t0) / edge_s)
    y = np.convolve(x, h, mode="same")
    return y[offset_in::DEC]


for L_ms in (10, 12, 15, 20, 24, 48):
    best_min, best_mean = 1.0, []
    for edge in (0.0, 0.002):
        vals = []
        for off in range(0, DEC, 16):
            y = burst_through_channel(L_ms * 1e-3, edge, off)
            for ph in range(PIECE):
                pcs = np.add.reduceat(y[ph:len(y) - (len(y) - ph) % PIECE], np.arange(0, len(y) - ph - (len(y) - ph) % PIECE, PIECE))
                taps = np.convolve(pcs, np.ones(TAP_PIECES), mode="valid")
                vals.append(taps.max() / 15.0)
        vals = np.array(vals)
        print(f"  mark {L_ms:3d} ms, edges {'hard' if edge == 0 else '2 ms raised-cosine'}: largest tap / (15 A) "
              f"min {vals.min():.3f}, mean {vals.mean():.3f}  (power {20 * math.log10(vals.min()):+.2f} to "
              f"{20 * math.log10(vals.mean()):+.2f} dB relative to A^2)")

# ---------------------------------------------------------------------------------------------------------
section("11. Piece length trade-off: mean / worst edge-placement loss for pieces of 2, 3 and 4 samples")
for ps in (2, 3, 4):
    PIECE = ps
    row = []
    for T_ms in (12, 24, 48):
        m, w = piece_loss(T_ms, r_re, k_re)
        row.append(f"{T_ms} ms: {m:.2f}/{w:.2f} dB")
    print(f"  piece {ps} samples ({ps / R * 1e3:.2f} ms, {R / ps:.0f} pieces/s): " + ";  ".join(row))
PIECE = 3

# ---------------------------------------------------------------------------------------------------------
section("12. Fading inside a mark: expected energy of a coherent whole-mark sum, relative to no fading")
print("  flat Rayleigh fading with a Gaussian Doppler spectrum of standard deviation f_D/2 (the 2-sigma convention")
print("  of the development set's B2 group; VE3NEA's Butterworth spectrum is not exactly Gaussian: approximate)")
print("  E|sum|^2 / (n^2 E|g|^2) = (2/T^2) * integral_0^T (T - tau) * rho(tau) dtau, rho = exp(-2 pi^2 sigma^2 tau^2)")
fds = (0.1, 0.3, 1.0, 2.0, 5.0)
print("  " + " " * 14 + "".join(f"{x:>8.1f}" for x in fds) + "   (f_D, Hz)")
for name, T in marks:
    vals = []
    for fd in fds:
        sig = fd / 2
        tt = np.linspace(0, T, 4001)
        rho = np.exp(-2 * math.pi ** 2 * sig ** 2 * tt ** 2)
        trap = getattr(np, "trapezoid", None) or getattr(np, "trapz")
        g = 2 / T ** 2 * trap((T - tt) * rho, tt)
        vals.append(10 * math.log10(g))
    print(f"  {name:14s}" + "".join(f"{v:8.1f}" for v in vals) + "   dB")

print()
print("done")
