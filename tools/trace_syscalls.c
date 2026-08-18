#define _GNU_SOURCE 1
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(__x86_64__)
#error "trace_syscalls is currently an x86-64 verification tool"
#endif

#define TRACE_MAX_PIDS 2048U
#define TRACE_MAX_SYSCALL 1024U

struct tracee {
    pid_t pid;
    bool used;
    bool is_master;
    bool entering;
    bool sandboxed;
    long current_nr;
};

static const char *syscall_name(long nr)
{
#define CERV_NR_NAME(name) do { if (nr == (long)SYS_##name) return #name; } while (0)
#ifdef SYS_read
    CERV_NR_NAME(read);
#endif
#ifdef SYS_write
    CERV_NR_NAME(write);
#endif
#ifdef SYS_close
    CERV_NR_NAME(close);
#endif
#ifdef SYS_poll
    CERV_NR_NAME(poll);
#endif
#ifdef SYS_ppoll
    CERV_NR_NAME(ppoll);
#endif
#ifdef SYS_wait4
    CERV_NR_NAME(wait4);
#endif
#ifdef SYS_waitid
    CERV_NR_NAME(waitid);
#endif
#ifdef SYS_kill
    CERV_NR_NAME(kill);
#endif
#ifdef SYS_clock_gettime
    CERV_NR_NAME(clock_gettime);
#endif
#ifdef SYS_rt_sigreturn
    CERV_NR_NAME(rt_sigreturn);
#endif
#ifdef SYS_exit
    CERV_NR_NAME(exit);
#endif
#ifdef SYS_exit_group
    CERV_NR_NAME(exit_group);
#endif
#ifdef SYS_epoll_wait
    CERV_NR_NAME(epoll_wait);
#endif
#ifdef SYS_epoll_pwait
    CERV_NR_NAME(epoll_pwait);
#endif
#ifdef SYS_epoll_pwait2
    CERV_NR_NAME(epoll_pwait2);
#endif
#ifdef SYS_epoll_ctl
    CERV_NR_NAME(epoll_ctl);
#endif
#ifdef SYS_accept4
    CERV_NR_NAME(accept4);
#endif
#ifdef SYS_recvfrom
    CERV_NR_NAME(recvfrom);
#endif
#ifdef SYS_recvmsg
    CERV_NR_NAME(recvmsg);
#endif
#ifdef SYS_sendto
    CERV_NR_NAME(sendto);
#endif
#ifdef SYS_sendmsg
    CERV_NR_NAME(sendmsg);
#endif
#ifdef SYS_sendfile
    CERV_NR_NAME(sendfile);
#endif
#ifdef SYS_openat2
    CERV_NR_NAME(openat2);
#endif
#ifdef SYS_fstat
    CERV_NR_NAME(fstat);
#endif
#ifdef SYS_newfstatat
    CERV_NR_NAME(newfstatat);
#endif
#ifdef SYS_statx
    CERV_NR_NAME(statx);
#endif
#ifdef SYS_pread64
    CERV_NR_NAME(pread64);
#endif
#ifdef SYS_munmap
    CERV_NR_NAME(munmap);
#endif
#ifdef SYS_brk
    CERV_NR_NAME(brk);
#endif
#ifdef SYS_madvise
    CERV_NR_NAME(madvise);
#endif
#ifdef SYS_seccomp
    CERV_NR_NAME(seccomp);
#endif
#undef CERV_NR_NAME
    return NULL;
}

static struct tracee *find_tracee(struct tracee tracees[], pid_t pid)
{
    size_t i;
    for (i = 0U; i < TRACE_MAX_PIDS; ++i) {
        if (tracees[i].used && tracees[i].pid == pid) return &tracees[i];
    }
    return NULL;
}

static struct tracee *add_tracee(struct tracee tracees[], pid_t pid, bool master)
{
    size_t i;
    struct tracee *existing = find_tracee(tracees, pid);
    if (existing != NULL) return existing;
    for (i = 0U; i < TRACE_MAX_PIDS; ++i) {
        if (!tracees[i].used) {
            tracees[i].pid = pid;
            tracees[i].used = true;
            tracees[i].is_master = master;
            tracees[i].entering = true;
            tracees[i].sandboxed = false;
            tracees[i].current_nr = -1L;
            return &tracees[i];
        }
    }
    return NULL;
}

static void remove_tracee(struct tracee *tracee)
{
    if (tracee != NULL) tracee->used = false;
}

static bool any_tracee(const struct tracee tracees[])
{
    size_t i;
    for (i = 0U; i < TRACE_MAX_PIDS; ++i) if (tracees[i].used) return true;
    return false;
}

static bool write_pid_file(const char *path, pid_t pid)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    if (fprintf(f, "%ld\n", (long)pid) < 0 || fclose(f) != 0) return false;
    return true;
}

