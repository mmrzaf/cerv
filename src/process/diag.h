#ifndef CERV_DIAG_H
#define CERV_DIAG_H

#include <stddef.h>
#include <sys/types.h>

/* Best-effort lifecycle diagnostics. Preparation is idempotent. */
void cerv_diag_prepare(void);
void cerv_diag_close(void);
void cerv_diag_message(const char *level, const char *event, const char *detail);
void cerv_diag_worker_exit(pid_t pid, int status);
void cerv_diag_startup(size_t workers, size_t max_connections, size_t max_worker_slots,
                       size_t worker_memory_bytes, size_t required_worker_fds);

#endif
