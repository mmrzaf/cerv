#!/usr/bin/env python3
"""Generate a deterministic SPDX 2.3 JSON SBOM for a Cerv release bundle."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import uuid
import datetime as dt


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def spdx_id(rel: str) -> str:
    digest = hashlib.sha256(rel.encode("utf-8")).hexdigest()[:20]
    return f"SPDXRef-File-{digest}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", required=True)
    ap.add_argument("--source-root", type=Path, required=True)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--libc", required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()

    root = args.source_root.resolve()
    included_roots = ("src/", "docs/", "proof/", "fuzz/", "test/", "tools/", "bench/", "deploy/", ".github/")
    top_files = {"Makefile", "README.md", "CONTRIBUTING.md", "CHANGELOG.md", "SECURITY.md", "LICENSE", "VERSION", "RELEASE_EPOCH", ".clang-tidy", ".dockerignore", ".editorconfig", ".gitattributes", ".gitignore", "Dockerfile"}
    files: list[dict[str, object]] = []
    file_rels: list[str] = []
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(root).as_posix()
        if rel.startswith("build/") or rel.startswith("dist/") or rel.startswith(".git/"):
            continue
        if "/__pycache__/" in f"/{rel}" or rel.endswith(".pyc"):
            continue
        if rel not in top_files and not rel.startswith(included_roots):
            continue
        file_rels.append(rel)
        files.append({
            "SPDXID": spdx_id(rel),
            "fileName": f"./{rel}",
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(p)}],
            "licenseConcluded": "NOASSERTION",
            "licenseInfoInFiles": ["NOASSERTION"],
            "copyrightText": "NOASSERTION",
        })

    source_digest = hashlib.sha256("\n".join(f"{r}:{sha256(root / r)}" for r in file_rels).encode()).hexdigest()
    namespace_uuid = uuid.uuid5(uuid.NAMESPACE_URL, f"cerv:{args.version}:{source_digest}:{sha256(args.binary)}")
    package_id = "SPDXRef-Package-Cerv"
    relationships = [{"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES", "relatedSpdxElement": package_id}]
    relationships += [{"spdxElementId": package_id, "relationshipType": "CONTAINS", "relatedSpdxElement": f["SPDXID"]} for f in files]

    doc = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"cerv-{args.version}",
        "documentNamespace": f"urn:uuid:{namespace_uuid}",
        "creationInfo": {
            "creators": ["Tool: cerv/tools/generate_sbom.py"],
            "created": dt.datetime.fromtimestamp(int(os.environ.get("SOURCE_DATE_EPOCH", "0")), tz=dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        },
        "packages": [{
            "name": "cerv",
            "SPDXID": package_id,
            "versionInfo": args.version,
            "downloadLocation": "NOASSERTION",
            "filesAnalyzed": True,
            "licenseConcluded": "MIT",
            "licenseDeclared": "MIT",
            "copyrightText": "Copyright (c) 2026 mmrzaf",
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(args.binary)}],
            "externalRefs": [
                {"referenceCategory": "OTHER", "referenceType": "cerv-compiler", "referenceLocator": args.compiler},
                {"referenceCategory": "OTHER", "referenceType": "cerv-libc", "referenceLocator": args.libc},
            ],
        }],
        "files": files,
        "relationships": relationships,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(doc, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
