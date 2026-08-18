# Changelog

## 1.0.0

Initial Cerv release.

- Strict bounded HTTP/1.x static-file origin for GET and HEAD.
- Conditional requests, single byte ranges, HTTP dates, representation-specific validators, MIME mapping, and precompressed Brotli/gzip sidecars.
- Linux root-FD confinement with mandatory `openat2()` restrictions and regular-file-only serving.
- Fixed-capacity epoll workers supervised by a fail-stop master process.
- Bounded sequential HTTP/1.1 connection reuse with no request-body or pipelining contract.
- `sendfile()` fast path with bounded `pread()` + `send()` fallback.
- Explicit header, write-progress, lifetime, startup, and shutdown deadlines.
- Mandatory `no_new_privs` and seccomp profiles with optional Landlock defense in depth.
- Native `CERV_*` environment configuration, CLI overrides, FD-aware automatic connection sizing, and opt-in SPA fallback.
- Non-root multi-stage Docker runtime plus nginx, systemd, Compose, and SPA deployment examples.
- Dual-compiler tests, sanitizers, static analyzers, fuzzing, adversarial integration, bounded model checks, syscall auditing, cross-architecture seccomp validation, reproducible bundles, SBOM, provenance, and hardened release checks.
