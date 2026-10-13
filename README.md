# Social Home ESPHome dashboard

Firmware that turns an ESPHome touch panel into a [Social Home](https://github.com/social-home-io/socialhome)
wall dashboard: the same household view as the browser dashboard, on a panel
on the wall.

> **Status: bootstrap.** The firmware boots, joins Wi-Fi through Improv, works
> as a Home Assistant Bluetooth proxy and updates itself over the air. The
> dashboard screens follow once the Social Home display API ships; until then
> the panel shows "Not connected yet" and its firmware version.

## Supported panels

| Panel | Chip | Screen | Bluetooth proxy |
|---|---|---|---|
| Guition JC8012P4A1 (10.1") | ESP32-P4 + ESP32-C6 | 1280×800 (landscape) | yes |

The catalogue lives in [`devices/manifest.json`](devices/manifest.json).

## Install

1. Open the [web installer](https://social-home-io.github.io/esphome-dashboard/)
   in Chrome or Edge on a desktop computer.
2. Plug the panel in with USB-C, pick it, press **Install**.
3. After flashing, the installer asks for your Wi-Fi (Improv over serial).

Other ways to set Wi-Fi:

- **Improv over Bluetooth:** Home Assistant discovers the panel, or use
  [improv-wifi.com](https://www.improv-wifi.com/) from a phone. The panel asks
  you to tap **Allow** before it accepts credentials.
- **Captive portal (fallback):** without Wi-Fi the panel opens a
  `socialhome-dashboard-xxxxxx` access point after about a minute.

### Home Assistant

Home Assistant finds the panel through ESPHome. Adopting it is optional: the
dashboard works without Home Assistant, and the native API never reboots the
panel when nothing connects. Once adopted, the panel also extends Home
Assistant's Bluetooth range (active Bluetooth proxy).

To build it yourself from the ESPHome dashboard, adopt the panel or start from
[`devices/guition-jc8012p4a1/esphome.yaml`](devices/guition-jc8012p4a1/esphome.yaml),
which pulls everything else from this repository.

### Updates

Releases publish firmware and an update manifest on GitHub Pages. The panel
exposes a **Firmware** update entity (and a **Co-processor firmware** entity
for the ESP32-C6 radio) that installs over the air.

## Repository layout

```
devices/manifest.json          device catalogue (drives the CI matrix)
devices/<slug>/esphome.yaml    user-adoptable config (remote packages)
devices/<slug>/packages.yaml   substitutions (screen size, fonts) + package list
devices/<slug>/device/         hardware: display, touch, radio, backlight, fonts
common/                        shared packages: network, API, OTA, update,
                               Improv, Bluetooth proxy, backlight, time
components/socialhome_display/ the dashboard (ESPHome external component)
builds/<slug>[.factory].yaml   CI and release entry points
docs/                          GitHub Pages web installer
scripts/                       CI helpers (device matrix, update manifest)
tests/                         helper tests; host C++ tests come with the app
```

## Development

```sh
python3 -m venv .venv
.venv/bin/pip install "esphome==$(sed -n 's/^ESPHOME_VERSION=//p' .github/esphome.env)" yamllint
.venv/bin/esphome config builds/guition-jc8012p4a1.factory.yaml
.venv/bin/esphome compile builds/guition-jc8012p4a1.factory.yaml
.venv/bin/esphome run builds/guition-jc8012p4a1.factory.yaml   # flash over USB
```

CI compiles every device on each pull request and uploads the factory and OTA
images as build artifacts, so a PR build can be flashed for testing. See
[CONTRIBUTING.md](CONTRIBUTING.md) to add a panel.

Releases use CalVer tags (`YYYY.M.D`, no `v` prefix). Publishing a release
builds every device, attaches the images to the release and deploys the web
installer plus update manifests to GitHub Pages.

## License

- The sources in this repository (YAML, C++, Python, web installer) are
  licensed under the [Mozilla Public License 2.0](LICENSE).
- The **compiled firmware** links ESPHome, whose C++ runtime is GPLv3, so the
  binaries are distributed under the **GNU GPL v3**. The sources to rebuild
  them are this repository plus the pinned ESPHome release
  (`.github/esphome.env`).
- The touch controller firmware blob is downloaded at build time from
  [esphome-libs/gsl3670-firmware](https://github.com/esphome-libs/gsl3670-firmware)
  (Apache-2.0); Roboto is downloaded from Google Fonts (Apache-2.0 / OFL).
- Planned: emoji tiles will use [Twemoji](https://github.com/jdecked/twemoji)
  graphics, licensed CC-BY 4.0. Attribution will ship in the firmware's about
  screen and a NOTICE file when they are added.
