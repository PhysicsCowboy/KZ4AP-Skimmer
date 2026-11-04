// UTC times as ms since 1970-01-01 00:00 UTC, and their text, without the C library's time zone functions.
#pragma once

#include <cstdint>
#include <string>

namespace kz4ap {

// ms since 1970-01-01 00:00 UTC of a civil date and time, proleptic Gregorian; no range check.
std::int64_t unix_ms_from_civil(int year, int month, int day, int hour, int minute, int second, int millisecond);

struct CivilTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, millisecond = 0;
    int day_of_week = 4;  // 0 Sunday to 6 Saturday, as Windows's SYSTEMTIME counts
};

// The inverse of unix_ms_from_civil.
CivilTime civil_from_unix_ms(std::int64_t unix_ms);

// ISO 8601 with ms, for example "2017-08-21T18:00:00.093Z".
std::string utc_text(std::int64_t unix_ms);

}  // namespace kz4ap
