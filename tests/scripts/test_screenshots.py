# SPDX-License-Identifier: MPL-2.0
"""Tests for the pure parts of scripts/screenshots (no browser, panel or Pillow)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "screenshots"))

import boxes  # noqa: E402
import metrics  # noqa: E402
import naming  # noqa: E402

DUMP = """0 0 1280 800
  0 0 1280 800
    0 0 120 800
      15 16 88 113
        23 24 72 72
        37 102 43 19 "Home"
      15 262 88 113
        23 270 72 72
        15 348 88 19 "Leo"
      27 720 64 64
        44 737 30 30
    120 0 1160 800
      148 16 1104 48
        148 16 123 48
          164 26 91 26 "Overview"
        271 16 165 48
          287 26 101 26 "Messages"
        436 16 90 48
          452 26 58 26 "Notes"
      148 184 257 102 "16:23"
      691 389 390 29
        691 390 55 26 "@leo "
        746 390 66 26 "Shoes "
        812 390 36 26 "on! "
        848 391 23 23
      148 900 100 20 "Below the fold"
0 0 1280 800
  230 223 820 354
    263 256 204 38 "Panel settings"
    263 480 298 64
      263 488 183 56
        289 500 131 26 "Forget server"
"""

LOCKED = """0 0 1280 800
  320 61 640 678
    524 80 232 113 "16:25"
    480 246 320 439
      480 246 320 76
        496 258 288 21 "Wrong PIN"
        496 289 288 21 "Tries left: 4"
      601 334 77 21 "Enter PIN"
      480 399 320 286
""" + "".join(
    f"        {480 + 110 * (k % 3)} {399 + 74 * (k // 3)} 100 64\n"
    f"          {522 + 110 * (k % 3)} {415 + 74 * (k // 3)} 16 33 \"{d}\"\n"
    for k, d in enumerate("123456789")) + """        480 621 100 64
          514 642 32 22
        590 621 100 64
          632 637 16 33 "0"
        700 621 100 64
          737 640 26 26
