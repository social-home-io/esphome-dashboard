# Tests

- `scripts/` — unit tests for the CI helper scripts
  (`python -m unittest discover -s tests -t .`).
- `cpp/` — host C++ tests for the dashboard component: the JSON parser against
  every pinned Social Home fixture (the Connect from Home Assistant claim
  answers too), the layout / URL / emoji / request body helpers (text and
  emoji runs), how timeline bars (the school stripes too, and the lanes' heights), day-list rows and picture tiles are painted
  (past and cancelled words keep 4.5:1 in light, dark and night), how live data greys while offline (the words keep 4.5:1, the grey "now" mark its label; pictures greyed as CSS `grayscale()`, a grey copy of the decoded pixels), the scheme's note cards and clock dial, a stale map spot's "last seen" line, the map-picture fetch policy, the PNG decoders, which mDNS
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
