---
name: panel-screenshots
description: Screenshots the wall panel (the host SDL build of this firmware) next to the browser wall dashboard (headless Chromium, 1280×800) against one scratch Social Home with a seeded demo family, for every screen × locale × light/dark, with side-by-side compare images, LVGL-vs-DOM box offsets, and a diff against an earlier run. Use before opening a PR that changes what the wall shows (CLAUDE.md "verify side by side", both repos), for a release sign-off, or to compare two firmware / Social Home versions. One command — scripts/screenshots/run.py — then scripts/screenshots/diff.py OLD NEW.
---

# Panel vs browser screenshots

The browser dashboard in social-home-io/socialhome (`socialhome/dashboard/`)
is the source of truth; the panel must look the same. This skill takes both
pictures of the same household a moment apart, measures where every text
sits on each, and keeps the files under fixed names so a later run can be
laid over an earlier one.

## When to use

- **Before a PR that changes what the wall shows** — layout, colours, copy,
  a board, the lock screen / PIN pad, night / offline / stale states, tabs.
  Both repos' CLAUDE.md ask for the side-by-side; put it in the PR.
- **Release sign-off** — all screens, `en,de`, `light,dark` (add `auto`).
- **Comparing two versions** — run on `main`, run on your branch (same
  `--time-zone`), `diff.py` the two.

## Prerequisites

- The devcontainer (`.devcontainer/`): ESPHome in `.venv`, SDL2 / Mbed TLS,
  Chromium, Pillow (ESPHome brings it). Outside it: `cmake build-essential
  libsdl2-dev libssl-dev libmbedtls-dev chromium` from apt and ESPHome at the
  version in `.github/esphome.env` in `.venv` (`pip install pillow` if that
  venv has none). No X display is needed — the SDL panel is headless.
- **A socialhome checkout** at the commit the contract pins
  (`SOCIALHOME_REF` in `tests/fixtures/display-api/SOURCE`), or the
  socialhome PR branch this firmware change pairs with. A worktree is fine:

  ```sh
  git -C ../socialhome worktree add ../socialhome-shots <ref>
  cp ../socialhome/socialhome/_version.py ../socialhome-shots/socialhome/   # hatch-vcs writes it on install
  ```

- **A Python with Social Home's dependencies.** The script never installs
  anything: it runs that Python with `PYTHONPATH=<checkout>` (the checkout
  wins over an installed copy). Default `<checkout>/.venv/bin/python`; point
  `--socialhome-python` at another checkout's venv when the worktree has
  none (e.g. `../socialhome/.venv/bin/python`).

## The one command

```sh
.venv/bin/python scripts/screenshots/run.py \
  --socialhome ../socialhome-shots \
  --socialhome-python ../socialhome/.venv/bin/python \
  --out /tmp/shots-main \
  --locale en,de --scheme light,dark
```

Options: `--screens a,b,…` (default all: `setup, pairing, overview,
messages, notes, map, reader, picture, settings, night, lock, pinpad,
wrong-pin, last-try, offline`), `--ports BASE` (Social Home on BASE, its
dashboard on BASE+1, the run's control API on BASE+2; default 19780 — give
each parallel run its own), `--time-zone` (see below), `--esphome`,
`--panel-binary` (skip the build), `--chromium`, `--browser-font
roboto|system` (default roboto, see below), `--keep-work`.

What it does, in order — and it stops everything it started on success,
error or Ctrl-C:

1. Builds `builds/host-sdl.yaml` with a `discovery_stub` naming the
   household (the host build has no mDNS) and copies the program aside.
2. Starts Social Home from the checkout (temp data dir) and seeds the
   release sign-off family: Anna (admin), Pascal, Mia 11 (reader view),
   Leo 6 (picture view); Home / School / Work / Gran's zones and where each
   is; Mia's and Leo's timetables (a cancelled lesson, a moved room); timed
   and all-day calendar items; chores; shopping; seven sticky notes;
   household chat with `@mia`, `@leo` and an `@here`. Dashboard on, tablet
   password, PIN `2468`. Household time is ~16:05 (a time zone is
   picked so the real clock reads that), so lessons are under way — a
   weekday where one is, else (Saturday morning to Sunday morning UTC) the
   weekend day is made a school day for the run.
