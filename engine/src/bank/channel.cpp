#include "kz4ap/bank/channel.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace kz4ap::bank {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Python's int(round(x)) for the sample counts: ties to even.
std::int64_t round_samples(double x) { return static_cast<std::int64_t>(std::nearbyint(x)); }

// The start of the character that contains t (or the first after it); t if none (channel.py _char_start_at).
double char_start_at(const std::vector<Char>& chars, double t) {
    for (const auto& c : chars)
        if ((c.start_s <= t && t <= c.end_s) || c.start_s > t) return c.start_s;
    return t;
}

}  // namespace

// --- Output -------------------------------------------------------------------------------------------

void Output::append_new(const std::vector<Char>& chars) {
    const double last = chars_.empty() ? -std::numeric_limits<double>::infinity() : chars_.back().start_s;
    std::size_t first = chars.size();  // the fresh characters are chars[first ...]
    while (first > 0 && !(chars[first - 1].start_s <= last)) --first;
    chars_.insert(chars_.end(), chars.begin() + static_cast<std::ptrdiff_t>(first), chars.end());
}

void Output::replace_from(double from_s, const std::vector<Char>& chars, double t_s, const std::string& reason) {
    const double lo = t_s - reach_s_;
    const double cut = std::max(from_s, lo);  // Python max(from_s, lo): from_s unless lo is larger
    std::vector<Char> kept, old;
    for (const auto& c : chars_) {
        if (c.start_s < lo || c.end_s < cut)
            kept.push_back(c);
        else
            old.push_back(c);
    }
    double after = cut;  // max([cut] + kept ends): > cut only for a kept character that starts before lo
    for (const auto& c : kept)
        if (c.end_s > after) after = c.end_s;
    std::string old_text, new_text;
    for (const auto& c : old) old_text += c.text;
    const std::size_t from_index = kept.size();
    for (const auto& c : chars)
        if (c.start_s >= after) {
            kept.push_back(c);
            new_text += c.text;
        }
    chars_ = std::move(kept);
    if (old_text != new_text) {
        double first = cut;  // >= lo: the reach is at most reach_s
        if (!old.empty()) {
            first = old.front().start_s;
            for (const auto& c : old)
                if (c.start_s < first) first = c.start_s;
        }
        corrections_.push_back(Correction{t_s, first, t_s - first, old_text, new_text, reason, from_index});
    }
}

std::string Output::text() const {
    std::string s;
    for (const auto& c : chars_) s += c.text;
    return s;
}

// --- Branch -------------------------------------------------------------------------------------------

Branch::Branch(int index_, double length_s_, int n, double rate_hz, const BankConfig& cfg,
               const TextModel& text_model)
    : index(index_),
      length_s(length_s_),
      delay_s((n - 1) / (2.0 * rate_hz)),
      fit(cfg),
      rate_(rate_hz),
      cfg_(&cfg),
      text_model_(&text_model) {}

void Branch::on_edges(const std::vector<std::pair<std::int64_t, bool>>& changes, double a, const Prior& prior,
                      bool provisional) {
    const double var_t = resolution_var_s2(length_s, a, rate_);
    for (const auto& [n, down] : changes) {
        const double t = time(n);
        if (down) {
            if (up_at) {
                observe(false, t - *up_at, var_t, prior, provisional);
                end_space(t - *up_at, var_t, current);
            }
            down_at = t;
        } else if (down_at) {
            observe(true, t - *down_at, var_t, prior, provisional);
            ++marks_in_over;
            add_element(*down_at, t, var_t, current);
            up_at = t;
            down_at.reset();
        }
    }
}

