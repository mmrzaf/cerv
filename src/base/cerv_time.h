#ifndef CERV_TIME_H
#define CERV_TIME_H

#include <stdbool.h>
#include <stdint.h>

struct cerv_mono_time {
    uint64_t ns;
};

struct cerv_duration {
    uint64_t ns;
};

bool cerv_duration_from_ms(uint64_t ms, struct cerv_duration *out);
bool cerv_duration_from_seconds(uint64_t seconds, struct cerv_duration *out);
struct cerv_mono_time cerv_mono_add_saturating(struct cerv_mono_time t, struct cerv_duration d);
uint64_t cerv_mono_remaining_ms_ceil(struct cerv_mono_time now, struct cerv_mono_time deadline);

#endif
