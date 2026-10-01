# Changelog

## 1.0.0

Initial Cerv release.

- Strict bounded HTTP/1.x static-file origin for GET and HEAD.
- Conditional requests, single byte ranges, HTTP dates, MIME mapping, and precompressed Brotli/gzip sidecars.
- Strong ETags built from size, modification time, and content-coding only, identical on every replica serving the same tree and never exposing device, inode, or ctime; `If-Range` resumes downloads with them.
- Dotfiles and dot-directories (`.env`, `.git/`, ...) are never served; only a leading `.well-known/` is public.
- Linux root-FD confinement with mandatory `openat2()` restrictions and regular-file-only serving.
- Fixed-capacity epoll workers supervised by a fail-stop master process; workers drain out if the master dies, and `SIGHUP` drains like `SIGTERM` unless it was ignored at start.
- Workers survive transient descriptor or memory exhaustion on `accept()` with a bounded backoff instead of failing the service.
- Bounded sequential HTTP/1.1 connection reuse with no request-body or pipelining contract; idle connections close silently and stalled requests receive `408`.
- `sendfile()` fast path with bounded `pread()` + `send()` fallback, and response headers coalesced with content to halve TCP segments for small files.
- Explicit header, write-progress, lifetime, startup, and shutdown deadlines.
- Mandatory `no_new_privs` and seccomp profiles with Landlock defense in depth, enforceable with `--landlock require`.
- Native `CERV_*` environment configuration, CLI overrides with errors that state what is expected, FD-aware automatic connection sizing, and opt-in SPA fallback for extensionless client-side routes (missing assets stay `404`).
- Non-root multi-stage Docker runtime plus nginx, systemd, Compose, and SPA deployment examples.
- Dual-compiler tests, sanitizers, static analyzers, fuzzing, adversarial integration, bounded model checks, syscall auditing, cross-architecture seccomp validation, reproducible bundles, SBOM, provenance, and hardened release checks.
