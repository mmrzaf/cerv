#ifndef CERV_CLOCK_H
#define CERV_CLOCK_H

#include "base/cerv_time.h"

#include <stdbool.h>
#include <stdint.h>

bool cerv_clock_mono_now(struct cerv_mono_time *out);
bool cerv_clock_unix_now(int64_t *out);

#endif
