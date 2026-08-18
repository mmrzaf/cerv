# Additional References

> **Status:** Reference  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Supporting primary links for HTTP, Linux, verification, and supply-chain work.  

These links are included so implementation work can jump directly to primary material. They are not substitutes for reading the exact normative section relevant to a change.

## HTTP and URI

- RFC 9110 — https://www.rfc-editor.org/rfc/rfc9110
- RFC 9111 — https://www.rfc-editor.org/rfc/rfc9111
- RFC 9112 — https://www.rfc-editor.org/rfc/rfc9112
- RFC 8246 — https://www.rfc-editor.org/rfc/rfc8246
- RFC 3986 — https://www.rfc-editor.org/rfc/rfc3986
- RFC 2119 — https://www.rfc-editor.org/rfc/rfc2119
- RFC 8174 — https://www.rfc-editor.org/rfc/rfc8174

## Linux man-pages and kernel API

- openat2(2) — https://man7.org/linux/man-pages/man2/openat2.2.html
- epoll(7) — https://man7.org/linux/man-pages/man7/epoll.7.html
- epoll_ctl(2) — https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
- accept(2) / accept4() — https://man7.org/linux/man-pages/man2/accept.2.html
- listen(2) — https://man7.org/linux/man-pages/man2/listen.2.html
- sendfile(2) — https://man7.org/linux/man-pages/man2/sendfile.2.html
- signalfd(2) — https://man7.org/linux/man-pages/man2/signalfd.2.html
- clock_gettime(2) — https://man7.org/linux/man-pages/man2/clock_gettime.2.html
- getrlimit(2) — https://man7.org/linux/man-pages/man2/getrlimit.2.html
- no_new_privs — https://docs.kernel.org/userspace-api/no_new_privs.html
- seccomp filter — https://docs.kernel.org/userspace-api/seccomp_filter.html
- Landlock userspace API — https://docs.kernel.org/userspace-api/landlock.html

## C verification toolchain

- Clang AddressSanitizer — https://clang.llvm.org/docs/AddressSanitizer.html
- Clang UndefinedBehaviorSanitizer — https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
- LLVM libFuzzer — https://llvm.org/docs/LibFuzzer.html
- Clang Static Analyzer — https://clang-analyzer.llvm.org/
- clang-tidy — https://clang.llvm.org/extra/clang-tidy/
- GCC documentation — https://gcc.gnu.org/onlinedocs/
- CBMC / CProver — https://diffblue.github.io/cbmc/cprover_documentation.html

## Supply chain and reproducibility

- SPDX — https://spdx.dev/
- SLSA specification — https://slsa.dev/spec/
- GitHub secure use of Actions — https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions
- GitHub artifact attestations — https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations
- Reproducible Builds — https://reproducible-builds.org/
- SOURCE_DATE_EPOCH — https://reproducible-builds.org/docs/source-date-epoch/
