# Standards and Platform Basis

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Authoritative standards, C language baseline, Linux baseline, architectures, and platform assumptions.  

## HTTP standards

Cerv's HTTP behavior SHALL be designed against the current HTTP core specifications rather than obsolete RFC 723x-era summaries:

- RFC 9110, HTTP Semantics (STD 97);
- RFC 9111, HTTP Caching;
- RFC 9112, HTTP/1.1 (STD 99), including later updates that apply to HTTP/1.1;
- RFC 8246 for the `immutable` Cache-Control extension;
- RFC 2119 + RFC 8174 (BCP 14) for normative language;
- URI syntax from RFC 3986 where HTTP delegates URI rules.

Where Cerv deliberately accepts less than the full optional syntax permitted by HTTP, the restriction MUST be one a recipient is allowed to choose, or the server MUST return the standards-appropriate error rather than silently reinterpret the input.

## C and libc

Cerv SHALL target ISO C17. This does **not** mean freestanding C and does not imply a no-libc project.

The baseline intentionally uses a normal Linux libc for standard facilities and syscall wrappers where available. Direct `syscall()` use SHALL be limited to Linux interfaces for which the chosen libc does not provide a suitable wrapper, with `openat2()` being the canonical example on common libc versions.

Rationale:

- eliminating libc would add startup, ABI, formatting, errno, and syscall-wrapper work that does not improve the core mission;
- a libc boundary is widely understood and can be tested across glibc/musl if desired;
- Linux-specific behavior remains explicit even when invoked through libc.

## Linux baseline

Cerv is Linux software, not portable Unix software.

Cerv SHALL require Linux 6.1 or newer. This baseline is old enough to be operationally mature while comfortably containing the kernel primitives Cerv relies on. Cerv SHALL NOT carry weaker compatibility fallbacks solely to support older kernels.

The minimum kernel SHALL, at minimum, support:

- `openat2()` and the required resolution flags;
- `epoll` and the chosen listener wakeup strategy;
- `accept4()`;
- `signalfd()`;
- `sendfile()`;
- monotonic clocks;
- `PR_SET_NO_NEW_PRIVS` and seccomp-BPF required by the final sandbox profile;
- Landlock when available for optional filesystem defense in depth.

Cerv SHALL fail at startup when a **required security primitive** is unavailable. It SHALL NOT silently fall back from `openat2()` confinement to a weaker pathname strategy merely to run on an older kernel.

## CPU architectures

Official stable artifacts SHOULD target:

- x86-64;
- AArch64.

Architecture-specific optimizations SHALL NOT alter protocol behavior, bounds, or safety policy. Generic release artifacts SHALL NOT use `-march=native` or equivalent host-specific code generation.

## Platform assumptions

The production profile assumes:

- a local or well-behaved read-only filesystem;
- a monotonic clock supplied by Linux;
- an init/container system responsible for identity, restart, cgroups, namespaces, and deployment policy;
- a reverse proxy or CDN when TLS, HTTP/2, HTTP/3, internet edge policy, rate limiting, or broad observability is required.

Remote network filesystems, FUSE filesystems, dying block devices, and kernel-level stalls are not guaranteed to preserve latency bounds. They MAY work, but their liveness properties are outside Cerv's core guarantee.
