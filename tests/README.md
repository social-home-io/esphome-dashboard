# Tests

- `scripts/` — unit tests for the CI helper scripts and the pure parts of
  the screenshot tooling (`scripts/screenshots/`: file names, panel-dump vs
  browser box offsets, run-to-run diff maths)
  (`python -m unittest discover -s tests -t .`).
- `cpp/` — host C++ tests for the dashboard component: the JSON parser against
  every pinned Social Home fixture (the Connect from Home Assistant claim
  answers too), the layout / URL / emoji / request body helpers (text and
  emoji runs, a cut text ending in the browser's "…"), how timeline bars (the school stripes too, and the lanes' heights with their all-day chips, and which bars show only their icon), the browser's CSS line boxes on the panel's fonts (inline labels and tags on a shared baseline too), the browser's text widths over the panel's hinted ones (where a day-list row puts its title under the time), whether the rail names the Home tile (not in the picture view), the Overview's NEXT / messages column width, day-list rows and picture tiles are painted
  (past and cancelled words keep 4.5:1 in light, dark and night; a cancelled row's strike through its icon), how live data greys while offline (the words keep 4.5:1, the grey "now" mark its label; pictures greyed as CSS `grayscale()`, a grey copy of the decoded pixels), the scheme's note cards and clock dial, a stale map spot's "last seen" line, the map-picture fetch policy, the Privacy lock state Home Assistant sees (and that `ha_unlock` never rebuilds the lock screen), the PNG decoders, which mDNS
  answers the address screen lists (and the URL a tap sets) and the sealed
  connect claim (X25519 / HKDF / AES-GCM and strict base64url) against
  Social Home's shared vectors (`sealed-claim-vectors.json`), the panel's
  strings (theme or built-in English, `{n}` / `{host}` filled safely) and the
  cached theme (round-trip through the stored chunks, damage ignored), where
  the offline note sits (under the tabs, ahead of the @here banners) and its
  drawn ⚠ / ↻, and which command file a host panel reads. Mbed TLS 3.6
  (the line ESP-IDF ships) is fetched and built with the tests. `png_bench`
  (built alongside, not a test) times the map picture's decode:
  `build/tests/png_bench map.png`.
  `cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests`.
- `fixtures/display-api/` — the Social Home display API contract (schemas and
  real answers), copied verbatim from `docs/display-api/v1/` at the commit in
  `SOURCE`. `python3 scripts/display_api_contract.py` checks the copy (CI does
  too); after the server changes the API, bump `SOCIALHOME_REF`, run it with
  `--update` and make the parser tests pass again.
- Host panel (`builds/host-sdl.yaml`) — screenshots without hardware, driven
  through a command file (`builds/host/host_driver.h`). Each panel reads its
  own: `-s driver_file PATH` if given, else `$ESPHOME_PREFDIR/socialhome-host.cmd`,
  else `/tmp/socialhome-host-<pid>.cmd`; the panel logs `Reading commands
  from …` at start-up. Running two panels side by side, give each its own
  `ESPHOME_PREFDIR` (it holds the panel's server and token too) and its own
  `ESPHOME_SNAPSHOT_DIR`. A command file another running panel already
  reads is refused with an error, rather than both running each other's taps.
  `dump NAME` writes `NAME.txt` next to the snapshots: every visible object's
  box (x y w h), a label's font weight (`@300`) and text, to line the layout
  up with the browser's
  `getBoundingClientRect()`.
- Panel vs browser screenshots — `scripts/screenshots/run.py` does all of
  the above in one command: a scratch Social Home with the demo family, the
  host panel built, paired and driven through every screen next to the
  browser dashboard in headless Chromium, compare images, `dump` vs
  `getBoundingClientRect()` offsets, and `diff.py` between two runs. How to
  run and read it: `.claude/skills/panel-screenshots/SKILL.md`.
  `scripts/screenshots/check.py RUN` asserts the panel matches the browser
  (shots, ±4 px offsets, overlaps, words, font weight class, log errors) bar the reasoned
  exceptions in `scripts/screenshots/accepted.json`; the **Panel UI**
  workflow runs both on every pull request.
