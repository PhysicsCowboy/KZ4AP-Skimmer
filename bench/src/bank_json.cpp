#include "bank_json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <vector>

namespace kz4ap::bench {

namespace {

// Every ProtoConfig field, in its declaration order (training/kz4ap_proto/params.py), with Plan B's B4a fields in
// dits in place of the three in seconds they replaced (rekey_after_dits for rekey_after_s, periodicity_windows_dits
// for periodicity_windows_s, rekey_timeout_ratio for rekey_timeout_s), then Plan B's BankConfig fields, which the
// prototype does not have (noise_stuck_s: B3; the overrides in seconds rekey_after_s and rekey_timeout_s: B4a, for
// ablations, unset by default; periodicity_windows_s: the prototype's field, here since B4a, the default windows
// again since B4d; periodicity_window_mode: B4a-C, "seconds" by default since B4d; periodicity_unselected_windows_s:
// B4a-C, the shared-window variant; rekey_clear_moves_stretch, rekey_timeout_from_first_mark and rekey_lead_dits:
// B4d, the re-key clocks).
#define KZ4AP_BANK_CONFIG_FIELDS(X)                                                                           \
    X(min_wpm) X(max_wpm) X(ladder_step) X(length_dits) X(block_s) X(noise_method) X(mask_bias) X(noise_tau_s) \
    X(noise_warmup_s) X(noise_guard) X(neighbor_guard) X(segment_s) X(spectrum_smoothing_hz) X(guard_margin_s) \
    X(min_clean_fraction) X(amplitude_tau_s) X(prior_key_down) X(hysteresis_nats) X(squelch_a)                 \
    X(squelch_ref_s) X(squelch_exponent) X(false_marks_per_s) X(x_on_values) X(release_probability)           \
    X(rekey_after_dits) X(seed_memory_rekeys) X(fit_memory) X(t_grid_step) X(q_grid) X(w_grid) X(tg_grid)      \
    X(sigma_ln_mark) X(sigma_ln_space) X(outlier_prior) X(outlier_range_s) X(prior_sigma_ln)                  \
    X(refine_iterations) X(min_fit_weight) X(periodicity_method) X(periodicity_windows_dits)                   \
    X(periodicity_update_s) X(periodicity_rate_hz) X(comb_teeth) X(comb_width) X(spectrum_nulls)               \
    X(spectrum_null_width) X(spectrum_front_floor_db) X(comb_confidence_min) X(edge_confidence_min)            \
    X(spectrum_confidence_min) X(eligibility_tolerance) X(switch_persistence) X(quality_tie_nats)              \
    X(text_tie_nats) X(text_window_chars) X(text_separation_nats) X(new_over_min_s) X(new_over_gaps)           \
    X(rekey_timeout_ratio) X(fresh_fit_min_obs) X(correction_reach_s) X(noise_stuck_s) X(rekey_after_s)        \
    X(rekey_timeout_s) X(periodicity_windows_s) X(periodicity_window_mode) X(periodicity_unselected_windows_s)        X(rekey_clear_moves_stretch) X(rekey_timeout_from_first_mark) X(rekey_lead_dits)

ordered_json field_json(bool v) { return ordered_json(v); }
ordered_json field_json(double v) { return ordered_json(v); }
ordered_json field_json(int v) { return ordered_json(v); }
ordered_json field_json(const std::string& v) { return ordered_json(v); }
ordered_json field_json(const std::vector<double>& v) {
    ordered_json a = ordered_json::array();
    for (double x : v) a.push_back(x);
    return a;
}

void assign(const std::string& name, bool& dst, const ordered_json& v) {
    if (!v.is_boolean()) throw std::invalid_argument(name + ": true or false is needed");
    dst = v.get<bool>();
}
void assign(const std::string& name, double& dst, const ordered_json& v) {
    if (!v.is_number()) throw std::invalid_argument(name + ": a number is needed");
    dst = v.get<double>();
}
void assign(const std::string& name, int& dst, const ordered_json& v) {
    if (v.is_number_integer()) {
        dst = v.get<int>();
    } else if (v.is_number_float() && std::floor(v.get<double>()) == v.get<double>()) {
        dst = static_cast<int>(v.get<double>());
    } else {
        throw std::invalid_argument(name + ": an integer is needed");
    }
}
void assign(const std::string& name, std::string& dst, const ordered_json& v) {
    if (!v.is_string()) throw std::invalid_argument(name + ": a string is needed");
    dst = v.get<std::string>();
}
void assign(const std::string& name, std::vector<double>& dst, const ordered_json& v) {
    if (!v.is_array()) throw std::invalid_argument(name + ": a list of numbers is needed");
    std::vector<double> out;
    for (const auto& x : v) {
        if (!x.is_number()) throw std::invalid_argument(name + ": a list of numbers is needed");
        out.push_back(x.get<double>());
    }
    dst = std::move(out);
}

ordered_json none_or_round6(double x) { return std::isnan(x) ? ordered_json(nullptr) : ordered_json(py_round(x, 6)); }

void append_escaped(std::string& out, const std::string& s) {
    static const char* hex = "0123456789abcdef";
    auto u_escape = [&](unsigned cp) {
        out += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) out += hex[(cp >> shift) & 0xF];
    };
    out += '"';
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20 || c == 0x7F)  // json escapes everything outside ' ' ... '~'
                        u_escape(c);
                    else
                        out += static_cast<char>(c);
            }
            ++i;
            continue;
        }
        // UTF-8 to a code point, then \uXXXX (a surrogate pair above U+FFFF), as ensure_ascii does
        int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        unsigned cp = c & (len == 4 ? 0x07u : len == 3 ? 0x0Fu : 0x1Fu);
        for (int k = 1; k < len && i + static_cast<std::size_t>(k) < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3Fu);
        i += static_cast<std::size_t>(len);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            u_escape(0xD800 + (cp >> 10));
            u_escape(0xDC00 + (cp & 0x3FF));
        } else {
            u_escape(cp);
        }
    }
    out += '"';
}

