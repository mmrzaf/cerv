# Filesystem Confinement

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Root FD, openat2 confinement, symlink policy, regular-file requirements, races, and error classification.  

## Root FD

At startup Cerv opens the configured document root as a directory file descriptor and validates that it is a directory.

After startup, request handling SHALL resolve relative to this FD. It SHALL NOT concatenate an absolute root string with attacker-controlled path text and call ordinary absolute-path `open()`.

## openat2 security model

The baseline file open uses Linux `openat2()` with a zero-initialized `struct open_how` and resolution constraints that include:

```text
RESOLVE_BENEATH
RESOLVE_NO_SYMLINKS
RESOLVE_NO_MAGICLINKS
```

`RESOLVE_BENEATH` prevents resolution from escaping the supplied root directory through path traversal/absolute resolution. `RESOLVE_NO_SYMLINKS` prevents symbolic-link traversal throughout lookup. `RESOLVE_NO_MAGICLINKS` is included explicitly for policy clarity even where implied by stronger flags.

The exact `open_how` size passed to the syscall SHALL follow the documented extensible ABI rules.

Under `RESOLVE_BENEATH`, Linux can report `EAGAIN` when it cannot prove confinement because of concurrent rename/mount activity. Cerv retries the `openat2()` operation a fixed three total attempts and then returns its internal policy-rejection class; it never falls back to weaker resolution.

## No fallback to weaker containment

If the kernel does not support required `openat2()` behavior, startup fails.

Cerv SHALL NOT silently fall back to:

- `realpath()`/`canonicalize()` followed by reopen;
- lexical prefix checks;
- chroot-only containment;
- a component walk with weaker semantics solely for old-kernel compatibility.

A future compatibility build would be a distinct product profile with a distinct security claim, not an invisible fallback.

## Final object requirements

Cerv opens candidate representations with `O_NONBLOCK` so a hostile FIFO or similar special object cannot stall the worker during `openat2()`. After opening, Cerv SHALL verify that the representation is a regular file using descriptor-based metadata.

Cerv SHALL NOT serve:

- directories;
- FIFOs;
- Unix sockets;
- device nodes;
- symbolic links;
- other special inode types.

The check occurs on the opened descriptor, not on a pathname before opening.

## Hard links

Hard links are not distinguishable from ordinary regular-file directory entries after resolution. Cerv SHALL allow them.

The operator is responsible for ensuring the document root does not contain hard links to content they do not intend to expose.

This is a deployment-content property, not a pathname traversal issue.

## Mount points

The baseline does not set `RESOLVE_NO_XDEV`. Mount points under the document root are therefore allowed unless deployment policy forbids them.

Rationale: forbidding all mount crossings can break legitimate container/read-only mount layouts and is not necessary for root containment.

However, mounted remote/FUSE filesystems alter liveness assumptions. Production guidance SHOULD recommend a local read-only filesystem for strongest predictability.

## Representation race model

The important race boundary is the opened file descriptor.

Once Cerv selects and opens a representation, response metadata and transfer SHOULD be derived from that FD. The pathname can change afterward without redirecting the already-open FD to another inode.

Deployment SHOULD replace immutable content atomically rather than mutate served files in place. In-place mutation during a transfer can still produce semantic inconsistencies (e.g. metadata and bytes observed at different moments) that no pathname confinement mechanism eliminates.

## Error classification

Filesystem errors MUST be classified instead of flattened to 404.

Cerv SHALL classify filesystem outcomes as follows:

| **Error class** | **HTTP / action** |
| --- | --- |
| Path absent / non-directory component | 404 |
| Symlink/magic-link forbidden by policy | 404 to the client; retain an internal policy-rejection class for diagnostics |
| Permission denied | 403 |
| Opened object not regular | 404; retain a distinct internal non-regular class |
| Per-process FD exhaustion | 500/503 + fatal/operational diagnostic; never 404 |
| System-wide FD exhaustion | 503/fatal operational condition |
| I/O error | 500 |
| Unsupported kernel primitive at runtime | internal invariant/startup defect; fail-stop |

Error bodies MUST NOT expose raw paths or errno strings to clients. Diagnostics MAY expose sanitized errno names/numbers locally within bounded records.
