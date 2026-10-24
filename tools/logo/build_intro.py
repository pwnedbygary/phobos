"""Builds the intro's pixel art, android/app/src/main/cpp/PhobosIntroArt.hpp, from the README's banner.

    python3 -m venv .venv && .venv/bin/pip install pillow
    .venv/bin/python tools/logo/build_intro.py

docs/images/phobos-logo.png (build_banner.py's output) is the moon and "phobos" on one art grid, SCALE screen pixels to
the art pixel. Each is cut out at one pixel per art pixel and written as C++ arrays, which the intro (PhobosIntro.cpp)
draws on Android and the desktop alike: the moon's colors as they are, the word's through its few colors. Run it again
after build_banner.py remakes the banner, rather than editing the header.
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
BANNER = os.path.join(ROOT, "docs", "images", "phobos-logo.png")
OUT = os.path.join(ROOT, "android", "app", "src", "main", "cpp", "PhobosIntroArt.hpp")
SCALE = 4  # build_banner.py's default


def pixels(image):
    """An RGBA image's pixels, row by row."""
    px = image.load()
    return [px[x, y] for y in range(image.height) for x in range(image.width)]


def art_grid():
    """The banner at one pixel per art pixel, checked to be whole cells of SCALE x SCALE alike."""
    banner = Image.open(BANNER).convert("RGBA")
    w, h = banner.size
    assert w % SCALE == 0 and h % SCALE == 0, "the banner isn't on a whole art grid"
    px = banner.load()
    for y in range(h):
        for x in range(w):
            assert px[x, y] == px[x - x % SCALE, y - y % SCALE], \
                f"the banner's cell at {x // SCALE},{y // SCALE} varies"
    art = banner.resize((w // SCALE, h // SCALE), Image.NEAREST)
    assert all(a in (0, 255) for *_, a in pixels(art)), "the banner has partly clear pixels"
    return art


def split(art):
    """The moon (left of the first clear column past it) and the word (right of that gap), each cropped to itself."""
    a = art.load()
    filled = [any(a[x, y][3] for y in range(art.height)) for x in range(art.width)]
    first = filled.index(True)
    gap = filled.index(False, first)
    word = filled.index(True, gap)
    moon = art.crop((0, 0, gap, art.height))
    rest = art.crop((word, 0, art.width, art.height))
    return moon.crop(moon.getchannel("A").getbbox()), rest.crop(rest.getchannel("A").getbbox())


def rows(values, per_line, width):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + " ".join(f"{v:>{width}}," for v in values[i:i + per_line]))
    return "\n".join(lines)


def main():
    moon, word = split(art_grid())
    moon_px = [(0xff000000 | r << 16 | g << 8 | b) if a else 0 for r, g, b, a in pixels(moon)]
    colors = sorted({(r, g, b) for r, g, b, a in pixels(word) if a})
    assert len(colors) < 16, "the word has more colors than it should"
    index = {c: i + 1 for i, c in enumerate(colors)}
    word_px = [index[(r, g, b)] if a else 0 for r, g, b, a in pixels(word)]
    palette = rows(["0x00000000"] + [f"0xff{r:02x}{g:02x}{b:02x}" for r, g, b in colors], 9, 10)
    text = f"""\
// The intro's pixel art (PhobosIntro.cpp): made by tools/logo/build_intro.py from docs/images/phobos-logo.png, one
// pixel per art pixel. Run that script again after a change to the banner, rather than editing this file.
#pragma once

#include <cstdint>

namespace phobos::intro::art {{

// The moon, the logo's, a d-pad in it: 0xAARRGGBB, its alpha 0xff where the moon is and 0 around it.
constexpr int moonWidth = {moon.width};
constexpr int moonHeight = {moon.height};
constexpr std::uint32_t moon[moonWidth * moonHeight] = {{
{rows([f"0x{v:08x}" for v in moon_px], 9, 10)}
}};

// "phobos", with its outline and shadow: each pixel's index in wordColors, 0 where there's none of the word.
constexpr int wordWidth = {word.width};
constexpr int wordHeight = {word.height};
constexpr std::uint32_t wordColors[] = {{
{palette}
}};
constexpr std::uint8_t word[wordWidth * wordHeight] = {{
{rows(word_px, 28, 2)}
}};

}}
"""
    with open(OUT, "w", encoding="utf-8") as out:
        out.write(text)
    print(f"{os.path.relpath(OUT, ROOT)}: the moon {moon.width}x{moon.height}, the word {word.width}x{word.height} "
          f"in {len(colors)} colors")


if __name__ == "__main__":
    main()
