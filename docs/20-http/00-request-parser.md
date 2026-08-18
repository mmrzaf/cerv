# HTTP Request Parser

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Message framing, request-line parsing, versions, methods, target forms, Host, fields, and parser results.  

## Parser philosophy

The HTTP parser is security-critical code. It SHALL be intentionally narrow, byte-oriented, allocation-free, deterministic, and independently fuzzable.

The parser SHALL distinguish:

- syntactically invalid input;
- syntactically valid but unsupported method/semantics;
- syntactically valid request that maps to a resource lookup;
- incomplete input requiring more bytes;
- explicit size-limit violation.

The parser SHALL never read beyond the received byte count or depend on NUL termination.

## Message boundary

Cerv parses exactly one complete HTTP request per parser invocation. A TCP connection may carry a bounded sequence of such requests under the persistence contract; request bytes are never parsed as an unbounded stream.

The request header section ends at the first canonical:

```text
CRLF CRLF
```

Cerv's policy is to require canonical CRLF line endings. Bare LF is rejected. Bare CR is rejected.

This is intentionally stricter than HTTP robustness allowances that permit some recipients to recognize bare LF; it removes a class of parser differential and is acceptable for Cerv's deployment target.

## Request line

The request line has exactly:

```text
method SP request-target SP HTTP-version CRLF
```

Cerv SHALL require exactly one ASCII SP at each delimiter. Tabs or multiple whitespace sequences are rejected rather than normalized.

The method is parsed as a token; the request target is bounded by the request-line limit; `HTTP/` is case-sensitive.

## HTTP version

Cerv SHALL support HTTP/1.0 and HTTP/1.1 request syntax. HTTP/1.1 is the preferred deployment protocol because bounded connection reuse is available only there.

For protocol versioning:

- major version 1 with a higher minor version SHALL be handled according to HTTP version semantics rather than blindly answered with 505;
- an unsupported major version MAY receive 505 HTTP Version Not Supported;
- Cerv emits an `HTTP/1.1` response status-line for both accepted HTTP/1.0 and HTTP/1.1 requests; responses remain HTTP/1.0-interpretable by using connection close and no transfer coding; this policy SHALL be tested.

HTTP/1.1 is persistent by default under Cerv's bounded reuse contract. HTTP/1.0 always closes. Cerv accepts `Connection: close`; successful HTTP/1.1 connections may be reused for at most 64 sequential requests, while parser/resource/error paths close. Cerv does not promise request pipelining.

## Methods

Cerv implements:

- GET;
- HEAD.

A syntactically valid and recognized HTTP method that Cerv does not allow for the resource class SHOULD receive `405 Method Not Allowed` with:

```text
Allow: GET, HEAD
```

A syntactically valid but unrecognized/unimplemented method token MAY receive `501 Not Implemented`, consistent with HTTP semantics.

No method causes Cerv to read or process a request body.

## Request-target forms

Cerv SHALL correctly distinguish the HTTP/1.1 request-target forms:

- origin-form;
- absolute-form;
- authority-form;
- asterisk-form.

For GET/HEAD static serving:

- origin-form is normal;
- absolute-form MUST be accepted because HTTP/1.1 requires servers to accept it;
- authority-form is only meaningful for CONNECT and therefore will not reach file lookup under Cerv's method policy;
- asterisk-form is associated with server-wide OPTIONS and therefore will not reach file lookup under Cerv's method policy.

Rejecting absolute-form merely because direct clients normally use origin-form would be non-conforming.

## Absolute-form authority handling

When an origin server receives an absolute-form request target, HTTP/1.1 requires the origin server to use the authority from the request target rather than the received Host field for target reconstruction.

Cerv SHALL therefore parse the authority in absolute-form sufficiently to enforce HTTP target semantics. It SHALL NOT incorrectly compare/override it using a different Host interpretation.

This rule MUST have proxy-differential tests because it is a common source of mistaken “strictness.”

## Host

For HTTP/1.1:

