// The bank decoder's channel: a port of training/kz4ap_proto/channel.py (docs/signal-processing.md section 8c,
// "Channel decoder"). One station's stream through the 32 branches, each with its own amplitude, keying,
// timing, duration fit and text; the periodicity estimate; branch selection; new overs with re-keying; and the
// channel's published text with corrections. BankChannel is the prototype's ChannelDecoder.run made streaming:
// push() takes any number of samples and processes every complete block, finish() the last partial block, so
// the result does not depend on how the stream is split. Times are in seconds (stream time; a branch's times
// have its group delay removed) and become samples only at the point of use; sample indices are 64-bit.
#pragma once

#include "kz4ap/bank/bank_config.hpp"
#include "kz4ap/bank/filters.hpp"
#include "kz4ap/bank/fit.hpp"
#include "kz4ap/bank/keying.hpp"
#include "kz4ap/bank/noise.hpp"
#include "kz4ap/bank/periodicity.hpp"
#include "kz4ap/bank/selection.hpp"
#include "kz4ap/bank/timing.hpp"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace kz4ap {
struct BankDecoderTestAccess;  // test-only seam (engine/tests/bank_decoder_test.cpp); no code in the engine
}  // namespace kz4ap

namespace kz4ap::bank {

// One published character.
struct Char {
    std::string text;      // a symbol, "*" for a pattern with no code, or " " for a word space
    double start_s = 0.0;  // stream time, the branch's group delay removed, s
    double end_s = 0.0;    // s
};

// A correction: everything published from from_s on was replaced.
struct Correction {
    double t_s = 0.0;      // when it was issued, s
    double from_s = 0.0;   // the replaced text started here, s
    double reach_s = 0.0;  // t_s - from_s, s (at most the correction reach)
    std::string old_text;  // the replaced characters' text
    std::string new_text;  // the text that replaced it
    std::string reason;    // "switch" (branch selection), "rekey" (an over's first marks re-keyed) or "timeout"
                           // (re-keyed, or deleted, because the over's amplitude stayed unknown for the branch's
                           // re-key time-out)
    // The number of published characters kept before the replacement (the prototype's len(kept)): the
    // characters from this index on were replaced by new_text's characters. Not in the prototype's JSON.
    std::size_t from_index = 0;
    // The first position of the published list whose character's text this correction changed (the list before
    // against the list after). The characters before it are untouched; from_index can be larger, when the kept
    // characters are not a prefix of the list (characters that overlap in time). Not in the prototype; for the
    // engine's TextCorrection (docs/signal-processing.md appendix A.8c, "The consumer's rule").
    std::size_t first_changed_index = 0;
};

// The channel's text (spec 4.8): the selected branch's characters appended as they are decided, and
// corrections that replace everything from a time on, reaching back at most reach_s.
class Output {
public:
    explicit Output(double reach_s) : reach_s_(reach_s) {}

    // Appends the characters (in time order) that start after the last one published.
    void append_new(const std::vector<Char>& chars);
    // Replaces the published text from from_s (s) on by chars, at time t_s (s); nothing that starts more than
    // reach_s before t_s changes. The cut is made on overlap: a published character that has not ended by the cut
    // (cut = max(from_s, t_s - reach_s)) is replaced, one that starts before t_s - reach_s is kept; a new character
    // is taken only if it starts at or after both the cut and the end of the last kept character. A correction is
    // recorded only if the replaced text differs from the new.
    void replace_from(double from_s, const std::vector<Char>& chars, double t_s, const std::string& reason);

    std::string text() const;
    const std::vector<Char>& chars() const { return chars_; }
    const std::vector<Correction>& corrections() const { return corrections_; }
    double reach_s() const { return reach_s_; }
    // The number of characters append_new has published in all (corrections do not count). Observation only, for
    // the engine's BankDecoder (its immediate text); not in the prototype.
    std::size_t appended() const { return appended_; }
    // For every replace_from that changed the list's text, recorded as a correction or not (a same-text
    // replacement that reorders characters overlapping in time changes it without one): the first position whose
    // character's text changed. Observation only, for the engine's BankDecoder; not in the prototype.
    const std::vector<std::size_t>& text_changes() const { return text_changes_; }

private:
    double reach_s_;
    std::size_t appended_ = 0;
    std::vector<std::size_t> text_changes_;
    std::vector<Char> chars_;
    std::vector<Correction> corrections_;
};

// The T_P prior as the fit takes it: (T_P s or none, weight); weight 1 once T_P is confident, else 0.
struct Prior {
    std::optional<double> t_s;
    double weight = 0.0;
};

// The fresh fit's fitted parameters (T, w, q, T_g): k in the 1/2 k ln n penalty (Branch::fresh_wins).
inline constexpr int kFitParameters = 4;

// One branch's marks and spaces, duration fit and text (spec 4.3, 4.5, 4.7): the prototype's Branch. Its state
// is public, as the channel reads it (and the prototype exposes it).
class Branch {
public:
    // A mark or space of a re-keyed stretch: (is_mark, d s, var s^2, start s, end s).
    struct TimedObs {
        bool is_mark;
        double d_s;
        double var_t_s2;
        double start_s;
        double end_s;
    };
    // What rekey_over returns.
    struct RekeyResult {
        double amp2;    // the winning s^2, FS^2
        bool key_now;   // key state at the stretch's end
        double from_s;  // the stretch's start, s
        int marks;      // marks keyed
    };

