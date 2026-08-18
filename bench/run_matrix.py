#!/usr/bin/env python3
"""Run a repeatable local ApacheBench matrix and emit machine-readable JSON."""
from __future__ import annotations
import argparse
import datetime as dt
import json
import platform
import re
import shutil
import subprocess
from pathlib import Path

RPS = re.compile(r"Requests per second:\s+([0-9.]+)")
P50 = re.compile(r"\s+50%\s+([0-9]+)")
P99 = re.compile(r"\s+99%\s+([0-9]+)")
FAILED = re.compile(r"Failed requests:\s+([0-9]+)")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", default="http://127.0.0.1:8080")
    ap.add_argument("--requests", type=int, default=5000)
    ap.add_argument("--concurrency", type=int, nargs="+", default=[1, 32, 128])
    ap.add_argument("--paths", nargs="+", default=["/0b.bin", "/1k.bin", "/16k.bin", "/128k.bin", "/missing"])
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    ab = shutil.which("ab")
    if not ab:
        raise SystemExit("ApacheBench (ab) is required")
    rows = []
    for path in a.paths:
        for c in a.concurrency:
            cmd = [ab, "-k", "-n", str(a.requests), "-c", str(c), a.base_url.rstrip("/") + path]
            p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            text = p.stdout
            def get(rx, cast=float, default=None):
                m = rx.search(text)
                return cast(m.group(1)) if m else default
            rows.append({
                "path": path,
                "concurrency": c,
                "requests": a.requests,
                "exitCode": p.returncode,
                "requestsPerSecond": get(RPS),
                "failedRequests": get(FAILED, int),
                "p50Ms": get(P50, int),
                "p99Ms": get(P99, int),
            })
    doc = {
        "schema": "cerv-benchmark-v1",
        "recordedAtUtc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
        "platform": platform.platform(),
        "baseUrl": a.base_url,
        "tool": subprocess.check_output([ab, "-V"], text=True, stderr=subprocess.STDOUT).splitlines()[0],
        "results": rows,
    }
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
    bad = [r for r in rows if r["exitCode"] != 0 or (r["failedRequests"] or 0) != 0]
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
