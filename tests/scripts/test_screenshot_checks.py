# SPDX-License-Identifier: MPL-2.0
"""Tests for scripts/screenshots/checks.py and check.py (what CI asserts)."""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "screenshots"))

import boxes  # noqa: E402
import cdp  # noqa: E402
import check  # noqa: E402
import checks  # noqa: E402

SHOT = {"screen": "overview", "locale": "de", "scheme": "dark"}


def row(text, dx=0.0, dy=0.0, dw=0.0, dh=0.0):
    return {"text": text, "browser": [100, 100, 50, 20], "panel": [100 + dx, 100 + dy, 50 + dw, 20 + dh],
            "dx": dx, "dy": dy, "dw": dw, "dh": dh}


class AcceptedTest(unittest.TestCase):
    def test_load(self) -> None:
        acc = checks.load_accepted({
            "tolerance_px": 3,
            "offsets": [{"screens": ["reader"], "text": "^mia$", "max": {"dx": 12}, "reason": "r"}],
            "texts": [{"screens": ["*"], "side": "browser", "words": ["Unlock"], "reason": "r"}],
            "layout": [{"screens": ["night"], "text": "overlap", "reason": "r"}],
            "log": [{"match": "lvgl took", "reason": "r"}],
        })
        self.assertEqual(acc.tolerance, 3)
        self.assertEqual(acc.offsets[0].max, {"dx": 12.0})
        self.assertEqual(acc.texts[0].words, frozenset({"unlock"}))
        self.assertEqual(len(acc.all()), 4)

    def test_every_entry_needs_a_reason_and_a_known_shape(self) -> None:
        bad = [
            {"offsets": [{"screens": ["reader"], "max": {"dx": 9}}]},
            {"offsets": [{"screens": ["nope"], "max": {"dx": 9}, "reason": "r"}]},
            {"offsets": [{"screens": ["reader"], "max": {"dz": 9}, "reason": "r"}]},
            {"offsets": [{"screens": ["reader"], "text": "(", "max": {"dx": 9}, "reason": "r"}]},
            {"texts": [{"side": "both", "words": ["a"], "reason": "r"}]},
            {"texts": [{"side": "panel", "words": [], "reason": "r"}]},
            {"layout": [{"reason": "r"}]},
            {"log": [{"reason": "r"}]},
            [],
        ]
        for data in bad:
            with self.subTest(data=data), self.assertRaises(checks.AcceptedError):
                checks.load_accepted(data)

    def test_the_repo_allow_list_loads(self) -> None:
        path = ROOT / "scripts" / "screenshots" / "accepted.json"
        acc = checks.load_accepted(json.loads(path.read_text(encoding="utf-8")))
        self.assertTrue(all(r.reason for r in acc.all()))

    def test_date_rules_hold_every_day(self) -> None:
        """The date heading is accepted on any weekday and month, not only the
        day the rules were written (they once named Wednesday and October)."""
        path = ROOT / "scripts" / "screenshots" / "accepted.json"
        acc = checks.load_accepted(json.loads(path.read_text(encoding="utf-8")))
        en_days = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]
        de_days = ["Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag"]
        en_months = ["January", "February", "March", "April", "May", "June", "July",
                     "August", "September", "October", "November", "December"]
        de_months = ["Januar", "Februar", "März", "April", "Mai", "Juni", "Juli",
                     "August", "September", "Oktober", "November", "Dezember"]
        dates = {
            "en": [f"{d}, {m} {n}" for d in en_days for m in en_months for n in (1, 17, 31)],
            "de": [f"{d}, {n}. {m}" for d in de_days for m in de_months for n in (1, 17, 31)],
        }
        # The offsets CI saw for the date (Saturday, 17 October 2026).
        seen = {"overview": {"dx": 0, "dy": -2.2, "dw": -3.3, "dh": 5},
                "offline": {"dx": 0, "dy": -2.2, "dw": -3.3, "dh": 5},
                "night": {"dx": 1.9, "dy": -3, "dw": -1.8, "dh": 40}}
        for screen, delta in seen.items():
            for locale, texts in dates.items():
                shot = {"screen": screen, "locale": locale, "scheme": "light"}
                rows = [{"text": text, **delta} for text in texts]
                with self.subTest(screen=screen, locale=locale):
                    self.assertEqual(checks.offset_failures(shot, rows, acc), [])

    def test_scope(self) -> None:
        acc = checks.load_accepted({"offsets": [
            {"screens": ["overview"], "locales": ["en"], "max": {"dx": 9}, "reason": "r"}]})
        self.assertFalse(acc.offsets[0].scoped(SHOT))
        self.assertTrue(acc.offsets[0].scoped({**SHOT, "locale": "en"}))


