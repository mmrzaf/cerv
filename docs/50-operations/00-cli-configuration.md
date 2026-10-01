# CLI and Environment Configuration Contract

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Command-line/environment surface, precedence, root/listen semantics, durations, invalid input, and no-reload policy.

## Principle

Configuration is another input language and state-space multiplier. Cerv therefore keeps it intentionally small and validates it completely before serving.

Cerv supports exactly two startup configuration sources in the 1.0 contract:

1. process environment variables with the `CERV_` prefix;
2. command-line arguments.

There is no configuration-file parser and no in-process reload mechanism.

Precedence is:

```text
compiled defaults < environment < command line
```

The environment is read once during startup. Workers receive immutable resolved configuration and never consult environment variables on the request path.

## CLI surface

```text
cerv [OPTIONS] [ROOT]

--listen ADDRESS:PORT
--workers N|auto
--max-connections N|auto
--header-timeout DURATION
--write-timeout DURATION
--max-lifetime DURATION
--shutdown-timeout DURATION
--spa-fallback PATH
--landlock auto|require
--immutable
--mutable
-h, --help
-V, --version
```

Unknown or duplicate singleton options are errors. `--mutable` and `--immutable` are mutually exclusive singleton cache-policy overrides.

## Environment surface

```text
CERV_LISTEN
CERV_WORKERS
CERV_MAX_CONNECTIONS
CERV_HEADER_TIMEOUT
CERV_WRITE_TIMEOUT
CERV_MAX_LIFETIME
CERV_SHUTDOWN_TIMEOUT
CERV_ROOT
CERV_IMMUTABLE
CERV_SPA_FALLBACK
CERV_LANDLOCK
```

`CERV_IMMUTABLE` accepts `1/0`, `true/false`, `yes/no`, or `on/off` case-insensitively. An empty `CERV_SPA_FALLBACK` disables fallback; other present values must be valid root-relative fallback paths.

A command-line option/ROOT overrides the corresponding environment value. This makes container images usable without a shell entrypoint while preserving explicit per-invocation overrides.

`--help` and `--version` are available even when unrelated environment configuration is invalid.

## ROOT

ROOT is required after precedence resolution: either positional `ROOT` or `CERV_ROOT` must be present. A positional ROOT overrides `CERV_ROOT`.

Cerv rejects an empty root, non-directory root, inaccessible root, or root that cannot be safely opened under the startup policy.

After opening the root FD, Cerv does not depend on the textual root path during request service.

## Listen address

A listening address is required after precedence resolution: `--listen` or `CERV_LISTEN`.

Cerv accepts explicit numeric IPv4/IPv6 address plus port with unambiguous syntax. It does not guess loopback vs all interfaces and does not resolve DNS names/service-name ports in the baseline.

IPv6 literals use bracket syntax such as `[::1]:8080`.

## Duration syntax

Durations use one small grammar: positive integer plus suffix.

```text
500ms
5s
30s
5m
```

No floating point and no compound durations. Parsing uses checked integer arithmetic and rejects overflow/zero where semantically invalid.

## Worker count semantics

`--workers N` / `CERV_WORKERS=N` requires `1 <= N <= CERV_WORKERS_MAX` and, for an explicit numeric connection limit, `N <= max-connections`.

`auto` is deterministic Linux-derived configuration:

1. count CPUs in the process `sched_getaffinity()` mask, dynamically enlarging the mask when required up to `CERV_AFFINITY_CPU_PROBE_MAX`;
2. clamp to `CERV_WORKERS_MAX`;
3. when unified cgroup-v2 membership is visible, inspect finite `cpu.max` quotas from the current cgroup toward the root;
4. convert each finite quota to `ceil(quota / period)` workers and keep the minimum count;
5. never resolve below one worker.

Readable-but-malformed quota metadata is a startup error rather than a guess.

## Connection-cap semantics

`--max-connections N` / `CERV_MAX_CONNECTIONS=N` is a hard service-wide slot count. Startup refuses it when the largest worker partition's conservative FD requirement exceeds the process soft `RLIMIT_NOFILE`.

