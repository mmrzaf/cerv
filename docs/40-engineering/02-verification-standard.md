# Verification Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Sanitizers, static analysis, fuzzing, corpus policy, bounded model checking, proof boundaries, and coverage.  

## Verification layers

No single tool is treated as proof of correctness. Cerv uses overlapping layers:

1.  compiler warnings;
2.  unit tests;
3.  integration/protocol tests;
4.  AddressSanitizer + UndefinedBehaviorSanitizer;
5.  leak checking where applicable;
6.  static analyzers;
7.  coverage-guided fuzzing;
8.  bounded model checking of suitable helpers;
9.  stress/load/soak testing;
10.  manual standards/security review.

## AddressSanitizer

ASan builds SHALL run unit/integration suites and fuzz targets regularly. It is expected to catch classes such as out-of-bounds access and use-after-free.

Any ASan finding blocks release.

## UndefinedBehaviorSanitizer

UBSan SHALL be used to catch undefined behavior including arithmetic/shift/alignment categories enabled by the chosen profile.

The sanitizer configuration SHALL be recorded and kept stable enough that CI meaningfully compares revisions.

## Leak checking

Because steady-state Cerv intentionally does not allocate per request, leak behavior should be simple. LeakSanitizer or the ASan leak mode SHOULD run where supported, especially across startup/shutdown and error paths.

## MemorySanitizer

MSan MAY run in targeted/scheduled CI because uninitialized-read detection is highly valuable but requires compatible instrumented dependencies/runtime to avoid noisy false signals.

It is not a substitute for initializing all project structures explicitly.

## ThreadSanitizer

TSan is not a baseline gate while workers are single-threaded and the master does not share mutable memory with them after fork. If threads are ever introduced, TSan becomes mandatory and the architecture requires a fresh concurrency review.

## Static analysis

At least these SHOULD be used:

- Clang Static Analyzer;
- selected clang-tidy checks (bugprone, analyzer, cert-style correctness where applicable);
- GCC `-fanalyzer` on a dedicated analysis build.

Static analyzer findings are triaged individually. Suppression requires a source-level explanation where feasible, not a blanket project disable.

## Fuzzing philosophy

Anything that maps attacker bytes into structured state should be callable without a network socket.

Fuzz targets SHOULD include:

- complete HTTP header parser;
- request-line parser;
- request-target/percent decoder;
- Host/authority parser;
- Content-Length parser;
- Accept-Encoding parser;
- ETag list parser;
- HTTP-date parser;
- Range parser;
- response-header builder with structured randomized metadata.

Each fuzzer must have a small invariant oracle beyond “doesn't crash” when possible.

Examples:

- parser success implies all stored spans are within input bounds;
- decode success implies output is NUL-free and within destination capacity;
- range success implies `0 <= start <= end < length` for non-empty selected ranges;
- response build success implies length <= fixed capacity and ends in exactly one header terminator.

## Fuzz corpus

Seed corpora SHALL include valid ordinary requests and deliberately pathological examples from HTTP request-smuggling research patterns, RFC edge cases, and previously fixed Cerv regressions.

Every externally discovered parser bug SHOULD become a permanent regression corpus entry.

## Fuzzer resource policy

CI fuzzing has a bounded time budget; scheduled/nightly fuzzing can run longer. Published releases SHOULD receive extended fuzz runs on every supported architecture where practical.

Fuzzing is never described as “exhaustive.”

## Bounded model checking with CBMC

Cerv's bounded philosophy makes several pure helpers unusually suitable for CBMC.

Primary bounded-model-checking targets include:

- percent decoder/path segment validator;
- decimal integer parser;
- qvalue parser;
- range normalization;
- response-buffer append helpers;
- timer heap operations at bounded capacity;
- slot freelist operations;
- generation-token packing/unpacking.

Properties SHOULD include:

- no out-of-bounds read/write;
- no pointer invalidity;
- no signed overflow/undefined shift;
- asserted output invariants;
- no integer conversion violation under modeled inputs;
- correct loop unwinding.

CBMC verification SHALL enable unwinding assertions (or an equivalently sound bounded strategy) so “proof” does not silently ignore executions beyond the chosen unwind bound.

## Formal proof boundaries

The project SHALL describe CBMC results precisely:

“These properties hold for this function under the modeled assumptions and proven unwind bounds.”

Do not claim whole-server formal verification when syscalls, kernel behavior, and unmodeled C library code are outside the model.

## Coverage

Line/branch coverage is useful for finding untested code but SHALL NOT be used as a correctness metric by itself.

More important is semantic coverage of:

- each parser result;
- each state transition;
- each error classification;
- each boundary condition;
- each timeout type;
- each supported status;
- each representation selection combination.

## Architecture and differential gates

The verification suite includes a stateful fixed-arena fuzzer, direct production connection-state/deadline bounded checks, an nginx exact-byte framing differential, a ptrace post-seccomp syscall-vocabulary audit, and AArch64 Linux-UAPI compilation of the exact architecture-specific seccomp translation unit. `make cross-aarch64` additionally cross-links the complete production tree when an AArch64 glibc cross-compiler exists; `make cross-aarch64-required` makes that external toolchain mandatory. Environment/tool absence SHALL be reported as a skip, never represented as a successful full cross-runtime test.
