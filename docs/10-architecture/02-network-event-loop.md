# Network and Event Loop

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Listener, epoll, accept behavior, readiness handling, fairness, and file transmission.  

## Listening socket

The master SHALL create the listening socket before forking workers. Listening addresses are numeric IPv4 or IPv6 literals; DNS resolution is intentionally outside Cerv.

Socket setup SHOULD include, where applicable:

- `SOCK_STREAM`;
- nonblocking/CLOEXEC policy established explicitly;
- `SO_REUSEADDR` where operationally useful and well understood;
- `bind()`;
- `listen()` with a configured/requested backlog.

Cerv uses one shared inherited listener rather than a `SO_REUSEPORT` listener set. With fixed per-worker slot partitions, reuseport flow hashing can strand completed connections on a locally full worker while another worker still has free service capacity. A shared listener preserves the service-wide `--max-connections` capacity model and keeps one admission point.

The production design therefore keeps one admission point and does not depend on reuseport flow hashing. There is exactly one listener implementation.

## Kernel backlog is an external queue

Cerv's “no user-space queue” invariant does not pretend TCP has no queueing. Linux maintains a completed-connection accept backlog. Cerv SHALL document this as an **external kernel-owned queue**.

The requested `listen()` backlog is capped by Linux's `net.core.somaxconn`. Startup diagnostics SHOULD report the requested backlog and SHOULD report the effective system cap when practical.

The backlog SHALL NOT be treated as an application work queue. Workers accept promptly into fixed slots or reject overload.

## epoll mode

The baseline SHALL use **level-triggered epoll**.

Reasons:

- its readiness semantics are closer to `poll()` and easier to audit;
- edge-triggered epoll requires draining resources to `EAGAIN` correctly on every path, increasing stall risk;
- Cerv can control fairness using explicit operation budgets rather than edge-trigger mechanics.

Every socket registered with epoll SHALL be nonblocking.

## Listener registration and wakeups

Each worker has its own epoll instance and observes the inherited listener with `EPOLLEXCLUSIVE`. Multiworker admission is cooperative:

- after a worker performs successful accept work for a listener event, it removes and re-adds listener interest before returning to epoll when it still has local capacity; this bounded yield gives peer epoll instances a chance to become the next exclusive waiter;
- once a worker's fixed local slot partition is full, it removes listener interest completely before another accept;
- when that worker releases a slot, it re-adds listener interest;
- shutdown removes listener interest permanently.

The yield costs at most one listener DEL/ADD pair per successful listener dispatch and does not create shared mutable request state. `EPOLLEXCLUSIVE` does not promise equal distribution, so correctness is defined as bounded work, no persistent local-full monopolization, usable service-wide slot capacity, and continued admitted-connection progress rather than perfect per-worker request counts.

Stress/integration evidence SHALL cover steady/burst admission, saturation across all worker partitions, worker exit, recovery when slots free, and absence of request starvation.

## accept loop

When the listener is readable, a worker SHALL call `accept4()` with:

```text
SOCK_NONBLOCK | SOCK_CLOEXEC
```

atomically applied to the accepted socket.

The worker SHALL accept until either:

- `EAGAIN`/`EWOULDBLOCK`;
- its **accept dispatch budget** is exhausted;
- shutdown has disabled new admissions.

For a single-worker runtime, when local slots are exhausted an already-accepted overflow socket may receive the bounded immediate 503 path described below. In the permanent multiworker topology, a worker that observes its local partition is full removes shared-listener interest **before** another accept so another worker with capacity can consume the shared backlog. If all workers are full, additional completed connections remain in Linux's bounded/external accept backlog until capacity returns or client/kernel timeout behavior resolves them. Cerv creates no user-space admission queue.

The baseline accept budget is 64 accept/reject attempts per listener dispatch.

Linux may surface certain pending network errors through `accept()`/`accept4()`. The implementation SHALL classify documented transient network errors appropriately rather than treating every non-EAGAIN error as a fatal server failure.

