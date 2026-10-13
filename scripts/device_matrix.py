#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Turn devices/manifest.json into a GitHub Actions build matrix.

Prints ``{"include": [{"slug": ..., "name": ..., "chip_family": ...}, ...]}``
as a single JSON line, ready for ``fromJSON()``. Also checks that every
catalogued device has its folder and build entry points, so a half-added
device fails CI early.

Usage: scripts/device_matrix.py [--manifest devices/manifest.json]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SLUG_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
REQUIRED_KEYS = ("slug", "name", "chip_family", "width", "height", "ble")


def build_matrix(manifest: dict, root: Path = ROOT) -> dict:
    devices = manifest.get("devices")
    if not isinstance(devices, list) or not devices:
        raise ValueError("manifest has no devices")
    include = []
    seen: set[str] = set()
    for device in devices:
        missing = [key for key in REQUIRED_KEYS if key not in device]
        if missing:
            raise ValueError(f"device {device.get('slug', '?')} misses {missing}")
        slug = device["slug"]
        if not SLUG_RE.match(slug):
            raise ValueError(f"invalid slug {slug!r}")
        if slug in seen:
            raise ValueError(f"duplicate slug {slug!r}")
        seen.add(slug)
        for rel in (
            f"devices/{slug}/esphome.yaml",
            f"devices/{slug}/packages.yaml",
            f"builds/{slug}.yaml",
            f"builds/{slug}.factory.yaml",
        ):
            if not (root / rel).is_file():
                raise ValueError(f"device {slug}: missing {rel}")
        include.append(
            {
                "slug": slug,
                "name": device["name"],
                "chip_family": device["chip_family"],
            }
        )
    return {"include": include}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--manifest", type=Path, default=ROOT / "devices" / "manifest.json"
    )
    args = parser.parse_args(argv)
    try:
        matrix = build_matrix(json.loads(args.manifest.read_text()))
    except (ValueError, json.JSONDecodeError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    print(json.dumps(matrix, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    sys.exit(main())
