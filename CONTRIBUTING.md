# Contributing to Cerv

Cerv is deliberately small, bounded, and Linux-specific. Changes should preserve those properties before optimizing convenience or breadth.

## Branch and change flow

`develop` is the integration branch. Normal changes should be proposed against `develop`, kept focused, and merged only after the applicable verification gates pass. Release tags are created by a maintainer from a reviewed commit; automation validates and publishes tags but never creates or moves them.

Keep commits narrow enough that ownership, protocol, resource-bound, and security changes can be reviewed independently. Avoid mixing mechanical refactors with behavioral changes when they can be separated cleanly.

## Before sending a change

At minimum run:

```sh
make test
make sanitize
make analyze
make proof
make hardening-check
```

Changes to parsers, state machines, bounds, sandbox policy, release tooling, or deployment behavior should also run the focused fuzz, differential, syscall, reproducibility, or container checks described in `README.md` and `docs/40-engineering/02-verification-standard.md`.

`make verification-check` is the complete locally executable verification pipeline. `make release-check` additionally requires Docker or Podman for the container smoke test.

## Architecture rules

- Respect the dependency directions enforced by `tools/check_source_layers.sh`.
- Keep public headers self-contained.
- Keep attacker-controlled parsing bounded and allocation-free.
- Preserve explicit FD, buffer, timer, and connection ownership contracts.
- Do not add request-path heap allocation, hidden queues, blocking logging, or weaker filesystem confinement without an explicit contract change and evidence.
- Add regression coverage for every correctness or security fix. Add a persistent fuzz seed or proof case when the bug class fits those tools.

## Style

Use strict C17 and the repository warning profile. Prefer small named modules and direct Linux interfaces over framework abstractions. Comments should explain invariants, ownership, bounds, or non-obvious policy. Keep change narratives in Git commits and release notes.

Documentation under `docs/` is normative where marked. Update it in the same change whenever behavior, limits, deployment expectations, or release contracts change.
