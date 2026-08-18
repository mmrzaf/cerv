#include "base/bounds.h"
#include "runtime/capacity.h"
#include "runtime/conn.h"
#include "runtime/timer.h"
#include "runtime/worker.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const size_t samples[] = {1U, 64U, 256U, 1024U, 4096U};
    size_t i;

    printf("Cerv fixed-capacity bounds (build architecture)\n");
    printf("sizeof(cerv_conn)=%zu\n", sizeof(struct cerv_conn));
    printf("sizeof(cerv_timer_node)=%zu\n", sizeof(struct cerv_timer_node));
    printf("sizeof(cerv_worker)=%zu\n", sizeof(struct cerv_worker));
    printf("request_bytes_per_connection=%u\n", (unsigned)CERV_REQUEST_BYTES_MAX);
    printf("keepalive_requests_per_connection=%" PRIu32 "\n", CERV_KEEPALIVE_REQUESTS_MAX);
    printf("fd_safety_margin=%u\n", (unsigned)CERV_FD_SAFETY_MARGIN);

    for (i = 0U; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        size_t memory = 0U;
        size_t fds = 0U;
        if (!cerv_runtime_worker_memory_bytes(samples[i], &memory) ||
            !cerv_runtime_required_worker_fds(samples[i], &fds)) {
            fprintf(stderr, "bound calculation failed for %zu slots\n", samples[i]);
            return EXIT_FAILURE;
        }
        printf("slots=%zu worker_arena_bytes=%zu required_worker_fds=%zu\n",
               samples[i], memory, fds);
    }
    return EXIT_SUCCESS;
}
