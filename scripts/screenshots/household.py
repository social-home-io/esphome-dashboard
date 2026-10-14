# SPDX-License-Identifier: MPL-2.0
"""A scratch Social Home with the demo household, for panel screenshots.

Runs under **Social Home's** Python (its dependencies, ``PYTHONPATH`` at the
checkout) — ``run.py`` starts it; it is not imported by anything here.

Boots Social Home in-process (standalone, a temporary data dir), seeds the
family the release sign-off uses — Anna, Pascal, Mia (11, reads) and Leo (6,
picture view); home / school / work / Gran's zones and where everyone is; a
school timetable each for Mia and Leo (a cancelled and a moved lesson);
calendars with timed and all-day items; chores; a shopping list; sticky
notes; household chat with @mentions and an @here — then turns the wall
dashboard on (with a tablet password and a PIN) and serves a small control
API for the screenshot run:

  POST /pair?code=XXXXXX&name=N   the admin pairs a panel code
  POST /night?on=1|0              night window around now / back to 22:00
  POST /locale?l=en               the dashboard language
  POST /theme?mode=light|dark|auto
  POST /unlock                    the admin lifts every panel's privacy lock
  GET  /cookie                    a fresh tablet session cookie (sh_dash)
  GET  /info                      ports, time zone, local time, user ids

Times are relative to now in a household time zone picked so the real clock
reads about 16:05 on a weekday: lessons are under way, the NEXT card has
something coming, the map is by day. Two runs at different times of day
therefore show the same board, give or take the minute.

Usage: python household.py MAIN_PORT DASHBOARD_PORT CONTROL_PORT DATA_DIR [TIME_ZONE]
"""

from __future__ import annotations

import asyncio
import io
import json
import os
import sys
from datetime import UTC, datetime, timedelta
from types import MappingProxyType
from zoneinfo import ZoneInfo

import aiohttp
from aiohttp import web
from PIL import Image, ImageDraw, ImageFont

from socialhome.app import create_app
from socialhome.app_keys import db_key
from socialhome.auth import sha256_token_hash
from socialhome.config import Config
from socialhome.crypto import derive_user_id

PASSWORD = "tablet-password-1"
PIN = "2468"
EMOJI_FONT = "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"

# Weekday zones spread around the clock; one of them reads ~16:05 now.
ZONES = [
    "Pacific/Honolulu", "America/Anchorage", "America/Los_Angeles", "America/Denver",
    "America/Chicago", "America/New_York", "America/Halifax", "America/St_Johns",
    "America/Sao_Paulo", "Atlantic/Cape_Verde", "Atlantic/Reykjavik", "Europe/London",
    "Europe/Berlin", "Europe/Athens", "Europe/Istanbul", "Asia/Tehran", "Asia/Dubai",
    "Asia/Kabul", "Asia/Karachi", "Asia/Kolkata", "Asia/Kathmandu", "Asia/Dhaka",
    "Asia/Yangon", "Asia/Bangkok", "Asia/Singapore", "Australia/Eucla", "Asia/Tokyo",
    "Australia/Darwin", "Australia/Brisbane", "Australia/Adelaide", "Australia/Sydney",
    "Pacific/Noumea", "Pacific/Auckland", "Pacific/Chatham", "America/Caracas",
    "America/Bogota", "America/Mexico_City",
]


def pick_zone(now_utc: datetime, target_min: int = 16 * 60) -> str:
    """The weekday zone whose local time is closest to just after target."""
    best: tuple[int, str] | None = None
    for z in ZONES:
        loc = now_utc.astimezone(ZoneInfo(z))
        if loc.weekday() > 4:
            continue
        d = abs(loc.hour * 60 + loc.minute - target_min - 4)
        if best is None or d < best[0]:
            best = (d, z)
    assert best is not None
    return best[1]


