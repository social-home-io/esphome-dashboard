// SPDX-License-Identifier: MPL-2.0
// Renders one install button per device from devices.json (a copy of
// devices/manifest.json) and release.json (which stable release the firmware
// on this site comes from), both written by scripts/build_site.py.

const RELEASES_URL = "https://github.com/social-home-io/esphome-dashboard/releases";
const list = document.getElementById("devices");
const releaseNote = document.getElementById("release");

function link(href, text) {
  const anchor = document.createElement("a");
  anchor.href = href;
  anchor.textContent = text;
  return anchor;
}

function deviceItem(device, release) {
  const item = document.createElement("li");
  item.className = "device";

  const text = document.createElement("div");
  const name = document.createElement("h3");
  name.textContent = device.name;
  const meta = document.createElement("p");
  meta.className = "muted";
  const features = [`${device.width}×${device.height}`, device.chip];
  if (device.ble) features.push("Bluetooth proxy");
  meta.textContent = features.join(" · ");
  text.append(name, meta);
  item.append(text);

  if (!release.firmware.includes(device.slug)) {
    const pending = document.createElement("span");
    pending.className = "muted";
    pending.textContent = release.tag ? "Coming in the next release" : "No firmware release yet";
    item.append(pending);
    return item;
  }

  const button = document.createElement("esp-web-install-button");
  button.setAttribute("manifest", `firmware/${device.slug}/manifest.json`);
  const unsupported = document.createElement("span");
  unsupported.slot = "unsupported";
  unsupported.className = "muted";
  unsupported.textContent = "Needs Chrome or Edge";
  button.append(unsupported);
  item.append(button);
  return item;
}

function showRelease(release) {
  if (release.tag) {
    releaseNote.replaceChildren(
      `Firmware ${release.tag} (`,
      link(release.release_url, "release notes"),
      ").",
    );
  } else {
    releaseNote.classList.add("callout");
    releaseNote.replaceChildren(
      "No firmware release yet. Install buttons appear here once the first release is published; follow the ",
      link(RELEASES_URL, "releases page"),
      " to hear about it.",
    );
  }
  releaseNote.hidden = false;
}

async function loadJson(path) {
  const response = await fetch(path, { cache: "no-cache" });
  if (!response.ok) throw new Error(`${path}: ${response.status} ${response.statusText}`);
  return response.json();
}

async function render() {
  try {
    const [{ devices }, release] = await Promise.all([
      loadJson("devices.json"),
      loadJson("release.json"),
    ]);
    showRelease(release);
    list.replaceChildren(...devices.map((device) => deviceItem(device, release)));
  } catch (err) {
    const item = document.createElement("li");
    item.className = "device muted";
    item.textContent = `Couldn't load the device list (${err.message}). Reload to try again.`;
    list.replaceChildren(item);
  }
  if (!("serial" in navigator)) {
    document.getElementById("unsupported").hidden = false;
  }
}

render();