3. Starts the panel fresh (own `ESPHOME_PREFDIR` → own driver file, own
   snapshot dir), shoots the address screen, taps the household, shoots the
   pairing code, pairs it as the admin.
4. Per locale (a Chromium per locale, `--lang`) and scheme: sets the
   dashboard language and theme, restarts the panel (fresh theme), takes a
   new tablet cookie, then per screen: browser to the screen → panel to the
   screen + `shot` + `dump` → browser screenshot + boxes. When the shots
   wouldn't fit in the current minute it waits for the next one; if the
   minute still turned between the two shots it takes the pair again.

About 5 min per locale × scheme with every screen (the full `en,de` ×
`light,dark` set: ~20 min, 54 shots); the plain screens alone take ~1 min.
Offline is the slow one: it waits for the browser's two failed 30 s
refreshes.

## Output (fixed names — `scripts/screenshots/naming.py`)

| file | what |
|---|---|
| `panel-NAME.png`, `browser-NAME.png` | the two screens, NAME = `screen-locale-scheme` (`setup`, `pairing`: no suffix — the panel speaks built-in English before it has a household) |
| `compare-NAME.png` | browser left, panel right, labelled |
| `boxes-NAME.json` | the panel `dump`, the browser's text boxes, the matched offsets, both shot times |
| `offsets-NAME.md` | one row per text both screens show: browser box, panel box, Δx Δy Δw Δh (panel − browser), `**` over 4 px; texts only one side has |
| `offsets.md` | the run: commits, household time zone, per shot the panel / browser minute, matched texts, median and largest offsets |
| `run.json` | the same as data, plus notes (minutes that differ, a step that didn't reach its state) |
| `logs/` | `household.log`, `panel.log`, `build.log` |

`settings`, `setup` and `pairing` are panel-only: no browser shot or compare.

## Reading the results

- **Compare images first.** Same content, same order, same colours, same
  states (NOW / NEXT, cancelled, NEW badges, @here banner, greyed offline
  data). Then the boxes.
- **offsets-NAME.md:** a texts' Δy / Δx is where it sits; Δw is mostly font
  (see below). Look for a whole block shifted (all rows below a point with
  the same Δy — a margin or a row height), not single words. A text in
  "Only in the browser / panel" is missing on the other screen, or worded
  differently, or > 160 px away from where the other screen has it.
- **Compare two runs:**

  ```sh
  .venv/bin/python scripts/screenshots/diff.py /tmp/shots-main /tmp/shots-branch
  ```

  `diff.md` (also printed) in `NEW/diff/`: per shot the share of changed
  pixels on panel and browser, texts that moved / appeared / went, offsets
  that changed; `diff-panel-NAME.png` / `diff-browser-NAME.png` show before
  | after | changed pixels in magenta. Give the second run the first's time
  zone (`--time-zone` from `run.json`), else a different hour of the board
  shows. Two runs of the same code still differ in the clock minute, "N min
  ago" / "in N min", the NOW / NEXT progress and the panel id on `settings`.

## Known, accepted differences

- **Fonts:** the panel draws Roboto. By default (`--browser-font roboto`)
  the run gives Chromium a fontconfig that draws the dashboard's
  `system-ui` stack with the very Roboto files the panel build fetched
  (`builds/.esphome/font/`), as an Android tablet would — so Δw shows real
  font-size differences, not DejaVu's wider glyphs. `--browser-font system`
  is the machine's own sans (DejaVu on Linux: wider, other line breaks).
- **Box shape:** many panel labels are as wide as their cell / row with the
  text centred or left in it; the browser box is the text itself. Δw (and
  Δx for centred text) then says nothing about where the text is drawn.
  `scripts/screenshots/accepted.json` names each such case.
- **Emoji:** Twemoji pictures on the panel, Noto Color Emoji in Chromium.
- **Speech:** the browser's read-aloud (🔊) has no panel counterpart.
- **Auto scheme ("Auto by day"):** the panel follows the household's Auto
  (dark at night); the headless browser is told `prefers-color-scheme:
  light`. By day both are light.

## Checking a run (what CI asserts)

```sh
python3 scripts/screenshots/check.py /tmp/shots-branch
```

Fails (exit 1, `check.md` in the run dir, printed too) when: an expected
shot is missing or a step didn't reach its state; a text both screens show
is more than ±4 px off (Δx Δy Δw Δh, or further than `boxes.match` reaches);
a panel text runs off the screen, is cut sideways by its container, or is
drawn over another text in the same layer (the clock over the offline
note); the two screens say different words (clock times and numbers
aside); a text both show is in another weight class — light (≤ 350),
regular (≤ 550), bold — than the browser's computed font-weight (the dump
names each label's font weight; 500 / 600 draw as the panel's 400 / 700);
the panel log has an `[E]` line or a crash, the household log a
traceback. Except what `scripts/screenshots/accepted.json` accepts — every
entry scoped to screens (and optionally locales / schemes), with bounds and
a **reason** (format: `checks.py`). `--suggest FILE` writes entries that
would accept a run's failures: a start for writing real ones, never to
paste unread.

