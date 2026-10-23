#pragma once

#include <cstdint>

// Phobos's intro, as Phobos opens to the Library: Mars turning among the stars and the moon (the logo's, a d-pad in it)
// orbiting it, then the moon coming into focus as the logo, "phobos" drawn in below it, and a dissolve into the
// Library. It's pixel art on a small canvas that the host scales up by a whole number: Android's IntroSplash and the
// desktop's Shell draw the same frames.
namespace phobos::intro {

// The canvas for a window of [width] x [height] pixels: [scale] window pixels to a canvas pixel, the scale that brings
// the window's shorter side nearest 180 canvas pixels, and enough canvas pixels to cover the window.
struct Canvas {
  int width = 0;
  int height = 0;
  int scale = 1;
};
auto canvasFor(int width, int height) -> Canvas;

// Where things are at [ms] into the intro on a canvas of [width] x [height], skipped at [skippedAt] ms (negative: not
// skipped). render() draws this; the tests check the choreography by it.
struct Scene {
  float spin = 0;        // how far Mars has turned, in radians
  float marsX = 0;       // Mars's middle and radius, in canvas pixels
  float marsY = 0;
  float marsRadius = 0;
  int marsBlur = 0;      // how far Mars is blurred, in canvas pixels, as the camera pulls focus onto the moon
  float marsDim = 0;     // how much darker it gets meanwhile, 0 to 1
  int moonX = 0;         // the moon's top left and size: its art scaled, the art's own size once it's in focus
  int moonY = 0;
  int moonWidth = 0;
  int moonHeight = 0;
  int moonBlur = 0;      // how far the moon is blurred: out of focus in the distance, until the camera pulls focus
  bool moonInFront = true;  // in front of Mars, or behind it
  float starZoom = 1;    // the stars spread from the moon's final middle as the camera moves in
  int wordX = 0;         // "phobos"'s top left, below the moon in focus
  int wordY = 0;
  float wordShown = 0;   // how much of the word is drawn in, 0 to 1
  float glint = -1;      // a glint's way across the logo, 0 to 1 (negative: none)
  float fade = 1;        // the opening fade from black, 0 to 1
  float dissolve = 0;    // how much has dissolved into the Library, 0 to 1
};
auto sceneAt(int width, int height, int ms, int skippedAt) -> Scene;

// Draws the intro at [ms] into [pixels]: [width] x [height] RGBA bytes, rows [stride] bytes apart. Every pixel is
// opaque but those the closing dissolve has let the Library through, which are 0 in all four bytes. A skip at
// [skippedAt] ms (negative: none) hurries the dissolve along. Returns false once everything has dissolved: the
// intro is over.
auto render(std::uint8_t* pixels, int width, int height, int stride, int ms, int skippedAt) -> bool;

}
