# CLAUDE.md — esphome-dashboard

Instructions for AI agents working on the Social Home wall-panel firmware.
Read `CONTRIBUTING.md` (PR rules, labels, dev container, adding a device) and
`tests/README.md` (host tests, host SDL panel, test driver) first.

## One product on two screens

This firmware draws the Social Home household wall dashboard on ESPHome
touch panels (LVGL). The browser wall dashboard in
[social-home-io/socialhome](https://github.com/social-home-io/socialhome)
(`socialhome/dashboard/`) is the **source of truth**: the panel must look and
behave the same. Its own `CLAUDE.md` carries the mirror of this rule.

- **A change to what the wall shows needs a PR in both repos.** Layout,
  colours / palettes, copy, a board's content, the lock screen / PIN pad,
  night / offline / stale states, tab rules, a new or removed element — open
  the esphome-dashboard PR together with the matching socialhome PR and link
  them in both descriptions. A change only one screen can show (setup,
  pairing, settings, Improv on the panel; read-aloud in the browser) says so
  in the PR instead.
- **The panel gets everything through the display API** (`/api/display/v1/`).
  Never hard-code what the server knows: strings come pre-formatted and
  translated from the payload or `/theme` (the built-in English in
  `panel_strings.cpp` is only the fallback before a server answered), colours
  from `/theme` and the board. Missing data → a socialhome PR that adds it
  additively to v1.
- **The contract is pinned** in `tests/fixtures/display-api/` at the commit in
  `SOURCE`. Merge the socialhome PR first, then bump `SOCIALHOME_REF` to its
  merge commit, run `python3 scripts/display_api_contract.py --update`, fix
  the parser / host tests, and merge the firmware PR. CI fails when the copy
  drifts from the pinned commit.
- **Verify side by side** before opening the PR: the host SDL build
  (`builds/host-sdl.yaml`) next to the browser dashboard at 1280×800 against
  the same Social Home — light / dark / night, English + one other locale.
  The test driver's `dump` command gives LVGL object boxes to compare with
  the browser's measured boxes. Put the comparison in the PR; any remaining
  difference is fixed or listed (font and emoji artwork are the accepted
  ones). Give every host panel its own driver file (see `tests/README.md`).

## Firmware rules

- Only ESPHome's own components and code we write. Never copy from projects
  whose licence isn't MPL-2.0-compatible (e.g. jtenniswood/espcontrol,
  PolyForm Noncommercial). Sources are MPL-2.0; the compiled firmware is
  GPLv3 because it links ESPHome.
- ESPHome is pinned in `.github/esphome.env`; Renovate bumps it.
- Crypto wire shapes carry a suite tag and unknown suites are rejected (the
  sealed Connect-from-Home-Assistant claim, `x25519-hkdf-sha256-aes256gcm`).
  Never log the token, the claim or keys.
- Network, decoding and crypto run off the LVGL thread; the main loop never
  blocks on HTTP.
- New logic gets a host C++ test in `tests/cpp/` (pure helpers in `util.h`
  and friends are the pattern); CI runs them with the device compile.
- Releases are CalVer tags `YYYY.M.D[.N]`; a pre-release never replaces the
  update manifest panels poll.
