# Project Contract

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Mission, non-goals, normative language, and version philosophy.  

## Mission

Cerv SHALL be a static HTTP origin server for Linux whose primary design objectives, in order, are:

1.  protocol and file-serving correctness;
2.  deterministic rejection of ambiguous or unsupported input;
3.  explicit resource bounds and predictable overload behavior;
4.  operational stability under normal, hostile, slow-client, and saturation conditions;
5.  efficient use of Linux primitives without unnecessary framework or runtime machinery;
6.  auditable implementation and release process.

Performance matters, but a performance optimization SHALL NOT weaken a documented invariant without an explicit specification revision and evidence that the new design remains safe and bounded.

## Non-goals

The following are outside the core project unless a future specification deliberately reopens them:

- dynamic application execution;
- CGI, FastCGI, reverse proxying, templating, scripting, plugins, modules, or embedded interpreters;
- TLS implementation;
- HTTP/2 or HTTP/3 implementation;
- WebSockets or protocol upgrade;
- request-body processing;
- upload/write/delete operations;
- directory listings;
- implicit/unconfigured SPA routing or a general application router;
- arbitrary custom response headers;
- on-the-fly gzip/Brotli/zstd compression;
- configuration files or reloadable configuration;
- service daemonization, PID files, user switching, or init-system replacement;
- DNS resolution for the listening address in the baseline;
- cross-platform portability layers;
- portability to non-Linux Unix systems;
- support for filesystems with weak or surprising semantics as a guaranteed production target;
- “clever” optimizations whose value has not been demonstrated by benchmark and failure testing.

A non-goal MAY be revisited only when it materially improves the core mission rather than broadening Cerv into a general web server.

## Design test for proposed features

A proposed capability should answer at least one of the following convincingly:

- Does it improve correctness of serving files?
- Does it remove ambiguity between Cerv, clients, and intermediaries?
- Does it strengthen or clarify a bound?
- Does it improve liveness or overload behavior without making state harder to reason about?
- Does it exploit a Linux primitive that materially simplifies or strengthens the system?
- Does it improve verifiability, release trust, or operational diagnosis?

If none apply, the default answer is **no**.

## Normative language

The key words **MUST**, **MUST NOT**, **REQUIRED**, **SHALL**, **SHALL NOT**, **SHOULD**, **SHOULD NOT**, **RECOMMENDED**, **NOT RECOMMENDED**, **MAY**, and **OPTIONAL** are used in the BCP 14 sense when written in uppercase.

This document separates:

- **Protocol requirements** inherited from HTTP standards;
- **Cerv policy requirements**, which intentionally choose one allowed behavior where HTTP permits multiple behaviors;
- **implementation recommendations**, which can change without changing visible protocol behavior;

## Compatibility policy

Within the maintained 1.0.x line:

- observable protocol changes require changelog entries and compatibility analysis;
- bounds SHALL NOT silently increase;
- parser acceptance SHALL NOT silently broaden;
- security-sensitive filesystem or sandbox policy SHALL NOT silently weaken;
- security fixes MAY tighten rejection of invalid or ambiguous input when the change is documented;
- release provenance and verification gates remain part of the project contract.

A future major version MAY deliberately revise these contracts, but such changes must be explicit rather than accidental side effects of implementation work.
