# Core Invariants

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** System-wide invariants that every implementation component must preserve.  

This appendix is intended to become a source-review checklist.

## Process/runtime invariants

1.  Worker count is fixed after startup.
2.  Each worker is single-threaded.
3.  Workers do not share mutable connection/request memory.
4.  No Cerv-owned request queue exists.
5.  Total connection slots equal the configured total, partitioned across workers.
6.  No steady-state request path performs Cerv-owned dynamic allocation.
7.  Every owned FD is either valid and has exactly one owner or equals the invalid sentinel.
8.  Every connection slot has one generation value that changes before reuse.
9.  Every epoll connection token is validated against current generation.
10.  Every live connection has a valid absolute lifetime deadline.

## Parser invariants

1.  Parser reads only `[buf, buf+received)`.
2.  No parser function requires NUL termination unless a checked copy created it.
3.  Request size cannot exceed fixed capacity.
4.  Every stored span lies inside input or fixed output storage.
5.  No field name is normalized to repair invalid syntax.
6.  Transfer-Encoding is never silently ignored.
7.  Nonzero/ambiguous Content-Length is never processed as zero body.
8.  At most `CERV_KEEPALIVE_REQUESTS_MAX` sequential requests are processed per connection; the cap is 64 in Cerv 1.0.
9.  A persistent connection returns to an empty receive state only after the prior response is complete; no request bytes or representation FD survive the per-request reset.

## Path/filesystem invariants

1.  URL decoding occurs exactly once.
2.  Decoded NUL is impossible in an accepted filesystem path.
3.  Dot segments are not accepted.
4.  Encoded slash/backslash is not accepted under baseline policy.
5.  Request file lookup is relative to opened root FD.
6.  Required `openat2()` resolution restrictions are always present.
7.  Symlink traversal is impossible under accepted lookup.
8.  Only descriptor-verified regular files are served.
9.  File metadata used for a response belongs to the opened selected representation.

## Response invariants

1.  Response header construction cannot overflow its fixed buffer.
2.  Response framing has a deterministic Content-Length/no-body rule.
3.  HEAD never sends content bytes.
4.  304 never sends content bytes.
5.  Range offsets never exceed selected representation bounds.
6.  Content-Encoding/Length/ETag/Last-Modified describe the selected representation.
7.  `Vary: Accept-Encoding` is present when negotiation can alter selection.
8.  Client input is never reflected into error body.
9.  Parser/resource/internal errors close after their response; successful HTTP/1.1 responses may persist only within the fixed request-count and absolute-lifetime caps.
10. `Connection: close`, HTTP/1.0, or the 64th request forces an explicit close response.

## Deadline invariants

1.  Header deadline starts at accept for the first request and at completion of the previous response for each permitted persistent request; received bytes never extend that request deadline.
2.  Write no-progress deadline resets only on actual progress.
3.  Absolute lifetime deadline never resets.
4.  Timer heap contains no more nodes than slots.
5.  Slot heap index and heap node slot identity agree.
6.  Expired timers cannot be postponed indefinitely by a busy event stream.
