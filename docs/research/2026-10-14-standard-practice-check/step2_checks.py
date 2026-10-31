"""Step 2: independent re-derivation of numbers the design notes attribute to review 1 (numpy only).

Run: .venv/Scripts/python -I docs/research/2026-10-14-standard-practice-check/step2_checks.py
Sections C1..C12 are cited by number in step-2-comparison.md. Every printed quantity names its unit.

Channel model (from docs/signal-processing.md, A.7): Blackman-windowed sinc low-pass, 4097 taps at
fs = 192 000 samples/s, cutoff 150 Hz (-6 dB re passband at the cutoff, a property of a windowed sinc),
unity gain at 0 Hz, decimated by 128 to r = 1500 samples/s. A "piece" is the sum of 3 channel samples (2 ms).
"""
import math

import numpy as np

FS_HI = 192_000.0  # input rate, samples/s
DEC = 128  # decimation factor
R = FS_HI / DEC  # channel rate, 1500 samples/s
FC = 150.0  # cutoff, Hz
NTAP = 4097
PIECE = 3  # channel samples per piece (2 ms)


def db(x):
    return 10.0 * np.log10(x)


def channel_filter():
    n = np.arange(NTAP) - (NTAP - 1) / 2.0
    h = (2 * FC / FS_HI) * np.sinc(2 * FC * n / FS_HI) * np.blackman(NTAP)
    return h / h.sum()


H = channel_filter()
# noise autocorrelation of the decimated channel noise, complex white input of unit variance per input sample
R_HI = np.correlate(H, H, mode="full")  # lag 0 at index NTAP-1
MID = NTAP - 1
KMAX = (NTAP - 1) // DEC
R_CH = np.array([R_HI[MID + DEC * k] for k in range(KMAX + 1)])  # channel-rate autocovariance, k >= 0
B_N = FS_HI * np.sum(H**2) / np.sum(H) ** 2  # noise bandwidth, Hz


def var_sum(n, rk=None):
    """Exact variance of a sum of n consecutive channel samples: sum_{|k|<n} (n-|k|) R[k]."""
    if rk is None:
        rk = R_CH
    k = np.arange(1, min(n, len(rk)))
    return n * rk[0] + 2.0 * np.sum((n - k) * rk[k])


VAR_CACHE = {}


def var_sum_c(n):
    if n not in VAR_CACHE:
        VAR_CACHE[n] = var_sum(n)
    return VAR_CACHE[n]


def step_response():
    return np.cumsum(H)  # response to a unit step starting at high-rate index 0, value at index i


STEP = step_response()


def channel_pulse(t_on_hi, dur_hi, n_ch):
    """Channel samples (real, carrier at 0 Hz, amplitude 1) of a rectangular mark starting at high-rate
    index t_on_hi and lasting dur_hi high-rate samples. Channel sample m is taken at high-rate index
    m*DEC + (NTAP-1)//2 so that the filter's delay is removed."""
    def s(x):
        if x < 0:
            return 0.0
        if x >= NTAP:
            return 1.0
        return STEP[int(x)]

    out = np.zeros(n_ch)
    for m in range(n_ch):
        t = m * DEC + (NTAP - 1) // 2  # output time whose input time is m*DEC (delay removed)
        out[m] = s(t - t_on_hi) - s(t - t_on_hi - dur_hi)
    return out


