"""Derives the Library's pixel-art consoles from the Systematic SVGs: ui/theme/ConsolePixels.kt.

    python3 -m venv .venv && .venv/bin/pip install cairosvg pillow numpy
    .venv/bin/python tools/console-art/pixelate.py [--sheet consoles.png]

Each console in android/app/src/main/assets/platforms (BAXY Square's Systematic set, MIT, and Phobos's PSP in its
style; see LICENSE) is drawn without its white halo, cropped to itself, and laid on a grid CELLS cells along its
longer side. Every cell takes one of the SVG's own colors: the one covering most of it, unless a darker one covers a
good share and stands out from it (a slot, a button, a line), so small dark details survive. A picture's palette
holds only the colors its cells took. The app maps those colors onto each style's limited palette
(ConsoleTreatment.kt). --sheet also writes the pictures in their own colors.
"""
import argparse
import io
import os
import re

import cairosvg
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
ASSETS = os.path.join(ROOT, "android", "app", "src", "main", "assets", "platforms")
OUT = os.path.join(ROOT, "android", "app", "src", "main", "java", "com", "phobos", "emulator", "ui", "theme", "ConsolePixels.kt")

# The consoles the Library draws: CONSOLE_ART in ui/PlatformIcons.kt.
CONSOLES = [
    "atari2600", "colecovision", "nes", "snes", "arcade", "laseractive", "n64", "gb", "gbc", "gba", "sms", "genesis",
    "sega32", "gamegear", "segacd", "psx", "psp", "neogeomvs", "neo-geo-cd", "neo-geo-pocket", "neo-geo-pocket-color",
    "zx-spectrum", "pce", "pcecd", "supergrafx", "wonderswan", "wonderswan-color", "msx", "msx2",
]
CELLS = 32
RENDER = 1024
SYMBOLS = "0123456789abcdefghijklmnopqrstuvwxyz"

# The halo: a group at half opacity (most of the set), or one white shape at half or 0.6 (the arcade, the
# PlayStation, the PSP). Kept in step with ConsoleArtPalette.withoutHalo.
HALO_GROUP = re.compile(r'<g clip-path="url\(#\w+\)" style="opacity:\.5">.*?</g>', re.S)
WHITE = r'(?:#fff\b|#ffffff\b|white\b)'
HALO_SHAPE = re.compile(
    r'<(?:path|rect|circle|ellipse)\b[^>]*?(?:fill:' + WHITE + r'[^>]*?opacity:\.[56]|opacity:\.[56][^>]*?fill:' + WHITE +
    r'|fill="' + WHITE + r'"[^>]*?opacity="\.[56]")[^>]*/>', re.S)
COLOR = re.compile(r'(?<![\w-])(?:fill|stroke|stop-color)(?:\s*:\s*|\s*=\s*["\'])(#[0-9a-fA-F]{6}(?![0-9a-fA-F])|#[0-9a-fA-F]{3}(?![0-9a-fA-F])|[a-zA-Z]+(?![\w(-]))')
NAMED = {"black": "#000000", "white": "#ffffff", "gray": "#808080", "grey": "#808080", "silver": "#c0c0c0", "red": "#ff0000"}


def without_halo(svg):
    stripped = HALO_GROUP.sub("", svg, count=1)
    if stripped == svg:
        stripped = HALO_SHAPE.sub("", svg, count=1)
    if stripped == svg:
        raise SystemExit("no halo found")
    return stripped


def hex_rgb(value):
    v = NAMED.get(value.lower(), value.lower())
    if not v.startswith("#"):
        return None
    if len(v) == 4:
        v = "#" + "".join(c * 2 for c in v[1:])
    return tuple(int(v[i:i + 2], 16) for i in (1, 3, 5))


def lightness(rgb):
    """Oklab lightness, as ConsoleArtPalette weighs greys."""
    def lin(c):
        c /= 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(c) for c in rgb)
    l_ = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m_ = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s_ = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    return 0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_


