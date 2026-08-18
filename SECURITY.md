# Security Policy

## Supported versions

The current Cerv 1.0.x line receives security fixes. Users should run the newest available 1.0.x release.

## Reporting a vulnerability

Please report suspected vulnerabilities **privately** through the repository host's private security-reporting/advisory feature when available. Do not open a public issue containing exploit details, private filesystem paths, crash artifacts with secrets, or a working proof of concept before coordination.

If the repository does not expose a private reporting route, contact the maintainer through a private channel published by the repository owner before disclosing exploit details publicly. Cerv does not invent a reporting address or signing identity that the maintainer does not control.

A useful report includes the Cerv version, architecture/kernel/libc, relevant configuration, the smallest reproducer you can provide safely, and whether the issue crosses the documented HTTP, filesystem, process, or sandbox boundary.

## Fix expectations

Security fixes should include a regression test and, where applicable, a persistent fuzz seed or proof update. Release notes should describe impact and upgrade guidance without publishing unnecessary exploit detail before users have a reasonable opportunity to update.

## Security model boundaries

The normative security contracts are under `docs/`. Cerv deliberately depends on Linux `openat2()` confinement and mandatory seccomp/no-new-privs hardening. Landlock is defense in depth and is not a substitute for those boundaries.
