# SPDX-License-Identifier: MPL-2.0
"""Comparing two runs (pure: no I/O, no Pillow) — used by diff.py.

Pixels: diff.py has Pillow reduce a pair of shots to a histogram of the
per-pixel largest channel difference (0–255); a pixel counts as changed when
that difference is above a small threshold (anti-aliasing noise stays out).

Boxes: the texts of two ``boxes-*.json`` files are lined up the way
boxes.match lines up panel and browser — by normalized text, repeats in
reading order — and a box that moved or changed size is reported.
"""

from __future__ import annotations

from boxes import normalize


def changed_from_histogram(hist: list[int], threshold: int = 16) -> int:
    """Pixels whose largest channel difference is above ``threshold``."""
    if not 0 <= threshold < len(hist):
        raise ValueError("threshold out of range")
    return sum(hist[threshold + 1:])


def percent(part: int, total: int) -> float:
    return round(100.0 * part / total, 3) if total else 0.0


def box_changes(old: list[dict], new: list[dict], tolerance: float = 1.0) -> dict:
    """Texts that moved / resized more than ``tolerance`` px between runs,
    and texts only one run has.

    ``old`` / ``new`` are lists of ``{"text", "box": [x, y, w, h]}``.
    """
    def group(items):
        out: dict[str, list[dict]] = {}
        for it in sorted(items, key=lambda i: (round(i["box"][1]), round(i["box"][0]))):
            t = normalize(it["text"])
            if t:
                out.setdefault(t, []).append(it)
        return out

    a, b = group(old), group(new)
    moved, gone, added = [], [], []
    for t, items in a.items():
        others = b.get(t, [])
        for k, it in enumerate(items):
            if k >= len(others):
                gone.append(it)
                continue
            ob, nb = it["box"], others[k]["box"]
            d = [round(nb[i] - ob[i], 1) for i in range(4)]
            if any(abs(v) > tolerance for v in d):
                moved.append({"text": it["text"], "old": list(ob), "new": list(nb),
                              "dx": d[0], "dy": d[1], "dw": d[2], "dh": d[3]})
    for t, items in b.items():
        added.extend(items[len(a.get(t, [])):])
    return {"moved": moved, "gone": gone, "added": added}


def offset_changes(old_rows: list[dict], new_rows: list[dict], tolerance: float = 1.0) -> list[dict]:
    """Panel-vs-browser offsets that got better or worse between runs.

    Rows are boxes.match rows; returns ``{"text", "old_dy", "new_dy", ...}``
    for every text on both whose Δx / Δy / Δw / Δh changed > tolerance.
    """
    def index(rows):
        out: dict[str, list[dict]] = {}
        for r in rows:
            out.setdefault(normalize(r["text"]), []).append(r)
        return out

    a, b = index(old_rows), index(new_rows)
    out = []
    for t, rows in a.items():
        for k, r in enumerate(rows):
            if k >= len(b.get(t, [])):
                continue
            n = b[t][k]
            if any(abs(n[f] - r[f]) > tolerance for f in ("dx", "dy", "dw", "dh")):
                out.append({"text": r["text"],
                            **{f"old_{f}": r[f] for f in ("dx", "dy", "dw", "dh")},
                            **{f"new_{f}": n[f] for f in ("dx", "dy", "dw", "dh")}})
    return out
