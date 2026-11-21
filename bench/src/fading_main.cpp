// kz4ap-fading: how fast one station's carrier fades, measured on a span of an I/Q recording (text-level-1; the owner's "auroral"
// clips). The station is mixed to 0 Hz, low-passed (Blackman-windowed sinc, cutoff 200 Hz) and decimated to about 1 500 samples/s;
// its key-down samples are those above +10 dB re the noise (the noise from the 20th percentile of |y|^2 in 10.67 ms blocks, as an
// exponential's), in runs of at least 8 ms less 3 ms at each end. Within the runs it reports:
//   - the complex gain's correlation |rho(tau)| = |sum y[k+tau] y*[k]| / sum |y|^2 over pairs inside one run, the noise taken out of
//     the power, tau = 1 ... 40 samples; and from it the Doppler spread by the Gaussian model |rho(tau)| = exp(-2 pi^2 sigma^2 tau^2),
//     least squares on ln |rho| where 0.2 <= |rho| <= 0.97: f_D = 2 sigma (Watterson's and DeepCW's two-sigma width; the
//     generator's notes, docs/research/deepcw-generator-notes.md). The carrier's mean frequency is removed first (the lag-1 phase).
//   - the slow fading: the median key-down level per second, dB re the noise, its range and spread.
//   - the median mark and gap lengths, ms (key-down runs with gaps under 6 ms closed; gaps up to 300 ms).
//   kz4ap-fading FILE.wav --offset-hz F --start S --seconds T [--rho RHO.csv]
#include "kz4ap/wav_reader.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using C = std::complex<double>;

