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


def request(port: int, target: str = "/client/deep/route") -> bytes:
    with socket.create_connection(("127.0.0.1", port), timeout=0.5) as sock:
        sock.sendall(
            f"GET {target} HTTP/1.1\r\n".encode("ascii")
            + b"Host: env-test\r\n"
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
        Path(root, ".env").write_text("SECRET=1\n", encoding="utf-8")
        Path(root, ".well-known").mkdir()
        Path(root, ".well-known", "security.txt").write_text("Contact: mailto:security@example.com\n", encoding="utf-8")
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
            # Policy through the real sandboxed executable: assets 404, dotfiles hidden, .well-known public.
            missing_asset = request(port, "/assets/missing.js")
            if b"HTTP/1.1 404 Not Found\r\n" not in missing_asset or b"env spa" in missing_asset:
                raise AssertionError("a missing asset must be a real 404, not the SPA shell")
            for target in ("/.env", "/%2eenv", "/.well-known/.secret"):
                hidden = request(port, target)
                if b"HTTP/1.1 404 Not Found\r\n" not in hidden or b"SECRET" in hidden:
                    raise AssertionError(f"hidden path {target} was not a plain 404: {hidden!r}")
            public = request(port, "/.well-known/security.txt")
            if b"HTTP/1.1 200 OK\r\n" not in public or b"Contact:" not in public:
                raise AssertionError("/.well-known/ must stay public")
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

        # --landlock require: serve when the kernel provides Landlock, otherwise refuse to start.
        landlock_on = "landlock=on" in stderr
        port = free_port()
        env.update(CERV_LISTEN=f"127.0.0.1:{port}", CERV_MAX_CONNECTIONS="auto", CERV_LANDLOCK="require")
        required = subprocess.Popen(
            [cerv],
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            if landlock_on:
                answer = b""
                for _ in range(100):
                    if required.poll() is not None:
                        break
                    try:
                        answer = request(port, "/hello-required")
                        break
                    except OSError:
                        time.sleep(0.02)
                if b"HTTP/1.1 200 OK\r\n" not in answer:
                    raise AssertionError("--landlock require did not serve on a Landlock-capable kernel")
                required.terminate()
                if required.wait(timeout=5) != 0:
                    raise AssertionError("--landlock require Cerv did not shut down cleanly")
            else:
                if required.wait(timeout=15) != 1:
                    raise AssertionError("--landlock require must fail startup when Landlock is unavailable")
                if "landlock=required" not in (required.stderr.read() if required.stderr else ""):
                    raise AssertionError("missing Landlock requirement diagnostic")
        finally:
            if required.poll() is None:
                required.kill()
                required.wait()
        env.pop("CERV_LANDLOCK")

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
    print("env runtime integration: auto capacity + SPA + dotfile policy + landlock option + strict numeric cap passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
