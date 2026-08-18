#define _POSIX_C_SOURCE 200809L
#include "linux/clock.h"

#include "base/checked.h"

#include <stdint.h>
#include <time.h>

_Static_assert((time_t)-1 < (time_t)0, "Cerv requires signed time_t");
_Static_assert(sizeof(time_t) <= sizeof(int64_t), "time_t must fit int64_t");

bool cerv_clock_mono_now(struct cerv_mono_time *out)
{
    struct timespec ts;
    uint64_t seconds_ns;
    uint64_t total;
    if (out == NULL || clock_gettime(CLOCK_MONOTONIC, &ts) != 0 || ts.tv_sec < (time_t)0 ||
        ts.tv_nsec < 0L || ts.tv_nsec >= 1000000000L) return false;
    if (!cerv_u64_mul((uint64_t)ts.tv_sec, UINT64_C(1000000000), &seconds_ns) ||
        !cerv_u64_add(seconds_ns, (uint64_t)ts.tv_nsec, &total)) return false;
    out->ns = total;
    return true;
}

bool cerv_clock_unix_now(int64_t *out)
{
    struct timespec ts;
    if (out == NULL || clock_gettime(CLOCK_REALTIME, &ts) != 0) return false;
    *out = (int64_t)ts.tv_sec;
    return true;
}
