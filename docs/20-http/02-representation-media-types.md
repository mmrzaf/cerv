# Representation Selection and Media Types

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Precompressed representations, Accept-Encoding, identity semantics, Vary, Content-Encoding, and MIME.  

## Logical resource vs selected representation

Cerv SHALL distinguish the logical resource path from the physical representation file selected for transfer.

For a logical request such as:

```text
/assets/app.js
```

possible physical files are:

```text
assets/app.js
assets/app.js.br
assets/app.js.gz
```

Selection depends on availability and `Accept-Encoding`. Response metadata such as Content-Length, ETag, Last-Modified, byte ranges, and file transfer offsets MUST describe the **selected representation**, not blindly the logical base file.

## No dynamic compression

Cerv SHALL NOT compress content at request time.

Benefits:

- no compression library dependency in the request path;
- no attacker-controlled compression CPU cost;
- no per-request compressor memory;
- reproducible representation bytes;
- simpler ETags and ranges;
- build/deployment systems can optimize sidecars once.

## Sidecar discovery

Precompressed representation selection SHALL be determined by content negotiation rather than by a hardcoded unconditional encoding preference.

Cerv SHOULD open only representations that can be selected given the client's quality values. Missing, policy-rejected, permission-denied, or non-regular optional sidecars do not prevent selection of a lower-ranked acceptable representation. System/operational failures such as descriptor exhaustion, I/O failure, or an unsupported confinement primitive propagate instead of being treated as absence.

Sidecar resolution uses the same `openat2()` policy as the identity file.

## Accept-Encoding parser

`Accept-Encoding` SHALL be parsed according to HTTP semantics with a small dedicated parser.

Quality values SHOULD be represented as integer thousandths:

```text
0 .. 1000
```

No floating-point parser is needed.

The parser MUST handle:

- `br`;
- `gzip`;
- `identity`;
- `*` wildcard;
- `q=0` exclusions;
- absent `Accept-Encoding` semantics;
- optional whitespace permitted by field grammar;
- invalid qvalue syntax according to the chosen strict field policy;
- duplicate codings deterministically.

## Identity semantics

Identity encoding is acceptable by default unless explicitly excluded by the field semantics, including the relevant `identity;q=0` or wildcard exclusion case.

If no available representation has non-zero acceptability, Cerv SHALL return `406 Not Acceptable` rather than silently send an excluded identity representation.

This behavior MUST have direct protocol tests because simplified encoding parsers commonly get it wrong.

## Equal quality tie-break

When multiple available representations have the same positive client quality, Cerv needs a deterministic server preference.

When quality values are equal, Cerv SHALL use this deterministic preference order:

```text
br > gzip > identity
```

This is a server preference only after honoring client qvalues. The tie-break SHALL be documented and tested.

## Vary

Whenever the selected representation can vary based on `Accept-Encoding`, the response SHALL include:

```text
Vary: Accept-Encoding
```

This includes relevant 200/206/304 responses where caches need to understand the negotiation dimension.

## Content-Encoding

For selected Brotli or gzip sidecars, Cerv emits the appropriate `Content-Encoding` value.

The sidecar suffix is an internal deployment convention. The logical `Content-Type` is determined from the unencoded logical resource name, not from `.br` or `.gz`.

## Media type mapping

Cerv SHOULD contain a curated compile-time extension-to-media-type table covering common static web assets without importing a dynamic MIME database parser.

The baseline media-type table SHALL include at least:

```text
.html .htm      text/html; charset=utf-8
.css            text/css; charset=utf-8
.js .mjs        text/javascript; charset=utf-8
.json .map      application/json
.wasm           application/wasm
.xml            application/xml
.txt            text/plain; charset=utf-8
.svg            image/svg+xml
.png            image/png
.jpg .jpeg      image/jpeg
.gif            image/gif
.webp           image/webp
.avif           image/avif
.ico            image/x-icon
.woff           font/woff
.woff2          font/woff2
.pdf            application/pdf
.mp4            video/mp4
.webm           video/webm
.mp3            audio/mpeg
.ogg            audio/ogg
```

The table SHALL use IANA-registered media types where registrations exist. Additions require a normative documentation and test update; unknown extensions use Cerv's documented binary fallback type.

Unknown extensions return:

```text
application/octet-stream
```

Cerv SHALL NOT sniff content.

## nosniff

Responses serving files SHOULD include:

```text
X-Content-Type-Options: nosniff
```

Cerv owns this header; operators cannot override it through arbitrary custom-header configuration because such configuration is not in the core.
