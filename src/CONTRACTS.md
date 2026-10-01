# Internal API Contracts

This file is implementation documentation, not a replacement for CES. It records the cross-translation-unit contracts required by `docs/10-architecture/06-source-architecture.md`.

## Contract defaults

Unless a function-specific row says otherwise:

- byte spans are borrowed views; the callee never takes ownership and never retains pointers beyond the call except where the returned/request structure explicitly stores borrowed spans into the caller's request bytes;
- output pointers must be non-NULL; a successful call writes the documented result, while failure leaves the output unspecified unless the row promises otherwise;
- pure functions do not allocate, free, open/close descriptors, perform filesystem access, perform socket I/O, block, or intentionally modify `errno`; the Linux clock/filesystem, representation lifecycle, connection, timer, worker, configuration-discovery, diagnostics, and supervisor sections below explicitly override descriptor/filesystem/socket/time/process/signal side effects where stated;
- work is O(input length) or O(1) and bounded by the explicit caller/domain limit stated below;
- all attacker-controlled text is bytes plus length, not NUL-terminated text;
- invalid attacker input is returned as an ordinary false/result-enum value, never an invariant failure;
- functions preserve memory safety for every input in their accepted domain, including failure paths.

`struct cerv_span` is valid when `ptr != NULL`, or when `ptr == NULL && len == 0`. A function may impose a stricter non-empty requirement.

## `base/slice.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_span_equal` | two valid spans | exact byte equality; reads neither span when length is zero | O(min lengths) via bounded `memcmp` |
| `cerv_span_equal_ascii_ci` | valid span; non-NULL NUL-terminated internal ASCII literal | ASCII-only case-insensitive equality; no locale dependency | span length + internal literal length |
| `cerv_span_trim_ows` | valid span | borrowed subspan with leading/trailing SP/HTAB removed; NULL/zero remains NULL/zero | input length |

## `base/path.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_path_is_hidden` | NULL or a decoded relative path of any length | true iff some `/`-separated segment begins with `.` other than an exact, case-sensitive leading `.well-known`; NULL and empty are not hidden | O(length), no allocation |
| `cerv_path_last_segment_has_dot` | NULL or a decoded relative path | true iff the final `/`-separated segment contains `.`; used to tell asset requests from client-side routes | O(length), no allocation |

## `base/checked.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_size_add` | any `size_t` pair | true iff mathematical sum fits `size_t`; output written only on success | O(1) |
| `cerv_size_sub` | any `size_t` pair | true iff `a - b` is non-negative in the unsigned domain; output written only on success | O(1) |
| `cerv_size_mul` | any `size_t` pair | true iff mathematical product fits `size_t`; output written only on success | O(1) |
| `cerv_u64_add` | any `uint64_t` pair | true iff mathematical sum fits `uint64_t`; output written only on success | O(1) |
| `cerv_u64_sub` | any `uint64_t` pair | true iff `a - b` is non-negative in the unsigned domain; output written only on success | O(1) |
| `cerv_u64_mul` | any `uint64_t` pair | true iff mathematical product fits `uint64_t`; output written only on success | O(1) |
| `cerv_u64_decimal` | non-NULL bytes, `0 < len <= 20` | accepts ASCII decimal digits only; rejects longer spans and overflow before unbounded work; output written only on success | at most 20 bytes |
| `cerv_u64_to_off_t` | any `uint64_t`; Linux 64-bit signed `off_t` platform contract | true iff exactly representable as non-negative `off_t` | O(1) |

## `base/buffer.h`

A `struct cerv_buffer` owns no storage. The caller owns `storage` for the entire lifetime of the buffer view. Successful appends preserve `used <= capacity`; failed appends do not change `used` or storage.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_buffer_init` | non-NULL buffer; storage may be NULL only for zero usable capacity | initializes an empty borrowed fixed-capacity writer | O(1) |
| `cerv_buffer_append` | initialized buffer; `src != NULL` when `len > 0` | true iff `used + len <= capacity` without overflow; copies exactly `len` bytes with overlap-safe semantics | requested append length |
| `cerv_buffer_append_byte` | initialized buffer | one-byte specialization of append | O(1) |
| `cerv_buffer_append_u64` | initialized buffer; any `uint64_t` | appends canonical base-10 digits atomically or fails without changing `used` | at most 20 bytes |

## `base/cerv_time.h`

Time values are monotonic-domain scalar nanoseconds. They carry no wall-clock or timezone meaning.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_duration_from_ms` | any `uint64_t` milliseconds | true iff nanosecond conversion fits; output written only on success | O(1) |
| `cerv_duration_from_seconds` | any `uint64_t` seconds | true iff nanosecond conversion fits; output written only on success | O(1) |
| `cerv_mono_add_saturating` | any monotonic time and duration | exact sum or `UINT64_MAX` saturation; never wraps | O(1) |
| `cerv_mono_remaining_ms_ceil` | any `now`, `deadline` | zero if expired; otherwise positive ceiling in milliseconds | O(1) |

