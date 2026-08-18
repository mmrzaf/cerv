#include "base/cerv_time.h"

#include "base/checked.h"

#include <stdint.h>

#define CERV_NS_PER_MS UINT64_C(1000000)
#define CERV_NS_PER_SECOND UINT64_C(1000000000)

bool cerv_duration_from_ms(uint64_t ms, struct cerv_duration *out)
{
    uint64_t ns = UINT64_C(0);
    if (out == NULL || !cerv_u64_mul(ms, CERV_NS_PER_MS, &ns)) {
        return false;
    }
    out->ns = ns;
    return true;
}

bool cerv_duration_from_seconds(uint64_t seconds, struct cerv_duration *out)
{
    uint64_t ns = UINT64_C(0);
    if (out == NULL || !cerv_u64_mul(seconds, CERV_NS_PER_SECOND, &ns)) {
        return false;
    }
    out->ns = ns;
    return true;
}

struct cerv_mono_time cerv_mono_add_saturating(struct cerv_mono_time t, struct cerv_duration d)
{
    struct cerv_mono_time out = {.ns = UINT64_MAX};
    if (d.ns <= UINT64_MAX - t.ns) {
        out.ns = t.ns + d.ns;
    }
    return out;
}

uint64_t cerv_mono_remaining_ms_ceil(struct cerv_mono_time now, struct cerv_mono_time deadline)
{
    uint64_t delta = UINT64_C(0);
    if (deadline.ns <= now.ns) {
        return UINT64_C(0);
    }
    delta = deadline.ns - now.ns;
    return delta / CERV_NS_PER_MS + (delta % CERV_NS_PER_MS != UINT64_C(0) ? UINT64_C(1) : UINT64_C(0));
}
