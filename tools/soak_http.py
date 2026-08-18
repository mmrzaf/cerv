#!/usr/bin/env python3
"""Bounded local keep-alive soak client; reports errors and latency without external deps."""
from __future__ import annotations
import argparse
import concurrent.futures
import socket
import statistics
import threading
import time


def worker(host: str, port: int, path: str, stop: float, ident: int):
    errors = 0
    requests = 0
    lats = []
    while time.monotonic() < stop:
        try:
            s = socket.create_connection((host, port), timeout=2)
            s.settimeout(2)
            for _ in range(64):
                if time.monotonic() >= stop:
                    break
                req = f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode()
                t0 = time.monotonic_ns()
                s.sendall(req)
                data = b""
                while b"\r\n\r\n" not in data:
                    chunk = s.recv(65536)
                    if not chunk:
                        raise ConnectionError("early EOF")
                    data += chunk
                head, body = data.split(b"\r\n\r\n", 1)
                line = head.split(b"\r\n", 1)[0]
                if b" 200 " not in line:
                    raise RuntimeError(f"unexpected status {line!r}")
                length = None
                close = False
                for h in head.split(b"\r\n")[1:]:
                    name, _, value = h.partition(b":")
                    if name.lower() == b"content-length":
                        length = int(value.strip())
                    if name.lower() == b"connection" and value.strip().lower() == b"close":
                        close = True
                if length is None:
                    raise RuntimeError("missing content-length")
                while len(body) < length:
                    chunk = s.recv(min(65536, length - len(body)))
                    if not chunk:
                        raise ConnectionError("short body")
                    body += chunk
                requests += 1
                lats.append((time.monotonic_ns() - t0) / 1e6)
                if close:
                    break
            s.close()
        except Exception:
            errors += 1
            try:
                s.close()
            except Exception:
                pass
    return requests, errors, lats


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--path", default="/1k.bin")
    ap.add_argument("--duration", type=float, default=60.0)
    ap.add_argument("--clients", type=int, default=32)
    a = ap.parse_args()
    stop = time.monotonic() + a.duration
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.clients) as ex:
        results = list(ex.map(lambda i: worker(a.host, a.port, a.path, stop, i), range(a.clients)))
    reqs = sum(r[0] for r in results)
    errs = sum(r[1] for r in results)
    lats = [x for r in results for x in r[2]]
    p50 = statistics.median(lats) if lats else 0.0
    p99 = sorted(lats)[min(len(lats) - 1, int(len(lats) * 0.99))] if lats else 0.0
    print(f"requests={reqs} errors={errs} duration_s={a.duration:.3f} rps={reqs/a.duration:.2f} p50_ms={p50:.3f} p99_ms={p99:.3f}")
    return 0 if errs == 0 and reqs > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
