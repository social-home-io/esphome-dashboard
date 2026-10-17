#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Assemble the GitHub Pages site: the web installer plus the OTA manifests.

Pages hosts one site per repository and every deploy replaces all of it, so
each deploy must carry the firmware panels poll for updates. The site is:

* ``docs/`` at the checked-out commit (the installer page), with the ESP Web
  Tools version ``docs/package.json`` pins (Dependabot bumps it there) written
  over ``{{ESP_WEB_TOOLS_VERSION}}`` in every page; the package.json itself is
  not published,
* ``devices.json`` (a copy of ``devices/manifest.json``),
* ``release.json`` (which release the firmware comes from, for the page),
* ``firmware/<slug>/{<slug>.factory.bin, <slug>.ota.bin, manifest.json}``
  for every catalogued device that has both images attached to the latest
  published, non-draft, non-pre-release release.

Two steps, so the assembly never touches the network:

  scripts/build_site.py fetch --repo OWNER/NAME --dest DIR
      Writes DIR/release.json (``null`` when nothing is published yet) and
      downloads the device images of ``releases/latest`` into DIR.

  scripts/build_site.py assemble --release-dir DIR --out site \
      [--github-output FILE]
      Builds the site from DIR. When a release exists but carries none of the
      catalogued images (its build is still attaching them, or failed), it
      writes nothing and reports ``deploy=false``: deploying would drop every
      manifest the panels poll.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import web_manifest

ROOT = Path(__file__).resolve().parents[1]

# Same shape release.yml accepts: CalVer YYYY.M.D[.N], no v prefix.
CALVER = re.compile(r"^[0-9]{4}\.[1-9][0-9]?\.[1-9][0-9]?(\.[0-9]+)?$")


# The installer pages name ESP Web Tools by this placeholder; docs/package.json
# holds the exact version (an npm dependency Dependabot can bump).
ESP_WEB_TOOLS_PLACEHOLDER = "{{ESP_WEB_TOOLS_VERSION}}"
EXACT_VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


class SiteError(ValueError):
    """``docs/`` can't be rendered into the installer site."""


class ReleaseError(ValueError):
    """The release listing can't be deployed as the update source."""


def image_names(slug: str) -> tuple[str, str]:
    return f"{slug}.factory.bin", f"{slug}.ota.bin"


def validate_release(release: dict) -> dict:
    """Check a ``releases/latest`` entry is a stable CalVer release."""
    tag = release.get("tag_name")
    if not isinstance(tag, str) or not CALVER.match(tag):
        raise ReleaseError(f"tag {tag!r} is not CalVer YYYY.M.D[.N] (no v prefix)")
    if release.get("draft") or release.get("prerelease"):
        raise ReleaseError(f"release {tag} is a draft or pre-release")
    url = release.get("html_url")
    if not isinstance(url, str) or not url.startswith("https://"):
        raise ReleaseError(f"release {tag} has no https html_url")
    return release


def esp_web_tools_version(docs: Path) -> str:
    """The exact ESP Web Tools version ``docs/package.json`` pins."""
    package = docs / "package.json"
    if not package.is_file():
        raise SiteError(f"{package} is missing: it pins ESP Web Tools for the installer")
    version = json.loads(package.read_text()).get("dependencies", {}).get("esp-web-tools")
    if not isinstance(version, str) or not EXACT_VERSION.match(version):
        raise SiteError(f"{package}: esp-web-tools must be an exact x.y.z version, not {version!r}")
    return version


def render_docs(docs: Path, out: Path) -> None:
    """Copy ``docs`` to ``out`` with the ESP Web Tools version filled in."""
    version = esp_web_tools_version(docs)
    if not any(ESP_WEB_TOOLS_PLACEHOLDER in page.read_text() for page in docs.rglob("*.html")):
        raise SiteError(f"no page in {docs} loads ESP Web Tools via {ESP_WEB_TOOLS_PLACEHOLDER}")
    shutil.copytree(docs, out, ignore=shutil.ignore_patterns("package.json"))
    for page in sorted(out.rglob("*.html")):
        text = page.read_text()
        if ESP_WEB_TOOLS_PLACEHOLDER in text:
            page.write_text(text.replace(ESP_WEB_TOOLS_PLACEHOLDER, version))


def pick_firmware(devices: list[dict], asset_names: set[str]) -> tuple[list[str], list[str]]:
    """Slugs with both images in the release, plus warnings for half-attached ones."""
    picked: list[str] = []
    warnings: list[str] = []
    for device in devices:
        slug = device["slug"]
        present = [name for name in image_names(slug) if name in asset_names]
        if len(present) == 2:
            picked.append(slug)
        elif present:
            warnings.append(f"{slug}: release carries only {present[0]}, skipping the device")
    return picked, warnings


