#ifndef CERV_SANDBOX_H
#define CERV_SANDBOX_H

#include "linux/fs.h"

#include <stdbool.h>

enum cerv_landlock_status {
    CERV_LANDLOCK_ENABLED = 0,
    CERV_LANDLOCK_UNAVAILABLE
};

enum cerv_seccomp_profile {
    CERV_SECCOMP_WORKER = 0,
    CERV_SECCOMP_MASTER
};

bool cerv_sandbox_no_new_privs(void);
bool cerv_sandbox_landlock_read_root(const struct cerv_fs_root *root, enum cerv_landlock_status *status);
bool cerv_sandbox_seccomp_install(enum cerv_seccomp_profile profile);
bool cerv_sandbox_worker_enter(const struct cerv_fs_root *root, enum cerv_landlock_status *landlock_status);
bool cerv_sandbox_master_enter(void);

#endif
