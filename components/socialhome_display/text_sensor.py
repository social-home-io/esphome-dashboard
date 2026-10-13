# SPDX-License-Identifier: MPL-2.0
"""Text sensors Home Assistant (and through it Social Home) reads.

- ``status``: unpaired / pairing / paired / locked / unreachable, published
  when it changes.
- ``node``: the ESPHome node name, published once at boot. Social Home finds
  it in Home Assistant's entity registry by its name and derives the
  ``esphome.<node>_connect_social_home`` action from it.
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
    }
)


async def to_code(config):
    display = await cg.get_variable(config[CONF_SOCIALHOME_DISPLAY_ID])
    if status := config.get(CONF_STATUS):
        cg.add(display.set_status_sensor(await text_sensor.new_text_sensor(status)))
    if node := config.get(CONF_NODE):
        cg.add(display.set_node_sensor(await text_sensor.new_text_sensor(node)))