## `http/http_date.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_http_date_parse` | valid span; `current_year` 0..9999 | accepts only the three HTTP-date wire forms implemented by CES, rejects impossible lengths before scanning, validates weekday/civil date/time, writes output only on success | fixed 24/29/30..33-byte forms |
| `cerv_http_date_format_imf` | Unix second whose civil year is 0000..9999 | writes exactly 29 IMF-fixdate bytes on success, no terminator; rejects values outside the safe domain before civil arithmetic | exactly 29 output bytes |

## `http/http_fields.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_http_is_token` | valid non-empty span up to `CERV_FIELD_LINE_MAX` | true only for HTTP token bytes; over-domain spans are rejected before scanning | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_field_value_valid` | valid span, empty allowed | rejects NUL/invalid controls and over-domain spans; accepts HTAB/SP, visible ASCII, obs-text | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_authority_parse` | valid span; empty allowed only when flag set | validates Host/authority grammar used by Cerv; over-domain spans are rejected; output spans borrow input | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_content_length_parse` | valid span | rejects over-domain spans before trimming; accepts one non-empty OWS-trimmed decimal integer with overflow rejection | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_qvalue_parse` | valid non-empty span | explicit `CERV_Q_OK/INVALID`; qvalue becomes integer 0..1000; output only on success | max syntactic qvalue length 5 |
| `cerv_accept_encoding_init` | non-NULL state | establishes “field absent” state | O(1) |
| `cerv_accept_encoding_add_field` | initialized state; valid field-value span | rejects over-domain spans; parses one combined field value; RFC list empty members are ignored; success commits deterministic qualities; failure is transactional and leaves all logical state fields unchanged | at most `CERV_FIELD_LINE_MAX` bytes; fixed state only |
| `cerv_accept_encoding_quality` | initialized state; non-NULL internal coding literal | returns integer quality 0..1000 using absent/wildcard/identity semantics | O(1) for Cerv-supported codings |
| `cerv_entity_tag_parse` | valid span | explicit OK/INVALID; rejects over-domain spans; parsed opaque tag borrows input | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_entity_tag_weak_equal` | entity tags returned by parser or equivalent valid structs | compares opaque tags only | opaque-tag length |
| `cerv_entity_tag_strong_equal` | entity tags returned by parser or equivalent valid structs | true only when both strong and opaque tags equal | opaque-tag length |
| `cerv_if_none_match_valid` | valid span | validates wildcard or comma-separated entity-tag list; RFC list empty members are ignored; over-domain spans rejected before scanning | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_if_none_match_matches` | valid If-None-Match span and current tag | weak comparison; RFC list empty members are ignored; malformed/over-domain input never matches | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_if_range_parse` | valid span; `current_year` 0..9999 | rejects over-domain spans; returns ETAG, DATE, or INVALID; weak tags remain explicitly marked weak for later strong-validation policy | at most `CERV_FIELD_LINE_MAX` bytes; date branch fixed-length |

`Accept-Encoding` duplicate supported codings are resolved by keeping the highest qvalue. This is a frozen deterministic parser rule; representation tie-breaking is a later-layer concern.

## `http/http_target.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_http_target_parse` | valid non-empty raw request-target span | distinguishes supported origin/absolute/authority/asterisk syntax; returned spans borrow raw bytes; no decode or filesystem lookup | `CERV_REQUEST_LINE_MAX` |
| `cerv_http_target_decode_path` | successfully parsed origin/absolute target | decodes exactly once into caller-owned fixed array; success is relative, NUL-terminated, NUL-free before terminator, within capacity; rejects encoded slash, raw/decoded backslash, dot segments, repeated interior slash, decoded controls | input path <= request-line limit; output `< CERV_PATH_BYTES_MAX` |

Absolute-form accepts Cerv's HTTP target URI schemes (`http` and `https`) and requires a syntactically valid non-empty authority. The authority span, not `Host`, is the effective target authority at request-parser level.

## `http/http_range.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_http_range_parse` | valid field-value span | rejects over-domain spans; distinguishes one byte range, multiple valid byte ranges, syntactically valid unsupported units, and malformed input; unknown units still require a valid generic non-empty range-set; RFC list empty members are ignored; output spec only meaningful for SINGLE | at most `CERV_FIELD_LINE_MAX` bytes |
| `cerv_http_range_normalize` | valid range kind; arbitrary integer members and representation length | returns satisfiable selection or unsatisfiable; success guarantees `start <= end < length` and `count == end - start + 1` | O(1) |

## `http/http_request.h`

`struct cerv_http_request` contains borrowed spans into the original request buffer. The caller must keep those bytes alive and unchanged while using the parsed request. No arbitrary header map is retained.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_http_request_line_parse` | one request-line span without CRLF | explicit parser result; initializes output before parsing; on OK all stored spans lie inside the supplied line | inspects at most `CERV_REQUEST_LINE_MAX + 1` bytes; over-limit classification is deterministic |
| `cerv_http_request_parse` | received byte buffer for one connection; `received` may exceed structural maximum so the parser can classify overflow | explicit incomplete/OK/protocol/limit result; writes `out` only on OK; on OK `header_bytes == received`, field count is bounded, no body/extra bytes exist, all spans borrow the input; duplicate `Range` field lines are rejected as ambiguous singleton-field combinations | scans at most `CERV_REQUEST_BYTES_MAX + 1`; request line/field line/count exact bounds enforced |
| `cerv_http_request_if_none_match_matches` | successfully parsed request and current entity tag | weak-matches across all stored If-None-Match field lines; false for NULL request | at most `CERV_FIELD_COUNT_MAX` fields and field-line bounded spans |

