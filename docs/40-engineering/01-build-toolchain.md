# Build and Toolchain Policy

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Build system, compiler matrix, warnings, optimization, hardening, sanitizer builds, and libc linking policy.  

## Build system

A plain Makefile is the preferred starting point.

Required developer targets SHOULD include:

```text
make
make test
make architecture-check
make sanitize
make analyze
make fuzz
make proof
make differential
make syscall-audit
make cross-aarch64
make bench
make release-check
```

The normal build SHALL NOT download dependencies or execute network access.

`make test` SHALL include the source architecture check. `make architecture-check` verifies that production internal includes are qualified and that directory dependencies obey the one-way layer policy defined by `docs/10-architecture/06-source-architecture.md`. This check is structural enforcement, not a substitute for header self-containment compilation.

## Compilers

Clang and GCC are both mandatory CI compilers.

One MAY be designated the release compiler, but the other provides independent diagnostics/code generation and catches assumptions that accidentally depend on a single compiler.

Toolchain versions for official releases SHALL be recorded exactly.

## Warning baseline

The baseline warning policy for both compilers is:

```text
-std=c17
-Wall
-Wextra
-Wpedantic
-Werror
-Wconversion
-Wsign-conversion
-Wshadow
-Wformat=2
-Wundef
-Wstrict-prototypes
-Wmissing-prototypes
-Wcast-qual
-Wwrite-strings
-Wvla
-Wswitch-enum
-fno-common
```

Warnings are part of the build gate. Suppressions SHALL be local, justified, and reviewed.

## Optimization

Release baseline:

```text
-O2
```

Cerv SHALL NOT use `-Ofast` because transformations that weaken language/IEEE semantics for benchmark gain are not aligned with the project.

LTO MAY be enabled only after:

- both supported architectures are tested;
- sanitizer/debug workflows remain practical;
- reproducibility is verified;
- performance benefit is material.

## Hardening flags

The baseline release hardening flags, subject to compiler/platform support checks, are:

```text
-fPIE
-fstack-protector-strong
-fstack-clash-protection
-D_FORTIFY_SOURCE=3
```

and linker options such as:

```text
-pie
-Wl,-z,relro
-Wl,-z,now
-Wl,-z,noexecstack
```

The project SHALL verify resulting ELF properties rather than assume flags were honored.

GCC's aggregate hardening options may evolve across versions; Cerv should prefer a reviewed explicit set in release policy so a compiler upgrade does not silently change the hardening contract.

## Debug/sanitizer builds

Sanitizer/debug builds SHOULD use sufficient debug information and frame-pointer settings for actionable traces. Optimization level should preserve bug discovery while staying representative (often `-O1` or `-O2` depending sanitizer guidance).

No sanitizer finding is “expected noise” in the release branch.


## Instrumented sandbox exception

ASan/UBSan/LSan and LLVM coverage runtimes execute implementation/runtime syscalls outside Cerv's reviewed service vocabulary. Instrumented verification builds therefore define `CERV_INSTRUMENTED_BUILD`, which preserves `no_new_privs` but omits active Landlock/seccomp installation in the tested process. This macro is verification-only and SHALL NOT appear in release compiler flags. Ordinary dual-compiler integration plus `make syscall-audit` exercise the exact production sandbox.