double median(std::vector<double> v) {
    if (v.empty()) return std::nan("");
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: kz4ap-fading FILE.wav --offset-hz F --start S --seconds T [--rho RHO.csv]");
        std::string file = argv[1], rho_csv;
        double offset_hz = 0.0, start_s = 0.0, seconds = 0.0;
        for (int i = 2; i + 1 < argc; i += 2) {
            const std::string a = argv[i], v = argv[i + 1];
            if (a == "--offset-hz") offset_hz = std::stod(v);
            else if (a == "--start") start_s = std::stod(v);
            else if (a == "--seconds") seconds = std::stod(v);
            else if (a == "--rho") rho_csv = v;
            else throw std::runtime_error("unknown option " + a);
        }
        kz4ap::WavIqReader reader(file);
        const double fs = reader.sample_rate();
        const int decimation = std::max(1, static_cast<int>(std::lround(fs / 1500.0)));
        const double rate = fs / decimation;  // samples/s after decimation
        reader.seek(static_cast<std::uint64_t>(std::llround(start_s * fs)));
        std::vector<kz4ap::Sample> raw(static_cast<std::size_t>(std::llround(seconds * fs)));
        raw.resize(reader.read(raw));
        // The low-pass: Blackman-windowed sinc, cutoff 200 Hz, 8 decimations each side.
        const int half = 8 * decimation;
        std::vector<double> taps(static_cast<std::size_t>(2 * half + 1));
        double sum = 0.0;
        for (int n = -half; n <= half; ++n) {
            const double x = 2.0 * 200.0 / fs * n;
            const double sinc = n == 0 ? 1.0 : std::sin(std::numbers::pi * x) / (std::numbers::pi * x);
            const double w = 0.42 + 0.5 * std::cos(std::numbers::pi * n / half) + 0.08 * std::cos(2.0 * std::numbers::pi * n / half);
            taps[static_cast<std::size_t>(n + half)] = sinc * w;
            sum += sinc * w;
        }
        for (double& t : taps) t /= sum;
        std::vector<C> mixed(raw.size());
        for (std::size_t k = 0; k < raw.size(); ++k)
            mixed[k] = C(raw[k].real(), raw[k].imag()) * std::polar(1.0, -2.0 * std::numbers::pi * offset_hz * static_cast<double>(k) / fs);
        std::vector<C> y;
        for (std::size_t c = static_cast<std::size_t>(half); c + static_cast<std::size_t>(half) < raw.size(); c += static_cast<std::size_t>(decimation)) {
            C acc = 0.0;
            for (int n = -half; n <= half; ++n) acc += taps[static_cast<std::size_t>(n + half)] * mixed[c + static_cast<std::size_t>(n)];
            y.push_back(acc);
        }
        const std::size_t n = y.size();
        // The noise: the 20th percentile of the block means of |y|^2 over 16-sample blocks, as an exponential's quantile / 16 averaged
        // values is nearly the mean; taken as the mean of the blocks at or under it (heuristic, for a 30 dB station).
        std::vector<double> blocks;
        for (std::size_t b = 0; b + 16 <= n; b += 16) {
            double s = 0.0;
            for (std::size_t k = b; k < b + 16; ++k) s += std::norm(y[k]);
            blocks.push_back(s / 16.0);
        }
        std::vector<double> sorted = blocks;
        std::sort(sorted.begin(), sorted.end());
        double noise = 0.0;
        const std::size_t q = std::max<std::size_t>(1, sorted.size() / 5);
        for (std::size_t i = 0; i < q; ++i) noise += sorted[i];
        noise /= static_cast<double>(q);
        // Key-down runs.
        std::vector<char> down(n);
        for (std::size_t k = 0; k < n; ++k) down[k] = static_cast<char>(std::norm(y[k]) > 10.0 * noise);
        const int close = static_cast<int>(std::lround(0.006 * rate));
        std::vector<std::pair<std::size_t, std::size_t>> runs;  // [begin, end) after trimming 3 ms
        const std::size_t trim = static_cast<std::size_t>(std::lround(0.003 * rate)), min_len = static_cast<std::size_t>(std::lround(0.008 * rate));
        for (std::size_t k = 0; k < n;) {
            if (!down[k]) { ++k; continue; }
            std::size_t e = k;
            while (e < n && down[e]) ++e;
            if (e - k >= min_len + 2 * trim) runs.push_back({k + trim, e - trim});
            k = e;
        }
        // Mean frequency from the lag-1 phase, removed.
        C lag1 = 0.0;
        for (const auto& [b, e] : runs)
            for (std::size_t k = b; k + 1 < e; ++k) lag1 += y[k + 1] * std::conj(y[k]);
        const double omega = std::arg(lag1);  // rad per sample
        for (std::size_t k = 0; k < n; ++k) y[k] *= std::polar(1.0, -omega * static_cast<double>(k));
        // |rho(tau)|.
        double power = 0.0;
        std::size_t count = 0;
        for (const auto& [b, e] : runs)
            for (std::size_t k = b; k < e; ++k) { power += std::norm(y[k]); ++count; }
        const double signal = power / static_cast<double>(count) - noise;
        std::vector<double> rho(41, 0.0);
        for (int tau = 1; tau <= 40; ++tau) {
            C r = 0.0;
            double p = 0.0;
            for (const auto& [b, e] : runs)
                for (std::size_t k = b; k + static_cast<std::size_t>(tau) < e; ++k) {
                    r += y[k + static_cast<std::size_t>(tau)] * std::conj(y[k]);
                    p += std::norm(y[k]) - noise;
                }
            rho[static_cast<std::size_t>(tau)] = p > 0.0 ? std::abs(r) / p : std::nan("");
        }
        double sxx = 0.0, sxy = 0.0;
        int used = 0;
        for (int tau = 1; tau <= 40; ++tau) {
            const double r = rho[static_cast<std::size_t>(tau)];
            if (!(r >= 0.2 && r <= 0.97)) continue;
            const double t = tau / rate;
            sxx += t * t * t * t;
            sxy += t * t * std::log(r);
            ++used;
        }
        const double sigma = used > 0 && sxy < 0.0 ? std::sqrt(-sxy / sxx / (2.0 * std::numbers::pi * std::numbers::pi)) : std::nan("");
        // Slow fading: the median key-down level per second.
        std::vector<double> levels;
        for (std::size_t s = 0; s + static_cast<std::size_t>(rate) <= n; s += static_cast<std::size_t>(rate)) {
            std::vector<double> v;
            for (const auto& [b, e] : runs)
                for (std::size_t k = std::max(b, s); k < std::min(e, s + static_cast<std::size_t>(rate)); ++k) v.push_back(std::norm(y[k]));
            if (v.size() > 20) levels.push_back(10.0 * std::log10(median(v) / noise));
        }
        double mean_level = 0.0, var_level = 0.0;
        for (double l : levels) mean_level += l;
        mean_level /= std::max<std::size_t>(1, levels.size());
        for (double l : levels) var_level += (l - mean_level) * (l - mean_level);
        var_level /= std::max<std::size_t>(1, levels.size());
        // Durations, with gaps under 6 ms closed.
        std::vector<char> keyed = down;
        for (std::size_t k = 0; k < n;) {
            if (keyed[k]) { ++k; continue; }
            std::size_t e = k;
            while (e < n && !keyed[e]) ++e;
            if (k > 0 && e < n && static_cast<int>(e - k) < close) std::fill(keyed.begin() + static_cast<std::ptrdiff_t>(k), keyed.begin() + static_cast<std::ptrdiff_t>(e), static_cast<char>(1));
            k = e;
        }
        std::vector<double> marks, gaps;
        for (std::size_t k = 0; k < n;) {
            std::size_t e = k;
            while (e < n && keyed[e] == keyed[k]) ++e;
            const double ms = 1000.0 * static_cast<double>(e - k) / rate;
            if (k > 0 && e < n) (keyed[k] ? marks : gaps).push_back(ms);
            k = e;
        }
        std::vector<double> short_gaps;
        for (double g : gaps) if (g < 300.0) short_gaps.push_back(g);
        std::sort(marks.begin(), marks.end());
        const auto pct = [](const std::vector<double>& v, double p) { return v.empty() ? std::nan("") : v[static_cast<std::size_t>(p * (v.size() - 1))]; };
        std::printf("rate %.1f samples/s; key-down samples %zu in %zu runs; carrier %.2f Hz from the offset; key-down %.1f dB re the noise "
                    "(the noise %.3g FS^2 a sample)\n",
                    rate, count, runs.size(), omega * rate / (2.0 * std::numbers::pi), 10.0 * std::log10(signal / noise), noise);
        std::printf("|rho| at 2.67, 6.67, 13.3, 26.7 ms: %.3f %.3f %.3f %.3f; Gaussian fit over %d lags: sigma_D %.2f Hz, f_D = 2 sigma_D %.2f Hz\n",
                    rho[4], rho[10], rho[20], rho[40], used, sigma, 2.0 * sigma);
        std::printf("slow fading: %zu s; median key-down level per second %.1f dB re the noise, spread %.1f dB rms, range %.1f dB\n",
                    levels.size(), mean_level, std::sqrt(var_level),
                    levels.empty() ? 0.0 : *std::max_element(levels.begin(), levels.end()) - *std::min_element(levels.begin(), levels.end()));
        std::printf("marks %zu: 10th, 50th, 90th percentile %.1f %.1f %.1f ms; gaps under 300 ms %zu: median %.1f ms\n", marks.size(),
                    pct(marks, 0.1), pct(marks, 0.5), pct(marks, 0.9), short_gaps.size(), median(short_gaps));
        // The fall after a mark: mean |y|^2 j samples after each run's last sample at or above -6 dB re the run's median power, in dB re
        // that median, while no later run has started (an echo would stand as a shelf for its delay; a clean transmitter falls in a
        // few ms through the low-pass).
        {
            std::vector<double> total(61, 0.0);
            std::vector<int> count_at(61, 0);
            for (std::size_t r = 0; r < runs.size(); ++r) {
                const auto [b, e] = runs[r];
                if (e - b < 12) continue;
                std::vector<double> v;
                for (std::size_t k = b; k < e; ++k) v.push_back(std::norm(y[k]));
                const double level = median(v);
                std::size_t last = b;
                for (std::size_t k = b; k < std::min(n, e + 3 * trim); ++k) if (std::norm(y[k]) >= level / 4.0) last = k;
                const std::size_t next = r + 1 < runs.size() ? runs[r + 1].first - trim : n;
                for (std::size_t j = 0; j <= 60 && last + j < next; ++j) {
                    total[j] += std::norm(y[last + j]) / level;
                    ++count_at[j];
                }
            }
            std::printf("fall after a mark, dB re its median level (runs counted), at 0, 2.7, 5.3, 8, 13.3, 20, 26.7, 40 ms:");
            for (const int j : {0, 4, 8, 12, 20, 30, 40, 60})
                std::printf(" %.1f (%d)", count_at[static_cast<std::size_t>(j)] ? 10.0 * std::log10(total[static_cast<std::size_t>(j)] / count_at[static_cast<std::size_t>(j)]) : std::nan(""), count_at[static_cast<std::size_t>(j)]);
            std::printf("; the noise at %.1f\n", 10.0 * std::log10(noise / (signal + noise)));
        }
        if (!rho_csv.empty()) {
            std::ofstream out(rho_csv);
            out << "tau_ms,rho\n";
            for (int tau = 1; tau <= 40; ++tau) out << 1000.0 * tau / rate << "," << rho[static_cast<std::size_t>(tau)] << "\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "kz4ap-fading: %s\n", e.what());
        return 1;
    }
}
