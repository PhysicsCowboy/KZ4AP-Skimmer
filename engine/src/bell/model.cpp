// Bell's model routines XTRANS, PTRANS, SPDTR and MODEL (listing pp. 170-178); see model.hpp. Each statement follows
// the listing's: the same operations in the same order, so the double-precision results equal those of the listing
// compiled in double precision up to the math library's exp and pow.
#include "kz4ap/bell/model.hpp"

#include <cmath>

namespace kz4ap::bell {

double xtrans(int ielem, double d0_ms, int irate, const BellConfig& config) {
    const int mscale = at1(config.xtrans_kimap, ielem);              // 21100
    const double rscale = ms(config.dot_length_wpm_s) / irate;       // 21200: 1200./IRATE
    const double b0 = d0_ms / (mscale * rscale);                     // 21300
    const double b1 = (d0_ms + ms(config.step_s)) / (mscale * rscale);  // 21400
    double alpha = 0.0;
    if (ielem == kPause)
        alpha = config.xtrans_alpha_dots_pause * at1(config.xtrans_aparm, 3);       // 22400: 14.*APARM(3)
    else if (ielem == kWordSpace)
        alpha = config.xtrans_alpha_dots_word_space * at1(config.xtrans_aparm, 2);  // 22100: 7.*APARM(2)
    else
        alpha = mscale * at1(config.xtrans_aparm, 1);                                 // 21800
    if (b1 <= 1.0) {                                                  // 22600, 23100-23300
        const double p1 = 1.0 - 0.5 * std::exp(alpha * (b1 - 1.0));
        const double p0 = 1.0 - 0.5 * std::exp(alpha * (b0 - 1.0));
        return p1 / p0;
    }
    if (b0 < 1.0 && b1 > 1.0) {                                       // 22700, 23600-23800
        const double p1 = 0.5 * std::exp(-alpha * (b1 - 1.0));
        const double p0 = 1.0 - 0.5 * std::exp(alpha * (b0 - 1.0));
        return p1 / p0;
    }
    return std::exp(-alpha * (b1 - b0));                              // 22800
}

void ptrans(int kelem, int irate, int lambda, int ilrate, double ptrx, double& psum,
            std::array<double, kSuccessors>& pin, int n, const MorseTree& tree, const BellConfig& config) {
    const int element_state = at1(tree.ielmst, lambda);
    const int element = at1(config.ilam1, element_state);
    double& p = at1(pin, n);
    if (kelem == element) {                                           // 27800-28000
        p = ptrx;
        if (irate != 3) p = 0.0;
    } else {
        const double pelem = at1(at1(config.elemtr, element_state), kelem);  // 28800
        const double prate = spdtr(irate, ilrate, kelem, element, config);   // 29300
        p = (1.0 - ptrx) * pelem * prate;                                     // 29800
    }
    psum = psum + p;                                                  // 29900
}

double spdtr(int isrt, int ilrt, int iselm, int ilelm, const BellConfig& config) {
    if (ilelm == iselm) return isrt == 3 ? 1.0 : 0.0;                 // 33300-33500
    const int idel = at1(at1(config.memdel, ilelm), iselm);           // 34200
    const int ind1 = at1(at1(config.mempr, ilelm), iselm);            // 34300
    if (ind1 == 0) return 0.0;                                        // 34400-34500
    const int idelsp = (isrt - 3) * idel;                             // 34800
    double p = at1(at1(config.rtrans, isrt), ind1);                   // 34900
    const int israte = ilrt + idelsp;                                 // 35000
    if (israte > config.max_wpm) p = 0.0;                             // 35100
    if (israte < config.min_wpm) p = 0.0;                             // 35200
    return p;
}

void model(double dur_ms, int ielm, int ilr, [[maybe_unused]] int isr, int ixs, double& phi, double& qa, double& hz,
           const BellConfig& config) {
    hz = ixs;                                                         // 11900
    const double r1 = ms(config.dot_length_wpm_s) / ilr;              // 12400: 1200./ILR
    double bauds = dur_ms / r1;                                       // 12500
    if (bauds >= config.model_max_bauds) bauds = config.model_max_bauds;  // 12600
    if (ielm < kElementSpace) {                                       // 12800-13000: the path is in a mark
        qa = config.q_mark_au2;
        phi = config.phi_mark;
        return;
    }
    if (ixs != 0) {                                                   // 13300-13600: a space to a mark
        phi = config.phi_space_to_mark;
        qa = config.q_jump_au2 * std::exp(config.q_jump_rate_per_baud * (bauds - config.q_jump_center_bauds));
        qa = qa + config.q_ramp_au2_per_baud * bauds *
                      std::exp(config.q_ramp_rate_per_baud * (config.q_ramp_center_bauds - bauds));
        return;
    }
    const double xsamp = config.keyup_phi_factor * r1;                // 13900: a space to a space
    phi = std::pow(10.0, config.keyup_phi_exponent / xsamp);          // 14000
    if (bauds >= config.model_max_bauds) phi = config.phi_keyup_after_max;  // 14100
    qa = config.q_keyup_au2;                                          // 14200
}

}  // namespace kz4ap::bell
