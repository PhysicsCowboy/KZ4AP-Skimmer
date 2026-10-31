// The systematic term of a sample's predictive variance (design notes, "Detection, amplitude, frequency and the mark's
// shape", part 9 as amended 2026-10-29 by D23; plan 1b, Tasks 5 and 10). The decoder runs it (search.hpp;
// viterbi_decoder.hpp).
//
// The model's own mismatch (the template's spread over the edge's position and the transmitter's rise, and what the
// model leaves out: fading other than modeled, transmitter ripple, the receiver's AGC, a neighbor's leakage) is a
// systematic effect added in quadrature to the noise, proportional to the carrier's amplitude (D15, D16, D17), as a
// function of the story's own prediction, never of the observation (D23; Kennedy and O'Hagan 2001):
//
//     V_k = sigma_y^2 + C_k^2 (f^2 s_k^2 + v_k),   C_k^2 = mu_k|k-1^2 + P_k|k-1      (FS^2),
//
// C_k^2 the story's predicted carrier power E[a^2] under its prediction N(mu_k|k-1, P_k|k-1) (amplitude.hpp), fixed
// before the sample is scored; on the newborn's grid C^2 = a_i^2 at each point (newborn.hpp). sigma_y^2 is the front
// filter's output noise power (FS^2), s_k the story's template and v_k its edge term (1; mark_template.hpp), f the floor
// (1; 0.02, a placeholder until the prototype measures it; owner, 2026-10-23). Where s_k = 0 (and so v_k = 0), V_k =
// sigma_y^2 and the sample scores 0. V_k enters part 4's full density p(e_k | a, V_k), scored against the reference
// p0(e_k | sigma_y^2) shared by every story (observation.hpp, story_sample_score); it is never divided by w.
//
// The misfit bound (derived; design notes part 9): for the right story at the right amplitude a, a template error
// delta_k (a fraction of full) costs, against a perfect template scored with the same V_k, within 0.02 nats,
//     w [delta_k^2 / (f^2 s_k^2 + v_k) + (1/2) max(0, -ln(1 + delta_k / s_k))]      (nats per sample),
// 0.73 nats on the plateau for the 3.83% misfit of a 4.7 ms rise at f = 0.02 (Systematic.MisfitBound). It is a bound on
// the noise-free cost.
#pragma once

namespace kz4ap::viterbi {

// V_k (FS^2) = sigma2_fs2 + carrier_power_fs2 (floor^2 s^2 + v) (header). sigma2_fs2: sigma_y^2 (FS^2);
// carrier_power_fs2: C^2, the story's mu^2 + P from its prediction (FS^2; a_i^2 on the newborn's grid); s, v: the story's
// template and edge term (1); floor: f (1). Exactly sigma2_fs2 where s = 0 and v = 0.
double systematic_variance_fs2(double sigma2_fs2, double carrier_power_fs2, double s, double v, double floor);

}  // namespace kz4ap::viterbi
