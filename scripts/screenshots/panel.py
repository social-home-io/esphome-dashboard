# SPDX-License-Identifier: MPL-2.0
"""The host SDL panel (builds/host-sdl.yaml) as a process a run drives.

Each panel gets its own prefs dir (its server, token and cached theme), its
own snapshot dir and so its own driver command file
(``$ESPHOME_PREFDIR/socialhome-host.cmd``, builds/host/driver_path.h): two
panels on one machine never run each other's taps.
"""

from __future__ import annotations

import os
import re
import signal
import subprocess
import time

import boxes
import images

CMD_FILE = "socialhome-host.cmd"


class PanelError(RuntimeError):
    pass


class Panel:
    def __init__(self, binary: str, workdir: str, log_path: str) -> None:
        self.binary = binary
        self.prefdir = os.path.join(workdir, "prefs")
        self.snapdir = os.path.join(workdir, "snaps")
        self.log_path = log_path
        self.cmd = os.path.join(self.prefdir, CMD_FILE)
        self.proc: subprocess.Popen | None = None
        self._log = None

    # ---- process -------------------------------------------------------

    def start(self, fresh: bool = False) -> None:
        self.stop()
        os.makedirs(self.prefdir, exist_ok=True)
        os.makedirs(self.snapdir, exist_ok=True)
        if fresh:
            for f in os.listdir(self.prefdir):
                os.remove(os.path.join(self.prefdir, f))
        for f in (self.cmd, self.cmd + ".done", self.cmd + ".tmp"):
            if os.path.exists(f):
                os.remove(f)
        env = dict(os.environ, ESPHOME_PREFDIR=self.prefdir, ESPHOME_SNAPSHOT_DIR=self.snapdir)
        self._log = open(self.log_path, "ab")
        self._log_start = os.path.getsize(self.log_path)
        self.proc = subprocess.Popen([self.binary], env=env, cwd=self.prefdir,
                                     stdin=subprocess.DEVNULL, stdout=self._log,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            text = self.log_since_start()
            if "is already read by another host panel" in text:
                raise PanelError(f"{self.cmd} is held by another host panel")
            if "Reading commands from" in text:
                return
            if self.proc.poll() is not None:
                raise PanelError(f"panel exited ({self.proc.returncode}); see {self.log_path}")
            time.sleep(0.2)
        raise PanelError("panel did not start its driver; see " + self.log_path)

    def stop(self) -> None:
        if self.proc is not None and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.proc = None
        if self._log is not None:
            self._log.close()
            self._log = None

    def log_since_start(self) -> str:
        with open(self.log_path, "rb") as f:
            f.seek(self._log_start)
            return f.read().decode("utf-8", "replace")

    def log_mark(self) -> int:
        return os.path.getsize(self.log_path)

    def log_since(self, mark: int) -> str:
        with open(self.log_path, "rb") as f:
            f.seek(mark)
            return f.read().decode("utf-8", "replace")

    # ---- driver ----------------------------------------------------------

    def run(self, *steps: str, timeout: float = 120) -> None:
        """Run driver steps (``tap X Y``, ``wait N`` — N ticks of 250 ms,
        ``shot NAME``, ``dump NAME``, ``hold X Y``, ``type TEXT``) and wait
        for the panel to finish them."""
        if self.proc is None or self.proc.poll() is not None:
            raise PanelError("panel is not running; see " + self.log_path)
        done = self.cmd + ".done"
        if os.path.exists(done):
            os.remove(done)
        with open(self.cmd + ".tmp", "w") as f:
            f.write("\n".join(steps) + "\n")
        os.replace(self.cmd + ".tmp", self.cmd)
        deadline = time.monotonic() + timeout
        while not os.path.exists(done):
            if time.monotonic() > deadline:
                raise PanelError(f"panel did not finish {steps!r} in {timeout} s")
            if self.proc.poll() is not None:
                raise PanelError("panel exited; see " + self.log_path)
            time.sleep(0.1)
        time.sleep(0.2)  # the BMP is flushed right after the step

    def tap(self, node: boxes.Node | tuple[int, int]) -> str:
        x, y = node.centre if isinstance(node, boxes.Node) else node
        return f"tap {x} {y}"

    def dump(self) -> list[boxes.Node]:
        """The current screen's object tree."""
        self.run("dump probe")
        path = os.path.join(self.snapdir, "probe.txt")
        with open(path, encoding="utf-8") as f:
            text = f.read()
        os.remove(path)
        return boxes.parse_dump(text)

    def capture(self, name: str, png: str, dump_txt: str | None = None,
                before: tuple[str, ...] = ()) -> float:
        """Run ``before`` then shoot (and dump) ``name``: the PNG lands at
        ``png``, the dump at ``dump_txt``. Returns the time of the shot."""
        steps = list(before) + [f"shot {name}"]
        if dump_txt:
            steps.append(f"dump {name}")
        self.run(*steps)
        when = time.time()
        bmp = os.path.join(self.snapdir, name + ".bmp")
        if not os.path.exists(bmp):
            raise PanelError(f"no snapshot {bmp}; see {self.log_path}")
        images.bmp_to_png(bmp, png)
        os.remove(bmp)
        if dump_txt:
            os.replace(os.path.join(self.snapdir, name + ".txt"), dump_txt)
        return when

    def wait_for_log(self, pattern: str, mark: int, timeout: float) -> re.Match | None:
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            matches = list(rx.finditer(self.log_since(mark)))
            if matches:
                return matches[-1]
            time.sleep(0.3)
        return None
