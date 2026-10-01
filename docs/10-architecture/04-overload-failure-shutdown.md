# Overload, Failure, and Shutdown

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Admission, saturation, failure classes, fail-stop rules, supervision, and graceful shutdown.  

## No user-space admission queue

Cerv SHALL NOT accept a client into an unbounded or delayed application work queue.

A worker either has an immediately available fixed connection slot or it does not.

## Saturation response

When a worker has already accepted a connection but has no available slot (the single-worker/race fallback), Cerv SHOULD make one bounded best-effort attempt to send a minimal:

```text
HTTP/1.1 503 Service Unavailable
Retry-After: 1
Connection: close
Cache-Control: no-store
```

with an exact Content-Length and short body or no body according to the frozen response policy, then close.

The overload path MUST NOT allocate, register the socket into the normal connection arena, or block waiting for it to become writable.

If the immediate nonblocking send cannot complete, Cerv closes. Overload handling must not become a second queue.

## Overload accept budget

When local slots are exhausted, a multiworker worker deregisters the shared listener and does no further accepts until a slot is released. The single-worker overflow path remains bounded by the normal accept budget. This prevents a connection flood from consuming worker CPU and preserves already-admitted progress. The Linux accept backlog remains an external kernel-owned queue, not Cerv application state.

## Internal failure categories

Runtime failures SHALL be categorized at least as:

- client protocol error;
- normal resource absence;
- client/network disconnect;
- local saturation;
- local operational resource exhaustion;
- filesystem I/O failure;
- internal invariant failure;
- unexpected worker termination.

These categories determine whether Cerv returns an HTTP error, silently closes, emits a local diagnostic, or terminates.

## Fail-stop internal invariants

When Cerv detects an impossible internal state indicating memory/state corruption or a violated programmer invariant, it SHOULD fail-stop rather than attempt recovery inside the compromised worker.

Attacker-controlled invalid input MUST NEVER be handled by assertions; it receives a protocol result.

## Signal architecture

Traditional asynchronous signal handlers SHOULD be kept minimal or avoided for lifecycle logic.

The preferred model is:

1.  block lifecycle signals in the process/thread signal mask;
2.  consume them through `signalfd()` in the master's event loop;
3.  coordinate workers using explicit signals/lifecycle states.

Signals of interest include at least:

- SIGTERM;
- SIGINT;
- SIGHUP;
- SIGCHLD.

SIGPIPE is ignored globally as described earlier.

## Graceful shutdown sequence

On SIGTERM, SIGINT, or SIGHUP:

1.  master marks service draining;
2.  new admission is stopped;
3.  workers remove/disable listener interest and mark every admitted connection close-after-response;
4.  an in-flight response may finish, while an idle reusable connection may serve at most its next already-admitted request and then closes; persistent reuse cannot prolong drain;
5.  all remaining connection work stays subject to the existing per-request/write/absolute-lifetime deadlines;
6.  an absolute master shutdown deadline is established;
7.  when all workers drain, they exit cleanly;
8.  at deadline, remaining workers/connections are terminated according to the hard-stop policy;
9.  master reaps children and exits with the documented status.

Shutdown is idempotent. The first SIGTERM/SIGINT/SIGHUP starts graceful drain and an absolute monotonic deadline. A second such signal sends SIGKILL to all still-live workers immediately. Deadline expiry does the same. A requested shutdown exits status 0 after all children are reaped even when the hard-stop path was required; unexpected/internal failure exits nonzero.

## Hangup and ignored hangup

Cerv has no configuration reload, so SIGHUP has no better meaning than the terminal-hangup one: it requests the same graceful shutdown as SIGTERM. Without this the signal's default action would terminate the master abruptly and leave its workers serving. A process started with SIGHUP already ignored (`nohup`, daemon launchers) keeps ignoring it, and workers inherit that disposition, so a service deliberately detached from its terminal survives the terminal closing.

## Workers never outlive the master

Every worker asks the kernel to deliver SIGTERM when its parent dies (`PR_SET_PDEATHSIG`), and verifies after the request that its parent is still the master that forked it, which closes the window where the master dies between `fork()` and the `prctl`. A master that is killed with SIGKILL, or by the OOM killer, therefore cannot leave orphaned workers holding the listening socket. Each worker handles the death signal exactly like a requested shutdown: it stops accepting, lets in-flight responses finish within the existing write and lifetime deadlines, and exits with status 0. The request is made before the seccomp profile is installed.

## Unexpected worker death

The preferred baseline is **fail the service visibly** if a worker dies unexpectedly during steady-state operation.

The master SHOULD:

- record one bounded fatal diagnostic;
- initiate shutdown of peer workers;
- exit non-zero;
- allow systemd/container orchestration to restart the complete service.

Automatically respawning a worker forever can hide deterministic crash loops and create an apparently healthy but degraded server. If restart-in-master is ever added, it requires rate limits, crash-loop policy, and health semantics.

## Uninterruptible kernel sleep

A process stuck in Linux uninterruptible sleep due to a device/filesystem problem may not exit promptly even after SIGKILL until the kernel operation returns.

Cerv SHALL not promise otherwise. Host-level supervision must treat a shutdown deadline exceeded by kernel-blocked workers as an infrastructure fault.

## Master fail-cleanup invariant

Every supervisor return path owns child cleanup. If the master's own lifecycle poll/signalfd processing fails, it SHALL NOT return while workers remain serving: all known live children are sent SIGKILL, reaped, and only then may the master return nonzero. Startup failure follows the same no-orphan rule.
