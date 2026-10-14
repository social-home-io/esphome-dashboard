# SPDX-License-Identifier: MPL-2.0
"""Social Home wall dashboard for ESPHome LVGL panels.

Finds Social Home on the network (mDNS, ``_socialhome-dash._tcp``; needs
``mdns:``), pairs with a household (a code typed in Admin › Dashboard, or
Connect from Home Assistant through the connect_social_home action, whose
claim is sealed to the panel's own X25519 key), polls
the display API (``/api/display/v1/``) and draws the household dashboard with
LVGL: Overview, Messages, Notes, Map, each person's page and the picture
view, the night screen and the privacy lock.
"""

import re

import esphome.codegen as cg
from esphome.components import font
from esphome.components.lvgl.defines import add_lv_use, get_lv_fonts_used
from esphome.components.lvgl.lvcode import LvglComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODEL, CONF_TIME_ID
from esphome.core import CORE

CODEOWNERS = ["@social-home-io"]
DEPENDENCIES = ["lvgl"]


def AUTO_LOAD():
    # The host build (SDL screenshots) borrows cpp-httplib from http_request.
    if CORE.is_host:
        return ["font", "json", "http_request"]
    return ["font", "json"]


CONF_LVGL_ID = "lvgl_id"
CONF_SERVER_URL = "server_url"
CONF_POLL_INTERVAL = "poll_interval"
CONF_SCREEN_WIDTH = "screen_width"
CONF_SCREEN_HEIGHT = "screen_height"
CONF_UI_SCALE = "ui_scale"
CONF_FONTS = "fonts"
CONF_BACKLIGHT_ID = "backlight_id"
CONF_SCREEN_OFF_TIMEOUT = "screen_off_timeout"
CONF_NIGHT_BRIGHTNESS = "night_brightness"
CONF_DISCOVERY_STUB = "discovery_stub"

# Font roles, in FontRole order (socialhome_display.h).
FONT_ROLES = (
    "small",
    "label",
    "body",
    "body_bold",
    "large",
    "title",
    "display",
    "clock",
    "night",
    "lock",
    "text",
    "text_bold",
    "lock_time",
    "text_large",
)

socialhome_display_ns = cg.esphome_ns.namespace("socialhome_display")
SocialHomeDisplay = socialhome_display_ns.class_("SocialHomeDisplay", cg.Component)

light_ns = cg.esphome_ns.namespace("light")
LightState = light_ns.class_("LightState", cg.EntityBase, cg.Component)
time_ns = cg.esphome_ns.namespace("time")
RealTimeClock = time_ns.class_("RealTimeClock", cg.PollingComponent)


def _model(value):
    # Sent in a JSON body and a header: keep it to a plain slug.
    value = cv.string_strict(value)
    if len(value) > 64 or not re.fullmatch(r"[A-Za-z0-9._ -]*", value):
        raise cv.Invalid("model: up to 64 letters, digits, '.', '_', '-' or spaces")
    return value


def _host_only(value):
    # A fixed "found on this network" list for the SDL screenshots: the host
    # build has no mDNS. Never on a panel, which browses the real network.
    if not CORE.is_host:
        raise cv.Invalid(f"{CONF_DISCOVERY_STUB} is for the host build only")
    return cv.string_strict(value)


def _lv_uses(config):
    # Widgets created at runtime from C++: LVGL only compiles what is declared.
    add_lv_use(
        "obj", "label", "image", "line", "textarea", "keyboard", "buttonmatrix", "flex"
    )
    # The on-screen keyboard's keys (backspace, enter, …) are LVGL symbols,
    # which only the built-in Montserrat carries: a fallback for that one font.
    get_lv_fonts_used().add("montserrat_24")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SocialHomeDisplay),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Optional(CONF_SERVER_URL, default=""): cv.string_strict,
            cv.Optional(CONF_POLL_INTERVAL, default="30s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(seconds=5)),
            ),
            cv.Required(CONF_SCREEN_WIDTH): cv.int_range(min=320, max=4096),
            cv.Required(CONF_SCREEN_HEIGHT): cv.int_range(min=240, max=4096),
            cv.Optional(CONF_UI_SCALE, default=1.0): cv.float_range(min=0.5, max=3.0),
            cv.Optional(CONF_MODEL, default="esphome-dashboard"): _model,
            cv.Required(CONF_FONTS): cv.Schema(
                {
                    cv.Required("body"): cv.use_id(font.Font),
                    **{
                        cv.Optional(role): cv.use_id(font.Font)
                        for role in FONT_ROLES
                        if role != "body"
                    },
                }
            ),
            cv.Optional(CONF_BACKLIGHT_ID): cv.use_id(LightState),
            cv.Optional(CONF_TIME_ID): cv.use_id(RealTimeClock),
            cv.Optional(
                CONF_SCREEN_OFF_TIMEOUT, default="10min"
            ): cv.positive_time_period_minutes,
            cv.Optional(CONF_NIGHT_BRIGHTNESS, default=0.15): cv.percentage,
            # "name|host|ipv4|port|k=v,k=v;…" (discovery.h)
            cv.Optional(CONF_DISCOVERY_STUB): _host_only,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _lv_uses,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    lvgl = await cg.get_variable(config[CONF_LVGL_ID])
    cg.add(var.set_lvgl(lvgl))
    cg.add(var.set_server_url(config[CONF_SERVER_URL]))
    cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))
    cg.add(var.set_screen_size(config[CONF_SCREEN_WIDTH], config[CONF_SCREEN_HEIGHT]))
    cg.add(var.set_ui_scale(config[CONF_UI_SCALE]))
    cg.add(var.set_model(config[CONF_MODEL]))
    cg.add(var.set_screen_off_minutes(config[CONF_SCREEN_OFF_TIMEOUT].total_minutes))
    cg.add(var.set_night_brightness(config[CONF_NIGHT_BRIGHTNESS]))
    for role, font_id in config[CONF_FONTS].items():
        index = FONT_ROLES.index(role)
        cg.add(
            var.set_font(
                cg.RawExpression(
                    f"static_cast<esphome::socialhome_display::FontRole>({index})"
                ),
                await cg.get_variable(font_id),
            )
        )
    if config.get(CONF_DISCOVERY_STUB):
        cg.add(var.set_discovery_stub(config[CONF_DISCOVERY_STUB]))
    if CONF_BACKLIGHT_ID in config:
        cg.add(var.set_backlight(await cg.get_variable(config[CONF_BACKLIGHT_ID])))
    if CONF_TIME_ID in config:
        cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    # PNG decoding for avatars and emoji (MIT; the same library ESPHome's
    # runtime_image uses).
    cg.add_library("pngle", "1.1.0")
    # The sealed connect claim (claim_seal.cpp) uses Mbed TLS: ESP-IDF's on a
    # panel, the system libmbedcrypto (libmbedtls-dev) on the host build.
    if CORE.is_host:
        cg.add_build_flag("-lmbedcrypto")