Parser status precedence is deterministic: syntax/framing and structural-limit failures are resolved before unsupported expectation/method semantics where both are present. Any `Transfer-Encoding`, duplicate `Content-Length`, nonzero/malformed `Content-Length`, or bytes after the canonical header terminator are rejected.

## `base/invariant.h`

| Function | Accepted input | Result / preserved invariant | Bound / side effect |
| --- | --- | --- | --- |
| `cerv_invariant_fail` | internal impossible state only; never attacker-input validation | does not return; fail-stop via `abort()` | O(1); may terminate process; no ownership transfer |
| `CERV_INVARIANT(expr)` | internal boolean invariant | evaluates expression once; continues if true, otherwise calls `cerv_invariant_fail` | always enabled regardless of `NDEBUG` |


## `linux/clock.h`

This is a concrete Linux time-acquisition boundary. It does not replace the pure arithmetic in `base/cerv_time.h`.

| Function | Accepted input | Result / preserved invariant | Side effect / bound |
| --- | --- | --- | --- |
| `cerv_clock_mono_now` | non-NULL output; Linux signed `time_t` fitting `int64_t` | reads `CLOCK_MONOTONIC`, validates timespec fields, checked-converts to scalar nanoseconds | one `clock_gettime()`; no allocation |
| `cerv_clock_unix_now` | non-NULL output; Linux signed `time_t` fitting `int64_t` | reads `CLOCK_REALTIME` and returns whole Unix seconds | one `clock_gettime()`; no allocation |

## `linux/fs.h`

