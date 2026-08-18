#ifndef CERV_LINUX_SECCOMP_FILTERS_H
#define CERV_LINUX_SECCOMP_FILTERS_H

#include <linux/filter.h>
#include <stddef.h>

const struct sock_filter *cerv_seccomp_worker_filter(size_t *count);
const struct sock_filter *cerv_seccomp_master_filter(size_t *count);

#endif
