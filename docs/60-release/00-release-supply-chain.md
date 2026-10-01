# Release and Supply-Chain Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Artifacts, SBOM, provenance, CI policy, reproducibility, publication gates, and security response.

## Release principle

A production server is not complete when source code compiles. The release process must let an operator answer:

- What source revision produced this binary?
- What toolchain and flags were used?
- What tests and analysis passed?
- What dependencies/runtime components are present?
- Has the artifact been altered?
- Can the build be reproduced?

## Release artifacts

A published release SHOULD provide, per supported architecture/profile:

- executable artifact;
- SHA-256 checksum manifest;
- signed artifact attestation material from the hosted release process;
- SPDX SBOM;
- build provenance;
- separate debug symbols or a documented symbol package;
- source archive derived from the release revision;
- changelog/release notes;
- verification instructions.

Container images MAY be provided, but the standalone binary remains a first-class artifact. When a container image is published, its immutable digest SHALL be recorded and the image SHOULD include registry-native provenance/SBOM attestations.

## SBOM

SPDX is the preferred SBOM format.

The SBOM SHOULD identify at least:

- Cerv version/source commit when available;
- libc/runtime relationship of the artifact;
- compiler/toolchain where representable;
- bundled licenses/components;
- container base/components when a container is shipped.

Even a low-dependency C program benefits from explicit evidence that it really is low-dependency.

## Provenance and attestations

Hosted release automation SHOULD generate signed build provenance using the repository platform's OIDC-backed attestation mechanism. Local deterministic bundles MAY contain unsigned provenance for inspection, but they SHALL NOT be presented as equivalent to a hosted signed attestation.

Cerv SHALL claim only the provenance properties actually achieved.

## GitHub Actions policy

When GitHub Actions is used:

- workflows use least `GITHUB_TOKEN` permissions;
- third-party actions are pinned to full commit SHAs and updated only by deliberate maintainer change;
- publication jobs run only for repository-controlled release tags or explicit maintainer dispatch;
- untrusted pull-request code never receives publication credentials;
- release files receive GitHub artifact attestations;
- published container images receive registry-linked attestations;
- production publication uses protected repository/tag/environment policy where available.


## Hosted GitHub publication

The checked-in release workflow is triggered only by a `v`-prefixed Git tag. The tag's base version MUST match the repository `VERSION` file. The workflow validates and publishes the tag; it SHALL NOT create, rewrite, or move tags.

The hosted publication profile produces all local release-bundle artifacts plus:

- a Docker-loadable Linux amd64 image archive attached to the GitHub Release;
- a Linux amd64/arm64 image pushed to GHCR;
- the immutable GHCR image digest;
- a release-level checksum manifest covering the distributable archives and digest record;
- GitHub artifact attestations for release files;
- a registry-linked attestation for the GHCR image.

Tags with a SemVer suffix are published as prereleases and SHALL NOT update stable GHCR aliases. An exact stable product-version tag MAY additionally publish `major.minor.patch`, `major.minor`, `major`, and `latest` aliases that all resolve to the same attested image digest.

The workflow's third-party Actions SHALL be pinned to immutable full commit SHAs. Pins change only through reviewed maintainer commits.

## Reproducible builds

Declared source/build inputs SHOULD produce bit-for-bit identical artifacts.

Controls include:

- declared toolchain versions;
- stable build environment;
- `SOURCE_DATE_EPOCH` or equivalent source-derived timestamps;
- normalized archive ordering/metadata;
- deterministic build IDs or no build IDs;
- removal/remapping of build-host absolute paths;
- fixed locale and timezone.

Reproducibility SHALL be tested with independent clean builds rather than assumed.

## Publication validation

The exact artifact intended for publication—not a locally rebuilt approximation—SHALL be the artifact used for staging/canary checks. The applicable profile SHOULD include:

- extended fuzzing;
- sanitizer/integration suites;
- load and saturation tests;
- shutdown tests;
- reverse-proxy interoperability;
- filesystem adversarial tests;
- soak testing.

Any fix made after those checks requires the affected validation to run again on the new exact artifact.

## Release gates

Publication is blocked by:

- compiler warning;
- sanitizer finding;
- unresolved high-confidence static-analyzer finding;
- fuzz crash/hang not proven external;
- required proof failure;
- failing protocol conformance test;
- memory/FD bound regression without explicit contract change;
- unexplained performance-tail regression;
- missing provenance/SBOM/checksums required by the publication profile;
- reproducibility failure when reproducibility is required.

## Security response

`SECURITY.md` SHALL describe a private vulnerability-reporting route or repository-host private reporting mechanism.

Security fixes SHALL include regression tests and, when applicable, fuzz corpus seeds or proof updates.
