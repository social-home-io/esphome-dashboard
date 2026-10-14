# SPDX-License-Identifier: MPL-2.0
"""Tests for scripts/build_site.py (the GitHub Pages site assembly)."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import build_site  # noqa: E402

DEVICES = {
    "devices": [
        {"slug": "panel-a", "name": "Panel A", "chip_family": "ESP32-P4"},
        {"slug": "panel-b", "name": "Panel B", "chip_family": "ESP32-S3"},
    ]
}


def release(tag: str = "2026.10.14", assets: list[str] | None = None, **extra) -> dict:
    return {
        "tag_name": tag,
        "html_url": f"https://github.com/o/r/releases/tag/{tag}",
        "draft": False,
        "prerelease": False,
        "assets": [{"name": name} for name in (assets or [])],
        **extra,
    }


class Workspace:
    """A temp dir with docs/, a device catalogue and a release-assets dir."""

    def __enter__(self) -> Workspace:
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.docs = root / "docs"
        self.docs.mkdir()
        (self.docs / "index.html").write_text("<!doctype html>")
        self.devices = root / "manifest.json"
        self.devices.write_text(json.dumps(DEVICES))
        self.assets = root / "release"
        self.assets.mkdir()
        self.out = root / "site"
        return self

    def __exit__(self, *exc: object) -> None:
        self._tmp.cleanup()

    def images(self, slug: str) -> list[str]:
        names = list(build_site.image_names(slug))
        for name in names:
            (self.assets / name).write_bytes(f"{name} bytes".encode())
        return names

    def assemble(self, rel: dict | None) -> dict:
        return build_site.assemble(
            docs=self.docs,
            devices_manifest=self.devices,
            release=rel,
            assets=self.assets,
            out=self.out,
        )


class ValidateReleaseTest(unittest.TestCase):
    def test_stable_calver_passes(self) -> None:
        for tag in ("2026.10.14", "2026.1.2.3"):
            build_site.validate_release(release(tag))

    def test_rejects_bad_tags(self) -> None:
        for tag in ("v2026.10.14", "2026.10.04", "2026.10", "latest", None):
            with self.subTest(tag=tag), self.assertRaisesRegex(build_site.ReleaseError, "CalVer"):
                build_site.validate_release(release(tag))

    def test_rejects_prerelease_and_draft(self) -> None:
        for flag in ("prerelease", "draft"):
            with self.subTest(flag=flag), self.assertRaises(build_site.ReleaseError):
                build_site.validate_release(release(**{flag: True}))

    def test_rejects_non_https_url(self) -> None:
        with self.assertRaisesRegex(build_site.ReleaseError, "https"):
            build_site.validate_release(release(html_url="http://example.invalid"))


class PickFirmwareTest(unittest.TestCase):
    def test_needs_both_images(self) -> None:
        names = {"panel-a.factory.bin", "panel-a.ota.bin", "panel-b.ota.bin", "notes.txt"}
        picked, warnings = build_site.pick_firmware(DEVICES["devices"], names)
        self.assertEqual(picked, ["panel-a"])
        self.assertEqual(len(warnings), 1)
        self.assertIn("panel-b", warnings[0])

    def test_device_absent_from_release_is_silent(self) -> None:
        picked, warnings = build_site.pick_firmware(
            DEVICES["devices"], {"panel-a.factory.bin", "panel-a.ota.bin"}
        )
        self.assertEqual((picked, warnings), (["panel-a"], []))


class ParseLatestTest(unittest.TestCase):
    def test_ok(self) -> None:
        self.assertEqual(build_site.parse_latest(0, '{"tag_name": "x"}', ""), {"tag_name": "x"})

    def test_404_means_no_release(self) -> None:
        self.assertIsNone(build_site.parse_latest(1, "", "gh: Not Found (HTTP 404)"))

    def test_other_errors_raise(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "HTTP 500"):
            build_site.parse_latest(1, "", "gh: Server Error (HTTP 500)")


class AssembleTest(unittest.TestCase):
    def test_no_release_deploys_without_firmware(self) -> None:
        with Workspace() as ws:
            summary = ws.assemble(None)
            self.assertTrue(summary["deploy"])
            self.assertTrue((ws.out / "index.html").is_file())
            self.assertEqual(json.loads((ws.out / "devices.json").read_text()), DEVICES)
            self.assertFalse((ws.out / "firmware").exists())
            self.assertEqual(
                json.loads((ws.out / "release.json").read_text()),
                {"tag": None, "release_url": None, "firmware": []},
            )

    def test_release_writes_manifest_per_device(self) -> None:
        with Workspace() as ws:
            names = ws.images("panel-a")
            rel = release(assets=names)
            summary = ws.assemble(rel)
            self.assertEqual(summary["firmware"], ["panel-a"])
            target = ws.out / "firmware" / "panel-a"
            self.assertEqual(
                sorted(p.name for p in target.iterdir()),
                ["manifest.json", "panel-a.factory.bin", "panel-a.ota.bin"],
            )
            manifest = json.loads((target / "manifest.json").read_text())
            ota = manifest["builds"][0]["ota"]
            self.assertEqual(manifest["version"], "2026.10.14")
            self.assertEqual(manifest["builds"][0]["chipFamily"], "ESP32-P4")
            self.assertEqual(manifest["builds"][0]["parts"], [{"path": "panel-a.factory.bin", "offset": 0}])
            self.assertEqual(ota["path"], "panel-a.ota.bin")
            self.assertEqual(
                ota["md5"], hashlib.md5(b"panel-a.ota.bin bytes", usedforsecurity=False).hexdigest()
            )
            self.assertEqual(ota["release_url"], rel["html_url"])
            self.assertFalse((ws.out / "firmware" / "panel-b").exists())
            self.assertEqual(
                json.loads((ws.out / "release.json").read_text()),
                {"tag": "2026.10.14", "release_url": rel["html_url"], "firmware": ["panel-a"]},
            )

    def test_release_without_images_keeps_live_site(self) -> None:
        with Workspace() as ws:
            summary = ws.assemble(release(assets=["notes.txt"]))
            self.assertFalse(summary["deploy"])
            self.assertFalse(ws.out.exists())
            self.assertIn("keeping the live site", summary["warnings"][-1])

    def test_listed_but_missing_download_fails(self) -> None:
        with Workspace() as ws, self.assertRaises(FileNotFoundError):
            ws.assemble(release(assets=list(build_site.image_names("panel-a"))))

    def test_prerelease_never_deploys(self) -> None:
        with Workspace() as ws, self.assertRaises(build_site.ReleaseError):
            ws.assemble(release(assets=ws.images("panel-a"), prerelease=True))

    def test_refuses_existing_out(self) -> None:
        with Workspace() as ws:
            ws.out.mkdir()
            with self.assertRaises(FileExistsError):
                ws.assemble(None)

    def test_cli_writes_github_output(self) -> None:
        with Workspace() as ws:
            names = ws.images("panel-a")
            (ws.assets / "release.json").write_text(json.dumps(release(assets=names)))
            output = ws.assets.parent / "gh-output"
            code = build_site.main(
                [
                    "--devices", str(ws.devices),
                    "assemble",
                    "--release-dir", str(ws.assets),
                    "--docs", str(ws.docs),
                    "--out", str(ws.out),
                    "--github-output", str(output),
                ]
            )
            self.assertEqual(code, 0)
            self.assertEqual(output.read_text(), "deploy=true\ntag=2026.10.14\n")

    def test_cli_no_release(self) -> None:
        with Workspace() as ws:
            (ws.assets / "release.json").write_text("null\n")
            code = build_site.main(
                ["--devices", str(ws.devices), "assemble", "--release-dir", str(ws.assets),
                 "--docs", str(ws.docs), "--out", str(ws.out)]
            )
            self.assertEqual(code, 0)
            self.assertFalse((ws.out / "firmware").exists())


if __name__ == "__main__":
    unittest.main()