void dump(std::string& out, const ordered_json& v) {
    switch (v.type()) {
        case ordered_json::value_t::null: out += "null"; break;
        case ordered_json::value_t::boolean: out += v.get<bool>() ? "true" : "false"; break;
        case ordered_json::value_t::number_integer: out += std::to_string(v.get<std::int64_t>()); break;
        case ordered_json::value_t::number_unsigned: out += std::to_string(v.get<std::uint64_t>()); break;
        case ordered_json::value_t::number_float: {
            const double x = v.get<double>();
            if (std::isnan(x))
                out += "NaN";
            else if (std::isinf(x))
                out += x > 0 ? "Infinity" : "-Infinity";
            else
                out += py_float_repr(x);
            break;
        }
        case ordered_json::value_t::string: append_escaped(out, v.get_ref<const std::string&>()); break;
        case ordered_json::value_t::array: {
            out += '[';
            bool first = true;
            for (const auto& x : v) {
                if (!first) out += ", ";
                first = false;
                dump(out, x);
            }
            out += ']';
            break;
        }
        case ordered_json::value_t::object: {
            out += '{';
            bool first = true;
            for (const auto& [key, x] : v.items()) {
                if (!first) out += ", ";
                first = false;
                append_escaped(out, key);
                out += ": ";
                dump(out, x);
            }
            out += '}';
            break;
        }
        default: throw std::invalid_argument("py_dumps: a value JSON cannot hold");
    }
}

}  // namespace

double py_round(double x, int ndigits) {
    if (!std::isfinite(x)) return x;
    char buf[512];
    const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, ndigits);
    if (r.ec != std::errc()) throw std::runtime_error("py_round: to_chars failed");
    double out = 0.0;
    const auto p = std::from_chars(buf, r.ptr, out);
    if (p.ec != std::errc()) throw std::runtime_error("py_round: from_chars failed");
    return out;
}

