# Engineering Doctrine

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Rules that keep implementation changes aligned with Cerv's architecture.

## Production-form rule

Production code SHALL use the architecture that Cerv intends to maintain. Temporary scaffolding MAY exist in tests or local evaluation tooling, but SHALL NOT distort production interfaces, ownership, state machines, or module boundaries.

A mechanism that is not justified by standards, Linux contracts, verification, or measurement SHALL remain outside the production implementation.

## Correctness before convenience

Implementation difficulty is not evidence against a design. A simpler implementation SHALL be preferred only when it preserves or strengthens correctness, boundedness, auditability, and standard behavior.

Cerv SHALL NOT choose a weaker filesystem primitive, parser rule, timeout model, state representation, or release check merely for implementation convenience.

## Standard before novel

Where a mature standard or Linux interface directly satisfies Cerv's requirements, Cerv SHOULD use it rather than inventing a private mechanism.

Novel mechanisms are justified only when they materially improve the project contract and can be tested at least as thoroughly as the standard alternative.

## Bounded before fast

Every hot-path structure and loop SHALL have a finite, documented bound before performance tuning begins.

Performance work MAY change representation, batching, syscall choice, or scheduling, but SHALL NOT make bounds implicit or weaken failure behavior.

## Explicit ownership

Every file descriptor, buffer, connection slot, timer entry, and process-owned resource SHALL have one explicit owner at every point in its lifetime.

Ownership transfer SHALL be visible in code. Hidden ownership through callback graphs, generic containers, shared mutable state, or implicit cleanup conventions is prohibited.

## Domain-driven source structure

Production source SHALL be organized by Cerv domain and invariant, not by generic abstractions.

The project SHALL avoid dumping grounds such as `util`, `common`, `helpers`, `misc`, or generic framework layers. Shared primitives are permitted only when they have a narrow, stable contract used by multiple domains.

Subdirectories SHOULD remain shallow and correspond to meaningful dependency or ownership boundaries.

## No obsolete internal compatibility

The implementation SHALL NOT retain obsolete internal APIs, adapters, duplicate mechanisms, or compatibility shims solely to preserve superseded internal designs.

When a mechanism is replaced, its production implementation and dead compatibility surface SHALL be removed unless a current public compatibility contract requires it.

## Linux deliberately, not theatrically

Cerv depends on Linux because Linux primitives strengthen or simplify the contract. A Linux-specific mechanism SHALL earn its place through correctness, boundedness, isolation, or measured efficiency.

Cerv SHALL NOT adopt advanced kernel mechanisms solely because they are newer or lower-level.

## Auditability is a performance feature

When two implementations perform similarly, the implementation with fewer states, clearer ownership, smaller syscall vocabulary, simpler error behavior, and stronger verification SHALL be preferred.

A micro-optimization that materially increases proof, fuzzing, or maintenance difficulty requires strong benchmark evidence and documented justification.

## Evidence hierarchy

Design decisions SHOULD be justified in this order:

1. protocol or language standard requirement;
2. documented Linux/kernel contract;
3. proved or exhaustively tested invariant;
4. reproducible measurement;
5. reasoned engineering judgment.

Convention, fashion, framework popularity, and generated-code convenience are not sufficient evidence.
