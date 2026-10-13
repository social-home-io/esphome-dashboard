#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Write the per-device firmware manifest served from GitHub Pages.

One file serves two readers:

* ESP Web Tools (browser installer) reads ``builds[].parts`` and flashes the
  factory image at offset 0.
* ESPHome's ``update: platform: http_request`` reads ``builds[].ota``
  (``path``, ``md5``, ``summary``, ``release_url``) and pulls the OTA image.

Paths are relative to the manifest, so both images are served from Pages next
to it (release asset URLs redirect, which the device's HTTP client does not
cope with well).

Usage:
  scripts/web_manifest.py --slug guition-jc8012p4a1 --name "..." \
      --chip-family ESP32-P4 --version 2026.10.13 --dir site/firmware/<slug> \
      [--release-url URL] [--summary TEXT]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def md5_of(path: Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def build_manifest(
    *,
    slug: str,
    name: str,
    chip_family: str,
    version: str,
    directory: Path,
    release_url: str | None = None,
    summary: str | None = None,
) -> dict:
    factory = f"{slug}.factory.bin"
    ota = f"{slug}.ota.bin"
    for filename in (factory, ota):
        if not (directory / filename).is_file():
            raise FileNotFoundError(directory / filename)
    ota_info: dict[str, str] = {"path": ota, "md5": md5_of(directory / ota)}
    ota_info["summary"] = summary or f"Social Home dashboard {version}"
    if release_url:
        ota_info["release_url"] = release_url
    return {
        "name": f"Social Home dashboard for {name}",
        "version": version,
        "home_assistant_domain": "esphome",
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": chip_family,
                "parts": [{"path": factory, "offset": 0}],
                "ota": ota_info,
            }
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--slug", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--chip-family", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--dir", type=Path, required=True)
    parser.add_argument("--release-url")
    parser.add_argument("--summary")
    args = parser.parse_args(argv)
    try:
        manifest = build_manifest(
            slug=args.slug,
            name=args.name,
            chip_family=args.chip_family,
            version=args.version,
            directory=args.dir,
            release_url=args.release_url,
            summary=args.summary,
        )
    except FileNotFoundError as err:
        print(f"error: missing firmware image {err}", file=sys.stderr)
        return 1
    (args.dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
