# Request Target and Path Policy

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** URI decoding, path syntax, slash/dot-segment policy, query handling, directories, and dotfiles.  

## Separation of URI parsing and filesystem resolution

Cerv SHALL treat these as distinct stages:

1.  parse HTTP request-target form;
2.  obtain the path component relevant to the origin resource;
3.  separate and ignore query for filesystem selection;
4.  perform strict percent-decoding/path validation;
5.  map the resulting relative path beneath the configured root using kernel-enforced resolution.

The path parser SHALL NOT call filesystem functions.

## Decode exactly once

Percent-encoding is decoded exactly once.

Examples:

- `%2e` becomes `.` and is then subject to dot-segment rejection;
- `%252e` becomes the literal bytes `%2e` and is **not decoded again**;
- malformed `%` triplets are 400.

No double-decoding layer exists.

## Encoded slash

Cerv SHOULD reject percent-encoded `/` (`%2F` case-insensitively) rather than decode it into a new path-segment boundary.

Reason: intermediaries differ in whether/when they decode encoded slashes. Rejecting the form avoids a path interpretation differential while preserving all ordinary static paths.

This is Cerv policy, not a claim that URI syntax universally forbids encoded slash.

## Backslash

Raw or percent-decoded backslash SHOULD be rejected in the resource path.

Linux does not treat backslash as a separator, but browsers/proxies/security middleware can normalize it inconsistently. Cerv has no need to support it in URL paths and therefore removes the ambiguity.

## Dot segments

After the one decode pass, any path segment exactly equal to:

```text
.
..
```

is rejected rather than normalized.

This includes encoded forms that decode to those segments.

The server does not need path traversal syntax, even when traversal would later be contained by `openat2()`.

## Repeated slash and empty segments

Cerv SHOULD reject interior empty path segments created by repeated slashes rather than collapse them.

Example:

```text
/a//b
```

is rejected instead of normalized to `/a/b`.

This keeps the mapping from accepted URL path to filesystem lookup deterministic and minimizes disagreement with intermediaries.

The leading `/` required by origin-form is not an interior empty segment.

## NUL and controls

A decoded NUL byte is rejected. Control octets not valid in the target syntax are rejected.

Path handling SHALL use explicit lengths throughout; NUL rejection is still required because filesystem interfaces ultimately use NUL-terminated path strings.

## Non-ASCII bytes

Cerv does not perform Unicode normalization or case folding.

Percent-encoded octets may map to non-ASCII Linux filename bytes as long as they pass the path safety rules and can be represented in the filesystem pathname API.

The mapping is byte-oriented. Operators are responsible for naming consistency in the root.

## Query

The query component does not select a different file. It is ignored after syntax separation.

Therefore these map to the same resource path:

```text
/app.js
/app.js?v=123
```

Conditional/cache behavior is still governed by the selected representation, not the query text.

## Fragment marker

A URI fragment is not part of an HTTP request target sent to the origin. A raw `#` in a request target SHOULD therefore be rejected rather than treated as a fragment delimiter.

## Directory mapping

Cerv does not list directories.

A path ending in `/` SHALL map to the fixed index filename:

```text
index.html
```

The index filename is compile-time fixed to `index.html` in the baseline and is not configurable.

A request to `/foo` SHALL NOT automatically redirect to `/foo/` in the baseline. It is either a regular file named `foo` or not found. This avoids directory probing/redirect policy and keeps path mapping literal.

## Optional SPA fallback

Cerv MAY be configured with one fixed root-relative SPA fallback path using `--spa-fallback PATH` or `CERV_SPA_FALLBACK`. The default is disabled.

The fallback is applied **only after the normal requested logical path produces `CERV_REPRESENTATION_NOT_FOUND`**. Cerv then performs ordinary representation selection for the configured fallback path. This means the fallback's own MIME type, precompressed sidecars, validators, ranges, HEAD behavior, and cache policy are used normally.

The fallback SHALL NOT replace malformed-request/path failures, forbidden filesystem results, representation negotiation failure (406), descriptor/resource exhaustion, or internal I/O errors. It is therefore a static-origin 404 fallback, not an application router or generic error-page mechanism.

The configured fallback path is parsed at startup as a bounded logical path. A leading `/` is accepted for operator convenience and stripped; empty segments, `.`/`..`, backslashes, query/fragment markers, control bytes, and a trailing slash are rejected. An empty `CERV_SPA_FALLBACK` disables the feature.

## Dotfiles

Cerv SHALL NOT maintain a generic “dotfile denylist.”

Reasons:

- `.well-known` is a legitimate web namespace;
- security-sensitive files should not be deployed inside the document root;
- filename heuristics are an unreliable substitute for root-content discipline.

The root is an explicit trust boundary: **everything reachable as an allowed regular file is considered intended to be serveable** unless a later explicit policy says otherwise.
