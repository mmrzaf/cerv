# Source layout

Cerv's production source is intentionally shallow, layered, and explicit. Directory boundaries correspond to ownership, dependency, and syscall boundaries that matter during review.

```text
src/
├── main.c
├── base/
├── http/
├── linux/
├── serve/
├── runtime/
└── process/
```

## `base/`

Checked arithmetic, slices, bounded buffers, time-value helpers, and invariant handling. This layer has no dependency on HTTP, filesystem, socket, or process-control modules.

## `http/`

Pure HTTP request/field/date/range/target parsing and normalization. These functions operate on bounded byte spans and do not perform filesystem or network I/O.

## `linux/`

Concrete Linux boundaries: monotonic/realtime clock access, confined filesystem operations, seccomp policy, and optional Landlock setup.

## `serve/`

Media types, representation selection, validators, ranges, and response planning. This layer composes HTTP semantics with already-confined file descriptors.

## `runtime/`

Fixed-capacity arenas, timer heap, connection state, socket/file ownership, and the single-threaded epoll worker.

## `process/`

Immutable startup configuration, worker-count discovery, lifecycle diagnostics, resource planning, forking, signal handling, and supervision.

## Dependency rule

Dependencies flow downward through these layers; lower layers do not reach into process/runtime policy. `tools/check_source_layers.sh` enforces the permitted include directions, and `make headers` verifies that public headers are self-contained under GCC and Clang.

`CONTRACTS.md` is the concrete cross-module ownership/API contract register.
