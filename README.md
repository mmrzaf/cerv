# Cerv

Cerv is a small, bounded Linux HTTP file server written in C17. It is designed for the boring production job of serving a read-only file tree behind a reverse proxy/CDN while keeping protocol, filesystem, memory, FD, deadline, and process behavior explicit enough to audit.

> **Serve files correctly. Reject ambiguity. Stay bounded. Depend on Linux.**

Current product version: **1.0.0**.

## What Cerv does

- Linux-only HTTP/1.x origin serving for GET and HEAD.
- Strict request framing, Host/absolute-form handling, conditional requests, byte ranges, HTTP dates, and precompressed `.br`/`.gz` sidecars.
- Root-FD filesystem confinement using mandatory `openat2()` restrictions; regular files only and no symlink traversal.
- Fixed-capacity single-threaded epoll workers supervised by one master process.
- Bounded sequential HTTP/1.1 connection reuse: at most 64 successful requests per socket, with no request-body support or pipelining contract.
- `sendfile()` fast path with a bounded `pread()+send()` fallback.
- Absolute header/lifetime/shutdown deadlines plus write no-progress deadlines.
- Mandatory worker/master `no_new_privs` + seccomp; Landlock is optional defense in depth when the running kernel provides it.
- Native environment-variable configuration with command-line override semantics.
- FD-aware automatic connection sizing.
- Optional `--spa-fallback` / `CERV_SPA_FALLBACK` for client-side-routed static applications.
- Dotfiles and dot-directories (`.env`, `.git/`, ...) are never served; only `.well-known/` is public.
- No request-time Cerv allocation, no user-space request queue, and no synchronous core access log.

TLS, HTTP/2/3, dynamic compression, authentication, directory listing, general application routing, and access logging are intentionally outside the core; put those at the edge.

## Build

Requirements are Linux, a C17 toolchain, GNU Make, and libc development headers.

```sh
make
make release
./build/release/cerv --version
```

The controlled release build uses GCC by default (`RELEASE_CC=gcc`). Clang is independently exercised by the verification suite.

## Run

```sh
./build/release/cerv \
  --listen 127.0.0.1:8080 \
  --workers auto \
  --max-connections auto \
  /srv/cerv
```

Cerv stays in the foreground. `--max-connections auto` targets at most 4096 service-wide slots and caps that target to what the resolved worker count and current `RLIMIT_NOFILE` can support. A numeric value is a hard operator request and startup refuses it when its derived worker FD requirement cannot be met.

The same configuration can be supplied natively through the environment:

```sh
CERV_LISTEN=127.0.0.1:8080 \
CERV_ROOT=/srv/cerv \
CERV_WORKERS=auto \
CERV_MAX_CONNECTIONS=auto \
./build/release/cerv
```

Configuration precedence is:

```text
built-in defaults < environment < command line
```

For nginx, systemd, and container examples, see `deploy/`.

## Configuration

```text
Usage: cerv [OPTIONS] [ROOT]

--listen ADDRESS:PORT
--workers N|auto
--max-connections N|auto
--header-timeout DURATION
--write-timeout DURATION
--max-lifetime DURATION
--shutdown-timeout DURATION
--spa-fallback PATH
--immutable
--mutable
-h, --help
-V, --version
```

Environment mirrors use the `CERV_` prefix:

- `CERV_LISTEN`
- `CERV_ROOT`
- `CERV_WORKERS`
- `CERV_MAX_CONNECTIONS`
- `CERV_HEADER_TIMEOUT`
- `CERV_WRITE_TIMEOUT`
- `CERV_MAX_LIFETIME`
- `CERV_SHUTDOWN_TIMEOUT`
- `CERV_IMMUTABLE`
- `CERV_SPA_FALLBACK`

Addresses are numeric IPv4 or bracketed IPv6. Durations accept positive `ms`, `s`, or `m` values. `--immutable` is an operator assertion; `--mutable` explicitly overrides an immutable environment setting.

### SPA runtime

```sh
CERV_LISTEN=0.0.0.0:8080 \
CERV_ROOT=./dist \
CERV_SPA_FALLBACK=/index.html \
./build/release/cerv
```

Only genuine file-not-found outcomes for extensionless client-side routes fall back to the configured file; a missing `/app.js` is still a `404`. Permission, negotiation, malformed-request, and operational failures preserve their real status.

## Docker

Build the runtime image:

