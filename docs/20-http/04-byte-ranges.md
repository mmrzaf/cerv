# Byte Range Requests

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Single-range semantics, range parsing, satisfiability, HEAD behavior, and If-Range policy.  

## Scope

Single byte-range support is core file-serving correctness rather than feature expansion.

Cerv SHALL support one `bytes` range for GET:

```text
Range: bytes=0-499
Range: bytes=500-
Range: bytes=-500
```

Multipart range generation is outside the baseline.

## Selected representation coordinates

Range coordinates refer to the octets of the selected representation.

If Brotli is selected, byte offsets address the `.br` representation. If gzip is selected, they address `.gz`. If identity is selected, they address the original file.

This follows HTTP representation semantics and makes `sendfile()` offsets straightforward.

## Range parser

The parser SHALL use checked integer arithmetic and explicit `off_t` conversion checks.

It MUST correctly handle:

- start-end;
- start-;
- -suffix_length;
- zero-length file;
- integer overflow;
- end before start;
- start beyond representation length;
- suffix length greater than representation length;
- optional whitespace only where grammar allows it;
- unsupported range units.

The range helper is a strong CBMC candidate because input and output are finite integers and the loop/state space is small.

## Valid satisfiable range

A satisfiable single range yields:

```text
206 Partial Content
Content-Range: bytes <start>-<end>/<length>
Content-Length: <selected_count>
Accept-Ranges: bytes
```

The file-transfer state is initialized to the selected offset/end rather than opening a second type of transfer pipeline.

## Unsatisfiable range

An unsatisfiable byte range yields `416 Range Not Satisfiable` and includes:

```text
Content-Range: bytes */<current-length>
```

where required/appropriate by HTTP semantics.

## Multiple ranges

Cerv SHALL NOT generate `multipart/byteranges`. If a syntactically valid `Range` field contains more than one range-spec, Cerv SHALL ignore the `Range` field and process the request as though the field were absent. HTTP permits a server to ignore Range, and this keeps Cerv conforming without introducing multipart response state.

Malformed Range syntax remains subject to Cerv's strict field-validation policy; Cerv SHALL NOT reinterpret malformed syntax as a valid single range.

## HEAD with Range

Cerv SHALL ignore `Range` on HEAD. HTTP defines Range handling for GET; a Range field on a method for which range handling is not defined is ignored. The HEAD response otherwise follows Cerv's normal HEAD metadata contract and contains no body.

## If-Range

Cerv honors `If-Range` with the stricter validator semantics HTTP requires for range resumption:

- an entity-tag validator is a match only when it is a **strong** tag byte-for-byte equal to the selected representation's ETag; Cerv's own ETags are strong, so a client resuming a download with the ETag it received gets `206`;
- a weak tag, a non-matching tag, or a tag for a different content-coding never matches;
- a date validator never matches: descriptor mtime cannot prove the representation did not change twice within one HTTP-date second, so Cerv does not treat Last-Modified as a strong validator;
- a malformed `If-Range`, or more than one `If-Range` field, never matches;
- `If-Range` without `Range` is ignored.

When the condition does not match, Cerv ignores `Range` and sends the full selected representation with `200`. When it matches, the `Range` is processed normally, including `416` for an unsatisfiable range.
