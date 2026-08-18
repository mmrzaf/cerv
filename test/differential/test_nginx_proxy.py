#!/usr/bin/env python3
"""Framing differential through an established HTTP intermediary.

This is intentionally an integration harness, not a generic HTTP client.  It
sends exact byte strings directly to Cerv and through nginx and checks Cerv's
strict framing policy plus the proxy boundary's safe reject-or-canonicalize
behavior.
"""
from __future__ import annotations

import pathlib
import signal
import socket
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass


@dataclass(frozen=True)
class Case:
    name: str
    wire: bytes
    direct: int
    proxy: frozenset[int]


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def wait_listen(port: int, proc: subprocess.Popen[bytes]) -> None:
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError(f"process exited before listening on {port}: {proc.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.05):
                return
        except OSError:
            time.sleep(0.02)
    raise RuntimeError(f"timed out waiting for port {port}")


def request_status(port: int, wire: bytes) -> int:
    data = bytearray()
    with socket.create_connection(("127.0.0.1", port), timeout=1.0) as s:
        s.settimeout(2.0)
        s.sendall(wire)
        while len(data) < 64 * 1024:
            try:
                chunk = s.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            data.extend(chunk)
    first = bytes(data).split(b"\r\n", 1)[0]
    parts = first.split(b" ")
    if len(parts) < 2 or not parts[0].startswith(b"HTTP/") or len(parts[1]) != 3 or not parts[1].isdigit():
        raise AssertionError(f"no HTTP status from port {port}: {first!r}")
    return int(parts[1])


def stop(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=4.0)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=4.0)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_nginx_proxy.py CERV NGINX", file=sys.stderr)
        return 2
    cerv = pathlib.Path(sys.argv[1]).resolve()
    nginx = pathlib.Path(sys.argv[2]).resolve()
    cases = [
        Case("valid_origin", b"GET / HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n", 200, frozenset({200})),
        Case("valid_absolute_host_disagree", b"GET http://authority.example/ HTTP/1.1\r\nHost: other.example\r\nConnection: close\r\n\r\n", 200, frozenset({200})),
        Case("content_length_zero", b"GET / HTTP/1.1\r\nHost: example\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 200, frozenset({200})),
        # nginx may consume and canonicalize the zero-length chunked body before
        # forwarding, or reject it.  Direct Cerv must never accept TE itself.
        Case("transfer_encoding", b"GET / HTTP/1.1\r\nHost: example\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n0\r\n\r\n", 400, frozenset({200, 400})),
        Case("content_length_body", b"GET / HTTP/1.1\r\nHost: example\r\nContent-Length: 1\r\nConnection: close\r\n\r\nX", 400, frozenset({400})),
        Case("duplicate_content_length", b"GET / HTTP/1.1\r\nHost: example\r\nContent-Length: 0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("content_length_comma", b"GET / HTTP/1.1\r\nHost: example\r\nContent-Length: 0,0\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("content_length_plus", b"GET / HTTP/1.1\r\nHost: example\r\nContent-Length: +0\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("te_plus_cl", b"GET / HTTP/1.1\r\nHost: example\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\nConnection: close\r\n\r\n0\r\n\r\n", 400, frozenset({400})),
        Case("obs_fold", b"GET / HTTP/1.1\r\nHost: example\r\n X-Test: folded\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("whitespace_before_colon", b"GET / HTTP/1.1\r\nHost : example\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("missing_host", b"GET / HTTP/1.1\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        Case("duplicate_host_case", b"GET / HTTP/1.1\r\nHost: a.example\r\nhOsT: b.example\r\nConnection: close\r\n\r\n", 400, frozenset({400})),
        # nginx can safely interpret this as pipelining and close after the first
        # request; direct Cerv deliberately rejects any already-buffered tail.
        Case("extra_request_bytes", b"GET / HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\nGET / HTTP/1.1\r\nHost: example\r\nConnection: close\r\n\r\n", 400, frozenset({200, 400})),
    ]

    with tempfile.TemporaryDirectory(prefix="cerv-diff-") as tmp_text:
        tmp = pathlib.Path(tmp_text)
        (tmp / "index.html").write_bytes(b"cerv differential\n")
        backend_port = free_port()
        proxy_port = free_port()
        while proxy_port == backend_port:
            proxy_port = free_port()
        cerv_err = (tmp / "cerv.err").open("wb")
        nginx_err = (tmp / "nginx.stderr").open("wb")
        cerv_proc = subprocess.Popen(
            [str(cerv), "--listen", f"127.0.0.1:{backend_port}", "--workers", "1", "--max-connections", "32", str(tmp)],
            stdout=subprocess.DEVNULL,
            stderr=cerv_err,
        )
        nginx_proc: subprocess.Popen[bytes] | None = None
        try:
            wait_listen(backend_port, cerv_proc)
            conf = tmp / "nginx.conf"
            conf.write_text(
                f"pid {tmp / 'nginx.pid'};\n"
                f"error_log {tmp / 'nginx-error.log'} notice;\n"
                "events { worker_connections 256; }\n"
                "http {\n"
                "  access_log off;\n"
                f"  server {{ listen 127.0.0.1:{proxy_port};\n"
                "    location / {\n"
                "      proxy_http_version 1.1;\n"
                "      proxy_set_header Connection close;\n"
                f"      proxy_pass http://127.0.0.1:{backend_port};\n"
                "    }\n"
                "  }\n"
                "}\n"
            )
            nginx_proc = subprocess.Popen(
                [str(nginx), "-c", str(conf), "-p", str(tmp), "-g", "daemon off; master_process off;"],
                stdout=subprocess.DEVNULL,
                stderr=nginx_err,
            )
            wait_listen(proxy_port, nginx_proc)
            checks = 0
            for case in cases:
                direct = request_status(backend_port, case.wire)
                checks += 1
                if direct != case.direct:
                    raise AssertionError(f"{case.name}: direct Cerv expected {case.direct}, got {direct}")
                proxied = request_status(proxy_port, case.wire)
                checks += 1
                if proxied not in case.proxy:
                    allowed = ",".join(str(x) for x in sorted(case.proxy))
                    raise AssertionError(f"{case.name}: nginx->Cerv expected one of {{{allowed}}}, got {proxied}")
            print(f"nginx differential: {checks} framing/parser checks passed")
            return 0
        finally:
            if nginx_proc is not None:
                stop(nginx_proc)
            stop(cerv_proc)
            cerv_err.close()
            nginx_err.close()


if __name__ == "__main__":
    raise SystemExit(main())
