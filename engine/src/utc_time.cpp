#include "kz4ap/utc_time.hpp"

#include <cstdio>

namespace kz4ap {

std::int64_t unix_ms_from_civil(int year, int month, int day, int hour, int minute, int second, int millisecond) {
    // Days from 1970-01-01 of a proleptic Gregorian date (H. Hinnant's days_from_civil).
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + doe - 719468;
    return ((days * 24 + hour) * 60 + minute) * 60000 + static_cast<std::int64_t>(second) * 1000 + millisecond;
}

CivilTime civil_from_unix_ms(std::int64_t unix_ms) {
    // H. Hinnant's civil_from_days, the day floored for times before 1970.
    std::int64_t days = unix_ms >= 0 ? unix_ms / 86400000 : -((-unix_ms + 86399999) / 86400000);
    const std::int64_t ms_of_day = unix_ms - days * 86400000;
    CivilTime c;
    c.day_of_week = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday, 4
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    c.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2 ? 1 : 0));
    c.hour = static_cast<int>(ms_of_day / 3600000);
    c.minute = static_cast<int>(ms_of_day / 60000 % 60);
    c.second = static_cast<int>(ms_of_day / 1000 % 60);
    c.millisecond = static_cast<int>(ms_of_day % 1000);
    return c;
}

std::string utc_text(std::int64_t unix_ms) {
    const CivilTime c = civil_from_unix_ms(unix_ms);
    char text[48];
    std::snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", c.year, c.month, c.day, c.hour, c.minute, c.second,
                  c.millisecond);
    return text;
}

}  // namespace kz4ap
