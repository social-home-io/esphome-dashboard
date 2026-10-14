# SPDX-License-Identifier: MPL-2.0
"""What check.py asserts about a screenshot run (pure: no I/O, no Pillow).

A run of run.py passes when:

  (a) every expected shot is there — the panel's, and the browser's for the
      screens both have — and every step reached its state (run.json notes);
  (b) every text both screens show sits within ``tolerance`` px on the panel
      of where the browser has it (Δx Δy Δw Δh), unless accepted.json
      accepts that difference with a reason;
  (c) no panel text runs off the screen or is cut sideways by its
      container, and no two panel texts are drawn over each other;
  (d) both screens say the same words (clock times and numbers aside, which
      tick between the two shots), in the same weight class — light (≤ 350),
      regular (≤ 550) or bold — where the panel's dump names its font weight;
  (e) the panel log has no ``[E]`` line and no crash, the household log no
      Python traceback.

accepted.json lists the known, accepted differences — every entry with a
``reason``. Entries scope themselves with ``screens`` (screen names such as
``reader``, or ``*``) and optionally ``locales`` / ``schemes``:

  offsets     ``{"text": regex, "max": {"dx": 12, "dy": 8, "dw": 30, "dh": 4}}``
              — rows whose normalized text matches (``re.search``; omit for
              every text) may be off by up to ``max`` px per field (fields
              left out keep the tolerance);
  texts       ``{"side": "panel" | "browser", "words": [...]}`` — words only
              that side may say;
  layout      ``{"text": regex}`` — panel texts allowed off-screen, cut or
              overlapping;
  weights     ``{"text": regex}`` — texts (normalized) allowed in another
              weight class than the browser's;
  log         ``{"match": regex}`` — panel / household log lines allowed.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

import boxes
import naming

DEFAULT_TOLERANCE = 4.0
FIELDS = ("dx", "dy", "dw", "dh")


# ---- accepted.json ----------------------------------------------------------


class AcceptedError(ValueError):
    pass


@dataclass
class Rule:
    kind: str
    index: int
    reason: str
    screens: tuple[str, ...]
    locales: tuple[str, ...] = ()
    schemes: tuple[str, ...] = ()
    text: re.Pattern | None = None
    max: dict[str, float] = field(default_factory=dict)
    side: str = ""
    words: frozenset[str] = frozenset()
    used: bool = False

    def scoped(self, shot: dict[str, str]) -> bool:
        return (("*" in self.screens or shot["screen"] in self.screens)
                and (not self.locales or shot["locale"] in self.locales)
                and (not self.schemes or shot["scheme"] in self.schemes))

    def label(self) -> str:
        return f"{self.kind}[{self.index}]"


@dataclass
class Accepted:
    tolerance: float
    offsets: list[Rule]
    texts: list[Rule]
    layout: list[Rule]
    log: list[Rule]
    weights: list[Rule] = field(default_factory=list)

    def all(self) -> list[Rule]:
        return self.offsets + self.texts + self.layout + self.weights + self.log


def load_accepted(data: dict) -> Accepted:
    """Validate accepted.json (a dict) into rules; AcceptedError on a bad
    entry — every entry needs a non-empty ``reason``."""
    if not isinstance(data, dict):
        raise AcceptedError("accepted.json: not an object")
    tol = float(data.get("tolerance_px", DEFAULT_TOLERANCE))
    out: dict[str, list[Rule]] = {"offsets": [], "texts": [], "layout": [], "weights": [], "log": []}
    for kind in out:
        for i, e in enumerate(data.get(kind, [])):
            where = f"accepted.json {kind}[{i}]"
            if not isinstance(e, dict):
                raise AcceptedError(f"{where}: not an object")
            reason = str(e.get("reason", "")).strip()
            if not reason:
                raise AcceptedError(f"{where}: needs a reason")
            screens = tuple(e.get("screens", ("*",)) if kind != "log" else ("*",))
            for s in screens:
                if s != "*" and s not in naming.SCREENS:
                    raise AcceptedError(f"{where}: unknown screen {s!r}")
            rule = Rule(kind, i, reason, screens, tuple(e.get("locales", ())),
                        tuple(e.get("schemes", ())))
            pattern = e.get("match") if kind == "log" else e.get("text")
            if pattern is not None:
                try:
                    rule.text = re.compile(pattern)
                except re.error as err:
                    raise AcceptedError(f"{where}: bad regex: {err}") from None
            elif kind in ("log", "layout", "weights"):
                raise AcceptedError(f"{where}: needs {'match' if kind == 'log' else 'text'}")
            if kind == "offsets":
                mx = e.get("max", {})
                if not mx or set(mx) - set(FIELDS):
                    raise AcceptedError(f"{where}: max needs some of {', '.join(FIELDS)}")
                rule.max = {k: float(v) for k, v in mx.items()}
            if kind == "texts":
                rule.side = e.get("side", "")
                if rule.side not in ("panel", "browser"):
                    raise AcceptedError(f"{where}: side is panel or browser")
                rule.words = frozenset(w.casefold() for w in e.get("words", ()))
                if not rule.words:
                    raise AcceptedError(f"{where}: needs words")
            out[kind].append(rule)
    return Accepted(tol, out["offsets"], out["texts"], out["layout"], out["log"], out["weights"])


def shot_parts(name: str) -> dict[str, str]:
    """``overview-de-dark`` → screen / locale / scheme."""
    parts = naming.parse_file(naming.file_name("panel", name))
    if parts is None:
        raise ValueError(f"not a shot name: {name!r}")
    return parts


# ---- (a) expected shots -------------------------------------------------------


def expected_files(meta: dict) -> list[str]:
    """The files a finished run must have, from its run.json."""
    out = []
    for screen in meta.get("screens", []):
        if screen in naming.UNLOCALISED:
            combos = [("", "")]
        else:
            combos = [(lo, sc) for lo in meta.get("locales", []) for sc in meta.get("schemes", [])]
        for lo, sc in combos:
            name = naming.shot_name(screen, lo, sc) if lo else screen
            kinds = ("panel", "boxes") if screen in naming.PANEL_ONLY else (
                "panel", "browser", "compare", "boxes", "offsets")
            out += [naming.file_name(k, name) for k in kinds]
    return out


# Notes run.py writes when a step didn't reach its state (a minute that
# turned between the two shots is expected now and then: not a failure).
_STATE_NOTE = re.compile(r"not reached|tries left|browser font: no Roboto")


def note_failures(notes: list[str]) -> list[str]:
    return [n for n in notes if _STATE_NOTE.search(n)]


# ---- (b) offsets ------------------------------------------------------------


def far_rows(panel_only: list[dict], browser_only: list[dict],
             panel_all: list[dict] | None = None, browser_all: list[dict] | None = None) -> list[dict]:
    """Texts both screens show but further apart than boxes.match reaches:
    paired anyway, so a block that moved a long way still counts. Only texts
    each screen shows once (of ``*_all``) and without digits: "3 min ago" by
    the alert and "3 min ago" in the map's list are two things, not one that
    moved."""
    def once(items: list[dict], every: list[dict] | None) -> list[dict]:
        counts: dict[str, int] = {}
        for it in every if every is not None else items:
            if not it.get("joined"):
                t = boxes.normalize(it["text"])
                counts[t] = counts.get(t, 0) + 1
        return [it for it in items if counts.get(boxes.normalize(it["text"])) == 1
                and not any(ch.isdigit() for ch in it["text"])]

    rows, _, _ = boxes.match(once(panel_only, panel_all), once(browser_only, browser_all), reach=10 ** 6)
    for r in rows:
        r["far"] = True
    return rows


def offset_failures(shot: dict[str, str], rows: list[dict], acc: Accepted) -> list[dict]:
    """Rows off by more than the tolerance that no rule accepts."""
    bad = []
    for r in rows:
        over = [k for k in FIELDS if abs(r[k]) > acc.tolerance]
        if not over:
            continue
        text = boxes.normalize(r["text"])
        ok = False
        for rule in acc.offsets:
            if not rule.scoped(shot) or (rule.text is not None and not rule.text.search(text)):
                continue
            if all(abs(r[k]) <= rule.max.get(k, acc.tolerance) for k in FIELDS):
                rule.used = ok = True
                break
        if not ok:
            bad.append({**r, "over": over})
    return bad


# ---- (c) panel layout ---------------------------------------------------------


def _intersect(a, b) -> tuple[int, int, int, int]:
    x1, y1 = max(a[0], b[0]), max(a[1], b[1])
    x2, y2 = min(a[0] + a[2], b[0] + b[2]), min(a[1] + a[3], b[1] + b[3])
    return (x1, y1, max(0, x2 - x1), max(0, y2 - y1))


def _ink(box) -> tuple[int, int, int, int]:
    """A label's line box less the line spacing above and below the glyphs
    (a fifth of its height each): big clocks' line boxes touch their
    neighbours' without any glyph doing so."""
    inset = round(box[3] * 0.2)
    return (box[0], box[1] + inset, box[2], box[3] - 2 * inset)


