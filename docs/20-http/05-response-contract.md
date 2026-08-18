# Response Contract

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Response construction, header ownership, connection close, HEAD, errors, and Content-Length behavior.  

## Response construction

All response headers SHALL be constructed into a fixed-size response buffer using length-aware append helpers.

A helper MUST return failure before writing past capacity. Truncating an HTTP response header is forbidden.

Failure to construct a response that should fit the compile-time contract is an internal invariant failure, not an invitation to dynamically allocate more memory.

## Header ownership

Cerv owns its response grammar. There is no arbitrary `--header` mechanism in the core baseline.

Depending on status/representation, owned fields include:

- Date;
- Connection;
- Content-Type;
- Content-Length;
- Content-Encoding;
- Vary;
- ETag;
- Last-Modified;
- Cache-Control;
- Accept-Ranges;
- Content-Range;
- Allow;
- Retry-After;
- X-Content-Type-Options.

A `Server` field SHOULD be omitted by default because it is not needed for function and adds fingerprinting/version-management obligations.

## Connection persistence and close

Cerv 1.0 supports **bounded sequential HTTP/1.1 persistence** because reverse-proxy measurements showed a material cost from reconnecting for every small file. Persistence does not create an unbounded session state.

The rules are:

- HTTP/1.1 GET/HEAD requests are persistent by default when the selected resource response is safe to reuse.
- HTTP/1.0 always closes; Cerv does not implement HTTP/1.0 keep-alive negotiation.
- `Connection: close` always forces close.
- parser errors, lookup/representation failures, internal failures, deadline expiry, or transfer failure close the connection.
- a connection serves at most `CERV_KEEPALIVE_REQUESTS_MAX` sequential requests; Cerv 1.0 fixes this at 64 and emits `Connection: close` on the final permitted response.
- the absolute connection lifetime continues from the original accept and never resets.
- each new persistent request receives a fresh absolute header deadline beginning only after the previous response completed; bytes received do not extend it.
- response/file/request scratch state and any selected representation FD are cleared/closed before returning to receive state.
- when a worker enters graceful drain, every admitted connection is forced close-after-response: an in-flight response finishes and closes, while an idle reusable connection may serve at most its next request and that response carries `Connection: close`; persistence cannot extend shutdown.
- Cerv does not promise HTTP request pipelining. A request parser invocation still accepts exactly one complete request and rejects trailing bytes in that invocation; clients and reverse proxies SHOULD send the next request only after receiving the prior response.

When a response will close, HTTP/1.1 Cerv emits:

```text
Connection: close
```

For a persistent HTTP/1.1 response Cerv omits a `Connection` field; persistence is the protocol default. The bounded reuse cap is a fairness/resource bound, not an operator-tunable cache or session setting.

## HEAD

HEAD uses the same selection and metadata logic as GET where practical but sends no content body.

The headers SHOULD describe what the corresponding GET would have returned, subject to HTTP allowances for fields whose computation would require body generation.

Because Cerv's representations already exist as files, Content-Length and validators are normally cheap and SHOULD be present consistently.

## Error bodies

Error bodies SHALL be compile-time constant, short, plain text, and non-reflective.

They MUST NOT include:

- request target;
- Host;
- decoded filesystem path;
- errno text;
- worker PID;
- build path;
- stack data;
- client-supplied header values.

Cerv's baseline status vocabulary includes:

```text
400 Bad Request
403 Forbidden
404 Not Found
405 Method Not Allowed
406 Not Acceptable
408 Request Timeout
414 URI Too Long
416 Range Not Satisfiable
417 Expectation Failed
431 Request Header Fields Too Large
500 Internal Server Error
501 Not Implemented
503 Service Unavailable
505 HTTP Version Not Supported
```

Only statuses reachable by defined states should exist in the response table.

## Content-Length on errors

Constant error bodies allow exact compile-time/runtime-known `Content-Length` values. Cerv SHALL NOT rely on connection close as an excuse to emit ambiguous response framing.

HEAD error responses follow HEAD body suppression while retaining the corresponding representation length where HTTP semantics call for it.

## Status-line reason phrases

HTTP/1.1 reason phrases are not semantically authoritative. Cerv MAY use conventional fixed phrases for readability. They SHALL be compile-time constants with no client input.

## Date on errors

Cerv's Date policy SHALL follow RFC 9110's origin-server requirements, including the distinction between ordinary 2xx/3xx/4xx responses and statuses where Date is optional.

Implementation should prefer one central response-builder decision table rather than scattered ad hoc exceptions.
