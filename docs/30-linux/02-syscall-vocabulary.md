# Syscall Vocabulary

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Master and worker syscall vocabulary baseline for auditing and confinement.  

Cerv uses reviewed architecture-specific seccomp filters. The source of truth for exact allowed numbers is `src/linux/seccomp_filters.c`; this document records the semantic vocabulary and measurement policy.

## Master startup/lifecycle

Likely families include:

```text
socket
setsockopt
bind
listen
open/openat/openat2 for startup validation as selected
fstat/newfstatat/statx
getrlimit/prlimit64
sched_getaffinity
sigprocmask/pthread_sigmask equivalent
signalfd4
pipe2 (startup readiness only)
fork/clone-family as libc implements fork
wait4/waitid
kill
poll for master lifecycle/readiness
read/write
close
clock_gettime
exit_group
```

Actual libc may invoke supporting syscalls. Trace release builds.

## Worker steady state

Likely families include:

```text
epoll_create1
epoll_ctl
epoll_wait or epoll_pwait2
signalfd4/read for worker lifecycle control
accept4
recvfrom/recvmsg
sendto/sendmsg
sendfile
openat2
fstat/newfstatat/statx
pread64
clock_gettime
rt_sigaction/sigaction libc wrapper for SIGPIPE policy
close
write
exit_group
```

Do not derive seccomp solely from source-level function names; libc/runtime startup and architecture ABI matter.

## Measured steady-state profile

The release verification harness traces descendants with `ptrace`, records syscalls only **after each process successfully installs seccomp**, and checks the observed names are subsets of the reviewed profile. On the x86-64 verification runner, the exercised request/lifecycle mix observed:

```text
master: read write close poll wait4 kill exit_group
worker: read write close fstat munmap sendfile sendto recvfrom exit_group epoll_wait epoll_ctl accept4 openat2
```

The allowlist is intentionally a reviewed superset of one trace because libc/architecture/kernel paths can select equivalent interfaces such as `ppoll`, `waitid`, `epoll_pwait*`, `recvmsg`/`sendmsg`, `newfstatat`/`statx`, `pread64`, `clock_gettime`, allocator teardown (`brk`/`madvise`/`munmap`), and signal return. Adding a syscall to production requires source justification plus an updated measurement/review record; a one-off trace is never sufficient justification by itself.

The master filter is installed only after forking, readiness, root/listener closure, and other startup-only work. The worker filter is installed after its fixed storage, epoll/control state, root/listener inheritance, and optional Landlock setup are complete but before the worker reports ready.