void Branch::observe(bool is_mark, double d, double var_t, const Prior& prior, bool provisional) {
    // A provisional duration enters no fit; after the re-keying every duration enters the decoding fit, and the
    // rival fresh fit while there is one (channel.py Branch._observe).
    if (provisional || !(d > 0.0)) return;
    fit.add(is_mark, d, var_t);
    current = fit.best(prior.t_s, prior.weight);
    if (!rival) return;
    rival->add(is_mark, d, var_t);
    over_obs.push_back(Obs{is_mark, d, var_t});
    // The rival's best is needed only once fresh_wins can be true (lazily: the same value either way).
    std::optional<Fit> fresh;
    if (static_cast<int>(over_obs.size()) >= cfg_->fresh_fit_min_obs) fresh = rival->best(prior.t_s, prior.weight);
    if (fresh_wins(fresh, current, over_obs)) {
        fit = std::move(*rival);
        current = fresh;
        rival.reset();
    } else if (over_obs.size() >= rival->history_capacity()) {
        // the previous over's memory now weighs lambda^(4 N_mem) = e^-4 = 1.8% of the continued fit's: the
        // competition ends and the continued fit stays
        rival.reset();
    }
}

bool Branch::fresh_wins(const std::optional<Fit>& fresh, const std::optional<Fit>& old,
                        const std::vector<Obs>& obs) const {
    // At least fresh_fit_min_obs of this over's marks and spaces, and a log-likelihood gain on them above
    // 1/2 k ln n nats, k = kFitParameters, n = obs.size().
    const auto n = obs.size();
    if (!fresh || static_cast<int>(n) < cfg_->fresh_fit_min_obs) return false;
    if (!old) return true;
    const double gain = static_cast<double>(n) *
                        (observations_loglik(fresh, obs, *cfg_) - observations_loglik(old, obs, *cfg_));
    return gain > 0.5 * kFitParameters * std::log(static_cast<double>(n));
}

void Branch::add_element(double start, double end, double var_t, const std::optional<Fit>& f) {
    if (!f) return;  // a provisional mark with no fit to classify it: the re-keying decodes it
    if (elements.empty()) char_start = start;
    elements += classify_mark(*f, end - start, var_t, *cfg_) ? "-" : ".";
    char_end = end;
}

void Branch::end_space(double d, double var_t, const std::optional<Fit>& f) {
    if (!f) return;
    const std::string kind = classify_space(*f, d, var_t, *cfg_);
    if (kind != "element") finish_char();
    if (kind == "word") word_space();
}

void Branch::finish_char() {
    if (!elements.empty()) {
        chars.push_back(Char{decode_pattern(elements), char_start, char_end});
        elements.clear();
        word_open = true;
    }
}

void Branch::word_space() {
    if (word_open) {
        chars.push_back(Char{" ", char_end, char_end});
        word_open = false;
    }
}

double Branch::silence_limit_s() const {
    const double tg = current ? current->tg_s : 1.2 / cfg_->min_wpm;
    return std::max(cfg_->new_over_min_s, cfg_->new_over_gaps * tg);
}

bool Branch::new_over_due(std::int64_t n_now, bool key_down) const {
    return !key_down && up_at && time(n_now) - *up_at > silence_limit_s();
}

void Branch::start_over(std::int64_t n_now, const Prior& prior, bool was_unknown) {
    finish_char();
    word_space();
    if (!prev_fit && !fit.history().empty()) prev_fit = std::move(fit);
    fit = DurationFit(*cfg_);
    rival.reset();
    over_obs.clear();
    current = prev_fit ? prev_fit->best(prior.t_s, prior.weight) : std::nullopt;
    over_start_n = n_now;
    if (!was_unknown) unknown_since_n = timeout_from_n = n_now;
    over_pending = true;
    marks_in_over = 0;
    down_at.reset();
    up_at.reset();
}