def pixelate(name):
    svg = without_halo(open(os.path.join(ASSETS, name + ".svg")).read())
    palette = sorted({c for c in (hex_rgb(v) for v in COLOR.findall(svg)) if c is not None}, key=lightness)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=RENDER)
    image = Image.open(io.BytesIO(png)).convert("RGBA")
    if image.height > image.width:
        image = Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode(), output_height=RENDER))).convert("RGBA")
    pixels = np.asarray(image).astype(np.int32)
    solid = pixels[..., 3] >= 128
    ys, xs = np.nonzero(solid)
    pixels = pixels[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    solid = solid[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    h, w = solid.shape
    cols, rows = (CELLS, max(1, round(CELLS * h / w))) if w >= h else (max(1, round(CELLS * w / h)), CELLS)
    colors = np.array(palette)
    nearest = np.argmin(((pixels[..., None, :3] - colors[None, None, :, :]) ** 2).sum(-1), axis=-1)
    light = [lightness(c) for c in palette]
    xb = np.linspace(0, w, cols + 1).round().astype(int)
    yb = np.linspace(0, h, rows + 1).round().astype(int)
    lines = []
    for j in range(rows):
        line = ""
        for i in range(cols):
            cell = solid[yb[j]:yb[j + 1], xb[i]:xb[i + 1]]
            if cell.size == 0 or cell.mean() < 0.5:
                line += "."
                continue
            counts = np.bincount(nearest[yb[j]:yb[j + 1], xb[i]:xb[i + 1]][cell], minlength=len(palette)) / cell.sum()
            choice = int(np.argmax(counts))
            darker = [k for k in range(len(palette)) if counts[k] >= 0.25 and light[choice] - light[k] >= 0.18]
            if darker:
                choice = min(darker, key=lambda k: light[k])
            line += SYMBOLS[choice]
        lines.append(line)
    # Only the colors some cell took, in the same order.
    used = sorted({SYMBOLS.index(c) for line in lines for c in line if c != "."})
    symbol = {old: SYMBOLS[new] for new, old in enumerate(used)}
    return [palette[k] for k in used], ["".join(c if c == "." else symbol[SYMBOLS.index(c)] for c in line) for line in lines]


def kotlin(pictures):
    out = [
        "package com.phobos.emulator.ui.theme",
        "",
        "/**",
        " * The Library's consoles as pixel art, generated by tools/console-art/pixelate.py from the Systematic SVGs",
        " * (BAXY Square's, MIT; and Phobos's PSP in their style: see LICENSE): each a grid of cells, [CELLS] along its",
        " * longer side, every cell one of its SVG's own colors (a symbol into [Picture.palette], `.` for none).",
        " * Regenerate rather than edit by hand.",
        " */",
        "internal object ConsolePixels {",
        f"    const val CELLS = {CELLS}",
        "",
        "    class Picture(val palette: IntArray, val rows: List<String>) {",
        "        val width: Int get() = rows[0].length",
        "        val height: Int get() = rows.size",
        "",
        "        /** The palette index of the cell at ([x], [y]), or -1 where the picture is empty. */",
        f"        fun at(x: Int, y: Int): Int = rows[y][x].let {{ if (it == '.') -1 else \"{SYMBOLS}\".indexOf(it) }}",
        "    }",
        "",
        "    val pictures: Map<String, Picture> = mapOf(",
    ]
    for name, (palette, lines) in pictures.items():
        colors = ", ".join("0xFF%02X%02X%02X.toInt()" % c for c in palette)
        out.append(f'        "{name}" to Picture(')
        out.append(f"            intArrayOf({colors}),")
        out.append("            listOf(")
        out.extend(f'                "{line}",' for line in lines)
        out.append("            ),")
        out.append("        ),")
    out += ["    )", "}", ""]
    return "\n".join(out)


def sheet(pictures, path, scale=6):
    tiles = []
    for name, (palette, lines) in pictures.items():
        h, w = len(lines), len(lines[0])
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        for y, line in enumerate(lines):
            for x, c in enumerate(line):
                if c != ".":
                    img.putpixel((x, y), palette[SYMBOLS.index(c)] + (255,))
        tiles.append(img.resize((w * scale, h * scale), Image.NEAREST))
    cell = CELLS * scale + 16
    columns = 8
    out = Image.new("RGBA", (columns * cell, ((len(tiles) + columns - 1) // columns) * cell), (40, 44, 52, 255))
    for k, tile in enumerate(tiles):
        out.alpha_composite(tile, ((k % columns) * cell + (cell - tile.width) // 2, (k // columns) * cell + (cell - tile.height) // 2))
    out.save(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sheet")
    args = parser.parse_args()
    pictures = {name: pixelate(name) for name in CONSOLES}
    with open(OUT, "w") as f:
        f.write(kotlin(pictures))
    if args.sheet:
        sheet(pictures, args.sheet)


if __name__ == "__main__":
    main()
