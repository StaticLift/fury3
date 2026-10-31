"""Builds the Vita LiveArea art (sce_sys/) from the game's title screen.

    python3 make_livearea.py <title.png> <out sce_sys dir>

The title screen is the 640x400 Fury3 splash (docs/screenshots/1-menu.png).
LiveArea images must be 8-bit indexed PNGs.
"""
import os
import sys
from PIL import Image, ImageEnhance, ImageFilter

src, out = sys.argv[1], sys.argv[2]
title = Image.open(src).convert("RGB")
os.makedirs(os.path.join(out, "livearea", "contents"), exist_ok=True)


def save8(img, path):
    img.convert("RGB").quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(path, optimize=True)


# Bubble: the whole FURY3 logo with the planets, feathered onto a blurred
# square of the starburst (the logo is wider than the art above the copyright).
logo = title.crop((136, 28, 552, 336)).resize((128, 95), Image.LANCZOS)
icon = title.crop((140, 0, 540, 400)).resize((128, 128), Image.LANCZOS)
icon = ImageEnhance.Brightness(icon.filter(ImageFilter.GaussianBlur(4))).enhance(0.6)
mask = Image.new("L", logo.size, 255)
for y in range(8):
    a = int(255 * (y + 1) / 9)
    for x in range(logo.size[0]):
        mask.putpixel((x, y), a)
        mask.putpixel((x, logo.size[1] - 1 - y), a)
icon.paste(ImageEnhance.Sharpness(logo).enhance(1.4), (0, 17), mask)
save8(icon, os.path.join(out, "icon0.png"))

# Start gate (280x158): the whole logo with the planets.
gate = title.crop((52, 30, 588, 332)).resize((280, 158), Image.LANCZOS)
save8(ImageEnhance.Sharpness(gate).enhance(1.3), os.path.join(out, "livearea", "contents", "startup.png"))

# Background (840x500): the splash filling the screen, softened and dimmed so the gate stands out.
bg = title.crop((38, 0, 601, 335)).resize((840, 500), Image.LANCZOS)
bg = bg.filter(ImageFilter.GaussianBlur(3))
bg = ImageEnhance.Brightness(bg).enhance(0.7)
save8(bg, os.path.join(out, "livearea", "contents", "bg.png"))

with open(os.path.join(out, "livearea", "contents", "template.xml"), "w", newline="\n") as f:
    f.write("""<?xml version="1.0" encoding="utf-8"?>

<livearea style="a1" format-ver="01.00" content-rev="2">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>

  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
""")
