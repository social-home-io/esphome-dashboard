#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Compare two screenshot runs of run.py: what changed between them.

    .venv/bin/python scripts/screenshots/diff.py OLD_DIR NEW_DIR [--out DIR]

For every shot both runs have (same file name):
  - the share of pixels that changed, panel and browser (a pixel counts when
    a colour channel moved by more than --threshold, 16 by default);
  - texts whose box moved or resized (panel dump, browser boxes), texts only
    one run has, and panel − browser offsets that changed;
  - diff-<kind>-<name>.png: before | after | the changed pixels in magenta.

Writes diff.md (and prints it) into --out (default NEW_DIR/diff). Expect the
clock minute, "N min ago" and the panel id (settings) to differ between two
runs of the same code; anything else is a change.
"""

from __future__ import annotations

import argparse
import json
import os
import sys

import boxes
import metrics
import naming
from images import diff_image, histogram


def shot_names(path: str) -> dict[str, dict[str, str]]:
    """name → {kind: file} for the run in ``path``."""
    out: dict[str, dict[str, str]] = {}
    for f in sorted(os.listdir(path)):
        parts = naming.parse_file(f)
        if parts:
            out.setdefault(parts["name"], {})[parts["kind"]] = os.path.join(path, f)
    return out


def order(name: str) -> tuple[int, str]:
    """Shots in the order a run takes them."""
    screen = max((s for s in naming.SCREENS if name == s or name.startswith(s + "-")), key=len)
    return (naming.SCREENS.index(screen), name)


def texts(data: dict) -> tuple[list[dict], list[dict]]:
    panel = [p for p in boxes.panel_texts(boxes.parse_dump(data.get("panel_dump", "")))
             if not p["joined"]]
    return panel, boxes.browser_texts(data.get("browser", []))


def compare(old_dir: str, new_dir: str, out: str, threshold: int = 16) -> str:
    os.makedirs(out, exist_ok=True)
    old, new = shot_names(old_dir), shot_names(new_dir)
    lines = ["# Screenshot diff", "", f"Before: `{old_dir}`  ", f"After: `{new_dir}`", "",
             "| shot | panel changed | browser changed | panel texts moved | browser texts moved "
             "| offsets changed |", "|---|---|---|---|---|---|"]
    details: list[str] = []
    for name in sorted(set(old) & set(new), key=order):
        cells = []
        for kind in ("panel", "browser"):
            a, b = old[name].get(kind), new[name].get(kind)
            if not (a and b):
                cells.append("–")
                continue
            hist, sa, sb = histogram(a, b)
            changed = metrics.changed_from_histogram(hist, threshold)
            pct = metrics.percent(changed, min(sa[0], sb[0]) * min(sa[1], sb[1]))
            cells.append(f"{pct:g} %" + ("" if sa == sb else f" (size {sa} → {sb})"))
            if changed:
                diff_image(a, b, os.path.join(out, f"diff-{kind}-{name}.png"), name, threshold)
        moved_cells = []
        bo, bn = old[name].get("boxes"), new[name].get("boxes")
        if bo and bn:
            with open(bo, encoding="utf-8") as f:
                do = json.load(f)
            with open(bn, encoding="utf-8") as f:
                dn = json.load(f)
            (po, bro), (pn, brn) = texts(do), texts(dn)
            for label, (a_items, b_items) in (("panel", (po, pn)), ("browser", (bro, brn))):
                ch = metrics.box_changes(a_items, b_items)
                moved_cells.append(f"{len(ch['moved'])} (+{len(ch['added'])} / −{len(ch['gone'])})")
                for m in ch["moved"]:
                    details.append(f"- {name} {label}: \"{m['text'][:40]}\" moved "
                                   f"{m['dx']:+g} / {m['dy']:+g}, size {m['dw']:+g} / {m['dh']:+g}")
                for it in ch["added"]:
                    details.append(f"- {name} {label}: new \"{it['text'][:40]}\"")
                for it in ch["gone"]:
                    details.append(f"- {name} {label}: gone \"{it['text'][:40]}\"")
            oc = metrics.offset_changes(do.get("offsets", []), dn.get("offsets", []))
            moved_cells.append(str(len(oc)))
            for c in oc:
                details.append(f"- {name} offset: \"{c['text'][:40]}\" Δy {c['old_dy']:+g} → "
                               f"{c['new_dy']:+g}, Δx {c['old_dx']:+g} → {c['new_dx']:+g}")
        else:
            moved_cells = ["–", "–", "–"]
        lines.append(f"| {name} | " + " | ".join(cells + moved_cells) + " |")
    only_old, only_new = sorted(set(old) - set(new)), sorted(set(new) - set(old))
    if only_old:
        lines += ["", "Only before: " + ", ".join(only_old)]
    if only_new:
        lines += ["", "Only after: " + ", ".join(only_new)]
    if details:
        lines += ["", "## Changes", "", *details]
    report = "\n".join(lines) + "\n"
    with open(os.path.join(out, "diff.md"), "w", encoding="utf-8") as f:
        f.write(report)
    return report


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("old")
    p.add_argument("new")
    p.add_argument("--out", help="where diff.md and the diff images go (default NEW/diff)")
    p.add_argument("--threshold", type=int, default=16,
                   help="channel difference (0–255) a pixel needs to count as changed")
    a = p.parse_args(argv)
    for d in (a.old, a.new):
        if not os.path.isdir(d):
            p.error(f"{d} is not a directory")
    print(compare(a.old, a.new, a.out or os.path.join(a.new, "diff"), a.threshold), end="")
    return 0


if __name__ == "__main__":
    sys.exit(main())
