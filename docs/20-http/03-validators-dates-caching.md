# Validators, Dates, and Caching

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Date handling, validators, conditional precedence, cache defaults, immutable mode, and representation-specific metadata.  

## Date generation

When HTTP semantics require an origin server with a clock to generate a `Date` field, Cerv SHALL do so.

Cerv emits HTTP-date in IMF-fixdate form using GMT.

Date formatting SHALL use a dedicated bounded formatter or carefully controlled libc time conversion. Locale-dependent output is forbidden.

## HTTP-date parsing

HTTP recipients are required to accept the three historical HTTP-date forms defined by the HTTP specification. Therefore Cerv's conditional-date parser SHALL accept:

- IMF-fixdate;
- obsolete RFC 850-style date;
- ANSI C `asctime()`-style date.

Cerv SHALL only **emit** IMF-fixdate.

The parser MUST be independent of process locale and timezone and must reject impossible dates safely.

## Last-Modified

For a selected representation Cerv SHOULD emit `Last-Modified` based on descriptor metadata when a meaningful modification time is available.

Rules:

- precision is seconds for HTTP-date;
- representation metadata, not logical-base metadata, is used;
- a Last-Modified value MUST NOT be later than the response Date;
- if filesystem mtime is in the future relative to Date, Cerv clamps Last-Modified to the response Date.

## ETag strength

Cerv needs deterministic validators without hashing an entire file on every request, and the validator must be identical on every replica that serves the same tree. Cerv therefore emits a **strong ETag** derived only from properties of the selected representation that do not depend on the host:

- the selected representation's byte length;
- its modification time with nanosecond precision;
- its content-coding.

The wire encoding is frozen as `"` followed by the byte length as 16 lowercase zero-padded hex digits, `-`, the mtime seconds as 16 hex digits, `-`, the mtime nanoseconds as 8 hex digits, an optional content-coding suffix, and a closing `"`. The suffix is empty for identity, `-gz` for gzip, and `-br` for Brotli, so representations with different codings never share a validator even if their size and mtime coincide. Signed seconds use their defined modulo-`2^64` conversion before hexadecimal formatting. For example, a 4-byte identity file modified at 1700000000.123456789 has the ETag `"0000000000000004-000000006553f100-075bcd15"`.

Device number, inode number, and ctime are deliberately **not** part of the validator. They differ between hosts, between container layers, and after every deploy or `chmod`, so including them would make revalidation fail whenever a load balancer switches replicas and would disclose filesystem internals to clients.

Marking the validator strong is a deliberate operator contract: **a changed file must have a changed size or modification time.** Deployments that publish immutable release directories or replace files atomically (write a new file, then rename) satisfy this by construction. A same-size in-place rewrite that also preserves the modification time to the filesystem's timestamp granularity cannot be detected and is outside the contract. Strength is what allows `If-Range` resumption to work.

## Why not hash each request

Hashing every file before serving would make validator generation O(file_size) and can double filesystem work for large cold files. It also destroys the desired fast path.

## If-None-Match

For GET/HEAD, `If-None-Match` is evaluated using the weak comparison semantics required by HTTP.

Cerv MUST handle:

- a comma-separated list of entity tags;
- wildcard `*`;
- weak tags supplied by clients or intermediaries;
- optional whitespace;
- malformed syntax according to the chosen conditional-field policy.

If the condition matches an existing selected representation, Cerv returns `304 Not Modified` for GET/HEAD.

## Conditional precedence

When `If-None-Match` is present, `If-Modified-Since` is ignored for the normal cache-validation decision as required by HTTP semantics.

Cerv SHALL encode this precedence directly in tests; it SHALL NOT accidentally evaluate the date first and override the ETag result.

## If-Modified-Since

If `If-None-Match` is absent and a valid `If-Modified-Since` is present, Cerv evaluates it against Last-Modified according to HTTP semantics.

Invalid `If-Modified-Since` is ignored rather than treated as a malformed request.

This is an example where “strict” does **not** mean rejecting every malformed optional semantic field: the protocol defines recipient behavior and Cerv follows it.

## 304 response metadata

A `304 Not Modified` has no content body. Cerv SHOULD emit the fields required/appropriate for a cache to update stored metadata, including the representation's:

- ETag;
- Date;
- Cache-Control;
- Vary when applicable;
- Last-Modified when normally emitted and applicable.

It SHALL NOT send a file body for HEAD or 304.

## Default cache policy

Cerv SHALL avoid guessing that filenames are content-addressed.

The baseline default is:

```text
Cache-Control: no-cache
```

In HTTP, `no-cache` permits storage but requires validation before reuse. This pairs well with ETag/Last-Modified and avoids accidentally pinning mutable files for long periods.

Changing this default requires a normative documentation and test update backed by deployment evidence.

## Immutable mode

Cerv MAY expose one explicit root-wide operator assertion:

```text
--immutable
```

When enabled, the operator is asserting that every deployed representation beneath this Cerv root follows an immutable/content-addressed deployment model. Cerv then emits the frozen baseline policy:

```text
Cache-Control: public, max-age=31536000, immutable
```

The baseline `max-age` is exactly 31536000 seconds.

Cerv SHALL NOT infer immutability from apparent hashes, dates, hexadecimal substrings, or filename patterns.

## Representation-specific validators

Because Brotli/gzip/identity bytes differ, validators and ranges apply to the **selected encoded representation**.

Cerv SHALL NOT reuse identity representation size/mtime/ETag blindly for `.br` or `.gz` sidecars.
