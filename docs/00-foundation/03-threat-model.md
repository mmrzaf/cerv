# Threat Model

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Adversary capabilities, security goals, availability goals, and protocol differential threats.  

## Adversary capabilities

Cerv SHALL assume an unauthenticated remote peer can:

- open connections at high rate;
- keep connections slow or partially written;
- send arbitrary octets within TCP;
- send malformed HTTP grammar;
- send framing combinations commonly associated with request smuggling;
- send duplicate or conflicting fields;
- target boundary lengths exactly and repeatedly;
- request large files while reading slowly;
- disconnect at any point;
- manipulate content negotiation and conditional fields;
- probe path traversal, percent encoding, slash ambiguity, backslash handling, symlink behavior, and race windows;
- attempt to create CPU unfairness or FD pressure through legal requests.

Cerv SHALL also assume that deployment mistakes are possible: writable document roots, unexpected symlinks, weak file permissions, constrained `RLIMIT_NOFILE`, slow log sinks, stale clocks, and incompatible reverse-proxy normalization.

## Out-of-scope adversaries

Cerv does not claim to protect against:

- a compromised kernel;
- hostile root on the same machine;
- hardware failure;
- arbitrary denial of service against the host/network beyond Cerv's process-owned bounds;
- malicious content intentionally placed inside the configured root by an authorized operator;
- semantic attacks against HTML/JS content itself;
- TLS-layer attacks when TLS is terminated elsewhere.

## Security goals

Cerv's security goals are:

1.  Never resolve a request outside the configured filesystem root through pathname semantics Cerv permits.
2.  Never traverse symbolic links while opening a served representation.
3.  Never interpret a request body or transfer coding in a way that can desynchronize Cerv from a conforming intermediary.
4.  Never require unbounded request-owned memory.
5.  Never accumulate an unbounded user-space work queue.
6.  Fail closed on malformed protocol and invalid configuration.
7.  Separate worker memory corruption/failure from other workers where practical through process isolation.
8.  Reduce post-startup kernel attack surface with defense-in-depth mechanisms after the core design stabilizes.

## Availability goals

Under saturation, Cerv SHALL prefer immediate deterministic rejection to hidden latency accumulation.

A slow peer MUST consume no more than one configured connection slot and its fixed per-connection memory. Slow writers and slow readers SHALL be disconnected by deadline policy. A large transfer SHALL NOT monopolize an event loop indefinitely; per-dispatch work SHALL be budgeted.

## Protocol differential threat

A major security concern for any custom HTTP/1 parser is differential interpretation between Cerv and intermediaries.

Therefore:

- standards-mandated grammar errors are rejected;
- message framing is handled before application semantics;
- body-capable framing is not “ignored”; it is explicitly rejected according to policy;
- normalization is minimized;
- request-target decoding occurs exactly once;
- no ambiguous rewrite layer exists inside Cerv.
