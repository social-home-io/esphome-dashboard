#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Copy the factory + OTA images of the last compile into <out>/<slug>.*.bin.
# Usage: scripts/collect_firmware.sh <slug> <out-dir>
set -euo pipefail

slug="$1"
out="$2"
root="$(cd "$(dirname "$0")/.." && pwd)"

mapfile -t factories < <(find "${root}/builds/.esphome/build" -name firmware.factory.bin \
  \( -path '*/build/*' -o -path '*/.pioenvs/*' \) -printf '%T@ %p\n' | sort -rn | cut -d' ' -f2-)
if [[ ${#factories[@]} -eq 0 ]]; then
  echo "error: no firmware.factory.bin under builds/.esphome/build" >&2
  exit 1
fi
factory="${factories[0]}"
ota="$(dirname "${factory}")/firmware.ota.bin"
if [[ ! -f "${ota}" ]]; then
  echo "error: ${ota} missing" >&2
  exit 1
fi

mkdir -p "${out}"
cp "${factory}" "${out}/${slug}.factory.bin"
cp "${ota}" "${out}/${slug}.ota.bin"
ls -l "${out}/${slug}".*.bin
