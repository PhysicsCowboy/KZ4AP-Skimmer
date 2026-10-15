"""Derived numbers for the step-1 standard-practice review (numpy only).

Run: .venv/Scripts/python -I docs/research/2026-10-14-standard-practice-check/numbers.py
Every quantity printed carries its unit. Sections are numbered N1..N12 and cited by number in
step-1-standard-practice.md.
"""
import numpy as np

FS = 1500.0  # channel sample rate, samples/s (from the brief)
B_N = 252.0  # channel noise bandwidth, Hz (two-sided, from the brief)
SPEEDS = [5.0, 10.0, 20.0, 40.0, 60.0, 100.0]  # WPM


def db(x):
    return 10.0 * np.log10(x)


def sinc(x):
    return np.sinc(x)  # numpy sinc is sin(pi x)/(pi x)


def qfunc(x):
    """Gaussian tail probability Q(x), from the standard library's complementary error function."""
    import math
    return 0.5 * math.erfc(x / math.sqrt(2.0))


def log_i0_scaled(x):
    """log(I0(x)) for x >= 0, stable for large x."""
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    small = x < 600.0
    out[small] = np.log(np.i0(x[small]))
    xl = x[~small]
    # asymptotic: I0(x) ~ e^x / sqrt(2 pi x) * (1 + 1/(8x) + 9/(128x^2))
    out[~small] = xl - 0.5 * np.log(2 * np.pi * xl) + np.log1p(1 / (8 * xl) + 9 / (128 * xl**2))
    return out


def noncoherent_ook_pe(es_n0):
    """Per-decision error probability of noncoherent on-off keying with the optimum threshold,
    equal priors. es_n0 = E/N0 (linear) of the key-down element at the matched-filter output.
    Statistic r = |z|, noise complex Gaussian with E|n|^2 = N0 (normalized), signal amplitude sqrt(E).
    """
    n0 = 1.0
    nu = np.sqrt(es_n0 * n0)
    s2 = n0 / 2.0  # per-component variance
    r = np.linspace(0.0, nu + 12.0 * np.sqrt(s2) + 6.0, 200001)
    dr = r[1] - r[0]
    # Rayleigh (H0) and Rician (H1) densities
    p0 = (r / s2) * np.exp(-(r**2) / (2 * s2))
    with np.errstate(divide="ignore"):
        logp1 = np.log(np.maximum(r, 1e-300) / s2) - (r**2 + nu**2) / (2 * s2) + log_i0_scaled(r * nu / s2)
    p1 = np.exp(logp1)
    p1[0] = 0.0
    # optimum threshold where p0 = p1 (equal priors); error = 0.5*(P0(r>g) + P1(r<g))
    c0 = np.cumsum(p0) * dr
    c1 = np.cumsum(p1) * dr
    pe = 0.5 * ((1.0 - c0) + c1)
    k = int(np.argmin(pe))
    return pe[k], r[k] / np.sqrt(n0)


def coherent_ook_pe(es_n0):
    return qfunc(np.sqrt(es_n0 / 2.0))


def find_esn0_for_pe(fun, target, lo_db=-5.0, hi_db=30.0):
    for _ in range(60):
        mid = 0.5 * (lo_db + hi_db)
        pe = fun(10 ** (mid / 10.0))
        pe = pe[0] if isinstance(pe, tuple) else pe
        if pe > target:
            lo_db = mid
        else:
            hi_db = mid
    return 0.5 * (lo_db + hi_db)


