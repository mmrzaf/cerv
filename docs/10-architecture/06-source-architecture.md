# Source Architecture and Internal Interfaces

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Source layout, dependency direction, ownership, module contracts, and state-machine structure.

The source architecture exists to make ownership, state transitions, syscall boundaries, and protocol boundaries visible. Cerv is an executable, not a reusable framework; its internal structure SHOULD optimize for auditability rather than abstraction reuse.

## Repository shape

Production source is intentionally shallow but no longer flat:

```text
src/
├── main.c
├── README.md
├── CONTRACTS.md
├── base/
│   ├── bounds.h
│   ├── buffer.c / buffer.h
│   ├── checked.c / checked.h
│   ├── invariant.c / invariant.h
│   ├── path.c / path.h
│   ├── slice.c / slice.h
│   └── time.c / cerv_time.h
├── http/
│   ├── http_date.c / http_date.h
│   ├── http_fields.c / http_fields.h
│   ├── http_range.c / http_range.h
│   ├── http_request.c / http_request.h
│   └── http_target.c / http_target.h
├── linux/
│   ├── clock.c / clock.h
│   ├── fs.c / fs.h
│   ├── sandbox.c / sandbox.h
│   └── seccomp_filters.c / seccomp_filters.h
├── serve/
│   ├── media_type.c / media_type.h
│   ├── representation.c / representation.h
│   └── response.c / response.h
├── runtime/
│   ├── arena.c
│   ├── capacity.c / capacity.h
│   ├── conn.c / conn.h
│   ├── conn_state.c
│   ├── timer.c / timer.h
│   └── worker.c / worker.h
└── process/
    ├── config.c / config.h
    ├── config_workers.c / config_workers.h
    ├── diag.c / diag.h
    └── supervisor.c / supervisor.h
```

These directories are architectural boundaries, not namespaces or framework packages. Further nesting SHOULD NOT be introduced unless a new durable ownership or dependency boundary appears.

The exact file count is not sacred. The boundaries are. Production source files SHALL NOT receive numeric filename prefixes. Numeric ordering is a documentation/navigation concern; conventional source names are clearer for C includes, diagnostics, tooling, and symbol-to-file discovery.

All internal project includes SHALL be qualified from `src/`, for example `#include "http/http_request.h"`. The normal verification gate SHALL reject forbidden cross-layer includes.

## Directory responsibilities

- `base/` contains bounded, allocation-free primitives with no HTTP, filesystem, socket, process, or Linux-runtime knowledge.
- `http/` contains pure HTTP/request-target grammar and semantics. It may depend only on `base/` and itself.
- `linux/` contains narrow concrete Linux syscall boundaries shared by higher layers. It is not a portability abstraction.
- `serve/` maps logical HTTP resources to filesystem representations and pure response plans.
- `runtime/` owns fixed connection/timer storage, runtime capacity formulas, socket/file transfer state, and the epoll worker.
- `process/` owns immutable startup configuration, auto-worker discovery, bounded lifecycle diagnostics, and supervisor/process topology.
- `main.c` is composition only: parse configuration and enter the supervisor.

## Module dependency direction

Dependencies SHALL remain one-way:

```text
base
 ├──> http
 ├──> linux
 │      └──────┐
 └─────────────┼──> serve
               │      └──> runtime
               │              └──> process
               └──────────────────> process

main -> process
```

More concretely, the permitted cross-directory edges are:

```text
http    -> base
linux   -> base
serve   -> base, http, linux
runtime -> base, http, linux, serve
process -> base, linux, runtime
main    -> process
```

Same-directory dependencies are allowed when they preserve the module contracts. Lower layers SHALL NOT call upward into worker/supervisor logic. HTTP parsing SHALL NOT know about sockets. Filesystem confinement SHALL NOT know about HTTP status codes. Response planning SHALL consume classified domain results rather than raw `errno` values.

`runtime/capacity.c` owns formulas that depend on concrete runtime representation sizes such as `sizeof(struct cerv_conn)` and `sizeof(struct cerv_timer_node)`. CLI configuration SHALL NOT include runtime representation headers merely to compute those sizes.

`process/config_workers.c` owns Linux affinity/cgroup worker discovery. `process/config.c` owns native environment/CLI grammar, precedence, bounded SPA fallback configuration, and deterministic service-slot partitioning; it does not own Linux cgroup traversal or runtime object sizing.

