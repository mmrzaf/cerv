# Performance Engineering Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Benchmark methodology, workload corpus, cache state, concurrency, metrics, tail behavior, and optimization policy.  

## Performance contract

Cerv is expected to be efficient, but it SHALL be optimized against measured bottlenecks rather than ideology.

The performance goal is:

High and stable useful throughput with low tail latency and predictable behavior from idle through saturation, without weakening correctness or bounds.

## Benchmarks are comparative

No single requests-per-second number is a meaningful project target across hardware and workloads.

Every optimization should compare:

- before vs after on identical hardware/kernel/filesystem;
- same compiler profile;
- same file corpus;
- same logging/edge setup;
- repeated runs with variance reported.

A mature static server such as Nginx MAY be included as an external control to calibrate expectations. Comparative benchmarks SHALL use equivalent workloads and deployment topology.

## Workload corpus

At minimum benchmark files of approximately:

```text
0 B
128 B
1 KiB
16 KiB
128 KiB
1 MiB
16 MiB
1 GiB
```

Include realistic HTML/CSS/JS/WASM/image/video artifacts and precompressed sidecars.

## Cache state

Separate:

- warm page-cache runs;
- deliberately colder/cache-disrupted runs where reproducible;
- metadata-heavy 404/sidecar-miss workloads.

Do not publish “disk performance” claims based only on page-cache hits.

## Concurrency sweep

Test from one client through saturation, e.g. logarithmic/convenient points:

```text
1, 8, 32, 128, 512, 1024, max, >max
```

Actual values depend on configured connection capacity and hardware.

## Response mix

Benchmark separately and in representative mixes:

- 200 small identity;
- 200 large identity;
- Brotli sidecar;
- gzip sidecar;
- 304;
- 206;
- 404;
- malformed 400;
- overload 503.

Error handling must not be pathologically more expensive than successful service.

## Metrics

Capture at least:

- throughput;
- p50/p95/p99/max latency;
- CPU utilization per worker and total;
- RSS/PSS where available;
- context switches;
- syscalls/request using `perf`/strace-style measurement in controlled runs;
- accepted connections/second;
- error/503 counts;
- file descriptor count;
- worker load balance;
- network bytes;
- fairness between small and large transfers.

## Tail behavior

Cerv's differentiator should be **shape under stress**, not just peak throughput.

Plots/reports should show what happens as offered load crosses capacity:

- Does p99 explode before 503s appear?
- Do existing clients starve?
- Does memory rise?
- Does CPU spin?
- Does recovery after load removal happen immediately?

A bounded server should transition from success to explicit rejection cleanly.

## Worker-count tuning

`--workers auto` is a convenience policy, not an assumption that “one worker per CPU” is always optimal.

Benchmarks SHALL sweep worker count for:

- small hot files;
- large sendfile workloads;
- mixed workload;
- slow-client pressure;
- filesystem stalls if they can be safely simulated.

The automatic default should follow data and may cap below CPU count on some systems.

## io_uring policy

`io_uring` is not baseline simply because it is newer.

A future io_uring design must demonstrate:

- material benefit on Cerv's real workloads;
- equally clear resource bounds;
- no significant increase in parser/filesystem security complexity;
- stable behavior across the declared kernel baseline;
- acceptable operational/debugging burden;
- a clean cancellation/shutdown story.

Until then, epoll + openat2 + sendfile is the standard architecture.
