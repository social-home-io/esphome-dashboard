# SPDX-License-Identifier: MPL-2.0
"""Screens and file names of a screenshot run (pure: no I/O).

Names are deterministic so a later run can be laid over an earlier one:
``<kind>-<screen>-<locale>-<scheme>.<ext>``, e.g. ``panel-overview-de-dark.png``.
Screens the panel shows before it has a household (``setup``, ``pairing``)
speak the panel's built-in English whatever the run's locale, so they carry
no locale / scheme: ``panel-setup.png``.
"""

from __future__ import annotations

import re

# In the order a run takes them.
SCREENS = (
    "setup", "pairing",
    "overview", "messages", "notes", "map", "reader", "picture", "settings",
    "night", "lock", "pinpad", "wrong-pin", "last-try", "offline",
)
# Only the panel has these (the browser's tablet has no address, pairing or
# panel settings screen): no browser shot, no compare image.
PANEL_ONLY = frozenset({"setup", "pairing", "settings"})
# Shown before the panel knows the household's language and colours.
UNLOCALISED = frozenset({"setup", "pairing"})
SCHEMES = ("light", "dark", "auto")
KINDS = ("panel", "browser", "compare", "boxes", "offsets")
EXT = {"panel": "png", "browser": "png", "compare": "png", "boxes": "json", "offsets": "md"}

_LOCALE = re.compile(r"^[a-z]{2,3}(-[A-Za-z0-9]{2,8})*$")
_FILE = re.compile(r"^(?P<kind>[a-z]+)-(?P<rest>.+)\.(?P<ext>png|json|md)$")
_TAIL = re.compile(r"^-(?P<locale>.+)-(?P<scheme>light|dark|auto)$")


def split_list(value: str, allowed: tuple[str, ...] | None = None, what: str = "value") -> list[str]:
    """``"a,b, c"`` → ``["a", "b", "c"]`` (order kept, duplicates dropped);
    ValueError for an empty list or an item not in ``allowed``."""
    out: list[str] = []
    for item in (v.strip() for v in value.split(",")):
        if not item or item in out:
            continue
        if allowed is not None and item not in allowed:
            raise ValueError(f"unknown {what} {item!r} (choose from {', '.join(allowed)})")
        out.append(item)
    if not out:
        raise ValueError(f"no {what} given")
    return out


def check_locale(locale: str) -> str:
    if not _LOCALE.match(locale):
        raise ValueError(f"not a locale: {locale!r}")
    return locale


def ordered(screens: list[str]) -> list[str]:
    """The screens in the order a run takes them."""
    return [s for s in SCREENS if s in screens]


def shot_name(screen: str, locale: str, scheme: str) -> str:
    """``overview-de-dark``; ``setup`` for the unlocalised screens."""
    if screen not in SCREENS:
        raise ValueError(f"unknown screen {screen!r}")
    if screen in UNLOCALISED:
        return screen
    return f"{screen}-{check_locale(locale)}-{scheme}"


def file_name(kind: str, name: str) -> str:
    """``panel-overview-de-dark.png`` for kind ``panel``."""
    if kind not in EXT:
        raise ValueError(f"unknown kind {kind!r}")
    return f"{kind}-{name}.{EXT[kind]}"


def parse_file(filename: str) -> dict[str, str] | None:
    """The parts of a run's file name, or None when it isn't one.

    ``{"kind", "screen", "locale", "scheme", "name"}`` (locale / scheme empty
    for the unlocalised screens); ``name`` is what ``shot_name`` gave.
    """
    m = _FILE.match(filename)
    if not m or m.group("kind") not in EXT or EXT[m.group("kind")] != m.group("ext"):
        return None
    rest = m.group("rest")
    for screen in sorted(SCREENS, key=len, reverse=True):
        if rest == screen and screen in UNLOCALISED:
            return {"kind": m.group("kind"), "screen": screen, "locale": "", "scheme": "",
                    "name": screen}
        if rest.startswith(screen + "-") and screen not in UNLOCALISED:
            tail = _TAIL.match(rest[len(screen):])
            if tail and _LOCALE.match(tail.group("locale")):
                return {"kind": m.group("kind"), "screen": screen, "locale": tail.group("locale"),
                        "scheme": tail.group("scheme"), "name": rest}
    return None


def browser_scheme(scheme: str) -> str:
    """What the browser's prefers-color-scheme emulates for a theme mode.

    ``auto`` follows the device: the headless browser is told "light" (by
    day the household's Auto reads light on both screens)."""
    return "dark" if scheme == "dark" else "light"
