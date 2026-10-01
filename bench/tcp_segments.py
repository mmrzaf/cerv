#!/usr/bin/env python3
"""Measure TCP segments (system-wide OutSegs delta) per keep-alive response for one small file.

Run on an otherwise quiet host; the counter covers every TCP segment the kernel transmits, in both directions,
including pure ACKs, so the figure is comparable only between runs on the same machine. Example:

    bench/tcp_segments.py build/release/cerv --requests 3000
"""
from __future__ import annotations

import argparse
import socket
import subprocess
import tempfile
import time
from pathlib import Path

KEEPALIVE_REUSE = 60  # stay below CERV_KEEPALIVE_REQUESTS_MAX (64)


def out_segs() -> int:
    with open("/proc/net/snmp", encoding="ascii") as handle:
        rows = [line.split() for line in handle if line.startswith("Tcp:")]
    return int(rows[1][rows[0].index("OutSegs")])


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


class Client:
    def __init__(self, port: int) -> None:
        self.port = port
        self.sock: socket.socket | None = None
        self.used = 0

    def request(self, path: bytes) -> None:
        if self.sock is None or self.used >= KEEPALIVE_REUSE:
            if self.sock is not None:
                self.sock.close()
            self.sock = socket.create_connection(("127.0.0.1", self.port))
            self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.used = 0
        self.used += 1
        self.sock.sendall(b"GET " + path + b" HTTP/1.1\r\nHost: bench\r\n\r\n")
        buffer = b""
        while b"\r\n\r\n" not in buffer:
            buffer += self.sock.recv(65536)
        head, _, body = buffer.partition(b"\r\n\r\n")
        length = int(next(line for line in head.split(b"\r\n") if line.lower().startswith(b"content-length")).split(b":")[1])
        while len(body) < length:
            body += self.sock.recv(65536)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cerv", help="path to the Cerv executable")
    parser.add_argument("--requests", type=int, default=3000)
    parser.add_argument("--size", type=int, default=600, help="file size in bytes")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="cerv-segments-") as root:
        Path(root, "small.bin").write_bytes(b"x" * args.size)
        port = free_port()
        server = subprocess.Popen(
            [args.cerv, "--listen", f"127.0.0.1:{port}", "--workers", "1", root],
            stderr=subprocess.DEVNULL,
        )
        try:
            time.sleep(0.6)
            client = Client(port)
            for _ in range(20):
                client.request(b"/small.bin")
            time.sleep(0.2)
            before = out_segs()
            started = time.monotonic()
            for _ in range(args.requests):
                client.request(b"/small.bin")
            elapsed = time.monotonic() - started
            time.sleep(0.2)
            after = out_segs()
        finally:
            server.terminate()
            server.wait()
    print(
        f"requests={args.requests} size={args.size}B "
        f"tcp_segments_per_request={(after - before) / args.requests:.2f} "
        f"ms_per_request={elapsed / args.requests * 1000:.3f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