`--max-connections auto` / `CERV_MAX_CONNECTIONS=auto` is the default. It begins with the `CERV_DEFAULT_MAX_CONNECTIONS` service-wide target (4096), then caps the service-wide total so the largest worker partition satisfies:

```text
required_worker_fds = 2 * worker_slots + CERV_FD_SAFETY_MARGIN
required_worker_fds <= RLIMIT_NOFILE.soft
```

The result is never above 4096, never below the resolved worker count, and is still partitioned deterministically across workers. Automatic sizing does not change the compile-time/request-state memory bound per slot; it only chooses how many preallocated slots the service may start with under the current FD limit.

This is intentionally startup adaptation, not runtime growth.

## SPA fallback

`--spa-fallback PATH` / `CERV_SPA_FALLBACK=PATH` enables one bounded static fallback for client-side routed applications. PATH is a root-relative file path; an optional leading `/` is ignored for filesystem resolution. Empty segments, `.`/`..`, backslash, query/fragment syntax, control bytes, trailing-directory syntax, and hidden (dot-prefixed) segments are rejected.

Fallback occurs only when the originally requested representation resolves to `CERV_REPRESENTATION_NOT_FOUND` **and** the request names a client-side route, meaning its final path segment has no `.` or it ends in `/`. Requests for missing files with an extension, such as `/app.js` or `/logo.png`, remain `404`. It does **not** replace forbidden, not-acceptable, FD-exhaustion, I/O, malformed-request, or unsupported-confinement outcomes.

The fallback is selected through the ordinary representation negotiator, so precompressed `.br`/`.gz` sidecars, validators, ranges, MIME type, HEAD semantics, and cache policy all apply to the fallback file exactly as they do to an ordinary request.

This feature is deliberately narrower than application routing: there is one configured static fallback and no route table, rewrite language, regex, or request-body semantics.

## Landlock policy

`--landlock auto|require` / `CERV_LANDLOCK=auto|require` selects what happens when the running kernel cannot provide Landlock (kernel older than 5.13, Landlock absent from the active LSM list, or a container policy that blocks the syscall).

- `auto` is the default. Workers use Landlock when available, report `landlock=unavailable` in the startup `sandbox` diagnostic at `warn` level when it is not, and serve anyway. Mandatory `openat2()` confinement, `no_new_privs`, and seccomp are unaffected.
- `require` makes Landlock part of the security contract for this deployment. A worker that cannot establish its read-only-root ruleset exits before reporting ready, so the service fails startup with a diagnostic naming the requirement instead of serving with one fewer containment layer. A Landlock setup error on a kernel that does report support is fatal in both modes.

Operators who control their kernels should prefer `require`: without Landlock, a compromised worker is limited only by seccomp, whose filter cannot constrain the path argument of `openat2()`.

## Cache-policy override

Mutable/conservative cache behavior is the default. `--immutable` or a true `CERV_IMMUTABLE` is an operator assertion about the complete served tree. `--mutable` explicitly forces the conservative policy even when `CERV_IMMUTABLE` is true.

Do not enable immutable mode merely because an application uses hashed assets if `index.html` or other deployment entrypoints are expected to change in place under the same URL.

## Unknown arguments and invalid environment

Unknown CLI options are fatal configuration errors and exit with code 2. Cerv never silently ignores a misspelled flag.

A malformed environment value that is actually selected by precedence is also a fatal configuration error. An invalid environment value is ignored only when a corresponding command-line value explicitly overrides it.

## No reload

Cerv does not reload configuration in place. SIGHUP is not a reload signal; it requests the same graceful shutdown as SIGTERM, unless it was ignored when the process started. Configuration changes use a process restart/rolling deployment. This removes signal/config concurrency and keeps every worker's settings immutable during its lifetime.

## Startup input bound class

CLI/environment strings are operator-controlled, NUL-terminated process startup input rather than attacker-controlled HTTP request input. Numeric conversion and Cerv-owned destinations remain checked/fixed-size. SPA fallback is additionally copied into fixed path storage before service. The borrowed root-path pointer is used only during startup root opening.
