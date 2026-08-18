#define _GNU_SOURCE
#include "process/config_workers.h"

#include "base/bounds.h"
#include "base/checked.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CERV_CGROUP_ROOT "/sys/fs/cgroup"
#define CERV_SELF_CGROUP_PATH "/proc/self/cgroup"
#define CERV_CGROUP_PATH_MAX ((size_t)4096)

bool cerv_config_auto_worker_count(size_t affinity_count, bool quota_limited,
                                   uint64_t quota, uint64_t period, size_t *out)
{
    size_t count;
    if (out == NULL || affinity_count == 0U) return false;
    count = affinity_count > CERV_WORKERS_MAX ? CERV_WORKERS_MAX : affinity_count;
    if (quota_limited) {
        uint64_t rounded;
        uint64_t quota_workers;
        if (quota == UINT64_C(0) || period == UINT64_C(0) || quota > UINT64_MAX - (period - UINT64_C(1))) return false;
        rounded = quota + period - UINT64_C(1);
        quota_workers = rounded / period;
        if (quota_workers == UINT64_C(0)) quota_workers = UINT64_C(1);
        if (quota_workers < (uint64_t)count) count = (size_t)quota_workers;
    }
    if (count == 0U) count = 1U;
    *out = count;
    return true;
}

static bool cerv_read_small_file(const char *path, char *buf, size_t cap, size_t *out_len)
{
    int fd;
    ssize_t n;
    ssize_t extra;
    if (path == NULL || buf == NULL || cap < 2U || out_len == NULL) return false;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    do { n = read(fd, buf, cap - 1U); } while (n < 0 && errno == EINTR);
    if (n < 0) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return false;
    }
    if ((size_t)n == cap - 1U) {
        unsigned char byte;
        do { extra = read(fd, &byte, 1U); } while (extra < 0 && errno == EINTR);
        if (extra != 0) {
            int saved = extra < 0 ? errno : EOVERFLOW;
            (void)close(fd);
            errno = saved;
            return false;
        }
    }
    if (close(fd) != 0) return false;
    buf[(size_t)n] = '\0';
    *out_len = (size_t)n;
    return true;
}

static bool cerv_self_cgroup_v2(char *path, size_t cap, bool *found)
{
    char buf[CERV_CGROUP_PATH_MAX];
    size_t len;
    size_t pos = 0U;
    if (path == NULL || cap < 2U || found == NULL) return false;
    *found = false;
    if (!cerv_read_small_file(CERV_SELF_CGROUP_PATH, buf, sizeof(buf), &len)) return false;
    while (pos < len) {
        size_t line_start = pos;
        size_t line_end;
        while (pos < len && buf[pos] != '\n') ++pos;
        line_end = pos;
        if (pos < len) ++pos;
        if (line_end >= line_start + 3U && buf[line_start] == '0' &&
            buf[line_start + 1U] == ':' && buf[line_start + 2U] == ':') {
            size_t path_len = line_end - (line_start + 3U);
            const char *src = buf + line_start + 3U;
            if (path_len == 0U || src[0] != '/' || path_len >= cap) return false;
            memcpy(path, src, path_len);
            path[path_len] = '\0';
            *found = true;
            return true;
        }
    }
    return true;
}

static bool cerv_cpu_max_ws(unsigned char c)
{
    return c == (unsigned char)' ' || c == (unsigned char)'\t' ||
           c == (unsigned char)'\r' || c == (unsigned char)'\n';
}

bool cerv_config_parse_cpu_max(const unsigned char *data, size_t len, bool *limited,
                               uint64_t *quota, uint64_t *period)
{
    size_t pos = 0U;
    size_t first_start;
    size_t first_len;
    size_t second_start;
    size_t second_len;
    uint64_t parsed_quota = UINT64_C(0);
    uint64_t parsed_period = UINT64_C(0);
    bool parsed_limited;
    if (data == NULL || limited == NULL || quota == NULL || period == NULL || len == 0U || len >= (size_t)96) return false;
    while (pos < len && cerv_cpu_max_ws(data[pos])) ++pos;
    first_start = pos;
    while (pos < len && !cerv_cpu_max_ws(data[pos])) ++pos;
    first_len = pos - first_start;
    if (first_len == 0U || pos == len) return false;
    while (pos < len && cerv_cpu_max_ws(data[pos])) ++pos;
    second_start = pos;
    while (pos < len && !cerv_cpu_max_ws(data[pos])) ++pos;
    second_len = pos - second_start;
    if (second_len == 0U) return false;
    while (pos < len && cerv_cpu_max_ws(data[pos])) ++pos;
    if (pos != len || !cerv_u64_decimal(data + second_start, second_len, &parsed_period) ||
        parsed_period == UINT64_C(0)) return false;
    if (first_len == 3U && memcmp(data + first_start, "max", 3U) == 0) {
        parsed_limited = false;
    } else {
        if (!cerv_u64_decimal(data + first_start, first_len, &parsed_quota) || parsed_quota == UINT64_C(0)) return false;
        parsed_limited = true;
    }
    *limited = parsed_limited;
    *quota = parsed_quota;
    *period = parsed_period;
    return true;
}