Branch::RekeyResult Branch::rekey_over(std::span<const double> P_stretch, std::int64_t n0, double sigma2,
                                       const std::vector<double>& amp_candidates, double a_min, const Prior& prior) {
    struct Candidate {
        double score;
        double amp2;
        std::vector<int> key;
        std::optional<DurationFit> fit;
        std::optional<Fit> result;
        std::optional<DurationFit> rival;
        std::vector<TimedObs> obs;
        std::optional<double> down_at, up_at;
    };
    const double from_s = time(n0);
    std::optional<Candidate> best;
    for (const double amp2 : amp_candidates) {
        std::vector<int> key = rekey(P_stretch, sigma2, amp2, *cfg_, a_min);
        const double var_t = resolution_var_s2(length_s, std::sqrt(std::max(amp2, 0.0) / sigma2), rate_);
        // The stretch's marks and spaces (channel.py Branch._observations: edges of the key from key up).
        std::vector<TimedObs> obs;
        std::optional<double> d_at, u_at;
        int prev = 0;
        for (std::size_t i = 0; i < key.size(); ++i) {
            if (key[i] == prev) continue;
            prev = key[i];
            const double t = time(n0 + static_cast<std::int64_t>(i));
            if (key[i]) {
                if (u_at) obs.push_back(TimedObs{false, t - *u_at, var_t, *u_at, t});
                d_at = t;
            } else if (d_at) {
                obs.push_back(TimedObs{true, t - *d_at, var_t, *d_at, t});
                u_at = t;
                d_at.reset();
            }
        }
        std::vector<Obs> obs3;
        obs3.reserve(obs.size());
        for (const auto& o : obs) obs3.push_back(Obs{o.is_mark, o.d_s, o.var_t_s2});
        // The fresh and the continued fit take each observation in turn.
        DurationFit fresh(*cfg_);
        std::optional<DurationFit> cont;
        if (prev_fit) cont = prev_fit->copy();
        for (const auto& o : obs3) {
            fresh.add(o.is_mark, o.d_s, o.var_t_s2);
            if (cont) cont->add(o.is_mark, o.d_s, o.var_t_s2);
        }
        // The fresh fit's best is needed only if it decodes (no previous fit) or may win (lazily: same value).
        const bool need_fresh = !cont || static_cast<int>(obs3.size()) >= cfg_->fresh_fit_min_obs;
        std::optional<Fit> fresh_result;
        if (need_fresh) fresh_result = fresh.best(prior.t_s, prior.weight);
        Candidate c{0.0, amp2, std::move(key), std::nullopt, std::nullopt, std::nullopt, std::move(obs), d_at, u_at};
        if (!cont) {
            c.result = fresh_result;
            c.fit = std::move(fresh);
        } else {
            const std::optional<Fit> cont_result = cont->best(prior.t_s, prior.weight);
            if (fresh_wins(fresh_result, cont_result, obs3)) {
                c.result = fresh_result;
                c.fit = std::move(fresh);
            } else {
                c.result = cont_result;
                c.fit = std::move(cont);
                c.rival = std::move(fresh);
            }
        }
        c.score = observations_loglik(c.result, obs3, *cfg_);
        if (!best || c.score > best->score) best = std::move(c);
    }
    if (!best) throw std::invalid_argument("rekey_over needs at least one amplitude candidate");
    Candidate& b = *best;
    fit = std::move(*b.fit);
    prev_fit.reset();
    over_obs.clear();
    if (b.rival)
        for (const auto& o : b.obs) over_obs.push_back(Obs{o.is_mark, o.d_s, o.var_t_s2});
    if (b.rival && over_obs.size() < b.rival->history_capacity())
        rival = std::move(b.rival);
    else
        rival.reset();
    if (b.result) current = b.result;
    redecode(from_s, b.obs, current);
    int marks = 0;
    for (const auto& o : b.obs) marks += o.is_mark ? 1 : 0;
    marks_in_over = marks;
    down_at = b.down_at;
    up_at = b.up_at;
    return RekeyResult{b.amp2, !b.key.empty() && b.key.back() != 0, from_s, marks};
}

double Branch::clear_over(std::int64_t n0) {
    const double from_s = time(n0);
    redecode(from_s, {}, std::nullopt);
    marks_in_over = 0;
    down_at.reset();
    up_at.reset();
    return from_s;
}

void Branch::redecode(double from_s, const std::vector<TimedObs>& obs, const std::optional<Fit>& f) {
    std::erase_if(chars, [from_s](const Char& c) { return !(c.start_s < from_s); });
    word_open = !chars.empty() && chars.back().text != " ";
    elements.clear();
    if (!f) return;
    for (const auto& o : obs) {
        if (o.is_mark)
            add_element(o.start_s, o.end_s, o.var_t_s2, f);
        else
            end_space(o.d_s, o.var_t_s2, f);
    }
}

std::optional<double> Branch::text_logprob(int window) const {
    std::vector<std::string> recent;  // newest first, as the prototype collects them
    for (auto it = chars.rbegin(); it != chars.rend(); ++it) {
        if (static_cast<int>(recent.size()) >= window) break;
        if (it->text != " ") recent.push_back(it->text);
    }
    return text_model_->mean_logprob(recent);
}

