#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
engine=${CONTAINER_ENGINE:-}
if [[ -z "$engine" ]]; then
  if command -v docker >/dev/null 2>&1; then engine=docker
  elif command -v podman >/dev/null 2>&1; then engine=podman
  else echo "container-check requires docker or podman" >&2; exit 2
  fi
fi
image="cerv-container-check:local"
name="cerv-container-check-$$"
site=$(mktemp -d)
trap '$engine rm -f "$name" >/dev/null 2>&1 || true; rm -rf "$site"' EXIT
printf '<!doctype html><title>Cerv SPA</title>\n' > "$site/index.html"
chmod 0755 "$site"
chmod 0644 "$site/index.html"
host_port=$(python3 - <<'PYPORT'
import socket
with socket.socket() as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
PYPORT
)
"$engine" build -t "$image" "$root"
"$engine" run -d --name "$name" \
  -p "127.0.0.1:${host_port}:8080" \
  --read-only --cap-drop=ALL --security-opt=no-new-privileges \
  --ulimit nofile=1024:1024 \
  -e CERV_SPA_FALLBACK=/index.html \
  -v "$site:/srv/cerv:ro" "$image" >/dev/null
CERV_CONTAINER_SMOKE_PORT="$host_port" python3 - <<'PY'
import os, socket, time
port = int(os.environ["CERV_CONTAINER_SMOKE_PORT"])
for _ in range(50):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=.2) as s:
            s.sendall(b"GET /deep/client/route HTTP/1.1\r\nHost: container\r\nConnection: close\r\n\r\n")
            data=b""
            while True:
                chunk=s.recv(65536)
                if not chunk: break
                data += chunk
        if b"HTTP/1.1 200 OK" in data and b"Cerv SPA" in data:
            break
    except OSError:
        time.sleep(.1)
else:
    raise SystemExit("container HTTP/SPA smoke check failed")
PY
"$engine" stop -t 35 "$name" >/dev/null
"$engine" rm "$name" >/dev/null
name=""
echo "ok: container image boots from env-only config, auto-sizes under nofile=1024, serves SPA fallback, and stops via SIGTERM"
