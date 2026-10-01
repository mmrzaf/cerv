# Verification Contract

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Protocol, filesystem, bounds, liveness, C safety, operations, release trust, and performance acceptance criteria.

A Cerv release is acceptable only when the applicable requirements below are satisfied or an environment-dependent omission is reported explicitly rather than represented as success.

## Protocol

- GET/HEAD behavior is reviewed against RFC 9110/9112.
- Host and absolute-form handling is conformant.
- Message framing has no body/Transfer-Encoding ambiguity.
- Parser limits have exact boundary tests.
- Conditional precedence is correct.
- The required HTTP-date forms are accepted.
- Accept-Encoding identity semantics are correct.
- Single-range behavior and unsupported multi-range behavior are frozen against RFC 9110.
- Every emitted status/header has a documented state.

## Filesystem

- `openat2()` confinement is mandatory and tested.
- Symlinks cannot be traversed.
- Only regular files are served.
- Descriptor-derived metadata is used consistently.
- Root-escape adversarial tests pass under concurrent mutations feasible in CI.

## Bounds

- Memory bounds are measured and documented per architecture.
- No request-time Cerv allocator calls occur.
- Total connection cap is enforced.
- FD budget is validated at startup.
- Parser work is bounded by fixed input limits.
- Timer heap capacity cannot exceed slots.
- File-send and accept dispatch budgets are enforced.
- Overload does not grow memory or user-space queue state.

## Liveness

- Header, write no-progress, absolute lifetime, startup, and shutdown deadlines work at boundaries.
- Existing connections retain progress under overload.
- Large transfers do not starve small transfers beyond documented scheduler behavior.
- Service capacity returns immediately after saturation is removed.
- Kernel/filesystem liveness caveats are documented without misleading hard guarantees.

## C safety

- Clang and GCC are warning-clean under required flags.
- ASan/UBSan suites are clean.
- Leak checks are clean.
- Required static-analyzer findings are resolved.
- Fuzz targets have sustained clean runs and persistent regression corpora.
- Required CBMC properties pass with unwinding assertions when CBMC is part of the publication profile.

## Operations

- SIGTERM/SIGINT/SIGHUP drain behavior, ignored-SIGHUP semantics, and worker exit on master death are documented and tested.
- Worker crash behavior is documented and tested.
- Stress tests leave no zombies or FD leaks.
- Foreground systemd/container deployments are documented.
- Reverse-proxy integration tests exist.
- Environment and CLI configuration precedence is tested through the real executable.
- Automatic connection sizing is tested against constrained `RLIMIT_NOFILE` values.

## Release trust

- Release source/tag is immutable according to repository policy.
- SHA-256 checksums are published.
- SPDX SBOM is published.
- Hosted provenance/attestation is published for release artifacts.
- CI third-party actions are pinned immutably.
- Release artifacts are built in a controlled environment.
- Reproducibility status is measured and documented.
- `SECURITY.md` defines the private-reporting expectations.

## Performance

- Benchmark methodology is published and repeatable.
- No unexplained p99 or saturation pathology is accepted.
- Throughput, CPU, and memory remain operationally useful for the intended static-origin role.
- Performance changes preserve correctness tests, security boundaries, and resource bounds.
