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

// The system clock now, ms since 1970-01-01 00:00 UTC (as Windows keeps it: no leap seconds).
std::int64_t system_unix_ms();

// A live source's first sample, ms since 1970 UTC, from the system clock when its first block of samples arrived: that block's
// samples ran for block_samples / sample_rate before it, so its first sample was then. Not counted: the radio's and its driver's
// buffering between the antenna and the block's arrival, which delays the arrival and makes this time late by that much (a few
// ms to a few tens of ms for a USB radio: a heuristic estimate, not measured), and the system clock's own error.
std::int64_t first_sample_unix_ms(std::int64_t block_arrival_unix_ms, std::uint64_t block_samples, int sample_rate);

}  // namespace kz4ap