static void print_set(FILE *out, const char *label, const bool observed[])
{
    size_t i;
    fprintf(out, "%s", label);
    for (i = 0U; i < TRACE_MAX_SYSCALL; ++i) {
        if (observed[i]) {
            const char *name = syscall_name((long)i);
            if (name != NULL) fprintf(out, " %s", name);
            else fprintf(out, " syscall_%zu", i);
        }
    }
    fputc('\n', out);
}

int main(int argc, char **argv)
{
    const char *output_path;
    const char *pid_path;
    FILE *out = NULL;
    pid_t child;
    struct tracee tracees[TRACE_MAX_PIDS] = {{0}};
    bool master_observed[TRACE_MAX_SYSCALL] = {false};
    bool worker_observed[TRACE_MAX_SYSCALL] = {false};
    int root_exit = 0;
    bool root_seen_exit = false;

    if (argc < 5 || strcmp(argv[3], "--") != 0) {
        fprintf(stderr, "usage: %s OUTPUT PIDFILE -- PROGRAM [ARGS...]\n", argv[0]);
        return 2;
    }
    output_path = argv[1];
    pid_path = argv[2];
    child = fork();
    if (child < (pid_t)0) {
        perror("fork");
        return 1;
    }
    if (child == (pid_t)0) {
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) != 0) _exit(126);
        if (raise(SIGSTOP) != 0) _exit(126);
        execvp(argv[4], &argv[4]);
        _exit(127);
    }
    if (!write_pid_file(pid_path, child)) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        fprintf(stderr, "cannot write pid file\n");
        return 1;
    }
    if (add_tracee(tracees, child, true) == NULL) return 1;

    while (any_tracee(tracees)) {
        int status = 0;
        pid_t pid = waitpid((pid_t)-1, &status, __WALL);
        struct tracee *t;
        int deliver = 0;
        if (pid < (pid_t)0) {
            if (errno == EINTR) continue;
            if (errno == ECHILD) break;
            perror("waitpid");
            return 1;
        }
        t = find_tracee(tracees, pid);
        if (t == NULL) {
            t = add_tracee(tracees, pid, false);
            if (t == NULL) {
                fprintf(stderr, "tracee table exhausted\n");
                return 1;
            }
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (pid == child) {
                root_seen_exit = true;
                root_exit = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            }
            remove_tracee(t);
            continue;
        }
        if (!WIFSTOPPED(status)) continue;

        if ((unsigned)status >> 16U != 0U) {
            unsigned event = (unsigned)status >> 16U;
            if (event == (unsigned)PTRACE_EVENT_FORK || event == (unsigned)PTRACE_EVENT_VFORK ||
                event == (unsigned)PTRACE_EVENT_CLONE) {
                unsigned long message = 0UL;
                if (ptrace(PTRACE_GETEVENTMSG, pid, NULL, &message) == 0) {
                    if (message > 0UL && message <= (unsigned long)INT32_MAX) {
                        if (add_tracee(tracees, (pid_t)message, false) == NULL) return 1;
                    }
                }
            }
        }

        if (WSTOPSIG(status) == (SIGTRAP | 0x80)) {
            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, pid, NULL, &regs) != 0) {
                if (errno != ESRCH) { perror("PTRACE_GETREGS"); return 1; }
            } else if (t->entering) {
                long nr = (long)regs.orig_rax;
                t->current_nr = nr;
                if (t->sandboxed && nr >= 0L && nr < (long)TRACE_MAX_SYSCALL) {
                    if (t->is_master) master_observed[(size_t)nr] = true;
                    else worker_observed[(size_t)nr] = true;
                }
                t->entering = false;
            } else {
#ifdef SYS_seccomp
                if (t->current_nr == (long)SYS_seccomp && (long)regs.rax == 0L) t->sandboxed = true;
#endif
                t->entering = true;
            }
        } else {
            int sig = WSTOPSIG(status);
            if (sig != SIGSTOP && sig != SIGTRAP) deliver = sig;
        }

        if (ptrace(PTRACE_SETOPTIONS, pid, NULL,
                   (void *)(uintptr_t)(PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                                       PTRACE_O_TRACECLONE | PTRACE_O_EXITKILL)) != 0 && errno != ESRCH) {
            perror("PTRACE_SETOPTIONS");
            return 1;
        }
        if (ptrace(PTRACE_SYSCALL, pid, NULL, (void *)(intptr_t)deliver) != 0 && errno != ESRCH) {
            perror("PTRACE_SYSCALL");
            return 1;
        }
    }

    out = fopen(output_path, "w");
    if (out == NULL) { perror("fopen output"); return 1; }
    print_set(out, "master-post-seccomp:", master_observed);
    print_set(out, "worker-post-seccomp:", worker_observed);
    if (fclose(out) != 0) return 1;
    if (!root_seen_exit) return 1;
    return root_exit;
}
