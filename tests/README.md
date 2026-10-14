# Tests

- `scripts/` — unit tests for the CI helper scripts
  (`python -m unittest discover -s tests -t .`).
- `cpp/` — host C++ tests for the dashboard component: the JSON parser against
  every pinned Social Home fixture (the Connect from Home Assistant claim
  answers too), the layout / URL / emoji / request body helpers (text and
  emoji runs), how timeline bars, day-list rows and picture tiles are painted
  (past and cancelled words keep 4.5:1 in light, dark and night), the map-picture fetch policy, the PNG decoders, which mDNS
  answers the address screen lists (and the URL a tap sets) and the sealed
  connect claim (X25519 / HKDF / AES-GCM and strict base64url) against
  Social Home's shared vectors (`sealed-claim-vectors.json`), the panel's
  strings (theme or built-in English, `{n}` / `{host}` filled safely) and the
  cached theme (round-trip through the stored chunks, damage ignored). Mbed TLS 3.6
  (the line ESP-IDF ships) is fetched and built with the tests. `png_bench`
  (built alongside, not a test) times the map picture's decode:
  `build/tests/png_bench map.png`.
  `cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests`.
- `fixtures/display-api/` — the Social Home display API contract (schemas and
  real answers), copied verbatim from `docs/display-api/v1/` at the commit in
  `SOURCE`. `python3 scripts/display_api_contract.py` checks the copy (CI does
  too); after the server changes the API, bump `SOCIALHOME_REF`, run it with
  `--update` and make the parser tests pass again.
