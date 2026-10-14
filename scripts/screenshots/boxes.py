# SPDX-License-Identifier: MPL-2.0
"""Panel ``dump`` boxes vs browser boxes (pure: no I/O, no Pillow).

The host panel's ``dump NAME`` (builds/host/host_driver.h) writes one line
per visible LVGL object, depth first, two spaces of indent per level::

    148 16 1104 48
      164 26 91 26 "Overview"

(x y w h in screen pixels, then a label's text). It dumps the active screen
and then the top layer (sheets such as the panel settings), each from depth 0.

The browser side is a list of ``{x, y, w, h, text, ...}`` — one per element
with its own text (cdp.BOXES_JS). Boxes are lined up by their text: a label
on the panel and an element in the browser that say the same thing (case,
spacing and emoji aside — CSS upper-cases some labels, emoji are pictures on
both screens). The panel splits a text with emoji into a label per word; the
words of one parent are tried joined as well.
"""

from __future__ import annotations

import re
import statistics
import unicodedata
from dataclasses import dataclass, field

SCREEN_W, SCREEN_H = 1280, 800


@dataclass
class Node:
    depth: int
    x: int
    y: int
    w: int
    h: int
    text: str | None = None
    parent: "Node | None" = field(default=None, repr=False)
    kids: list["Node"] = field(default_factory=list, repr=False)

    @property
    def box(self) -> tuple[int, int, int, int]:
        return (self.x, self.y, self.w, self.h)

    @property
    def centre(self) -> tuple[int, int]:
        return (self.x + self.w // 2, self.y + self.h // 2)

    def walk(self):
        yield self
        for k in self.kids:
            yield from k.walk()


_LINE = re.compile(r'^( *)(-?\d+) (-?\d+) (\d+) (\d+)(?: "(.*)")?$')


def parse_dump(text: str) -> list[Node]:
    """The roots of a dump (the screen, then the top layer)."""
    roots: list[Node] = []
    stack: list[Node] = []
    for raw in text.splitlines():
        if not raw.strip():
            continue
        m = _LINE.match(raw)
        if not m:
            # A label with a newline in it: the rest belongs to the last text.
            if stack and stack[-1].text is not None:
                stack[-1].text += "\n" + raw.rstrip('"')
            continue
        depth = len(m.group(1)) // 2
        node = Node(depth, int(m.group(2)), int(m.group(3)), int(m.group(4)),
                    int(m.group(5)), m.group(6))
        while stack and stack[-1].depth >= depth:
            stack.pop()
        if stack:
            node.parent = stack[-1]
            stack[-1].kids.append(node)
        else:
            roots.append(node)
        stack.append(node)
    return roots


def on_screen(box, width: int = SCREEN_W, height: int = SCREEN_H) -> bool:
    x, y, w, h = box
    return w > 0 and h > 0 and x < width and y < height and x + w > 0 and y + h > 0


_SPACE = re.compile(r"\s+")


def normalize(text: str) -> str:
    """Comparable text: emoji / symbols / joiners dropped, case folded,
    white space collapsed."""
    kept = []
    for ch in text:
        cat = unicodedata.category(ch)
        if cat in ("So", "Sk", "Cs", "Co", "Cf", "Mn") or 0x1F000 <= ord(ch) <= 0x1FFFF:
            kept.append(" ")
        else:
            kept.append(ch)
    return _SPACE.sub(" ", "".join(kept)).strip().casefold()


def union(boxes) -> tuple[int, int, int, int]:
    x1 = min(b[0] for b in boxes)
    y1 = min(b[1] for b in boxes)
    x2 = max(b[0] + b[2] for b in boxes)
    y2 = max(b[1] + b[3] for b in boxes)
    return (x1, y1, x2 - x1, y2 - y1)


def panel_texts(roots: list[Node]) -> list[dict]:
    """Every on-screen label, plus the joined words of each parent whose
    children are a run of labels (the panel's text-with-emoji)."""
    out: list[dict] = []
    group = 0
    for root in roots:
        for n in root.walk():
            group += 1
            if n.text is not None and n.text.strip() and on_screen(n.box):
                out.append({"text": n.text, "box": n.box, "joined": False,
                            "group": -group if n.parent is None else id(n.parent)})
            labels = [k for k in n.kids if k.text is not None and on_screen(k.box)]
            if len(labels) >= 2:
                joined = "".join(k.text for k in labels)
                out.append({"text": joined, "box": union([k.box for k in labels]),
                            "joined": True, "group": id(n)})
    return out


def browser_texts(items: list[dict]) -> list[dict]:
    out = []
    for it in items:
        box = (it["x"], it["y"], it["w"], it["h"])
        if it.get("text", "").strip() and on_screen(box):
            out.append({"text": it["text"], "box": box})
    return out


def match(panel: list[dict], browser: list[dict], reach: float = 160) -> tuple[list[dict], list[dict], list[dict]]:
    """Line up panel and browser texts.

    Returns (rows, panel_only, browser_only). Each row has ``text``,
    ``browser`` and ``panel`` boxes and ``dx dy dw dh`` (panel − browser).
    A text shown more than once pairs nearest first; two boxes more than
    ``reach`` px apart (|Δx| + |Δy|) are not the same thing. A joined panel
    run stands in only where no single label says the text.
    """
    by_text: dict[str, list[int]] = {}
    for i, item in enumerate(browser):
        t = normalize(item["text"])
        if t:
            by_text.setdefault(t, []).append(i)
    pairs = []
    for j, p in enumerate(panel):
        t = normalize(p["text"])
        for i in by_text.get(t, []):
            b = browser[i]["box"]
            dist = abs(p["box"][0] - b[0]) + abs(p["box"][1] - b[1])
            if dist <= reach:
                pairs.append((p["joined"], dist, i, j))
    pairs.sort()
    used_b: set[int] = set()
    used_p: set[int] = set()
    rows: list[dict] = []
    for _joined, _dist, i, j in pairs:
        if i in used_b or j in used_p:
            continue
        used_b.add(i)
        used_p.add(j)
        rows.append(row(browser[i]["text"], browser[i]["box"], panel[j]["box"]))
    rows.sort(key=lambda r: (round(r["browser"][1]), round(r["browser"][0])))
    browser_only = [b for i, b in enumerate(browser) if i not in used_b and normalize(b["text"])]
    # The words of a run that matched as a whole aren't missing.
    runs = {panel[j].get("group") for j in used_p if panel[j]["joined"]}
    panel_only = [p for j, p in enumerate(panel) if j not in used_p and not p["joined"]
                  and normalize(p["text"]) and p.get("group") not in runs]
    return rows, panel_only, browser_only


def row(text: str, bbox, pbox) -> dict:
    return {
        "text": text, "browser": list(bbox), "panel": list(pbox),
        "dx": round(pbox[0] - bbox[0], 1), "dy": round(pbox[1] - bbox[1], 1),
        "dw": round(pbox[2] - bbox[2], 1), "dh": round(pbox[3] - bbox[3], 1),
    }


def summary(rows: list[dict]) -> dict:
    """Counts and the median / largest absolute offsets."""
    if not rows:
        return {"matched": 0}
    out: dict = {"matched": len(rows)}
    for k in ("dx", "dy", "dw", "dh"):
        vals = [abs(r[k]) for r in rows]
        out[f"median_{k}"] = round(statistics.median(vals), 1)
        out[f"max_{k}"] = round(max(vals), 1)
    return out


def _cell(text: str, limit: int = 40) -> str:
    t = _SPACE.sub(" ", text).strip().replace("|", "\\|")
    return t if len(t) <= limit else t[: limit - 1] + "…"


def _fmt(box) -> str:
    return " / ".join(f"{v:g}" for v in box)


def offsets_markdown(name: str, rows: list[dict], panel_only: list[dict],
                     browser_only: list[dict], flag: float = 4.0) -> str:
    """The per-screen report: one row per text, ``**`` on offsets > flag px."""
    s = summary(rows)
    lines = [f"# Offsets: {name}", ""]
    if rows:
        lines.append(
            f"{s['matched']} texts on both screens; median |Δx| {s['median_dx']:g} px, "
            f"|Δy| {s['median_dy']:g} px; largest |Δx| {s['max_dx']:g}, |Δy| {s['max_dy']:g}, "
            f"|Δw| {s['max_dw']:g}, |Δh| {s['max_dh']:g} px. Δ = panel − browser.")
    else:
        lines.append("No text found on both screens.")
    lines += ["", "| text | browser x / y / w / h | panel x / y / w / h | Δx | Δy | Δw | Δh |",
              "|---|---|---|---|---|---|---|"]
    for r in rows:
        d = [f"**{r[k]:+g}**" if abs(r[k]) > flag else f"{r[k]:+g}" for k in ("dx", "dy", "dw", "dh")]
        lines.append(f"| {_cell(r['text'])} | {_fmt(r['browser'])} | {_fmt(r['panel'])} | "
                     + " | ".join(d) + " |")
    if browser_only:
        lines += ["", "Only in the browser: " + "; ".join(
            f"{_cell(b['text'], 30)} ({_fmt(b['box'])})" for b in browser_only)]
    if panel_only:
        lines += ["", "Only on the panel: " + "; ".join(
            f"{_cell(p['text'], 30)} ({_fmt(p['box'])})" for p in panel_only)]
    return "\n".join(lines) + "\n"


# ---- finding things to tap on the panel ---------------------------------


def find_label(roots: list[Node], text: str, *, exact: bool = True) -> Node | None:
    want = normalize(text)
    for root in roots:
        for n in root.walk():
            if n.text is None or not on_screen(n.box):
                continue
            got = normalize(n.text)
            if (got == want) if exact else (want in got):
                return n
    return None


def find_all(roots: list[Node], pred) -> list[Node]:
    return [n for root in roots for n in root.walk() if pred(n)]


_CLOCK = re.compile(r"^\d{1,2}[:.]\d{2}$")


def biggest_clock(roots: list[Node]) -> Node | None:
    """The tallest on-screen HH:MM label (the Overview / lock clock)."""
    clocks = find_all(roots, lambda n: n.text is not None and bool(_CLOCK.match(n.text.strip()))
                      and on_screen(n.box))
    return max(clocks, key=lambda n: n.h, default=None)


def tab_bar(roots: list[Node]) -> list[Node]:
    """The tabs, left to right: the top-most row of ≥ 3 equally tall
    siblings, each holding a label, right of the rail."""
    best: list[Node] = []
    for root in roots[:1]:
        for n in root.walk():
            kids = [k for k in n.kids if k.x >= 100 and k.y < 120]
            if len(kids) >= 3 and len({k.h for k in kids}) == 1 and len({k.y for k in kids}) == 1 \
                    and all(any(c.text for c in k.kids) for k in kids):
                if not best or kids[0].y < best[0].y:
                    best = kids
    return sorted(best, key=lambda k: k.x)


def rail_item(roots: list[Node], name: str) -> Node | None:
    """The rail entry with the label ``name`` (the label's parent)."""
    for n in find_all(roots, lambda n: n.text is not None and on_screen(n.box) and n.x < 140):
        if normalize(n.text) == normalize(name) and n.parent is not None:
            return n.parent
    return None


def rail_home(roots: list[Node], person: str) -> Node | None:
    """The rail's first entry, the household (its label is translated, the
    people's names aren't: found through ``person``)."""
    item = rail_item(roots, person)
    col = item.parent if item is not None else None
    return col.kids[0] if col is not None and col.kids else None


def rail_lock(roots: list[Node], person: str) -> Node | None:
    """The lock button: the rail's last square child."""
    item = rail_item(roots, person)
    col = item.parent if item is not None else None
    if col is None:
        return None
    squares = [k for k in col.kids if k.w == k.h and k.w > 0]
    return squares[-1] if squares else None


def keypad(roots: list[Node]) -> dict[str, Node] | None:
    """The PIN pad keys by digit, plus ``ok`` (the pad's last key)."""
    zero = find_label(roots, "0")
    if zero is None or zero.parent is None or zero.parent.parent is None:
        return None
    pad = zero.parent.parent
    keys: dict[str, Node] = {}
    for k in pad.kids:
        for c in k.kids:
            if c.text is not None and c.text.strip().isdigit():
                keys[c.text.strip()] = k
    if pad.kids:
        keys["ok"] = pad.kids[-1]
    return keys if len(keys) == 11 else None


def tries_left(roots: list[Node]) -> int | None:
    """The tries left the wrong-PIN note says ("Tries left: 2"; the last-try
    sentence has no number: 1), whatever the language: the note is the box
    of labels above the pad, in the pad's column. None without a note."""
    zero = find_label(roots, "0")
    if zero is None or zero.parent is None or zero.parent.parent is None:
        return None
    column = zero.parent.parent.parent
    if column is None:
        return None
    for box in column.kids:
        labels = [k for k in box.kids if k.text is not None]
        if len(labels) >= 2 and len(labels) == len(box.kids):
            for lab in labels:
                nums = re.findall(r"\d+", lab.text)
                if len(nums) == 1:
                    return int(nums[0])
            # "Last try — one more wrong PIN signs this screen out."
            return 1
    return None