This is the sole request-time pathname-to-kernel boundary. It is Linux-specific and intentionally owns descriptor operations. Request-time file resolution never falls back from `openat2()` to a weaker pathname algorithm.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_fs_root_open` | non-NULL, non-empty startup path; non-NULL output | opens and descriptor-validates a directory root; probes required `openat2()` confinement flags; returns `UNSUPPORTED` rather than weakening containment | on OK, output owns one root FD until `cerv_fs_root_close`; startup/config path is trusted operator input |
| `cerv_fs_root_close` | NULL or initialized/closed root | idempotently closes an owned root FD and stores `-1` | consumes owned root FD if present |
| `cerv_fs_open_regular` | live root; NUL-terminated non-empty relative path of at most `CERV_REPRESENTATION_PATH_BYTES_MAX`; non-NULL output | boundedly validates the string before syscall; resolves with `RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS`; retries `EAGAIN` at most three attempts; opens nonblocking to avoid FIFO-open stalls; accepts only descriptor-verified regular files; returns explicit filesystem class | on OK, output owns exactly one FD; on every failure output FD is `-1`; pathname pre-scan is bounded by representation path maximum |
| `cerv_fs_file_close` | NULL or initialized/closed file | idempotently closes an owned selected-file FD and stores `-1` | consumes owned FD if present |
| `cerv_fs_classify_errno` | any integer errno value | deterministic filesystem classification independent of HTTP serialization | O(1), no ownership |

Normalized metadata fields (`size`, mtime seconds and nanoseconds) are captured from the same opened descriptor later used for transfer. The build statically requires signed `off_t`/`time_t` and metadata widths representable by the fixed normalized types.

## `serve/media_type.h`

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_media_type_for_path` | valid logical `struct cerv_path` | returns a pointer to a compile-time constant media type derived from the logical extension; unknown/no extension returns `application/octet-stream`; never derives MIME from `.br`/`.gz` sidecars | scans only the bounded logical path; no allocation |

## `serve/representation.h`

`struct cerv_representation` owns `file.fd` while it is non-negative. Its metadata, length, encoding, MIME type, and ETag all describe that exact opened physical representation.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_representation_select` | live root, valid decoded logical path, initialized Accept-Encoding state, non-NULL output | hidden (dot-prefixed) paths return NOT_FOUND without touching the filesystem; otherwise considers identity/`.gz`/`.br`; honors qvalues before deterministic `br > gzip > identity` tie-break; optional sidecar absence/policy/permission/nonregular failures can fall through to another acceptable representation; operational failures propagate; unsupported confinement remains an internal class that response planning treats as fail-stop; if existing forms are all explicitly unacceptable returns 406 class | fixed three candidates; on OK output owns one FD; on failure owns none |
| `cerv_representation_close` | NULL or initialized representation | closes any still-owned representation FD | consumes owned FD if present |
| `cerv_representation_take_fd` | initialized representation | returns current FD and stores `-1`; NULL returns `-1` | transfers ownership to caller exactly once |
| `cerv_representation_etag` | NULL or initialized representation | borrowed view of the bounded generated ETag; NULL yields empty span | O(1), no ownership transfer |
| `cerv_content_encoding_name` | encoding enum | static `gzip`/`br` literal or NULL for identity/unknown | O(1) |

The ETag wire format is a strong, quoted validator of at most 47 bytes: the size as 16 lowercase hex digits, `-`, mtime seconds as 16 hex digits, `-`, mtime nanoseconds as 8 hex digits, then `-gz` or `-br` for sidecar encodings and nothing for identity. Signed seconds are serialized by their defined `uint64_t` modulo conversion. The format is ASCII-only, fixed-width per field, architecture-independent over the normalized metadata domain, representation-specific, and independent of device, inode, and ctime so that every replica serving the same tree emits the same validator.

## `serve/response.h`

Response planning is pure with respect to networking: it performs no socket or filesystem lookup and allocates no memory. It borrows request/representation state for the call only. It does not own or transfer the representation FD.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_response_plan_resource` | parsed GET/HEAD request; classified representation result; live representation when result is OK; representable response time; non-NULL output | deterministically produces 200/206/304/416 or mapped 4xx/5xx plan; conditionals precede ranges per HTTP rules; ranges address selected-representation bytes; HEAD/304 never send content; all arithmetic and `off_t` conversion checked | serialization never exceeds `CERV_RESPONSE_BYTES_MAX`; no socket/file I/O; representation FD remains caller-owned |
| `cerv_response_plan_error` | supported context-free error status except 416; HEAD flag; representable response time | fixed non-reflective error body and exact Content-Length; 405 adds Allow, 503 adds bounded Retry-After/no-store; 416 is rejected because it requires current selected-representation length and is built only by resource planning | fixed response buffer and compile-time body |
| `cerv_response_status_from_parse` | parser result enum | deterministic parser-result to response-status mapping; non-error parser states map to internal 500 sentinel and are not valid runtime error inputs | O(1) |

