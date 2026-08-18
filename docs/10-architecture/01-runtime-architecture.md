# Runtime Architecture

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Process topology, master/worker responsibilities, isolation, and connection-slot partitioning.  

## Baseline process topology

Cerv SHALL use the following process topology:

```text
                     +------------------+
                     |      master      |
                     | config / listen  |
                     | signals / reap   |
                     +---------+--------+
                               |
                         fork fixed N
                               |
             +-----------------+-----------------+
             |                 |                 |
       +-----v------+    +-----v------+    +-----v------+
       | worker 0  |    | worker 1  |    | worker N-1|
       | epoll     |    | epoll     |    | epoll      |
       | arena     |    | arena     |    | arena      |
       | timer heap|    | timer heap|    | timer heap |
       +-----------+    +-----------+    +------------+
             \                 |                 /
              \-------- shared listener --------/
```

Each worker is single-threaded and owns its request-processing state. There are no worker threads, request queues, mutexes, condition variables, or shared mutable connection objects.

## Why processes

C makes memory-safety failures more consequential than in a memory-safe language. Worker processes provide a useful failure boundary with little conceptual cost:

- a corrupted worker does not share an address space with its peers;
- no cross-thread object lifetime rules are required;
- no lock ordering exists;
- per-worker memory is independently bounded;
- crash behavior is observable by the master/init system.

This is defense in depth, not a substitute for memory-safety testing.

## Master responsibilities

The master SHOULD do only startup and lifecycle work:

1.  parse and validate CLI;
2.  validate kernel/runtime prerequisites;
3.  open/validate the document root;
4.  calculate resource requirements and inspect limits;
5.  create/configure/listen on the socket;
6.  establish signal policy;
7.  fork a fixed worker set;
8.  supervise worker exit;
9.  coordinate graceful shutdown;
10.  emit bounded lifecycle/fatal diagnostics.

The master SHALL NOT become a per-request dispatcher.

## Worker responsibilities

Each worker owns:

- one epoll instance;
- its partition of connection slots;
- its timer heap;
- request/response buffers contained in slots or fixed worker storage;
- accepted client sockets;
- representation FDs opened for those clients;
- the request state machine.

Workers SHALL NOT communicate request data with each other.

The worker-local runtime is isolated beneath the supervisor: `worker.c` owns epoll/admission/dispatch, `conn.c` owns accepted-socket and response-transfer state, and `timer.c` owns the fixed-capacity deadline heap. The master creates the listener and opens the filesystem root before forking. Workers inherit and borrow those process-local descriptor references at the worker API boundary; after all workers report ready, the master closes its listener/root copies and retains only lifecycle state.

## Connection-slot partitioning

The resolved maximum connection count means **that many total Cerv-managed client connections**, not that many per worker. A numeric `--max-connections N` / `CERV_MAX_CONNECTIONS=N` is a hard service-wide request. `auto` is the default: it starts from `CERV_DEFAULT_MAX_CONNECTIONS` (4096) and reduces that target when necessary so the largest worker partition fits the current soft `RLIMIT_NOFILE` under Cerv's conservative `2 * local_slots + CERV_FD_SAFETY_MARGIN` FD model.

Automatic sizing is a startup capacity decision only; it does not create dynamic slot growth. After resolution, the fixed slot total SHALL be partitioned deterministically across workers. If `N` does not divide evenly, the first `N mod workers` workers MAY receive one additional slot.

This avoids the common configuration trap where increasing worker count silently multiplies the process's total capacity and memory footprint.


## Process lifecycle

Startup is deliberately one-way:

1. parse immutable CLI configuration;
2. block lifecycle signals and create the master `signalfd`;
3. open/validate the document root;
4. derive largest-worker slot/memory/FD requirements and inspect `RLIMIT_NOFILE`;
5. create one nonblocking/CLOEXEC listening socket;
6. create one bounded startup-readiness pipe;
7. fork the fixed worker set;
8. each worker creates its own `signalfd`, allocates its fixed arena/timer storage, initializes epoll, and emits one fixed readiness record;
9. only after every worker is ready does the master close its listener/root/readiness descriptors and announce readiness.

There is no worker respawn loop. Unexpected steady-state worker death is a service-fatal event: the master drains/hard-stops peers as needed, reaps all children, and exits nonzero so the external service manager owns restart policy.