- exactly one Host field line is required;
- missing Host is 400;
- more than one Host field line is 400;
- invalid Host field value is 400.

For HTTP/1.0, Host is optional but, if present, SHALL be syntax-validated.

Cerv is not initially a name-based virtual-host router. The Host/authority is validated for protocol correctness but does not select a different document root.

## Header field syntax

For every field line:

- field name is validated as an HTTP field-name/token;
- whitespace between field name and colon is rejected with 400;
- obsolete line folding (`obs-fold`) is rejected;
- invalid control octets are rejected;
- NUL is rejected;
- field values are bounded by the field-line limit;
- field names are compared case-insensitively without locale-sensitive routines.

Cerv SHALL trim only the whitespace HTTP permits around field values. It SHALL NOT trim field names to “repair” invalid grammar.

## Unknown headers

Unknown syntactically valid fields are generally ignored after validation.

Cerv SHALL NOT store an arbitrary header map. Known fields are recognized into fixed request-state fields; unknown fields disappear after syntax validation.

Exceptions exist where an unknown/extension field changes message semantics in a way Cerv must reject; protocol framing fields are the primary example.

## Transfer-Encoding

Cerv does not implement request transfer codings.

Any request containing a `Transfer-Encoding` field SHALL be rejected with 400 and the connection closed.

Cerv SHALL NOT “ignore” Transfer-Encoding, because framing ambiguity between recipients is security-sensitive.

## Content-Length

Cerv does not process request bodies.

Policy:

- zero `Content-Length` field lines: accepted if other semantics permit;
- exactly one valid decimal `Content-Length: 0`: accepted;
- non-zero Content-Length: 400 and close;
- malformed Content-Length: 400 and close;
- overflow: 400 and close;
- duplicate Content-Length field lines: 400 and close, even if values are identical.

HTTP may allow recipients to handle some identical duplicate Content-Length values. Cerv deliberately chooses the stricter valid recipient policy because it has no need for body framing and wants one unambiguous input form.

If both Transfer-Encoding and Content-Length appear, the request is rejected.

## Request bodies and extra bytes

Because Cerv accepts only zero-length requests, bytes after the terminating header section are not interpreted as a second request or body.

If header semantics indicate a non-zero body, Cerv sends a final error without entering a body state and closes.

If extra bytes are already present after a nominal zero-body header section, Cerv SHOULD treat this as malformed input and close after an error rather than process the first request and leave ambiguous pipelined data. The exact status is a conformance test item.

## Expect

Cerv does not need a `100 Continue` state.

When a request carries `Expect: 100-continue` together with semantics Cerv will reject (for example, a non-zero body), Cerv SHOULD send the appropriate final error immediately and close.

Unsupported expectations are handled with `417 Expectation Failed` where HTTP semantics call for it.

## Header count

Every field line, including unknown fields, increments the bounded field count. Requests exceeding the maximum field count receive an explicit header-limit error, normally `431 Request Header Fields Too Large`, and close.

The response decision between 400 and 431 SHALL be deterministic across:

- total header bytes exceeded;
- individual field line exceeded;
- field count exceeded;
- request line exceeded (normally 414 when attributable to request target length, otherwise 400/431 policy must be fixed).

## Parser result type

A parser API SHOULD return an enum plus fixed parsed metadata rather than use errno-style overloaded integers. Example conceptual result classes:

```text
CERV_PARSE_INCOMPLETE
CERV_PARSE_OK
CERV_PARSE_BAD_REQUEST
CERV_PARSE_URI_TOO_LONG
CERV_PARSE_HEADERS_TOO_LARGE
CERV_PARSE_METHOD_NOT_ALLOWED
CERV_PARSE_NOT_IMPLEMENTED
CERV_PARSE_HTTP_VERSION_UNSUPPORTED
CERV_PARSE_EXPECTATION_FAILED
```

Protocol status selection MAY be separated from low-level grammar parsing, but the distinction must remain explicit.