// --- BankChannel --------------------------------------------------------------------------------------

BankChannel::BankChannel(const BankConfig& cfg, double rate_hz)
    : cfg_(cfg),
      rate_(rate_hz),
      n_(branch_samples(branch_lengths_s(cfg), rate_hz)),
      lengths_([&] {
          std::vector<double> l(n_.size());
          for (std::size_t k = 0; k < n_.size(); ++k) l[k] = n_[k] / rate_hz;
          return l;
      }()),
      noise_(make_noise(cfg_, rate_hz, n_)),
      keyer_(cfg_, rate_hz, lengths_),
      periodicity_(cfg_, rate_hz),
      selector_(cfg_, lengths_),
      out_(cfg.correction_reach_s),
      block_(static_cast<int>(std::max<std::int64_t>(1, round_samples(cfg.block_s * rate_hz)))),
      reach_(round_samples(cfg.correction_reach_s * rate_hz)),
      timeout_(round_samples(cfg.rekey_timeout_s * rate_hz)) {
    branches_.reserve(n_.size());
    for (std::size_t k = 0; k < n_.size(); ++k)
        branches_.emplace_back(static_cast<int>(k), lengths_[k], n_[k], rate_hz, cfg_, text_model_);
    const int n_max = n_.empty() ? 1 : *std::max_element(n_.begin(), n_.end());
    csum_ring_.assign(static_cast<std::size_t>(n_max) + 1, {0.0, 0.0});
    // How far back the window keeps u and P (samples): the re-key's stretch (at most correction_reach_s), the
    // noise estimates' look-back (the three-tap's 2 N_k, a spectrum segment with its mask's reach) and the
    // warm-up (read from the stream's start), with two blocks of margin. A bound, not a tuned value.
    keep_back_ = std::max<std::int64_t>(reach_, 0) + 3 * static_cast<std::int64_t>(n_max) +
                 round_samples((cfg.segment_s + 2.0 * cfg.guard_margin_s + cfg.noise_warmup_s) * rate_hz) +
                 2 * static_cast<std::int64_t>(block_);
    p_win_.rows = static_cast<int>(n_.size());
    p_win_.cols = 0;
}

BankChannel::~BankChannel() = default;

void BankChannel::push(std::span<const std::complex<double>> u) {
    if (finished_) throw std::logic_error("BankChannel::push after finish");
    for (const auto& x : u) {
        append_sample(x);
        if (total_ - processed_ >= block_) {
            process_block(processed_, processed_ + block_);
            processed_ += block_;
        }
    }
}

void BankChannel::finish() {
    if (finished_) return;
    finished_ = true;
    if (total_ > processed_) {
        process_block(processed_, total_);
        processed_ = total_;
    }
    for (auto& br : branches_) br.flush();
    if (!branches_.empty()) out_.append_new(branches_[static_cast<std::size_t>(selector_.current())].chars);
}

ChannelResult BankChannel::result() const {
    ChannelResult r = result_;
    r.text = out_.text();
    r.chars = out_.chars();
    r.corrections = out_.corrections();
    return r;
}

void BankChannel::append_sample(std::complex<double> x) {
    if (total_ - base_ >= p_win_.cols) compact();
    const std::size_t R = csum_ring_.size();
    const std::int64_t m = total_;  // this sample's index
    csum_ += x;                     // c[m + 1], numpy's cumsum order
    csum_ring_[static_cast<std::size_t>((m + 1) % static_cast<std::int64_t>(R))] = csum_;
    const auto col = static_cast<std::size_t>(m - base_);
    const auto cols = static_cast<std::size_t>(p_win_.cols);
    for (std::size_t k = 0; k < n_.size(); ++k) {
        const std::int64_t lo = std::max<std::int64_t>(m + 1 - n_[k], 0);
        const std::complex<double> lo_c = csum_ring_[static_cast<std::size_t>(lo % static_cast<std::int64_t>(R))];
        const double scale = 1.0 / n_[k];  // numpy's complex division by n + 0j
        const std::complex<double> v{(csum_.real() - lo_c.real()) * scale, (csum_.imag() - lo_c.imag()) * scale};
        p_win_.v[k * cols + col] = boxcar_power_f32(v);
    }
    u_win_[col] = x;
    ++total_;
}

