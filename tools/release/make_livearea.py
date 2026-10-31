#!/usr/bin/env python3
"""Draws the public LiveArea art into tools/release/livearea.

    make_livearea.py

Everything here is drawn from scratch and uses nothing from the game, so it can ship in a
public VPK. The Vita wants 128x128, 840x500 and 280x158 PNGs with an 8-bit palette.
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'livearea')
NIGHT, DUSK, EMBER = (9, 13, 28), (78, 34, 66), (244, 122, 44)
LINE, GOLD = (70, 214, 196), (250, 214, 120)
FONTS = ('C:/Windows/Fonts/arialbd.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')


def font(size):
    for f in FONTS:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    raise SystemExit('no bold font found')


def mix(a, b, t):
    return tuple(int(x + (y - x) * t) for x, y in zip(a, b))


def height(x, z):
    return (math.sin(x * 1.7 + z * 0.6) + 0.6 * math.sin(x * 3.1 - z * 1.3) + 0.3 * math.sin(x * 6.3 + z * 2.2)) * 0.5


def scene(w, h, ship=True):
    """A dusk sky over a wireframe ridge, drawn at twice the size and reduced."""
    W, H = w * 2, h * 2
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    horizon = H * 0.58
    for y in range(H):
        t = min(1, y / horizon)
        d.line([(0, y), (W, y)], fill=mix(NIGHT, DUSK, t ** 1.6) if t < 1 else NIGHT)
    d.ellipse([W * 0.68, horizon - H * 0.2, W * 0.68 + H * 0.3, horizon + H * 0.1], fill=EMBER)
    d.rectangle([0, horizon, W, H], fill=NIGHT)

    def point(col, row):
        z = 1 + row * 0.22
        x = (col - 14) * 0.34
        y = 1.1 - height(x, row * 0.42) * 0.5 * min(1, row / 3)
        return W / 2 + x / z * W * 0.5, horizon + y / z * H * 0.34
    for row in range(26, -1, -1):
        c = mix(LINE, NIGHT, row / 30)
        wide = max(1, int(W / 420))
        d.line([point(i, row) for i in range(29)], fill=c, width=wide)
        if row < 26:
            for i in range(29):
                d.line([point(i, row), point(i, row + 1)], fill=c, width=wide)
    if ship:
        s, cx, cy = H * 0.15, W * 0.42, H * 0.34
        pts = [(0, -1), (0.16, 0.1), (1.25, 0.72), (0.2, 0.52), (0, 0.72), (-0.2, 0.52), (-1.25, 0.72), (-0.16, 0.1)]
        d.polygon([(cx + x * s, cy + y * s) for x, y in pts], fill=(20, 26, 46), outline=GOLD)
        d.ellipse([cx - s * .07, cy + s * .66, cx + s * .07, cy + s * .82], fill=EMBER)
    return im.resize((w, h), Image.LANCZOS)


def title(im, text, size, y):
    d = ImageDraw.Draw(im)
    f = font(size)
    x = (im.width - d.textlength(text, font=f)) / 2
    d.text((x + 2, y + 2), text, font=f, fill=(0, 0, 0))
    d.text((x, y), text, font=f, fill=GOLD)


def save(im, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.quantize(256, method=Image.MEDIANCUT, dither=Image.NONE).save(path, optimize=True)


def main():
    icon = scene(128, 128)
    title(icon, 'FURY3', 30, 92)
    save(icon, os.path.join(OUT, 'icon0.png'))
    save(scene(840, 500), os.path.join(OUT, 'livearea', 'contents', 'bg.png'))
    gate = scene(280, 158, ship=False)
    title(gate, 'FURY3', 42, 30)
    save(gate, os.path.join(OUT, 'livearea', 'contents', 'startup.png'))
    with open(os.path.join(OUT, 'livearea', 'contents', 'template.xml'), 'w', newline='\n') as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n\n'
                '<livearea style="a1" format-ver="01.00" content-rev="2">\n'
                '  <livearea-background>\n    <image>bg.png</image>\n  </livearea-background>\n\n'
                '  <gate>\n    <startup-image>startup.png</startup-image>\n  </gate>\n</livearea>\n')


if __name__ == '__main__':
    main()