```sh
docker build -t cerv:1.0.0 .
docker run --rm \
  -p 8080:8080 \
  --read-only \
  --cap-drop=ALL \
  --security-opt=no-new-privileges \
  --ulimit nofile=65536:65536 \
  -v "$PWD/dist:/srv/cerv:ro" \
  cerv:1.0.0
```

The runtime stage is `scratch`: it contains only the statically linked Cerv executable plus image metadata, runs as non-root UID/GID `65532:65532`, starts Cerv directly as PID 1, and defaults to environment-only configuration (`0.0.0.0:8080`, `/srv/cerv`, auto workers, FD-aware auto connection sizing). See `deploy/container/` for Compose and SPA multi-stage examples.

## Verification

Important local gates:

```sh
make test                 # GCC + Clang + architecture/header checks
make sanitize             # ASan + UBSan + LeakSanitizer
make analyze              # GCC -fanalyzer + Clang Static Analyzer (+ clang-tidy when installed)
make coverage             # LLVM coverage
make fuzz                 # 17 independent libFuzzer targets
make proof                # native exhaustive checks + CBMC when installed
make proof-required       # require CBMC
make differential         # direct-vs-nginx framing differential
make syscall-audit        # measured post-seccomp syscall vocabulary
make cross-aarch64        # AArch64 seccomp/UAPI plus full cross-link when toolchain exists
make bounds-report        # concrete per-slot memory/FD derivation
make reproducible         # two controlled clean builds must be byte-identical
make hardening-check      # static PIE + RELRO/NOW + non-executable stack
make container-check      # Docker/Podman image + SPA/runtime smoke test
make verification-check   # complete locally executable verification pipeline
make release-check        # verification plus container smoke check
```

`make release-bundle` creates deterministic source and architecture-specific binary bundles under `dist/`, including separate debug symbols, SHA-256 manifests, SPDX 2.3 SBOM, and local build provenance.

## GitHub releases

The repository ships SHA-pinned GitHub Actions for continuous verification and publication. Release publication is tag-driven: the maintainer creates a `v`-prefixed tag whose base version matches `VERSION`, and the workflow verifies the tagged tree before publishing anything. The workflow never creates or moves Git tags.

A hosted release publishes:

- deterministic source and Linux x86-64 binary bundles;
- a Docker-loadable Linux amd64 image archive as a GitHub Release asset;
- a multi-architecture Linux amd64/arm64 image in GHCR;
- SHA-256 manifests and the GHCR image digest;
- GitHub/Sigstore artifact attestations for release files and the registry image.

A tag with a SemVer suffix is treated as a GitHub prerelease and does not receive `latest` or stable major/minor image aliases. An exact stable version tag receives the stable GHCR aliases.

## Source architecture

```text
src/
├── main.c
├── base/       # checked/bounded primitives
├── http/       # pure HTTP and request-target semantics
├── linux/      # concrete Linux syscall boundaries and sandboxing
├── serve/      # media type, representation selection, response planning
├── runtime/    # capacity, timer, connection state, epoll worker
└── process/    # configuration, diagnostics, supervision
```

`tools/check_source_layers.sh` enforces permitted dependency directions. Public headers are self-contained under GCC and Clang. See `src/CONTRACTS.md` for concrete ownership/API contracts and `docs/10-architecture/06-source-architecture.md` for the normative layer model.

## Performance and bounds

`bench/generate_corpus.py` creates the file-size corpus and `bench/run_matrix.py` records a repeatable ApacheBench matrix as JSON. Local benchmark numbers are evidence for regressions and deployment tuning, not portable performance promises.

`make bounds-report` derives memory and FD costs from the exact built structs. Remeasure it after compiler, architecture, or structure changes instead of copying old numbers into capacity plans.

Bounded HTTP/1.1 reuse is part of the production design because reverse-proxy small-file workloads benefit materially from upstream connection reuse while Cerv keeps a 64-request cap, absolute lifetime bound, and no-pipelining contract.

## Security and support

Read `SECURITY.md` before production deployment. The normative security model and operational limitations live under `docs/`. Cerv does not claim a hard userspace deadline over a task stuck in uninterruptible kernel/filesystem I/O.

## Documentation

- `CONTRIBUTING.md` — contribution workflow and verification expectations.
- `docs/00-index.md` — normative documentation map.
- `docs/` — product, HTTP, Linux, operations, engineering, and release contracts.
- `src/CONTRACTS.md` — concrete module ownership and API invariants.
- `deploy/` — nginx, systemd, and container deployment examples.