void BankChannel::compact() {
    const std::int64_t cap_max = keep_back_ + block_ + round_samples(2.0 * rate_) + 1;
    const std::int64_t drop = processed_ - keep_back_ - base_;
    const std::int64_t valid = total_ - base_;
    if (p_win_.cols < cap_max || drop <= 0) {
        // grow (up to cap_max; beyond it only if nothing can be dropped, which the block loop rules out)
        const std::int64_t want = std::max<std::int64_t>(
            valid + 1, std::min<std::int64_t>(cap_max, std::max<std::int64_t>(2 * p_win_.cols, 4096)));
        Matrix grown;
        grown.rows = p_win_.rows;
        grown.cols = static_cast<int>(want);
        grown.v.assign(static_cast<std::size_t>(grown.rows) * static_cast<std::size_t>(want), 0.0);
        for (int k = 0; k < p_win_.rows; ++k)
            if (valid > 0)
                std::memcpy(&grown.v[static_cast<std::size_t>(k) * static_cast<std::size_t>(want)],
                            &p_win_.v[static_cast<std::size_t>(k) * static_cast<std::size_t>(p_win_.cols)],
                            static_cast<std::size_t>(valid) * sizeof(double));
        p_win_ = std::move(grown);
        u_win_.resize(static_cast<std::size_t>(want));
        return;
    }
    const auto keep = static_cast<std::size_t>(valid - drop);
    const auto d = static_cast<std::size_t>(drop);
    for (int k = 0; k < p_win_.rows; ++k) {
        double* row = &p_win_.v[static_cast<std::size_t>(k) * static_cast<std::size_t>(p_win_.cols)];
        std::memmove(row, row + d, keep * sizeof(double));
    }
    std::memmove(u_win_.data(), u_win_.data() + d, keep * sizeof(std::complex<double>));
    base_ += drop;
}

std::span<const double> BankChannel::p_row(int k, std::int64_t from, std::int64_t to) const {
    const double* row = &p_win_.v[static_cast<std::size_t>(k) * static_cast<std::size_t>(p_win_.cols)];
    return {row + (from - base_), static_cast<std::size_t>(std::max<std::int64_t>(0, to - from))};
}

