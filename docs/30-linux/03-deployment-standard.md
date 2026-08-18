# Deployment Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Recommended topology, reverse-proxy contract, read-only content, service managers, health checking, and containers.  

## Recommended topology

The preferred internet-facing topology is:

```text
Internet
   |
TLS / HTTP/2 / HTTP/3 / CDN / edge policy
   |
reverse proxy on trusted network/host boundary
   |
HTTP/1.x
   |
Cerv
   |
read-only local static root
```

Cerv does not require a proxy for parser safety, but the proxy owns concerns Cerv intentionally refuses to implement.

## Reverse-proxy contract

Deployment documentation SHOULD include tested examples for common proxies but avoid tying correctness to one vendor.

Important interoperability checks:

- proxy sends canonical HTTP/1 request lines/headers;
- upstream connection closure is handled efficiently;
- absolute-form behavior is tested where proxy modes can emit it;
- encoded slash/backslash normalization does not create surprising reachability differences;
- Host forwarding/reconstruction is understood;
- proxy timeouts exceed or intentionally align with Cerv deadlines;
- proxy does not retry non-idempotent methods to Cerv (Cerv only serves GET/HEAD anyway);
- CDN caching respects `Vary`, validators, and Cache-Control.

## Read-only content

Production roots SHOULD be mounted/readable by Cerv but not writable by the Cerv UID.

Deployment SHOULD use immutable/atomic release directories:

```text
/releases/1234/...
```

and switch the service/root reference atomically between deployments instead of editing live files in place.

## Service manager

Cerv runs in the foreground.

systemd/container runtime handles:

- restart policy;
- UID/GID;
- working directory;
- capability policy;
- cgroup memory/CPU/PID limits;
- filesystem namespace/read-only mounts;
- stdout/stderr capture;
- health/lifecycle monitoring.

No double-daemon/fork-to-background behavior exists.

## Health checking

A dedicated dynamic `/healthz` endpoint is not automatically part of core Cerv because it introduces a resource that is not a file.

Preferred health check options:

- TCP connect plus request of a small known static file shipped in the deployment;
- process/supervisor health combined with representative static GET.

If a built-in health endpoint is later justified, it must be compile-time/simple, bounded, and clearly excluded from document-root semantics.

## Container profile

A hardened container SHOULD use:

- non-root UID/GID;
- read-only root filesystem;
- document root mounted read-only;
- no added capabilities;
- `no-new-privileges`;
- tight seccomp once Cerv's required syscall profile is known;
- memory/CPU/PID limits;
- no shell/package manager in a minimal runtime image when practical.

Container minimalism is useful but secondary to verifiable release provenance and runtime invariants.

The repository-root multi-stage `Dockerfile` is the canonical runtime image profile. It runs Cerv directly as a non-root PID 1 and configures the common container defaults through Cerv's native `CERV_*` environment parser rather than a shell wrapper. See `docs/50-operations/03-container-runtime.md` for the exact image/SPA contract.

Because container `RLIMIT_NOFILE` is runtime policy rather than an image constant, the official image uses `CERV_MAX_CONNECTIONS=auto`. A numeric connection cap remains an explicit hard request and requires a sufficient container `nofile` limit.

## Deployment behavior

Cerv's master remains the foreground process and owns the fixed worker set for its entire lifetime. Operators SHOULD supervise that master as one service/cgroup. An unexpected worker exit intentionally terminates the complete Cerv service nonzero; systemd/container orchestration is the restart boundary.

The integration suite sends absolute-form HTTP/1.1 requests with ordinary `X-Forwarded-*` headers through the real multiworker runtime. Proxy-added unknown valid fields remain harmless, absolute-form authority handling follows the protocol contract, and Cerv does not trust forwarding headers for filesystem routing or client identity.

Proxy/service-manager shutdown grace SHOULD exceed Cerv's configured `--shutdown-timeout` plus a small orchestration margin so the master gets the opportunity to drain and reap workers before the manager escalates.
