// Bell's decoder (E. L. Bell, 1977, the Naval Postgraduate School thesis; the program is the settled transcription
// reference/bell1977/listing.txt): every constant of his receiver chain (RCVR, BPFDET, NOISE) and of his decoder
// (PROCES and the routines it calls, INITL's tables), with the listing's value, its unit and its source line. The
// reference sheet docs/research/bell-1977-reference.md, sections (b) and (c), gives each one's meaning and derivation.
//
// - Sources are cited as "p. N L": the listing's printed page N and Bell's sequence number L.
// - Every value is Bell's (the listing's; the owner's decision D1), unless marked "our choice" with its reason.
// - Units: s, Hz, rad, WPM; AU, the amplitude unit of his simulator (key-down carrier 1 AU nominal), and AU². A
//   per-step quantity names its step: "per 8 kHz sample" (0.125 ms) or "per 5 ms step" (the decoder's step τ).
//   Times are stored in s and converted to the listing's ms only at the point of use, by ms() below; the tests check
//   that the converted values equal the listing's literals exactly (bit for bit), so the port computes what the
//   listing computes.
// - Bell's integer codes stay 1-based as values: elements 1-6 (dot, dash, e-sp, c-sp, w-sp, pause), element-states
//   1-16, letter-tree states 1-400 (0: no state), speed branches 1-5, speeds in WPM. Only container indexing converts,
//   in the one helper at1() below.
// - Not here: the simulator's constants (KEY, SIMSG1 and INPUTL's run settings), which this project implements in
//   its Python generator (the plan's D5, Task 7) and in the compiled reference (Task 3), and the display's (MAIN's
//   18 and 512 loops, DISPLA, STATS), which do not touch the decoding. The letter tree is in tree.hpp.
#pragma once

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace kz4ap::bell {

// Bell's 1-based index into a container: at1(c, i) is the Fortran c(i), for i = 1 ... size; anything else throws
// std::out_of_range. Two-dimensional tables are arrays of rows, so at1(at1(t, i), j) is t(i, j).
template <class Container>
auto& at1(Container& c, int i) {
    if (i < 1 || static_cast<std::size_t>(i) > c.size())
        throw std::out_of_range("kz4ap::bell::at1: index " + std::to_string(i) + " outside 1.." +
                                std::to_string(c.size()));
    return c[static_cast<std::size_t>(i - 1)];
}

// s to ms: the listing computes times in ms; the configuration holds them in s.
constexpr double ms(double seconds) { return seconds * 1000.0; }

// Bell's element codes [p. 160 00600 (mean lengths 1, 3, 1, 3, 7, 14 dots), p. 161 06300 (key states)].
inline constexpr int kDot = 1;
inline constexpr int kDash = 2;
inline constexpr int kElementSpace = 3;    // e-sp
inline constexpr int kCharacterSpace = 4;  // c-sp
inline constexpr int kWordSpace = 5;       // w-sp
inline constexpr int kPause = 6;
inline constexpr int kElements = 6;
inline constexpr int kElementStates = 16;  // Table XII's (previous element, current element) pairs [p. 105]
inline constexpr int kSpeedBranches = 5;   // speed changes -2, -1, 0, +1, +2 steps (Table X) [p. 161 09500]
inline constexpr int kSuccessors = kElements * kSpeedBranches;  // 30 per path; n = (i_r - 1)·6 + k [p. 169 17200]

struct BellConfig {
    // ---- Sampling (MAIN, INPUTL) ----
    // s, TAU: one 8 kHz sample of the simulator and the receiver chain [p. 160 00600]
    double sample_period_s = 0.000125;
    // s, τ: the decoder's step, every 40th 8 kHz sample (NOISE and PROCES run when NP reaches 40) [p. 159 01800];
    // also XTRANS's D0 + 5 ms [p. 170 21400] and PATH's + 5 ms per step [p. 174 42700]
    double step_s = 0.005;
    // AU², RN: the measurement variance before NOISE's first call; NOISE overwrites it before PROCES uses it
    // [p. 159 00400]
    double initial_rn_au2 = 0.1;

    // ---- The listing's 2π and RCVR (p. 189 00100-02800) ----
    // rad, the listing's stand-in for 2π (relative error +7.5e-7): INPUTL's multiplier of FC [p. 160 03000] and the
    // modulus of every AMOD phase wrap [p. 189 01500, 02300]; kept, not replaced by 2π (sheet (j) item 11)
    double two_pi_listing = 6.28319;
    // Hz, FREQ: the simulated carrier; not given by Bell [our choice, sheet (e): RCVR's output frequency]
    double carrier_hz = 1000.0;
    // rad/s, RCVR's up-mixer advance 6283.2·TAU per 8 kHz sample, not scaled by 6.28319: with the wrap at 6.28319 it
    // runs at 1000.00159 Hz [p. 189 02200]
    double rcvr_up_mixer_rad_per_s = 6283.2;
    // per 8 kHz sample, dimensionless: RCVR's one-pole low-pass y_n = y_{n-1} + 0.070·(x_n - y_{n-1}) on I and Q
    // [p. 189 01900-02000]
    double rcvr_lowpass_coefficient = 0.070;

    // ---- BPFDET (pp. 189-190, 03700-09100); per 8 kHz sample, dimensionless ----
    // first resonator (radius 0.90): w_n = C1·w_{n-1} - C2·w_{n-2} + C·ZIN_n [p. 189 05400, p. 190 06100]
    double bpf1_c1 = 1.2726;
    double bpf1_c2 = 0.8100;
    double bpf1_gain = 0.1900;
    // second resonator (radius 0.97): v_n = CK1·v_{n-1} - CK2·v_{n-2} + CG·w_n [p. 189 05300, p. 190 06500]
    double bpf2_ck1 = 1.37158;
    double bpf2_ck2 = 0.9409;
    double bpf2_gain = 0.0150;
    // envelope low-pass A(1..4): Z_n = A1·(d_n + 3d_{n-1} + 3d_{n-2} + d_{n-3}) + A2·Z_{n-1} - A3·Z_{n-2} - A4·Z_{n-3},
    // d = |v| [p. 189 05200, p. 190 08100-08700]
    std::array<double, 4> envelope_lowpass_a = {0.000030051, 2.9507982, 2.90396345, -0.953135172};

    // ---- NOISE (pp. 190-191, 09600-15000); heuristic, "selected empirically" [p. 124] ----
    // s, YLONG: the running minimum's window, 200 decoder samples (1.0 s at 5 ms) [p. 190 10900, 11500-12000]
    double noise_long_window_s = 1.0;
    // s, YSHORT: 50 decoder samples (0.25 s); it never changes the result (sheet (b)) [p. 190 10900, 11700-12200]
    double noise_short_window_s = 0.25;
    // AU, YLONG, YSHORT, YMIN1 and YMIN2 at the start [p. 190 11100, 11300]
    double noise_buffer_initial_au = 1.0;
    // AU, YMAVG at the start [p. 190 11300]
    double noise_ymavg_initial_au = 0.05;
    // per 5 ms step, dimensionless: YMAVG <- YMAVG + 0.004·(YMIN - YMAVG); time constant 1.25 s [p. 191 14300]
    double noise_ymavg_coefficient = 0.004;
    // AU: R = 0.30 AU·YMAVG [p. 191 14500]
    double noise_r_factor_au = 0.30;
    // AU²: R's floor [p. 191 14600]
    double noise_r_floor_au2 = 0.005;
    // dimensionless: z = 1.1·(Z - 2.4·YMAVG - 0.05 AU) [p. 191 14700]
    double noise_z_gain = 1.1;
    double noise_floor_factor = 2.4;
    // AU [p. 191 14700]
    double noise_z_offset_au = 0.05;

    // ---- PROCES's start (p. 167 05500-p. 168 06100) and the path store ----
    // paths saved at most: the dimension of LAMBDA, DUR, ILRATE (p. 167 05000) and SAVEP's limit NSAV >= 25
    // [p. 182 36200]; ISAVE starts at 25 [p. 167 05500]
    int max_paths = 25;
    // letter-tree state of every path at the start, LAMBDA = 25*5 [p. 167 05600]; with rule N1 the e-sp after T
    int start_state = 5;
    // s, DUR = 25*1000. ms: time in the start element [p. 167 06000]
    double start_duration_s = 1.0;
    // WPM, ILRATE = 5*10, 5*20, 5*30, 5*40, 5*50 [p. 167 05700]
    std::array<int, 25> start_speeds_wpm = {10, 10, 10, 10, 10, 20, 20, 20, 20, 20, 30, 30, 30,
                                            30, 30, 40, 40, 40, 40, 40, 50, 50, 50, 50, 50};
    // P = 750*1.: every path's probability at the start [p. 167 05800]
    double start_probability = 1.0;
    // LAMSAV = 750*5, ILRSAV = 750*20 WPM, PATHSV = 25*5: the new nodes' initial labels; ILRSAV's stale values enter
    // SPROB's speed estimate through state-0 successors (sheet (b), SPROB) [p. 167 06000, p. 168 06100]
    int initial_lamsav = 5;
    int initial_ilrsav_wpm = 20;
    int initial_pathsv = 5;
    // probability: XHAT = 1 when PX > 0.5 [p. 168 09100]
    double key_down_threshold = 0.5;

    // ---- KALFIL (pp. 176-177) ----
    // AU, YKKIP = 25*.5: amplitude estimate of every path at the start [p. 176 03700]
    double kalman_initial_amplitude_au = 0.5;
    // AU², PKKIP = 25*.10: its variance [p. 176 03700]
    double kalman_initial_variance_au2 = 0.10;
    // probability, PINMIN: a successor with PIN <= this gets likelihood 0 without a Kalman step [p. 176 03800, 04400]
    double kalman_min_transition_probability = 0.00010;
    // AU: the updated amplitude estimate is raised to this when it is <= it [p. 177 07600]
    double kalman_amplitude_floor_au = 0.01;
    // dimensionless: likelihood 0 when a = ν²/(2S) > 1000 [p. 177 07900]
    double likelihood_max_exponent = 1000.0;

    // ---- MODEL (pp. 177-178, 09500-14500) ----
    // s·WPM: the dot length Δ = 1.2 s / r at r WPM; the listing's 1200./rate in ms [p. 178 12400; also p. 170 21200,
    // p. 160 02900]
    double dot_length_wpm_s = 1.2;
    // bauds: B = DUR/Δ is capped here [p. 178 12600], and the key-up Φ is 1 from here on [p. 178 14100]
    double model_max_bauds = 14.0;
    // AU² per 5 ms step: Q while the path is in a mark, every successor [p. 178 12900]
    double q_mark_au2 = 0.0001;
    // dimensionless: Φ while the path is in a mark [p. 178 13000]
    double phi_mark = 1.0;
    // dimensionless: Φ from a space to a mark [p. 178 13400]
    double phi_space_to_mark = 1.0;
    // Q from a space of B bauds to a mark, AU² per 5 ms step:
    //   Q(B) = 0.15·e^{0.6(B - 14)} + 0.01·B·e^{0.2(1 - B)} [p. 178 13500-13600]
    double q_jump_au2 = 0.15;               // AU²
    double q_jump_rate_per_baud = 0.6;      // 1/baud
    double q_jump_center_bauds = 14.0;      // bauds
    double q_ramp_au2_per_baud = 0.01;      // AU² per baud
    double q_ramp_rate_per_baud = 0.2;      // 1/baud
    double q_ramp_center_bauds = 1.0;       // bauds
    // key-up decay, space to space: Φ = 10^(-2/(22.4·Δ/ms)) per 5 ms step [p. 178 13900-14000]. Both numbers are
    // dimensionless as the listing applies them, with Δ in ms; together they make 10^(-1/56) per baud of amplitude
    // (-0.357 dB of amplitude per baud), independent of speed (sheet (b))
    double keyup_phi_exponent = -2.0;
    double keyup_phi_factor = 22.4;
    // dimensionless: the key-up Φ once B >= model_max_bauds [p. 178 14100]
    double phi_keyup_after_max = 1.0;
    // AU² per 5 ms step: Q from a space to a space [p. 178 14200]
    double q_keyup_au2 = 0.0;

    // ---- XTRANS (pp. 170-171, 18600-24100) ----
    // dots, KIMAP: nominal length m of each element, dot ... pause [p. 170 20500]
    std::array<int, 6> xtrans_kimap = {1, 3, 1, 3, 7, 14};
    // per baud, APARM: the Laplacian density's rate α for dot, dash, e-sp, c-sp; w-sp; pause; XTRANS uses
    // α_m = m·APARM [p. 170 20600, 21800-22400]. The text derives 3.61, 1.81, 0.90 (p. 98); the listing's are used
    std::array<double, 3> xtrans_aparm = {3.000, 1.500, 1.000};

    // ---- SPDTR (p. 172) ----
    // WPM: a speed branch leaving [min_wpm, max_wpm] has probability 0 [p. 172 35100-35200]
    int min_wpm = 10;
    int max_wpm = 60;

    // ---- SAVEP (pp. 180-183) ----
    // probability, POPT: keep nodes until their total reaches this [p. 181 30800, p. 182 36100]
    double p_opt = 0.90;
    // probability: an element's best node is kept if its P >= this [p. 181 32500]
    double element_best_min_probability = 0.000001;

    // ---- TRELIS (pp. 184-186) ----
    // s: the longest decision delay, 199 decoder samples at 5 ms, for converged and forced decisions alike; the
    // trellis store holds decision_delay_s/step_s + 1 = 200 stages, NDELAY [p. 184 02200, p. 185 07300-08600,
    // p. 186 14500-14700] (derived, sheet (b)); the text says 200 samples, 1 s (p. 123)
    double decision_delay_s = 0.995;
    // PTHTRL = 5000*5 and LMDSAV = 5000*5: parent index and state of every stored node at the start [p. 184 02100]
    int initial_trellis_value = 5;
    // IPNOD = 25*1 [p. 184 02300]
    int initial_ipnod = 1;

    // ---- INITL's IBLANK and TRANSL's layout ----
    // blanks after a letter ended by a w-sp (IELMST 3, 7) and by a pause (IELMST 4, 8) [p. 161 11100-11400]
    int iblank_word_space = 1;
    int iblank_pause = 2;
    // characters per output line: a new line and 10 blanks when NCHAR reaches 52 [p. 188 28300-28600]
    int transl_line_chars = 52;
    int transl_indent_chars = 10;

    // ---- INITL's tables (p. 161) ----
    // key state of elements 1-6, ISX [p. 161 06300] and ILAMX [p. 161 07500] (equal)
    std::array<int, 6> isx = {1, 1, 0, 0, 0, 0};
    std::array<int, 6> ilamx = {1, 1, 0, 0, 0, 0};
    // ILAM1: the current element of element-state m = 1 ... 16 [p. 161 07400]
    std::array<int, 16> ilam1 = {3, 4, 5, 6, 3, 4, 5, 6, 1, 2, 1, 2, 1, 2, 1, 2};
    // ELEMTR(m, k): probability of element k next, from element-state m (Table XII as listed) [p. 161 08700-09200].
    // Row m; the listing's DATA is column-major. Row 9 sums to 0.998, as listed
    std::array<std::array<double, 6>, 16> elemtr = {{
        {0.55, 0.45, 0.0, 0.0, 0.0, 0.0},         // 1  e-sp after dot
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 2  c-sp after dot
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 3  w-sp after dot
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 4  pause after dot
        {0.55, 0.45, 0.0, 0.0, 0.0, 0.0},         // 5  e-sp after dash
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 6  c-sp after dash
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 7  w-sp after dash
        {0.5, 0.5, 0.0, 0.0, 0.0, 0.0},           // 8  pause after dash
        {0.0, 0.0, 0.581, 0.335, 0.067, 0.015},   // 9  dot after e-sp
        {0.0, 0.0, 0.54, 0.376, 0.069, 0.015},    // 10 dash after e-sp
        {0.0, 0.0, 0.923, 0.062, 0.012, 0.003},   // 11 dot after c-sp
        {0.0, 0.0, 0.923, 0.062, 0.012, 0.003},   // 12 dash after c-sp
        {0.0, 0.0, 0.923, 0.062, 0.012, 0.003},   // 13 dot after w-sp
        {0.0, 0.0, 0.923, 0.062, 0.012, 0.003},   // 14 dash after w-sp
        {0.0, 0.0, 0.95, 0.04, 0.009, 0.001},     // 15 dot after pause
        {0.0, 0.0, 0.95, 0.04, 0.009, 0.001},     // 16 dash after pause
    }};
    // RTRANS(i, c): probability of speed branch i = 1 ... 5 (change -2, -1, 0, +1, +2 steps of MEMDEL) in column c
    // (Table X) [p. 161 09500]. Row i
    std::array<std::array<double, 2>, 5> rtrans = {{
        {0.1, 0.15},
        {0.2, 0.2},
        {0.4, 0.3},
        {0.2, 0.2},
        {0.1, 0.15},
    }};
    // MEMDEL(from, to): WPM, the speed step when the element changes from `from` to `to` [p. 161 09600-09800]. Row
    // `from`
    std::array<std::array<int, 6>, 6> memdel = {{
        {0, 0, 2, 2, 2, 2},    // dot
        {0, 0, 2, 2, 2, 2},    // dash
        {2, 2, 0, 0, 0, 0},    // e-sp
        {2, 2, 0, 0, 0, 0},    // c-sp
        {5, 5, 0, 0, 0, 0},    // w-sp
        {10, 10, 0, 0, 0, 0},  // pause
    }};
    // MEMPR(from, to): the RTRANS column of the transition, 0: no transition [p. 161 09900-10000]. Row `from`
    std::array<std::array<int, 6>, 6> mempr = {{
        {0, 0, 1, 1, 1, 1},  // dot
        {0, 0, 1, 1, 1, 1},  // dash
        {1, 1, 0, 0, 0, 0},  // e-sp
        {2, 2, 0, 0, 0, 0},  // c-sp
        {1, 1, 0, 0, 0, 0},  // w-sp
        {2, 2, 0, 0, 0, 0},  // pause
    }};
};

}  // namespace kz4ap::bell
