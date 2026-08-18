# Observability and Diagnostics

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Bounded diagnostics, logging policy, metrics philosophy, hygiene, and exit codes.  

## Principle

Observability must not be allowed to own request liveness.

A server that is correct until its stderr/logging pipeline slows down is not predictably bounded. Therefore Cerv's core v1 design intentionally avoids synchronous per-request access logging.

## Core diagnostics

The core SHALL emit only bounded operational records for events such as:

- startup configuration summary;
- kernel/platform validation failure;
- bind/listen failure;
- resource-limit validation failure;
- worker unexpected exit;
- system-wide fatal I/O/resource condition where diagnosis is needed;
- graceful shutdown start/end;
- version/build identity when requested.

Each record SHALL have a maximum size and be constructed in fixed storage.

## Access logs

Per-request access logs are **not a baseline core feature**.

Recommended production architecture places Cerv behind a reverse proxy/CDN that already owns edge access logs and can buffer/export them independently.

If access logging is later added, it must answer:

- What is its bounded memory footprint?
- What happens when the consumer blocks?
- Are records dropped or sampled?
- How are dropped records counted without a new unbounded channel?
- Can the log path ever stall a request worker?

A feature that cannot answer these is rejected.

## Metrics

Cerv does not initially expose a Prometheus/HTTP metrics endpoint because that adds another HTTP resource class and observability subsystem.

Operators can observe:

- proxy metrics;
- process RSS/CPU;
- cgroup counters;
- file-descriptor count;
- process/worker health;
- host network metrics.

A future bounded stats mechanism (for example signal-triggered summary to stderr or a fixed local control FD) may be considered, but it is not required to serve files correctly.

## Diagnostic data hygiene

Diagnostics SHOULD avoid logging attacker-controlled values by default. If a future debug mode includes them, values MUST be length-limited and escaped so raw control bytes cannot forge log records or terminal sequences.

## Exit codes

Cerv SHALL define stable broad exit-code classes, e.g.:

- 0: clean requested shutdown / version/help success;
- 2: CLI/configuration error;
- 1 or another documented code: startup/runtime fatal failure.

Do not create dozens of externally significant exit codes unless operations demonstrate a need.

## Diagnostic wire policy

`CERV_DIAG_BYTES_MAX` is 512 bytes including fixed record storage. Diagnostics are constructed with fixed stack buffers and contain only bounded internal lifecycle/resource details. At startup Cerv attempts to open a separate `O_NONBLOCK|O_CLOEXEC` descriptor referring to stderr. The diagnostic channel is enabled only for a non-regular sink; regular-file stderr is deliberately treated as unavailable because `O_NONBLOCK` does not make regular-file filesystem I/O nonblocking. Each record receives at most one `write()` attempt; partial output, `EINTR`, `EAGAIN`, unavailable sinks, and other failures are dropped with no retry or serving/lifecycle dependency.

Workers do not emit per-request records. The master emits startup/backlog/ready/shutdown records and unexpected-worker/resource/fatal diagnostics. There is no access-log queue, logger thread, or hidden request-to-master channel.
