// UTC times as ms since 1970-01-01 00:00 UTC, and their text, without the C library's time zone functions.
#pragma once

#include <cstdint>
#include <string>

namespace kz4ap {

// ms since 1970-01-01 00:00 UTC of a civil date and time, proleptic Gregorian; no range check.
std::int64_t unix_ms_from_civil(int year, int month, int day, int hour, int minute, int second, int millisecond);

// ISO 8601 with ms, for example "2017-08-21T18:00:00.093Z".
std::string utc_text(std::int64_t unix_ms);

}  // namespace kz4ap
