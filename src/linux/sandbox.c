#define _GNU_SOURCE 1
#include "linux/sandbox.h"

#include "linux/seccomp_filters.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_landlock_create_ruleset
#define CERV_HAVE_LANDLOCK_SYSCALLS 0
#else
#define CERV_HAVE_LANDLOCK_SYSCALLS 1
#endif

bool cerv_sandbox_no_new_privs(void)
{
    return prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L) == 0;
}

#if CERV_HAVE_LANDLOCK_SYSCALLS
static uint64_t cerv_landlock_handled_fs(int abi)
{
    uint64_t rights =
        (uint64_t)LANDLOCK_ACCESS_FS_EXECUTE |
        (uint64_t)LANDLOCK_ACCESS_FS_WRITE_FILE |
        (uint64_t)LANDLOCK_ACCESS_FS_READ_FILE |
        (uint64_t)LANDLOCK_ACCESS_FS_READ_DIR |
        (uint64_t)LANDLOCK_ACCESS_FS_REMOVE_DIR |
        (uint64_t)LANDLOCK_ACCESS_FS_REMOVE_FILE |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_CHAR |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_DIR |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_REG |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_SOCK |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_FIFO |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_BLOCK |
        (uint64_t)LANDLOCK_ACCESS_FS_MAKE_SYM;
#ifdef LANDLOCK_ACCESS_FS_REFER
    if (abi >= 2) rights |= (uint64_t)LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi >= 3) rights |= (uint64_t)LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
#ifdef LANDLOCK_ACCESS_FS_IOCTL_DEV
    if (abi >= 5) rights |= (uint64_t)LANDLOCK_ACCESS_FS_IOCTL_DEV;
#endif
#ifdef LANDLOCK_ACCESS_FS_RESOLVE_UNIX
    if (abi >= 9) rights |= (uint64_t)LANDLOCK_ACCESS_FS_RESOLVE_UNIX;
#endif
    return rights;
}
#endif

bool cerv_sandbox_landlock_read_root(const struct cerv_fs_root *root, enum cerv_landlock_status *status)
{
#if CERV_HAVE_LANDLOCK_SYSCALLS
    struct landlock_ruleset_attr ruleset_attr = {0};
    struct landlock_path_beneath_attr path_attr = {0};
    long abi;
    long ruleset_fd;
    long added;
    long restricted;
    int saved;

    if (root == NULL || root->fd < 0 || status == NULL) return false;
    abi = syscall(SYS_landlock_create_ruleset, NULL, 0U, LANDLOCK_CREATE_RULESET_VERSION);
    if (abi < 0L) {
        if (errno == ENOSYS || errno == EOPNOTSUPP || errno == EPERM) {
            *status = CERV_LANDLOCK_UNAVAILABLE;
            return true;
        }
        return false;
    }
    if (abi < 1L || abi > (long)INT32_MAX) return false;
    ruleset_attr.handled_access_fs = cerv_landlock_handled_fs((int)abi);
    ruleset_fd = syscall(SYS_landlock_create_ruleset, &ruleset_attr, sizeof(ruleset_attr), 0U);
    if (ruleset_fd < 0L || ruleset_fd > (long)INT32_MAX) return false;

    path_attr.allowed_access = (uint64_t)LANDLOCK_ACCESS_FS_READ_FILE | (uint64_t)LANDLOCK_ACCESS_FS_READ_DIR;
    path_attr.parent_fd = root->fd;
    added = syscall(SYS_landlock_add_rule, (int)ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_attr, 0U);
    if (added < 0L) {
        saved = errno;
        (void)close((int)ruleset_fd);
        errno = saved;
        return false;
    }
    restricted = syscall(SYS_landlock_restrict_self, (int)ruleset_fd, 0U);
    if (restricted < 0L) {
        saved = errno;
        (void)close((int)ruleset_fd);
        errno = saved;
        return false;
    }
    (void)close((int)ruleset_fd);
    *status = CERV_LANDLOCK_ENABLED;
    return true;
#else
    if (root == NULL || root->fd < 0 || status == NULL) return false;
    *status = CERV_LANDLOCK_UNAVAILABLE;
    return true;
#endif
}

static bool cerv_seccomp_apply(const struct sock_filter *filter, size_t count)
{
    struct sock_fprog program;
    long result;
    if (filter == NULL || count == 0U || count > (size_t)UINT16_MAX) return false;
    program.len = (unsigned short)count;
    program.filter = (struct sock_filter *)(uintptr_t)(const void *)filter;
    result = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0U, &program);
    return result == 0L;
}

static bool cerv_seccomp_worker(void)
{
    size_t count = 0U;
    const struct sock_filter *filter = cerv_seccomp_worker_filter(&count);
    return cerv_seccomp_apply(filter, count);
}

static bool cerv_seccomp_master(void)
{
    size_t count = 0U;
    const struct sock_filter *filter = cerv_seccomp_master_filter(&count);
    return cerv_seccomp_apply(filter, count);
}

bool cerv_sandbox_seccomp_install(enum cerv_seccomp_profile profile)
{
    switch (profile) {
    case CERV_SECCOMP_WORKER:
        return cerv_seccomp_worker();
    case CERV_SECCOMP_MASTER:
        return cerv_seccomp_master();
    default:
        return false;
    }
}

bool cerv_sandbox_worker_enter(const struct cerv_fs_root *root, enum cerv_landlock_status *landlock_status)
{
#ifdef CERV_INSTRUMENTED_BUILD
    if (root == NULL || root->fd < 0 || landlock_status == NULL) return false;
    *landlock_status = CERV_LANDLOCK_UNAVAILABLE;
    return cerv_sandbox_no_new_privs();
#else
    if (!cerv_sandbox_no_new_privs() || !cerv_sandbox_landlock_read_root(root, landlock_status)) return false;
    return cerv_sandbox_seccomp_install(CERV_SECCOMP_WORKER);
#endif
}

bool cerv_sandbox_master_enter(void)
{
    if (!cerv_sandbox_no_new_privs()) return false;
#ifdef CERV_INSTRUMENTED_BUILD
    return true;
#else
    return cerv_sandbox_seccomp_install(CERV_SECCOMP_MASTER);
#endif
}
