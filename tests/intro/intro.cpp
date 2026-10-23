// The intro's tests (android/app/src/main/cpp/PhobosIntro.cpp, which the Android app and the desktop program both draw
// with): the canvas a window gets, the frames' choreography, the logo drawn as its art is, the dissolve into the
// Library and a skip. They run on the host, no window or device needed: intro-tests, which desktop.yml's Linux job
// builds and runs (CMakeLists.txt).
#include "PhobosIntro.hpp"
#include "PhobosIntroArt.hpp"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace phobos::intro;

namespace {

int checks = 0;
int failures = 0;

void check(bool passed, const char* what, int line) {
  checks++;
  if (passed) return;
  failures++;
  std::printf("intro.cpp:%d: %s\n", line, what);
}
#define CHECK(condition) check((condition), #condition, __LINE__)

// A frame of the intro, as render() draws it into a canvas of its own.
struct Frame {
  int width, height;
  std::vector<std::uint8_t> pixels;
  bool running;

  Frame(int width, int height, int ms, int skippedAt = -1)
      : width(width), height(height), pixels(std::size_t(width) * height * 4) {
    running = render(pixels.data(), width, height, width * 4, ms, skippedAt);
  }
  auto at(int x, int y) const -> const std::uint8_t* {
    return &pixels[(std::size_t(y) * width + x) * 4];
  }
  auto clear(int x, int y) const -> bool {
    return at(x, y)[3] == 0;
  }
  auto clearCount() const -> int {
    int n = 0;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) n += clear(x, y);
    }
    return n;
  }
  // Whether the art (0xAARRGGBB, its alpha 0 where there's none of it) is drawn exactly, its top left at (x0, y0).
  auto shows(const std::uint32_t* art, int w, int h, int x0, int y0) const -> bool {
    if (x0 < 0 || y0 < 0 || x0 + w > width || y0 + h > height) return false;
    for (int j = 0; j < h; j++) {
      for (int i = 0; i < w; i++) {
        std::uint32_t c = art[j * w + i];
        if (!(c >> 24)) continue;
        auto* p = at(x0 + i, y0 + j);
        if (p[0] != ((c >> 16) & 0xff) || p[1] != ((c >> 8) & 0xff) || p[2] != (c & 0xff) || p[3] != 255) return false;
      }
    }
    return true;
  }
  // Where the art is drawn exactly, searching the whole frame: {x, y}, or {-1, -1} where it isn't, or is twice.
  auto find(const std::uint32_t* art, int w, int h, int found[2]) const -> void {
    found[0] = found[1] = -1;
    int count = 0;
    for (int y = 0; y + h <= height; y++) {
      for (int x = 0; x + w <= width; x++) {
        if (shows(art, w, h, x, y)) {
          found[0] = x;
          found[1] = y;
          count++;
        }
      }
    }
    if (count != 1) found[0] = found[1] = -1;
  }
};

// The word's art as 0xAARRGGBB, from its palette.
auto wordArt() -> std::vector<std::uint32_t> {
  std::vector<std::uint32_t> out(art::wordWidth * art::wordHeight);
  for (int i = 0; i < art::wordWidth * art::wordHeight; i++) out[i] = art::wordColors[art::word[i]];
  return out;
}

void theCanvasCoversTheWindowAtAWholeScale() {
  // Scales worked out by hand: the window's shorter side over 180, rounded, and at least 1.
  struct Case {
    int width, height, scale, canvasWidth, canvasHeight;
  } cases[] = {
    {1920, 1080, 6, 320, 180},  // the RP6, a 1080p monitor
    {1280, 720, 4, 320, 180},
    {1280, 800, 4, 320, 200},   // the Steam Deck
    {2560, 1440, 8, 320, 180},
    {1080, 2400, 6, 180, 400},  // a phone held upright
    {1921, 1080, 6, 321, 180},  // a pixel over: one more canvas column, part of it off the window
    {800, 600, 3, 267, 200},
    {100, 50, 1, 100, 50},      // smaller than the canvas is meant to be: one to one
  };
  for (auto& c : cases) {
    auto canvas = canvasFor(c.width, c.height);
    CHECK(canvas.scale == c.scale && canvas.width == c.canvasWidth && canvas.height == c.canvasHeight);
  }
  for (int width = 1; width < 3000; width += 37) {
    for (int height = 1; height < 3000; height += 41) {
      auto canvas = canvasFor(width, height);
      bool covers = canvas.width * canvas.scale >= width && canvas.height * canvas.scale >= height;
      bool tight = (canvas.width - 1) * canvas.scale < width && (canvas.height - 1) * canvas.scale < height;
      if (!covers || !tight) {
        CHECK(covers && tight);
        return;
      }
    }
  }
  auto none = canvasFor(0, 600);
  CHECK(none.width == 0 && none.height == 0);
}

