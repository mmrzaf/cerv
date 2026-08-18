#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CERV=${1:-"$ROOT_DIR/build/release/cerv"}
TRACE=${2:-"$ROOT_DIR/build/tools/trace_syscalls"}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cerv-syscall-audit-XXXXXX")
TRACE_PID=
MASTER_PID=
cleanup() {
    set +e
    if [[ -n ${MASTER_PID:-} ]] && kill -0 "$MASTER_PID" 2>/dev/null; then kill -KILL "$MASTER_PID" 2>/dev/null; fi
    if [[ -n ${TRACE_PID:-} ]] && kill -0 "$TRACE_PID" 2>/dev/null; then kill -KILL "$TRACE_PID" 2>/dev/null; fi
    wait "$TRACE_PID" 2>/dev/null || true
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

python3 - "$TMP/large.bin" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
p.write_bytes((b"0123456789abcdef" * 131072))
(p.parent / "index.html").write_bytes(b"cerv syscall audit\n")
PY

PORT=$(python3 - <<'PY'
import socket
with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
PY
)

mkdir -p "$ROOT_DIR/build/tools" "$ROOT_DIR/build/audit"
"$TRACE" "$TMP/observed.txt" "$TMP/master.pid" -- "$CERV" \
    --listen "127.0.0.1:$PORT" --workers 1 --max-connections 16 "$TMP" \
    >"$TMP/stdout" 2>"$TMP/stderr" &
TRACE_PID=$!

for _ in $(seq 1 500); do
    if [[ -s "$TMP/master.pid" ]]; then MASTER_PID=$(cat "$TMP/master.pid"); break; fi
    sleep 0.01
done
[[ -n ${MASTER_PID:-} ]] || { echo "ERROR: tracer did not report master pid" >&2; exit 1; }

python3 - "$PORT" <<'PY'
import socket, sys, time
port = int(sys.argv[1])
for _ in range(500):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.05):
            break
    except OSError:
        time.sleep(0.01)
else:
    raise SystemExit("Cerv did not become ready")

def req(wire: bytes) -> bytes:
    data = bytearray()
    with socket.create_connection(("127.0.0.1", port), timeout=1.0) as s:
        s.settimeout(2.0)
        s.sendall(wire)
        while True:
            part = s.recv(65536)
            if not part:
                break
            data.extend(part)
    return bytes(data)

cases = [
    (b"GET / HTTP/1.1\r\nHost: audit\r\nConnection: close\r\n\r\n", b" 200 "),
    (b"HEAD / HTTP/1.1\r\nHost: audit\r\nConnection: close\r\n\r\n", b" 200 "),
    (b"GET /large.bin HTTP/1.1\r\nHost: audit\r\nRange: bytes=17-65552\r\nConnection: close\r\n\r\n", b" 206 "),
    (b"GET /missing HTTP/1.1\r\nHost: audit\r\nConnection: close\r\n\r\n", b" 404 "),
    (b"GET / HTTP/1.1\r\nHost: audit\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n0\r\n\r\n", b" 400 "),
]
for wire, expected in cases:
    response = req(wire)
    first = response.split(b"\r\n", 1)[0]
    if expected not in first:
        raise SystemExit(f"unexpected response {first!r}")
PY

# Verify that both master and worker reached the intended kernel state while live.
grep -q '^NoNewPrivs:[[:space:]]*1$' "/proc/$MASTER_PID/status"
grep -q '^Seccomp:[[:space:]]*2$' "/proc/$MASTER_PID/status"
WORKER_PID=$(ps -eo pid=,ppid= | awk -v master="$MASTER_PID" '$2 == master {print $1; exit}')
[[ -n "$WORKER_PID" ]]
grep -q '^NoNewPrivs:[[:space:]]*1$' "/proc/$WORKER_PID/status"
grep -q '^Seccomp:[[:space:]]*2$' "/proc/$WORKER_PID/status"

kill -TERM "$MASTER_PID"
wait "$TRACE_PID"
TRACE_PID=
MASTER_PID=

cp "$TMP/observed.txt" "$ROOT_DIR/build/audit/syscalls.txt"

master_allowed='read write close poll ppoll wait4 waitid kill clock_gettime rt_sigreturn exit exit_group'
worker_allowed='read write close epoll_wait epoll_pwait epoll_pwait2 epoll_ctl accept4 recvfrom recvmsg sendto sendmsg sendfile openat2 fstat newfstatat statx pread64 clock_gettime rt_sigreturn munmap brk madvise exit exit_group'
check_line() {
    local label=$1 allowed=$2 line token
    line=$(grep "^$label" "$TMP/observed.txt")
    [[ -n "$line" ]]
    for token in ${line#*:}; do
        case " $allowed " in
            *" $token "*) ;;
            *) echo "ERROR: $label observed syscall outside reviewed profile: $token" >&2; exit 1 ;;
        esac
    done
}
check_line 'master-post-seccomp:' "$master_allowed"
check_line 'worker-post-seccomp:' "$worker_allowed"

echo "ok: measured post-seccomp syscall vocabulary"
cat "$TMP/observed.txt"
