// SPDX-License-Identifier: MPL-2.0
// Renders one install button per device from devices.json (a copy of
// devices/manifest.json made by the release workflow).

const list = document.getElementById("devices");

function deviceItem(device, hasFirmware) {
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

  if (!hasFirmware) {
    const pending = document.createElement("span");
    pending.className = "muted";
    pending.textContent = "No release yet";
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

async function hasFirmware(slug) {
  try {
    const response = await fetch(`firmware/${slug}/manifest.json`, { method: "HEAD" });
    return response.ok;
  } catch {
    return false;
  }
}

async function render() {
  try {
    const response = await fetch("devices.json");
    if (!response.ok) throw new Error(response.statusText);
    const { devices } = await response.json();
    const items = await Promise.all(
      devices.map(async (device) => deviceItem(device, await hasFirmware(device.slug))),
    );
    list.replaceChildren(...items);
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