# argv[5]: a zone to use instead (run.py --time-zone, to match an earlier run).
TZ = sys.argv[5] if len(sys.argv) > 5 and sys.argv[5] else pick_zone(datetime.now(UTC))
ZONE = ZoneInfo(TZ)
NOW = datetime.now(ZONE)
# Anchor rounded down to 5 min, so "now" lessons stay current all run long.
ANCHOR = NOW.replace(second=0, microsecond=0) - timedelta(minutes=NOW.minute % 5)


def at(hh: int, mm: int = 0, day: int = 0) -> datetime:
    return NOW.replace(hour=hh, minute=mm, second=0, microsecond=0) + timedelta(days=day)


def rel(minutes: int) -> datetime:
    return ANCHOR + timedelta(minutes=minutes)


def ago(minutes: int) -> datetime:
    return NOW - timedelta(minutes=minutes)


def hm(d: datetime) -> str:
    return d.strftime("%H:%M")


def utc_stamp(dt: datetime) -> str:
    return dt.astimezone(UTC).strftime("%Y-%m-%d %H:%M:%S")


# username: (display name, calendar colour, avatar emoji, avatar background)
PEOPLE = {
    "anna": ("Anna", "#D2552F", "👩🏼", "#F6D6C8"),
    "pascal": ("Pascal", "#357BBB", "👨🏼", "#CFE0F2"),
    "mia": ("Mia", "#3F7D57", "👧🏼", "#D3EBDB"),
    "leo": ("Leo", "#C8922E", "👦🏼", "#F6E5BF"),
}

# who, when, text, type — yesterday evening and today, all < 24 h old.
MESSAGES = [
    ("pascal", at(18, 40, -1), "Dinner at 7:30, can someone start the rice? 🍚", "text"),
    ("mia", at(19, 55, -1), "Can we watch a movie after dinner? 🎬", "text"),
    ("anna", at(21, 5, -1), "Leo's swimming bag is packed and waiting by the front door.", "text"),
    ("pascal", at(7, 40), "Bins are out. Have a good day everyone! ☀️", "text"),
    ("leo", at(12, 50), "", "audio"),
    ("pascal", ago(48), "", "image"),
    ("anna", ago(36), "@mia Don't forget your piano book, it's on the kitchen table 🎹", "text"),
    ("mia", ago(24), "Music is in room 12 today, they're painting room 3 🎨", "text"),
    ("pascal", ago(11), "@leo Shoes on! 👟", "text"),
    ("anna", ago(3), "@here Leaving work now, I'll pick up a cake for Gran 🍰", "text"),
]

STICKIES = [
    ("Plumber Tue 9am, Anna is home 🔧", "#FFF9B1"),
    ("Band practice Thursday 🎸", "#8855EE"),
    ("Wifi password is on the fridge 📶", "#B3D4FF"),
    ("Leo's swim bag: towel, goggles, shampoo!", "#B3FFB3"),
    ("Mia: library books back by Friday 📚", "#E8B3FF"),
    ("Gran's birthday Saturday: card & flowers 🎂", "#FFB3B3"),
    ("Bins out Wednesday night ♻️", "#FFD4B3"),
]
SHOPPING = ["Milk", "Bread", "Eggs", "Apples", "Pasta", "Tea", "Butter", "Cheese"]

# Zürich, Wiedikon. name, icon, lat, lon, radius (m), is_home
ZONES_MAP = [
    ("Home", "🏠", 47.3672, 8.5203, 100, True),
    ("School", "🏫", 47.3749, 8.5113, 150, False),
    ("Work", "💼", 47.3779, 8.5402, 150, False),
    ("Gran's", "👵", 47.3720, 8.4990, 100, False),
]
# username: (lat, lon, minutes since the location last changed). Pascal and
# Leo are home, Mia is at school, Anna just left work (her @here message).
WHERE = {
    "pascal": (47.3673, 8.5201, 48),
    "leo": (47.3671, 8.5205, 36),
    "mia": (47.3750, 8.5115, 27),
    "anna": (47.3712, 8.5330, 2),
}


