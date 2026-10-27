#include "kz4ap/viterbi/systematic.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kz4ap::viterbi {

CarrierWindow::CarrierWindow(int back_samples, int ahead_samples) : back_(back_samples), ahead_(ahead_samples) {
    if (back_samples < 0 || ahead_samples < 0)
        throw std::invalid_argument("CarrierWindow: the memory and look-ahead must not be negative (samples)");
}

void CarrierWindow::push(double e2_fs2) {
    if (finished_) throw std::logic_error("CarrierWindow: push after finish");
    if (!(e2_fs2 >= 0.0) || !std::isfinite(e2_fs2))
        throw std::invalid_argument("CarrierWindow: e^2 must be finite and not negative (FS^2)");
    pending_.push_back(e2_fs2);
    ++pushed_;
}

std::optional<double> CarrierWindow::next_peak_fs2() {
    const std::size_t ahead = static_cast<std::size_t>(ahead_);
    // Ready once sample next_ + ahead exists, or once the stream has ended and next_ exists.
    if (!(pushed_ > next_ + ahead || (finished_ && next_ < pushed_))) return std::nullopt;
    // Admit the samples up to next_ + ahead only: a later one would drop smaller values this window still needs.
    const std::size_t last = std::min(next_ + ahead, pushed_ - 1);
    while (admitted_ <= last) {
        const double value = pending_.front();
        pending_.pop_front();
        while (!peaks_.empty() && peaks_.back().second <= value) peaks_.pop_back();
        peaks_.emplace_back(admitted_, value);
        ++admitted_;
    }
    // Drop the samples before next_ - back.
    const std::size_t back = static_cast<std::size_t>(back_);
    while (peaks_.front().first + back < next_) peaks_.pop_front();
    ++next_;
    return peaks_.front().second;
}

void CarrierWindow::finish() { finished_ = true; }

double v_prime_fs2(double sigma2_fs2, double window_peak_fs2, double e_fs, double s, double v, double floor) {
    const double s2 = s * s;
    const double c2 = s != 0.0 ? std::max(window_peak_fs2, e_fs * e_fs / s2) : window_peak_fs2;
    return sigma2_fs2 + c2 * (floor * floor * s2 + v);
}

double score_cap_nats(double w, double s, double v, double floor) {
    if (s == 0.0) return 0.0;
    const double s2 = s * s;
    const double leeway = floor * floor * s2 + v;
    if (!(leeway > 0.0)) return std::numeric_limits<double>::infinity();
    return w * s2 / leeway;
}

}  // namespace kz4ap::viterbi
