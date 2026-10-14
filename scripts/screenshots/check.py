#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Check a screenshot run: does the panel show what the browser shows?

    python3 scripts/screenshots/check.py RUN_DIR [--accepted FILE] [--summary FILE]

Reads the run.py output in RUN_DIR and asserts (checks.py has the details):
every expected shot was taken, every text sits within the tolerance (4 px)
of the browser's, no panel text is off-screen / cut sideways / drawn over
another, both screens say the same words in the same weight class (light /
regular / bold), and the logs have no errors —
except what scripts/screenshots/accepted.json accepts, each with a reason.

Writes check.md (and check.json) into RUN_DIR, prints check.md, appends it
to --summary (CI: $GITHUB_STEP_SUMMARY). Exit 1 when anything failed, 2 when
the run or accepted.json can't be read. Standard library only.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import sys

import boxes
import checks
import naming

HERE = os.path.dirname(os.path.abspath(__file__))


def read(path: str) -> str:
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def check_shot(name: str, data: dict, acc: checks.Accepted) -> dict:
    shot = checks.shot_parts(name)
    roots = boxes.parse_dump(data.get("panel_dump", ""))
    out: dict = {"name": name, "screen": shot["screen"], "panel_only": shot["screen"] in naming.PANEL_ONLY}
    out["layout"] = checks.layout_failures(shot, checks.layout_problems(roots), acc)
    if out["panel_only"]:
        out.update(texts=0, max_pos=0.0, max_size=0.0, offsets=[], only_panel=[], only_browser=[], weights=[])
    else:
        p_texts = boxes.panel_texts(roots)
        b_texts = boxes.browser_texts(data.get("browser", []))
        matched, p_only, b_only = boxes.match(p_texts, b_texts)
        rows = list(data.get("offsets", [])) + checks.far_rows(p_only, b_only, p_texts, b_texts)
        out["texts"] = len(rows)
        out["max_pos"] = max((max(abs(r["dx"]), abs(r["dy"])) for r in rows), default=0.0)
        out["max_size"] = max((max(abs(r["dw"]), abs(r["dh"])) for r in rows), default=0.0)
        out["offsets"] = checks.offset_failures(shot, rows, acc)
        only_p, only_b = checks.word_differences(
            [p["text"] for p in p_texts if not p["joined"]], [b["text"] for b in b_texts])
        out["only_panel"], out["only_browser"] = checks.word_failures(shot, only_p, only_b, acc)
        out["weights"] = checks.weight_failures(shot, matched, acc)
    out["ok"] = not (out["layout"] or out["offsets"] or out["only_panel"] or out["only_browser"]
                     or out["weights"])
    return out


def _cell(text: str, limit: int = 32) -> str:
    t = " ".join(text.split()).replace("|", "\\|")
    return t if len(t) <= limit else t[: limit - 1] + "…"


def report(run_dir: str, meta: dict, general: list[str], shots: list[dict],
           unused: list[checks.Rule], tol: float, artifact: str) -> str:
    ok = not general and all(s["ok"] for s in shots)
    combos = ", ".join(f"{lo}/{sc}" for lo in meta.get("locales", []) for sc in meta.get("schemes", []))
    lines = [f"## Panel UI {'passed' if ok else 'FAILED'}: {combos or os.path.basename(run_dir)}", ""]
    if meta:
        lines.append(f"Social Home `{str(meta.get('socialhome_commit', ''))[:12]}`, firmware "
                     f"`{str(meta.get('firmware_commit', ''))[:12]}`, household time "
                     f"{meta.get('started', '?')} ({meta.get('time_zone', '?')}), "
                     f"{round(meta.get('took_s', 0) / 60, 1)} min, browser font "
                     f"{meta.get('browser_font', '?')}. Tolerance ±{tol:g} px "
                     "(panel − browser) unless scripts/screenshots/accepted.json accepts it.")
    if artifact:
        lines.append(f"Compare images, offsets and logs: [{artifact.rsplit('/', 1)[-1]}]({artifact}).")
    if general:
        lines += ["", "**Run problems:**", *[f"- {g}" for g in general]]
    lines += ["", "| shot | result | texts on both | largest Δx/Δy | largest Δw/Δh | problems |",
              "|---|---|---|---|---|---|"]
    for s in shots:
        probs = (len(s["offsets"]) + len(s["layout"]) + len(s["only_panel"]) + len(s["only_browser"])
                 + len(s["weights"]))
        if s["panel_only"]:
            lines.append(f"| {s['name']} | {'✅' if s['ok'] else '❌'} | panel only | | | {probs or ''} |")
        else:
            lines.append(f"| {s['name']} | {'✅' if s['ok'] else '❌'} | {s['texts']} | {s['max_pos']:g} | "
                         f"{s['max_size']:g} | {probs or ''} |")
    for s in shots:
        if s["ok"]:
            continue
        lines += ["", f"### {s['name']}"]
        for r in s["offsets"]:
            d = " ".join(f"{k} {r[k]:+g}" for k in checks.FIELDS)
            lines.append(f"- offset: {_cell(r['text'])!r} browser {r['browser']} panel {r['panel']}: {d}"
                         + (" (further than boxes.match reaches)" if r.get("far") else ""))
        lines += [f"- layout: {p}" for p in s["layout"]]
        if s["only_panel"]:
            lines.append("- only the panel says: " + ", ".join(s["only_panel"]))
        if s["only_browser"]:
            lines.append("- only the browser says: " + ", ".join(s["only_browser"]))
        lines += [f"- weight: {_cell(w['text'])!r} browser {w['browser']}, panel {w['panel']}"
                  for w in s["weights"]]
    if unused:
        lines += ["", "Accepted differences not seen in this run (stale? remove them if no run needs them): "
                  + ", ".join(r.label() for r in unused)]
    return "\n".join(lines) + "\n"


