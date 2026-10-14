# SPDX-License-Identifier: MPL-2.0
"""The Pillow part: BMP → PNG, side-by-side compare sheets, diff images."""

from __future__ import annotations

import os

from PIL import Image, ImageChops, ImageDraw, ImageFont

GAP, HEAD = 24, 56
_FONTS = ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
          "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf")


def _font(size: int = 26):
    for path in _FONTS:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def bmp_to_png(src: str, dst: str) -> None:
    with Image.open(src) as im:
        im.convert("RGB").save(dst, optimize=False)


def side_by_side(left: str, right: str, out: str, left_title: str, right_title: str) -> None:
    """``left`` and ``right`` next to each other under a title bar each."""
    with Image.open(left) as a, Image.open(right) as b:
        a, b = a.convert("RGB"), b.convert("RGB")
        sheet = Image.new("RGB", (a.width + GAP + b.width, HEAD + max(a.height, b.height)),
                          (40, 40, 40))
        d = ImageDraw.Draw(sheet)
        f = _font()
        d.text((16, 14), left_title, font=f, fill="white")
        d.text((a.width + GAP + 16, 14), right_title, font=f, fill="white")
        sheet.paste(a, (0, HEAD))
        sheet.paste(b, (a.width + GAP, HEAD))
        sheet.save(out)


def histogram(old: str, new: str) -> tuple[list[int], tuple[int, int], tuple[int, int]]:
    """Histogram of the per-pixel largest channel difference, and both sizes.
    Shots of different sizes are compared over their common area."""
    with Image.open(old) as a, Image.open(new) as b:
        sa, sb = a.size, b.size
        w, h = min(sa[0], sb[0]), min(sa[1], sb[1])
        a = a.convert("RGB").crop((0, 0, w, h))
        b = b.convert("RGB").crop((0, 0, w, h))
        diff = ImageChops.difference(a, b)
        r, g, bl = diff.split()
        return ImageChops.lighter(ImageChops.lighter(r, g), bl).histogram(), sa, sb


def diff_image(old: str, new: str, out: str, title: str, threshold: int = 16) -> None:
    """old | new | the new shot dimmed with changed pixels in magenta."""
    with Image.open(old) as a, Image.open(new) as b:
        a, b = a.convert("RGB"), b.convert("RGB")
        w, h = min(a.width, b.width), min(a.height, b.height)
        diff = ImageChops.difference(a.crop((0, 0, w, h)), b.crop((0, 0, w, h)))
        r, g, bl = diff.split()
        mask = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(
            lambda v: 255 if v > threshold else 0)
        marked = Image.blend(b.crop((0, 0, w, h)), Image.new("RGB", (w, h), (255, 255, 255)), 0.6)
        marked.paste(Image.new("RGB", (w, h), (230, 0, 160)), (0, 0), mask)
        sheet = Image.new("RGB", (a.width + b.width + w + 2 * GAP, HEAD + max(a.height, b.height)),
                          (40, 40, 40))
        d = ImageDraw.Draw(sheet)
        f = _font()
        d.text((16, 14), f"Before  ·  {title}", font=f, fill="white")
        d.text((a.width + GAP + 16, 14), "After", font=f, fill="white")
        d.text((a.width + b.width + 2 * GAP + 16, 14), "Changed pixels", font=f, fill="white")
        sheet.paste(a, (0, HEAD))
        sheet.paste(b, (a.width + GAP, HEAD))
        sheet.paste(marked, (a.width + b.width + 2 * GAP, HEAD))
        sheet.save(out)