def visible_labels(root: boxes.Node, width: int = boxes.SCREEN_W,
                   height: int = boxes.SCREEN_H) -> list[tuple[boxes.Node, tuple]]:
    """(label, the part its containers let show) for every label with text
    of which something shows."""
    out = []

    def walk(n: boxes.Node, clip) -> None:
        c = _intersect(clip, n.box)
        if n.text is not None and n.text.strip() and c[2] > 0 and c[3] > 0:
            out.append((n, c))
        for k in n.kids:
            walk(k, c)

    walk(root, (0, 0, width, height))
    return out


def layout_problems(roots: list[boxes.Node], width: int = boxes.SCREEN_W,
                    height: int = boxes.SCREEN_H) -> list[str]:
    """Panel texts off the screen, cut sideways, or drawn over each other.

    A text cut at the bottom by a scrolling list is how lists look; cut at
    the side it's truncated. Overlaps count within one layer (the screen, or
    the top layer's sheet — a sheet over the board is meant to cover it)."""
    out = []
    for root in roots:
        labels = visible_labels(root, width, height)
        for n, c in labels:
            if n.x < 0 or n.y < 0 or n.x + n.w > width:
                out.append(f"off-screen: {n.text!r} at {list(n.box)}")
            elif c[2] < n.w - 1:
                out.append(f"cut sideways: {n.text!r} at {list(n.box)}, {c[2]} of {n.w} px show")
        for i, (a, ca) in enumerate(labels):
            for b, cb in labels[i + 1:]:
                o = _intersect(_ink(ca), _ink(cb))
                if o[2] > 2 and o[3] > 2:
                    out.append(f"overlap: {a.text!r} at {list(ca)} and {b.text!r} at {list(cb)}")
    return out