    // n: the boxcar's samples; length_s = n / rate_hz, s. cfg and text_model must outlive the branch.
    Branch(int index, double length_s, int n, double rate_hz, const BankConfig& cfg, const TextModel& text_model);

    // Stream time of sample index n on this branch's time base (group delay removed), s.
    double time(std::int64_t n) const { return static_cast<double>(n) / rate_ - delay_s; }
    // Key changes (sample index, key down after it) with the branch's amplitude over noise a (dimensionless).
    // provisional: keyed by the unknown-amplitude test, so its durations enter no fit.
    void on_edges(const std::vector<std::pair<std::int64_t, bool>>& changes, double a, const Prior& prior,
                  bool provisional);
    void flush() { finish_char(); }
    // T_new = max(new_over_min_s, new_over_gaps x T_g), s; T_g of the slowest standard speed without a fit.
    double silence_limit_s() const;
    // The key has been up for longer than T_new since the last key-up.
    bool new_over_due(std::int64_t n_now, bool key_down) const;
    // A possible new over (spec 4.7): a fresh fit, the previous over's kept as the fallback.
    void start_over(std::int64_t n_now, const Prior& prior, bool was_unknown);
    // Re-keys the stretch P_stretch (|v|^2 FS^2) from sample n0 with the full LLR at each candidate s^2 (FS^2).
    RekeyResult rekey_over(std::span<const double> P_stretch, std::int64_t n0, double sigma2,
                           const std::vector<double>& amp_candidates, double a_min, const Prior& prior);
    // The re-key time-out with no amplitude to key with: the stretch's characters from sample n0 on are
    // deleted. Returns the stretch's start time, s.
    double clear_over(std::int64_t n0);
    // Mean log-probability of the branch's last `window` characters (word spaces left out), nats per
    // character; none without characters.
    std::optional<double> text_logprob(int window) const;

    int index;
    double length_s;
    double delay_s;  // a boxcar's group delay (linear phase), (n - 1) / (2 r), s
    DurationFit fit;                         // the fit that decodes (its best is `current`)
    std::optional<DurationFit> prev_fit;     // the previous over's, until this over's start is re-keyed
    std::optional<DurationFit> rival;        // after a re-key the previous fit won: this over's fresh fit
    std::vector<Obs> over_obs;               // this over's re-keyed and later marks and spaces
    std::optional<Fit> current;              // the Fit that decodes
    std::vector<Char> chars;
    std::string elements;
    double char_start = 0.0;
    double char_end = 0.0;
    bool word_open = false;
    std::optional<double> down_at;
    std::optional<double> up_at;             // this over's last key-up (none: none yet)
    std::int64_t over_start_n = 0;           // sample index of the latest over start
    std::int64_t unknown_since_n = 0;        // sample index where the amplitude became unknown
    std::int64_t timeout_from_n = 0;         // the re-key time-out counts from this sample index
    bool over_pending = false;               // an over started by a silence, not yet confirmed by a re-key
    int marks_in_over = 0;

private:
    void observe(bool is_mark, double d, double var_t, const Prior& prior, bool provisional);
    bool fresh_wins(const std::optional<Fit>& fresh, const std::optional<Fit>& old, const std::vector<Obs>& obs) const;
    void add_element(double start, double end, double var_t, const std::optional<Fit>& fit);
    void end_space(double d, double var_t, const std::optional<Fit>& fit);
    void finish_char();
    void word_space();
    void redecode(double from_s, const std::vector<TimedObs>& obs, const std::optional<Fit>& fit);

    double rate_;
    const BankConfig* cfg_;
    const TextModel* text_model_;
};

// One selection: (t s, branch index, its fit's T s or NaN).
struct Selection {
    double t_s;
    int branch;
    double t_dit_s;
};

// One recomputation of the periodicity estimate: (t s, T_P s or NaN, confidence, window s or NaN, per window
// (T s or none, score)); confidence and scores dimensionless (the comb).
struct PeriodicityRecord {
    double t_s;
    double t_p_s;
    double confidence;
    double window_s;
    std::vector<std::pair<std::optional<double>, double>> per_window;
};

// The prototype's ChannelResult (without p1, the offline experiments' posterior).
struct ChannelResult {
    std::string text;
    std::vector<Char> chars;
    std::vector<Correction> corrections;
    std::vector<Selection> selections;
    std::vector<PeriodicityRecord> periodicity;
    // s, the selected branch's time base: where an over started, counted once its re-key keyed a mark
    std::vector<double> over_starts;
    int switches = 0;
};

// One channel through the bank, streaming (the prototype's ChannelDecoder.run).
class BankChannel {
public:
    // The configuration's timing (bank_timing(cfg): time constants in dits).
    BankChannel(const BankConfig& cfg, double rate_hz);
    // An explicit timing (e.g. fixed_timing, stage 1's constants in seconds, for the golden tests). Throws
    // std::invalid_argument if a per-branch list does not hold one value per branch.
    BankChannel(const BankConfig& cfg, double rate_hz, const BankTiming& timing);
    ~BankChannel();
    BankChannel(const BankChannel&) = delete;
    BankChannel& operator=(const BankChannel&) = delete;

