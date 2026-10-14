# Contributing

Every change lands through a pull request; `main` is never pushed to directly.
Each PR needs at least one label so the release notes sort it:
`breaking`, `security`, `feat`, `fix`, `docs` or `chore`.

CI must be green (yamllint, script tests, `esphome config` + `esphome compile`
for every device) before a PR is merged.

Write configs and code from the ESPHome documentation and source. Do not copy
from projects whose licence is not compatible with MPL-2.0 (for example
PolyForm Noncommercial).

## Development container

Open the repo in VS Code with the Dev Containers extension (or any
devcontainer-capable editor / GitHub Codespaces) and it sets itself up
with what CI uses: ESPHome at the version pinned in
`.github/esphome.env`, yamllint, CMake for the host C++ tests, and SDL2 +
Mbed TLS for the host SDL panel, plus Xvfb (`DISPLAY=:99`) and Chromium
for screenshots.

- `.venv/bin/esphome compile builds/host-sdl.yaml` — the panel UI on your
  desktop, against a Social Home you point it at.
- `.venv/bin/esphome compile builds/guition-jc8012p4a1.factory.yaml` — the
  panel firmware.
- `cmake --build build/tests && ctest --test-dir build/tests` — host tests.
- `.venv/bin/python scripts/screenshots/run.py --socialhome ../socialhome --out /tmp/shots`
  — the panel next to the browser dashboard, every screen, for the PR
  (`.claude/skills/panel-screenshots/SKILL.md`).
- `python3 scripts/screenshots/check.py /tmp/shots` — what the Panel UI CI
  job asserts about that run.
- `python3 scripts/build_site.py fetch --repo social-home-io/esphome-dashboard --dest /tmp/rel`
  then `python3 scripts/build_site.py assemble --release-dir /tmp/rel --out /tmp/site`
  and `python3 -m http.server -d /tmp/site` — the installer site exactly as
  Pages deploys it (`docs/` changes go live on merge to `main`).

USB isn't passed into the container (it depends on your OS): flash with
the [web installer](https://social-home-io.github.io/esphome-dashboard/) or
`esphome run` on the host; OTA uploads (`esphome upload --device <ip>`)
work from inside.

## Adding a device

A new panel needs no app code changes as long as it is a colour touch panel of
at least 1280×800 that ESPHome supports.

1. **Hardware package:** create `devices/<slug>/device/device.yaml` with the
   `esp32` (or other platform), PSRAM, display, touchscreen, I²C, radio and an
   output named `backlight_output`. Give the display the id `main_display`,
   the touchscreen `main_touch` and LVGL the id `lvgl_comp` with one page
   `main_page`. Set LVGL `rotation` so the UI is landscape.
2. **Fonts:** `devices/<slug>/device/fonts.yaml` defines `font_small`,
   `font_label`, `font_body`, `font_body_bold`, `font_large`, `font_title`,
   `font_display`, `font_clock`, `font_night`, `font_lock_time`, `font_lock`,
   `font_text`, `font_text_bold`, `font_text_large`, `font_medium`,
   `font_medium_bold`, `font_heading`, `font_message`, `font_caption`,
   `font_tag`, `font_micro`, `font_clock_small` and `font_clock_tab` from the `font_size_*`
   substitutions (the clock fonts need digits and `:` only). A font left out
   falls back to a nearby size.
3. **Packages:** `devices/<slug>/packages.yaml` sets the substitutions
   (`device_slug`, `screen_width`, `screen_height`, `ui_scale`, font sizes)
   and lists the `common/` packages. Give `socialhome_display` the id
   `socialhome`: `common/api.yaml` (the `connect_social_home` action and the
   Social Home text sensors) refers to it. Leave out `bluetooth_proxy.yaml` and the
   `esp32_improv` part of Improv when the panel has no BLE, and
   `update_esp32_hosted.yaml` when it has no hosted co-processor.
4. **User config:** `devices/<slug>/esphome.yaml`, a copy of an existing one
   with the slug changed. This is what Home Assistant adopts.
5. **Build entry points:** `builds/<slug>.yaml` and
   `builds/<slug>.factory.yaml`, copies of an existing pair with the slug
   changed.
6. **Catalogue:** add the device to `devices/manifest.json` (`slug`, `name`,
   `chip`, `chip_family` as ESP Web Tools spells it, `width`, `height`, `ble`,
   `models`, `revisions`). CI picks it up from there.
7. Check locally:

   ```sh
   python3 scripts/device_matrix.py
   .venv/bin/esphome config builds/<slug>.factory.yaml
   .venv/bin/esphome compile builds/<slug>.factory.yaml
   ```

8. Flash it, set Wi-Fi through Improv, and note in the PR what you verified on
   real hardware (display orientation, touch, Wi-Fi, Bluetooth proxy, OTA).
