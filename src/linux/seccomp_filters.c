#include "linux/seccomp_filters.h"

#include <asm/unistd.h>
#include <linux/audit.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__x86_64__)
#define CERV_AUDIT_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define CERV_AUDIT_ARCH AUDIT_ARCH_AARCH64
#else
#error "Cerv supports seccomp profiles only on x86-64 and AArch64"
#endif

#define CERV_SC_ALLOW(syscall_nr) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)(syscall_nr), 0U, 1U), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

static const struct sock_filter cerv_worker_filter[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CERV_AUDIT_ARCH, 1U, 0U),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)offsetof(struct seccomp_data, nr)),
#ifdef __NR_read
    CERV_SC_ALLOW(__NR_read),
#endif
#ifdef __NR_write
    CERV_SC_ALLOW(__NR_write),
#endif
#ifdef __NR_close
    CERV_SC_ALLOW(__NR_close),
#endif
#ifdef __NR_epoll_wait
    CERV_SC_ALLOW(__NR_epoll_wait),
#endif
#ifdef __NR_epoll_pwait
    CERV_SC_ALLOW(__NR_epoll_pwait),
#endif
#ifdef __NR_epoll_pwait2
    CERV_SC_ALLOW(__NR_epoll_pwait2),
#endif
#ifdef __NR_epoll_ctl
    CERV_SC_ALLOW(__NR_epoll_ctl),
#endif
#ifdef __NR_accept4
    CERV_SC_ALLOW(__NR_accept4),
#endif
#ifdef __NR_recvfrom
    CERV_SC_ALLOW(__NR_recvfrom),
#endif
#ifdef __NR_recvmsg
    CERV_SC_ALLOW(__NR_recvmsg),
#endif
#ifdef __NR_sendto
    CERV_SC_ALLOW(__NR_sendto),
#endif
#ifdef __NR_sendmsg
    CERV_SC_ALLOW(__NR_sendmsg),
#endif
#ifdef __NR_sendfile
    CERV_SC_ALLOW(__NR_sendfile),
#endif
#ifdef __NR_openat2
    CERV_SC_ALLOW(__NR_openat2),
#endif
#ifdef __NR_fstat
    CERV_SC_ALLOW(__NR_fstat),
#endif
#ifdef __NR_newfstatat
    CERV_SC_ALLOW(__NR_newfstatat),
#endif
#ifdef __NR_statx
    CERV_SC_ALLOW(__NR_statx),
#endif
#ifdef __NR_pread64
    CERV_SC_ALLOW(__NR_pread64),
#endif
#ifdef __NR_clock_gettime
    CERV_SC_ALLOW(__NR_clock_gettime),
#endif
#ifdef __NR_rt_sigreturn
    CERV_SC_ALLOW(__NR_rt_sigreturn),
#endif
#ifdef __NR_munmap
    CERV_SC_ALLOW(__NR_munmap),
#endif
#ifdef __NR_brk
    CERV_SC_ALLOW(__NR_brk),
#endif
#ifdef __NR_madvise
    CERV_SC_ALLOW(__NR_madvise),
#endif
#ifdef __NR_exit
    CERV_SC_ALLOW(__NR_exit),
#endif
#ifdef __NR_exit_group
    CERV_SC_ALLOW(__NR_exit_group),
#endif
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS)
};

static const struct sock_filter cerv_master_filter[] = {
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)offsetof(struct seccomp_data, arch)),
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, CERV_AUDIT_ARCH, 1U, 0U),
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (uint32_t)offsetof(struct seccomp_data, nr)),
#ifdef __NR_read
    CERV_SC_ALLOW(__NR_read),
#endif
#ifdef __NR_write
    CERV_SC_ALLOW(__NR_write),
#endif
#ifdef __NR_close
    CERV_SC_ALLOW(__NR_close),
#endif
#ifdef __NR_poll
    CERV_SC_ALLOW(__NR_poll),
#endif
#ifdef __NR_ppoll
    CERV_SC_ALLOW(__NR_ppoll),
#endif
#ifdef __NR_wait4
    CERV_SC_ALLOW(__NR_wait4),
#endif
#ifdef __NR_waitid
    CERV_SC_ALLOW(__NR_waitid),
#endif
#ifdef __NR_kill
    CERV_SC_ALLOW(__NR_kill),
#endif
#ifdef __NR_clock_gettime
    CERV_SC_ALLOW(__NR_clock_gettime),
#endif
#ifdef __NR_rt_sigreturn
    CERV_SC_ALLOW(__NR_rt_sigreturn),
#endif
#ifdef __NR_exit
    CERV_SC_ALLOW(__NR_exit),
#endif
#ifdef __NR_exit_group
    CERV_SC_ALLOW(__NR_exit_group),
#endif
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS)
};

const struct sock_filter *cerv_seccomp_worker_filter(size_t *count)
{
    if (count != NULL) *count = sizeof(cerv_worker_filter) / sizeof(cerv_worker_filter[0]);
    return cerv_worker_filter;
}

const struct sock_filter *cerv_seccomp_master_filter(size_t *count)
{
    if (count != NULL) *count = sizeof(cerv_master_filter) / sizeof(cerv_master_filter[0]);
    return cerv_master_filter;
}