"""


class NamingTest(unittest.TestCase):
    def test_shot_names(self) -> None:
        self.assertEqual(naming.shot_name("overview", "de", "dark"), "overview-de-dark")
        self.assertEqual(naming.shot_name("wrong-pin", "en", "light"), "wrong-pin-en-light")
        self.assertEqual(naming.shot_name("setup", "de", "dark"), "setup")
        self.assertEqual(naming.file_name("panel", "map-en-auto"), "panel-map-en-auto.png")
        self.assertEqual(naming.file_name("boxes", "setup"), "boxes-setup.json")
        self.assertEqual(naming.file_name("offsets", "night-de-light"), "offsets-night-de-light.md")
        with self.assertRaises(ValueError):
            naming.shot_name("kitchen", "en", "light")
        with self.assertRaises(ValueError):
            naming.shot_name("overview", "EN!", "light")

    def test_parse_round_trip(self) -> None:
        for screen in naming.SCREENS:
            for kind in naming.KINDS:
                for locale, scheme in (("en", "light"), ("de-CH", "dark"), ("fr", "auto")):
                    name = naming.shot_name(screen, locale, scheme)
                    parts = naming.parse_file(naming.file_name(kind, name))
                    self.assertIsNotNone(parts, name)
                    self.assertEqual(parts["name"], name)
                    self.assertEqual(parts["screen"], screen)
                    self.assertEqual(parts["kind"], kind)

    def test_parse_rejects_others(self) -> None:
        for f in ("panel-overview.png", "panel-setup-en-light.png", "boxes-overview-en-light.png",
                  "compare-kitchen-en-light.png", "run.json", "offsets.md", "diff-panel-map-en-dark.png"):
            self.assertIsNone(naming.parse_file(f), f)

    def test_lists(self) -> None:
        self.assertEqual(naming.split_list("en, de,en"), ["en", "de"])
        with self.assertRaises(ValueError):
            naming.split_list(" , ")
        with self.assertRaises(ValueError):
            naming.split_list("light,sepia", naming.SCHEMES, "scheme")
        self.assertEqual(naming.ordered(["offline", "overview", "setup"]),
                         ["setup", "overview", "offline"])
        self.assertEqual(naming.browser_scheme("auto"), "light")
        self.assertEqual(naming.browser_scheme("dark"), "dark")


class DumpTest(unittest.TestCase):
    def setUp(self) -> None:
        self.roots = boxes.parse_dump(DUMP)

    def test_tree(self) -> None:
        self.assertEqual(len(self.roots), 2)  # screen + top layer
        home = boxes.find_label(self.roots, "home")
        self.assertEqual(home.box, (37, 102, 43, 19))
        self.assertEqual(home.parent.box, (15, 16, 88, 113))
        self.assertEqual(boxes.find_label(self.roots, "Panel settings").depth, 2)

    def test_finders(self) -> None:
        self.assertEqual([t.x for t in boxes.tab_bar(self.roots)], [148, 271, 436])
        self.assertEqual(boxes.rail_item(self.roots, "Leo").box, (15, 262, 88, 113))
        self.assertIsNone(boxes.rail_item(self.roots, "Overview"))  # not in the rail
        self.assertEqual(boxes.rail_lock(self.roots, "Leo").centre, (59, 752))
        self.assertEqual(boxes.rail_home(self.roots, "Leo").box, (15, 16, 88, 113))
        self.assertEqual(boxes.biggest_clock(self.roots).text, "16:23")

    def test_keypad_and_tries(self) -> None:
        roots = boxes.parse_dump(LOCKED)
        pad = boxes.keypad(roots)
        self.assertEqual(sorted(pad), sorted(list("0123456789") + ["ok"]))
        self.assertEqual(pad["1"].centre, (530, 431))
        self.assertEqual(pad["ok"].centre, (750, 653))
        self.assertEqual(boxes.tries_left(roots), 4)  # not the date's or the clock's numbers
        self.assertIsNone(boxes.tries_left(boxes.parse_dump(DUMP)))
        last = LOCKED.replace('"Tries left: 4"', '"Letzter Versuch — noch eine falsche PIN meldet ab."')
        self.assertEqual(boxes.tries_left(boxes.parse_dump(last)), 1)

    def test_panel_texts(self) -> None:
        texts = boxes.panel_texts(self.roots)
        plain = [t["text"] for t in texts if not t["joined"]]
        self.assertNotIn("Below the fold", plain)  # off screen
        joined = [t for t in texts if t["joined"] and t["text"].startswith("@leo")]
        self.assertEqual(joined[0]["text"], "@leo Shoes on! ")
        self.assertEqual(joined[0]["box"], (691, 390, 157, 26))


class MatchTest(unittest.TestCase):
    def test_normalize(self) -> None:
        self.assertEqual(boxes.normalize("MESSAGE FOR  EVERYONE"), "message for everyone")
        self.assertEqual(boxes.normalize("@leo Shoes on! 👟"), "@leo shoes on!")
        self.assertEqual(boxes.normalize("Bins out ♻️"), "bins out")
        self.assertEqual(boxes.normalize("👩🏼‍🍳"), "")

    def test_offsets_maths(self) -> None:
        panel = boxes.panel_texts(boxes.parse_dump(DUMP))
        browser = [
            {"x": 164, "y": 26, "w": 105.4, "h": 24, "text": "Overview"},
            {"x": 309.4, "y": 26, "w": 110.6, "h": 24, "text": "Messages"},
            {"x": 691, "y": 393.7, "w": 206.1, "h": 25, "text": "@leo Shoes on! 👟"},
            {"x": 45.4, "y": 358.2, "w": 28.3, "h": 19, "text": "Leo"},
            {"x": 900, "y": 700, "w": 20, "h": 20, "text": "Leo"},  # too far from the panel's
            {"x": 10, "y": 900, "w": 20, "h": 20, "text": "Notes"},  # off screen
        ]
        rows, p_only, b_only = boxes.match(panel, boxes.browser_texts(browser))
        by = {r["text"]: r for r in rows}
        self.assertEqual(set(by), {"Overview", "Messages", "@leo Shoes on! 👟", "Leo"})
        self.assertEqual((by["Overview"]["dx"], by["Overview"]["dw"], by["Overview"]["dh"]),
                         (0, -14.4, 2))
        self.assertEqual(by["Messages"]["dx"], -22.4)
        self.assertEqual(by["@leo Shoes on! 👟"]["dy"], -3.7)
        self.assertEqual(by["Leo"]["browser"], [45.4, 358.2, 28.3, 19])  # nearest pairs
        self.assertEqual([b["text"] for b in b_only], ["Leo"])
        only = {p["text"] for p in p_only}
        self.assertIn("Notes", only)
        self.assertNotIn("Shoes ", only)  # its run matched
        s = boxes.summary(rows)
        self.assertEqual(s["matched"], 4)
        self.assertEqual(s["max_dx"], 30.4)  # Leo: 15 − 45.4
        md = boxes.offsets_markdown("overview-en-light", rows, p_only, b_only)
        self.assertIn("| Messages | 309.4 / 26 / 110.6 / 24 | 287 / 26 / 101 / 26 | **-22.4** |", md)
        self.assertIn("Only in the browser: Leo", md)

    def test_repeats_pair_nearest(self) -> None:
        panel = [{"text": "12", "box": (855, 441, 29, 26), "joined": False},
                 {"text": "12", "box": (595, 579, 18, 19), "joined": False}]
        browser = [{"text": "12", "box": (593.9, 580, 20.4, 19)}]
        rows, p_only, _ = boxes.match(panel, browser)
        self.assertEqual(rows[0]["panel"], [595, 579, 18, 19])
        self.assertEqual(p_only[0]["box"], (855, 441, 29, 26))


class MetricsTest(unittest.TestCase):
    def test_pixels(self) -> None:
        hist = [0] * 256
        hist[0], hist[10], hist[16], hist[17], hist[255] = 900, 50, 20, 20, 10
        self.assertEqual(metrics.changed_from_histogram(hist, 16), 30)
        self.assertEqual(metrics.changed_from_histogram(hist, 0), 100)
        self.assertEqual(metrics.percent(30, 1000), 3.0)
        self.assertEqual(metrics.percent(1, 3), 33.333)
        self.assertEqual(metrics.percent(0, 0), 0.0)
        with self.assertRaises(ValueError):
            metrics.changed_from_histogram(hist, 256)

    def test_box_changes(self) -> None:
        old = [{"text": "Overview", "box": [164, 26, 91, 26]},
               {"text": "16:23", "box": [148, 184, 257, 102]},
               {"text": "Gone", "box": [0, 0, 1, 1]}]
        new = [{"text": "Overview", "box": [164, 27, 91, 26]},  # within tolerance
               {"text": "16:23", "box": [148, 190, 250, 102]},
               {"text": "New", "box": [0, 0, 1, 1]}]
        ch = metrics.box_changes(old, new)
        self.assertEqual([(m["text"], m["dy"], m["dw"]) for m in ch["moved"]], [("16:23", 6, -7)])
        self.assertEqual([g["text"] for g in ch["gone"]], ["Gone"])
        self.assertEqual([a["text"] for a in ch["added"]], ["New"])

    def test_offset_changes(self) -> None:
        def r(text, dx, dy):
            return {"text": text, "dx": dx, "dy": dy, "dw": 0, "dh": 0}
        out = metrics.offset_changes([r("Map", -55.5, 0), r("Notes", -40, 0)],
                                     [r("Map", 0, 0), r("Notes", -40.5, 0)])
        self.assertEqual(len(out), 1)
        self.assertEqual((out[0]["text"], out[0]["old_dx"], out[0]["new_dx"]), ("Map", -55.5, 0))


if __name__ == "__main__":
    unittest.main()
