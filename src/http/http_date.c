#include "http/http_date.h"

#include <stdint.h>
#include <string.h>

static const char *const cerv_wday_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const cerv_wday_long[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const cerv_months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static bool cerv_digits(const unsigned char *p, size_t n, unsigned *out)
{
    unsigned v = 0U;
    size_t i = 0U;
    if (p == NULL || out == NULL || n == 0U) return false;
    for (i = 0U; i < n; ++i) {
        if (p[i] < (unsigned char)'0' || p[i] > (unsigned char)'9') return false;
        v = v * 10U + (unsigned)(p[i] - (unsigned char)'0');
    }
    *out = v;
    return true;
}

static int cerv_month_index(const unsigned char *p)
{
    int i = 0;
    for (i = 0; i < 12; ++i) {
        if (memcmp(p, cerv_months[i], 3U) == 0) return i + 1;
    }
    return 0;
}

static bool cerv_leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static unsigned cerv_days_in_month(int year, unsigned month)
{
    static const unsigned days[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    if (month == 0U || month > 12U) return 0U;
    if (month == 2U && cerv_leap(year)) return 29U;
    return days[month - 1U];
}

/* Days since 1970-01-01. Valid far beyond Cerv's 4-digit HTTP year domain. */
static int64_t cerv_days_from_civil(int year, unsigned month, unsigned day)
{
    int adjusted = year - (month <= 2U ? 1 : 0);
    int era = (adjusted >= 0 ? adjusted : adjusted - 399) / 400;
    unsigned yoe = (unsigned)(adjusted - era * 400);
    unsigned mp = month > 2U ? month - 3U : month + 9U;
    unsigned doy = (153U * mp + 2U) / 5U + day - 1U;
    unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * INT64_C(146097) + (int64_t)doe - INT64_C(719468);
}

static int cerv_weekday(int year, unsigned month, unsigned day)
{
    int64_t days = cerv_days_from_civil(year, month, day);
    int64_t w = (days + INT64_C(4)) % INT64_C(7);
    if (w < 0) w += INT64_C(7);
    return (int)w;
}

static bool cerv_finish_date(int year, unsigned month, unsigned day, unsigned hour,
                             unsigned minute, unsigned second, int supplied_wday,
                             struct cerv_http_date *out)
{
    int64_t days = 0;
    int64_t seconds = 0;
    if (out == NULL || year < 0 || year > 9999 || month == 0U || month > 12U ||
        day == 0U || day > cerv_days_in_month(year, month) || hour > 23U ||
        minute > 59U || second > 60U) {
        return false;
    }
    if (supplied_wday < 0 || supplied_wday > 6 || supplied_wday != cerv_weekday(year, month, day)) {
        return false;
    }
    days = cerv_days_from_civil(year, month, day);
    seconds = days * INT64_C(86400) + (int64_t)hour * INT64_C(3600) +
              (int64_t)minute * INT64_C(60) + (int64_t)second;
    *out = (struct cerv_http_date){
        .unix_seconds = seconds,
        .year = year,
        .month = month,
        .day = day,
        .hour = hour,
        .minute = minute,
        .second = second
    };
    return true;
}

static int cerv_short_wday(const unsigned char *p)
{
    int i = 0;
    for (i = 0; i < 7; ++i) {
        if (memcmp(p, cerv_wday_short[i], 3U) == 0) return i;
    }
    return -1;
}

static int cerv_long_wday(struct cerv_span s)
{
    int i = 0;
    for (i = 0; i < 7; ++i) {
        size_t n = strlen(cerv_wday_long[i]);
        if (s.len == n && memcmp(s.ptr, cerv_wday_long[i], n) == 0) return i;
    }
    return -1;
}

static bool cerv_parse_imf(struct cerv_span in, struct cerv_http_date *out)
{
    unsigned day = 0U, year = 0U, hour = 0U, minute = 0U, second = 0U;
    int month = 0, wday = 0;
    if (in.len != 29U) return false;
    if (in.ptr[3] != (unsigned char)',' || in.ptr[4] != (unsigned char)' ' ||
        in.ptr[7] != (unsigned char)' ' || in.ptr[11] != (unsigned char)' ' ||
        in.ptr[16] != (unsigned char)' ' || in.ptr[19] != (unsigned char)':' ||
        in.ptr[22] != (unsigned char)':' || in.ptr[25] != (unsigned char)' ' ||
        memcmp(in.ptr + 26U, "GMT", 3U) != 0) return false;
    wday = cerv_short_wday(in.ptr);
    month = cerv_month_index(in.ptr + 8U);
    if (wday < 0 || month == 0 || !cerv_digits(in.ptr + 5U, 2U, &day) ||
        !cerv_digits(in.ptr + 12U, 4U, &year) || !cerv_digits(in.ptr + 17U, 2U, &hour) ||
        !cerv_digits(in.ptr + 20U, 2U, &minute) || !cerv_digits(in.ptr + 23U, 2U, &second)) return false;
    return cerv_finish_date((int)year, (unsigned)month, day, hour, minute, second, wday, out);
}

static bool cerv_parse_rfc850(struct cerv_span in, int current_year, struct cerv_http_date *out)
{
    size_t comma = 0U;
    size_t pos = 0U;
    unsigned day = 0U, yy = 0U, hour = 0U, minute = 0U, second = 0U;
    int month = 0, wday = 0, year = 0, century = 0;
    while (comma < in.len && in.ptr[comma] != (unsigned char)',') ++comma;
    if (comma == 0U || comma + 24U != in.len || in.ptr[comma + 1U] != (unsigned char)' ') return false;
    wday = cerv_long_wday((struct cerv_span){.ptr = in.ptr, .len = comma});
    pos = comma + 2U;
    if (wday < 0 || pos + 22U != in.len || in.ptr[pos + 2U] != (unsigned char)'-' ||
        in.ptr[pos + 6U] != (unsigned char)'-' || in.ptr[pos + 9U] != (unsigned char)' ' ||
        in.ptr[pos + 12U] != (unsigned char)':' || in.ptr[pos + 15U] != (unsigned char)':' ||
        in.ptr[pos + 18U] != (unsigned char)' ' || memcmp(in.ptr + pos + 19U, "GMT", 3U) != 0) return false;
    month = cerv_month_index(in.ptr + pos + 3U);
    if (month == 0 || !cerv_digits(in.ptr + pos, 2U, &day) || !cerv_digits(in.ptr + pos + 7U, 2U, &yy) ||
        !cerv_digits(in.ptr + pos + 10U, 2U, &hour) || !cerv_digits(in.ptr + pos + 13U, 2U, &minute) ||
        !cerv_digits(in.ptr + pos + 16U, 2U, &second)) return false;
    century = current_year - current_year % 100;
    year = century + (int)yy;
    if (year > current_year + 50) year -= 100;
    return cerv_finish_date(year, (unsigned)month, day, hour, minute, second, wday, out);
}

static bool cerv_parse_asctime(struct cerv_span in, struct cerv_http_date *out)
{
    unsigned day = 0U, year = 0U, hour = 0U, minute = 0U, second = 0U;
    int month = 0, wday = 0;
    if (in.len != 24U || in.ptr[3] != (unsigned char)' ' || in.ptr[7] != (unsigned char)' ' ||
        in.ptr[10] != (unsigned char)' ' || in.ptr[13] != (unsigned char)':' ||
        in.ptr[16] != (unsigned char)':' || in.ptr[19] != (unsigned char)' ') return false;
    wday = cerv_short_wday(in.ptr);
    month = cerv_month_index(in.ptr + 4U);
    if (wday < 0 || month == 0) return false;
    if (in.ptr[8] == (unsigned char)' ') {
        if (!cerv_digits(in.ptr + 9U, 1U, &day)) return false;
    } else if (!cerv_digits(in.ptr + 8U, 2U, &day)) {
        return false;
    }
    if (!cerv_digits(in.ptr + 11U, 2U, &hour) || !cerv_digits(in.ptr + 14U, 2U, &minute) ||
        !cerv_digits(in.ptr + 17U, 2U, &second) || !cerv_digits(in.ptr + 20U, 4U, &year)) return false;
    return cerv_finish_date((int)year, (unsigned)month, day, hour, minute, second, wday, out);
}

bool cerv_http_date_parse(struct cerv_span in, int current_year, struct cerv_http_date *out)
{
    if (out == NULL || in.ptr == NULL || current_year < 0 || current_year > 9999) return false;
    if (!(in.len == 24U || in.len == 29U || (in.len >= 30U && in.len <= 33U))) return false;
    if (cerv_parse_imf(in, out)) return true;
    if (cerv_parse_rfc850(in, current_year, out)) return true;
    return cerv_parse_asctime(in, out);
}

static void cerv_civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day)
{
    int64_t shifted = z + INT64_C(719468);
    int64_t era = (shifted >= 0 ? shifted : shifted - INT64_C(146096)) / INT64_C(146097);
    unsigned doe = (unsigned)(shifted - era * INT64_C(146097));
    unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    int y = (int)yoe + (int)(era * INT64_C(400));
    unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    unsigned mp = (5U * doy + 2U) / 153U;
    unsigned d = doy - (153U * mp + 2U) / 5U + 1U;
    unsigned m = mp < 10U ? mp + 3U : mp - 9U;
    y += m <= 2U ? 1 : 0;
    *year = y;
    *month = m;
    *day = d;
}

bool cerv_http_date_format_imf(int64_t unix_seconds, unsigned char out[29])
{
    /* Bound before civil-date arithmetic so every accepted int64_t is UB-free. */
    static const int64_t min_http_second = INT64_C(-62167219200);
    static const int64_t max_http_second = INT64_C(253402300799);
    int64_t days = 0;
    int64_t sod = 0;
    int year = 0, wday = 0;
    unsigned month = 0U, day = 0U, hour = 0U, minute = 0U, second = 0U;
    if (out == NULL || unix_seconds < min_http_second || unix_seconds > max_http_second) return false;
    days = unix_seconds / INT64_C(86400);
    sod = unix_seconds % INT64_C(86400);
    if (sod < 0) {
        sod += INT64_C(86400);
        --days;
    }
    cerv_civil_from_days(days, &year, &month, &day);
    if (year < 0 || year > 9999) return false;
    hour = (unsigned)(sod / INT64_C(3600));
    minute = (unsigned)((sod % INT64_C(3600)) / INT64_C(60));
    second = (unsigned)(sod % INT64_C(60));
    wday = (int)((days + INT64_C(4)) % INT64_C(7));
    if (wday < 0) wday += 7;
    memcpy(out, cerv_wday_short[wday], 3U);
    out[3] = (unsigned char)','; out[4] = (unsigned char)' ';
    out[5] = (unsigned char)('0' + day / 10U); out[6] = (unsigned char)('0' + day % 10U); out[7] = (unsigned char)' ';
    memcpy(out + 8U, cerv_months[month - 1U], 3U); out[11] = (unsigned char)' ';
    out[12] = (unsigned char)('0' + (unsigned)year / 1000U % 10U);
    out[13] = (unsigned char)('0' + (unsigned)year / 100U % 10U);
    out[14] = (unsigned char)('0' + (unsigned)year / 10U % 10U);
    out[15] = (unsigned char)('0' + (unsigned)year % 10U); out[16] = (unsigned char)' ';
    out[17] = (unsigned char)('0' + hour / 10U); out[18] = (unsigned char)('0' + hour % 10U); out[19] = (unsigned char)':';
    out[20] = (unsigned char)('0' + minute / 10U); out[21] = (unsigned char)('0' + minute % 10U); out[22] = (unsigned char)':';
    out[23] = (unsigned char)('0' + second / 10U); out[24] = (unsigned char)('0' + second % 10U); out[25] = (unsigned char)' ';
    memcpy(out + 26U, "GMT", 3U);
    return true;
}
