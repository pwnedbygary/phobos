//sceJpeg (ares/psp/kernel/jpeg.cpp): the library's context, pictures decoded into YCbCr planes and into pixels, and
//planes converted into pixels, checked against what pspautotests' jpeg tests recorded on a PSP (init, create,
//delete, finish, getoutputinfo, decode, decodes, decodeycbcr, decodeycbcrs, csc, mjpegcsc): the results they print,
//what they print of the buffers written, and which calls let other threads run, for pictures made here of their
//pictures' sizes (jpeg-maker.hpp); not where the PSP answered arguments games never pass in ways nothing explains
//(jpeg.cpp lists them). With PSP_AUTOTESTS naming a pspautotests checkout, its four pictures are decoded too.
#include "kernel-machine.hpp"
#include "jpeg-maker.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Data = 0x0894'0000, Out = 0x0898'0000, Planes = 0x08b0'0000;
constexpr u32 NotJpeg = 0x8065'0023, BadMarkerLength = 0x8065'0004, BadSampling = 0x8065'0016;
constexpr u32 BadSize = 0x8065'0020, WrongState = 0x8065'0039, NullBuffer = 0x8065'0051;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

auto put(KernelMachine& m, const std::vector<u8>& bytes, u32 at = Data) -> u32 {
  m.system.memory.copyIn(at, bytes.data(), bytes.size());
  return bytes.size();
}

//A picture whose 8x8 blocks are each one value, a plane's own pattern of them: its DCT has a DC term only, so it
//decodes to its very samples.
auto flat(u32 width, u32 height, u32 restart = 0, bool tables = true) -> jpeg_maker::Picture {
  jpeg_maker::Picture p;
  p.width = width, p.height = height, p.restart = restart, p.tables = tables;
  u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
  p.y.resize(width * height), p.cb.resize(half * halfHeight), p.cr.resize(half * halfHeight);
  for(u32 y = 0; y < height; y++) for(u32 x = 0; x < width; x++) p.y[y * width + x] = (x / 8 * 37 + y / 8 * 91) % 256;
  for(u32 y = 0; y < halfHeight; y++) {
    for(u32 x = 0; x < half; x++) {
      p.cb[y * half + x] = (x / 8 * 53 + y / 8 * 17 + 40) % 256;
      p.cr[y * half + x] = 255 - (x / 8 * 29 + y / 8 * 71) % 256;
    }
  }
  return p;
}

//A picture of slopes and stripes: real AC terms, which decode to its samples give or take rounding.
auto sloped(u32 width, u32 height, u32 quantizer = 1) -> jpeg_maker::Picture {
  jpeg_maker::Picture p;
  p.width = width, p.height = height, p.quantizer = quantizer;
  u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
  p.y.resize(width * height), p.cb.resize(half * halfHeight), p.cr.resize(half * halfHeight);
  for(u32 y = 0; y < height; y++) {
    for(u32 x = 0; x < width; x++) p.y[y * width + x] = (x * 255 / std::max(1u, width - 1) + (y % 4) * 9) % 256;
  }
  for(u32 y = 0; y < halfHeight; y++) {
    for(u32 x = 0; x < half; x++) p.cb[y * half + x] = 60 + y * 3 % 120, p.cr[y * half + x] = 200 - x * 2 % 150;
  }
  return p;
}

//The planes as the library writes them: Y, then Cb, then Cr.
auto planar(const jpeg_maker::Picture& p) -> std::vector<u8> {
  std::vector<u8> out = p.y;
  out.insert(out.end(), p.cb.begin(), p.cb.end());
  out.insert(out.end(), p.cr.begin(), p.cr.end());
  return out;
}

//sceJpegMJpegCsc's sums (jpeg.cpp), which decoding into pixels uses too, worked out again here
auto enginePixel(s32 y, s32 cb, s32 cr) -> u32 {
  s32 d = std::max(cb - 128, -127), e = std::max(cr - 128, -127);
  s32 r = std::clamp(y + ((359 * e + 128) >> 8), 0, 255), g = std::clamp(y + ((-88 * d - 182 * e + 128) >> 8), 0, 255);
  s32 b = std::clamp(y + ((454 * d + 128) >> 8), 0, 255);
  return r | g << 8 | b << 16;
}

