# Social Home ESPHome dashboard

Firmware that turns an ESPHome touch panel into a [Social Home](https://github.com/social-home-io/socialhome)
wall dashboard: the same household view as the browser dashboard, on a panel
on the wall.

> **Status: beta.** The panel finds Social Home on the home network, pairs
> with the household and shows the household dashboard: Overview, Messages
> (with a count of new ones on the tab), Notes, Map ("Who's where") with the
> map picture, each person's page and the picture view for children, the
> night screen and the privacy lock with its PIN pad, and "Got it" on
> mentions. It joins Wi-Fi through Improv, works as a Home Assistant
> Bluetooth proxy and updates itself over the air.

![The panel's Overview](docs/screenshots/panel-overview.png)

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

### Connect to Social Home

1. Once on Wi-Fi the panel looks for Social Home on your network and lists
   what it finds under **Found on this network**: tap yours. Not listed (the
   Home Assistant App doesn't announce itself, and some networks block
   mDNS)? Type its IP or host name; the dashboard port 8100 is added for
   you.

   ![The address screen with a Social Home found](docs/screenshots/panel-setup-found.png)
2. The panel shows a six-character code. In Social Home open
   **Admin › Dashboard › Wall panels › Add** and enter it.
3. The panel shows the dashboard. It polls every 30 seconds (and only
   downloads what changed).

Or let Home Assistant do it: adopt the panel in Home Assistant, then press
**Connect** for it in Social Home. Social Home finds the panel through Home
Assistant and calls the panel's `connect_social_home` action with its address
and a one-time claim (valid ten minutes, used once); the panel trades the
claim for its token, no code needed. Connecting a panel that is already
paired moves it to the new pairing; if the connect fails, a paired panel
keeps its old pairing.

Hold the clock for five seconds to open the panel's settings: the server, the
panel id, the firmware, the licences, and **Forget server** to start over.
Removing the panel in Social Home sends it back to the pairing code.

### Home Assistant

Home Assistant finds the panel through ESPHome. Adopting it is optional: the
dashboard works without Home Assistant, and the native API never reboots the
panel when nothing connects. Once adopted, the panel also extends Home
Assistant's Bluetooth range (active Bluetooth proxy), offers the
`connect_social_home` action Social Home uses to connect it (see above) and
reports two text sensors: **Social Home status** (`unpaired`, `pairing`,
`paired`, `locked` or `unreachable`) and **Social Home node** (diagnostic: the
ESPHome node name, which Social Home reads to call the action).

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
tests/                         script tests, host C++ tests (tests/cpp) and the
                               pinned display API contract (tests/fixtures)
builds/host-sdl.yaml           the app in a headless SDL window, for screenshots
```

## The app

`components/socialhome_display` is plain C++ on ESPHome's LVGL:

- `parser.*` / `model.h`: the Social Home display API v1 (`/api/display/v1/`)
  parsed with ArduinoJson into structs. Unknown fields are ignored; an
  `api_version` other than 1 shows "This panel needs an update".
- `net.*`: HTTP on a worker thread (`esp_http_client` on the panel,
  cpp-httplib on the host build) with ETag revalidation; avatars and emoji are
  decoded (pngle) into ARGB8888 off the main loop.
- `images.*`: a small PSRAM LRU cache of those pictures.
- `socialhome_display.cpp`: setup → pairing (a code, or Connect from Home
  Assistant via `POST /pair/claim`) → polling, every status code the
  API documents (401 pair again, 404/409 back to Overview, 423 lock screen,
  429 Retry-After), clock, idle return, night dimming, screen-off timeout.
- `ui_*.cpp`: the screens, laid out from the 1280×800 browser dashboard and
  scaled from `screen_width` / `screen_height` / `ui_scale`.

Every string a person reads comes from the server in the household's language;
the panel's own English is only for the setup screens. Emoji are pictures
(Twemoji, CC-BY 4.0) from the server, as the fonts carry none.

## Development

```sh
python3 -m venv .venv
.venv/bin/pip install "esphome==$(sed -n 's/^ESPHOME_VERSION=//p' .github/esphome.env)" yamllint
.venv/bin/esphome config builds/guition-jc8012p4a1.factory.yaml
.venv/bin/esphome compile builds/guition-jc8012p4a1.factory.yaml
.venv/bin/esphome run builds/guition-jc8012p4a1.factory.yaml   # flash over USB
```

Host tests (parser against the pinned fixtures, layout maths):

```sh
cmake -S tests/cpp -B build/tests && cmake --build build/tests && ctest --test-dir build/tests
```

Screenshots without hardware (needs `libsdl2-dev` and `libssl-dev`): build
`builds/host-sdl.yaml`, run the binary, and drive it through its command file
(see `builds/host/host_driver.h`). It writes BMPs to
`builds/.esphome/snapshots/socialhome-host/`. The host build has no mDNS: to
see the **Found on this network** list, pass a fixed one, e.g.
`-s discovery_stub "Social Home 1a2b3c|socialhome-1a2b3c|127.0.0.1|8100|api=1,id=1a2b3c"`.

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