class ExpectedTest(unittest.TestCase):
    def test_files(self) -> None:
        files = checks.expected_files({"screens": ["setup", "overview", "settings"],
                                       "locales": ["en"], "schemes": ["light", "dark"]})
        self.assertIn("panel-setup.png", files)
        self.assertNotIn("browser-setup.png", files)
        self.assertIn("compare-overview-en-dark.png", files)
        self.assertIn("offsets-overview-en-light.md", files)
        self.assertIn("boxes-settings-en-light.json", files)
        self.assertNotIn("browser-settings-en-light.png", files)
        self.assertEqual(len(files), 2 + 2 * 5 + 2 * 2)

    def test_notes(self) -> None:
        notes = ["overview-en-light: panel 16:04 vs browser 16:05 (minute turned)",
                 "offline-en-light: offline not reached (browser True, panel False)",
                 "wrong-pin-en-light: panel says None tries left after 1 wrong PINs (expected 4)"]
        self.assertEqual(checks.note_failures(notes), notes[1:])


class OffsetTest(unittest.TestCase):
    def test_tolerance_and_rules(self) -> None:
        acc = checks.load_accepted({"offsets": [
            {"screens": ["overview"], "text": "^karte$", "max": {"dx": 70, "dw": 10}, "reason": "r"}]})
        rows = [row("Übersicht", dx=4, dw=-4), row("Karte", dx=-66, dw=-9), row("Karte", dx=-71),
                row("Notizen", dy=5)]
        bad = checks.offset_failures(SHOT, rows, acc)
        self.assertEqual([(b["text"], b["over"]) for b in bad], [("Karte", ["dx"]), ("Notizen", ["dy"])])
        self.assertTrue(acc.offsets[0].used)
        self.assertEqual(checks.offset_failures({**SHOT, "screen": "map"}, rows[1:2], acc)[0]["text"], "Karte")

    def test_far_rows(self) -> None:
        panel = [{"text": "Leo", "box": (500, 600, 30, 20), "joined": False, "group": 1}]
        browser = [{"text": "Leo", "box": (100, 100, 30, 20)}]
        rows = checks.far_rows(panel, browser)
        self.assertEqual((rows[0]["dx"], rows[0]["dy"], rows[0]["far"]), (400, 500, True))


CLOCK_OVER_NOTE = """0 0 1280 800
  120 0 1160 800
    148 70 600 40
      160 78 400 24 "Offline — showing what we had at 16:02"
    148 60 257 102 "16:20"
    148 300 200 20 "Wednesday"
    148 320 200 20 "Leo"
"""

FINE = """0 0 1280 800
  120 0 1160 800
    537 51 205 26 "The Vizeli Household"
    524 70 232 113 "16:08"
    1000 200 60 20
      990 200 100 20 "Cut at the side"
    148 780 300 26 "Bottom row of a list"
    1250 400 60 20 "Off the edge"
0 0 1280 800
  263 256 204 38 "Panel settings"
"""


class FarTest(unittest.TestCase):
    def test_only_unique_texts_without_digits(self) -> None:
        panel = [{"text": "3 min ago", "box": (900, 600, 60, 20), "joined": False, "group": 1},
                 {"text": "Leo", "box": (500, 600, 30, 20), "joined": False, "group": 2}]
        browser = [{"text": "3 min ago", "box": (100, 100, 60, 20)}, {"text": "Leo", "box": (100, 100, 30, 20)},
                   {"text": "Leo", "box": (10, 10, 30, 20)}]
        self.assertEqual(checks.far_rows(panel, browser[:2]), [checks.far_rows(panel[1:], browser[1:2])[0]])
        self.assertEqual(checks.far_rows(panel, browser[:2], panel, browser), [])


class SuggestTest(unittest.TestCase):
    def test_entries(self) -> None:
        shots = [{"screen": "overview", "offsets": [{**row("16:20", dy=9), "over": ["dy"]}],
                  "only_panel": ["zuhause"], "only_browser": [], "layout": ["overlap: 'a' at [1]"]},
                 {"screen": "night", "offsets": [{**row("16:21", dy=-12), "over": ["dy"]}],
                  "only_panel": [], "only_browser": [], "layout": []}]
        sug = check.suggest(shots)
        self.assertEqual(sug["offsets"], [{"screens": ["night", "overview"], "text": r"^\d+:\d+$",
                                           "max": {"dy": 15}, "reason": "TODO"}])
        self.assertEqual(sug["texts"][0]["words"], ["zuhause"])
        acc = checks.load_accepted({**sug, "texts": [], "layout": []})
        self.assertEqual(checks.offset_failures(SHOT | {"screen": "night"}, [row("09:41", dy=12)], acc), [])