//What the recordings print of a buffer filled with 0xcc first ("wrote %d bytes, %d per row"): the bytes up to the
//last word written, over its first count words, and up to the first word not, looked for among as many words as
//that count of bytes (as the tests' loop compares them).
auto wrote(KernelMachine& m, u32 address, u32 count) -> std::pair<u32, u32> {
  u32 last = 0, first = 0;
  for(u32 n = count + 1; n-- > 0;) if(word(m, address + n * 4) != 0xcccc'cccc) { last = (n + 1) * 4; break; }
  for(u32 n = 0; n < last; n++) if(word(m, address + n * 4) == 0xcccc'cccc) { first = n * 4; break; }
  return {last, first};
}

//What csc's and mjpegcsc's patterns print: each run of equal pixels, " first-last pixel".
auto runs(KernelMachine& m, u32 address, u32 count) -> std::string {
  std::string text;
  u32 start = 0, last = word(m, address);
  char line[40];
  for(u32 n = 1; n <= count; n++) {
    u32 pixel = n < count ? word(m, address + n * 4) : ~last;
    if(pixel == last) continue;
    std::snprintf(line, sizeof(line), " %u-%u %08x", start, n - 1, last);
    text += line;
    start = n, last = pixel;
  }
  return text;
}

auto bytes(std::initializer_list<u8> list) -> std::vector<u8> { return list; }
}

//The library's context as init, create, delete and finish recorded: starting twice refused (0x80650042), a context
//made and taken away only with the library started (0x80650039 otherwise, and for a second one), up to 1024 pixels
//wide compared signed (create's widths), any height, and the library ended only with no context made.
static auto jpegContext() -> void {
  KernelMachine m;
  CHECK(m.call("sceJpegCreateMJpeg", {480, 272}), WrongState);
  CHECK(m.call("sceJpegDeleteMJpeg", {}), WrongState);
  CHECK(m.call("sceJpegFinishMJpeg", {}), WrongState);
  CHECK(m.call("sceJpegInitMJpeg", {}), 0);
  CHECK(m.call("sceJpegInitMJpeg", {}), 0x8065'0042);
  CHECK(m.call("sceJpegDeleteMJpeg", {}), WrongState);
  CHECK(m.call("sceJpegCreateMJpeg", {480, 272}), 0);
  CHECK(m.call("sceJpegCreateMJpeg", {480, 272}), WrongState);
  CHECK(m.call("sceJpegFinishMJpeg", {}), WrongState);  //in use
  CHECK(m.call("sceJpegDeleteMJpeg", {}), 0);
  CHECK(m.call("sceJpegDeleteMJpeg", {}), WrongState);
  for(u32 size : {0x8000'0000u, 0xffff'0000u, 0xffff'8000u, 0xffff'ffffu, 0u, 1u, 2u, 3u, 4u, 5u, 480u, 512u, 1023u,
                  1024u, 1025u, 2048u, 0x8000u, 0x7fff'ffffu}) {
    bool wide = s32(size) > 1024;
    CHECK(m.call("sceJpegCreateMJpeg", {size, 272}), wide ? BadSize : 0);
    CHECK(m.call("sceJpegDeleteMJpeg", {}), wide ? WrongState : 0);
    CHECK(m.call("sceJpegCreateMJpeg", {480, size}), 0);
    CHECK(m.call("sceJpegDeleteMJpeg", {}), 0);
  }
  CHECK(m.call("sceJpegCreateMJpeg", {1024, 1024}), 0);
  CHECK(m.call("sceJpegDeleteMJpeg", {}), 0);
  CHECK(m.call("sceJpegFinishMJpeg", {}), 0);
  CHECK(m.call("sceJpegFinishMJpeg", {}), WrongState);
  CHECK(m.call("sceJpegInitMJpeg", {}), 0);
  CHECK(m.notes.size(), 0);
}

//Pictures decoded: sceJpegGetOutputInfo's size and colour information, sceJpegDecodeMJpegYCbCr's planes (a picture
//of flat blocks to the sample, at sizes that fill no MCU, part of one and many, with and without Annex K's tables in
//it and with restart markers; a picture of slopes and stripes to within 2 of each sample) and sceJpegDecodeMJpeg's
//pixels, a row of the picture to a row of the context's width (decode's 480x272 in a context 512 wide: 1920 bytes
//a row, the 32 pixels after them untouched), each sceJpegMJpegCsc's pixel of the planes. Each gives back its
//picture's width << 16 | height.
static auto jpegDecoded() -> void {
  for(auto picture : {flat(480, 272), flat(16, 16), flat(37, 23), flat(1, 1), flat(160, 48, 3), flat(48, 32, 1, false),
                      flat(64, 16, 2, false)}) {
    KernelMachine m;
    auto file = picture.make();
    u32 size = put(m, file);
    auto planes = planar(picture);
    u32 result = picture.width << 16 | picture.height;
    m.system.memory.write(4, R, 0xcccc'cccc);
    CHECK(m.call("sceJpegGetOutputInfo", {Data, size, R, 0}), planes.size());
    CHECK(word(m, R), 0x0002'0202);
    CHECK(m.call("sceJpegGetOutputInfo", {Data, size, 0, 0}), planes.size());  //no colour information wanted
    m.system.memory.fill(Planes, 0xcc, planes.size() + 4);
    CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size, Planes, u32(planes.size()), 0}), result);
    std::vector<u8> decoded(planes.size());
    m.system.memory.copyOut(decoded.data(), Planes, decoded.size());
    CHECK(decoded == planes, true);
    CHECK(word(m, Planes + planes.size()), 0xcccc'cccc);
    m.system.memory.fill(Planes, 0xcc, planes.size());
    CHECK(m.call("sceJpegDecodeMJpegYCbCrSuccessively", {Data, size, Planes, u32(planes.size()), 1}), result);
    m.system.memory.copyOut(decoded.data(), Planes, decoded.size());
    CHECK(decoded == planes, true);
    //into pixels, a context 32 pixels wider than the picture
    u32 stride = picture.width + 32;
    CHECK(m.call("sceJpegInitMJpeg", {}), 0);
    CHECK(m.call("sceJpegCreateMJpeg", {stride, picture.height}), 0);
    for(auto function : {"sceJpegDecodeMJpeg", "sceJpegDecodeMJpegSuccessively"}) {
      m.system.memory.fill(Out, 0xcc, stride * picture.height * 4);
      CHECK(m.call(function, {Data, size, Out, 0}), result);
      bool right = true;
      u32 half = (picture.width + 1) / 2;
      for(u32 y = 0; y < picture.height; y++) {
        for(u32 x = 0; x < picture.width; x++) {
          u32 c = y / 2 * half + x / 2;
          right &= word(m, Out + (y * stride + x) * 4) == enginePixel(picture.y[y * picture.width + x], picture.cb[c],
                                                                      picture.cr[c]);
        }
        right &= word(m, Out + (y * stride + picture.width) * 4) == 0xcccc'cccc;
      }
      CHECK(right, true);
    }
    CHECK(m.notes.size(), 0);
  }
  //slopes and stripes, and the same coarsely quantized: within 2 of each sample, then within 24
  for(u32 quantizer : {1, 12}) {
    KernelMachine m;
    auto picture = sloped(96, 40, quantizer);
    u32 size = put(m, picture.make());
    auto planes = planar(picture);
    CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size, Planes, u32(planes.size()), 0}), 96 << 16 | 40);
    s32 worst = 0;
    for(u32 n = 0; n < planes.size(); n++) {
      worst = std::max(worst, std::abs(s32(m.system.memory.read(1, Planes + n)) - s32(planes[n])));
    }
    CHECK(worst <= (quantizer == 1 ? 2 : 24), true);
    if(worst > (quantizer == 1 ? 2 : 24)) std::printf("  quantizer %u: %d off\n", quantizer, worst);
  }
  //a picture of noise, more than the 64 KiB of data first read, given as far more (a whole buffer of pictures' size)
  KernelMachine m;
  jpeg_maker::Picture picture;
  picture.width = 320, picture.height = 192;
  picture.y.resize(320 * 192), picture.cb.resize(160 * 96), picture.cr.resize(160 * 96);
  u32 seed = 1;
  for(auto plane : {&picture.y, &picture.cb, &picture.cr}) {
    for(auto& sample : *plane) seed = seed * 1'103'515'245 + 12'345, sample = 32 + (seed >> 16) % 192;
  }
  u32 size = put(m, picture.make());
  CHECK(size > 0x1'0000 && size < 0x4'0000, true);
  auto planes = planar(picture);
  CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size + 0x10'0000, Planes, u32(planes.size()), 0}), 320 << 16 | 192);
  s32 worst = 0;
  for(u32 n = 0; n < planes.size(); n++) {
    worst = std::max(worst, std::abs(s32(m.system.memory.read(1, Planes + n)) - s32(planes[n])));
  }
  CHECK(worst <= 2, true);
  if(size <= 0x1'0000 || size >= 0x4'0000 || worst > 2) std::printf("  noise: %u bytes, %d off\n", size, worst);
}

