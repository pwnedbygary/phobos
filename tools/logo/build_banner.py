"""Builds the README's banner: the pixel-art moon beside a pixel-art "phobos", on the moon's own pixel grid.

    python3 -m venv .venv && .venv/bin/pip install pillow numpy
    .venv/bin/python tools/logo/build_banner.py [--scale 4]

moon-greenscreen.png is pixel art drawn 10.2 image pixels to the art pixel; the moon is keyed (build_icon.py's
key), then sampled once per art pixel, so it scales up by whole numbers without blurring. The word is Avenir Next
Bold Italic (a macOS system font) rasterized onto the same grid, in the old logo's purple: bands from dark at the
top of its x-height to light at its baseline, a dark outline and a drop shadow. The banner is transparent, and its
colors read on both light and dark pages.
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from build_icon import HERE, key_green

OUT = os.path.join(HERE, "..", "..", "docs", "images", "phobos-logo.png")
FONT = ("/System/Library/Fonts/Avenir Next.ttc", 1)  # Bold Italic
X_HEIGHT = 22  # art pixels
GAP = 7
PITCH, PHASE_X, PHASE_Y = 10.2, 5.3, 8.6  # moon-greenscreen.png's art grid
SUPER = 16  # the word is drawn this many times over, then averaged down to the grid

OUTLINE = (27, 16, 48)
SHADOW = (14, 8, 26)
RIM = (120, 88, 200)
BANDS = [(84, 46, 168), (102, 64, 190), (124, 88, 210), (150, 118, 228), (178, 152, 240), (206, 190, 250)]


def moon_sprite():
    """The keyed moon, one pixel per art pixel: each cell's middle, its median color, opaque or not."""
    keyed = np.asarray(key_green(Image.open(os.path.join(HERE, "moon-greenscreen.png")))).astype(np.int32)
    h, w, _ = keyed.shape
    cols, rows = int((w - PHASE_X) // PITCH), int((h - PHASE_Y) // PITCH)
    out = np.zeros((rows, cols, 4), np.uint8)
    for j in range(rows):
        for i in range(cols):
            x0, y0 = PHASE_X + i * PITCH, PHASE_Y + j * PITCH
            cell = keyed[round(y0 + 3):round(y0 + PITCH - 3), round(x0 + 3):round(x0 + PITCH - 3)].reshape(-1, 4)
            if cell.size == 0 or np.median(cell[:, 3]) < 128:
                continue
            out[j, i, :3] = np.median(cell[cell[:, 3] >= 128][:, :3], axis=0)
            out[j, i, 3] = 255
    sprite = Image.fromarray(out, "RGBA")
    return sprite.crop(sprite.getchannel("A").getbbox())


def word_mask():
    """"phobos" on the art grid, and where its x-height's top and baseline fall."""
    path, index = FONT
    probe = ImageFont.truetype(path, 1000, index=index)
    x_box = probe.getbbox("x")
    font = ImageFont.truetype(path, round(1000 * X_HEIGHT * SUPER / (x_box[3] - x_box[1])), index=index)
    left, top, right, bottom = font.getbbox("phobos")
    pad = SUPER * 8
    big = Image.new("L", (right - left + 2 * pad, bottom - top + 2 * pad), 0)
    ImageDraw.Draw(big).text((pad - left, pad - top), "phobos", font=font, fill=255)
    crop = big.getbbox()
    big = big.crop(crop)
    width, height = -(-big.width // SUPER) * SUPER, -(-big.height // SUPER) * SUPER
    canvas = Image.new("L", (width, height), 0)
    canvas.paste(big, (0, 0))
    cells = np.asarray(canvas, np.float32).reshape(height // SUPER, SUPER, width // SUPER, SUPER).mean(axis=(1, 3))
    o_box = font.getbbox("o")
    crop_top = crop[1] - (pad - top)  # the cropped picture's top, in the font's coordinates
    return cells >= 128, (o_box[1] - crop_top) / SUPER, (o_box[3] - crop_top) / SUPER


def dilate(mask):
    p = np.pad(mask, 1)
    out = p[1:-1, 1:-1].copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out |= p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx]
    return out


def word_sprite():
    """The word in bands of purple, with its outline, a light rim on its strokes' tops, and its shadow 2 px off."""
    mask, x_top, x_bottom = word_mask()
    pad, drop = 3, 2
    m = np.pad(mask, pad)
    h, w = m.shape
    outline = dilate(m)
    img = np.zeros((h + drop, w + drop, 4), np.uint8)
    img[drop:, drop:][outline] = SHADOW + (255,)
    img[:h, :w][outline] = OUTLINE + (255,)
    top, bottom = x_top + pad, x_bottom + pad
    for y in range(h):
        band = BANDS[min(len(BANDS) - 1, max(0, int((y - top) / max(1.0, bottom - top) * len(BANDS))))]
        img[y, :w][m[y]] = band + (255,)
    rim = m & ~np.roll(m, 1, axis=0)
    rim[0] = False
    for y, x in zip(*np.nonzero(rim)):
        if y <= top + 1:
            img[y, x, :3] = RIM
    return Image.fromarray(img, "RGBA"), pad + (x_top + x_bottom) / 2


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scale", type=int, default=4, help="screen pixels per art pixel")
    parser.add_argument("--out", default=OUT)
    args = parser.parse_args()

    moon = moon_sprite()
    word, x_middle = word_sprite()
    # the word's x-height centred on the moon
    word_top = round(moon.height / 2 - x_middle)
    moon_top = max(0, -word_top)
    word_top = max(0, word_top)
    art = Image.new("RGBA", (moon.width + GAP + word.width,
                             max(moon_top + moon.height, word_top + word.height)), (0, 0, 0, 0))
    art.alpha_composite(moon, (0, moon_top))
    art.alpha_composite(word, (moon.width + GAP, word_top))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    art.resize((art.width * args.scale, art.height * args.scale), Image.NEAREST).save(args.out, optimize=True)


if __name__ == "__main__":
    main()