def main():
    print("N1. PARIS timing, T = 1.2/w s")
    for w in SPEEDS:
        T = 1.2 / w
        print(
            f"  {w:5.0f} WPM: dot T = {1e3*T:6.1f} ms, dash 3T = {3e3*T:6.1f} ms, letter gap 3T = {3e3*T:6.1f} ms,"
            f" word gap 7T = {7e3*T:7.1f} ms, samples per dot at 1500 S/s = {FS*T:6.1f}"
        )

    print("\nN2. Matched (integrate-and-dump over T) filter: two-sided noise bandwidth 1/T, and E_dot/N0 = S500 + 10log10(500 Hz * T)")
    for w in SPEEDS:
        T = 1.2 / w
        print(f"  {w:5.0f} WPM: B_mf = 1/T = {1/T:6.2f} Hz; E_dot/N0 - S500 = {db(500.0*T):6.2f} dB")
    print("  Loss from integrating over the full channel noise bandwidth instead (coherent, narrowband signal): 10log10(B_N*T)")
    for w in SPEEDS:
        T = 1.2 / w
        print(f"  {w:5.0f} WPM: 10log10(252 Hz * T) = {db(B_N*T):6.2f} dB")

    print("\nN3. Frequency-offset loss of a matched (integrate-and-dump) filter: |sinc(df*T)|^2")
    for w in SPEEDS:
        T = 1.2 / w
        loss = -db(sinc(11.7 * T) ** 2) if abs(sinc(11.7 * T)) > 1e-12 else np.inf
        print(f"  {w:5.0f} WPM: loss at df = 11.7 Hz: {loss:7.2f} dB")
    # offset for 1 dB and 0.1 dB loss
    x = np.linspace(1e-6, 0.5, 500001)
    l = -db(sinc(x) ** 2)
    x1 = x[np.argmin(np.abs(l - 1.0))]
    x01 = x[np.argmin(np.abs(l - 0.1))]
    print(f"  1 dB loss at df*T = {x1:.4f}; 0.1 dB loss at df*T = {x01:.4f}")
    for w in SPEEDS:
        T = 1.2 / w
        print(f"  {w:5.0f} WPM: df for 1 dB loss = {x1/T:6.2f} Hz; for 0.1 dB loss = {x01/T:6.2f} Hz")

    print("\nN4. Element-decision error, coherent vs noncoherent on-off keying, optimum threshold, equal priors")
    for target in (1e-1, 1e-2, 1e-3):
        ec = find_esn0_for_pe(coherent_ook_pe, target)
        en = find_esn0_for_pe(lambda e: noncoherent_ook_pe(e)[0], target)
        print(f"  Pe = {target:g}: coherent needs E/N0 = {ec:5.2f} dB; noncoherent {en:5.2f} dB; noncoherent loss = {en-ec:4.2f} dB")
        for w in (5.0, 20.0, 100.0):
            T = 1.2 / w
            print(f"      noncoherent dot decision at {w:4.0f} WPM needs S500 = {en - db(500.0*T):6.2f} dB")

    print("\nN5. Rayleigh fading, Gaussian Doppler spectrum (Watterson; ITU-R F.1487 spread = 2 sigma)")
    print("  complex-gain correlation rho(tau) = exp(-2 pi^2 sigma^2 tau^2), sigma = spread/2")
    for spread in (0.05, 0.1, 0.5, 1.0, 5.0, 10.0):
        sig = spread / 2.0
        tau_half = np.sqrt(np.log(2.0) / (2 * np.pi**2 * sig**2))
        tau_e = np.sqrt(1.0 / (2 * np.pi**2 * sig**2))
        print(f"  spread {spread:5.2f} Hz: rho = 0.5 at {1e3*tau_half:9.1f} ms; rho = 1/e at {1e3*tau_e:9.1f} ms")
    print("  Correlation of the complex gain across one element, rho(T) and rho(3T), spread 5 Hz:")
    for w in SPEEDS:
        T = 1.2 / w
        sig = 2.5
        r1 = np.exp(-2 * np.pi**2 * sig**2 * T**2)
        r3 = np.exp(-2 * np.pi**2 * sig**2 * (3 * T) ** 2)
        print(f"  {w:5.0f} WPM: rho(T) = {r1:5.3f}, rho(3T) = {r3:5.3f}")
    print("  N5b. If the test set's 'Doppler spread' were instead the Clarke/Jakes maximum Doppler fm: rho = J0(2 pi fm tau)")
    th = np.linspace(0.0, np.pi, 200001)
    # J0(x) = (1/pi) int_0^pi cos(x sin th) dth, evaluated on a grid of x near the 0.5 crossing
    xg = np.linspace(1.3, 1.7, 4001)
    j0 = np.array([np.trapezoid(np.cos(x * np.sin(th)), th) / np.pi for x in xg])
    x_half = xg[np.argmin(np.abs(j0 - 0.5))]
    print(f"  J0(x) = 0.5 at x = {x_half:.4f} -> tau_half = {x_half/(2*np.pi):.4f}/fm")
    for fm in (0.05, 1.0, 5.0):
        print(f"  fm {fm:5.2f} Hz: rho = 0.5 at {1e3*x_half/(2*np.pi*fm):9.1f} ms")
    print("  ITU-R F.1487 test length 3000/spread:")
    for spread in (0.05, 0.1, 0.5, 1.0, 5.0):
        print(f"  spread {spread:5.2f} Hz: {3000.0/spread:9.0f} s = {3000.0/spread/3600.0:6.2f} h")

    print("\nN6. Correlated channel noise. Model channel filter (assumption): amplitude 1 to 39 Hz, raised-cosine to 0 at 279 Hz")
    f = np.linspace(-750.0, 750.0, 3000001)
    df = f[1] - f[0]
    a = np.abs(f)
    H = np.where(a <= 39.0, 1.0, np.where(a >= 279.0, 0.0, 0.5 * (1 + np.cos(np.pi * (a - 39.0) / 240.0))))
    nbw = np.sum(H**2) * df
    bstat = (np.sum(H**2) * df) ** 2 / (np.sum(H**4) * df)
    h150 = 0.5 * (1 + np.cos(np.pi * (150.0 - 39.0) / 240.0))
    print(f"  model: |H(150 Hz)| = {db(h150**2):.2f} dB re passband; noise bandwidth = {nbw:.1f} Hz (brief: 252 Hz)")
    print(f"  statistical bandwidth (int|H|^2)^2/int|H|^4 = {bstat:.1f} Hz")
    print(f"  log-likelihood overcount for a narrowband signal if samples are treated as independent: fs/B_N = {FS/B_N:.2f} (brief's B_N)")
    print(f"  noise-power estimate: independent samples per second ~ B_stat = {bstat:.0f}; overcount fs/B_stat = {FS/bstat:.2f}")
    # autocorrelation of the model noise
    hh = H**2
    lags = np.array([0.0, 1, 2, 3, 4, 6, 8]) / FS
    rho = [np.sum(hh * np.cos(2 * np.pi * f * t)) * df / nbw for t in lags]
    print("  model noise autocorrelation at lags (samples): " + ", ".join(f"{int(round(t*FS))}: {r:+.3f}" for t, r in zip(lags, rho)))

    print("\nN7. Matched-filter rejection of a neighbor, |sinc(df*T)|^2, in dB re passband")
    for w in SPEEDS:
        T = 1.2 / w
        vals = []
        for dfn in (30.0, 60.0, 120.0):
            g = sinc(dfn * T) ** 2
            env = 1.0 / (np.pi * dfn * T) ** 2  # sidelobe envelope
            vals.append(f"{dfn:4.0f} Hz: {db(max(g,1e-30)):7.1f} (envelope {db(min(env,1.0)):6.1f})")
        print(f"  {w:5.0f} WPM: " + "; ".join(vals))

    print("\nN8. Cramer-Rao bound on frequency from one mark of duration Tm: std(f) = sqrt(6)/(2 pi Tm sqrt(E/N0))")
    for w in (5.0, 20.0, 100.0):
        T = 1.2 / w
        for s500 in (10.0, 0.0, -10.0):
            for m, name in ((1, "dot"), (3, "dash")):
                Tm = m * T
                en0 = 10 ** (s500 / 10.0) * 500.0 * Tm
                sd = np.sqrt(6.0) / (2 * np.pi * Tm * np.sqrt(en0))
                print(f"  {w:4.0f} WPM, S500 = {s500:+5.1f} dB, {name}: E/N0 = {db(en0):5.1f} dB, std(f) >= {sd:7.3f} Hz")

    print("\nN9. Noise-power estimate precision: relative std = 1/sqrt(B*tau) for averaging time tau over bandwidth B")
    for B in (20.0, 100.0, 200.0):
        for tau in (0.25, 1.0, 4.0):
            rel = 1.0 / np.sqrt(B * tau)
            print(f"  B = {B:5.0f} Hz, tau = {tau:4.2f} s: relative std = {100*rel:5.1f} % = {db(1+rel):4.2f} dB")

    print("\nN10. PARIS duty cycle and Farnsworth gap stretch (Bloom 1990)")
    on_units = 8 + 4 + 5 + 2 + 3
    print(f"  PARIS: key-down units = {on_units} of 50 -> duty = {on_units/50:.2f}")
    stretch = []
    for c in (18.0, 25.0, 33.0):
        for s in (5.0, 10.0, 16.0):
            if s >= c:
                continue
            ta = (60.0 * c - 37.2 * s) / (s * c)
            tc = 3 * ta / 19
            tw = 7 * ta / 19
            u = 1.2 / c
            stretch.append((c, s, tc / (3 * u), tw / (7 * u)))
            print(f"  c = {c:4.0f} WPM, s = {s:4.0f} WPM: t_a = {ta:6.3f} s, letter gap = {tc:6.3f} s ({tc/(3*u):5.2f} x 3u),"
                  f" word gap = {tw:6.3f} s ({tw/(7*u):5.2f} x 7u)")

    print("\nN11. Log-spaced speed grid from 5 to 100 WPM")
    for r in (1.02, 1.05, 1.10, 1.20):
        n = np.log(100.0 / 5.0) / np.log(r) + 1
        print(f"  ratio {r:.2f}: {int(np.ceil(n))} speed states; worst-case speed error +/-{100*(np.sqrt(r)-1):.1f} %")
    print("  random walk in ln(speed) with per-element std s needs (ln 2 / s)^2 elements to drift by a factor 2:")
    for s in (0.01, 0.02, 0.05):
        print(f"  s = {s:.2f}: {(np.log(2)/s)**2:8.0f} elements")

    print("\nN12. Longest durations an explicit-duration model must represent")
    for w in (5.0, 20.0):
        T = 1.2 / w
        print(f"  {w:3.0f} WPM: word gap 7T = {7*T:5.2f} s")
    c, s = 33.0, 5.0
    ta = (60.0 * c - 37.2 * s) / (s * c)
    print(f"  Farnsworth c = {c:.0f}, s = {s:.0f} WPM: letter gap {3*ta/19:5.2f} s, word gap {7*ta/19:5.2f} s")
    for frame_ms in (1.0, 2.0, 5.0):
        print(f"  frame {frame_ms:.0f} ms: word gap of {7*ta/19:.2f} s spans {7*ta/19/(frame_ms/1e3):6.0f} frames;"
              f" a 100-WPM dot spans {12.0/frame_ms:4.1f} frames")


if __name__ == "__main__":
    main()