Cerv emits `HTTP/1.1` response status lines for accepted HTTP/1.0 and HTTP/1.1 requests. All current responses use close-delimited connection lifetime plus exact Content-Length where content exists and never use transfer coding, so the wire form remains interpretable by HTTP/1.0 recipients. This policy is frozen and tested.

Last-Modified is derived from the selected descriptor mtime at second precision and clamps future mtimes to the response Date. `If-Range` honors a range only for a strong entity-tag equal to the selected representation's ETag. Date-form `If-Range` is always false because Cerv's metadata model does not establish the HTTP strong-validator requirement that the representation could not have changed twice within the represented second.

## `runtime/timer.h`

`struct cerv_timer_heap` borrows caller-provided node storage for its entire initialized lifetime and never allocates. Each active logical timer is linked through exactly one caller-owned `struct cerv_timer_link`; the link stores either its current heap index or `CERV_TIMER_NOT_IN_HEAP`. Heap order is deterministic by `(deadline_ns, slot_index, generation)`.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_timer_link_init` | NULL or caller-owned link | stores the explicit not-in-heap marker | O(1); no ownership |
| `cerv_timer_heap_init` | non-NULL heap; storage present unless capacity zero | initializes empty fixed-capacity heap borrowing storage | O(1); no allocation |
| `cerv_timer_set` | initialized heap; valid link not present or present in this heap; slot/generation/deadline values | inserts or updates exactly one node and restores heap order | O(log N), capacity-bounded; no allocation |
| `cerv_timer_remove` | initialized heap; valid link absent or correctly linked into heap | idempotent for absent link; removes linked node and restores heap; link becomes not-in-heap | O(log N) |
| `cerv_timer_peek` | initialized heap and non-NULL output | copies earliest node; false for empty heap | O(1); no ownership |
| `cerv_timer_pop` | initialized non-empty heap and non-NULL output | copies and removes earliest node; removed link becomes not-in-heap | O(log N) |
| `cerv_timer_heap_valid` | initialized heap | verifies capacity, links, indices, and parent/child order without overflow-prone child arithmetic | O(N), used by tests/fuzz/proof rather than request dispatch |

The worker allocates exactly one timer capacity entry per connection slot and maintains at most one active heap node per live connection.

## `runtime/capacity.h`

Runtime capacity formulas live with the concrete runtime representations whose sizes they measure; startup configuration does not include connection/timer internals.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_runtime_worker_memory_bytes` | positive local slots | checked bytes for `slots * (sizeof(cerv_conn)+sizeof(cerv_timer_node))` | O(1), fails on overflow |
| `cerv_runtime_required_worker_fds` | positive local slots | checked conservative `2*slots + CERV_FD_SAFETY_MARGIN` | O(1), fails on overflow |
| `cerv_runtime_auto_connections` | positive resolved workers/target; finite `nofile_soft` or explicit infinity | computes the largest service-wide total <= target whose largest deterministic worker partition fits the current FD budget; fails if even one slot/worker cannot fit | pure O(1); no dynamic allocation or runtime slot growth |

## `runtime/conn.h`

The connection layer owns one accepted client socket and, after successful response planning transfers it, at most one selected representation FD. It performs bounded nonblocking socket/file transfer syscalls but does not register epoll interest, allocate memory, or own the filesystem root.