void everyPixelIsOpaqueOrLetsTheLibraryThroughWhole() {
  // Android's bitmaps are premultiplied: a pixel the Library shows through must be 0 in all four bytes.
  for (int ms : {0, 350, 1700, 3000, 4400, 5200, 5400}) {
    Frame frame(320, 180, ms);
    bool whole = true;
    for (int y = 0; y < frame.height; y++) {
      for (int x = 0; x < frame.width; x++) {
        auto* p = frame.at(x, y);
        if (p[3] != 255 && (p[0] || p[1] || p[2] || p[3])) whole = false;
      }
    }
    CHECK(whole);
  }
}

void itOpensFromBlack() {
  Frame frame(320, 180, 0);
  bool black = true;
  for (int y = 0; y < frame.height; y++) {
    for (int x = 0; x < frame.width; x++) {
      auto* p = frame.at(x, y);
      if (p[0] || p[1] || p[2] || p[3] != 255) black = false;
    }
  }
  CHECK(black);
  CHECK(frame.running);
  // and gets lighter as it fades in
  auto light = [](const Frame& f) {
    long sum = 0;
    for (auto v : f.pixels) sum += v;
    return sum;
  };
  CHECK(light(Frame(320, 180, 200)) < light(Frame(320, 180, 450)));
  CHECK(light(Frame(320, 180, 450)) < light(Frame(320, 180, 1000)));
}

void theLogoEndsUpDrawnAsItsArtIsTheWordBelowTheMoonBothCentred() {
  auto word = wordArt();
  // After the glint and the sparkles, before the dissolve, on windows of several shapes.
  for (auto size : {std::pair{320, 180}, std::pair{320, 200}, std::pair{180, 400}, std::pair{427, 180}}) {
    Frame frame(size.first, size.second, 4950);
    int moon[2], text[2];
    frame.find(art::moon, art::moonWidth, art::moonHeight, moon);
    frame.find(word.data(), art::wordWidth, art::wordHeight, text);
    CHECK(moon[0] >= 0);
    CHECK(text[0] >= 0);
    if (moon[0] < 0 || text[0] < 0) continue;
    CHECK(std::abs(2 * moon[0] + art::moonWidth - frame.width) <= 1);
    CHECK(std::abs(2 * text[0] + art::wordWidth - frame.width) <= 1);
    CHECK(text[1] > moon[1] + art::moonHeight);
    CHECK(text[1] - (moon[1] + art::moonHeight) < 16);
    CHECK(frame.running && frame.clearCount() == 0);
  }
  // Not while the camera is still moving in, nor the word before it's drawn in.
  Frame early(320, 180, 2900);
  int moon[2], text[2];
  early.find(art::moon, art::moonWidth, art::moonHeight, moon);
  early.find(word.data(), art::wordWidth, art::wordHeight, text);
  CHECK(moon[0] < 0 && text[0] < 0);
}

void theMoonOrbitsMarsThenComesIntoFocus() {
  auto inside = [](const Scene& s, float x, float y) {
    return std::hypot(x - s.marsX, y - s.marsY) < s.marsRadius;
  };
  auto middle = [](const Scene& s, float& x, float& y) {
    x = s.moonX + s.moonWidth / 2.0f;
    y = s.moonY + s.moonHeight / 2.0f;
  };
  float x, y;
  // It starts behind Mars, hidden.
  auto start = sceneAt(320, 180, 0, -1);
  middle(start, x, y);
  CHECK(!start.moonInFront && inside(start, x, y));
  // It crosses in front of Mars's face, small and out of focus, Mars sharp.
  bool crossed = false;
  for (int ms = 0; ms < 2100; ms += 20) {
    auto s = sceneAt(320, 180, ms, -1);
    middle(s, x, y);
    if (s.moonInFront && inside(s, x, y) && s.moonWidth < art::moonWidth / 3 && s.moonBlur > 0 && s.marsBlur == 0) {
      crossed = true;
    }
  }
  CHECK(crossed);
  // Then the focus changes over: the moon at its art's size, sharp, in front; Mars blurred.
  auto end = sceneAt(320, 180, 4000, -1);
  CHECK(end.moonInFront && end.moonWidth == art::moonWidth && end.moonHeight == art::moonHeight);
  CHECK(end.moonBlur == 0 && end.marsBlur > 0);
  // It grows all the way in, and its blur goes once and doesn't come back.
  int width = 0;
  bool grows = true, sharpened = false, blurredAgain = false;
  for (int ms = 2100; ms <= 3600; ms += 10) {
    auto s = sceneAt(320, 180, ms, -1);
    if (s.moonWidth < width) grows = false;
    width = s.moonWidth;
    if (s.moonBlur == 0) sharpened = true;
    else if (sharpened) blurredAgain = true;
  }
  CHECK(grows && sharpened && !blurredAgain);
}