def suggest(shots: list[dict], margin: float = 3.0) -> dict:
    """accepted.json entries (reason "TODO") that would accept every failure
    of this run: one per text (digits as ``\\d+``) with the screens it fails on
    and its largest offsets plus ``margin``. A start for a human to merge into
    real, explained entries — never paste it unread."""
    offsets: dict[str, dict] = {}
    texts: dict[tuple[str, str], set[str]] = {}
    layout: dict[str, set[str]] = {}
    weights: dict[str, set[str]] = {}
    for s in shots:
        for w in s.get("weights", []):
            key = "^" + re.sub(r"\d+", r"\\d+", re.escape(boxes.normalize(w["text"]))) + "$"
            weights.setdefault(key, set()).add(s["screen"])
        for r in s["offsets"]:
            norm = boxes.normalize(r["text"])
            key = "^" + re.sub(r"\d+", r"\\d+", re.escape(norm)) + "$"
            e = offsets.setdefault(key, {"screens": set(), "text": key, "max": {}})
            e["screens"].add(s["screen"])
            for k in r["over"]:
                e["max"][k] = max(e["max"].get(k, 0), math.ceil(abs(r[k]) + margin))
        for side in ("panel", "browser"):
            for w in s["only_" + side]:
                texts.setdefault((s["screen"], side), set()).add(w)
        for p in s["layout"]:
            layout.setdefault(re.escape(p.split(" at ")[0]), set()).add(s["screen"])
    return {
        "offsets": [{**e, "screens": sorted(e["screens"]), "reason": "TODO"} for e in offsets.values()],
        "texts": [{"screens": [sc], "side": side, "words": sorted(ws), "reason": "TODO"}
                  for (sc, side), ws in sorted(texts.items())],
        "layout": [{"screens": sorted(sc), "text": t, "reason": "TODO"} for t, sc in layout.items()],
        "weights": [{"screens": sorted(sc), "text": t, "reason": "TODO"} for t, sc in weights.items()],
    }


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("run", help="a run.py --out directory")
    p.add_argument("--accepted", default=os.path.join(HERE, "accepted.json"))
    p.add_argument("--summary", help="append the report here too (CI: $GITHUB_STEP_SUMMARY)")
    p.add_argument("--suggest", metavar="FILE",
                   help="also write accepted.json entries that would accept this run's failures")
    p.add_argument("--artifact-url", default="", help="where the run's files are (linked in the report)")
    a = p.parse_args(argv)
    try:
        acc = checks.load_accepted(json.loads(read(a.accepted) or "null"))
    except (ValueError, checks.AcceptedError) as e:
        print(f"error: {a.accepted}: {e}", file=sys.stderr)
        return 2
    if not os.path.isdir(a.run):
        print(f"error: no run in {a.run}", file=sys.stderr)
        return 2

    general: list[str] = []
    meta_text = read(os.path.join(a.run, "run.json"))
    meta = json.loads(meta_text) if meta_text else {}
    if not meta:
        general.append("run.py did not finish (no run.json): see logs/ and the job log")
    else:
        present = set(os.listdir(a.run))
        missing = [f for f in checks.expected_files(meta) if f not in present]
        if missing:
            general.append(f"{len(missing)} expected files missing: " + ", ".join(missing[:20]))
        general += [f"run note: {n}" for n in checks.note_failures(meta.get("notes", []))]

    shots = []
    for f in sorted(os.listdir(a.run), key=lambda f: order(f)):
        parts = naming.parse_file(f)
        if not parts or parts["kind"] != "boxes":
            continue
        data = json.loads(read(os.path.join(a.run, f)))
        shots.append(check_shot(parts["name"], data, acc))
    if not shots:
        general.append("no shots in the run")

    general += checks.log_failures(read(os.path.join(a.run, "logs", "panel.log")),
                                   read(os.path.join(a.run, "logs", "household.log")), acc)

    covered = [checks.shot_parts(s["name"]) for s in shots]
    unused = [r for r in acc.all() if not r.used and r.kind != "log"
              and any(r.scoped(c) for c in covered)]
    text = report(a.run, meta, general, shots, unused, acc.tolerance, a.artifact_url)
    with open(os.path.join(a.run, "check.md"), "w", encoding="utf-8") as f:
        f.write(text)
    ok = not general and all(s["ok"] for s in shots)
    with open(os.path.join(a.run, "check.json"), "w", encoding="utf-8") as f:
        json.dump({"ok": ok, "general": general, "shots": shots,
                   "unused_accepted": [r.label() for r in unused]}, f, ensure_ascii=False, indent=1)
    if a.suggest:
        with open(a.suggest, "w", encoding="utf-8") as f:
            json.dump(suggest(shots), f, ensure_ascii=False, indent=1)
    if a.summary:
        with open(a.summary, "a", encoding="utf-8") as f:
            f.write(text + "\n")
    print(text)
    return 0 if ok else 1


def order(filename: str) -> tuple[int, str]:
    parts = naming.parse_file(filename)
    if not parts:
        return (len(naming.SCREENS), filename)
    return (naming.SCREENS.index(parts["screen"]), parts["name"])


if __name__ == "__main__":
    sys.exit(main())
