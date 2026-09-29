"""Builds Phobos's launcher icon and in-app logo from the two source images beside this script.

    python3 -m venv .venv && .venv/bin/pip install pillow
    .venv/bin/python tools/logo/build_icon.py [--preview preview.png]

moon-greenscreen.png is the moon on a flat green screen; background-mars.png is Mars and the stars.
The launcher icon is adaptive: background-mars fills the 108 dp background layer, and the keyed
moon is the foreground, scaled so it stays inside the 66 dp circle every launcher mask keeps.
ic_launcher is the legacy icon, and drawable-nodpi/phobos_logo the in-app logo; both are the
composed icon as a circular launcher shows it (the central 72 dp, masked to a circle).
"""
import argparse
import os

from PIL import Image, ImageChops, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "..", "android", "app", "src", "main", "res")
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}
LAYER_DP = 108
VISIBLE_DP = 72
LEGACY_DP = 48
SAFE_RADIUS = 33 / LAYER_DP * 0.98
MASTER = 1024


def key_green(src):
    """Alpha from how far green dominates red and blue, with the green spill removed from edges."""
    rgb = src.convert("RGB")
    out = Image.new("RGBA", rgb.size)
    px_in, px_out = rgb.load(), out.load()
    lo, hi = 40, 110
    for y in range(rgb.height):
        for x in range(rgb.width):
            r, g, b = px_in[x, y]
            dominance = g - max(r, b)
            a = 255 if dominance <= lo else 0 if dominance >= hi else round(255 * (hi - dominance) / (hi - lo))
            if a < 255:
                g = min(g, max(r, b))
            px_out[x, y] = (r, g, b, a)
    return out


def tight(image):
    return image.crop(image.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox())


def place_in_safe_zone(moon, canvas_px):
    """Centers the moon and scales it so its farthest opaque pixel lies on the safe circle."""
    alpha = moon.getchannel("A").load()
    cx, cy = moon.width / 2, moon.height / 2
    far = max(((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
              for y in range(0, moon.height, 2) for x in range(0, moon.width, 2) if alpha[x, y] > 32)
    scale = SAFE_RADIUS * canvas_px / far
    size = (round(moon.width * scale), round(moon.height * scale))
    layer = Image.new("RGBA", (canvas_px, canvas_px), (0, 0, 0, 0))
    layer.alpha_composite(moon.resize(size, Image.LANCZOS), ((canvas_px - size[0]) // 2, (canvas_px - size[1]) // 2))
    return layer


def circle_masked(image, size):
    """The central 72 dp of a 108 dp layer, masked to a circle, as a circular launcher draws it."""
    inset = image.width * (LAYER_DP - VISIBLE_DP) / LAYER_DP / 2
    visible = image.crop((round(inset), round(inset), round(image.width - inset), round(image.height - inset)))
    big = visible.resize((size * 4, size * 4), Image.LANCZOS)
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).ellipse((0, 0, big.width - 1, big.height - 1), fill=255)
    big.putalpha(ImageChops.multiply(big.getchannel("A"), mask))
    return big.resize((size, size), Image.LANCZOS)


def webp(image, path_without_extension):
    """Lossless WebP, replacing a PNG of the same resource name."""
    image.save(path_without_extension + ".webp", "WEBP", lossless=True, quality=100, method=6)
    if os.path.exists(path_without_extension + ".png"):
        os.remove(path_without_extension + ".png")


def preview(composite, logo, path):
    sheet = Image.new("RGB", (1180, 540), (16, 18, 26))
    x = 24
    for size in (192, 144, 96, 48):
        icon = circle_masked(composite, size)
        sheet.paste(icon, (x, 24 + (192 - size) // 2), icon)
        x += size + 28
    inset = MASTER * (LAYER_DP - VISIBLE_DP) / LAYER_DP / 2
    visible = composite.crop((round(inset), round(inset), round(MASTER - inset), round(MASTER - inset))).resize((192, 192), Image.LANCZOS)
    for radius in (60, 30):
        mask = Image.new("L", (192, 192), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, 191, 191), radius=radius, fill=255)
        tile = visible.copy()
        tile.putalpha(mask)
        sheet.paste(tile, (x, 24), tile)
        x += 192 + 28
    for i, backdrop in enumerate(((16, 18, 26), (245, 243, 240))):
        tile = Image.new("RGBA", (240, 240), backdrop + (255,))
        tile.alpha_composite(logo.resize((200, 200), Image.LANCZOS), (20, 20))
        sheet.paste(tile, (24 + i * 260, 270))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="also write a preview sheet to this path")
    args = parser.parse_args()

    moon = tight(key_green(Image.open(os.path.join(HERE, "moon-greenscreen.png"))))
    background = Image.open(os.path.join(HERE, "background-mars.png")).convert("RGBA").resize((MASTER, MASTER), Image.LANCZOS)
    foreground = place_in_safe_zone(moon, MASTER)
    composite = background.copy()
    composite.alpha_composite(foreground)

    for density, factor in DENSITIES.items():
        folder = os.path.join(RES, f"mipmap-{density}")
        os.makedirs(folder, exist_ok=True)
        layer_px = round(LAYER_DP * factor)
        webp(foreground.resize((layer_px, layer_px), Image.LANCZOS), os.path.join(folder, "ic_launcher_foreground"))
        webp(background.resize((layer_px, layer_px), Image.LANCZOS).convert("RGB"), os.path.join(folder, "ic_launcher_background"))
        webp(circle_masked(composite, round(LEGACY_DP * factor)), os.path.join(folder, "ic_launcher"))

    logo = circle_masked(composite, 512)
    os.makedirs(os.path.join(RES, "drawable-nodpi"), exist_ok=True)
    webp(logo, os.path.join(RES, "drawable-nodpi", "phobos_logo"))
    if args.preview:
        preview(composite, logo, args.preview)


if __name__ == "__main__":
    main()
