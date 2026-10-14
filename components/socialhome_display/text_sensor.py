# SPDX-License-Identifier: MPL-2.0
"""Text sensors Home Assistant (and through it Social Home) reads.

- ``status``: unpaired / pairing / paired / locked / unreachable, published
  when it changes.
- ``node``: the ESPHome node name, published once at boot. Social Home finds
  it in Home Assistant's entity registry by its name and derives the
  ``esphome.<node>_connect_social_home`` action from it.
- ``key``: the panel's X25519 public key (43-character unpadded base64url),
  published at boot and whenever the key is renewed (Forget server). Social
  Home finds it in Home Assistant's entity registry by its name and seals
  the connect claim to it, so Home Assistant's recorder never sees the
  claim.
"""

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import SocialHomeDisplay

DEPENDENCIES = ["socialhome_display"]

CONF_SOCIALHOME_DISPLAY_ID = "socialhome_display_id"
CONF_STATUS = "status"
CONF_NODE = "node"
CONF_KEY = "key"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SOCIALHOME_DISPLAY_ID): cv.use_id(SocialHomeDisplay),
        cv.Optional(CONF_STATUS): text_sensor.text_sensor_schema(
            icon="mdi:tablet-dashboard",
        ),
        cv.Optional(CONF_NODE): text_sensor.text_sensor_schema(
            icon="mdi:identifier",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_KEY): text_sensor.text_sensor_schema(
            icon="mdi:key-variant",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    display = await cg.get_variable(config[CONF_SOCIALHOME_DISPLAY_ID])
    if status := config.get(CONF_STATUS):
        cg.add(display.set_status_sensor(await text_sensor.new_text_sensor(status)))
    if node := config.get(CONF_NODE):
        cg.add(display.set_node_sensor(await text_sensor.new_text_sensor(node)))
    if key := config.get(CONF_KEY):
        cg.add(display.set_key_sensor(await text_sensor.new_text_sensor(key)))
