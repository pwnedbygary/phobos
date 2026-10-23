#include "PhobosIntro.hpp"

#include "PhobosIntroArt.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace phobos::intro {

namespace {

constexpr float pi = 3.14159265f;

// The timeline, in ms.
constexpr int fadeInEnd = 700;       // the stars and Mars come out of black
constexpr int pushStart = 2100;      // the camera moves in on the moon and pulls focus onto it
constexpr int pushEnd = 3500;
constexpr int wordStart = 3500;      // "phobos" is drawn in below it
constexpr int wordEnd = 4100;
constexpr int glintStart = 4150;     // a glint crosses the logo, and two sparkles
constexpr int glintEnd = 4650;
constexpr int dissolveStart = 5000;  // the Library shows through
constexpr int dissolveLength = 600;
constexpr int skippedDissolveLength = 300;

struct Rgb {
  std::uint8_t r, g, b;
};
constexpr Rgb sky{0, 0, 9};
// Mars, dark to light, as the launcher icon's Mars has it (tools/logo/background-mars.png): the last two are its sunlit
// rim.
constexpr Rgb marsColors[] = {{22, 4, 13}, {49, 6, 11}, {73, 7, 10}, {111, 10, 7}, {137, 14, 7}, {168, 18, 5},
                              {189, 22, 4}, {210, 31, 4}, {230, 44, 6}, {247, 85, 24}, {252, 153, 73}, {252, 183, 89}};
constexpr Rgb iceColors[] = {{92, 40, 52}, {168, 120, 120}, {222, 196, 190}, {250, 240, 232}};
// The glow of its air beyond its edge, faint to bright.
constexpr Rgb glowColors[] = {{40, 6, 16}, {71, 9, 17}, {151, 21, 8}, {210, 37, 11}};
// The stars, as the icon's: dim blue, dim orange, blue, pale blue, orange; white for the brightest, a sparkle's arms.
constexpr Rgb starColors[] = {{24, 96, 168}, {120, 72, 44}, {48, 120, 192}, {192, 216, 240}, {240, 168, 96}};
constexpr Rgb starWhite{240, 240, 240};
constexpr Rgb sparkleArm{144, 192, 216};

// An ordered dither: Bayer's 8 x 8 matrix.
constexpr std::uint8_t bayer[8][8] = {
  { 0, 32,  8, 40,  2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44,  4, 36, 14, 46,  6, 38},
  {60, 28, 52, 20, 62, 30, 54, 22}, { 3, 35, 11, 43,  1, 33,  9, 41}, {51, 19, 59, 27, 49, 17, 57, 25},
  {15, 47,  7, 39, 13, 45,  5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

// The dither's threshold at (x, y), in (0, 1).
auto threshold(int x, int y) -> float {
  return (bayer[y & 7][x & 7] + 0.5f) / 64;
}

auto clamp01(float v) -> float {
  return std::clamp(v, 0.0f, 1.0f);
}

// How far [ms] is from [from] to [to], 0 to 1.
auto progress(int ms, int from, int to) -> float {
  return clamp01((float(ms) - float(from)) / float(to - from));
}

// Eased in and out (smootherstep).
auto ease(float t) -> float {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

auto lerp(float a, float b, float t) -> float {
  return a + (b - a) * t;
}

auto smoothstep(float from, float to, float v) -> float {
  float t = clamp01((v - from) / (to - from));
  return t * t * (3 - 2 * t);
}

auto scaled(Rgb c, float by) -> Rgb {
  auto channel = [by](std::uint8_t v) { return std::uint8_t(std::clamp(std::lround(v * by), 0L, 255L)); };
  return {channel(c.r), channel(c.g), channel(c.b)};
}

// A color [by] of the way to white.
auto lightened(Rgb c, float by) -> Rgb {
  auto channel = [by](std::uint8_t v) { return std::uint8_t(std::lround(v + (255 - v) * by)); };
  return {channel(c.r), channel(c.g), channel(c.b)};
}

auto unpack(std::uint32_t argb) -> Rgb {
  return {std::uint8_t(argb >> 16), std::uint8_t(argb >> 8), std::uint8_t(argb)};
}

// xorshift32: the same numbers from the same seed, on every machine.
struct Random {
  std::uint32_t state;
  auto next() -> std::uint32_t {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  // In [0, 1).
  auto unit() -> float {
    return (next() >> 8) / 16777216.0f;
  }
};

// Value noise in three dimensions, in [0, 1): the planet's surface is sampled on the sphere, so it has no seam.
auto lattice(int x, int y, int z) -> float {
  std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ std::uint32_t(z) * 0xcb1ab31fu;
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  h *= 0x297a2d39u;
  h ^= h >> 15;
  return (h >> 8) / 16777216.0f;
}

auto noise(float x, float y, float z) -> float {
  float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  int ix = int(fx), iy = int(fy), iz = int(fz);
  float tx = x - fx, ty = y - fy, tz = z - fz;
  tx = tx * tx * (3 - 2 * tx);
  ty = ty * ty * (3 - 2 * ty);
  tz = tz * tz * (3 - 2 * tz);
  auto row = [&](int dy, int dz) { return lerp(lattice(ix, iy + dy, iz + dz), lattice(ix + 1, iy + dy, iz + dz), tx); };
  return lerp(lerp(row(0, 0), row(1, 0), ty), lerp(row(0, 1), row(1, 1), ty), tz);
}

auto fbm(float x, float y, float z, int octaves) -> float {
  float sum = 0, amplitude = 0.5f, total = 0;
  for (int i = 0; i < octaves; i++) {
    sum += amplitude * noise(x, y, z);
    total += amplitude;
    x *= 2.03f;
    y *= 2.03f;
    z *= 2.03f;
    amplitude *= 0.5f;
  }
  return sum / total;
}

// Mars's surface by longitude (across) and latitude (down, north at the top): how light the ground is, and its ice.
constexpr int mapWidth = 256;
constexpr int mapHeight = 128;

struct MarsMap {
  std::vector<float> albedo = std::vector<float>(mapWidth * mapHeight);
  std::vector<std::uint8_t> ice = std::vector<std::uint8_t>(mapWidth * mapHeight);
};

auto makeMarsMap() -> MarsMap {
  struct Crater {
    float x, y, z, radius;
  };
  std::vector<Crater> craters;
  Random random{0x5eed1234u};
  for (int i = 0; i < 30; i++) {
    float y = random.unit() * 1.7f - 0.85f;  // within about 58 degrees of the equator
    float longitude = random.unit() * 2 * pi;
    float across = std::sqrt(1 - y * y);
    float radius = 0.035f + random.unit() * random.unit() * 0.11f;
    craters.push_back({across * std::sin(longitude), y, across * std::cos(longitude), radius});
  }
  MarsMap map;
  for (int j = 0; j < mapHeight; j++) {
    float latitude = pi / 2 - (j + 0.5f) * pi / mapHeight;
    for (int i = 0; i < mapWidth; i++) {
      float longitude = (i + 0.5f) * 2 * pi / mapWidth;
      float x = std::cos(latitude) * std::sin(longitude), y = std::sin(latitude);
      float z = std::cos(latitude) * std::cos(longitude);
      float base = fbm(x * 2.2f + 11, y * 2.2f + 3, z * 2.2f + 7, 5);
      float broad = fbm(x * 1.3f + 41, y * 1.3f + 19, z * 1.3f + 29, 4);
      float albedo = 0.66f + (base - 0.5f) * 0.55f - 0.26f * smoothstep(0.52f, 0.66f, broad);
      // a long canyon south of the equator, Valles Marineris's like
      if (longitude > 3.4f && longitude < 4.6f) {
        float middle = -0.13f + 0.035f * std::sin(longitude * 7);
        float width = 0.022f + 0.014f * std::sin(longitude * 3 + 1);
        float off = std::fabs(latitude - middle);
        if (off < width) albedo -= 0.24f;
        else if (off < width + 0.02f) albedo += 0.06f;
      }
      // a great volcano north of it, Olympus Mons's like: light, its caldera and its scarp dark
      {
        float vx = std::cos(0.33f) * std::sin(2.95f), vy = std::sin(0.33f), vz = std::cos(0.33f) * std::cos(2.95f);
        float d = std::sqrt((x - vx) * (x - vx) + (y - vy) * (y - vy) + (z - vz) * (z - vz));
        if (d < 0.025f) albedo -= 0.16f;
        else if (d < 0.12f) albedo += 0.12f;
        else if (d < 0.145f) albedo -= 0.1f;
      }
      for (auto& c : craters) {
        float d = std::sqrt((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) + (z - c.z) * (z - c.z)) / c.radius;
        if (d < 0.72f) albedo -= 0.16f;
        else if (d < 1) albedo += 0.2f;
        else if (d < 1.3f) albedo -= 0.05f;
      }
      float edge = (fbm(x * 6 + 5, y * 6 + 9, z * 6 + 13, 3) - 0.5f) * 0.12f;
      map.albedo[j * mapWidth + i] = std::clamp(albedo, 0.05f, 1.0f);
      map.ice[j * mapWidth + i] = latitude > 1.2f + edge || latitude < -1.32f + edge;
    }
  }
  return map;
}

auto marsMap() -> const MarsMap& {
  static const MarsMap map = makeMarsMap();
  return map;
}

// Where things go on a canvas: a 320 x 180 canvas is the design, others are scaled by [unit] (but the logo, whose art
// is drawn at its own size).
constexpr int lockupGap = 6;  // between the moon and the word
struct Layout {
  float unit;
  float marsX, marsY, marsRadius;  // as the intro starts
  float marsEndY, marsEndRadius;   // once the moon is in focus: lower, nearer, its top below the logo
  float orbitWidth, orbitHeight;   // the moon's orbit's half axes, and its middle's height: across Mars's face
  float orbitY;
  int heroX, heroY;                // the moon in focus, the logo's
  int wordX, wordY;
};

auto layoutFor(int width, int height) -> Layout {
  Layout l;
  l.unit = std::min(width / 320.0f, height / 180.0f);
  l.marsX = width / 2.0f;
  l.marsY = height / 2.0f + 4 * l.unit;
  l.marsRadius = 48 * l.unit;
  l.marsEndRadius = 1.75f * l.marsRadius;
  l.marsEndY = height + 0.5f * l.marsEndRadius;
  l.orbitWidth = 2.5f * l.marsRadius;
  l.orbitHeight = 0.24f * l.orbitWidth;
  l.orbitY = l.marsY - 0.12f * l.marsRadius;
  int lockupHeight = art::moonHeight + lockupGap + art::wordHeight;
  l.heroX = (width - art::moonWidth) / 2;
  l.heroY = std::max(0, int(std::lround((height - lockupHeight) * 0.4f)));
  l.wordX = (width - art::wordWidth) / 2;
  l.wordY = l.heroY + art::moonHeight + lockupGap;
  return l;
}

// The moon's orbit: it starts behind Mars, near its top, comes round its left and across its face, toward the right.
constexpr float orbitStart = -1.55f;
constexpr float orbitSpeed = 1.75f;  // radians a second
constexpr float orbitTilt = -0.16f;  // rising to the right
// How far the camera has moved in when the moon snaps into focus.
constexpr float focusAt = 0.45f;

// Mars's axis, leaning right and its north pole tipped toward us, and how fast it turns.
constexpr float axisLean = -0.4f;
constexpr float axisTip = 0.35f;
constexpr float spinStart = 1.2f;
constexpr float spinSpeed = 0.25f;  // radians a second

auto drawStars(std::vector<Rgb>& image, int width, int height, const Scene& scene, const Layout& l, int ms) -> void {
  Random random{0x9e3779b9u};
  int count = width * height / 260;
  float t = ms / 1000.0f;
  float focusX = l.heroX + art::moonWidth / 2.0f, focusY = l.heroY + art::moonHeight / 2.0f;
  auto plot = [&](int x, int y, Rgb c) {
    if (x >= 0 && y >= 0 && x < width && y < height) image[y * width + x] = c;
  };
  for (int i = 0; i < count; i++) {
    float x = random.unit() * width, y = random.unit() * height;
    float kind = random.unit(), hue = random.unit(), phase = random.unit() * 2 * pi, rate = 1.5f + random.unit() * 3;
    int layer = kind < 0.08f ? 2 : kind < 0.3f ? 1 : 0;  // a sparkle, a bright star or a dim one
    // the far ones drift slower
    x = std::fmod(x - (layer + 1) * 1.6f * t * l.unit + width * 8.0f, float(width));
    x = focusX + (x - focusX) * scene.starZoom;
    y = focusY + (y - focusY) * scene.starZoom;
    float twinkle = 0.65f + 0.35f * std::sin(phase + rate * t);
    int px = int(std::floor(x)), py = int(std::floor(y));
    if (layer == 0) {
      plot(px, py, scaled(starColors[hue < 0.7f ? 0 : 1], twinkle));
    } else if (layer == 1) {
      plot(px, py, scaled(starColors[2 + int(hue * 3)], twinkle));
    } else {
      plot(px, py, starWhite);
      int arm = twinkle > 0.85f ? 2 : 1;
      for (int d = 1; d <= arm; d++) {
        Rgb c = scaled(sparkleArm, d == arm && arm == 2 ? 0.6f : twinkle);
        plot(px - d, py, c);
        plot(px + d, py, c);
        plot(px, py - d, c);
        plot(px, py + d, c);
      }
    }
  }
}

// Box-blurs a layer of premultiplied RGBA floats along its rows ([step] 1) or its columns ([step] its width).
auto blurLine(std::vector<float>& data, int lines, int length, int lineStep, int step, int radius) -> void {
  std::vector<float> line(std::size_t(length) * 4);
  float weight = 1.0f / (2 * radius + 1);
  for (int n = 0; n < lines; n++) {
    float* first = data.data() + std::size_t(n) * lineStep * 4;
    for (int i = 0; i < length; i++) {
      for (int k = 0; k < 4; k++) line[i * 4 + k] = first[std::size_t(i) * step * 4 + k];
    }
    for (int i = 0; i < length; i++) {
      float sum[4] = {};
      for (int d = -radius; d <= radius; d++) {
        int at = i + d;
        if (at < 0 || at >= length) continue;
        for (int k = 0; k < 4; k++) sum[k] += line[at * 4 + k];
      }
      for (int k = 0; k < 4; k++) first[std::size_t(i) * step * 4 + k] = sum[k] * weight;
    }
  }
}

// Composites a layer of premultiplied RGBA floats, [w] x [h] with its top left at ([x0], [y0]), over the image.
auto composite(std::vector<Rgb>& image, int width, int height, const std::vector<float>& layer, int x0, int y0, int w,
               int h) -> void {
  for (int j = 0; j < h; j++) {
    int y = y0 + j;
    if (y < 0 || y >= height) continue;
    for (int i = 0; i < w; i++) {
      int x = x0 + i;
      const float* p = &layer[(std::size_t(j) * w + i) * 4];
      if (x < 0 || x >= width || p[3] <= 0) continue;
      Rgb& under = image[std::size_t(y) * width + x];
      float keep = 1 - std::min(1.0f, p[3]);
      under = {std::uint8_t(std::clamp(std::lround(p[0] + under.r * keep), 0L, 255L)),
               std::uint8_t(std::clamp(std::lround(p[1] + under.g * keep), 0L, 255L)),
               std::uint8_t(std::clamp(std::lround(p[2] + under.b * keep), 0L, 255L))};
    }
  }
}

auto drawMars(std::vector<Rgb>& image, int width, int height, const Scene& scene) -> void {
  const auto& map = marsMap();
  float radius = scene.marsRadius;
  if (radius <= 0) return;
  constexpr float glowWidth = 3.5f;  // canvas pixels beyond the edge
  int pad = int(glowWidth) + 2 + 2 * scene.marsBlur;
  int x0 = std::max(0, int(std::floor(scene.marsX - radius)) - pad);
  int y0 = std::max(0, int(std::floor(scene.marsY - radius)) - pad);
  int x1 = std::min(width, int(std::ceil(scene.marsX + radius)) + pad);
  int y1 = std::min(height, int(std::ceil(scene.marsY + radius)) + pad);
  if (x0 >= x1 || y0 >= y1) return;
  int w = x1 - x0, h = y1 - y0;
  std::vector<float> layer(std::size_t(w) * h * 4);
  float leanCos = std::cos(axisLean), leanSin = std::sin(axisLean);
  float tipCos = std::cos(axisTip), tipSin = std::sin(axisTip);
  float darken = 1 - scene.marsDim;
  for (int y = y0; y < y1; y++) {
    for (int x = x0; x < x1; x++) {
      float dx = (x + 0.5f - scene.marsX) / radius, dy = (y + 0.5f - scene.marsY) / radius;
      float r2 = dx * dx + dy * dy;
      float sunward = -(dx * 0.76f + dy * 0.65f);  // toward the sun at the upper left, in the picture's plane
      Rgb c;
      if (r2 <= 1) {
        float nz = std::sqrt(1 - r2);
        // the planet's own axes: y its axis, upward
        float ax = dx * leanCos - dy * leanSin, ay = -dx * leanSin - dy * leanCos;
        float by = ay * tipCos + nz * tipSin, bz = -ay * tipSin + nz * tipCos;
        float latitude = std::asin(std::clamp(by, -1.0f, 1.0f));
        float longitude = std::atan2(ax, bz) - scene.spin;
        int i = int(std::floor(longitude / (2 * pi) * mapWidth)) % mapWidth;
        if (i < 0) i += mapWidth;
        int j = std::clamp(int((pi / 2 - latitude) / pi * mapHeight), 0, mapHeight - 1);
        float sun = std::max(0.0f, -0.42f * dx - 0.36f * dy + 0.83f * nz);
        float value = map.albedo[j * mapWidth + i] * (0.1f + 1.05f * sun);
        value += std::pow(1 - nz, 3.0f) * std::max(0.0f, sunward) * 0.95f;  // the bright rim on its sunlit side
        if (map.ice[j * mapWidth + i]) {
          c = iceColors[std::clamp(int(std::floor(value * 3.2f + threshold(x, y))), 0, 3)];
        } else {
          c = marsColors[std::clamp(int(std::floor(value * 11 + threshold(x, y))), 0, 11)];
        }
      } else {
        float q = (std::sqrt(r2) - 1) * radius / glowWidth;
        if (q >= 1) continue;
        float strength = (1 - q) * (1 - q) * (0.35f + 0.65f * clamp01(sunward / std::sqrt(r2)));
        int index = int(std::floor(strength * 4 + threshold(x, y))) - 1;
        if (index < 0) continue;
        c = glowColors[std::min(index, 3)];
      }
      float* p = &layer[(std::size_t(y - y0) * w + (x - x0)) * 4];
      p[0] = c.r * darken;
      p[1] = c.g * darken;
      p[2] = c.b * darken;
      p[3] = 1;
    }
  }
  for (int pass = 0; pass < 2 && scene.marsBlur > 0; pass++) {
    blurLine(layer, h, w, w, 1, scene.marsBlur);
    blurLine(layer, w, h, 1, w, scene.marsBlur);
  }
  composite(image, width, height, layer, x0, y0, w, h);
}

// The moon's art scaled to [w] x [h], its top left at ([x0], [y0]): each pixel the average of the art's pixels it
// covers, and drawn where they cover at least half of it; then blurred by [blur]. At the art's own size, unblurred,
// the art as it is.
auto drawMoon(std::vector<Rgb>& image, int width, int height, int x0, int y0, int w, int h, int blur) -> void {
  if (w <= 0 || h <= 0) return;
  int pad = 2 * blur, lw = w + 2 * pad, lh = h + 2 * pad;
  std::vector<float> layer(std::size_t(lw) * lh * 4);
  float sx = float(art::moonWidth) / w, sy = float(art::moonHeight) / h;
  for (int j = 0; j < h; j++) {
    int y = y0 + j;
    if (y + pad < 0 || y - pad >= height) continue;
    float top = j * sy, bottom = (j + 1) * sy;
    for (int i = 0; i < w; i++) {
      int x = x0 + i;
      if (x + pad < 0 || x - pad >= width) continue;
      float left = i * sx, right = (i + 1) * sx;
      float cover = 0, r = 0, g = 0, b = 0;
      int ayEnd = std::min(art::moonHeight, int(std::ceil(bottom)));
      int axEnd = std::min(art::moonWidth, int(std::ceil(right)));
      for (int ay = int(top); ay < ayEnd; ay++) {
        float wy = std::min(bottom, ay + 1.0f) - std::max(top, float(ay));
        for (int ax = int(left); ax < axEnd; ax++) {
          std::uint32_t p = art::moon[ay * art::moonWidth + ax];
          if (!(p >> 24)) continue;
          float weight = (std::min(right, ax + 1.0f) - std::max(left, float(ax))) * wy;
          cover += weight;
          r += weight * ((p >> 16) & 0xff);
          g += weight * ((p >> 8) & 0xff);
          b += weight * (p & 0xff);
        }
      }
      if (cover * 2 < sx * sy) continue;
      float* p = &layer[(std::size_t(j + pad) * lw + (i + pad)) * 4];
      p[0] = std::round(r / cover);
      p[1] = std::round(g / cover);
      p[2] = std::round(b / cover);
      p[3] = 1;
    }
  }
  for (int pass = 0; pass < 2 && blur > 0; pass++) {
    blurLine(layer, lh, lw, lw, 1, blur);
    blurLine(layer, lw, lh, 1, lw, blur);
  }
  composite(image, width, height, layer, x0 - pad, y0 - pad, lw, lh);
}

// "phobos", [shown] of it drawn in from the left along its italic's lean, the edge drawn in bright.
constexpr float wordLean = 0.25f;
auto drawWord(std::vector<Rgb>& image, int width, int height, int x0, int y0, float shown) -> void {
  if (shown <= 0) return;
  float start = -wordLean * (art::wordHeight - 1) - 3, end = art::wordWidth + 3.0f;
  float edge = lerp(start, end, shown);
  for (int j = 0; j < art::wordHeight; j++) {
    int y = y0 + j;
    if (y < 0 || y >= height) continue;
    float limit = edge + wordLean * (art::wordHeight - 1 - j);
    for (int i = 0; i < art::wordWidth; i++) {
      int x = x0 + i;
      int index = art::word[j * art::wordWidth + i];
      if (!index || x < 0 || x >= width || i >= limit) continue;
      Rgb c = unpack(art::wordColors[index]);
      if (shown < 1 && i >= limit - 2.5f) c = lightened(c, 0.75f);
      image[y * width + x] = c;
    }
  }
}

// A glint crossing the logo, a band leaning as the word does: [at] is how far across it is, 0 to 1.
auto drawGlint(std::vector<Rgb>& image, int width, int height, const Layout& l, float at) -> void {
  int top = l.heroY, bottom = l.wordY + art::wordHeight;
  float from = std::min(l.heroX, l.wordX) - 12.0f;
  float to = std::max(l.heroX + art::moonWidth, l.wordX + art::wordWidth) + 12.0f + 0.5f * (bottom - top);
  float middle = lerp(from, to, at);
  auto glint = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    float off = std::fabs(x + 0.5f * (y - top) - middle);
    if (off < 2.4f) image[y * width + x] = lightened(image[y * width + x], off < 1.2f ? 0.7f : 0.45f);
  };
  for (int j = 0; j < art::moonHeight; j++) {
    for (int i = 0; i < art::moonWidth; i++) {
      if (art::moon[j * art::moonWidth + i] >> 24) glint(l.heroX + i, l.heroY + j);
    }
  }
  for (int j = 0; j < art::wordHeight; j++) {
    for (int i = 0; i < art::wordWidth; i++) {
      if (art::word[j * art::wordWidth + i] >= 3) glint(l.wordX + i, l.wordY + j);  // its fill, not its outline
    }
  }
}

// A four-pointed sparkle at ([x], [y]), [at] of the way through its life, 0 to 1.
auto drawSparkle(std::vector<Rgb>& image, int width, int height, int x, int y, float at) -> void {
  if (at <= 0 || at >= 1) return;
  int arm = int(std::lround(3 * std::sin(pi * at)));
  auto plot = [&](int px, int py, Rgb c) {
    if (px >= 0 && py >= 0 && px < width && py < height) image[py * width + px] = c;
  };
  plot(x, y, {255, 255, 255});
  for (int d = 1; d <= arm; d++) {
    Rgb c = d < arm ? Rgb{196, 224, 255} : Rgb{110, 150, 230};
    plot(x - d, y, c);
    plot(x + d, y, c);
    plot(x, y - d, c);
    plot(x, y + d, c);
  }
}

}

auto canvasFor(int width, int height) -> Canvas {
  if (width <= 0 || height <= 0) return {0, 0, 1};
  int scale = std::max(1, int(std::lround(std::min(width, height) / 180.0)));
  return {(width - 1) / scale + 1, (height - 1) / scale + 1, scale};
}

auto sceneAt(int width, int height, int ms, int skippedAt) -> Scene {
  Scene scene;
  auto l = layoutFor(std::max(width, 1), std::max(height, 1));
  float t = ms / 1000.0f;
  scene.spin = spinStart + spinSpeed * t;

  float push = ease(progress(ms, pushStart, pushEnd));
  float phi = orbitStart + orbitSpeed * t;
  float ex = -l.orbitWidth * std::cos(phi), ey = l.orbitHeight * std::sin(phi);
  float orbitX = l.marsX + ex * std::cos(orbitTilt) - ey * std::sin(orbitTilt);
  float orbitY = l.orbitY + ex * std::sin(orbitTilt) + ey * std::cos(orbitTilt);
  float depth = std::sin(phi);
  // nearer, bigger; from where the camera starts moving in, the moon only grows
  float nearness = std::sin(orbitStart + orbitSpeed * std::min(ms, pushStart) / 1000.0f);
  float orbitScale = l.unit * (0.16f + 0.035f * nearness);
  float scale = std::exp(lerp(std::log(orbitScale), 0.0f, push));
  scene.moonWidth = std::max(1, int(std::lround(art::moonWidth * scale)));
  scene.moonHeight = std::max(1, int(std::lround(art::moonHeight * scale)));
  if (push >= 1) {
    scene.moonX = l.heroX;
    scene.moonY = l.heroY;
  } else {
    float middleX = lerp(orbitX, l.heroX + art::moonWidth / 2.0f, push);
    float middleY = lerp(orbitY, l.heroY + art::moonHeight / 2.0f, push);
    scene.moonX = int(std::lround(middleX - scene.moonWidth / 2.0f));
    scene.moonY = int(std::lround(middleY - scene.moonHeight / 2.0f));
  }
  scene.moonBlur = push < focusAt ? 1 : 0;
  scene.moonInFront = push > 0 || depth > 0;

  scene.marsX = l.marsX;
  scene.marsY = lerp(l.marsY, l.marsEndY, push);
  scene.marsRadius = lerp(l.marsRadius, l.marsEndRadius, push);
  scene.marsBlur = int(std::lround(2.2f * push));
  scene.marsDim = 0.3f * push;
  scene.starZoom = 1 + 0.45f * push;

  scene.wordX = l.wordX;
  scene.wordY = l.wordY;
  scene.wordShown = progress(ms, wordStart, wordEnd);
  scene.glint = ms >= glintStart && ms <= glintEnd ? progress(ms, glintStart, glintEnd) : -1;
  scene.fade = ease(progress(ms, 0, fadeInEnd));
  scene.dissolve = progress(ms, dissolveStart, dissolveStart + dissolveLength);
  if (skippedAt >= 0 && ms >= skippedAt) {
    // faster from where it had got to at the skip
    float atSkip = progress(skippedAt, dissolveStart, dissolveStart + dissolveLength);
    scene.dissolve = clamp01(std::max(scene.dissolve, atSkip + float(ms - skippedAt) / skippedDissolveLength));
  }
  return scene;
}

auto render(std::uint8_t* pixels, int width, int height, int stride, int ms, int skippedAt) -> bool {
  auto scene = sceneAt(width, height, ms, skippedAt);
  if (!pixels || width <= 0 || height <= 0) return scene.dissolve < 1;
  auto l = layoutFor(width, height);
  std::vector<Rgb> image(std::size_t(width) * height, sky);
  drawStars(image, width, height, scene, l, ms);
  auto moon = [&] {
    drawMoon(image, width, height, scene.moonX, scene.moonY, scene.moonWidth, scene.moonHeight, scene.moonBlur);
  };
  if (!scene.moonInFront) moon();
  drawMars(image, width, height, scene);
  if (scene.moonInFront) moon();
  drawWord(image, width, height, scene.wordX, scene.wordY, scene.wordShown);
  if (scene.glint >= 0) drawGlint(image, width, height, l, scene.glint);
  drawSparkle(image, width, height, l.heroX + 61, l.heroY + 9, progress(ms, glintStart + 100, glintStart + 500));
  drawSparkle(image, width, height, l.wordX + art::wordWidth - 14, l.wordY + 8,
              progress(ms, glintStart + 300, glintStart + 700));
  for (int y = 0; y < height; y++) {
    std::uint8_t* row = pixels + std::size_t(y) * stride;
    for (int x = 0; x < width; x++) {
      std::uint8_t* out = row + x * 4;
      if (threshold(x, y) < scene.dissolve) {
        out[0] = out[1] = out[2] = out[3] = 0;
        continue;
      }
      Rgb c = image[std::size_t(y) * width + x];
      if (scene.fade < 1) c = scaled(c, std::floor(scene.fade * 8 + threshold(x, y)) / 8);
      out[0] = c.r;
      out[1] = c.g;
      out[2] = c.b;
      out[3] = 255;
    }
  }
  return scene.dissolve < 1;
}

}
