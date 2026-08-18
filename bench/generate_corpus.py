#!/usr/bin/env python3
"""Generate the deterministic Cerv performance corpus from the CES size matrix."""
from pathlib import Path
import argparse

SIZES = [
    ("0b.bin", 0),
    ("128b.bin", 128),
    ("1k.bin", 1024),
    ("16k.bin", 16 * 1024),
    ("128k.bin", 128 * 1024),
    ("1m.bin", 1024 * 1024),
    ("16m.bin", 16 * 1024 * 1024),
]


def fill(path: Path, size: int) -> None:
    pattern = bytes(range(256))
    with path.open("wb") as f:
        remaining = size
        while remaining:
            chunk = pattern[: min(remaining, len(pattern))]
            f.write(chunk)
            remaining -= len(chunk)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", type=Path)
    ap.add_argument("--with-sparse-1g", action="store_true")
    a = ap.parse_args()
    a.root.mkdir(parents=True, exist_ok=True)
    for name, size in SIZES:
        fill(a.root / name, size)
    (a.root / "index.html").write_text("cerv benchmark\n", encoding="ascii")
    if a.with_sparse_1g:
        with (a.root / "1g-sparse.bin").open("wb") as f:
            f.truncate(1024 * 1024 * 1024)


if __name__ == "__main__":
    main()