void BankChannel::process_block(std::int64_t n0, std::int64_t n1) {
    const double t_now = static_cast<double>(n1) / rate_;
    const int K = static_cast<int>(n_.size());
    noise_->update(std::span<const std::complex<double>>(u_win_.data(), static_cast<std::size_t>(total_ - base_)),
                   p_win_, n0, n1, base_);
    const std::vector<double> sigma2 = noise_->sigma2();
    Matrix P;  // the block's |v|^2 (already rounded to float32, as run's P), FS^2
    P.rows = K;
    P.cols = static_cast<int>(n1 - n0);
    P.v.resize(static_cast<std::size_t>(K) * static_cast<std::size_t>(P.cols));
    for (int k = 0; k < K; ++k) {
        const auto row = p_row(k, n0, n1);
        std::copy(row.begin(), row.end(), P.v.begin() + static_cast<std::ptrdiff_t>(k) * P.cols);
    }
    const KeyStep s = keyer_.step(P, sigma2);
    if (K > 0) periodicity_.push(std::span<const double>(s.p.v.data(), static_cast<std::size_t>(s.p.cols)));
    const PeriodicityUpdate upd = periodicity_.update();
    prior_ = upd.t_p_s ? Prior{upd.t_p_s, 1.0} : Prior{std::nullopt, 0.0};  // the prior counts once T_P is confident
    if (upd.updated)
        result_.periodicity.push_back(PeriodicityRecord{t_now, upd.t_p_s ? *upd.t_p_s : kNaN, upd.confidence,
                                                        upd.window_s ? *upd.window_s : kNaN,
                                                        periodicity_.per_window()});
    const auto changes = edges(s.key, s.before, n0);
    for (int k = 0; k < K; ++k) {
        const auto ku = static_cast<std::size_t>(k);
        Branch& br = branches_[ku];
        if (!changes[ku].empty()) br.on_edges(changes[ku], s.a[ku], prior_, keyer_.unknown[ku]);
        if (br.new_over_due(n1, keyer_.key[ku] != 0)) {
            br.start_over(n1, prior_, keyer_.unknown[ku]);
            keyer_.start_over(k);
        } else if (keyer_.unknown[ku]) {
            // The re-keyed stretch starts where the amplitude became unknown (at most correction_reach_s back), so
            // it also covers the provisional characters of overs restarted since then.
            const std::int64_t st = std::max(br.unknown_since_n, n1 - reach_);
            std::optional<int> marks;
            double from_s = 0.0;
            const char* reason = nullptr;
            if (br.marks_in_over && keyer_.weight[ku] >= keyer_.rekey_weight) {
                // W_min of keyed time since the over started
                std::vector<double> candidates{keyer_.amp2[ku]};
                if (std::isfinite(keyer_.prev_amp2[ku])) candidates.push_back(keyer_.prev_amp2[ku]);
                const auto r = br.rekey_over(p_row(k, st, n1), st, sigma2[ku], candidates, keyer_.a_min[ku], prior_);
                keyer_.finish_over_start(k, r.amp2, r.key_now);
                from_s = r.from_s;
                marks = r.marks;
                reason = "rekey";
            } else if (n1 - br.timeout_from_n >= timeout_) {
                // W_min not reached within rekey_timeout_s: re-key what exists with the previous over's
                // amplitude; if there is none, or it keys nothing, the stretch's provisional characters are
                // deleted and the amplitude stays unknown (the time-out counts again from now).
                const double prev2 = keyer_.prev_amp2[ku];
                bool keys = false;
                if (std::isfinite(prev2)) {
                    const auto key = rekey(p_row(k, st, n1), sigma2[ku], prev2, cfg_, keyer_.a_min[ku]);
                    keys = std::any_of(key.begin(), key.end(), [](int x) { return x != 0; });
                }
                if (keys) {
                    const auto r = br.rekey_over(p_row(k, st, n1), st, sigma2[ku], {prev2}, keyer_.a_min[ku], prior_);
                    keyer_.finish_over_start(k, r.amp2, r.key_now);
                    from_s = r.from_s;
                    marks = r.marks;
                } else {
                    from_s = br.clear_over(st);
                    br.timeout_from_n = n1;
                    keyer_.start_over(k);  // still unknown: W_min of keyed time counts afresh from now
                }
                reason = "timeout";
            }
            if (reason != nullptr) {
                const bool keyed_marks = marks && *marks > 0;
                if (keyed_marks && br.over_pending && k == selector_.current())
                    result_.over_starts.push_back(br.time(br.over_start_n));
                if (keyed_marks) br.over_pending = false;  // a re-key that keyed no mark leaves the over unconfirmed
                if (k == selector_.current()) out_.replace_from(from_s, br.chars, t_now, reason);
            }
        }
    }
    int instants = 0;  // selection instants: branch 1's key-ups in this block
    if (K > 0)
        for (const auto& e : changes[0]) instants += e.second ? 0 : 1;
    if (instants) {
        std::vector<BranchView> views;
        views.reserve(branches_.size());
        for (const auto& br : branches_)
            views.push_back(BranchView{br.index, br.length_s, br.current, br.text_logprob(cfg_.text_window_chars)});
        const int old = selector_.current();
        const int now = selector_.update(views, instants, t_now, prior_.t_s);
        const Branch& nb = branches_[static_cast<std::size_t>(now)];
        if (now != old) {
            ++result_.switches;
            const auto since = selector_.eligible_since()[static_cast<std::size_t>(now)];  // stream time, s
            // A fallback pick was never eligible: its text replaces from t_now, the switch's own time.
            const double start = char_start_at(nb.chars, (since ? *since : t_now) - nb.delay_s);
            out_.replace_from(start, nb.chars, t_now, "switch");
        }
        result_.selections.push_back(Selection{t_now, now, nb.current ? nb.current->t_s : kNaN});
    }
    if (K > 0) out_.append_new(branches_[static_cast<std::size_t>(selector_.current())].chars);
}

}  // namespace kz4ap::bank
