#!/usr/bin/env bash
# One-time devcontainer bootstrap (postCreateCommand).
#
# Installs the same tools CI uses, at the same versions:
#   - ESPHome pinned in .github/esphome-requirements.txt (Dependabot bumps it),
#   - yamllint,
# and configures the host C++ tests so `ctest` works on first launch.
#
# Flashing over USB is not wired into the container: USB pass-through
# depends on the host OS. Flash with the web installer
# (https://social-home-io.github.io/esphome-dashboard/) or run
# `esphome run` on the host; OTA (`esphome upload --device <ip>`)
# works from inside the container.

set -euo pipefail

# Debian's chromium launcher sources /etc/chromium.d/*.conf; the container
# can't use Chromium's sandbox and /dev/shm is small.
echo 'export CHROMIUM_FLAGS="$CHROMIUM_FLAGS --no-sandbox --disable-dev-shm-usage"' \
	| sudo tee /etc/chromium.d/devcontainer.conf >/dev/null

# The pinned version, for the message below (CI reads it the same way).
ESPHOME_VERSION="$(sed -n 's/^esphome==//p' .github/esphome-requirements.txt)"

uv venv .venv --seed
# ESPHome brings Pillow, which scripts/screenshots (panel vs browser shots)
# needs too; run it with .venv/bin/python.
uv pip install --refresh -r .github/esphome-requirements.txt yamllint

# Host C++ tests: configure once; `cmake --build build/tests && ctest
# --test-dir build/tests` from then on.
cmake -S tests/cpp -B build/tests -DCMAKE_BUILD_TYPE=Debug >/dev/null

echo "ESPHome ${ESPHOME_VERSION} ready in .venv — try:"
echo "  .venv/bin/esphome compile builds/host-sdl.yaml   # the panel UI in an SDL window"
echo "  .venv/bin/esphome compile builds/guition-jc8012p4a1.factory.yaml"
