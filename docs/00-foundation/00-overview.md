# Cerv Overview

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Mission, baseline decisions, and the meaning of boundedness.  

Cerv is a Linux-only static HTTP origin server written in C17. It exists to do one job with unusually explicit boundaries: receive an HTTP request, select a file representation rooted beneath one configured directory, produce a standards-correct response, and return to a known bounded state.

Cerv is **not** optimized for feature count, portability, dynamic content, configuration breadth, or benchmark spectacle. Its primary optimization target is a small and finite operational state space that can be inspected, tested, fuzzed, measured, and—in bounded components—formally checked.

The central project law is:

**Serve files correctly. Reject ambiguity. Stay bounded. Depend on Linux.**

Those four clauses are not marketing language. They are acceptance tests for every feature and implementation choice.

## Executive decisions

| **Area** | **Baseline decision** |
| --- | --- |
| Language | ISO C17; no C++ runtime; normal libc usage is allowed and expected |
| Platform | Linux only; Linux 6.1 or newer |
| Architectures | x86-64 and AArch64 |
| HTTP | HTTP/1.x origin server; GET and HEAD; bounded sequential HTTP/1.1 persistence; no request bodies or pipelining contract |
| Concurrency | Supervising master + fixed single-threaded worker processes |
| Eventing | Level-triggered epoll; one epoll instance per worker |
| Listener | One listener created by master and inherited by workers; `EPOLLEXCLUSIVE`, cooperative listener yield, and local-full deregistration/re-arm |
| Allocation | No Cerv-owned malloc/free in steady-state request processing |
| Admission | Fixed connection slots; no user-space request queue; overload rejected immediately |
| Filesystem | Root directory FD + openat2() confinement; no symlink traversal; regular files only |
| File transfer | sendfile() fast path; bounded pread()+send() fallback |
| Compression | Precompressed .br/.gz sidecars only; no dynamic compression |
| Range | One byte range only; no multipart ranges |
| Cache | Conservative revalidation by default; immutable caching only by explicit operator assertion |
| Configuration | Built-in defaults < native `CERV_*` environment < CLI; immutable after startup |
| SPA fallback | Optional one-file 404 fallback for extensionless routes; disabled by default; not a route/rewrite engine |
| Logging | No synchronous access log in the core v1 runtime; bounded lifecycle/fatal diagnostics only |
| TLS / H2 / H3 | Out of scope; terminate at a reverse proxy/CDN when needed |
| Verification | Dual compiler, sanitizers, fuzzing, static analysis, adversarial integration tests, bounded model checking for suitable pure/bounded helpers |
| Release | Reproducible-build goal, provenance, SBOM, hashes, pinned CI dependencies, staging/canary validation |

## What “bounded” means

For Cerv, boundedness is broader than “does not allocate too much memory.” A resource or behavior is considered bounded only when the implementation defines a finite maximum or finite state space that can be derived before or during operation.

Cerv aims to bound:

- accepted live connections;
- userspace memory owned by Cerv;
- request-line size, header bytes, header count, and individual field-line size;
- response-header construction;
- request parsing work per byte and per request;
- accepted methods and parser states;
- file descriptors under normal operation;
- per-event-loop dispatch work;
- file-transfer quantum per readiness dispatch;
- request header wait time;
- socket write no-progress time;
- total connection lifetime;
- graceful shutdown time, subject to documented kernel-I/O caveats;
- emitted diagnostic record sizes;
- supported configuration state;
- supported HTTP semantic state;
- release inputs and verification gates.

Cerv cannot honestly provide a hard wall-clock upper bound for arbitrary synchronous filesystem operations, page faults, device stalls, or tasks blocked in uninterruptible kernel sleep. The specification therefore distinguishes **Cerv-owned bounds** from **kernel/environment liveness assumptions** and uses process isolation to limit blast radius.