`struct cerv_conn` has explicit states: `FREE`, `RECV_HEADERS`, `SEND_HEADERS`, `SEND_BODY`, `SEND_FILE`, and `SEND_FALLBACK`. Close is a direct terminal result; there is no persistent closing state. The fixed request buffer is reused as fallback transfer scratch only after request parsing/planning has finished.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_conn_arena_init` | caller storage; capacity <= `UINT32_MAX` | initializes intrusive free-slot chain; generations start at zero/reserved | O(N) startup only; borrows slots |
| `cerv_conn_arena_acquire` | initialized arena, non-NULL output | removes one free slot, increments generation, returns FULL when none; refuses reuse after a slot reaches `UINT32_MAX` generation rather than wrapping | O(1); generation exhaustion is internal fail-stop class |
| `cerv_conn_arena_release` | exact arena-owned slot already cleaned to FREE, no FDs/timer | returns slot to free chain exactly once | O(1) |
| `cerv_conn_token` | non-NULL slot | encodes generation in high 32 bits and index in low 32 bits; NULL yields reserved listener token value | O(1) |
| `cerv_conn_arena_lookup_token` | initialized arena and any token | returns active slot only when index is in range, generation is nonzero/current, and state is non-FREE; stale tokens return NULL | O(1) |
| `cerv_conn_begin` | acquired FREE slot, owned accepted socket FD, nonzero durations | transfers socket ownership into slot and freezes absolute accept/header/lifetime timestamps | O(1) |
| `cerv_conn_cleanup` | NULL or any initialized slot | closes owned socket/file FDs and resets runtime state while preserving index/generation/free-link identity | bounded; consumes owned FDs |
| `cerv_conn_next_deadline` | active connection, output | returns min of lifetime and state-relevant header/write deadline | O(1) |
| `cerv_conn_should_close_response` | request close flag, resource success class, completed-request count | pure bounded persistence decision; close for explicit close/error/final allowed request | O(1) |
| `cerv_conn_on_readable` | RECV_HEADERS connection, borrowed live root, optional borrowed SPA fallback path, nonzero write timeout | performs at most `CERV_SOCKET_IO_QUANTUM` recv attempts into fixed request storage; incrementally recognizes CRLFCRLF; composes parser→path→representation→response; on representation `NOT_FOUND` only, and only for a directory-shaped or extensionless path, may retry the configured fallback path; returns KEEP/CLOSE/FATAL | no allocation; header deadline is never reset by bytes received; fallback never masks non-404 classes |
| `cerv_conn_on_writable` | sending connection, nonzero header/write timeouts | progresses fixed headers/body/file/fallback state with partial-I/O handling; positive writes reset write deadline; EAGAIN/EINTR/no-progress do not; a reusable completed response returns to a cleared RECV_HEADERS state with a fresh per-request header deadline | at most `CERV_SOCKET_IO_QUANTUM` transfer syscalls and `CERV_FILE_SEND_QUANTUM` file bytes per call |
| `cerv_conn_on_deadline` | active connection and current monotonic/wall time | hard lifetime closes first; expired receive-header deadline installs 408; expired write no-progress deadline closes | O(1); 408 uses fixed response planner |

The runtime permits at most `CERV_KEEPALIVE_REQUESTS_MAX` (64) sequential HTTP/1.1 requests on one admitted socket. HTTP/1.0, `Connection: close`, the final permitted request, parser/resource/internal errors, deadlines, and transfer errors close. A persistent successful response closes any selected file FD, clears request/response/transfer scratch, increments the bounded request count, preserves the original absolute lifetime deadline, installs a fresh per-request header deadline, and returns to `RECV_HEADERS`. Bytes after the canonical header terminator are still rejected by each strict parser invocation; Cerv does not promise request pipelining. `sendfile()` unsupported errors (`EINVAL`/`ENOSYS`) transition only to the bounded fallback state; arbitrary transfer errors do not.

## `runtime/worker.h`

A worker borrows the listening socket and filesystem root. It owns one epoll FD and the active connection socket/file FDs through its connection arena. Connection/timer storage is caller-provided fixed capacity. There is no accepted-request queue.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_worker_prepare_process` | process before serving traffic | installs process-wide `SIG_IGN` for SIGPIPE so `sendfile()` peer disconnect cannot terminate the worker | O(1), process-global signal disposition side effect |
| `cerv_worker_config_default` | non-NULL output | fills code-defined header/write/lifetime defaults, mutable-cache baseline, disabled SPA fallback, and disables multiworker shared-listener cooperation | O(1) |
| `cerv_worker_init` | live regular filesystem root; listening `SOCK_STREAM` FD already `O_NONBLOCK|FD_CLOEXEC`; fixed slot/timer storage; positive durations | enforces SIGPIPE policy, initializes arena/timer heap, creates CLOEXEC epoll FD, registers borrowed listener level-triggered with `EPOLLIN|EPOLLEXCLUSIVE` | owns epoll FD on success; borrows listener/root/storage |
| `cerv_worker_set_control_fd` | initialized worker; distinct nonblocking/CLOEXEC control FD not previously set | registers fixed control token for readable lifecycle notifications | borrows control FD; worker destroy does not close it |
| `cerv_worker_run_once` | initialized worker; wait >= -1 | expires due timers, waits level-triggered epoll, pre-recognizes control readiness, dispatches at most `CERV_EPOLL_BATCH_MAX` returned events and at most `CERV_ACCEPT_QUANTUM` accept attempts for a listener event, then expires timers again | no allocation; returns explicit OK/resource-exhausted/control-ready/fatal class |
| `cerv_worker_stop_accepting` | initialized worker | idempotently removes listener from epoll, prevents new admission, and marks every live connection close-after-response so persistence cannot extend drain | O(capacity); borrowed listener remains open; in-flight responses may finish and idle reusable sockets close after their next response |
| `cerv_worker_active_connections` | NULL or initialized worker | returns current arena occupancy; NULL returns zero | O(1) |
| `cerv_worker_destroy` | NULL or initialized/partially live worker | removes/closes all live connection-owned FDs and worker epoll FD; does not close borrowed listener/root | O(capacity); no allocation |

