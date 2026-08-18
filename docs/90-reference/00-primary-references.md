# Primary Reference Set

> **Status:** Reference  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Primary standards and documentation used by Cerv.  

The implementation and future revisions should prefer these primary sources over blog summaries.

## HTTP / URI

1.  **RFC 9110 — HTTP Semantics**, Internet Standard / STD 97, RFC Editor.
2.  **RFC 9111 — HTTP Caching**, HTTP core specification, RFC Editor.
3.  **RFC 9112 — HTTP/1.1**, Internet Standard / STD 99, RFC Editor. Note that later RFC updates to 9112 must also be tracked.
4.  **RFC 8246 — HTTP Immutable Responses**, `immutable` Cache-Control extension.
5.  **RFC 3986 — Uniform Resource Identifier (URI): Generic Syntax**.
6.  **RFC 2119 + RFC 8174 — BCP 14** normative requirement language.

Official source: RFC Editor (`rfc-editor.org`).

## Linux userspace API

Use current Linux man-pages and kernel documentation for:

- `openat2(2)`;
- `epoll(7)`;
- `epoll_ctl(2)` and `EPOLLEXCLUSIVE`;
- `accept(2)` / `accept4()`;
- `listen(2)`;
- `sendfile(2)`;
- `signalfd(2)`;
- `clock_gettime(2)`;
- `getrlimit(2)` / `RLIMIT_NOFILE`;
- `prctl(2)` / no-new-privileges where applicable;
- seccomp filter userspace API;
- Landlock userspace API.

Primary sources: Linux man-pages project (`man7.org`) and Linux kernel documentation (`docs.kernel.org`).

## C toolchains and dynamic analysis

Primary documentation:

- Clang AddressSanitizer documentation;
- Clang UndefinedBehaviorSanitizer documentation;
- LLVM libFuzzer documentation;
- Clang Static Analyzer / clang-tidy documentation;
- GCC warning options and `-fanalyzer` documentation.

## Bounded model checking

**CBMC / CProver documentation**. CBMC is a bounded model checker for C/C++ that can check properties such as array bounds, pointer safety, arithmetic exceptions, and user assertions after loop unwinding. Proof targets must use sound unwinding checks for the declared bounds.

## Supply chain

Primary references:

- SPDX specification/project documentation;
- SLSA specification (current stable version at release time);
- GitHub Actions security-hardening documentation when GitHub hosts CI;
- GitHub artifact-attestation documentation;
- Reproducible Builds project documentation, including SOURCE_DATE_EPOCH conventions.

## Coding guidance

SEI CERT C guidance MAY be used as supplemental secure-C review material. It does not override the ISO C standard, Linux ABI documentation, or the concrete Cerv coding rules in this specification.