def layout_failures(shot: dict[str, str], problems: list[str], acc: Accepted) -> list[str]:
    bad = []
    for p in problems:
        ok = False
        for rule in acc.layout:
            if rule.scoped(shot) and rule.text is not None and rule.text.search(p):
                rule.used = ok = True
                break
        if not ok:
            bad.append(p)
    return bad


# ---- (d) words ----------------------------------------------------------------

_TIME = re.compile(r"^\d{1,2}([:.]\d{2})?([–-]\d{1,2}([:.]\d{2})?)?$|^\d+$")
_EDGE = ".,:;!?…·()[]{}\"'«»„“”‘’›‹-–—/|*"
_ELLIPSIS = re.compile(r"(\.\.\.|…)\s*$")


def words(text: str) -> list[str]:
    """The words of a text, normalized; times and numbers dropped (they
    tick between the panel's shot and the browser's)."""
    out = []
    for w in boxes.normalize(text).split():
        w = w.strip(_EDGE)
        if w and not _TIME.match(w) and any(ch.isalnum() for ch in w):
            out.append(w)
    return out


def _cut_prefix(text: str) -> str | None:
    """A text cut with an ellipsis: what's left before its last, partial word."""
    if not _ELLIPSIS.search(text):
        return None
    head = _ELLIPSIS.sub("", text).rstrip()
    return " ".join(boxes.normalize(head).split()[:-1])