def main():
    print("C1. Channel filter as built here (A.7 description)")
    f = np.linspace(0, 750, 7501)
    Hf = np.abs(np.array([np.sum(H * np.exp(-2j * np.pi * ff * (np.arange(NTAP) - MID) / FS_HI)) for ff in (0, 39, 150, 279)]))
    print(f"  |H| at 0, 39, 150, 279 Hz: {', '.join(f'{db(x**2):.3f}' for x in Hf)} dB re passband")
    print(f"  noise bandwidth = {B_N:.1f} Hz (A.7: 252 Hz)")
    rho = R_CH / R_CH[0]
    print("  noise correlation at channel lags 0..8 samples (0.67 ms each): " + ", ".join(f"{x:+.3f}" for x in rho[:9]))
    # first zero crossing in continuous lag, from the high-rate autocorrelation
    rr = R_HI[MID:] / R_HI[MID]
    zc = np.argmax(rr < 0)
    print(f"  first zero of the noise autocorrelation at lag {1e3*zc/FS_HI:.2f} ms (ideal 300 Hz brick wall: 1/300 Hz = 3.33 ms)")

    print("\nC2. Variance of a sum of n samples vs treating 2 ms pieces as independent (notes, topic 4 item 7, 'D2')")
    vp = var_sum(PIECE)
    for ms in (8, 12, 24, 48, 120, 360, 720):
        n = int(round(ms * 1e-3 * R))
        n -= n % PIECE
        exact = var_sum(n)
        indep = (n // PIECE) * vp
        prop_T = n * (R_CH[0] + 2 * np.sum(R_CH[1:]))  # 'proportional to T' (PSD at 0 Hz)
        print(f"  {ms:4d} ms ({n:4d} samples): exact/independent-pieces = {exact/indep:5.3f} ({db(exact/indep):4.2f} dB);"
              f" 'proportional to T'/exact = {prop_T/exact:5.3f} ({db(prop_T/exact):+5.2f} dB)")
    # ideal 300 Hz brick-wall channel for comparison
    k = np.arange(0, 2000)
    rk_ideal = np.sinc(300.0 * k / R)
    vpi = var_sum(PIECE, rk_ideal)
    vals = []
    for ms in (8, 12, 48, 720):
        n = int(round(ms * 1e-3 * R)); n -= n % PIECE
        vals.append(f"{ms} ms: {var_sum(n, rk_ideal)/((n//PIECE)*vpi):.3f}")
    print("  ideal 300 Hz brick-wall channel, exact/independent-pieces: " + "; ".join(vals))

    print("\nC3. The 10 ms tap (5 pieces = 15 samples): noise bandwidth and SNR gain over one channel sample (topic 3 item 1)")
    vt = var_sum(15)
    b_tap = B_N * vt / (15**2 * R_CH[0])
    print(f"  tap noise bandwidth = {b_tap:.1f} Hz; SNR gain over one channel sample = {db(15**2*R_CH[0]/vt):.2f} dB (power ratio)")

    print("\nC4. Peak of the tap on a short mark, relative to a long mark's (15A)^2 (topic 3 item 1, 'D4')")
    for mark_ms in (10.0, 12.0):
        peaks = []
        dur = int(round(mark_ms * 1e-3 * FS_HI))
        for off in np.linspace(0, PIECE * DEC, 49)[:-1]:
            y = channel_pulse(int(round(20 * DEC + off)), dur, 60)
            pieces = y.reshape(-1, PIECE).sum(axis=1)
            taps = np.convolve(pieces, np.ones(5), mode="valid")
            peaks.append(np.max(np.abs(taps)) ** 2 / 15.0**2)
        peaks = np.array(peaks)
        print(f"  {mark_ms:4.1f} ms mark: tap peak re (15A)^2 from {db(peaks.min()):+.2f} to {db(peaks.max()):+.2f} dB over start offsets within a piece")
    # share of 100 WPM dots shorter than 10 ms with a log-normal factor of sigma 0.3 (median 12 ms)
    z = math.log(10.0 / 12.0) / 0.3
    print(f"  P(100 WPM dot < 10 ms | ln-sigma 0.3) = {0.5*math.erfc(-z/math.sqrt(2)):.3f}")

    print("\nC5. Cost of putting mark edges only at piece boundaries (topic 4 item 1, 'D11')")
    def edge_cost(mark_ms, piece, n_off=48):
        dur = int(round(mark_ms * 1e-3 * FS_HI))
        nmax = int(mark_ms * 1e-3 * R) + 4 * piece + 6
        costs = []
        costs_ideal = []
        for off in np.linspace(0, piece * DEC, n_off + 1)[:-1]:
            n_ch = 40 + nmax + 40
            n_ch -= n_ch % piece
            y = channel_pulse(int(round(40 * DEC + off)), dur, n_ch)
            c = np.concatenate([[0.0], np.cumsum(y)])
            # best window at sample resolution
            best_s = 0.0
            for a in range(30, 40 + nmax):
                for b in range(a + 1, min(a + nmax + 1, n_ch + 1)):
                    s = (c[b] - c[a]) ** 2 / var_sum_c(b - a)
                    best_s = max(best_s, s)
            best_p = 0.0
            for a in range(30 - 30 % piece, 40 + nmax, piece):
                for b in range(a + piece, min(a + nmax + piece + 1, n_ch + 1), piece):
                    s = (c[b] - c[a]) ** 2 / var_sum_c(b - a)
                    best_p = max(best_p, s)
            # ideal: E/N0 of the unfiltered mark; input noise has PSD N0 = 1/FS_HI per Hz (unit variance per
            # input sample), the carrier has amplitude 1, so E/N0 = T * FS_HI (dimensionless)
            ideal = (mark_ms * 1e-3) * FS_HI
            costs.append(db(best_s / best_p))
            costs_ideal.append(db(ideal / best_p))
        return np.mean(costs), np.max(costs), np.mean(costs_ideal), np.max(costs_ideal)
    for mark_ms, piece in ((12.0, 3), (12.0, 2), (48.0, 3)):
        m, w, mi, wi = edge_cost(mark_ms, piece)
        print(f"  {mark_ms:4.0f} ms mark, {piece}-sample ({1e3*piece/R:.2f} ms) pieces: loss vs best sample-resolution window"
              f" mean {m:.2f} dB, worst {w:.2f} dB; vs ideal E/N0 mean {mi:.2f} dB, worst {wi:.2f} dB")
    print("  C5b. Same question in a simpler model: white noise, unfiltered rectangular mark, edges anywhere in")
    print("       continuous time, pieces of length P; loss of the best piece-aligned window vs the exact mark")
    for mark_ms, p_ms in ((12.0, 2.0), (12.0, 4.0 / 3.0), (48.0, 2.0)):
        losses = []
        for off in np.linspace(0, p_ms, 2001)[:-1]:  # mark starts 'off' ms into a piece
            best = 0.0
            nmax = int(mark_ms / p_ms) + 3
            for a in range(0, 3):
                for b in range(a + 1, a + nmax + 1):
                    lo, hi = a * p_ms, b * p_ms  # window [lo, hi) in ms, mark is [off, off + mark_ms)
                    sig = max(0.0, min(hi, off + mark_ms) - max(lo, off))
                    best = max(best, sig**2 / (hi - lo))
            losses.append(db(mark_ms / best))
        losses = np.array(losses)
        print(f"  {mark_ms:4.0f} ms mark, {p_ms:.2f} ms pieces: mean loss {losses.mean():.2f} dB, worst {losses.max():.2f} dB")

    print("\nC6. Whole-mark sum with a residual frequency offset df: loss = sinc^2(df*T) (risk paragraph, 'D7')")
    for wpm, el, name in ((5, 3, "5 WPM dash"), (8, 3, "8 WPM dash"), (20, 3, "20 WPM dash"), (5, 1, "5 WPM dot")):
        T = el * 1.2 / wpm
        for dfz in (0.5, 2.0, 11.7):
            g = np.sinc(dfz * T) ** 2
            print(f"  {name} ({1e3*T:5.0f} ms), df = {dfz:4.1f} Hz: {db(max(g,1e-12)):7.1f} dB re centered; phase turn {dfz*T:5.2f} cycles")

    print("\nC7. Rayleigh fading inside a whole-mark coherent sum, Gaussian Doppler spectrum (risk paragraph, 'D8')")
    print("  E|sum g|^2 / (T^2 E|g|^2) = (2/T) int_0^T (1 - tau/T) rho(tau) dtau, rho = exp(-2 pi^2 s^2 tau^2)")
    for spread in (0.05, 0.5, 1.0, 5.0):
        for conv, s in (("2-sigma (ITU-R F.1487)", spread / 2.0), ("1-sigma", spread)):
            for wpm in (5, 20):
                T = 3 * 1.2 / wpm
                tau = np.linspace(0, T, 20001)
                rho = np.exp(-2 * np.pi**2 * s**2 * tau**2)
                val = (2.0 / T) * np.trapezoid((1 - tau / T) * rho, tau)
                print(f"  spread {spread:4.2f} Hz as {conv:22s}, {wpm:2d} WPM dash ({1e3*T:4.0f} ms): {db(val):6.2f} dB")

    print("\nC8. Mean key-down weight of a noise-only tap, Rician/Rayleigh posterior with prior p (topic 3 item 2, 'D6')")
    x = np.linspace(0, 12, 120001)
    dens = x * np.exp(-x**2 / 2)  # Rayleigh, unit per-component variance
    for s500_db in (0.0, -3.0, 3.0):
        a2 = 2 * 10 ** (s500_db / 10) * 500.0 / b_tap
        a = math.sqrt(a2)
        lam = -a2 / 2 + np.log(np.i0(a * x))
        for p in (0.3, 0.44, 0.5):
            g = lam + math.log(p / (1 - p))
            w = 1 / (1 + np.exp(-g))
            print(f"  S500 = {s500_db:+4.1f} dB (tap a^2 = {a2:5.2f}), prior p = {p:.2f}: mean weight = {np.trapezoid(w*dens, x):.3f}")

    print("\nC9. Grids (topic 2)")
    print(f"  speed: 12.0 ms * 1.1^31 = {12.0*1.1**31:.1f} ms = {1.2/(12.0e-3*1.1**31):.2f} WPM; worst speed error +/-{100*(math.sqrt(1.1)-1):.2f} %")
    r = 8 ** 0.25
    print(f"  Farnsworth: ratio 8^(1/4) = {r:.4f}; values " + ", ".join(f"{r**i:.2f}" for i in range(5))
          + f"; worst gap error +{100*(math.sqrt(r)-1):.1f} % / -{100*(1-1/math.sqrt(r)):.1f} %, ln error {math.log(math.sqrt(r)):.3f}")
    for sig in (0.3, 0.05):
        print(f"  penalty of a midway Farnsworth value at ln-sigma {sig}: {math.log(math.sqrt(r))**2/(2*sig**2):.3f} nats per gap;"
              f" of a midway speed: {math.log(math.sqrt(1.1))**2/(2*sig**2):.3f} nats per element")
    for c, s in ((18, 5), (33, 5), (25, 10)):
        ta = (60 * c - 37.2 * s) / (s * c)
        print(f"  ARRL (Bloom 1990) c = {c} WPM, s = {s} WPM: gap stretch t_c/(3u) = {ta/19/(1.2/c):.2f}")

    print("\nC10. CPU arithmetic (topic 5 item 5)")
    print(f"  0.042 s per channel-second / (500 pieces/s * 50 ns) = {0.042/(500*50e-9):.0f} hypotheses")
    print(f"  1700 hypotheses * 500 pieces/s = {1700*500:.0f} hypothesis-pieces/s; 10-25 Mflop/s means {10e6/(1700*500):.0f}-{25e6/(1700*500):.0f} flop each")

    print("\nC11. Fading time scales against the 0.5 s key-down window (topic 3 item 3)")
    for spread in (0.05, 0.5, 5.0):
        s = spread / 2
        t_half = math.sqrt(math.log(2) / (2 * math.pi**2 * s**2))
        print(f"  spread {spread:4.2f} Hz (2-sigma): gain correlation 0.5 after {t_half:7.3f} s;"
              f" 0.5 s of key-down at duty 0.44 spans {0.5/0.44:.2f} s = {0.5/0.44/t_half:6.2f} half-correlation times")

    print("\nC12. Exactness of deferring dot/dash to the end of the mark (topic 1 item 2): numeric check")
    # two log-normal duration laws (dot median 1, dash median 3, sigma 0.3) on a 0.02-dot grid, priors 0.5/0.5
    d = np.arange(1, 600) * 0.02
    def pmf(med, sig):
        p = np.exp(-(np.log(d / med)) ** 2 / (2 * sig**2)) / d
        return p / p.sum()
    pdot, pdash = pmf(1, 0.3), pmf(3, 0.3)
    mix = 0.5 * pdot + 0.5 * pdash
    surv = np.concatenate([[1.0], 1 - np.cumsum(mix)[:-1]])
    hz = mix / np.maximum(surv, 1e-300)
    cont = np.cumprod(np.concatenate([[1.0], 1 - hz[:-1]]))
    joint_dot = cont * hz * (0.5 * pdot / np.maximum(mix, 1e-300))
    print(f"  max |deferred-label path probability - prior*density| over durations = {np.max(np.abs(joint_dot - 0.5*pdot)):.2e}")


if __name__ == "__main__":
    main()
