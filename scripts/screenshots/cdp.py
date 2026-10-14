# SPDX-License-Identifier: MPL-2.0
"""Headless Chromium over the DevTools protocol, standard library only.

Talks to Chromium through ``--remote-debugging-pipe`` (JSON messages split by
NUL bytes on file descriptors 3 and 4), so there is no debugging port to
collide with another run and nothing to install.
"""

from __future__ import annotations

import base64
import json
import os
import queue
import shutil
import subprocess
import tempfile
import threading
import time
from typing import Any

# Element boxes for the offsets report: every element with its own text
# (a Range around its direct text nodes — the text's line box, closest to
# an LVGL label's), visible on screen.
BOXES_JS = r"""(() => {
  const out = [];
  const seen = new Set();
  for (const el of document.querySelectorAll('body *')) {
    const cs = getComputedStyle(el);
    if (cs.visibility === 'hidden' || cs.display === 'none') continue;
    // Screen-reader-only text (clipped to 1 px) isn't on the screen.
    if (el.closest('.dash-visually-hidden, .visually-hidden, .sr-only')) continue;
    const nodes = [...el.childNodes].filter(n => n.nodeType === 3 && n.data.trim());
    if (!nodes.length) continue;
    const range = document.createRange();
    range.setStartBefore(nodes[0]);
    range.setEndAfter(nodes[nodes.length - 1]);
    const r = range.getBoundingClientRect();
    if (r.width < 1 || r.height < 1 || r.bottom < 0 || r.top > innerHeight) continue;
    const text = nodes.map(n => n.data).join(' ').replace(/\s+/g, ' ').trim();
    const key = [Math.round(r.left), Math.round(r.top), text].join('|');
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({x: Math.round(r.left * 10) / 10, y: Math.round(r.top * 10) / 10,
              w: Math.round(r.width * 10) / 10, h: Math.round(r.height * 10) / 10,
              text, cls: String(el.className || ''), tag: el.tagName.toLowerCase(),
              font: cs.fontSize + ' ' + cs.fontWeight});
  }
  return JSON.stringify(out);
})()"""

# Fonts loaded, every <img> decoded (or failed) — at most `ms` long.
SETTLE_JS = r"""(async (ms) => {
  const imgs = [...document.images].filter(i => !i.complete);
  const loads = imgs.map(i => new Promise(r => { i.addEventListener('load', r, {once: true});
                                                 i.addEventListener('error', r, {once: true}); }));
  await Promise.race([Promise.all([document.fonts.ready, ...loads]),
                      new Promise(r => setTimeout(r, ms))]);
  return imgs.length;
})(%d)"""


class CdpError(RuntimeError):
    pass


class Browser:
    """One headless Chromium with one page, sized like the wall tablet."""

    def __init__(self, chromium: str, width: int = 1280, height: int = 800,
                 lang: str = "en-US") -> None:
        self.width, self.height = width, height
        self.profile = tempfile.mkdtemp(prefix="sh-shots-chrome-")
        to_child_r, self._to_child = os.pipe()
        self._from_child, from_child_w = os.pipe()

        def wire() -> None:  # in the child, before exec: fds 3 and 4
            os.dup2(to_child_r, 3)
            os.dup2(from_child_w, 4)

        self.proc = subprocess.Popen(
            [chromium, "--headless=new", "--no-sandbox", "--disable-gpu",
             "--disable-dev-shm-usage", "--remote-debugging-pipe",
             f"--user-data-dir={self.profile}", f"--lang={lang}", "--hide-scrollbars",
             "--font-render-hinting=none", "--no-first-run", "--mute-audio",
             "about:blank"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            pass_fds=(3, 4), preexec_fn=wire)
        os.close(to_child_r)
        os.close(from_child_w)
        self._next = 0
        self._replies: dict[int, queue.Queue] = {}
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()
        self.session = ""
        target = self.call("Target.createTarget", url="about:blank", session=False)["targetId"]
        self.session = self.call("Target.attachToTarget", targetId=target, flatten=True,
                                 session=False)["sessionId"]
        self.call("Page.enable")
        self.call("Network.enable")
        self.resize(width, height)

    def _read(self) -> None:
        buf = b""
        while True:
            try:
                chunk = os.read(self._from_child, 1 << 16)
            except OSError:
                chunk = b""
            if not chunk:
                for q in list(self._replies.values()):
                    q.put({"error": {"message": "browser closed"}})
                return
            buf += chunk
            while b"\0" in buf:
                raw, buf = buf.split(b"\0", 1)
                msg = json.loads(raw)
                q = self._replies.get(msg.get("id", -1))
                if q is not None:
                    q.put(msg)

    def call(self, method: str, timeout: float = 60, session: bool = True,
             **params: Any) -> dict:
        with self._lock:
            self._next += 1
            mid = self._next
            q: queue.Queue = queue.Queue()
            self._replies[mid] = q
            msg: dict[str, Any] = {"id": mid, "method": method, "params": params}
            if session and self.session:
                msg["sessionId"] = self.session
            os.write(self._to_child, json.dumps(msg).encode() + b"\0")
        try:
            reply = q.get(timeout=timeout)
        except queue.Empty:
            raise CdpError(f"{method}: no answer in {timeout} s") from None
        finally:
            self._replies.pop(mid, None)
        if "error" in reply:
            raise CdpError(f"{method}: {reply['error'].get('message')}")
        return reply.get("result", {})

    # ---- page helpers --------------------------------------------------

    def resize(self, width: int, height: int) -> None:
        self.call("Emulation.setDeviceMetricsOverride", width=width, height=height,
                  deviceScaleFactor=1, mobile=False)

    def scheme(self, value: str) -> None:
        self.call("Emulation.setEmulatedMedia",
                  features=[{"name": "prefers-color-scheme", "value": value}])

    def cookie(self, url: str, name: str, value: str) -> None:
        self.call("Network.setCookie", name=name, value=value, url=url, httpOnly=True,
                  sameSite="Strict")

    def offline(self, on: bool) -> None:
        self.call("Network.emulateNetworkConditions", offline=on, latency=0,
                  downloadThroughput=-1, uploadThroughput=-1)

    def js(self, expression: str, wait: float = 0.0) -> Any:
        r = self.call("Runtime.evaluate", expression=expression, awaitPromise=True,
                      returnByValue=True)
        if "exceptionDetails" in r:
            raise CdpError(f"JS failed: {r['exceptionDetails'].get('text')} in {expression[:80]}")
        if wait:
            time.sleep(wait)
        return r.get("result", {}).get("value")

    def navigate(self, url: str, settle: float = 2.0, img_ms: int = 8000) -> None:
        self.call("Page.navigate", url=url)
        time.sleep(settle)
        self.settle(img_ms)

    def settle(self, img_ms: int = 8000) -> None:
        try:
            self.js(SETTLE_JS % img_ms)
        except CdpError:
            pass  # navigating away mid-check: the screenshot shows what is there

    def screenshot(self, path: str) -> None:
        shot = self.call("Page.captureScreenshot", format="png")
        with open(path, "wb") as f:
            f.write(base64.b64decode(shot["data"]))

    def boxes(self) -> list[dict]:
        return json.loads(self.js(BOXES_JS))

    def close(self) -> None:
        try:
            self.call("Browser.close", session=False, timeout=5)
        except CdpError:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        for fd in (self._to_child, self._from_child):
            try:
                os.close(fd)
            except OSError:
                pass
        shutil.rmtree(self.profile, ignore_errors=True)