std::string py_float_repr(double x) {
    if (std::isnan(x)) return "nan";
    if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);  // shortest digits
    const std::string s(buf, r.ptr);
    // s = [-]d[.ddd]e(+|-)XX
    std::string sign, digits;
    std::size_t i = 0;
    if (s[0] == '-') {
        sign = "-";
        i = 1;
    }
    const std::size_t e = s.find('e');
    for (std::size_t j = i; j < e; ++j)
        if (s[j] != '.') digits += s[j];
    const int exponent = std::stoi(s.substr(e + 1));
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    const int decpt = exponent + 1;  // x = 0.d1d2... x 10^decpt
    const int n = static_cast<int>(digits.size());
    std::string out = sign;
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) {
            out += "0." + std::string(static_cast<std::size_t>(-decpt), '0') + digits;
        } else if (decpt >= n) {
            out += digits + std::string(static_cast<std::size_t>(decpt - n), '0') + ".0";
        } else {
            out += digits.substr(0, static_cast<std::size_t>(decpt)) + "." +
                   digits.substr(static_cast<std::size_t>(decpt));
        }
    } else {
        out += digits.substr(0, 1);
        if (n > 1) out += "." + digits.substr(1);
        const int ex = decpt - 1;
        out += ex < 0 ? "e-" : "e+";
        const std::string mag = std::to_string(ex < 0 ? -ex : ex);
        out += (mag.size() < 2 ? "0" : "") + mag;
    }
    return out;
}

std::string py_dumps(const ordered_json& value) {
    std::string out;
    dump(out, value);
    return out;
}

ordered_json to_json(const bank::ChannelResult& r) {
    ordered_json chars = ordered_json::array();
    for (const auto& c : r.chars)
        chars.push_back(ordered_json::array({c.text, py_round(c.start_s, 4), py_round(c.end_s, 4)}));
    ordered_json corrections = ordered_json::array();
    for (const auto& c : r.corrections) {
        ordered_json o = ordered_json::object();
        o["t_s"] = c.t_s;
        o["from_s"] = c.from_s;
        o["reach_s"] = c.reach_s;
        o["old"] = c.old_text;
        o["new"] = c.new_text;
        o["reason"] = c.reason;
        corrections.push_back(std::move(o));
    }
    ordered_json selections = ordered_json::array();
    for (const auto& s : r.selections)
        selections.push_back(ordered_json::array({py_round(s.t_s, 4), s.branch, none_or_round6(s.t_dit_s)}));
    ordered_json periodicity = ordered_json::array();
    for (const auto& p : r.periodicity) {
        ordered_json per = ordered_json::array();
        for (const auto& [t, score] : p.per_window)
            per.push_back(
                ordered_json::array({t ? ordered_json(py_round(*t, 6)) : ordered_json(nullptr), py_round(score, 4)}));
        periodicity.push_back(ordered_json::array(
            {py_round(p.t_s, 4), none_or_round6(p.t_p_s), py_round(p.confidence, 4), none_or_round6(p.window_s), per}));
    }
    ordered_json over_starts = ordered_json::array();
    for (double t : r.over_starts) over_starts.push_back(py_round(t, 4));
    ordered_json out = ordered_json::object();
    out["text"] = r.text;
    out["chars"] = std::move(chars);
    out["corrections"] = std::move(corrections);
    out["selections"] = std::move(selections);
    out["periodicity"] = std::move(periodicity);
    out["over_starts"] = std::move(over_starts);
    out["switches"] = r.switches;
    return out;
}

ordered_json to_json(const bank::BankConfig& cfg) {
    ordered_json out = ordered_json::object();
#define KZ4AP_FIELD_TO_JSON(name) out[#name] = field_json(cfg.name);
    KZ4AP_BANK_CONFIG_FIELDS(KZ4AP_FIELD_TO_JSON)
#undef KZ4AP_FIELD_TO_JSON
    return out;
}

void set_config_value(bank::BankConfig& cfg, const std::string& name, const ordered_json& value) {
#define KZ4AP_FIELD_SET(field)             \
    if (name == #field) {                  \
        assign(name, cfg.field, value);    \
        return;                            \
    }
    KZ4AP_BANK_CONFIG_FIELDS(KZ4AP_FIELD_SET)
#undef KZ4AP_FIELD_SET
    throw std::invalid_argument("unknown parameter '" + name + "'");
}

bank::BankConfig bank_config_from_json(const ordered_json& j) {
    if (!j.is_object()) throw std::invalid_argument("a configuration must be a JSON object");
    bank::BankConfig cfg;
    for (const auto& [key, value] : j.items()) set_config_value(cfg, key, value);
    return cfg;
}

}  // namespace kz4ap::bench