def avatar_png(emoji: str, bg: str, initial: str) -> bytes:
    img = Image.new("RGB", (512, 512), bg)
    try:
        font = ImageFont.truetype(EMOJI_FONT, 109)
        glyph = Image.new("RGBA", (160, 160), (0, 0, 0, 0))
        ImageDraw.Draw(glyph).text((80, 84), emoji, font=font, embedded_color=True, anchor="mm")
        glyph = glyph.crop(glyph.getbbox()).resize((300, 300), Image.LANCZOS)
        img.paste(glyph, ((512 - 300) // 2, (512 - 300) // 2 + 20), glyph)
    except OSError:  # no Noto Color Emoji: a letter will do
        ImageDraw.Draw(img).text((256, 256), initial, fill="black", anchor="mm",
                                 font=ImageFont.load_default(size=240))
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def auth(tok: str) -> dict[str, str]:
    return {"Authorization": f"Bearer {tok}"}


async def ok(r: aiohttp.ClientResponse, *codes: int):
    body = await r.text()
    if r.status not in (codes or (200, 201, 204)):
        raise RuntimeError(f"{r.method} {r.url}: {r.status} {body[:800]}")
    return json.loads(body) if body and r.content_type == "application/json" else None


class Household:
    def __init__(self, main: int, dash: int, data_dir: str) -> None:
        self.main, self.dash, self.data_dir = main, dash, data_dir
        self.base = f"http://127.0.0.1:{main}"
        self.dbase = f"http://127.0.0.1:{dash}"
        self.users: dict[str, str] = {}

    async def boot(self) -> None:
        cfg = Config(
            data_dir=self.data_dir,
            db_path=f"{self.data_dir}/sh.db",
            media_path=f"{self.data_dir}/media",
            apps_path=f"{self.data_dir}/apps",
            mode="standalone",
            log_level="WARNING",
            listen_host="127.0.0.1",
            listen_port=self.main,
            platform_options=MappingProxyType(
                {"standalone": MappingProxyType({"external_url": self.base})}),
        )
        app = create_app(cfg)
        self.runner = web.AppRunner(app)
        await self.runner.setup()
        await web.TCPSite(self.runner, "127.0.0.1", self.main).start()
        self.db = db = app[db_key]
        # Anna is the admin; setup is done (no /setup round trip).
        row = await db.fetchone(
            "SELECT identity_public_key FROM instance_identity WHERE id='self'")
        uid = derive_user_id(bytes.fromhex(row["identity_public_key"]), "anna")
        await db.enqueue(
            "INSERT OR REPLACE INTO users(username, user_id, display_name, is_admin)"
            " VALUES(?,?,?,1)", ("anna", uid, "Anna"))
        await db.enqueue(
            "INSERT INTO api_tokens(token_id, user_id, label, token_hash) VALUES(?,?,?,?)",
            ("t-anna", uid, "t", sha256_token_hash("anna-tok")))
        await db.enqueue(
            "INSERT OR REPLACE INTO instance_config(key, value) VALUES('setup_complete', '1')")
        self.users["anna"] = uid
        self.s = aiohttp.ClientSession()

    async def api(self, method: str, path: str, who: str = "anna", codes=(), **kw):
        r = await self.s.request(method, self.base + path, headers=auth(f"{who}-tok"), **kw)
        return await ok(r, *codes)

    async def seed(self) -> None:
        db = self.db
        await self.api("PUT", "/api/household/preferences", json={
            "tz": TZ, "household_name": "The Vizeli Household",
            "toggles": {"feat_timetable": True}})
        for name, (disp, *_r) in PEOPLE.items():
            if name == "anna":
                continue
            body = await self.api("POST", "/api/admin/users", codes=(201,), json={
                "username": name, "password": "password123", "display_name": disp})
            self.users[name] = body["user_id"]
            await db.enqueue(
                "INSERT INTO api_tokens(token_id, user_id, label, token_hash) VALUES(?,?,?,?)",
                (f"t-{name}", self.users[name], "t", sha256_token_hash(f"{name}-tok")))
        for name, (disp, _c, emoji, bg) in PEOPLE.items():
            form = aiohttp.FormData()
            form.add_field("file", avatar_png(emoji, bg, disp[0]), filename=f"{name}.png",
                           content_type="image/png")
            await self.api("POST", "/api/me/picture", who=name, data=form)
        await self.seed_timetables()
        await self.seed_calendars()
        await self.seed_map()
        await self.seed_lists()
        await self.seed_chat()

    async def add_timetable(self, who, name, color, entries, overrides) -> None:
        tt = (await self.api("POST", "/api/timetables", codes=(201,), json={
            "name": name, "template": "empty", "days": [0, 1, 2, 3, 4],
            "assignees": [self.users[who]], "color": color}))["timetable"]
        for e in entries:
            tt = (await self.api("POST", f"/api/timetables/{tt['id']}/entries",
                                 json={"version": tt["version"], **e}))["timetable"]
        for title, ov in overrides:
            eid = next(e["id"] for e in tt["entries"]
                       if e.get("title") == title and e["weekday"] == NOW.weekday())
            tt = (await self.api("POST", f"/api/timetables/{tt['id']}/overrides", json={
                "version": tt["version"], "date": NOW.date().isoformat(),
                "entry_id": eid, **ov}))["timetable"]

    async def seed_timetables(self) -> None:
        def lesson(wd, start, end, title, icon, room=None):
            d = dict(weekday=wd, start=start, end=end, kind="lesson", title=title, icon=icon)
            if room:
                d["room"] = room
            return d

        today, tomorrow = NOW.weekday(), (NOW + timedelta(days=1)).weekday()
        days = [today] if tomorrow > 4 else [today, tomorrow]
        mia, leo = [], []
        for d in days:
            mia += [
                lesson(d, "08:15", "09:00", "English", "📖", "14"),
                lesson(d, "09:05", "09:50", "Maths", "🔢", "14"),
                lesson(d, "10:10", "11:40", "PE", "⚽"),
                lesson(d, "13:00", "13:45", "Science", "🔬", "21"),
                lesson(d, hm(rel(-75)), hm(rel(-30)), "Art", "🎨", "9"),
                lesson(d, hm(rel(-25)), hm(rel(20)), "Music", "🎵", "3"),
            ]
            leo += [
                lesson(d, "08:00", "08:45", "Reading", "📖"),
                lesson(d, "08:50", "09:35", "Maths", "🔢"),
                dict(weekday=d, start="09:35", end="10:00", kind="break", label="Snack"),
                lesson(d, "10:00", "10:45", "Music", "🎵"),
                lesson(d, "10:50", "11:35", "Gym", "🤸"),
                lesson(d, "11:40", "12:15", "Painting", "🎨"),
            ]
        await self.add_timetable("mia", "Class 6b", "moss", mia,
                                 [("PE", {"kind": "cancel"}),
                                  ("Music", {"kind": "replace", "room": "12"})])
        await self.add_timetable("leo", "Class 1a", "amber", leo,
                                 [("Gym", {"kind": "cancel"})])

    async def seed_calendars(self) -> None:
        cals = {}
        for name, (disp, color, *_r) in PEOPLE.items():
            cal = await self.api("POST", "/api/calendars", who=name, codes=(201,),
                                 json={"name": disp, "color": color})
            cals[name] = cal["id"]
            await self.db.enqueue("UPDATE calendars SET color=? WHERE owner_username=?",
                                  (color, name))
        events = [
            ("anna", "Work", at(8, 30), rel(-5)),
            ("pascal", "Work", at(9, 0), at(13, 30)),
            ("leo", "Playground", at(14, 0), at(15, 15)),
            ("mia", "Piano", rel(35), rel(80)),
            ("leo", "Swimming", at(17, 0), at(18, 0)),
            ("pascal", "Dentist", at(17, 15), at(18, 0)),
            ("anna", "Yoga", at(19, 0, 1), at(20, 0, 1)),
            ("anna", "Pick up cake", rel(45), rel(60)),
            ("anna", "Parents' evening", at(18, 0, 1), at(18, 45, 1)),
            ("leo", "Football", at(16, 0, 1), at(17, 0, 1)),
            ("pascal", "Dentist with Mia", at(14, 30, 2), at(15, 15, 2)),
        ]
        for who, title, start, end in events:
            await self.api("POST", f"/api/calendars/{cals[who]}/events", who=who, codes=(201,),
                           json={"summary": title, "start": start.isoformat(),
                                 "end": end.isoformat()})
        for name in PEOPLE:
            await self.api("POST", f"/api/calendars/{cals[name]}/events", codes=(201,), json={
                "summary": "Dinner with Gran", "start": at(18, 30).isoformat(),
                "end": at(19, 45).isoformat(), "client_event_uuid": "dinner-gran"})
        # All-day items: chips above the lane's track.
        for who, title in [("leo", "Sports day"), ("mia", "Project week")]:
            await self.api("POST", f"/api/calendars/{cals[who]}/events", who=who, codes=(201,),
                           json={"summary": title, "start": at(0, 0).isoformat(),
                                 "end": at(0, 0, 1).isoformat(), "all_day": True})

    async def seed_map(self) -> None:
        for name, icon, lat, lon, radius, home in ZONES_MAP:
            await self.api("POST", "/api/household/zones", codes=(201,), json={
                "name": name, "icon": icon, "latitude": lat, "longitude": lon,
                "radius_m": radius, "is_home": home})
        for name, (lat, lon, mins) in WHERE.items():
            await self.api("POST", "/api/presence/location", who=name, json={
                "username": name, "latitude": lat, "longitude": lon, "accuracy_m": 12})
            await self.db.enqueue("UPDATE presence SET updated_at=? WHERE username=?",
                                  (ago(mins).astimezone(UTC).isoformat(), name))

    async def seed_lists(self) -> None:
        lst = await self.api("POST", "/api/tasks/lists", codes=(201,), json={"name": "Chores"})
        today = NOW.date()
        for title, who, due, done in [
            ("Take out recycling", "pascal", today - timedelta(days=1), False),
            ("Empty dishwasher", "mia", today, False),
            ("Feed the cat", "leo", today, False),
            ("Water the plants", "leo", today, True),
            ("Laundry", "anna", today, True),
            ("Book Leo's haircut", "anna", today, False),
            ("Buy Gran's birthday card", "anna", today + timedelta(days=1), False),
        ]:
            body = {"title": title, "due_date": due.isoformat(),
                    "assignees": [self.users[who]]}
            if done:
                body["status"] = "done"
            await self.api("POST", f"/api/tasks/lists/{lst['id']}/tasks", codes=(201,), json=body)
        for item in SHOPPING:
            await self.api("POST", "/api/shopping", codes=(201,), json={"text": item})
        for i, (text, color) in enumerate(STICKIES):
            body = await self.api("POST", "/api/stickies", codes=(201,),
                                  json={"content": text, "color": color})
            await self.db.enqueue("UPDATE stickies SET created_at=? WHERE id=?",
                                  (utc_stamp(NOW - timedelta(hours=10 - i)), body["id"]))

    async def seed_chat(self) -> None:
        chat = await self.api("GET", "/api/household/chat")
        cid = chat["conversation_id"]
        for who, when, text, mtype in MESSAGES:
            body = await self.api("POST", f"/api/conversations/{cid}/messages", who=who,
                                  codes=(201,), json={"content": text or "x"})
            await self.db.enqueue(
                "UPDATE conversation_messages SET created_at=?, type=?, content=? WHERE id=?",
                (utc_stamp(when), mtype, text, body["id"]))

    async def enable_dashboard(self) -> None:
        await self.api("PUT", "/api/admin/dashboard/password", json={"password": PASSWORD})
        await self.api("PUT", "/api/admin/dashboard/pin", json={"pin": PIN})
        await self.api("PUT", "/api/admin/dashboard", json={
            "enabled": True, "port": self.dash, "show_household_messages": True,
            "idle_return_minutes": 3, "address": "http://kitchen-tablet.local:8100",
            "night_start": "22:00", "night_end": "06:30",
            "people": {self.users["leo"]: {"picture": True},
                       self.users["mia"]: {"picture": False}}})

    # ---- control API -------------------------------------------------

    async def _passthrough(self, method: str, path: str, **kw) -> web.Response:
        r = await self.s.request(method, self.base + path, headers=auth("anna-tok"), **kw)
        return web.Response(status=r.status, text=await r.text())

    async def h_pair(self, req: web.Request) -> web.Response:
        return await self._passthrough("POST", "/api/admin/dashboard/displays", json={
            "code": req.query["code"], "name": req.query.get("name", "Kitchen panel")})

    async def h_night(self, req: web.Request) -> web.Response:
        if req.query.get("on") == "1":
            body = {"night_start": hm(NOW - timedelta(hours=1)),
                    "night_end": hm(NOW + timedelta(hours=6))}
        else:
            body = {"night_start": "22:00", "night_end": "06:30"}
        return await self._passthrough("PUT", "/api/admin/dashboard", json=body)

    async def h_locale(self, req: web.Request) -> web.Response:
        return await self._passthrough("PUT", "/api/admin/dashboard",
                                       json={"locale": req.query["l"]})

    async def h_theme(self, req: web.Request) -> web.Response:
        return await self._passthrough("PUT", "/api/theme", json={"mode": req.query["mode"]})

    async def h_unlock(self, req: web.Request) -> web.Response:
        settings = await self.api("GET", "/api/admin/dashboard")
        out = []
        for d in settings["displays"]:
            r = await self.s.patch(f"{self.base}/api/admin/dashboard/displays/{d['id']}",
                                   json={"locked": False}, headers=auth("anna-tok"))
            out.append(r.status)
        return web.json_response(out)

    async def h_cookie(self, req: web.Request) -> web.Response:
        r = await self.s.post(f"{self.dbase}/login", data={"password": PASSWORD, "keep": "1"},
                              headers={"Origin": self.dbase}, allow_redirects=False)
        return web.Response(text=r.cookies["sh_dash"].value)

    async def h_info(self, req: web.Request) -> web.Response:
        return web.json_response({
            "main": self.base, "dashboard": self.dbase, "tz": TZ,
            "local_now": datetime.now(ZONE).isoformat(), "users": self.users, "pin": PIN})


async def main() -> None:
    main_port, dash_port, ctl_port = (int(v) for v in sys.argv[1:4])
    data_dir = sys.argv[4]
    os.makedirs(data_dir, exist_ok=True)
    h = Household(main_port, dash_port, data_dir)
    await h.boot()
    await h.seed()
    await h.enable_dashboard()
    ctl = web.Application()
    ctl.router.add_post("/pair", h.h_pair)
    ctl.router.add_post("/night", h.h_night)
    ctl.router.add_post("/locale", h.h_locale)
    ctl.router.add_post("/theme", h.h_theme)
    ctl.router.add_post("/unlock", h.h_unlock)
    ctl.router.add_get("/cookie", h.h_cookie)
    ctl.router.add_get("/info", h.h_info)
    crun = web.AppRunner(ctl)
    await crun.setup()
    await web.TCPSite(crun, "127.0.0.1", ctl_port).start()
    print("READY", h.base, h.dbase, "tz", TZ, "local", NOW.strftime("%a %H:%M"), flush=True)
    await asyncio.Event().wait()


if __name__ == "__main__":
    asyncio.run(main())