`linux/clock.c` is the single shared `clock_gettime()` boundary for worker/supervisor wall and monotonic time acquisition. Pure time arithmetic remains in `base/time.c`.

## Internal API rule

Every cross-module function SHALL have a contract that identifies:

- accepted input domain;
- output or result type;
- ownership effects;
- whether it can allocate;
- whether it can block;
- relevant upper bounds;
- relevant `errno` or internal error behavior;
- invariants preserved on both success and failure.

Functions SHOULD return small explicit result enums where multiple failure classes matter. Boolean return values are appropriate only for genuinely binary outcomes.

## No umbrella state object

Cerv SHALL NOT introduce a giant mutable `context`, `app`, or `runtime` structure passed through unrelated layers.

State SHALL live with the owner that enforces its invariants: worker runtime state with the worker, connection state with the connection slot, filesystem root state with the filesystem layer, and configuration as immutable startup state.

## Connection state machine

Connection transitions SHALL be represented by a small explicit enum and centralized dispatch. Callback chains and hidden state transitions are prohibited.

The runtime states are explicit:

```text
CERV_CONN_FREE
CERV_CONN_RECV_HEADERS
CERV_CONN_SEND_HEADERS
CERV_CONN_SEND_BODY
CERV_CONN_SEND_FILE
CERV_CONN_SEND_FALLBACK
```

`SEND_BODY` is used only for the fixed in-memory body selected by response planning; regular-file content uses `SEND_FILE`/`SEND_FALLBACK`.

A separate persistent `CLOSING` state SHOULD exist only if cleanup genuinely requires asynchronous work; otherwise close and slot release should be a direct terminal transition.

## File descriptor ownership

At any instant each descriptor SHALL have one owner. Current ownership is:

```text
supervisor  -> master listener/root copies through startup; master signalfd/readiness lifecycle
worker      -> inherited listener/root references; epoll + worker signalfd; timer/slot state
connection  -> accepted socket and selected representation file descriptor
filesystem  -> immutable root directory descriptor reference per owning process
```

Descriptor duplication or inheritance SHALL be deliberate and documented.

## Header discipline

Headers SHALL be self-contained and minimal. They SHALL expose only declarations needed across translation units.

No internal header SHALL become a transitive include bucket. A `.c` file SHOULD include its own header first so missing dependencies fail immediately. Concrete implementation bounds that are not part of a header API SHALL be included by the `.c` file that uses them rather than leaked through an unrelated public header.

## Symbol naming

Project-visible symbols SHALL use the `cerv_` prefix. Struct and enum tags SHOULD remain explicit:

```text
cerv_http_request_parse
cerv_fs_open_regular
cerv_response_plan_resource

struct cerv_conn
enum cerv_conn_state
```

Typedefs SHOULD be used only where they materially improve correctness or express a fixed-width/domain type; they SHALL NOT hide pointer ownership or turn structs into pseudo-objects.

## Shared primitive policy

Cerv MAY have narrowly scoped shared primitives such as bounded buffer writing, checked arithmetic, monotonic time conversion, or a concrete Linux clock acquisition wrapper.

It SHALL NOT have broad `util.c`, `common.c`, `helpers.c`, generic containers, object systems, plugin interfaces, or home-grown framework layers.

## Architecture evolution

Changes to the source tree SHALL preserve the documented dependency directions, ownership rules, bounded-resource model, and syscall boundaries. New abstractions must reduce concrete complexity or strengthen a contract; directory or interface growth without an architectural payoff is discouraged.

## Process layer

`process/config.c` is the CLI grammar and deterministic service-slot partition module. `process/config_workers.c` owns automatic worker discovery and cgroup/affinity parsing. `runtime/capacity.c` owns worker storage/FD formulas that depend on runtime representation sizes. `process/diag.c` owns bounded best-effort lifecycle records and must never become a request dependency. `process/supervisor.c` is the process owner: it may depend downward on configuration, filesystem startup, worker contracts, runtime capacity, Linux clock acquisition, and diagnostics; lower layers SHALL NOT depend on supervisor state.

The supervisor does not own or inspect connection slots, request bytes, response plans, or representation descriptors. The only parent/child startup IPC is one fixed-record readiness pipe that is closed before steady state. Lifecycle coordination thereafter uses process signals consumed through per-process `signalfd` descriptors.
