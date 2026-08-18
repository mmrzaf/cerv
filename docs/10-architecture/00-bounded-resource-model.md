# Bounded Resource Model

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Memory, file descriptor, CPU, state-space, and environmental bounds.  

## Bound classes

Cerv defines four classes of bounds.

### A. Compile-time structural bounds

Examples:

- maximum request header bytes;
- maximum request-line bytes;
- maximum field count;
- maximum single field-line bytes;
- maximum response-header bytes;
- maximum event batch size;
- maximum error body size.

These bounds are part of the executable and SHOULD change rarely.

### B. Startup configuration bounds

Examples:

- worker count;
- total connection slots;
- socket backlog request;
- timeouts;
- immutable caching mode.

Configuration is validated once before serving.

### C. Runtime monotonic bounds

Examples:

- absolute per-request header deadline: from acceptance for request 1 and from prior-response completion for each permitted reuse;
- write no-progress deadline;
- maximum total connection lifetime;
- shutdown deadline.

These use `CLOCK_MONOTONIC`-derived timestamps and saturating/checked arithmetic.

### D. Environmental assumptions

Examples:

- filesystem syscall completion;
- kernel scheduling;
- page-cache/block-device health;
- host-level FD availability beyond Cerv's startup checks.

These cannot be honestly made hard real-time guarantees by this design and SHALL be documented as assumptions.

## Baseline structural bounds

The baseline structural bounds are:

| **Bound** | **Baseline** | **Reasoning** |
| --- | --- | --- |
| Total request header section | 16 KiB | Ample for intended clients; small fixed per-connection footprint |
| Request line | 4 KiB | Prevents target abuse while covering realistic static paths |
| Single field line | 8 KiB | Bounded parsing and response to pathological fields |
| Header field count | 64 | Avoids quadratic/metadata abuse; enough for normal proxy chains |
| Response header buffer | 4 KiB | Cerv owns a deliberately small header vocabulary |
| Error body | <=256 B | Compile-time constant plain-text errors |
| epoll event batch | 256 | Limits stack/work batch and syscall frequency |
| accept batch per dispatch | 64 | Prevents listener from starving active connections |
| file-send quantum | 1 MiB | Prevents a permanently writable large file from owning a worker loop |
| worker-count hard cap | 1024 | Bounds supervisor PID/readiness arrays and startup fan-out |
| requested listen backlog | 1024 | Fixed operator-visible request, still capped by Linux `somaxconn` |
| lifecycle diagnostic record | 512 B | Fixed best-effort nonblocking lifecycle construction |
| worker readiness deadline | 10 s | Absolute monotonic cap for the complete post-fork readiness wait |
| requests per connection | 64 | Bounded HTTP/1.1 sequential reuse; final response closes |

All limits SHALL have exact boundary tests: limit-1, limit, and limit+1.

## Baseline runtime defaults

| **Setting** | **Baseline default** | **Semantics** |
| --- | --- | --- |
| Total max connections | auto, target <= 4096 | Startup caps the 4096 service-wide target to the current worker/`RLIMIT_NOFILE` FD budget; explicit numeric values are hard requests |
| Header deadline | 5 s | Absolute per request: first from `accept4()` success, reused request from prior-response completion; never extended by received bytes |
| Write no-progress timeout | 30 s | Resets only after response bytes actually leave userspace/kernel call successfully |
| Max connection lifetime | 5 min | Absolute cap; never resets |
| Graceful shutdown | 30 s | Master-wide drain deadline |
| Worker startup readiness | 10 s | Absolute master wait after worker creation; timeout force-kills/reaps incomplete startup |

These values are part of the resource contract. A value MAY change only through an explicit specification change backed by boundary tests and measured operational evidence; the meaning of a bound SHALL NOT silently change.

## Memory accounting

Each worker SHALL allocate its connection arena and other request-serving structures before reporting ready and entering steady-state service. Allocation-size arithmetic is checked first; allocation failure is a startup failure, never a request-path fallback.
Automatic worker discovery may also allocate a temporary dynamically sized CPU-affinity mask during master startup, bounded by `CERV_AFFINITY_CPU_PROBE_MAX`; that storage is freed before service topology creation and is never request-path state.

The implementation SHALL make it possible to derive an upper bound of the form:

```text
worker_memory <= fixed_worker_state
               + connection_slots * sizeof(struct cerv_conn)
               + timer_heap_capacity * sizeof(timer_node)
               + epoll_batch_storage
               + bounded_auxiliary_tables
```

The release documentation SHOULD publish actual sizes for each architecture/compiler configuration after build.

No request path SHALL call `malloc()`, `calloc()`, `realloc()`, `free()`, `strdup()`, `asprintf()`, or allocator-backed convenience APIs owned by Cerv after worker initialization.

This rule is about reasoning and failure behavior, not a claim that general-purpose allocators are intrinsically slow.

## FD accounting

At startup Cerv SHALL calculate a conservative required FD budget including at least:

- listener(s);
- one epoll FD per worker;
- one signal FD in processes that consume signals through `signalfd()`;
- root directory FD per worker or inherited/shared-open equivalent;
- one client socket per active slot;
- at most one open representation file per active sending connection;
- master supervision descriptors if used;
- small fixed safety margin for diagnostics/runtime internals.

`RLIMIT_NOFILE` SHALL be inspected. If the configured maximum cannot be supported safely, Cerv SHALL fail startup with a precise diagnostic rather than discover `EMFILE` under load.

Runtime `EMFILE`/`ENFILE` remains a distinct operational failure category and MUST NOT be reported as 404.

Cerv derives the largest local partition as `ceil(total_connections / workers)`. Its conservative per-worker FD requirement is `2 * local_slots + CERV_FD_SAFETY_MARGIN`, where the two slot-dependent descriptors cover one client socket plus one selected representation FD and the fixed margin covers listener/root/epoll/signalfd/startup/runtime descriptors. The largest partition is checked against the per-process `RLIMIT_NOFILE` soft limit before workers are created. Worker arena/timer memory is overflow-checked and actually allocated before that worker reports ready.

## CPU work bounds

Parsing SHALL be single-pass or otherwise demonstrably linear in the bounded input size. Field handling SHALL avoid unbounded hash tables and attacker-dependent allocation.

Known headers SHOULD be recognized while scanning and stored in fixed fields/flags. Unknown valid fields SHOULD be syntax-validated and skipped without copying into a dynamic collection.

Operations over comma-separated values SHALL be bounded by the field-line limit and avoid repeated rescanning from the start.

## State-space bounds

Cerv SHOULD make protocol state enumerable. The implementation should contain a small finite set of:

- connection states;
- request parse results;
- methods;
- selected representation encodings;
- response status families Cerv can emit;
- filesystem result classifications;
- timer kinds;
- lifecycle states.

The project treats a finite state space as a verification feature: if a subsystem cannot be described compactly, it is probably too general for Cerv.

## Host/kernel-bounded operations

Not every Linux operation can be made latency-bounded by userspace. Cerv's finite user-space loop/work bounds do not claim a deadline for a task blocked indefinitely in kernel D-state, a dying block device, hostile FUSE/network filesystem, or `waitpid()` on a child the kernel itself cannot reap. The master preserves the stronger no-orphan invariant: abnormal cleanup sends SIGKILL and waits until every known worker is reaped. Service-manager escalation for a kernel-stuck process is therefore an explicit platform responsibility, not deferred Cerv work.