class LayoutTest(unittest.TestCase):
    def test_overlap(self) -> None:
        probs = checks.layout_problems(boxes.parse_dump(CLOCK_OVER_NOTE))
        self.assertEqual(len(probs), 1)
        self.assertIn("overlap", probs[0])
        self.assertIn("16:20", probs[0])

    def test_cut_and_off_screen_but_not_lists_or_sheets(self) -> None:
        probs = checks.layout_problems(boxes.parse_dump(FINE))
        self.assertEqual(len(probs), 2, probs)
        self.assertTrue(probs[0].startswith("cut sideways: 'Cut at the side'"))
        self.assertTrue(probs[1].startswith("off-screen: 'Off the edge'"))

    def test_allow(self) -> None:
        acc = checks.load_accepted({"layout": [{"screens": ["overview"], "text": "Off the edge",
                                                "reason": "r"}]})
        probs = checks.layout_problems(boxes.parse_dump(FINE))
        self.assertEqual(len(checks.layout_failures(SHOT, probs, acc)), 1)


class WordsTest(unittest.TestCase):
    def test_words(self) -> None:
        self.assertEqual(checks.words("16:35 · in 30 min"), ["in", "min"])
        self.assertEqual(checks.words("14:45–15:30 Art 🎨"), ["art"])
        self.assertEqual(checks.words("See all ›"), ["see", "all"])
        self.assertEqual(checks.words("Gran's"), ["gran's"])

    def test_differences(self) -> None:
        panel = ["16:35 Piano", "See all ›", "@here Leaving work no...", "Zuhause"]
        browser = ["16:36", "Piano", "See all", "›", "@here Leaving work now, I'll pick up …", "Unlock"]
        self.assertEqual(checks.word_differences(panel, browser), ({"zuhause"}, {"unlock"}))

    def test_allow(self) -> None:
        acc = checks.load_accepted({"texts": [{"screens": ["overview"], "side": "browser",
                                               "words": ["unlock"], "reason": "r"}]})
        self.assertEqual(checks.word_failures(SHOT, {"zuhause"}, {"unlock"}, acc), (["zuhause"], []))
        self.assertEqual(checks.word_failures(SHOT, {"unlock"}, set(), acc), (["unlock"], []))


class WeightTest(unittest.TestCase):
    DUMP = ("0 0 1280 800\n"
            "  1168 19 81 44 @700 \"16:13\"\n"
            "  164 26 83 23 @700 \"Overview\"\n"
            "  148 577 176 18 @400 \"Shoes on…\"\n"
            "  300 300 50 20 \"old dump\"\n")
    BROWSER = [
        {"x": 1168.2, "y": 18.5, "w": 83.8, "h": 42, "text": "16:13", "font": "36px 300"},
        {"x": 164, "y": 26, "w": 83, "h": 24, "text": "Overview", "font": "20px 600"},
        {"x": 148, "y": 575.5, "w": 80, "h": 18, "text": "Shoes on…", "font": "15px 500"},
        {"x": 300, "y": 300, "w": 50, "h": 20, "text": "old dump", "font": "16px 700"},
    ]

    def test_dump_and_css_weights(self) -> None:
        roots = boxes.parse_dump(self.DUMP)
        labels = [n for n in roots[0].walk() if n.text]
        self.assertEqual([n.weight for n in labels], [700, 700, 400, None])
        self.assertEqual(labels[0].text, "16:13")
        self.assertEqual(boxes.css_weight("24px 400"), 400)
        self.assertEqual(boxes.css_weight("22.5px 600"), 600)
        self.assertIsNone(boxes.css_weight("24px"))
        self.assertIsNone(boxes.css_weight(None))

    def test_classes(self) -> None:
        self.assertEqual([checks.weight_class(w) for w in (200, 300, 400, 500, 600, 700)],
                         ["light", "light", "regular", "regular", "bold", "bold"])

    def test_bold_clock_over_a_light_one_fails(self) -> None:
        rows, _, _ = boxes.match(boxes.panel_texts(boxes.parse_dump(self.DUMP)),
                                 boxes.browser_texts(self.BROWSER))
        acc = checks.load_accepted({})
        # 600 → 700 and 500 → 400 are the same class; an unknown panel
        # weight (an older dump) is never compared.
        self.assertEqual(checks.weight_failures(SHOT, rows, acc),
                         [{"text": "16:13", "browser": 300, "panel": 700}])
        acc = checks.load_accepted({"weights": [{"screens": ["overview"], "text": r"^\d+:\d+$",
                                                 "reason": "r"}]})
        self.assertEqual(checks.weight_failures(SHOT, rows, acc), [])
        self.assertTrue(acc.weights[0].used)
        with self.assertRaises(checks.AcceptedError):
            checks.load_accepted({"weights": [{"screens": ["overview"], "reason": "r"}]})


