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

The fallback is applied **only after the normal requested logical path produces `CERV_REPRESENTATION_NOT_FOUND`**, and **only for requests that name a client-side route**: a path whose final segment contains no `.`, or a directory-shaped path ending in `/`. A request whose final segment has a file extension (`/app.js`, `/assets/logo.png`, `/favicon.ico`) is a request for an asset, and an asset that does not exist is a real `404`; answering it with the HTML shell would hide deployment mistakes and give scripts and images the wrong media type. Cerv then performs ordinary representation selection for the configured fallback path. This means the fallback's own MIME type, precompressed sidecars, validators, ranges, HEAD behavior, and cache policy are used normally.

The fallback SHALL NOT replace malformed-request/path failures, forbidden filesystem results, representation negotiation failure (406), descriptor/resource exhaustion, or internal I/O errors. It is therefore a static-origin 404 fallback, not an application router or generic error-page mechanism.

The configured fallback path is parsed at startup as a bounded logical path. A leading `/` is accepted for operator convenience and stripped; empty segments, `.`/`..`, backslashes, query/fragment markers, control bytes, a trailing slash, and hidden (dot-prefixed) segments are rejected. An empty `CERV_SPA_FALLBACK` disables the feature.

## Dotfiles

Cerv never serves dotfiles. After percent-decoding, a request path is **hidden** when any of its `/`-separated segments begins with `.`; the single exception is a leading `.well-known` segment, the public namespace reserved by RFC 8615. A hidden path is answered exactly like an absent file (`404`), with no filesystem probe, so a client cannot tell whether `.env` or `.git/HEAD` exists.

Details:

- the check runs on the decoded path, so `/%2eenv` and `/.env` are equivalent;
- `.well-known/...` is public, but a dot-prefixed segment *inside* it (`.well-known/.secret`) and a `.well-known` segment that is not first (`app/.well-known/x`) are hidden;
- the comparison is exact and case-sensitive, so `.Well-Known` and `.well-known2` are hidden;
- a dot in the middle or at the end of a segment (`a./b`, `file.`) does not hide it;
- precompressed sidecars are looked up from the already-checked logical path and cannot reveal a hidden file;
- hidden paths do not trigger the SPA fallback for asset-shaped requests, and a hidden fallback path is rejected at startup.

This is a fixed policy, not a configurable denylist: version-control metadata, editor backups, credentials, and environment files are the most common way for a deployment mistake to become a disclosure, and no legitimate static site needs to publish them. Security-sensitive files should still not be deployed inside the document root at all; this rule is a safety net, not a substitute for root-content discipline.
