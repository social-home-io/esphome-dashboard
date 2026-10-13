#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check (or refresh) the pinned Social Home display API contract.

tests/fixtures/display-api/SOURCE names a Social Home commit. The schemas and
fixtures under tests/fixtures/display-api/v1/ must be byte-identical to
docs/display-api/v1/ at that commit, so the host parser tests run against
exactly what that server version sends.

    python3 scripts/display_api_contract.py           # check, exit 1 on drift
    python3 scripts/display_api_contract.py --update  # rewrite from the ref
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "tests" / "fixtures" / "display-api"
REMOTE_DIR = "docs/display-api/v1"


def read_source(path: Path = CONTRACT / "SOURCE") -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, _, value = line.partition("=")
        values[key.strip()] = value.strip()
    for key in ("SOCIALHOME_REPO", "SOCIALHOME_REF"):
        if not values.get(key):
            raise SystemExit(f"{path}: {key} is missing")
    return values


def list_remote(repo: str, ref: str) -> list[str]:
    """Paths (relative to docs/display-api/v1) of every file at ``ref``."""
    url = f"https://api.github.com/repos/{repo}/git/trees/{ref}?recursive=1"
    request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        tree = json.load(response)
    prefix = REMOTE_DIR + "/"
    return sorted(
        entry["path"][len(prefix) :]
        for entry in tree["tree"]
        if entry["type"] == "blob" and entry["path"].startswith(prefix)
    )


def fetch(repo: str, ref: str, rel: str) -> bytes:
    url = f"https://raw.githubusercontent.com/{repo}/{ref}/{REMOTE_DIR}/{rel}"
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read()


def local_files(root: Path = CONTRACT / "v1") -> list[str]:
    return sorted(str(p.relative_to(root)) for p in root.rglob("*") if p.is_file())


def compare(remote: dict[str, bytes], local_root: Path = CONTRACT / "v1") -> list[str]:
    """Human-readable differences between the remote files and the local copy."""
    problems = []
    local = set(local_files(local_root))
    for rel, body in sorted(remote.items()):
        if rel not in local:
            problems.append(f"missing locally: {rel}")
        elif (local_root / rel).read_bytes() != body:
            problems.append(f"differs: {rel}")
    for rel in sorted(local - set(remote)):
        problems.append(f"not in the pinned ref: {rel}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--update", action="store_true", help="rewrite the local copy")
    args = parser.parse_args(argv)
    source = read_source()
    repo, ref = source["SOCIALHOME_REPO"], source["SOCIALHOME_REF"]
    remote = {rel: fetch(repo, ref, rel) for rel in list_remote(repo, ref)}
    if not remote:
        print(f"{repo}@{ref} has no {REMOTE_DIR}", file=sys.stderr)
        return 1
    if args.update:
        root = CONTRACT / "v1"
        for rel in local_files(root):
            (root / rel).unlink()
        for rel, body in remote.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_bytes(body)
        print(f"updated {len(remote)} files from {repo}@{ref}")
        return 0
    problems = compare(remote)
    for line in problems:
        print(line, file=sys.stderr)
    if problems:
        print(f"the pinned contract drifted from {repo}@{ref}", file=sys.stderr)
        return 1
    print(f"{len(remote)} files match {repo}@{ref}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
