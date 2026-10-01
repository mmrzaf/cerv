# Time and Deadlines

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Monotonic time, connection deadlines, timer structure, and liveness caveats.  

## Clock source

All duration enforcement SHALL use `CLOCK_MONOTONIC` or a monotonic clock with equivalent semantics. Wall-clock time SHALL NOT be used for timeouts because NTP/administrator clock adjustments must not extend or prematurely expire a request.

Wall-clock time is still needed for HTTP `Date` and date validators.

## Header deadline

The first request on an accepted connection receives an absolute header deadline at acceptance:

```text
header_deadline = accept_monotonic_time + configured_header_timeout
```

If a successful HTTP/1.1 response is eligible for bounded connection reuse, the completed response transitions back to an empty receive state and installs the next request's deadline from that completion instant:

```text
next_header_deadline = prior_response_complete_monotonic_time + configured_header_timeout
```

When a header deadline expires, the response depends on whether a request had begun. If at least one byte of a request was received, the stalled request receives `408 Request Timeout` and the connection closes. If no byte arrived, as with a fresh connection that never spoke or a persistent connection that went idle between requests, Cerv closes it silently: a server may close an idle connection without a response (RFC 9112 section 9.5), and a `408` written to a connection the client believes is idle can be misread by that client as the answer to its next request.

Neither deadline resets when more header bytes arrive. The connection's independent absolute lifetime remains anchored at the original accept. This prevents a peer from retaining a slot indefinitely by trickling bytes while still allowing a bounded persistent backend connection to wait for its next sequential request.

## Write no-progress deadline

Once Cerv begins sending a response, it maintains a write no-progress deadline.

The deadline resets **only when response bytes make forward progress**, not merely because epoll reports writable or a syscall is attempted.

This bounds a slow/non-reading peer while allowing a legitimately large transfer to continue as long as it progresses.

## Maximum connection lifetime

Every connection also receives a hard monotonic maximum lifetime from acceptance. This deadline never resets.

It exists because a progress-reset write timeout alone could permit an extremely slow but continuously progressing transfer to occupy a slot indefinitely.

## Timer structure

Each worker SHALL use a fixed-capacity min-heap keyed by the next active deadline for each connection.

Requirements:

- capacity equals worker connection-slot capacity;
- no runtime allocation;
- each slot stores its current heap index or an explicit “not in heap” marker;
- insertion, update, and removal are O(log N);
- expiration is O(log N) per removed timer;
- heap invariants are unit-tested, fuzzed, and suitable for bounded model checking at reduced capacities;
- ordering ties are deterministic by slot index and generation so tests/proofs do not depend on incidental insertion order.

An O(N) full connection scan on every event-loop iteration is discouraged because it couples idle cost to configured capacity.

## epoll timeout calculation

Before `epoll_wait`, the worker computes the time until the earliest timer. Conversion to the epoll timeout unit SHALL use checked/saturating arithmetic and SHALL avoid rounding in a way that systematically lets deadlines run late.

If the earliest deadline has passed, the wait timeout is zero.

Expired timers SHALL be processed before and after event dispatch so a continuously busy event stream cannot indefinitely postpone timeout enforcement. The epoll millisecond timeout uses a ceiling conversion from the monotonic nanosecond deadline; an already-due timer produces a zero wait.

## Filesystem liveness caveat

A synchronous `openat2()`, metadata lookup, page fault, `pread()`, or `sendfile()` can block in the kernel under pathological storage conditions. Cerv cannot use epoll to impose a hard wall-clock deadline on arbitrary regular-file I/O.

Therefore the specification promises:

- hard bounds on userspace wait states that Cerv controls;
- process isolation so a blocked worker does not directly block all workers;
- deployment guidance favoring local, read-only, healthy filesystems;
- honest metrics/diagnostics around worker loss where possible;

but **not** a real-time guarantee over broken storage.

Moving regular-file I/O to asynchronous machinery solely to claim a stronger timer guarantee is a major architectural change and requires a separate specification/benchmark/security review.
