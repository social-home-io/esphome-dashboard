# SPDX-License-Identifier: MPL-2.0
"""Tests for the CI helper scripts (run: python -m unittest discover tests)."""

from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import device_matrix  # noqa: E402
import display_api_contract  # noqa: E402
import web_manifest  # noqa: E402


class DeviceMatrixTest(unittest.TestCase):
    def test_repo_manifest_builds_matrix(self) -> None:
        manifest = json.loads((ROOT / "devices" / "manifest.json").read_text())
        matrix = device_matrix.build_matrix(manifest)
        slugs = [entry["slug"] for entry in matrix["include"]]
        self.assertIn("guition-jc8012p4a1", slugs)
        self.assertEqual(len(slugs), len(set(slugs)))

    def test_missing_device_files_fail(self) -> None:
        manifest = {
            "devices": [
                {
                    "slug": "no-such-panel",
                    "name": "Nope",
                    "chip_family": "ESP32-P4",
                    "width": 1280,
                    "height": 800,
                    "ble": False,
                }
            ]
        }
        with self.assertRaisesRegex(ValueError, "missing devices/no-such-panel"):
            device_matrix.build_matrix(manifest)

    def test_missing_keys_fail(self) -> None:
        with self.assertRaisesRegex(ValueError, "misses"):
            device_matrix.build_matrix({"devices": [{"slug": "x"}]})

    def test_empty_manifest_fails(self) -> None:
        with self.assertRaisesRegex(ValueError, "no devices"):
            device_matrix.build_matrix({"devices": []})


class EsphomePinTest(unittest.TestCase):
    def test_one_exact_esphome_pin(self) -> None:
        """CI reads the version with `sed -n 's/^esphome==//p'`: one exact pin, nothing else."""
        lines = [
            line.strip()
            for line in (ROOT / ".github" / "esphome-requirements.txt").read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        self.assertEqual(len(lines), 1, lines)
        self.assertRegex(lines[0], r"^esphome==[0-9]{4}\.[0-9]{1,2}\.[0-9]+$")


class WebManifestTest(unittest.TestCase):
    def test_manifest_shape(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / "panel.factory.bin").write_bytes(b"factory")
            (directory / "panel.ota.bin").write_bytes(b"ota")
            manifest = web_manifest.build_manifest(
                slug="panel",
                name="Panel",
                chip_family="ESP32-P4",
                version="2026.10.13",
                directory=directory,
                release_url="https://example.invalid/r",
            )
        build = manifest["builds"][0]
        self.assertEqual(manifest["version"], "2026.10.13")
        self.assertEqual(build["chipFamily"], "ESP32-P4")
        self.assertEqual(build["parts"], [{"path": "panel.factory.bin", "offset": 0}])
        self.assertEqual(build["ota"]["path"], "panel.ota.bin")
        self.assertEqual(
            build["ota"]["md5"], hashlib.md5(b"ota", usedforsecurity=False).hexdigest()
        )
        self.assertEqual(build["ota"]["release_url"], "https://example.invalid/r")

    def test_missing_image_raises(self) -> None:
        with tempfile.TemporaryDirectory() as tmp, self.assertRaises(FileNotFoundError):
            web_manifest.build_manifest(
                slug="panel",
                name="Panel",
                chip_family="ESP32-P4",
                version="1",
                directory=Path(tmp),
            )


if __name__ == "__main__":
    unittest.main()


class DisplayApiContractTest(unittest.TestCase):
    def test_source_pins_a_commit(self) -> None:
        source = display_api_contract.read_source()
        self.assertEqual(source["SOCIALHOME_REPO"], "social-home-io/socialhome")
        self.assertRegex(source["SOCIALHOME_REF"], r"^[0-9a-f]{40}$")

    def test_fixtures_are_pinned(self) -> None:
        files = display_api_contract.local_files()
        self.assertIn("home.schema.json", files)
        self.assertIn("fixtures/home.json", files)

    def test_compare_reports_drift(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "fixtures").mkdir()
            (root / "a.json").write_bytes(b"{}")
            (root / "fixtures" / "b.json").write_bytes(b"[1]")
            (root / "extra.json").write_bytes(b"{}")
            remote = {"a.json": b"{}", "fixtures/b.json": b"[2]", "c.json": b"{}"}
            problems = display_api_contract.compare(remote, root)
        self.assertEqual(
            problems,
            [
                "missing locally: c.json",
                "differs: fixtures/b.json",
                "not in the pinned ref: extra.json",
            ],
        )

    def test_compare_clean(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.json").write_bytes(b"{}")
            self.assertEqual(display_api_contract.compare({"a.json": b"{}"}, root), [])