static bool cerv_read_cpu_max_at(const char *cgroup_path, bool *exists, bool *limited,
                                 uint64_t *quota, uint64_t *period)
{
    char full[CERV_CGROUP_PATH_MAX + 32U];
    char buf[96];
    size_t len;
    int n;
    if (cgroup_path == NULL || exists == NULL || limited == NULL || quota == NULL || period == NULL) return false;
    *exists = false;
    *limited = false;
    *quota = UINT64_C(0);
    *period = UINT64_C(0);
    n = snprintf(full, sizeof(full), "%s%s%s", CERV_CGROUP_ROOT,
                 strcmp(cgroup_path, "/") == 0 ? "" : cgroup_path, "/cpu.max");
    if (n < 0 || (size_t)n >= sizeof(full)) return false;
    if (!cerv_read_small_file(full, buf, sizeof(buf), &len)) {
        if (errno == ENOENT || errno == ENOTDIR) return true;
        return false;
    }
    *exists = true;
    return cerv_config_parse_cpu_max((const unsigned char *)buf, len, limited, quota, period);
}

static bool cerv_apply_cgroup_cpu_limits(size_t *count)
{
    char path[CERV_CGROUP_PATH_MAX];
    bool found;
    if (count == NULL || *count == 0U) return false;
    if (!cerv_self_cgroup_v2(path, sizeof(path), &found)) return false;
    if (!found) return true;
    for (;;) {
        bool exists;
        bool limited;
        uint64_t quota;
        uint64_t period;
        size_t next;
        if (!cerv_read_cpu_max_at(path, &exists, &limited, &quota, &period)) return false;
        if (exists && limited) {
            if (!cerv_config_auto_worker_count(*count, true, quota, period, &next)) return false;
            *count = next;
        }
        if (strcmp(path, "/") == 0) break;
        {
            char *slash = strrchr(path, '/');
            if (slash == NULL) return false;
            if (slash == path) path[1] = '\0';
            else *slash = '\0';
        }
    }
    return true;
}

static bool cerv_affinity_cpu_count(size_t *out)
{
    size_t capacity = (size_t)CPU_SETSIZE;
    if (out == NULL || capacity == 0U) return false;
    for (;;) {
        size_t set_size = CPU_ALLOC_SIZE(capacity);
        cpu_set_t *set = CPU_ALLOC(capacity);
        int affinity;
        int saved;
        if (set == NULL || set_size == 0U) {
            if (set != NULL) CPU_FREE(set);
            return false;
        }
        CPU_ZERO_S(set_size, set);
        if (sched_getaffinity(0, set_size, set) == 0) {
            affinity = CPU_COUNT_S(set_size, set);
            CPU_FREE(set);
            if (affinity <= 0) return false;
            *out = (size_t)affinity;
            return true;
        }
        saved = errno;
        CPU_FREE(set);
        if (saved != EINVAL || capacity >= CERV_AFFINITY_CPU_PROBE_MAX ||
            capacity > CERV_AFFINITY_CPU_PROBE_MAX / 2U) {
            errno = saved;
            return false;
        }
        capacity *= 2U;
    }
}

bool cerv_config_detect_auto_workers(size_t *out)
{
    size_t affinity;
    size_t count;
    if (out == NULL || !cerv_affinity_cpu_count(&affinity) ||
        !cerv_config_auto_worker_count(affinity, false, 0U, 0U, &count)) return false;
    if (!cerv_apply_cgroup_cpu_limits(&count)) return false;
    *out = count;
    return true;
}
