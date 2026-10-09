// Bell's model routines (reference sheet (b); listing pp. 170-178), one C++ function per Fortran routine, named after
// it in lower case, with its arguments in the same order and with the same meaning; the COMMON blocks they read are
// the trailing arguments (the letter tree and the configuration). Times are in ms, as in the listing (D0, DUR); speeds
// in WPM; Bell's codes 1-based. Double precision throughout.
#pragma once

#include "kz4ap/bell/config.hpp"
#include "kz4ap/bell/tree.hpp"

#include <array>

namespace kz4ap::bell {

// XTRANS(IELEM, D0, IRATE) [p. 170 18600-24100]: the probability that element `ielem`, keyed for `d0_ms` ms so far
// at `irate` WPM, lasts at least one more 5 ms step. With m = KIMAP(ielem) dots, Δ = 1200 ms/irate, b0 = D0/(mΔ),
// b1 = (D0 + 5 ms)/(mΔ) and α_m = m·APARM (3.0 m for dot ... c-sp, 7·1.5 for the w-sp, 14·1.0 for the pause):
//   b1 <= 1:       (1 - ½e^{α_m(b1-1)}) / (1 - ½e^{α_m(b0-1)})
//   b0 < 1 < b1:   ½e^{-α_m(b1-1)} / (1 - ½e^{α_m(b0-1)})
//   otherwise:     e^{-α_m(b1-b0)}
double xtrans(int ielem, double d0_ms, int irate, const BellConfig& config);

// PTRANS(KELEM, IRATE, LAMBDA, ILRATE, PTRX, PSUM, PIN, N) [p. 171 24600-30200]: the unnormalized probability PIN(n)
// of the successor with element `kelem` and speed branch `irate` (1-5) of a path in state `lambda` at `ilrate` WPM,
// whose stay probability is `ptrx`; adds it to `psum`. With e the path's element ILAM1(IELMST(lambda)):
//   kelem = e:    ptrx if irate = 3, else 0
//   kelem != e:   (1 - ptrx)·ELEMTR(IELMST(lambda), kelem)·SPDTR(irate, ilrate, kelem, e)
// It does not consult MEMFCN (sheet (b)). `lambda` must not be 0 (TRPROB handles state 0 before calling it).
void ptrans(int kelem, int irate, int lambda, int ilrate, double ptrx, double& psum,
            std::array<double, kSuccessors>& pin, int n, const MorseTree& tree, const BellConfig& config);

// SPDTR(ISRT, ILRT, ISELM, ILELM) [p. 172 30800-35500]: the probability of speed branch `isrt` (1-5) from a path at
// `ilrt` WPM in element `ilelm` to a successor in element `iselm`: 1 for branch 3 and 0 otherwise if the element is
// unchanged; else RTRANS(isrt, MEMPR(ilelm, iselm)) (0 if MEMPR is 0), and 0 if the new speed
// ilrt + (isrt - 3)·MEMDEL(ilelm, iselm) is above 60 or below 10 WPM.
double spdtr(int isrt, int ilrt, int iselm, int ilelm, const BellConfig& config);

// MODEL(DUR, IELM, ILR, ISR, IXS, PHI, QA, HZ) [pp. 177-178 09500-14500]: the amplitude model of a transition from a
// path in element `ielm`, `dur_ms` ms into it at `ilr` WPM, to a successor of key state `ixs`. HZ = ixs; with
// B = min(dur_ms/Δ, 14) bauds, Δ = 1200 ms/ilr:
//   path in a mark (ielm <= 2):              Φ = 1, Q = 10⁻⁴ AU² per 5 ms step
//   path in a space, successor a mark:       Φ = 1, Q = 0.15·e^{0.6(B-14)} + 0.01·B·e^{0.2(1-B)} AU² per 5 ms step
//   path in a space, successor a space:      Φ = 10^(-2/(22.4·Δ/ms)) per 5 ms step (1 once B >= 14), Q = 0
// `isr` (the successor's speed branch) is unused, as in the listing.
void model(double dur_ms, int ielm, int ilr, int isr, int ixs, double& phi, double& qa, double& hz,
           const BellConfig& config);

}  // namespace kz4ap::bell