//What every decoding function refuses, as getoutputinfo, decode, decodes, decodeycbcr and decodeycbcrs recorded:
//data reaching the kernel's addresses (0x7fffffff and 0x80000000 bytes: PRIV_REQUIRED), no data (0x80650023; for
//sceJpegDecodeMJpegYCbCr 0x80650051), none at all (0x80650004), one byte or data that doesn't start with SOI
//(0x80650023), and grey pictures or colours at half the width only (0x80650016). Then decoding into pixels: no
//buffer (INVALID_POINTER), a picture wider or taller than the context (0x80650020, before the library starts and
//before a context is made too, its size then 0 by 0; contexts of 512x16, 480x271, 479x272, 1024x271, 0x0 and -1x-1 for
//a 480x272 picture; 480x273 and 1024x272 taken), and the library not started (NOT_INITIALIZED, the context's size
//kept from before it ended); a context deleted keeps its size for decoding. Into planes: a buffer reaching the
//kernel's addresses (PRIV_REQUIRED), one too small (0x80650041, for 195839 and 195824 bytes of 480x272's 195840, a
//null one too), and a null one with room enough (taken by sceJpegDecodeMJpegYCbCr, INVALID_POINTER from its sibling).
static auto jpegRefused() -> void {
  KernelMachine m;
  auto picture = flat(480, 272);
  u32 size = put(m, picture.make());
  m.system.memory.fill(Data - 0x100, 0, 0x100);
  auto grey = bytes({0xff, 0xd8, 0xff, 0xc0, 0x00, 0x0b, 8, 0x01, 0x10, 0x01, 0xe0, 1, 1, 0x11, 0, 0xff, 0xd9});
  auto halfWidth = picture.make();
  for(u32 n = 0; n + 12 < halfWidth.size(); n++) {  //SOF0's Y sampling, 2x2 made 2x1
    if(halfWidth[n] == 0xff && halfWidth[n + 1] == 0xc0) { halfWidth[n + 11] = 0x21; break; }
  }
  constexpr u32 Grey = 0x0896'0000, HalfWidth = 0x0897'0000, Zeros = 0x0896'8000;
  u32 greySize = put(m, grey, Grey), halfSize = put(m, halfWidth, HalfWidth);
  m.system.memory.fill(Zeros, 0, 32);
  struct Case { u32 data, size, error, ycbcrError; };
  const Case cases[] = {{0, 0, NotJpeg, NullBuffer}, {Zeros, 32, NotJpeg, NotJpeg},
                        {Data, 0, BadMarkerLength, BadMarkerLength}, {Data, 1, NotJpeg, NotJpeg},
                        {Data, 0x7fff'ffff, 0x8000'0023, 0x8000'0023}, {Data, 0x8000'0000, 0x8000'0023, 0x8000'0023},
                        {Data - 1, size + 1, NotJpeg, NotJpeg}, {Grey, greySize, BadSampling, BadSampling},
                        {HalfWidth, halfSize, BadSampling, BadSampling}};
  m.call("sceJpegInitMJpeg", {});
  m.call("sceJpegCreateMJpeg", {512, 272});
  for(auto [data, length, error, ycbcrError] : cases) {
    m.system.memory.write(4, R, 0xcccc'cccc);
    CHECK(m.call("sceJpegGetOutputInfo", {data, length, R, 0}), error);
    CHECK(word(m, R), 0xcccc'cccc);
    CHECK(m.call("sceJpegDecodeMJpeg", {data, length, Out, 0}), error);
    CHECK(m.call("sceJpegDecodeMJpegSuccessively", {data, length, Out, 0}), error);
    CHECK(m.call("sceJpegDecodeMJpegYCbCr", {data, length, Planes, 0x10'0000, 0}), ycbcrError);
    CHECK(m.call("sceJpegDecodeMJpegYCbCrSuccessively", {data, length, Planes, 0x10'0000, 0}), error);
  }
  CHECK(m.call("sceJpegDecodeMJpeg", {Data, size, 0, 0}), 0x8000'0103);
  //(decode's frames: the bytes written as it prints them, looking over 512x512 words)
  struct Context { u32 width, height; bool fits; u32 bytes, row; };
  const Context contexts[] = {{512, 16, false, 0, 0}, {480, 271, false, 0, 0}, {480, 272, true, 522240, 522240},
                              {480, 273, true, 522240, 522240}, {479, 272, false, 0, 0}, {1024, 271, false, 0, 0},
                              {1024, 272, true, 1048580, 1920}, {0, 0, false, 0, 0}, {u32(-1), u32(-1), false, 0, 0}};
  for(auto [width, height, fits, bytes, row] : contexts) {
    m.call("sceJpegDeleteMJpeg", {});
    m.call("sceJpegCreateMJpeg", {width, height});
    m.system.memory.fill(Out, 0xcc, 0x10'0010);
    CHECK(m.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), fits ? 0x01e0'0110 : BadSize);
    CHECK(wrote(m, Out, 512 * 512) == std::make_pair(bytes, row), true);
  }
  //the mode changes nothing (decode's, decodeycbcr's and getoutputinfo's 1, 2, 3, 999 and -1)
  m.call("sceJpegDeleteMJpeg", {});
  m.call("sceJpegCreateMJpeg", {512, 272});
  auto planes = planar(picture);
  for(u32 mode : {1u, 2u, 3u, 999u, u32(-1)}) {
    m.system.memory.fill(Out, 0xcc, 0x10'0010);
    CHECK(m.call("sceJpegDecodeMJpeg", {Data, size, Out, mode}), 0x01e0'0110);
    CHECK(wrote(m, Out, 512 * 512) == std::make_pair(556928u, 1920u), true);
    m.system.memory.write(4, R, 0xcccc'cccc);
    CHECK(m.call("sceJpegGetOutputInfo", {Data, size, R, mode}), 195840);
    CHECK(word(m, R), 0x0002'0202);
    m.system.memory.fill(Planes, 0xcc, 195844);
    CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size, Planes, 195840, mode}), 0x01e0'0110);
    std::vector<u8> decoded(planes.size());
    m.system.memory.copyOut(decoded.data(), Planes, decoded.size());
    CHECK(decoded == planes && word(m, Planes + 195840) == 0xcccc'cccc, true);
  }
  KernelMachine fresh;
  put(fresh, picture.make());
  CHECK(fresh.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), BadSize);  //before the library starts
  fresh.call("sceJpegInitMJpeg", {});
  CHECK(fresh.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), BadSize);  //and before a context
  fresh.call("sceJpegCreateMJpeg", {512, 272});
  CHECK(fresh.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), 0x01e0'0110);
  fresh.call("sceJpegDeleteMJpeg", {});
  CHECK(fresh.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), 0x01e0'0110);  //the deleted context's size
  fresh.call("sceJpegFinishMJpeg", {});
  CHECK(fresh.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), 0x8000'0001);
  //into planes: no library needed
  CHECK(fresh.call("sceJpegDecodeMJpegYCbCr", {Data, size, Planes, 195840, 0}), 0x01e0'0110);
  for(auto function : {"sceJpegDecodeMJpegYCbCr", "sceJpegDecodeMJpegYCbCrSuccessively"}) {
    CHECK(m.call(function, {Data, size, Planes, u32(-1), 0}), 0x8000'0023);
    CHECK(m.call(function, {Data, size, Planes, 195839, 0}), 0x8065'0041);
    CHECK(m.call(function, {Data, size, Planes, 195824, 0}), 0x8065'0041);
    CHECK(m.call(function, {Data, size, 0, 195839, 0}), 0x8065'0041);
    CHECK(m.call(function, {Data, size, Planes, 195840, 0}), 0x01e0'0110);
  }
  CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size, 0, 0x10'0000, 0}), 0x01e0'0110);
  CHECK(m.call("sceJpegDecodeMJpegYCbCrSuccessively", {Data, size, 0, 0x10'0000, 0}), 0x8065'0010);
  CHECK(m.notes.size(), 0);
}

//sceJpegCsc as csc recorded: no need of the library; no buffer 0x80650051; colour information of two planes at
//2x2, 2x1 or 1x1 only (grey, 1x2, 3x3, 4x4 and 0xfffa0202's top 13 bits: 0x80650013); pixels a row of the picture to
//a row of the buffer, the rows overlapping when the buffer is narrower and a height of 0 one row and -1; the
//strides' bytes wrapping round (0x80000000: one place); the patterns' and the colours' pixels, each line as csc
//printed it.
static auto jpegCsc() -> void {
  KernelMachine m;
  constexpr u32 Frame = 0x0940'0000, Source = 0x0900'0000;
  m.system.memory.fill(Source, 0, 0x10'0000);
  auto convert = [&](u32 size, u32 stride, u32 colour, u32 count = 0x10'0000) {
    m.system.memory.fill(Frame, 0xcc, count * 4 + 4);
    u32 result = m.call("sceJpegCsc", {Frame, Source, size, stride, colour});
    return std::make_tuple(result, wrote(m, Frame, count));
  };
  using Wrote = std::pair<u32, u32>;
  CHECK(convert(0x0010'0010, 16, 0x0002'0202) == std::make_tuple(0u, Wrote{1024, 1024}), true);
  CHECK(m.call("sceJpegCsc", {0, Source, 0x0010'0010, 16, 0x0002'0202}), NullBuffer);
  CHECK(m.call("sceJpegCsc", {Frame, 0, 0x0010'0010, 16, 0x0002'0202}), NullBuffer);
  struct Size { u32 size, stride; u32 result; Wrote wrote; };
  const Size sizes[] = {
    {0x0020'0020, 0x10, 0, {2112, 2112}}, {0x0010'0020, 0x20, 0, {4032, 64}}, {0x0555'0555, 0x10, 0, {92756, 92756}},
    {0xffff'0001, 0x10, 0, {262140, 262140}}, {0x0007'0010, 0x10, 0, {988, 28}},
    {0x0008'ffff, 0x08, 0, {2097120, 2097120}}, {0x0008'0000, 0x08, u32(-1), {32, 32}},
    {0x0008'0001, 0x08, 0, {32, 32}}, {0x0010'0010, 0, 0, {64, 64}}, {0x0010'0010, 1, 0, {124, 124}},
    {0x0010'0002, 0x7ffff, 0, {2097212, 64}}, {0x0010'0002, 0x8000'0000, 0, {64, 64}}};
  for(auto [size, stride, result, written] : sizes) {
    CHECK(convert(size, stride, 0x0002'0202) == std::make_tuple(result, written), true);
  }
  for(u32 colour : {0x0001'0101u, 0x0002'0102u, 0x0002'0303u, 0x0002'0404u, 0xfffa'0202u}) {
    CHECK(m.call("sceJpegCsc", {Frame, Source, 0x0010'0010, 16, colour}), 0x8065'0013);
  }
  CHECK(convert(0x0010'0010, 16, 0x0002'0101) == std::make_tuple(0u, Wrote{1024, 1024}), true);
  CHECK(convert(0x0010'0010, 16, 0x0002'0201) == std::make_tuple(0u, Wrote{1024, 1024}), true);
  //the patterns: Y, Cb and Cr all 0xff but for the last of one of them
  struct Pattern { u32 colour, y, cb, cr; const char* printed; };
  const Pattern patterns[] = {
    {0x0002'0202, 255, 64, 64, " 0-254 00ff79ff 255-255 00e100b2"},
    {0x0002'0202, 256, 63, 64, " 0-237 00ff79ff 238-239 001cd0ff 240-253 00ff79ff 254-255 001cd0ff"},
    {0x0002'0202, 256, 64, 63, " 0-237 00ff79ff 238-239 00ffff4c 240-253 00ff79ff 254-255 00ffff4c"},
    {0x0002'0201, 255, 128, 128, " 0-254 00ff79ff 255-255 00e100b2"},
    {0x0002'0201, 256, 127, 128, " 0-253 00ff79ff 254-255 001cd0ff"},
    {0x0002'0201, 256, 128, 127, " 0-253 00ff79ff 254-255 00ffff4c"},
    {0x0002'0101, 255, 256, 256, " 0-254 00ff79ff 255-255 00e100b2"},
    {0x0002'0101, 256, 255, 256, " 0-254 00ff79ff 255-255 001cd0ff"},
    {0x0002'0101, 256, 256, 255, " 0-254 00ff79ff 255-255 00ffff4c"}};
  for(auto [colour, y, cb, cr, printed] : patterns) {
    u32 colours = colour == 0x0002'0101 ? 256 : colour == 0x0002'0201 ? 128 : 64;
    m.system.memory.fill(Source, 0, 768);
    m.system.memory.fill(Source, 0xff, y);
    m.system.memory.fill(Source + 256, 0xff, cb);
    m.system.memory.fill(Source + 256 + colours, 0xff, cr);
    CHECK(m.call("sceJpegCsc", {Frame, Source, 0x0010'0010, 16, colour}), 0);
    CHECK(runs(m, Frame, 256) == printed, true);
  }
  //the colours: one pixel's Y, Cb and Cr
  struct Colour { u32 y, cb, cr, pixel; };
  const Colour colours[] = {{0, 128, 128, 0}, {255, 128, 128, 0xffffff}, {255, 0, 128, 0x1cffff},
                            {255, 1, 128, 0x1effff}, {255, 254, 128, 0xffd4ff}, {255, 255, 128, 0xffd3ff},
                            {255, 128, 0, 0xffff4c}, {255, 128, 1, 0xffff4d}, {255, 128, 254, 0xffa5ff},
                            {255, 128, 255, 0xffa4ff}};
  for(u32 colour : {0x0002'0202u, 0x0002'0201u, 0x0002'0101u}) {
    u32 crAt = 256 + (colour == 0x0002'0202 ? 64 : colour == 0x0002'0201 ? 128 : 256);
    for(auto [y, cb, cr, pixel] : colours) {
      m.system.memory.fill(Source, 0, 768);
      m.system.memory.write(1, Source, y);
      m.system.memory.write(1, Source + 256, cb);
      m.system.memory.write(1, Source + crAt, cr);
      CHECK(m.call("sceJpegCsc", {Frame, Source, 0x0010'0010, 16, colour}), 0);
      CHECK(word(m, Frame), pixel);
    }
  }
  CHECK(m.notes.size(), 0);
}

//sceJpegMJpegCsc as mjpegcsc recorded: the library started (NOT_INITIALIZED before it and after it ends), a
//destination given (INVALID_POINTER), a null source read as zeros (its pixel 0x00008600), at most 720 by 480 with a
//buffer width of 1024 (0x80650020), rows as sceJpegCsc's, a height of 0 one row (and 0), the Media Engine's sums for
//the colours, and its transfers 8 bytes at a time: mjpegcsc's patterns from planes 4 bytes past a multiple of 8,
//with the bytes before its buffer given the PSP's, and a buffer width of 1, its rows every other one misaligned.
static auto jpegMJpegCsc() -> void {
  KernelMachine m;
  constexpr u32 Frame = 0x0940'0000, Source = 0x0900'0000, Misaligned = 0x0920'0004;
  m.system.memory.fill(Source, 0, 0x20'0000);
  using Wrote = std::pair<u32, u32>;
  auto convert = [&](u32 source, u32 size, u32 stride, u32 count = 0x10'0000) {
    m.system.memory.fill(Frame, 0xcc, count * 4 + 4);
    u32 result = m.call("sceJpegMJpegCsc", {Frame, source, size, stride});
    return std::make_tuple(result, wrote(m, Frame, count));
  };
  CHECK(m.call("sceJpegMJpegCsc", {Frame, Source, 0x0010'0010, 16}), 0x8000'0001);
  m.call("sceJpegInitMJpeg", {});
  CHECK(convert(Source, 0x0010'0010, 16) == std::make_tuple(0u, Wrote{1024, 1024}), true);
  CHECK(m.call("sceJpegMJpegCsc", {0, Source, 0x0010'0010, 16}), 0x8000'0103);
  CHECK(convert(0, 0x0010'0010, 16) == std::make_tuple(0u, Wrote{1024, 1024}), true);
  CHECK(word(m, Frame), 0x0000'8600);
  struct Size { u32 size, stride; u32 result; Wrote wrote; };
  const Size sizes[] = {
    {0x0020'0020, 0x10, 0, {2112, 2112}}, {0x0010'0020, 0x20, 0, {4032, 64}},
    {0x0555'0555, 0x10, BadSize, {0, 0}}, {0xffff'0001, 0x10, BadSize, {0, 0}}, {721 << 16 | 1, 0x10, BadSize, {0, 0}},
    {720 << 16 | 32, 0x10, 0, {4864, 4864}}, {720 << 16 | 16, 0x10, 0, {3840, 3840}},
    {0x0008'ffff, 0x08, BadSize, {0, 0}}, {0x0008'0000 | 481, 0x08, BadSize, {0, 0}},
    {0x0010'0000, 0x10, 0, {64, 64}}, {0x0010'0010, 0, 0, {64, 64}}, {0x0010'0010, 1, 0, {120, 120}},
    {0x0010'0010, 1024, 0, {61504, 64}}, {0x0010'0010, 1025, BadSize, {0, 0}},
    {0x0010'0002, 0x7fff'ffff, BadSize, {0, 0}}, {0x0010'0002, 0x8000'0000, 0, {64, 64}}};
  for(auto [size, stride, result, written] : sizes) {
    CHECK(convert(Source, size, stride) == std::make_tuple(result, written), true);
  }
  //a buffer width of 1 again, every pixel its own: each misaligned row written to its 8-byte groups, each 8 bytes'
  //halves the other way round (the patterns' transfers, worked out here byte by byte)
  for(u32 n = 0; n < 256; n++) m.system.memory.write(1, Source + n, n);
  m.system.memory.fill(Source + 256, 128, 128);
  m.system.memory.fill(Frame, 0xcc, 256);
  CHECK(m.call("sceJpegMJpegCsc", {Frame, Source, 0x0010'0010, 1}), 0);
  std::vector<u8> expected(256, 0xcc);
  for(u32 y = 0; y < 16; y++) {
    for(u32 n = 0; n < 64; n++) expected[(n & ~7u) + ((4 * y + n) & 7) + (4 * y & ~7u)] =
      enginePixel(y * 16 + n / 4, 128, 128) >> n % 4 * 8;
  }
  bool same = true;
  for(u32 n = 0; n < 256; n++) same &= m.system.memory.read(1, Frame + n) == expected[n];
  CHECK(same, true);
  m.system.memory.fill(Source, 0, 384);
  //the patterns, 480x272 planes from 4 bytes past a multiple of 8, the bytes before them the PSP's
  struct Pattern { u32 y, cb, cr; const char* printed; };
  const Pattern patterns[] = {
    {256, 64, 64, " 0-3 00ffffff 4-4 00505050 5-5 00575757 6-6 009b9b9b 7-7 00080808 8-15 00ffd3ff 16-487 00ffffff "
                  "488-495 00ffd3ff 496-130559 00ffffff"},
    {255, 64, 64, " 0-3 00ffffff 4-4 00505050 5-5 00575757 6-6 009b9b9b 7-7 00080808 8-13 00ffd3ff 14-15 001effff "
                  "16-487 00ffffff 488-493 00ffd3ff 494-495 001effff 496-130559 00ffffff"},
    {256, 63, 64, " 0-3 00ffffff 4-4 00505050 5-5 00575757 6-6 009b9b9b 7-7 00080808 8-13 00ffd3ff 14-15 00ffff4d "
                  "16-487 00ffffff 488-493 00ffd3ff 494-495 00ffff4d 496-130559 00ffffff"},
    {256, 64, 63, " 0-3 00ffffff 4-4 00505050 5-5 00575757 6-6 009b9b9b 7-7 00080808 8-15 00ffd3ff 16-487 00ffffff "
                  "488-495 00ffd3ff 496-130559 00ffffff"}};
  constexpr u32 Luma = 480 * 272, Colours = Luma / 4;
  for(auto [y, cb, cr, printed] : patterns) {
    m.system.memory.fill(Misaligned, 0, Luma + Colours * 2 + 64);
    m.system.memory.write(4, Misaligned - 4, 0x089b'5750);  //(the recording's word before its buffer)
    m.system.memory.fill(Misaligned, 0xff, Luma - 256 + y);
    m.system.memory.fill(Misaligned + Luma, 0x80, Colours - 64 + cb);
    m.system.memory.fill(Misaligned + Luma + Colours, 0x80, Colours - 64 + cr);
    m.system.memory.fill(Frame, 0xcc, Luma * 4);
    CHECK(m.call("sceJpegMJpegCsc", {Frame, Misaligned, 480 << 16 | 272, 480}), 0);
    CHECK(runs(m, Frame, Luma) == printed, true);
  }
  struct Colour { u32 y, cb, cr, pixel; };
  const Colour colours[] = {{0, 128, 128, 0}, {255, 128, 128, 0xffffff}, {255, 0, 128, 0x1effff},
                            {255, 1, 128, 0x1effff}, {255, 254, 128, 0xffd4ff}, {255, 255, 128, 0xffd3ff},
                            {255, 128, 0, 0xffff4d}, {255, 128, 1, 0xffff4d}, {255, 128, 254, 0xffa5ff},
                            {255, 128, 255, 0xffa5ff}};
  for(auto [y, cb, cr, pixel] : colours) {
    m.system.memory.fill(Source, 0, 768);
    m.system.memory.write(1, Source, y);
    m.system.memory.write(1, Source + 256, cb);
    m.system.memory.write(1, Source + 256 + 64, cr);
    CHECK(m.call("sceJpegMJpegCsc", {Frame, Source, 0x0010'0010, 16}), 0);
    CHECK(word(m, Frame), pixel);
  }
  m.call("sceJpegFinishMJpeg", {});
  CHECK(m.call("sceJpegMJpegCsc", {Frame, Source, 0x0010'0010, 16}), 0x8000'0001);
  CHECK(m.notes.size(), 0);
}

//Which calls let other threads run, as the tests' checkpoints saw (a thread of the caller's priority, started just
//before each call, runs during it only if the caller waits): starting and ending the library, the decoding functions
//and sceJpegGetOutputInfo once the buffers' addresses have passed (whatever the picture), the conversions when they
//convert (sceJpegMJpegCsc 16 rows or more); not the context's calls or what's refused before. On both engines.
static auto jpegWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto picture = flat(480, 272);
    u32 size = put(m, picture.make());
    constexpr u32 Flag = R + 0x800, Frame = Out;
    Assembler other{m, 0x0880'2000};
    other.li(t0, Flag); other.li(t1, 1); other.put(sw(t1, 0, t0)); other.put(jr(ra)); other.put(nop);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("other")); main.li(a1, 0x0880'2000); main.li(a2, 0x20); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    struct Call { const char* function; std::vector<u32> arguments; u32 result; bool waits; };
    const std::vector<Call> calls = {
      {"sceJpegInitMJpeg", {}, 0, true}, {"sceJpegInitMJpeg", {}, 0x8065'0042, false},
      {"sceJpegCreateMJpeg", {512, 272}, 0, false}, {"sceJpegGetOutputInfo", {Data, size, 0, 0}, 195840, true},
      {"sceJpegGetOutputInfo", {Data, 0x8000'0000, 0, 0}, 0x8000'0023, false},
      {"sceJpegGetOutputInfo", {0, 0, 0, 0}, NotJpeg, true},
      {"sceJpegDecodeMJpeg", {Data, size, 0, 0}, 0x8000'0103, true},
      {"sceJpegDecodeMJpeg", {Data, size, Frame, 0}, 0x01e0'0110, true},
      {"sceJpegDecodeMJpegYCbCr", {0, 0, Planes, 195840, 0}, NullBuffer, false},
      {"sceJpegDecodeMJpegYCbCr", {Data, size, Planes, u32(-1), 0}, 0x8000'0023, false},
      {"sceJpegDecodeMJpegYCbCr", {Data, 0, Planes, 195840, 0}, BadMarkerLength, true},
      {"sceJpegDecodeMJpegYCbCrSuccessively", {0, 0, Planes, 195840, 0}, NotJpeg, true},
      {"sceJpegCsc", {Frame, Planes, 0x0010'0010, 16, 0x0002'0202}, 0, true},
      {"sceJpegCsc", {Frame, Planes, 0x0008'0000, 8, 0x0002'0202}, u32(-1), true},
      {"sceJpegCsc", {0, Planes, 0x0010'0010, 16, 0x0002'0202}, NullBuffer, false},
      {"sceJpegCsc", {Frame, Planes, 0x0010'0010, 16, 0x0001'0101}, 0x8065'0013, false},
      {"sceJpegMJpegCsc", {Frame, Planes, 0x0010'0010, 16}, 0, true},
      {"sceJpegMJpegCsc", {Frame, Planes, 720 << 16 | 8, 16}, 0, false},
      {"sceJpegMJpegCsc", {Frame, Planes, 721 << 16 | 16, 16}, BadSize, false},
      {"sceJpegDeleteMJpeg", {}, 0, false}, {"sceJpegFinishMJpeg", {}, 0, true},
      {"sceJpegFinishMJpeg", {}, WrongState, false}};
    for(u32 n = 0; n < calls.size(); n++) {
      main.li(t0, Flag); main.put(sw(zero, 0, t0));
      main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
      static constexpr u32 Registers[] = {a0, a1, a2, a3, t0, t1};
      for(u32 k = 0; k < calls[n].arguments.size(); k++) main.li(Registers[k], calls[n].arguments[k]);
      main.call(calls[n].function);
      main.li(t0, R + n * 8); main.put(sw(v0, 0, t0));
      main.li(t1, Flag); main.put(lw(t1, 0, t1)); main.put(sw(t1, 4, t0));
      main.put(addu(a0, s0, zero));
      main.call("sceKernelTerminateThread");
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    for(u32 n = 0; n < calls.size(); n++) {
      CHECK(word(m, R + n * 8), calls[n].result);
      CHECK(word(m, R + n * 8 + 4), calls[n].waits);
      if(word(m, R + n * 8 + 4) != calls[n].waits) std::printf("  %s (call %u)\n", calls[n].function, n);
    }
  }
}

//With PSP_AUTOTESTS naming a pspautotests checkout, its pictures as getoutputinfo and decode recorded them:
//background.jpg 480x272 (195840 bytes, 0x00020202), geb.jpg 128x256 (49152), background2x1.jpg and
//backgroundgray.jpg refused (0x80650016).
static auto jpegAutotestsPictures() -> void {
  const char* tests = std::getenv("PSP_AUTOTESTS");
  if(!tests || !*tests) return;
  struct File { const char* name; u32 info, result; };
  for(auto [name, info, result] : {File{"background.jpg", 195840, 0x01e0'0110}, File{"geb.jpg", 49152, 0x0080'0100},
                                   File{"background2x1.jpg", BadSampling, BadSampling},
                                   File{"backgroundgray.jpg", BadSampling, BadSampling}}) {
    std::ifstream stream(std::string(tests) + "/tests/jpeg/" + name, std::ios::binary);
    std::vector<u8> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    CHECK(file.empty(), false);
    if(file.empty()) continue;
    KernelMachine m;
    u32 size = put(m, file);
    CHECK(m.call("sceJpegGetOutputInfo", {Data, size, R, 0}), info);
    CHECK(m.call("sceJpegDecodeMJpegYCbCr", {Data, size, Planes, 0x10'0000, 0}), result);
    m.call("sceJpegInitMJpeg", {});
    m.call("sceJpegCreateMJpeg", {512, 272});
    CHECK(m.call("sceJpegDecodeMJpeg", {Data, size, Out, 0}), result);
  }
}

auto jpegTests() -> Tests {
  return {
    {"jpeg the library's context", jpegContext},
    {"jpeg pictures decoded", jpegDecoded},
    {"jpeg data refused", jpegRefused},
    {"jpeg planes converted", jpegCsc},
    {"jpeg planes converted on the Media Engine", jpegMJpegCsc},
    {"jpeg calls that wait", jpegWaits},
    {"jpeg pspautotests' pictures", jpegAutotestsPictures},
  };
}

}
