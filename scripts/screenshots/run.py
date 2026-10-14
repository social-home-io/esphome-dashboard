#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Screenshot the wall panel next to the browser wall dashboard.

One command: starts a scratch Social Home from a checkout (with the demo
household), builds and pairs the host SDL panel, and for every locale ×
scheme × screen takes the panel's picture and the browser's (headless
Chromium, 1280×800) a moment apart, with the boxes of both. Stops
everything it started, also on failure or Ctrl-C.

    .venv/bin/python scripts/screenshots/run.py \\
        --socialhome ../socialhome --out /tmp/shots \\
        --locale en,de --scheme light,dark

Writes to --out (names: naming.py):
  panel-*.png, browser-*.png   the two screens
  compare-*.png                browser left, panel right, labelled
  boxes-*.json                 panel dump + browser boxes + their offsets
  offsets-*.md, offsets.md     panel − browser per text; the run's summary
  run.json                     commits, household time zone, shot times
  logs/                        household, panel, build

Compare two runs with diff.py. See .claude/skills/panel-screenshots/SKILL.md.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from zoneinfo import ZoneInfo

import boxes
import naming
from cdp import Browser
from images import side_by_side
from panel import Panel, PanelError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
HOUSEHOLD = "The Vizeli Household"
WRONG_PIN = "1111"
LOCK_SCREENS = ("lock", "pinpad", "wrong-pin", "last-try")


class RunError(RuntimeError):
    pass


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--socialhome", required=True,
                   help="a socialhome checkout (the commit tests/fixtures/display-api/SOURCE pins, "
                        "or the PR branch being paired)")
    p.add_argument("--socialhome-python",
                   help="Python with Social Home's dependencies (default: <checkout>/.venv/bin/python); "
                        "the checkout goes first on its PYTHONPATH, nothing is installed")
    p.add_argument("--out", required=True, help="output directory")
    p.add_argument("--locale", default="en", help="comma-separated, e.g. en,de (default en)")
    p.add_argument("--scheme", default="light,dark",
                   help="comma-separated: light, dark, auto (default light,dark)")
    p.add_argument("--screens", default=",".join(naming.SCREENS),
                   help="comma-separated (default all): " + ",".join(naming.SCREENS))
    p.add_argument("--ports", type=int, default=19780,
                   help="base port: Social Home on BASE, its dashboard on BASE+1, "
                        "the run's control API on BASE+2 (default 19780)")
    p.add_argument("--esphome", help="esphome executable (default: .venv/bin/esphome, else PATH)")
    p.add_argument("--panel-binary",
                   help="a host-sdl program built already (skips the build; the address "
                        "screen then has no household row and the URL is typed)")
    p.add_argument("--chromium", help="Chromium executable (default: chromium on PATH)")
    p.add_argument("--time-zone",
                   help="household time zone (default: a weekday zone where it is about 16:05 now); "
                        "give an earlier run's (run.json) to compare the same board")
    p.add_argument("--keep-work", action="store_true",
                   help="keep the scratch dir (Social Home data, panel prefs) for debugging")
    a = p.parse_args(argv)
    try:
        a.locales = [naming.check_locale(v) for v in naming.split_list(a.locale, None, "locale")]
        a.schemes = naming.split_list(a.scheme, naming.SCHEMES, "scheme")
        a.screen_list = naming.ordered(naming.split_list(a.screens, naming.SCREENS, "screen"))
    except ValueError as e:
        p.error(str(e))
    return a