The **Panel UI** workflow (`.github/workflows/panel-ui.yml`) runs `run.py`
+ `check.py` on every pull request and push to `main`, one job per locale
(en, de) × scheme (light, dark), against the Social Home `SOURCE` pins,
with the browser on Roboto. The job summary has the per-shot table and a
link to the artifact (every PNG, `offsets*.md`, `boxes-*.json`, `check.md`,
logs); a `diff.py` against the last green `main` run is added as
information, never a failure. A PR that fixes a gap tightens or deletes its
`accepted.json` entry (the report lists entries a run didn't need); a PR
that widens one says why in its description.

## Pitfalls (learned the hard way)

- **Driver file cross-talk:** two host panels reading one command file run
  each other's taps. The script gives its panel its own `ESPHOME_PREFDIR`;
  if you drive a panel by hand next to a run, give it another (the driver
  refuses a file another panel holds — see `tests/README.md`).
- **`VIRTUAL_ENV` pointing at another project's venv** (e.g. Social Home's)
  makes `esphome` / `uv pip install` act on the wrong venv. The script
  strips it for its children; run it with `env -u VIRTUAL_ENV` and never
  install into a venv you don't own.
- **Never restart Social Home mid-run:** it re-seeds a new database and the
  panel's token stops working. Offline is shot by freezing the server
  (`SIGSTOP`, browser network emulated offline) and thawing it (`SIGCONT`).
- **Night in the browser:** a tap (a `pointerdown` event) wakes the night
  screen for 60 s — a stray click before the shot shows the day board.
- **Minute boundaries** change the clock, "N min ago", NOW / NEXT and NEW
  badges. The script waits for a quiet minute and retakes a pair that
  straddles one; `offsets.md` lists both minutes. The browser's lock page is
  drawn by the last PIN answer and its clock ticks on its own schedule, so
  the lock steps wait for a quiet minute *before* the PIN is sent.
- **NEW badges expire:** a message is NEW for 15 min. The seeded "@leo
  Shoes on!" is 2 min old at the start, so about 13 min into a full run the
  badge (and the Messages tab's unread count) goes on both screens; a shot
  right then — the offline one above all, which shows each screen's last
  fetch — can differ by it. Take that shot again; CI's runs (~6 min) never
  reach it.
- **Wait for pictures:** the script waits for fonts and every `<img>` in the
  browser and gives the panel 7 s on the map (it fetches and decodes a PNG).
  A blank avatar or map on one side is a timing problem first.
- **PIN tries:** five per panel and per tablet session; the sixth wrong PIN
  signs the panel out (unpairs it). The run stops at "one try left" and
  lifts the lock as the admin (which also resets the tries).
- **Killing by name** (`pkill -f …program`) can hit another agent's panel or
  your own shell; stop what you started by PID. The script does.
- **Ports in use:** pick another `--ports` base; each run needs three.

## What it cannot verify

The real ESP32-P4 panel: the MIPI display's colours and gamma, touch
accuracy and long-press feel, backlight / screen-off, Wi-Fi and Bluetooth
proxy, Improv, OTA, memory and drawing speed, fonts at the device's actual
size. Say in the PR what you checked on hardware (CONTRIBUTING.md).

## Putting it in a PR

- Attach (or link) the `compare-*.png` of the screens the change touches,
  in both schemes and two locales, plus the relevant `offsets-*.md` rows.
- For a change of existing screens: run on `main` and on the branch with
  the same `--time-zone`, and paste the `diff.md` table — it shows what
  moved and that nothing else did.
- List every remaining difference as fixed or accepted (fonts and emoji are
  the accepted ones), and the socialhome commit you ran against.
