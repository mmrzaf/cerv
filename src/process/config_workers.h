#ifndef CERV_CONFIG_WORKERS_H
#define CERV_CONFIG_WORKERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cerv_config_detect_auto_workers(size_t *out);
bool cerv_config_parse_cpu_max(const unsigned char *data, size_t len, bool *limited,
                               uint64_t *quota, uint64_t *period);
bool cerv_config_auto_worker_count(size_t affinity_count, bool quota_limited,
                                   uint64_t quota, uint64_t period, size_t *out);

#endif