Local resource exhaustion surfaced by `accept4()` (`EMFILE`, `ENFILE`, `ENOBUFS`, `ENOMEM`) or by registering a new connection with epoll (`ENOSPC`, `ENOMEM`) is survivable. It is not a reason to terminate the worker and every in-flight response with it. The worker unregisters its listener, so a level-triggered readable backlog cannot spin the loop, and re-arms it after `CERV_ACCEPT_BACKOFF_MS`. Existing connections keep being served throughout, and completed connections wait in the kernel backlog. The same fixed interval applies to every occurrence; there is no unbounded or exponential state.

## Stale epoll event defense

Connection slots are reused. An epoll event that was queued for an old FD/slot lifetime must not be allowed to act on a newly reused slot.

Cerv SHOULD encode both:

- slot index;
- generation counter

into `epoll_event.data.u64` or an equivalent fixed-width token.

When dispatching an event, the generation in the token MUST match the current slot generation. A mismatch is stale and SHALL be ignored.

The baseline encoding is exact: the high 32 bits are the generation and the low 32 bits are the slot index. Generation zero is reserved for never-active slots. A generation increments on each acquisition; a slot is never acquired again after its generation reaches `UINT32_MAX`, so Cerv fails the worker/runtime invariant rather than wrapping a token into an old generation. Slot capacity is at most `UINT32_MAX`, with `UINT32_MAX` itself reserved as the free-list sentinel, so the all-ones listener token cannot collide with a live connection token.

## Fairness budget

No single ready object may cause unbounded work in one event-loop dispatch.

At minimum Cerv SHALL bound:

- accepts per listener dispatch;
- bytes sent from a large file per connection dispatch;
- parser work by fixed input size;
- error-response attempts during saturation.

The baseline file-send quantum is 1 MiB per readiness dispatch. In addition, one connection dispatch performs at most `CERV_SOCKET_IO_QUANTUM` (baseline 64) receive/send/pread/sendfile progress syscalls. If more remains, the socket stays registered for the state-appropriate readiness and the event loop proceeds.

This protects small requests from starvation behind a permanently writable large transfer.

## Header receive behavior

A connection in `RECV_HEADERS` reads into its fixed request buffer until one of these occurs:

1.  complete `CRLF CRLF` delimiter found;
2.  fixed buffer limit reached before completion;
3.  `recv()` returns EAGAIN;
4.  peer closes;
5.  monotonic header deadline expires;
6.  fatal socket error.

The implementation SHALL NOT grow the buffer.

It SHOULD detect header completion incrementally without rescanning the whole buffer after every read. A small delimiter-state field or scan offset is enough.

## SIGPIPE policy

Cerv SHALL ensure peer disconnects cannot terminate a worker through SIGPIPE.

Because `sendfile()` has no `MSG_NOSIGNAL` flag, Cerv ignores SIGPIPE process-wide before workers are forked/serve traffic. Worker initialization defensively enforces the same disposition, and ordinary `send()` calls also use `MSG_NOSIGNAL` for local clarity.

## File transmission

For a selected regular file, the primary path is `sendfile()` from file FD to socket FD.

The implementation MUST handle:

- partial successful transfers;
- `EAGAIN` / `EWOULDBLOCK`;
- `EINTR`;
- peer close/reset;
- file offsets larger than one syscall's permitted count;
- zero-byte files;
- range offsets;
- the Linux single-call maximum transfer count.

A single `sendfile()` call SHALL NOT request more than the configured dispatch quantum even if the file is larger.

## Fallback transfer

If `sendfile()` is unavailable for a particular descriptor combination or fails with a documented “unsupported” condition such as `EINVAL`/`ENOSYS` where fallback is safe, Cerv MAY use a bounded `pread()` + `send()` path.

The fallback SHALL reuse preallocated request/scratch storage. Cerv reuses the connection's fixed request byte array after request parsing and response planning have completed. It SHALL NOT allocate a transfer buffer dynamically.

The fallback state MUST preserve:

- current file offset;
- bytes valid in scratch buffer;
- bytes already sent from that scratch buffer;
- range end;
- write deadline semantics.

The fallback is a correctness path, not permission to hide arbitrary filesystem errors.

## Socket tuning

The baseline SHALL avoid speculative socket tuning such as custom send/receive buffer sizes, TCP_CORK, busy polling, or exotic TCP options.

Such options MAY be introduced only after production-like benchmark data shows a meaningful gain and failure/fairness tests show no regression.
