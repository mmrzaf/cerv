#!/usr/bin/env python3
"""Generate deterministic unsigned SLSA-style provenance for a local Cerv build."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--source-archive", type=Path, required=True)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--cflags", required=True)
    ap.add_argument("--ldflags", required=True)
    ap.add_argument("--source-date-epoch", required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()

    doc = {
        "_type": "https://in-toto.io/Statement/v1",
        "subject": [
            {"name": f"cerv-{args.version}-linux-x86_64/cerv", "digest": {"sha256": sha256(args.binary)}},
            {"name": args.source_archive.name, "digest": {"sha256": sha256(args.source_archive)}},
        ],
        "predicateType": "https://slsa.dev/provenance/v1",
        "predicate": {
            "buildDefinition": {
                "buildType": "urn:cerv:build-type:make-release:v1",
                "externalParameters": {
                    "version": args.version,
                    "compiler": args.compiler,
                    "cflags": args.cflags,
                    "ldflags": args.ldflags,
                    "sourceDateEpoch": args.source_date_epoch,
                },
                "internalParameters": {},
                "resolvedDependencies": [
                    {"uri": f"file:{args.source_archive.name}", "digest": {"sha256": sha256(args.source_archive)}}
                ],
            },
            "runDetails": {
                "builder": {"id": "urn:cerv:builder:local-controlled-release:v1"},
                "metadata": {},
                "byproducts": [],
            },
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