Accepted sockets are created with `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)`. In single-worker mode, an already-accepted overflow socket receives at most one immediate `MSG_DONTWAIT|MSG_NOSIGNAL` 503 send attempt and is closed without epoll registration or arena admission. In multiworker mode, `shared_listener_cooperative` instead removes the shared listener before accepting beyond a full local partition, re-arms on slot release, and boundedly DEL/ADD yields the listener after successful accept work while capacity remains. Linux transient accept errors are retried within the dispatch budget; local FD/memory/socket-buffer exhaustion returns `CERV_WORKER_RESOURCE_EXHAUSTED` to the supervisor.

The worker is the only module that mutates epoll registrations. Every connection event token is generation-checked before use. Each KEEP transition synchronizes both state-derived epoll interest and the connection's single next-deadline timer. Expired timers are processed before and after the epoll dispatch so continuous readiness cannot indefinitely defer timeout enforcement.


## `process/config.h`

Configuration is immutable after parsing succeeds. Cerv has one strict configuration grammar with three precedence levels: built-in defaults, native environment values, then CLI values. CLI `ROOT`/option strings and environment `CERV_ROOT` are borrowed from host-provided NUL-terminated storage for process lifetime; the optional SPA fallback is instead validated and copied into the fixed `struct cerv_config` buffer. Automatic worker discovery is delegated to `process/config_workers.h`.

| Function | Accepted input | Result / preserved invariant | Side effect / bound |
| --- | --- | --- | --- |
| `cerv_config_parse` | ordinary argc/argv; non-NULL output; optional fixed error buffer | CLI-only deterministic wrapper used by tests/fuzzers; exact grammar; unknown/duplicate singleton options rejected; numeric IPv4/bracketed IPv6 only; checked positive durations | startup only; may call automatic worker discovery; borrows ROOT text |
| `cerv_config_parse_with_env` | ordinary argc/argv; optional immutable `cerv_config_env`; non-NULL output | applies defaults < environment < CLI; accepts `N|auto` for workers/max-connections; validates fixed SPA fallback; explicit numeric max remains hard operator request | startup only; may call automatic worker discovery; no retained heap state |
| `cerv_config_env_read_process` | non-NULL output | snapshots pointers returned by `getenv` for the documented `CERV_*` names; does not parse or copy their values | startup only; borrowed process-environment storage |
| `cerv_config_partition_slots` | positive total/workers; workers <= total; index < workers | deterministic quotient/remainder partition; partitions differ by at most one | O(1) |
| help/version text accessors | none | return immutable internal literals; version is compiled from the repository `VERSION` value | O(1); borrowed storage |

## `process/config_workers.h`

Automatic worker discovery is startup-only Linux work and may temporarily allocate/free a bounded dynamic CPU-affinity mask; it is not reachable from request service.

| Function | Accepted input | Result / preserved invariant | Side effect / bound |
| --- | --- | --- | --- |
| `cerv_config_detect_auto_workers` | non-NULL output | affinity-derived count capped by visible finite cgroup-v2 CPU quotas and `CERV_WORKERS_MAX`, minimum one | startup dynamically allocates/frees a bounded affinity mask up to `CERV_AFFINITY_CPU_PROBE_MAX`, then reads `/proc/self/cgroup` and bounded `/sys/fs/cgroup/.../cpu.max` files |
| `cerv_config_parse_cpu_max` | non-empty span shorter than 96 bytes; non-NULL outputs | accepts exactly two whitespace-separated tokens: `max` or positive decimal quota, then positive decimal period; trailing non-whitespace is rejected | pure, bounded, transactional outputs |
| `cerv_config_auto_worker_count` | positive affinity count; optional positive quota/period | pure integer ceiling quota cap with minimum one and worker hard cap | O(1); no I/O |

