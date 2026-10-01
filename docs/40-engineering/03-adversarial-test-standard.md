# Adversarial Test Standard

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Parser, path, filesystem, negotiation, conditional, range, slow-client, saturation, shutdown, and exhaustion tests.  

## Parser differential corpus

At minimum, automated tests SHALL cover:

- whitespace before header colon;
- tabs in request-line delimiters;
- multiple SP where exact SP policy rejects;
- bare LF and bare CR;
- whitespace lines between request line and first field;
- obs-fold;
- invalid token characters in field names;
- NUL in every parser region;
- duplicate Host;
- missing Host in HTTP/1.1;
- invalid Host syntax;
- absolute-form with Host that disagrees, verifying target authority precedence;
- malformed HTTP-version;
- higher 1.x minor version;
- unsupported major version;
- Transfer-Encoding alone;
- Content-Length alone for 0/nonzero;
- malformed/overflowing Content-Length;
- duplicate identical Content-Length;
- duplicate differing Content-Length;
- TE + CL;
- bytes after zero-body headers;
- `Expect: 100-continue` combinations;
- request line/header exact size boundaries.

## Path corpus

Tests SHALL cover:

```text
/
/a
/a/
/a//b
/./a
/../a
/a/../b
/%2e/a
/%2e%2e/a
/%252e%252e/a
/a%2fb
/a%2Fb
/a\b
/a%5cb
/%00
/%
/%0
/%GG
/non-ASCII-percent-encoded-bytes
/#fragment-like-text
/?query
/.env
/%2eenv
/.git/HEAD
/a/.hidden/b
/.well-known/security.txt
/.well-known/.secret
/a/.well-known/x
```

For every accepted path, test that the filesystem path remains beneath root under concurrent rename/link activity to the extent the chosen kernel semantics allow.

## Filesystem attacks

Integration tests SHALL construct roots containing:

- final-component symlink;
- intermediate symlink;
- symlink loop;
- symlink to `/etc/passwd` or another outside fixture;
- FIFO;
- Unix socket;
- directory where file expected;
- dotfiles and dot-directories (`.env`, `.git/HEAD`, a dot-segment below `.well-known`) that exist on disk, asserting a plain 404 and no disclosure, alongside a public `.well-known/` file;
- missing assets (`/app.js`) under an SPA fallback, asserting 404 rather than the fallback document;
- hard link fixture;
- permission-denied file/directory;
- mountpoint fixture when CI allows;
- file renamed/replaced between requests;
- file unlinked after FD open;
- large sparse file;
- zero-byte file.

The tests SHALL assert both HTTP result and absence of unintended external file disclosure.

## Negotiation matrix

Accept-Encoding tests include:

- absent field;
- empty field if syntactically meaningful;
- br/gzip/identity individually;
- wildcard;
- each q=0;
- `identity;q=0`;
- `*;q=0`;
- quality ties;
- quality ordering different from server preference;
- malformed qvalues;
- duplicate codings;
- sidecar exists/absent/permission denied;
- identity absent but sidecar present;
- no acceptable representation =>406.

## Conditional matrix

Tests SHALL cover:

- exact strong ETag match;
- weak client tag vs strong current tag using weak If-None-Match comparison;
- non-match;
- list containing a match;
- wildcard `*`;
- If-None-Match + If-Modified-Since precedence;
- valid three HTTP-date formats;
- invalid If-Modified-Since ignored;
- future Last-Modified handling;
- 304 header presence;
- selected gzip/br representation validator independence.

## Range matrix

At minimum:

- `0-0`;
- `0-(len-1)`;
- `0-len` clamping semantics as required;
- `len-1-`;
- `len-` unsatisfiable;
- `-1`;
- `-len`;
- suffix larger than len;
- zero suffix if grammar permits/invalidates;
- reversed range;
- integer overflow;
- multiple ranges;
- unknown range unit;
- Range + If-Range matching strong ETag (206);
- Range + If-Range weak, mismatched, malformed, or duplicated ETag (200);
- Range + If-Range date (200);
- range over compressed representation;
- HEAD + Range;
- zero-byte file.

## Slow-client tests

Use deterministic socket clients to exercise:

- one byte of headers per interval;
- stop before CRLFCRLF;
- headers complete just before deadline;
- headers complete just after deadline;
- client stops reading response headers;
- client reads one byte at a time;
- large file with periodic progress just inside write timeout;
- total lifetime expiry despite continued progress.

Memory and connection-slot counts MUST remain bounded throughout.

## Saturation tests

Fill exactly all configured slots, then attempt additional connections.

Verify:

- existing connections continue making progress;
- extra clients receive best-effort 503 or prompt close according to overload path;
- no user-space queue grows;
- RSS remains within measured bound;
- CPU does not spin on permanently full listener state;
- after slots free, admission resumes;
- behavior remains stable over repeated saturation cycles.

## Signal/shutdown tests

Test SIGTERM/SIGINT:

- idle server;
- during header receive;
- during 200 small response;
- during large sendfile;
- during fallback transfer;
- during saturation;
- with one worker unexpectedly killed;
- repeated termination signal;
- shutdown deadline expiry.

All children SHALL be reaped. No listening socket or temporary process should survive the test harness.

## FD exhaustion tests

Where test environment permits, lower `RLIMIT_NOFILE` and verify:

- startup refuses impossible max-connection config;
- near-limit supported config works;
- induced runtime FD exhaustion produces operational failure classification, not 404;
- FD counts return to baseline after connection churn.
