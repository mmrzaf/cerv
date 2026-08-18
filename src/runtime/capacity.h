#ifndef CERV_CAPACITY_H
#define CERV_CAPACITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool cerv_runtime_worker_memory_bytes(size_t slots, size_t *out);
bool cerv_runtime_required_worker_fds(size_t slots, size_t *out);
bool cerv_runtime_auto_connections(size_t workers, size_t target, uint64_t nofile_soft,
                                   bool nofile_infinite, size_t *out);

#endif
