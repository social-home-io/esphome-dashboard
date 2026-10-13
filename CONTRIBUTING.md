# Contributing

Every change lands through a pull request; `main` is never pushed to directly.
Each PR needs at least one label so the release notes sort it:
`breaking`, `security`, `feat`, `fix`, `docs` or `chore`.

CI must be green (yamllint, script tests, `esphome config` + `esphome compile`
for every device) before a PR is merged.

Write configs and code from the ESPHome documentation and source. Do not copy
from projects whose licence is not compatible with MPL-2.0 (for example
PolyForm Noncommercial).

## Adding a device

A new panel needs no app code changes as long as it is a colour touch panel of
at least 1280×800 that ESPHome supports.

1. **Hardware package:** create `devices/<slug>/device/device.yaml` with the
   `esp32` (or other platform), PSRAM, display, touchscreen, I²C, radio and an
   output named `backlight_output`. Give the display the id `main_display`,
   the touchscreen `main_touch` and LVGL the id `lvgl_comp` with one page
   `main_page`. Set LVGL `rotation` so the UI is landscape.
2. **Fonts:** `devices/<slug>/device/fonts.yaml` defines `font_small`,
   `font_body` and `font_title` from the `font_size_*` substitutions.
3. **Packages:** `devices/<slug>/packages.yaml` sets the substitutions
   (`device_slug`, `screen_width`, `screen_height`, `ui_scale`, font sizes)
   and lists the `common/` packages. Leave out `bluetooth_proxy.yaml` and the
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
