# SPDX-License-Identifier: MPL-2.0
"""Social Home wall dashboard for ESPHome LVGL panels.

Bootstrap stage: shows a boot screen ("not connected yet") with the firmware
version. The server client and the dashboard screens land once the Social
Home display API is available.
"""

import esphome.codegen as cg
from esphome.components import font
from esphome.components.lvgl.defines import add_lv_use
from esphome.components.lvgl.lvcode import LvglComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@social-home-io"]
DEPENDENCIES = ["lvgl"]
AUTO_LOAD = ["font"]

CONF_LVGL_ID = "lvgl_id"
CONF_SERVER_URL = "server_url"
CONF_POLL_INTERVAL = "poll_interval"
CONF_SCREEN_WIDTH = "screen_width"
CONF_SCREEN_HEIGHT = "screen_height"
CONF_TITLE_FONT = "title_font"
CONF_BODY_FONT = "body_font"
CONF_SMALL_FONT = "small_font"

socialhome_display_ns = cg.esphome_ns.namespace("socialhome_display")
SocialHomeDisplay = socialhome_display_ns.class_("SocialHomeDisplay", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SocialHomeDisplay),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Optional(CONF_SERVER_URL, default=""): cv.Any(cv.url, cv.string_strict),
        cv.Optional(CONF_POLL_INTERVAL, default="30s"): cv.All(
            cv.positive_time_period_milliseconds,
            cv.Range(min=cv.TimePeriod(seconds=5)),
        ),
        cv.Required(CONF_SCREEN_WIDTH): cv.int_range(min=320, max=4096),
        cv.Required(CONF_SCREEN_HEIGHT): cv.int_range(min=240, max=4096),
        cv.Optional(CONF_TITLE_FONT): cv.use_id(font.Font),
        cv.Optional(CONF_BODY_FONT): cv.use_id(font.Font),
        cv.Optional(CONF_SMALL_FONT): cv.use_id(font.Font),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    lvgl = await cg.get_variable(config[CONF_LVGL_ID])
    cg.add(var.set_lvgl(lvgl))
    cg.add(var.set_server_url(config[CONF_SERVER_URL]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_screen_size(config[CONF_SCREEN_WIDTH], config[CONF_SCREEN_HEIGHT]))

    fonts = (
        (CONF_TITLE_FONT, var.set_title_font),
        (CONF_BODY_FONT, var.set_body_font),
        (CONF_SMALL_FONT, var.set_small_font),
    )
    for key, setter in fonts:
        if key in config:
            add_lv_use("font")
            cg.add(setter(await cg.get_variable(config[key])))
