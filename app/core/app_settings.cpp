#include "app_settings.hpp"

#include <cmath>

namespace kz4ap::app {

namespace {

bool is_speed(double speed) { return speed == 1.0 || speed == 2.0 || speed == 5.0 || speed == 10.0 || std::isinf(speed); }
bool is_rate(int rate) { return rate == 96000 || rate == 192000 || rate == 384000 || rate == 768000; }

template <typename T>
void take(T& field, const std::optional<T>& given) {
    if (given) field = *given;
}

// A given choice keeps its saved value, unless the user changed it in the run.
template <typename T>
void keep_given(T& out, const T& saved, const T& start, const T& now, bool given) {
    out = (given && now == start) ? saved : now;
}

}  // namespace

UserChoices sanitized(UserChoices c) {
    const UserChoices d;
    if (c.known.kind == KnownKind::File && c.known.path.empty()) c.known = {};
    if (c.known.kind != KnownKind::File) c.known.path.clear();
    if (!(c.speed > 0) || !is_speed(c.speed)) c.speed = d.speed;
    if (!(c.center_khz >= 1.0 && c.center_khz <= 2000000.0)) c.center_khz = d.center_khz;  // the Center box's range, kHz
    if (!is_rate(c.sample_rate_hz)) c.sample_rate_hz = d.sample_rate_hz;
    if (c.if_gain_reduction_db < 20 || c.if_gain_reduction_db > 59) c.if_gain_reduction_db = d.if_gain_reduction_db;
    if (c.lna_state < 0 || c.lna_state > 9) c.lna_state = d.lna_state;
    if (!(c.floor_dbfs >= -160.0 && c.floor_dbfs <= 0.0)) c.floor_dbfs = d.floor_dbfs;  // the floor slider's range, dBFS
    if (!(c.range_db >= 10.0 && c.range_db <= 120.0)) c.range_db = d.range_db;          // the range slider's, dB
    return c;
}

UserChoices start_choices(const UserChoices& saved, const GivenChoices& given) {
    UserChoices c = saved;
    take(c.decoder_only, given.decoder_only);
    take(c.signed_only, given.signed_only);
    take(c.known, given.known);
    take(c.speed, given.speed);
    take(c.radio_serial, given.radio_serial);
    take(c.center_khz, given.center_khz);
    take(c.sample_rate_hz, given.sample_rate_hz);
    take(c.if_gain_reduction_db, given.if_gain_reduction_db);
    take(c.lna_state, given.lna_state);
    return c;
}

UserChoices choices_to_save(const UserChoices& saved, const UserChoices& start, const UserChoices& now, const GivenChoices& given) {
    UserChoices out = now;
    keep_given(out.decoder_only, saved.decoder_only, start.decoder_only, now.decoder_only, given.decoder_only.has_value());
    keep_given(out.signed_only, saved.signed_only, start.signed_only, now.signed_only, given.signed_only.has_value());
    keep_given(out.known, saved.known, start.known, now.known, given.known.has_value());
    keep_given(out.speed, saved.speed, start.speed, now.speed, given.speed.has_value());
    keep_given(out.radio_serial, saved.radio_serial, start.radio_serial, now.radio_serial, given.radio_serial.has_value());
    keep_given(out.center_khz, saved.center_khz, start.center_khz, now.center_khz, given.center_khz.has_value());
    keep_given(out.sample_rate_hz, saved.sample_rate_hz, start.sample_rate_hz, now.sample_rate_hz, given.sample_rate_hz.has_value());
    keep_given(out.if_gain_reduction_db, saved.if_gain_reduction_db, start.if_gain_reduction_db, now.if_gain_reduction_db,
               given.if_gain_reduction_db.has_value());
    keep_given(out.lna_state, saved.lna_state, start.lna_state, now.lna_state, given.lna_state.has_value());
    return out;
}

}  // namespace kz4ap::app
