# Tests

- `scripts/` — unit tests for the CI helper scripts
  (`python -m unittest discover -s tests -t .`).
- `cpp/` — host C++ tests for the dashboard component: the JSON parser against
  every pinned Social Home fixture (the Connect from Home Assistant claim
  answers too), the layout / URL / emoji / request body helpers (text and
  emoji runs), the map-picture fetch policy and the PNG decoders. `png_bench`
  (built alongside, not a test) times the map picture's decode:
  `build/tests/png_bench map.png`.
  `cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests`.
- `fixtures/display-api/` — the Social Home display API contract (schemas and
  real answers), copied verbatim from `docs/display-api/v1/` at the commit in
  `SOURCE`. `python3 scripts/display_api_contract.py` checks the copy (CI does
  too); after the server changes the API, bump `SOCIALHOME_REF`, run it with
  `--update` and make the parser tests pass again.
