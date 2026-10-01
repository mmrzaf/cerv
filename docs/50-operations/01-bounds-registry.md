# Bounds Registry

> **Status:** Normative  
> **Specification:** Cerv Engineering Specification (CES) 1.0  
> **Scope:** Canonical registry of structural and runtime bounds.  

A single machine-readable/header-level bounds registry SHOULD eventually drive both code and generated documentation where possible.

| **Identifier** | **Meaning** | **Baseline value** |
| --- | --- | --- |
| CERV_REQUEST_BYTES_MAX | Complete request header storage | 16384 |
| CERV_REQUEST_LINE_MAX | Request line | 4096 |
| CERV_FIELD_LINE_MAX | One header field line | 8192 |
| CERV_FIELD_COUNT_MAX | Header field lines | 64 |
| CERV_RESPONSE_BYTES_MAX | Constructed response headers | 4096 |
| CERV_ERROR_BODY_MAX | Constant error body | 256 |
| CERV_EPOLL_BATCH_MAX | Events returned per wait | 256 |
| CERV_ACCEPT_QUANTUM | Accepted sockets per listener dispatch | 64 |
| CERV_ACCEPT_BACKOFF_MS | Interval a worker stops accepting after local descriptor/memory exhaustion before re-arming its listener | 100 ms |
| CERV_FILE_SEND_QUANTUM | File bytes attempted per connection dispatch | 1 MiB |
| CERV_SOCKET_IO_QUANTUM | Receive/send/pread/sendfile progress syscall attempts per connection dispatch | 64 |
| CERV_KEEPALIVE_REQUESTS_MAX | Maximum sequential requests on one HTTP/1.1 connection | 64 |
| CERV_LISTEN_BACKLOG | Requested Linux listen backlog | 1024 |
| CERV_WORKERS_MAX | Fixed worker-count hard cap | 1024 |
| CERV_AFFINITY_CPU_PROBE_MAX | Maximum dynamically probed Linux affinity-mask CPU span | 1048576 |
| CERV_DIAG_BYTES_MAX | Lifecycle diagnostic record storage | 512 bytes |
| CERV_FD_SAFETY_MARGIN | Fixed non-slot worker FD allowance | 16 |
| CERV_DEFAULT_MAX_CONNECTIONS | Automatic service-wide slot target/ceiling before `RLIMIT_NOFILE` capping | 4096 |
| CERV_DEFAULT_HEADER_TIMEOUT_MS | Per-request headers-complete deadline; first starts at accept, reuse starts at prior-response completion | 5000 ms |
| CERV_DEFAULT_WRITE_TIMEOUT_MS | No-progress interval | 30000 ms |
| CERV_DEFAULT_MAX_LIFETIME_MS | Accept-to-forced-close | 300000 ms |
| CERV_DEFAULT_SHUTDOWN_TIMEOUT_MS | Drain deadline | 30000 ms |
| CERV_STARTUP_READY_TIMEOUT_MS | Absolute post-fork worker readiness deadline | 10000 ms |

These values SHALL be defined once in code/configuration and consumed by tests/documentation rather than duplicated as magic numbers. Any change requires an explicit specification update, exact boundary tests, and measured justification.