class LogTest(unittest.TestCase):
    def test_logs(self) -> None:
        panel = ("\x1b[0;32m[I][app:1]: fine\x1b[0m\n"
                 "\x1b[1;31m[E][socialhome_display:9]: parse failed\x1b[0m\n"
                 "[E][known:1]: harmless\nSegmentation fault (core dumped)\n")
        household = "READY\nTraceback (most recent call last):\n  File x\n"
        acc = checks.load_accepted({"log": [{"match": r"\[E\]\[known:", "reason": "r"}]})
        bad = checks.log_failures(panel, household, acc)
        self.assertEqual(bad, ["panel.log: [E][socialhome_display:9]: parse failed",
                               "panel.log: Segmentation fault (core dumped)",
                               "household.log: Traceback (most recent call last):"])


class FontconfigTest(unittest.TestCase):
    def test_xml(self) -> None:
        xml = cdp.fontconfig_xml("/w/fonts & more", "/w/cache")
        self.assertIn("<dir>/w/fonts &amp; more</dir>", xml)
        self.assertIn("<family>system-ui</family><prefer><family>Roboto</family>", xml)
        self.assertIn("<family>sans-serif</family><prefer><family>Roboto</family>", xml)
        self.assertIn("/etc/fonts/fonts.conf", xml)


DUMP = """0 0 1280 800
  120 0 1160 800
    164 26 91 26 "Overview"
    148 184 257 102 "16:23"
"""


class CheckCliTest(unittest.TestCase):
    def run_dir(self, d: str, dx: float) -> None:
        meta = {"screens": ["overview"], "locales": ["en"], "schemes": ["light"], "notes": [],
                "socialhome_commit": "a" * 40, "firmware_commit": "b" * 40, "took_s": 60}
        Path(d, "run.json").write_text(json.dumps(meta))
        for kind in ("panel", "browser", "compare"):
            Path(d, f"{kind}-overview-en-light.png").write_bytes(b"")
        Path(d, "offsets-overview-en-light.md").write_text("")
        browser = [{"x": 164 - dx, "y": 26, "w": 91, "h": 26, "text": "Overview"},
                   {"x": 148, "y": 184, "w": 257, "h": 102, "text": "16:24"}]
        rows, _, _ = boxes.match(boxes.panel_texts(boxes.parse_dump(DUMP)), boxes.browser_texts(browser))
        Path(d, "boxes-overview-en-light.json").write_text(json.dumps(
            {"panel_dump": DUMP, "browser": browser, "offsets": rows}))
        os.makedirs(os.path.join(d, "logs"))
        Path(d, "logs", "panel.log").write_text("[I][x:1]: ok\n")

    def main(self, d: str, accepted: dict) -> tuple[int, str]:
        acc = os.path.join(d, "accepted.json")
        Path(acc).write_text(json.dumps(accepted))
        out = StringIO()
        with redirect_stdout(out):
            code = check.main([d, "--accepted", acc, "--summary", os.path.join(d, "summary.md")])
        return code, out.getvalue()

    def test_pass_and_fail(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            self.run_dir(d, dx=3)
            code, text = self.main(d, {})
            self.assertEqual(code, 0, text)
            self.assertIn("Panel UI passed", text)
        with tempfile.TemporaryDirectory() as d:
            self.run_dir(d, dx=20)
            code, text = self.main(d, {})
            self.assertEqual(code, 1)
            self.assertIn("offset: 'Overview'", text)
            self.assertIn("dx +20", text)
            self.assertEqual(Path(d, "summary.md").read_text().strip(), text.strip())
            code, text = self.main(d, {"offsets": [{"screens": ["overview"], "text": "overview",
                                                    "max": {"dx": 25}, "reason": "r"}]})
            self.assertEqual(code, 0, text)

    def test_missing_files_fail(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            self.run_dir(d, dx=0)
            os.remove(os.path.join(d, "compare-overview-en-light.png"))
            code, text = self.main(d, {})
            self.assertEqual(code, 1)
            self.assertIn("compare-overview-en-light.png", text)
        with tempfile.TemporaryDirectory() as d:
            code, text = self.main(d, {})
            self.assertEqual(code, 1)
            self.assertIn("did not finish", text)


if __name__ == "__main__":
    unittest.main()
