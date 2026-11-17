#include "kz4ap/keying_coherence.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kz4ap {

namespace {

// x less the mean of m consecutive values around it, into out: centered on it where it can be, slid inward at the series'
// ends so that it always holds m values (all of them when m >= the series' length).
void high_pass(std::span<const float> x, std::size_t m, std::vector<double>& prefix, std::vector<double>& out) {
    const std::size_t n = x.size();
    prefix.assign(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + static_cast<double>(x[i]);
    out.resize(n);
    const std::size_t half = m / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i >= half ? i - half : 0;
        const std::size_t hi = std::min(n, lo + m);
        const std::size_t from = hi >= m ? hi - m : 0;
        out[i] = static_cast<double>(x[i]) - (prefix[hi] - prefix[from]) / static_cast<double>(hi - from);
    }
}

}  // namespace

KeyingCoherence keying_coherence(std::span<const float> weak, std::span<const float> strong, std::size_t detrend_blocks) {
    if (weak.size() != strong.size()) throw std::invalid_argument("keying_coherence: the series differ in length");
    if (detrend_blocks < 1) throw std::invalid_argument("keying_coherence: detrend_blocks must be at least 1");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (weak.size() < 2) return {nan, nan};
    thread_local std::vector<double> prefix, a, b;
    const std::size_t m = std::min(detrend_blocks, weak.size());
    if (m == 1) {
        // The plain mean.
        high_pass(weak, weak.size(), prefix, a);
        high_pass(strong, strong.size(), prefix, b);
    } else {
        high_pass(weak, m, prefix, a);
        high_pass(strong, m, prefix, b);
    }
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        ab += a[i] * b[i];
        aa += a[i] * a[i];
        bb += b[i] * b[i];
    }
    if (!(aa > 0.0) || !(bb > 0.0)) return {nan, nan};
    double sw = 0.0, ss = 0.0;
    for (std::size_t i = 0; i < weak.size(); ++i) {
        sw += static_cast<double>(weak[i]);
        ss += static_cast<double>(strong[i]);
    }
    const double n = static_cast<double>(weak.size());
    return {ab / std::sqrt(aa * bb), ab / bb, sw / n, ss / n, aa / n, bb / n};
}

EnvelopeHistory::EnvelopeHistory(std::size_t capacity) : ring_(capacity, 0.0f) {}

void EnvelopeHistory::push(std::uint64_t block, float value) {
    if (ring_.empty()) return;
    if (size_ > 0 && block != newest_ + 1) size_ = 0;
    ring_[next_] = value;
    next_ = (next_ + 1) % ring_.size();
    size_ = std::min(size_ + 1, ring_.size());
    newest_ = block;
}

void EnvelopeHistory::last(std::size_t n, std::vector<float>& out) const {
    n = std::min(n, size_);
    out.resize(n);
    const std::size_t cap = ring_.size();
    for (std::size_t i = 0; i < n; ++i) out[i] = ring_[(next_ + cap - n + i) % cap];
}

bool EnvelopeHistory::range(std::uint64_t first, std::uint64_t last, std::vector<float>& out) const {
    if (size_ == 0 || first > last || first < oldest_block() || last > newest_) return false;
    const std::size_t n = static_cast<std::size_t>(last - first + 1);
    const std::size_t back = static_cast<std::size_t>(newest_ - last);  // values after `last`
    const std::size_t cap = ring_.size();
    out.resize(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = ring_[(next_ + 2 * cap - back - n + i) % cap];
    return true;
}

}  // namespace kz4ap
