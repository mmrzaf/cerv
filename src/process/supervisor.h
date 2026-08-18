#ifndef CERV_SUPERVISOR_H
#define CERV_SUPERVISOR_H

#include "process/config.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/resource.h>
#include <sys/types.h>

struct cerv_resource_plan {
    size_t max_worker_slots;
    size_t worker_memory_bytes;
    size_t required_worker_fds;
    rlim_t nofile_soft;
};

bool cerv_supervisor_resource_plan(const struct cerv_config *config, struct cerv_resource_plan *out);
bool cerv_supervisor_wait_startup(int signal_fd, int pipe_fd, const pid_t pids[], bool alive[],
                                  size_t worker_count, struct cerv_duration timeout, bool *landlock_all);
int cerv_supervisor_run(const struct cerv_config *config);

#endif