def word_differences(panel: list[str], browser: list[str]) -> tuple[set[str], set[str]]:
    """Words only the panel says, words only the browser says.

    Texts cut with an ellipsis on one side are the same message as the
    other side's text that starts the same: neither's words count."""
    def bag(texts: list[str], others: list[str]) -> set[str]:
        prefixes = [p for p in map(_cut_prefix, others) if p]
        out: set[str] = set()
        for t in texts:
            norm = " ".join(boxes.normalize(t).split())
            if _cut_prefix(t) is not None:
                continue
            if any(norm.startswith(p) for p in prefixes):
                continue
            out.update(words(t))
        return out

    p_all = {w for t in panel for w in words(t)}
    b_all = {w for t in browser for w in words(t)}
    return bag(panel, browser) - b_all, bag(browser, panel) - p_all


def word_failures(shot: dict[str, str], only_panel: set[str], only_browser: set[str],
                  acc: Accepted) -> tuple[list[str], list[str]]:
    def left(ws: set[str], side: str) -> list[str]:
        out = []
        for w in sorted(ws):
            rules = [r for r in acc.texts if r.side == side and r.scoped(shot) and w in r.words]
            for r in rules:
                r.used = True
            if not rules:
                out.append(w)
        return out

    return left(only_panel, "panel"), left(only_browser, "browser")


def weight_class(weight: int) -> str:
    """Light, regular or bold: the steps a reader tells apart. The browser's
    500 / 600 draw as the panel's 400 / 700 (it ships no medium / semibold
    Roboto), so only a jump between classes is a difference."""
    return "light" if weight <= 350 else "regular" if weight <= 550 else "bold"


def weight_failures(shot: dict[str, str], rows: list[dict], acc: Accepted) -> list[dict]:
    """Matched texts (rows with ``weight`` = [browser, panel]) drawn in
    another weight class than the browser's, unless a rule accepts them."""
    bad = []
    for r in rows:
        if "weight" not in r:
            continue
        b, p = r["weight"]
        if weight_class(b) == weight_class(p):
            continue
        text = boxes.normalize(r["text"])
        rules = [rule for rule in acc.weights if rule.scoped(shot) and rule.text.search(text)]
        for rule in rules:
            rule.used = True
        if not rules:
            bad.append({"text": r["text"], "browser": b, "panel": p})
    return bad


# ---- (e) logs -----------------------------------------------------------------

_PANEL_BAD = re.compile(r"\[E\]|Segmentation fault|Aborted|AddressSanitizer|terminate called|core dumped")
_HOUSEHOLD_BAD = re.compile(r"^Traceback \(most recent call last\)|\bCRITICAL\b")
_ANSI = re.compile(r"\x1b\[[0-9;]*m")


def log_failures(panel_log: str, household_log: str, acc: Accepted) -> list[str]:
    bad = []
    for source, text, rx in (("panel", panel_log, _PANEL_BAD), ("household", household_log, _HOUSEHOLD_BAD)):
        for line in text.splitlines():
            line = _ANSI.sub("", line).rstrip()
            if not rx.search(line):
                continue
            ok = False
            for rule in acc.log:
                if rule.text is not None and rule.text.search(line):
                    rule.used = ok = True
                    break
            if not ok:
                bad.append(f"{source}.log: {line[:300]}")
    return bad
