"""The prototype's parameters, in physical units (Hz, s, FS; counts of marks, spaces, characters or
selection instants). Samples and bins appear only at the point of use. Status of each value: the plan's
"Parameters" table (docs/plans/2026-09-30-milestone-2b-stage-1-filter-bank-prototype.md) and spec §6."""

from __future__ import annotations

import math
from dataclasses import dataclass, fields, replace


@dataclass(frozen=True)
class ProtoConfig:
    # Filter bank (spec 4.1)
    min_wpm: float = 5.0                   # owner
    max_wpm: float = 100.0                 # owner
    ladder_step: float = 1.1               # owner
    length_dits: float = 0.8               # heuristic
    block_s: float = 32 / 1500             # estimates advance once per block, s: a duration, 21.33 ms (written as
                                           # 32/1500 s because it equals the engine's channel block); heuristic
    # Noise (spec 4.2)
    noise_method: str = "spectrum"         # "spectrum": three-tap level x spectrum ratios; "spectrum-level": spectrum
                                           # level (both divide branch k by mask_bias[k]); "branch": the fallback;
                                           # measured (E10): "spectrum", variant (a), kept; neither other variant
                                           # qualified (results record, section 3.1)
    mask_bias: tuple[float, ...] = (       # b_mask,k, one per branch (both spectrum arms): branch k's sigma_v,k^2
        0.8370, 0.8293, 0.8263, 0.8213,    # from the masked, smoothed spectrum (flat mean of the accepted masked
        0.8171, 0.8136, 0.8095, 0.8074,    # periodograms) / its true sigma_v,k^2. Measured (Task 5): white noise,
        0.8047, 0.8025, 0.8008, 0.7988,    # 1 FS^2, seeds 101-110, 60 s each, 2362 of 3500 segments accepted;
        0.7973, 0.7958, 0.7945, 0.7935,    # per-seed scatter 2.6% (k=1) to 4.5% (k=32), so the 10-seed mean is
        0.7924, 0.7914, 0.7907, 0.7899,    # good to about 0.8-1.4%. In channel-shaped noise the same ratios are
        0.7892, 0.7885, 0.7880, 0.7875,    # 2.9% (k=1) to 5.9% (k=32) higher (Task 5 report). The whole-band
        0.7871, 0.7867, 0.7864, 0.7861,    # masked power reads 0.9745 of the truth (seed 8, 60 s): the mask removes
        0.7858, 0.7856, 0.7854, 0.7852,    # mostly low-frequency power. Valid ONLY for the defaults of the ladder,
    )                                      # segment_s, spectrum_smoothing_hz, guard_margin_s, min_clean_fraction,
                                           # neighbor_guard and the three-tap settings, at 1500 samples/s;
                                           # re-measure (test_proto_noise's calibration test) if any changes.
    noise_tau_s: float = 2.0               # tau_n, s of noise updates (milestone 2)
    noise_warmup_s: float = 0.32           # first estimate: 20% quantile of |v|^2 over this, s (milestone 2)
    noise_guard: float = 1.75              # kappa (milestone 2)
    neighbor_guard: float = 4.0            # kappa_n (milestone 2); also the spectrum's mark flag;
                                           # mask_bias measured with the default; re-measure it if this changes
    segment_s: float = 256 / 1500          # T_seg, s (bins 5.86 Hz wide); heuristic;
                                           # mask_bias measured with the default; re-measure it if this changes
    spectrum_smoothing_hz: float = 25.0    # the shape is averaged over +/- this, Hz; heuristic;
                                           # mask_bias measured with the default; re-measure it if this changes
    guard_margin_s: float = 0.02           # the spectrum's mark flag reaches this far, s; heuristic;
                                           # mask_bias measured with the default; re-measure it if this changes
    min_clean_fraction: float = 0.5        # a segment enters the spectrum only if this much of it is unflagged; heuristic;
                                           # mask_bias measured with the default; re-measure it if this changes
    # Keying (spec 4.3, 4.7)
    amplitude_tau_s: float = 0.5           # tau_a, s of key-down weight (milestone 2)
    prior_key_down: float = 0.44           # P1 (PARIS)
    hysteresis_nats: float = 1.0           # h (kept; heuristic)
    squelch_a: float = 3.0                 # a_min at squelch_ref_s (milestone 2; heuristic)
    squelch_ref_s: float = 0.016           # s
    squelch_exponent: float = 0.25         # a_min proportional to L^(1/4) (derived scaling)
    false_marks_per_s: float = 0.01        # R_fa target, per branch, noise alone; heuristic (E9)
    x_on_values: tuple[float, ...] = ()    # x_on per branch, calibrated to R_fa by measurement (E9); () = the
                                           # nominal sqrt(-2 ln(R_fa L_k)), heuristic (keys 7-10x more than R_fa)
    release_probability: float = 0.3       # key up where noise alone exceeds x this often (x_off = 1.55); heuristic
    rekey_after_s: float = 0.4             # W_min, s of keyed time while the amplitude is unknown; placeholder (E9)
    seed_memory_rekeys: float = 4.0        # while the amplitude is unknown, the seed (0.9 quantile of |v|^2) is taken
                                           # over the most recent keyed samples, at most this x W_min of keyed time
                                           # (1.6 s by default); heuristic (a bound on memory, several W_min long)
    # Duration fit (spec 4.5)
    fit_memory: float = 48.0               # N_mem, marks and spaces; measured (E4): 48 adopted over 24 and 12
                                           # (results record, section 3.7)
    t_grid_step: float = 0.01              # relative step of the T grid; measured (E5): kept (the "coarse" grids
                                           # below, with this step, had the lowest CPU; results record, section 3.8)
    q_grid: tuple[float, ...] = (3.0, 4.0, 5.0)                                            # measured (E5), "coarse"
    w_grid: tuple[float, ...] = (-0.4, 0.0, 0.4, 0.8)                                      # w/T; measured (E5), "coarse"
    tg_grid: tuple[float, ...] = (1.0, 1.59, 2.52, 4.0, 6.35)                              # T_g/T; measured (E5), "coarse"
    sigma_ln_mark: float = 0.15            # heuristic
    sigma_ln_space: float = 0.25           # heuristic
    outlier_prior: float = 0.05            # epsilon; heuristic
    outlier_range_s: tuple[float, float] = (0.001, 10.0)   # log-uniform outlier class, s; heuristic
    prior_sigma_ln: float = 0.1            # width of the T_P prior in ln T; heuristic
    refine_iterations: int = 2             # weighted-least-squares steps after the grid; heuristic
    min_fit_weight: float = 8.0            # elements of memory weight before a fit counts for eligibility; heuristic
    # Periodicity (spec 4.4)
    periodicity_method: str = "comb"       # "comb" (on Pi = 2T; default, owner), "edge" or "spectrum"; E1
    periodicity_windows_s: tuple[float, ...] = (2.0, 5.0, 10.0)   # placeholder (E2)
    periodicity_update_s: float = 0.25     # heuristic
    periodicity_rate_hz: float = 750.0     # p is averaged down to this rate, samples/s; heuristic
    comb_teeth: int = 4                    # teeth at k Pi (comb) or k T (edge comb); placeholder (E3)
    comb_width: float = 0.075              # comb tooth half-width, fraction of Pi (0.075 Pi = 15% of T; the edge comb
                                           # uses 2 x this, fraction of T); placeholder (E3). The edge comb has no
                                           # teeth or width of its own: it reuses comb_teeth and 2 x comb_width
    spectrum_nulls: int = 3                # placeholder (E3)
    spectrum_null_width: float = 0.15      # null band width x T (+/- half of it around k/T); placeholder (E3)
    spectrum_front_floor_db: float = -20.0 # the spectrum fit leaves out frequencies where the front end's |H(f)|^2
                                           # (branch 1's boxcar, then the averaging to periodicity_rate_hz) is below
                                           # this, dB relative to its DC power gain. With N_1 = 14 samples at 1500
                                           # samples/s that leaves out 97.1-119.6 Hz, 192.4-245.3 Hz and 280.4-375 Hz
                                           # (around the nulls at 107.1 and 214.3 Hz, and past the second sidelobe;
                                           # computed); heuristic
    comb_confidence_min: float = 0.03      # placeholder (E1)
    edge_confidence_min: float = 0.03      # placeholder (E1)
    spectrum_confidence_min: float = 1.5   # nats; placeholder (E1)
    # Selection (spec 4.6)
    eligibility_tolerance: float = math.log(1.1)   # heuristic (one ladder step)
    switch_persistence: int = 4            # M, selection instants in a row; placeholder (E6)
    quality_tie_nats: float = 0.05         # epsilon_Q, nats per element; placeholder (E8)
    text_tie_nats: float = 0.1             # nats per character; heuristic
    text_window_chars: int = 10            # placeholder (E8)
    text_separation_nats: float = 1.0      # "clearly separates" with none eligible, nats per character; heuristic
    # Silences and output (spec 4.7, 4.8)
    new_over_min_s: float = 0.5            # placeholder (E7)
    new_over_gaps: float = 12.0            # x T_g; placeholder (E7)
    rekey_timeout_s: float = 2.0           # channel time, s: if an over's amplitude is still unknown this long after it
                                           # became unknown (W_min of keyed time not reached), what exists is re-keyed
                                           # with the full LLR at the previous over's amplitude (no keying if there is
                                           # none) and corrected; inside the 20 s correction reach; placeholder,
                                           # heuristic (Task 11 review)
    fresh_fit_min_obs: int = 8             # marks and spaces of this over a fresh fit needs before it may replace the
                                           # previous over's (which it must also beat by 1/2 k ln n, BIC-style);
                                           # placeholder, heuristic (Task 11 review)
    correction_reach_s: float = 20.0       # owner

    def with_values(self, **changes) -> "ProtoConfig":
        """A copy with some values changed; lists become tuples (JSON has no tuples)."""
        names = {f.name for f in fields(self)}
        unknown = sorted(set(changes) - names)
        if unknown:
            raise ValueError(f"unknown parameters {unknown}")
        return replace(self, **{k: tuple(v) if isinstance(v, list) else v for k, v in changes.items()})