def port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # as aiohttp: TIME_WAIT is fine
        try:
            s.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def git_head(path: str) -> str:
    try:
        head = subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True,
                              text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", path, "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True).stdout.strip()
        return head + ("+dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


class Run:
    def __init__(self, a: argparse.Namespace) -> None:
        self.a = a
        self.out = os.path.abspath(a.out)
        self.logs = os.path.join(self.out, "logs")
        os.makedirs(self.logs, exist_ok=True)
        self.work = tempfile.mkdtemp(prefix="sh-shots-")
        self.main_port, self.dash_port, self.ctl_port = a.ports, a.ports + 1, a.ports + 2
        self.dbase = f"http://127.0.0.1:{self.dash_port}"
        self.household: subprocess.Popen | None = None
        self.browser: Browser | None = None
        self.panel: Panel | None = None
        self.shots: list[dict] = []
        self.summary: list[dict] = []
        self.notes: list[str] = []
        self.tz: ZoneInfo | None = None

    # ---- helpers -----------------------------------------------------------

    def log(self, msg: str) -> None:
        print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)

    def ctl(self, method: str, path: str, **query: str) -> str:
        url = f"http://127.0.0.1:{self.ctl_port}{path}"
        if query:
            url += "?" + urllib.parse.urlencode(query)
        req = urllib.request.Request(url, method=method, data=b"" if method == "POST" else None)
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read().decode()
        except urllib.error.HTTPError as e:
            raise RunError(f"{method} {path}: {e.code} {e.read().decode()[:300]}") from None

    def minute(self, ts: float) -> str:
        return datetime.fromtimestamp(ts, self.tz).strftime("%H:%M")

    def quiet_minute(self, needed: float) -> None:
        """Wait for the next minute when the shots wouldn't fit in this one."""
        sec = datetime.now(self.tz).second + datetime.now(self.tz).microsecond / 1e6
        if sec + needed > 59:
            time.sleep(60.5 - sec)

    def path(self, kind: str, name: str) -> str:
        return os.path.join(self.out, naming.file_name(kind, name))

    # ---- setup -------------------------------------------------------------

    def preflight(self) -> None:
        a = self.a
        self.checkout = os.path.abspath(a.socialhome)
        if not os.path.isfile(os.path.join(self.checkout, "socialhome", "__init__.py")):
            raise RunError(f"{self.checkout} is not a socialhome checkout")
        if not os.path.isfile(os.path.join(self.checkout, "socialhome", "_version.py")):
            raise RunError(f"{self.checkout}/socialhome/_version.py is missing (hatch-vcs writes it "
                           "on install): copy it from another checkout")
        self.python = a.socialhome_python or os.path.join(self.checkout, ".venv", "bin", "python")
        if not os.path.exists(self.python):
            raise RunError(f"no Python for Social Home at {self.python}: pass --socialhome-python")
        self.chromium = a.chromium or next(
            (p for p in map(shutil.which, ("chromium", "chromium-browser", "google-chrome")) if p),
            None)
        if not self.chromium:
            raise RunError("no Chromium found: install chromium (the devcontainer has it) "
                           "or pass --chromium")
        for port in (self.main_port, self.dash_port, self.ctl_port):
            if not port_free(port):
                raise RunError(f"port {port} is in use: pick another --ports base")

    def build_panel(self) -> str:
        if self.a.panel_binary:
            return os.path.abspath(self.a.panel_binary)
        esphome = self.a.esphome or (os.path.join(ROOT, ".venv", "bin", "esphome")
                                     if os.path.exists(os.path.join(ROOT, ".venv", "bin", "esphome"))
                                     else shutil.which("esphome"))
        if not esphome:
            raise RunError("no esphome: run .devcontainer/post-create.sh or pass --esphome")
        # The address screen lists the household (the host build has no mDNS).
        stub = f"{HOUSEHOLD}|socialhome-demo|127.0.0.1|{self.dash_port}|api=1,id=demo,path=/api/display/v1/"
        self.log("building the host SDL panel")
        env = {k: v for k, v in os.environ.items() if k != "VIRTUAL_ENV"}
        with open(os.path.join(self.logs, "build.log"), "w") as f:
            r = subprocess.run([esphome, "-s", "discovery_stub", stub, "compile", "builds/host-sdl.yaml"],
                               cwd=ROOT, env=env, stdout=f, stderr=subprocess.STDOUT)
        if r.returncode != 0:
            raise RunError("host-sdl build failed; see logs/build.log")
        built = os.path.join(ROOT, "builds", ".esphome", "build", "socialhome-host", ".pioenvs",
                             "socialhome-host", "program")
        # A copy: another build in this checkout can't swap it mid-run.
        program = os.path.join(self.work, "panel-program")
        shutil.copy2(built, program)
        return program

    def start_household(self) -> None:
        env = {k: v for k, v in os.environ.items() if k not in ("VIRTUAL_ENV", "PYTHONHOME")}
        env["PYTHONPATH"] = self.checkout
        self.log(f"starting Social Home from {self.checkout}")
        self.household_log = os.path.join(self.logs, "household.log")
        with open(self.household_log, "w") as f:
            self.household = subprocess.Popen(
                [self.python, os.path.join(HERE, "household.py"), str(self.main_port),
                 str(self.dash_port), str(self.ctl_port), os.path.join(self.work, "data"),
                 self.a.time_zone or ""],
                cwd=self.work, env=env, stdin=subprocess.DEVNULL, stdout=f,
                stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            with open(self.household_log, encoding="utf-8", errors="replace") as f:
                text = f.read()
            if "READY" in text:
                break
            if self.household.poll() is not None:
                raise RunError("Social Home exited while seeding; see logs/household.log:\n"
                               + text[-1500:])
            time.sleep(0.5)
        else:
            raise RunError("Social Home did not come up in 240 s; see logs/household.log")
        self.info = json.loads(self.ctl("GET", "/info"))
        self.tz = ZoneInfo(self.info["tz"])
        self.log(f"household ready: time zone {self.info['tz']}, local "
                 f"{datetime.now(self.tz).strftime('%a %H:%M')}")

    def wait_board(self, timeout: float = 90) -> list[boxes.Node]:
        """Poll the panel until it shows the household board (the tabs)."""
        assert self.panel
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            tree = self.panel.dump()
            if len(boxes.tab_bar(tree)) >= 3:
                return tree
            time.sleep(1)
        raise RunError("the panel never showed the board; see logs/panel.log")

    def pair_panel(self, program: str) -> None:
        self.panel = Panel(program, self.work, os.path.join(self.logs, "panel.log"))
        self.panel.start(fresh=True)
        row = None
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and row is None:
            tree = self.panel.dump()
            row = boxes.find_label(tree, HOUSEHOLD)
            if row is None and boxes.find_all(tree, lambda n: n.text == "") and self.a.panel_binary:
                break  # no discovery stub in this build: type the address
            time.sleep(0.5)
        if "setup" in self.a.screen_list:
            self.panel_only("setup")
        mark = self.panel.log_mark()
        if row is not None:
            self.panel.run(self.panel.tap(row), "wait 8")
        else:
            self.panel.run(f"type {self.dbase}", "wait 8")
        m = self.panel.wait_for_log(r"Pairing code ([A-Z0-9]+)", mark, 30)
        if not m:
            raise RunError("the panel showed no pairing code; see logs/panel.log")
        if "pairing" in self.a.screen_list:
            self.panel_only("pairing")
        self.ctl("POST", "/pair", code=m.group(1), name="Kitchen panel")
        self.log(f"paired the panel (code {m.group(1)})")
        self.wait_board()

    # ---- shots -------------------------------------------------------------

    def save_boxes(self, name: str, dump_txt: str | None, browser_items: list[dict] | None,
                   times: dict) -> dict:
        dump_text = ""
        if dump_txt:
            with open(dump_txt, encoding="utf-8") as f:
                dump_text = f.read()
            os.remove(dump_txt)
        data: dict = {"name": name, "times": times, "panel_dump": dump_text,
                      "browser": browser_items or []}
        if browser_items is not None:
            rows, p_only, b_only = boxes.match(boxes.panel_texts(boxes.parse_dump(dump_text)),
                                               boxes.browser_texts(browser_items))
            data["offsets"] = rows
            with open(self.path("offsets", name), "w", encoding="utf-8") as f:
                f.write(boxes.offsets_markdown(name, rows, p_only, b_only))
            self.summary.append({"name": name, **boxes.summary(rows), **times})
        with open(self.path("boxes", name), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
        return data

    def panel_only(self, screen: str, suffix: tuple[str, str] = ("", "")) -> None:
        assert self.panel
        name = naming.shot_name(screen, *suffix) if suffix[0] else screen
        dump_txt = os.path.join(self.work, name + ".dump")
        t = self.panel.capture(name, self.path("panel", name), dump_txt, before=("wait 4",))
        times = {"panel": datetime.fromtimestamp(t, self.tz).isoformat(timespec="seconds"),
                 "panel_minute": self.minute(t) if self.tz else ""}
        self.save_boxes(name, dump_txt, None, times)
        self.shots.append({"name": name, "panel_only": True, **times})
        self.log(f"  {name} (panel only)")

    def pair_shot(self, name: str, *, url: str | None, panel_steps: list[str],
                  browser_js: str | None = None, browser_wait: float = 1.5,
                  window: float = 20, retry: bool = True) -> None:
        """Browser to the screen, panel to the screen + shot, browser shot."""
        assert self.panel and self.browser
        for attempt in (1, 2):
            self.quiet_minute(window)
            if url is not None:
                self.browser.navigate(self.dbase + url)
            if browser_js:
                self.browser.js(browser_js, wait=browser_wait)
            dump_txt = os.path.join(self.work, name + ".dump")
            tp = self.panel.capture(name, self.path("panel", name), dump_txt,
                                    before=tuple(panel_steps))
            self.browser.settle(4000)
            # No focus ring from the script's own clicks (a finger leaves none).
            self.browser.js("document.activeElement && document.activeElement.blur()")
            self.browser.screenshot(self.path("browser", name))
            tb = time.time()
            items = self.browser.boxes()
            same = self.minute(tp) == self.minute(tb)
            if same or not retry or attempt == 2:
                break
            self.log(f"  {name}: the minute turned between the shots, again")
        times = {"panel": datetime.fromtimestamp(tp, self.tz).isoformat(timespec="seconds"),
                 "browser": datetime.fromtimestamp(tb, self.tz).isoformat(timespec="seconds"),
                 "panel_minute": self.minute(tp), "browser_minute": self.minute(tb),
                 "same_minute": same, "gap_s": round(tb - tp, 1)}
        self.save_boxes(name, dump_txt, items, times)
        side_by_side(self.path("browser", name), self.path("panel", name), self.path("compare", name),
                     f"Browser dashboard  ·  {name}", f"Wall panel (host SDL)  ·  {name}")
        self.shots.append({"name": name, **times})
        if not same:
            self.notes.append(f"{name}: panel {times['panel_minute']} vs browser "
                              f"{times['browser_minute']} (minute turned)")
        self.log(f"  {name} at {times['panel_minute']}" + ("" if same else " (minutes differ)"))

    # ---- one locale × scheme -------------------------------------------------

    def combination(self, locale: str, scheme: str) -> None:
        assert self.panel and self.browser
        self.log(f"{locale} / {scheme}")
        self.ctl("POST", "/locale", l=locale)
        self.ctl("POST", "/theme", mode=scheme)
        self.ctl("POST", "/night", on="0")
        self.ctl("POST", "/unlock")
        # A restart fetches the new theme (language, palette) at once.
        self.panel.start()
        tree = self.wait_board()
        self.panel.run("wait 12")  # pictures
        self.browser.scheme(naming.browser_scheme(scheme))
        self.cookie = self.ctl("GET", "/cookie")
        self.browser.cookie(self.dbase, "sh_dash", self.cookie)
        tabs = boxes.tab_bar(tree)
        home = boxes.rail_home(tree, "Anna")
        if home is None or len(tabs) < 4:
            raise RunError("can't find the panel's rail / tabs; see logs/panel.log")
        users = self.info["users"]
        go_home = [self.panel.tap(home), "wait 4", self.panel.tap(tabs[0]), "wait 4"]
        screens = self.a.screen_list
        sfx = (locale, scheme)

        def name(screen: str) -> str:
            return naming.shot_name(screen, *sfx)

        tab_shots = {"overview": ("/", 0, 8), "messages": ("/messages", 1, 8),
                     "notes": ("/notes", 2, 8), "map": ("/map", 3, 28)}
        for screen, (url, k, wait) in tab_shots.items():
            if screen in screens:
                self.pair_shot(name(screen), url=url, panel_steps=go_home[:2] + [
                    self.panel.tap(tabs[k]), f"wait {wait}"])
        for screen, who in (("reader", "Mia"), ("picture", "Leo")):
            if screen in screens:
                item = boxes.rail_item(tree, who)
                if item is None:
                    raise RunError(f"no {who} in the panel's rail")
                key = "mia" if who == "Mia" else "leo"
                self.pair_shot(name(screen), url=f"/p/{users[key]}",
                               panel_steps=[self.panel.tap(item), "wait 12"])
        if "settings" in screens:
            self.panel.run(*go_home)
            clock = boxes.biggest_clock(self.panel.dump())
            if clock is None:
                raise RunError("no clock to hold for the panel settings")
            self.panel.run(f"hold {clock.centre[0]} {clock.centre[1]}", "wait 26")
            self.panel_only("settings", suffix=sfx)
            self.close_sheet()
        if "night" in screens:
            # Home first: a tap at night wakes the board for a minute.
            self.panel.run(*go_home)
            self.ctl("POST", "/night", on="1")
            self.pair_shot(name("night"), url="/", panel_steps=["wait 60"], window=30)
            self.ctl("POST", "/night", on="0")
            self.panel.run("wait 60")
        if any(s in screens for s in LOCK_SCREENS):
            self.lock_flow(name, tree, go_home)
        if "offline" in screens:
            self.offline(name("offline"), go_home)

    def close_sheet(self) -> None:
        """Close the panel settings sheet with its last button."""
        assert self.panel
        tree = self.panel.dump()
        layer = tree[-1] if len(tree) > 1 else None
        buttons = [n for n in layer.walk() if len(n.kids) == 1 and n.kids[0].text] if layer else []
        if buttons:
            self.panel.run(self.panel.tap(buttons[-1]), "wait 4")

    def lock_flow(self, name, tree, go_home) -> None:
        """Lock both, open the PIN pad, then wrong PINs until one try is left.

        Panel and browser each have their own tries (the panel's per panel,
        the browser's per tablet session; five each): four wrong PINs leave
        one. One more would sign the panel out (unpair it), so the panel's
        own count is checked after every try. The admin unlock at the end
        (and at the start of every locale / scheme) gives fresh tries."""
        assert self.panel and self.browser
        screens = self.a.screen_list
        lock = boxes.rail_lock(tree, "Anna")
        if lock is None:
            raise RunError("no lock button in the panel's rail")
        # The browser locks a session of its own, not the run's main one.
        self.browser.cookie(self.dbase, "sh_dash", self.ctl("GET", "/cookie"))
        self.browser.navigate(self.dbase + "/")
        blur = "document.activeElement && document.activeElement.blur();"
        try:
            self.panel.run(*go_home)
            submit = "document.querySelector('form[action=\"/lock\"]').submit();"
            if "lock" in screens:
                self.pair_shot(name("lock"), url=None, panel_steps=[self.panel.tap(lock), "wait 16"],
                               browser_js=submit, browser_wait=2.5, retry=False)
            else:
                self.browser.js(submit, wait=2.5)
                self.panel.run(self.panel.tap(lock), "wait 16")
            clock = boxes.biggest_clock(self.panel.dump())
            if clock is None or clock.parent is None:
                raise RunError("the panel did not lock")
            pin_button = clock.parent.kids[-1]
            summary = "document.querySelector('.dash-lock-summary').click();" + blur
            if "pinpad" in screens:
                self.pair_shot(name("pinpad"), url=None, panel_steps=[self.panel.tap(pin_button), "wait 4"],
                               browser_js=summary, browser_wait=1, retry=False)
            else:
                self.browser.js(summary, wait=1)
                self.panel.run(self.panel.tap(pin_button), "wait 4")
            if not any(s in screens for s in ("wrong-pin", "last-try")):
                return
            keys = "".join(f"document.querySelector('[data-key=\"{d}\"]').click();" for d in WRONG_PIN)
            wrong_js = keys + "document.querySelector('[data-key=\"ok\"]').click();" + blur
            for attempt in range(1, 5):
                self.browser.js(wrong_js, wait=2.5)
                left = self.panel_wrong_pin(expect=5 - attempt)
                if left != 5 - attempt:
                    self.notes.append(f"{name('wrong-pin')}: panel says {left} tries left after "
                                      f"{attempt} wrong PINs (expected {5 - attempt})")
                    break
                shot = {1: "wrong-pin", 4: "last-try"}.get(attempt)
                if shot in screens:
                    self.pair_shot(name(shot), url=None, panel_steps=["wait 2"], retry=False)
        finally:
            self.ctl("POST", "/unlock")
            self.browser.cookie(self.dbase, "sh_dash", self.cookie)
            self.wait_board(60)

    def panel_wrong_pin(self, expect: int) -> int | None:
        """A wrong PIN on the panel; again if the panel didn't send it (it
        ignores keys while an earlier PIN is still being checked). The tries
        left after it."""
        assert self.panel
        left = None
        for _ in range(3):
            pad = boxes.keypad(self.panel.dump())
            if pad is None:
                raise RunError("no PIN pad on the panel")
            self.panel.run(*[self.panel.tap(pad[d]) for d in WRONG_PIN], self.panel.tap(pad["ok"]),
                           "wait 16")
            left = boxes.tries_left(self.panel.dump())
            # Unknown (no note found) or counted: never risk an extra try —
            # the try after the last signs the panel out.
            if left is None or left <= expect:
                return left
        return left

    def offline(self, shot: str, go_home: list[str]) -> None:
        """Freeze Social Home (SIGSTOP) — a restart would re-seed and drop the
        panel's token — until both screens show the offline note."""
        assert self.panel and self.browser and self.household
        self.browser.navigate(self.dbase + "/")
        self.panel.run(*go_home, "wait 8")
        mark = self.panel.log_mark()
        os.kill(self.household.pid, signal.SIGSTOP)
        self.browser.offline(True)
        try:
            ok_b = ok_p = False
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline and not (ok_b and ok_p):
                ok_b = ok_b or bool(self.browser.js("document.body.classList.contains('dash-stale')"))
                ok_p = ok_p or bool(self.panel.wait_for_log(r"\[W\]\[socialhome_display", mark, 0.1))
                time.sleep(1)
            if not (ok_b and ok_p):
                self.notes.append(f"{shot}: offline not reached (browser {ok_b}, panel {ok_p})")
            self.pair_shot(shot, url=None, panel_steps=["wait 8"], retry=False)
        finally:
            os.kill(self.household.pid, signal.SIGCONT)
            self.browser.offline(False)
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            if not self.browser.js("document.body.classList.contains('dash-stale')"):
                break
            time.sleep(1)
        self.wait_board(60)

    # ---- the run ------------------------------------------------------------

    def go(self) -> None:
        started = time.time()
        self.preflight()
        program = self.build_panel()
        self.start_household()
        self.pair_panel(program)
        want_pairs = [s for s in self.a.screen_list if s not in naming.UNLOCALISED]
        if want_pairs:
            for locale in self.a.locales:
                if self.browser:
                    self.browser.close()
                # --lang: the browser's own formatting follows the household's language.
                self.browser = Browser(self.chromium, lang=locale)
                for scheme in self.a.schemes:
                    self.combination(locale, scheme)
        self.write_index(started)

    def write_index(self, started: float) -> None:
        took = time.time() - started
        meta = {
            "socialhome": self.checkout, "socialhome_commit": git_head(self.checkout),
            "firmware_commit": git_head(ROOT), "time_zone": self.info["tz"],
            "started": datetime.fromtimestamp(started, self.tz).isoformat(timespec="seconds"),
            "took_s": round(took), "locales": self.a.locales, "schemes": self.a.schemes,
            "screens": self.a.screen_list, "shots": self.shots, "notes": self.notes,
        }
        with open(os.path.join(self.out, "run.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, ensure_ascii=False, indent=1)
        lines = [
            "# Panel vs browser: offsets", "",
            f"Social Home `{meta['socialhome_commit'][:12]}`, firmware `{meta['firmware_commit'][:12]}`, "
            f"household time {meta['started']} ({meta['time_zone']}), {round(took / 60, 1)} min.",
            "Δ = panel − browser, in px, over the texts both screens show (offsets-NAME.md has each).",
            "", "| shot | panel / browser minute | texts | median abs Δy | largest abs Δx | largest abs Δy |",
            "|---|---|---|---|---|---|"]
        for s in self.summary:
            m = s.get("matched", 0)
            minute = f"{s.get('panel_minute', '')} / {s.get('browser_minute', '')}"
            if m:
                lines.append(f"| {s['name']} | {minute} | {m} | {s['median_dy']:g} | {s['max_dx']:g} | "
                             f"{s['max_dy']:g} |")
            else:
                lines.append(f"| {s['name']} | {minute} | 0 | | | |")
        if self.notes:
            lines += ["", "Notes:", *[f"- {n}" for n in self.notes]]
        with open(os.path.join(self.out, "offsets.md"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        self.log(f"done in {round(took / 60, 1)} min: {len(self.shots)} shots in {self.out}")

    def cleanup(self) -> None:
        if self.panel:
            self.panel.stop()
        if self.browser:
            self.browser.close()
        if self.household and self.household.poll() is None:
            try:
                os.kill(self.household.pid, signal.SIGCONT)
                self.household.terminate()
                self.household.wait(timeout=20)
            except (ProcessLookupError, subprocess.TimeoutExpired):
                self.household.kill()
                self.household.wait()
        if self.a.keep_work:
            self.log(f"scratch kept in {self.work}")
        else:
            shutil.rmtree(self.work, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    a = parse_args(argv)

    def stop(signum, _frame):
        raise KeyboardInterrupt(f"signal {signum}")

    signal.signal(signal.SIGTERM, stop)
    run = Run(a)
    try:
        run.go()
    except (RunError, PanelError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130
    finally:
        run.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
