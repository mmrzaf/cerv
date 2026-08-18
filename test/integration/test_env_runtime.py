#!/usr/bin/env python3
"""End-to-end environment/auto-capacity/SPA regression for the real Cerv executable."""
from __future__ import annotations

import os
import resource
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def low_nofile() -> None:
    resource.setrlimit(resource.RLIMIT_NOFILE, (1024, 1024))


def request(port: int) -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
        sock.sendall(
            b"GET /client/deep/route HTTP/1.1\r\n"
            b"Host: env-test\r\n"
            b"Connection: close\r\n\r\n"
        )
        data = bytearray()
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                return bytes(data)
            data.extend(chunk)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_env_runtime.py CERV")
    cerv = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="cerv-env-test-") as root:
        Path(root, "index.html").write_text("<!doctype html><title>env spa</title>\n", encoding="utf-8")
        port = free_port()
        env = os.environ.copy()
        env.update(
            CERV_LISTEN=f"127.0.0.1:{port}",
            CERV_ROOT=root,
            CERV_WORKERS="1",
            CERV_MAX_CONNECTIONS="auto",
            CERV_SPA_FALLBACK="/index.html",
        )
        proc = subprocess.Popen(
            [cerv],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=low_nofile,
        )
        try:
            response = b""
            for _ in range(100):
                if proc.poll() is not None:
                    break
                try:
                    response = request(port)
                    break
                except OSError:
                    time.sleep(0.02)
            if b"HTTP/1.1 200 OK\r\n" not in response or b"env spa" not in response:
                raise AssertionError("env-only SPA request did not succeed")
            proc.terminate()
            if proc.wait(timeout=5) != 0:
                raise AssertionError("env-only Cerv did not shut down cleanly")
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            if "max_connections=504" not in stderr or "required_worker_fds=1024" not in stderr:
                raise AssertionError(f"auto capacity did not resolve against RLIMIT_NOFILE: {stderr!r}")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()

        # Numeric capacity is an explicit hard request and must not silently shrink.
        port = free_port()
        env.update(CERV_LISTEN=f"127.0.0.1:{port}", CERV_MAX_CONNECTIONS="4096")
        failed = subprocess.run(
            [cerv],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=low_nofile,
            timeout=5,
            check=False,
        )
        if failed.returncode != 1:
            raise AssertionError(f"explicit impossible connection cap returned {failed.returncode}")
        if "required_worker_fds=8208" not in failed.stderr or "nofile_soft=1024" not in failed.stderr:
            raise AssertionError(f"resource diagnostic missing FD math: {failed.stderr!r}")
    print("env runtime integration: auto capacity + SPA + strict numeric cap passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
