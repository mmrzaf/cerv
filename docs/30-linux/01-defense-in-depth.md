# Defense in Depth

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Non-root operation, no_new_privs, seccomp, Landlock, namespaces, core dumps, and ELF hardening.  

## Principle

Filesystem confinement and correct HTTP parsing are primary security controls. Sandboxing is additional containment, not an excuse for weak core logic.

## Non-root execution

Production Cerv SHOULD run as an unprivileged UID/GID supplied by the service manager/container runtime.

Cerv itself SHALL NOT implement user switching in the core. Binding privileged ports should be handled by a reverse proxy, socket activation design (if later specified), capability assignment outside Cerv, or a higher port.

## no_new_privs

Where compatible with the final lifecycle, Cerv SHOULD enable Linux `no_new_privs` before entering steady-state service, preventing `execve()` from granting additional privilege through setuid/setgid binaries or file capabilities.

Cerv should not execute external programs anyway; this remains useful defense in depth.

## seccomp

A steady-state Cerv worker should have a small syscall vocabulary. After the architecture is stable, a seccomp-BPF allowlist SHOULD be evaluated.

Important: Linux kernel documentation is explicit that seccomp filtering is **not a sandbox by itself**. It reduces exposed kernel surface and must be combined with ordinary access controls and other isolation.

The syscall allowlist SHALL be generated/reviewed from measured release behavior on each supported architecture and libc. Guessing a tiny list before implementation stabilizes risks fragile production failures.

Expected steady-state syscall families include:

```text
epoll_wait / epoll_pwait2 as selected
epoll_ctl
accept4
recvfrom/recvmsg or recv
sendto/sendmsg or send
sendfile
openat2
newfstatat/fstat/statx as used
pread64 for fallback
close
clock_gettime
write for bounded diagnostics
exit/exit_group
rt_sig* / signalfd-related lifecycle as actually used
```

The real list SHALL come from tracing and ABI review, not this illustrative list.

## Landlock

Landlock MAY be evaluated as an additional filesystem access-control layer because it can restrict ambient filesystem rights for unprivileged processes and composes with other Linux security mechanisms.

However:

- it is not the primary request-path containment mechanism;
- `openat2()` root resolution remains mandatory;
- rules must account for descriptors opened before restrictions and exact kernel Landlock ABI support;
- availability/configuration differs by kernel.

If enabled, lack of Landlock MAY be either fatal or optional depending on the declared release security profile. That choice must be explicit.

## Namespaces and read-only root

Production deployment SHOULD use host/container mechanisms to provide:

- read-only document root;
- read-only executable/container filesystem where practical;
- minimal filesystem visibility;
- no unnecessary Linux capabilities;
- `no_new_privs`/equivalent container setting;
- constrained CPU/memory/PID resources.

Cerv should remain safe without assuming a container, but defense layers are encouraged.

## Core dumps

Release deployment guidance SHOULD address core dumps. A C server crash dump can contain request data and operational context.

Reasonable profiles include:

- disable core dumps in internet-facing production by default;
- permit them only in controlled staging/diagnostic deployments;
- keep separate debug symbols for postmortem symbolication.

## ASLR/PIE and linker hardening

Official release builds SHOULD use position-independent executable and conventional ELF hardening such as RELRO/NOW and non-executable stack, subject to verification on supported toolchains.

These are defense in depth and do not replace C-level correctness.

## Sandbox profile

The release architecture enables `PR_SET_NO_NEW_PRIVS` in both worker and master before their steady-state seccomp profiles are installed. Failure to establish `no_new_privs` or the applicable seccomp filter is a startup/runtime-fatal condition; Cerv does not continue with a broader syscall surface. The BPF default action is process kill and the filter checks the Linux audit architecture before syscall-number matching.

Workers additionally attempt Landlock after opening/inheriting the document-root FD and before reporting ready. Cerv discovers the running Landlock ABI at runtime, creates a ruleset handling all filesystem rights known to the build headers for that ABI, and grants only read-file/read-directory beneath the document root. Landlock is an **optional defense-in-depth layer** in the baseline profile: an unavailable Landlock syscall is reported as `landlock=unavailable` and does not weaken or replace mandatory `openat2()` `RESOLVE_BENEATH|RESOLVE_NO_SYMLINKS|RESOLVE_NO_MAGICLINKS` confinement. For the ABI probe, `ENOSYS`, `EOPNOTSUPP`, and `EPERM` are explicit unavailability classes; `EPERM` permits operation inside an outer container policy that blocks the optional Landlock syscall. A Landlock setup error after a supported ABI has been established is fatal.

Instrumented sanitizer/coverage builds intentionally do not install seccomp/Landlock in the process under test because the instrumentation runtimes execute syscalls unrelated to Cerv. The ordinary dual-compiler integration suite and release syscall-audit gate execute the real sandbox and include a forbidden-syscall `SIGSYS` regression. This instrumentation exception SHALL NOT be enabled in release builds.