def assemble(
    *,
    docs: Path,
    devices_manifest: Path,
    release: dict | None,
    assets: Path,
    out: Path,
) -> dict:
    """Build the site into ``out`` (must not exist). Returns a summary."""
    devices = json.loads(devices_manifest.read_text())["devices"]
    picked: list[str] = []
    warnings: list[str] = []
    if release is not None:
        validate_release(release)
        names = {asset["name"] for asset in release.get("assets", [])}
        picked, warnings = pick_firmware(devices, names)
        if not picked:
            return {
                "deploy": False,
                "tag": release["tag_name"],
                "firmware": [],
                "warnings": [
                    *warnings,
                    f"release {release['tag_name']} has no device images yet; keeping the live site",
                ],
            }

    if out.exists():
        raise FileExistsError(f"{out} already exists; refusing to mix in stale files")
    render_docs(docs, out)
    shutil.copyfile(devices_manifest, out / "devices.json")

    by_slug = {device["slug"]: device for device in devices}
    for slug in picked:
        device = by_slug[slug]
        target = out / "firmware" / slug
        target.mkdir(parents=True)
        for name in image_names(slug):
            source = assets / name
            if not source.is_file():
                raise FileNotFoundError(source)
            shutil.copyfile(source, target / name)
        manifest = web_manifest.build_manifest(
            slug=slug,
            name=device["name"],
            chip_family=device["chip_family"],
            version=release["tag_name"],
            directory=target,
            release_url=release["html_url"],
        )
        (target / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    info = {
        "tag": release["tag_name"] if release else None,
        "release_url": release["html_url"] if release else None,
        "firmware": picked,
    }
    (out / "release.json").write_text(json.dumps(info, indent=2) + "\n")
    return {"deploy": True, **info, "warnings": warnings}


def parse_latest(returncode: int, stdout: str, stderr: str) -> dict | None:
    """Interpret ``gh api repos/R/releases/latest``: 404 means nothing published."""
    if returncode == 0:
        return json.loads(stdout)
    if "HTTP 404" in stderr:
        return None
    raise RuntimeError(f"gh api releases/latest failed: {stderr.strip()}")


def fetch(*, repo: str, dest: Path, devices_manifest: Path) -> dict | None:
    """Download the latest stable release's device images into ``dest``."""
    proc = subprocess.run(
        ["gh", "api", f"repos/{repo}/releases/latest"],
        capture_output=True,
        text=True,
        check=False,
    )
    release = parse_latest(proc.returncode, proc.stdout, proc.stderr)
    dest.mkdir(parents=True, exist_ok=True)
    if release is None:
        (dest / "release.json").write_text("null\n")
        return None
    validate_release(release)
    release = {
        key: release[key] for key in ("tag_name", "html_url", "draft", "prerelease")
    } | {"assets": [{"name": asset["name"]} for asset in release.get("assets", [])]}
    (dest / "release.json").write_text(json.dumps(release, indent=2) + "\n")

    devices = json.loads(devices_manifest.read_text())["devices"]
    picked, _ = pick_firmware(devices, {asset["name"] for asset in release["assets"]})
    if picked:
        patterns = [arg for slug in picked for name in image_names(slug) for arg in ("--pattern", name)]
        subprocess.run(
            ["gh", "release", "download", release["tag_name"], "--repo", repo,
             "--dir", str(dest), "--clobber", *patterns],
            check=True,
        )
    return release


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--devices", type=Path, default=ROOT / "devices" / "manifest.json")
    sub = parser.add_subparsers(dest="command", required=True)
    fetch_cmd = sub.add_parser("fetch", help="download the latest stable release's images")
    fetch_cmd.add_argument("--repo", required=True)
    fetch_cmd.add_argument("--dest", type=Path, required=True)
    build_cmd = sub.add_parser("assemble", help="build the site from a fetched release dir")
    build_cmd.add_argument("--release-dir", type=Path, required=True)
    build_cmd.add_argument("--out", type=Path, required=True)
    build_cmd.add_argument("--docs", type=Path, default=ROOT / "docs")
    build_cmd.add_argument("--github-output", type=Path)
    args = parser.parse_args(argv)

    try:
        if args.command == "fetch":
            release = fetch(repo=args.repo, dest=args.dest, devices_manifest=args.devices)
            print(f"latest stable release: {release['tag_name'] if release else 'none yet'}")
            return 0
        release = json.loads((args.release_dir / "release.json").read_text())
        summary = assemble(
            docs=args.docs,
            devices_manifest=args.devices,
            release=release,
            assets=args.release_dir,
            out=args.out,
        )
    except (ReleaseError, SiteError, FileNotFoundError, FileExistsError, RuntimeError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1
    for warning in summary["warnings"]:
        print(f"::warning::{warning}")
    print(json.dumps({key: summary[key] for key in ("deploy", "tag", "firmware")}))
    if args.github_output:
        with args.github_output.open("a") as handle:
            handle.write(f"deploy={'true' if summary['deploy'] else 'false'}\n")
            handle.write(f"tag={summary['tag'] or ''}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
