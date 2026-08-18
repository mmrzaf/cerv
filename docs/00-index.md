# Cerv Documentation Index

> **Status:** Normative index  
> **Specification:** Cerv Engineering Specification (CES) 1.0

Cerv's normative documentation is organized by architectural authority. Numeric prefixes define reading order and leave room for future documents without renaming the tree.

## Authority

When documents overlap:

1. protocol standards and explicit requirements in `00-foundation/` take precedence;
2. the more specific domain document takes precedence over a general architectural description;
3. core invariants and bounded-resource requirements cannot be weakened by an implementation detail;
4. unresolved questions must not be represented as contradictory normative requirements.

## Documentation tree

```text
docs/
├── 00-index.md
├── 00-foundation/
│   ├── 00-overview.md
│   ├── 01-project-contract.md
│   ├── 02-standards-platform.md
│   ├── 03-threat-model.md
│   ├── 04-core-invariants.md
│   └── 05-engineering-doctrine.md
├── 10-architecture/
│   ├── 00-bounded-resource-model.md
│   ├── 01-runtime-architecture.md
│   ├── 02-network-event-loop.md
│   ├── 03-time-deadlines.md
│   ├── 04-overload-failure-shutdown.md
│   ├── 05-observability-diagnostics.md
│   └── 06-source-architecture.md
├── 20-http/
│   ├── 00-request-parser.md
│   ├── 01-request-target-path-policy.md
│   ├── 02-representation-media-types.md
│   ├── 03-validators-dates-caching.md
│   ├── 04-byte-ranges.md
│   └── 05-response-contract.md
├── 30-linux/
│   ├── 00-filesystem-confinement.md
│   ├── 01-defense-in-depth.md
│   ├── 02-syscall-vocabulary.md
│   └── 03-deployment-standard.md
├── 40-engineering/
│   ├── 00-c-coding-standard.md
│   ├── 01-build-toolchain.md
│   ├── 02-verification-standard.md
│   ├── 03-adversarial-test-standard.md
│   └── 04-performance-standard.md
├── 50-operations/
│   ├── 00-cli-configuration.md
│   ├── 01-bounds-registry.md
│   ├── 02-verification-contract.md
│   └── 03-container-runtime.md
├── 60-release/
│   └── 00-release-supply-chain.md
├── 70-governance/
│   └── 00-source-review-standard.md
└── 90-reference/
    ├── 00-primary-references.md
    └── 01-additional-references.md
```

## Reading order

A contributor SHOULD read, in order:

1. `00-foundation/00-overview.md`
2. all of `00-foundation/`
3. all of `10-architecture/`
4. the domain being changed (`20-http/` or `30-linux/`)
5. the applicable engineering standards in `40-engineering/`
6. `50-operations/02-verification-contract.md`

The reference directory is supporting material and is not itself a source of Cerv-specific normative behavior.