void marsTurns() {
  auto s = sceneAt(320, 180, 800, -1);
  CHECK(sceneAt(320, 180, 1300, -1).spin > s.spin);
  Frame a(320, 180, 800), b(320, 180, 1300);
  // Inside Mars, away from its edge and from where the moon is in either frame.
  int inner = 0, changed = 0;
  auto t = sceneAt(320, 180, 1300, -1);
  for (int y = 0; y < 180; y++) {
    for (int x = 0; x < 320; x++) {
      if (std::hypot(x + 0.5f - s.marsX, y + 0.5f - s.marsY) > 0.8f * s.marsRadius) continue;
      auto byMoon = [&](const Scene& m) {
        return x >= m.moonX - 3 && x < m.moonX + m.moonWidth + 3 && y >= m.moonY - 3 && y < m.moonY + m.moonHeight + 3;
      };
      if (byMoon(s) || byMoon(t)) continue;
      inner++;
      changed += std::memcmp(a.at(x, y), b.at(x, y), 4) != 0;
    }
  }
  CHECK(inner > 3000);
  CHECK(changed * 5 > inner);
}

void theWordIsDrawnInFromTheLeft() {
  CHECK(sceneAt(320, 180, 3400, -1).wordShown == 0);
  float before = 0;
  bool grows = true;
  for (int ms = 3400; ms <= 4200; ms += 25) {
    float shown = sceneAt(320, 180, ms, -1).wordShown;
    if (shown < before) grows = false;
    before = shown;
  }
  CHECK(grows && before == 1);
  // Partway in, the word's left end is drawn and its right end isn't.
  Frame frame(320, 180, 3800);
  auto s = sceneAt(320, 180, 3800, -1);
  CHECK(s.wordShown > 0.2f && s.wordShown < 0.8f);
  auto drawnIn = [&](int i0, int i1) {
    int drawn = 0, word = 0;
    for (int j = 0; j < art::wordHeight; j++) {
      for (int i = i0; i < i1; i++) {
        if (!art::word[j * art::wordWidth + i]) continue;
        word++;
        std::uint32_t c = art::wordColors[art::word[j * art::wordWidth + i]];
        auto* p = frame.at(s.wordX + i, s.wordY + j);
        drawn += p[0] == ((c >> 16) & 0xff) && p[1] == ((c >> 8) & 0xff) && p[2] == (c & 0xff);
      }
    }
    return word ? float(drawn) / word : 0.0f;
  };
  CHECK(drawnIn(0, 25) > 0.9f);
  CHECK(drawnIn(art::wordWidth - 25, art::wordWidth) < 0.1f);
}

void itDissolvesIntoTheLibraryAndEnds() {
  Frame before(320, 180, 4990);
  CHECK(before.running && before.clearCount() == 0);
  Frame done(320, 180, 5600);
  CHECK(!done.running && done.clearCount() == 320 * 180);
  CHECK(!Frame(320, 180, 60000).running);
  // A pixel the Library shows through stays so, and about half have gone halfway through.
  Frame a(320, 180, 5150), b(320, 180, 5300), c(320, 180, 5450);
  bool kept = true;
  for (int y = 0; y < 180; y++) {
    for (int x = 0; x < 320; x++) {
      if ((a.clear(x, y) && !b.clear(x, y)) || (b.clear(x, y) && !c.clear(x, y))) kept = false;
    }
  }
  CHECK(kept);
  CHECK(a.clearCount() < b.clearCount() && b.clearCount() < c.clearCount());
  CHECK(std::abs(b.clearCount() - 320 * 180 / 2) < 320 * 180 / 20);
}