## `process/diag.h`

Diagnostics are master/startup lifecycle output, never a request path. Records use fixed storage capped by `CERV_DIAG_BYTES_MAX`; attacker-controlled request bytes are not emitted. Preparation obtains a separate nonblocking non-regular stderr sink or disables diagnostics; regular-file stderr is intentionally rejected. Each record performs one write attempt only. Failure/drop has no control-flow effect on serving correctness.

| Function | Accepted input | Result / preserved invariant | Bound |
| --- | --- | --- | --- |
| `cerv_diag_message` | nullable internal level/event/detail strings | formats one bounded `cerv key=value` line; truncates with suffix if needed | <= 512-byte stack record; best-effort stderr |
| `cerv_diag_worker_exit` | reaped child PID/status | emits bounded exit/signal classification | O(1) |
| `cerv_diag_startup` | derived startup counts | emits bounded resource/startup summary | O(1) |

## `process/supervisor.h`

The supervisor is a process-lifecycle owner, not a request dispatcher. `cerv_supervisor_run` is intended as the terminal service run function: it establishes process signal policy, creates/forks the fixed topology, and returns only after all workers it created are reaped. Lower layers do not retain supervisor pointers.

| Function | Accepted input | Result / preserved invariant | Ownership / bound |
| --- | --- | --- | --- |
| `cerv_supervisor_resource_plan` | validated positive workers and **resolved numeric** max-connections | derives largest local partition, checked worker storage bytes, conservative worker FD requirement, reads `RLIMIT_NOFILE`; false if unsupported | startup-only; no allocation; largest-partition bound |
| `cerv_supervisor_run` | fully parsed immutable config | resolves automatic max-connections against current `RLIMIT_NOFILE`, then returns 0 only for clean requested lifecycle and 1 for startup/runtime fatal; never returns with a known created worker still alive | master opens/owns startup root/listener/signalfd/readiness pipe; forks <= `CERV_WORKERS_MAX`; children allocate only before ready; no request dispatch |

Supervisor startup uses one CLOEXEC readiness pipe carrying one fixed worker-index/security record per child. The master read end is nonblocking; each child makes only its private write end blocking before seccomp so bounded atomic readiness records cannot fail spuriously on pipe-capacity EAGAIN. The pipe is closed before steady state. Workers close the inherited master `signalfd`, create their own TERM/INT `signalfd`, inherit root/listener descriptor references, allocate exact fixed slot/timer arrays, initialize the worker, report ready, and then serve. The master closes its root/listener copies only after all workers are ready. The complete readiness wait has the absolute `CERV_STARTUP_READY_TIMEOUT_MS` (10-second baseline) monotonic deadline; expiry force-kills/reaps incomplete startup.

The first SIGTERM/SIGINT starts drain and sends SIGTERM to live workers. A second termination signal or shutdown-deadline expiry sends SIGKILL. Unexpected steady-state worker death marks service fatal, initiates peer shutdown, and yields final status 1. Every abnormal master path force-kills/reaps remaining known workers before return; there is no in-master respawn loop.


## `linux/sandbox.h` / `linux/seccomp_filters.h`

Sandboxing is defense in depth above the mandatory filesystem/parser contracts. Release workers establish `no_new_privs`, attempt read-only-root Landlock, then install the worker seccomp allowlist before reporting ready. Release masters establish `no_new_privs` and the smaller lifecycle seccomp allowlist after startup-only descriptors/work are complete. Seccomp validates the audit architecture and defaults to process kill. Landlock ABI-probe `ENOSYS`, `EOPNOTSUPP`, or `EPERM` is treated as explicit unavailability and is nonfatal; `EPERM` covers outer container/seccomp policies that deny this optional probe. Other Landlock setup failures are fatal. Active sandbox installation is compiled out only for explicitly instrumented sanitizer/coverage verification builds whose runtimes require unrelated syscalls; ordinary test/release gates exercise the exact production filters.