    // Appends samples of the baseband stream u (station at 0 Hz, FS) and processes every complete block of
    // round(block_s x rate) samples; the remainder waits. Throws std::logic_error after finish().
    void push(std::span<const std::complex<double>> u);
    // Processes the remainder as the prototype's last (partial) block, then ends every branch's character and
    // publishes it. Further calls do nothing.
    void finish();
    // The result so far (after finish(): the prototype's run result).
    ChannelResult result() const;
    // The published text and its corrections so far.
    const Output& output() const { return out_; }

    // Samples pushed, and processed in blocks.
    std::int64_t samples() const { return total_; }
    std::int64_t processed() const { return processed_; }
    // The branches' samples N_k and realized lengths N_k / r, s.
    const std::vector<int>& branch_samples_n() const { return n_; }
    const std::vector<double>& lengths_s() const { return lengths_; }
    int block_samples() const { return block_; }
    // The periodicity estimator and the number of its recomputations recorded so far (for tests that look
    // inside a recomputation).
    const Periodicity& periodicity() const { return periodicity_; }
    // The keyer (each branch's amplitude state and W_min,k) and each branch's re-key time-out, samples.
    const BankKeyer& keyer() const { return keyer_; }
    const std::vector<std::int64_t>& rekey_timeout_samples() const { return timeout_; }
    std::size_t periodicity_records() const { return result_.periodicity.size(); }
    // The shared-window variant (B4a-C): the T-hat passed to the periodicity estimate at the last block, s (the
    // selected branch's fitted T, only when that fit is eligible and its over's start has been re-keyed); none
    // otherwise, before any selection and in the default mode.
    const std::optional<double>& periodicity_dit_s() const { return periodicity_dit_; }
    // The T-hat the shared mode's next block would use, from the channel's state now (the rule above). It evaluates
    // the rule in either mode when called; only the shared mode's blocks use it. For tests.
    std::optional<double> shared_window_dit() const;
    // The branches and the selected branch (read-only, for tests).
    const std::vector<Branch>& branches() const { return branches_; }
    int selected() const { return selector_.current(); }
    // The |v_k|^2 window's storage: values allocated (branches x columns) and their size, bytes.
    std::size_t p_window_values() const { return p_win_.v.size(); }
    std::size_t p_window_bytes() const { return p_win_.v.size() * sizeof(p_win_.v[0]); }
    // The noise estimate's diagnostics (Plan B, B3): how often its stuck-level recovery fired (each firing
    // resets every branch), and the blocks of exact zeros it skipped.
    int noise_recoveries() const { return noise_->recoveries(); }
    std::int64_t noise_zero_blocks() const { return noise_->zero_blocks(); }

private:
    // The resync test changes the published list directly, as no recorded stream makes the bank do it.
    friend struct ::kz4ap::BankDecoderTestAccess;

    void append_sample(std::complex<double> x);
    void process_block(std::int64_t n0, std::int64_t n1);
    void compact();
    // Row k of the |v_k|^2 window from absolute sample `from` to `to` (exclusive), converted to double
    // (exact), FS^2.
    std::vector<double> p_row(int k, std::int64_t from, std::int64_t to) const;

    BankConfig cfg_;
    double rate_;
    std::vector<int> n_;
    std::vector<double> lengths_;
    TextModel text_model_;
    std::unique_ptr<NoiseEstimator> noise_;
    BankKeyer keyer_;
    Periodicity periodicity_;
    Selector selector_;
    std::vector<Branch> branches_;
    Output out_;
    ChannelResult result_;
    int block_;
    std::int64_t reach_;
    std::vector<std::int64_t> timeout_;  // per branch: the re-key time-out, samples
    bool shared_windows_ = false;        // the shared-window variant (B4a-C): the windows follow T-hat
    std::optional<double> periodicity_dit_;
    Prior prior_;

    // Streaming state. The cumulative sum c[j] = u[0] + ... + u[j-1] (complex, FS) in a ring of the last
    // max N_k + 1 values; the window of u and of P (|v_k|^2 rounded to float32, FS^2, stored as float: lossless)
    // from absolute sample base_ to total_, kept back far enough for the re-key (correction_reach_s) and the noise
    // estimates.
    std::vector<std::complex<double>> csum_ring_;
    std::complex<double> csum_{0.0, 0.0};
    std::vector<std::complex<double>> u_win_;
    PowerMatrix p_win_;
    std::int64_t base_ = 0;
    std::int64_t keep_back_ = 0;
    std::int64_t total_ = 0;
    std::int64_t processed_ = 0;
    bool finished_ = false;
};

}  // namespace kz4ap::bank