void aSkipDissolvesAtOnceAndNeverUndoesTheDissolve() {
  // Skipped a second in: gone a third of a second later, half gone halfway.
  CHECK(Frame(320, 180, 1000, 1000).clearCount() == 0);
  Frame half(320, 180, 1150, 1000);
  CHECK(half.running && std::abs(half.clearCount() - 320 * 180 / 2) < 320 * 180 / 20);
  Frame gone(320, 180, 1300, 1000);
  CHECK(!gone.running && gone.clearCount() == 320 * 180);
  // A skip at the very start.
  CHECK(!Frame(320, 180, 300, 0).running);
  // A skip during the closing dissolve hurries it from where it was, and takes nothing back.
  CHECK(Frame(320, 180, 5200, 5200).pixels == Frame(320, 180, 5200).pixels);
  Frame plain(320, 180, 5250), skipped(320, 180, 5250, 5200);
  bool kept = true;
  for (int y = 0; y < 180; y++) {
    for (int x = 0; x < 320; x++) {
      if (plain.clear(x, y) && !skipped.clear(x, y)) kept = false;
    }
  }
  CHECK(kept && skipped.clearCount() > plain.clearCount());
  // A skip not yet come changes nothing.
  Frame later(320, 180, 2000, 2500), none(320, 180, 2000);
  CHECK(later.pixels == none.pixels);
}

void framesDependOnTheirMomentAloneAndKeepToTheirRows() {
  // The same moment drawn again, after others, is the same frame.
  Frame first(320, 180, 2600);
  Frame(320, 180, 4100);
  Frame(200, 300, 1000);
  CHECK(Frame(320, 180, 2600).pixels == first.pixels);
  // Rows further apart than the canvas's width: what's between them is left alone.
  int width = 97, height = 61, stride = width * 4 + 12;
  std::vector<std::uint8_t> pixels(std::size_t(stride) * height, 0xa5);
  render(pixels.data(), width, height, stride, 3000, -1);
  bool kept = true;
  for (int y = 0; y < height; y++) {
    for (int i = width * 4; i < stride; i++) kept &= pixels[std::size_t(y) * stride + i] == 0xa5;
  }
  CHECK(kept);
  Frame packed(width, height, 3000);
  bool same = true;
  for (int y = 0; y < height; y++) same &= !std::memcmp(&pixels[std::size_t(y) * stride], packed.at(0, y), width * 4);
  CHECK(same);
}

void oddSizesDrawWithoutTrouble() {
  // Under the sanitizers: nothing read or written out of bounds, whatever the canvas.
  for (auto size : {std::pair{1, 1}, std::pair{2, 3}, std::pair{7, 500}, std::pair{500, 7}, std::pair{1000, 1000},
                    std::pair{73, 70}, std::pair{166, 48}}) {
    for (int ms : {0, 1000, 2500, 3200, 3800, 4400, 5200}) Frame(size.first, size.second, ms);
  }
  CHECK(render(nullptr, 320, 180, 1280, 100, -1));
  CHECK(!render(nullptr, 0, 0, 0, 6000, -1));
}

void theEndsOfIntsRangeStayDefined() {
  // Under the sanitizers, which stop at undefined behaviour: no overflow, however big the window or far the moment.
  auto huge = canvasFor(INT_MAX, INT_MAX);
  long long width = huge.width, scale = huge.scale;
  CHECK(scale > 1 && width * scale >= INT_MAX && (width - 1) * scale < INT_MAX && huge.height == huge.width);
  Frame before(32, 18, INT_MIN);
  bool black = true;
  for (int i = 0; i < 32 * 18; i++) {
    auto* p = &before.pixels[std::size_t(i) * 4];
    if (p[0] || p[1] || p[2] || p[3] != 255) black = false;
  }
  CHECK(before.running && black);
  CHECK(!Frame(32, 18, INT_MAX).running);
  CHECK(!Frame(32, 18, INT_MAX, 0).running);
}

}

int main() {
  theCanvasCoversTheWindowAtAWholeScale();
  everyPixelIsOpaqueOrLetsTheLibraryThroughWhole();
  itOpensFromBlack();
  theLogoEndsUpDrawnAsItsArtIsTheWordBelowTheMoonBothCentred();
  theMoonOrbitsMarsThenComesIntoFocus();
  marsTurns();
  theWordIsDrawnInFromTheLeft();
  itDissolvesIntoTheLibraryAndEnds();
  aSkipDissolvesAtOnceAndNeverUndoesTheDissolve();
  framesDependOnTheirMomentAloneAndKeepToTheirRows();
  oddSizesDrawWithoutTrouble();
  theEndsOfIntsRangeStayDefined();
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
