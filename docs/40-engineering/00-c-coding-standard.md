# C Coding Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Language subset, strings, integers, arithmetic, ownership, initialization, syscalls, assertions, and comments.  

## Goal

Cerv C should be deliberately boring. The style exists to reduce the number of legal-looking ways to create lifetime, integer, and buffer bugs.

## Language subset

Baseline rules:

- C17;
- no C++;
- no variable-length arrays;
- no recursion in request/runtime paths;
- no `setjmp`/`longjmp`;
- no runtime dynamic loading (`dlopen`);
- no plugins;
- no `system()`, shell execution, or external command invocation;
- no compiler extensions in portable core code unless the extension is explicitly justified and both compilers support it;
- Linux UAPI/syscall usage is, by definition, platform-specific and isolated/documented.

## String discipline

Attacker-controlled protocol data is bytes plus length, never “a C string because it probably has a terminator.”

Ban or strongly discourage unbounded string functions in request code, including unsafe patterns around:

```text
strcpy
strcat
sprintf
gets
strlen(attacker_pointer_without_proven_bound)
```

Prefer explicit span types/arguments:

```text
const unsigned char *p
size_t len
```

If a NUL-terminated path must be produced for `openat2()`, it is copied/decoded into a fixed buffer only after bounds have been proven, and a terminator is written explicitly.

## Integer discipline

Use types according to domain:

- `size_t` for in-memory buffer lengths/indexes;
- `ssize_t` for syscall byte return values;
- `off_t` for file offsets/sizes when interfacing with file APIs;
- fixed-width unsigned integers for wire-independent encodings/generation counters where appropriate;
- `uint64_t` or a well-defined monotonic timestamp type for deadlines.

Every conversion across signed/unsigned domains or from filesystem sizes to buffer/index types SHALL be checked or structurally proven safe.

No implicit narrowing is accepted merely to silence compiler errors.

## Arithmetic helpers

Provide small checked helpers for:

- addition;
- subtraction;
- multiplication where needed;
- decimal accumulation;
- seconds/nanoseconds conversion;
- `off_t` range math;
- buffer `used + need <= capacity` checks.

The helpers SHOULD be unit-tested and suitable for formal/bounded analysis.

## No floating point in protocol parsing

HTTP quality values such as `q=0.875` SHALL be parsed into integer thousandths, not binary floating point.

This removes locale issues, rounding ambiguity, and unnecessary runtime machinery.

## Locale

Protocol parsing and formatting SHALL be locale-independent.

Do not use locale-sensitive character classification on arbitrary negative `char` values. ASCII token checks should be explicit tables/ranges on `unsigned char`.

Date formatting/parsing must force protocol English month/day names rather than trust process locale.

## File descriptor ownership

FD ownership SHALL be visible in data structures.

Recommended rule:

- unopened/invalid FD field is `-1`;
- the function/struct that owns the FD is responsible for exactly one close;
- transfers of ownership are explicit;
- connection cleanup is centralized and idempotent;
- close errors are handled according to documented policy but do not cause double-close retries.

FD reuse makes stale-close bugs especially dangerous, so cleanup code deserves dedicated tests.

## Initialization

Structures containing security-sensitive state SHALL be fully initialized before use. Prefer designated initializers/zero-initialization plus explicit invalid-FD sentinels where zero is a valid value.

Never depend on padding bytes or uninitialized fields for hashing/comparison/wire output.

## Macros

Macros SHALL not hide complex control flow or evaluate arguments multiple times.

Prefer `static inline` functions for checked helpers.

Compile-time constants and simple assertion wrappers are acceptable macro uses.

## Global state

Global mutable request state is prohibited.

Read-only compile-time tables are fine. Process lifecycle globals should be minimized and explicit. Worker request state belongs in worker/connection structs.

## Pointer arithmetic

Pointer arithmetic SHOULD be confined to narrow parsing/buffer helpers with explicit start/end bounds.

Prefer index/length calculations where they make overflow checking clearer.

## Syscall handling

Every syscall result SHALL be checked.

`EINTR`, `EAGAIN`, partial success, and Linux-specific errors are handled per syscall semantics rather than by a generic “retry everything” wrapper.

A single universal retry macro is discouraged because retry safety differs between calls and states.

## Assertions and invariants

Use an always-enabled project invariant mechanism (e.g. `CERV_INVARIANT`) for internal impossible states. Do not compile critical invariants away with `NDEBUG` unless another always-on mechanism replaces them.

Rules:

- attacker input -> normal error path;
- expected kernel error -> normal error path;
- impossible internal state -> invariant failure / fail-stop.

## Commenting style

Comments should document **why**, invariants, ownership, standards rationale, and non-obvious kernel behavior—not narrate obvious C syntax.

Security-sensitive code SHOULD cite the relevant RFC section/man-page concept in source comments without copying large standards text.
