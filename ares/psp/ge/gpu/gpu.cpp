#include "gpu.hpp"
#include "../../memory/memory.hpp"
#include "shaders/shaders.hpp"

#include <chrono>
#include <cmath>
#include <volk.h>

namespace ares::PlayStationPortable {

#include "vulkan.cpp"

//The records' layout: shaders/common.glsl's, word for word (the tests draw every kind of job both ways, so a word
//out of place shows).
namespace Record {
  enum : u32 {
    LookWords = 32,
    LookFlags = 0, LookFormat = 1, LookAlpha = 2, LookColorTest = 3, LookColorReference = 4, LookColorMask = 5,
    LookStencil = 6, LookStencilOps = 7, LookDepthTest = 8, LookBlend = 9, LookFixedA = 10, LookFixedB = 11,
    LookDither = 12, LookLogic = 14, LookWriteMask = 15, LookDepths = 16, LookFogColor = 17, LookFunction = 18,
    LookEnvironment = 19, LookTexels = 20, LookTexelWidth = 21, LookTexelRows = 22, LookWidth = 23, LookHeight = 24,
  };
  enum : u32 {
    FlagClear = 1 << 0, FlagClearColor = 1 << 1, FlagClearAlpha = 1 << 2, FlagClearDepth = 1 << 3,
    FlagAlphaTest = 1 << 4, FlagColorTest = 1 << 5, FlagStencilTest = 1 << 6, FlagDepthTest = 1 << 7,
    FlagBlend = 1 << 8, FlagDither = 1 << 9, FlagLogicOp = 1 << 10, FlagDepthWrite = 1 << 11,
    FlagDepthRange = 1 << 12, FlagFog = 1 << 13, FlagTextured = 1 << 14, FlagWithAlpha = 1 << 15,
    FlagDoubled = 1 << 16, FlagClampU = 1 << 17, FlagClampV = 1 << 18,
  };
  enum : u32 {
    JobWords = 64,
    JobKind = 0, JobLook = 1, JobFirstX = 2, JobLastX = 3, JobFirstY = 4, JobLastY = 5,
    SpriteZ = 6, SpriteColor = 7, SpriteSpecular = 8, SpriteLeftFog = 9, SpriteRightFog = 10, SpriteMiddle = 11,
    SpriteTurned = 12, SpriteColumns = 13, SpriteRows = 14,
    PointX = 6, PointY = 7, PointZ = 8, PointColor = 9, PointSpecular = 10, PointFog = 11, PointU = 12, PointV = 13,
    TriangleX = 6, TriangleY = 9, TriangleA = 12, TriangleB = 15, TriangleLeast = 18, TriangleTotal = 19,
    TriangleFlags = 20, TriangleFlatColor = 21, TriangleFlatSpecular = 22, TriangleColor = 23, TriangleSpecular = 26,
    TriangleZ = 29, TriangleFog = 32, TriangleU = 35, TriangleV = 38, TriangleQ = 41, TriangleW = 44,
    TriangleStartX = 47, TriangleStartY = 48, TriangleUBase = 49, TriangleVBase = 53, TriangleShifts = 57,
  };
  enum : u32 { Sprite = 0, Triangle = 1, Point = 2 };
}

static auto bitsOf(float value) -> u32 { u32 bits; std::memcpy(&bits, &value, 4); return bits; }

GPU::GPU(std::unique_ptr<Device> device) : device(std::move(device)) {}

//Whether the host's compiler fuses the software renderer's sums of products (raster.cpp's blendColor(): the middle
//product by itself, the first and third fused into it in turn, as clang does on ARM64), 1; rounds every step apart
//(x86-64 without FMA), 0; or does something else, -1 (the GPU can't follow it then). The same expression as there,
//compiled alike, on values where the three ways differ.
auto GPU::hostFuses() -> int {
  volatile float inputs[7] = {255.0f, 254.0f, 253.0f, 16777217.0f * 3, 33554431.0f, 25165823.0f, 3.0f};
  for(u32 attempt = 0; attempt < 4096; attempt++) {
    float v0 = inputs[0], v1 = inputs[1], v2 = inputs[2], w0 = inputs[3] + attempt, w1 = inputs[4] - attempt;
    float w2 = inputs[5] + attempt * 3, total = inputs[6];
    float compiled = (v0 * w0 + v1 * w1 + v2 * w2) / total;
    volatile float middle = v1 * w1;
    float fusedWay = std::fma(v2, w2, std::fma(v0, w0, float(middle))) / total;
    volatile float first = v0 * w0, third = v2 * w2;
    volatile float firstTwo = first + middle;
    volatile float apartSum = firstTwo + third;
    float apartWay = apartSum / total;
    if(fusedWay == apartWay) continue;  //(not values that tell them apart)
    return compiled == fusedWay ? 1 : compiled == apartWay ? 0 : -1;
  }
  return -1;
}

//The device's rounding found out: whether its own fma() is fused (by probe.comp, on products whose rounding before
//the addition shows), and the host's way (hostFuses()); the shaders made with them.
auto GPU::detect(std::string& error) -> bool {
  int host = hostFuses();
  if(host < 0) return error = "the host's compiler rounds the GE's sums in a way the GPU can't follow", false;
  if(!configure(host, true)) return error = "the GPU's shaders weren't made", false;
  std::vector<u32> cases;
  u32 seed = 1;
  auto next = [&] { seed = seed * 1664525 + 1013904223; return seed; };
  for(u32 n = 0; n < 256; n++) {  //a * b + c where a * b rounded first differs from it fused
    float a = 1 + float(next() >> 9) / 8388608.0f, b = 1 + float(next() >> 9) / 8388608.0f;
    float c = -(a * b) * (1 + float(next() >> 20) / 4294967296.0f);
    cases.insert(cases.end(), {bitsOf(a), bitsOf(b), bitsOf(c), 0, 0, 0, 0, 0});
  }
  auto results = probe(0, cases);
  if(results.size() != cases.size()) return error = "the GPU's arithmetic couldn't be probed", false;
  bool fusedEverywhere = true;
  for(u32 n = 0; n < 256; n++) {
    float a, b, c;
    std::memcpy(&a, &cases[n * 8], 4), std::memcpy(&b, &cases[n * 8 + 1], 4), std::memcpy(&c, &cases[n * 8 + 2], 4);
    fusedEverywhere &= results[n * 8 + 2] == bitsOf(std::fma(a, b, c));
  }
  if(!configure(host, fusedEverywhere)) return error = "the GPU's shaders weren't made", false;
  return true;
}

auto GPU::configure(bool fused, bool nativeFma) -> bool {
  this->fused = fused, this->nativeFma = nativeFma;
  return device->configure(fused, nativeFma);
}

//probe.comp's cases (8 words each), and its results (8 words each), or none if the GPU failed.
auto GPU::probe(u32 kind, const std::vector<u32>& cases) -> std::vector<u32> {
  u32 count = cases.size() / 8;
  std::memcpy(device->records(cases.size()), cases.data(), cases.size() * 4);
  u32* out = device->bins(cases.size());
  Parameters p;
  p.probeKind = kind, p.probeCount = count;
  if(!device->probe(p)) return {};
  out = device->bins(cases.size());
  return {out, out + cases.size()};
}

//texture.cpp's texelAxis(), the same arithmetic (a sprite's axes are worked out here, once a column and once a row,
//as spriteRows() works them out; the GPU looks them up).
static auto texelAxis(float coordinate, u32 size, bool clamp, bool linear) -> GE::TexelAxis {
  auto inside = [&](s32 c) -> s32 {
    s32 last = std::min<s32>(size, 512) - 1;
    return clamp ? std::clamp(c, 0, last) : c & last;
  };
  float held = std::isnan(coordinate) ? 0.0f : std::clamp(coordinate, -65536.0f, 65536.0f);
  if(!linear) return {inside(s32(std::floor(held))), 0, 0};
  s32 base = s32(std::floor(held * 256)) - 128;
  return {inside(base >> 8), inside((base >> 8) + 1), base >> 4 & 15};
}
static auto axisWord(GE::TexelAxis axis) -> u32 {  //(raster.comp's unpackAxis())
  return u32(axis.first) | u32(axis.second) << 10 | u32(axis.fraction) << 20;
}

//A 2D triangle's texture coordinate as raster.cpp steps it, start + (x - startX) / 16 * across + (y - startY) / 16
//* down in doubles, then a float, as whole numbers for the GPU (raster.comp's wholeCoordinate()): base + (x - startX)
//* stepAcross + (y - startY) * stepDown, over 2^shift. The steps are whole 65536ths (draw.cpp's shortStep()), and
//start a float, so with shift at least 20 and making start whole, every sum is a whole number; where the job's
//largest is below 2^53, none of the double steps rounded, and the float is the nearest to that whole number over
//2^shift, which the GPU rounds exactly. False where that isn't so (huge steps or coordinates): the job is then the
//software renderer's to draw. dx and dy: the job's least and greatest x - startX and y - startY, in sixteenths.
static auto wholeCoordinate(f64 start, f64 across, f64 down, s64 dxLow, s64 dxHigh, s64 dyLow, s64 dyHigh, u32* out,
                            u32& shift) -> bool {
  constexpr f64 Limit = 2147483648.0;  //(steps go to the GPU as 32-bit numbers)
  f64 acrossSteps = across * 65536, downSteps = down * 65536;
  if(!(std::abs(acrossSteps) < Limit) || !(std::abs(downSteps) < Limit) || !std::isfinite(start)) return false;
  s64 mantissa = 0;
  s32 exponent = 0;
  if(start != 0) {
    int power;
    mantissa = s64(std::ldexp(std::frexp(start, &power), 53)), exponent = power - 53;
    while(!(mantissa & 1)) mantissa >>= 1, exponent++;
  }
  s32 bits = std::max(20, -exponent);
  if(bits > 62 || (mantissa && exponent + bits > 30)) return false;
  s64 base = mantissa * (s64(1) << (exponent + bits));
  f64 scale = std::ldexp(1.0, bits - 20);
  if(!(std::abs(acrossSteps * scale) < Limit) || !(std::abs(downSteps * scale) < Limit)) return false;
  s64 stepAcross = s64(acrossSteps * scale), stepDown = s64(downSteps * scale);
  s64 dx = std::max(std::abs(dxLow), std::abs(dxHigh)), dy = std::max(std::abs(dyLow), std::abs(dyHigh));
  if(dx >= (s64(1) << 21) || dy >= (s64(1) << 21)) return false;
  if(std::abs(base) + std::abs(stepAcross) * dx + std::abs(stepDown) * dy >= (s64(1) << 53)) return false;
  out[0] = u32(base), out[1] = u32(u64(base) >> 32), out[2] = u32(s32(stepAcross)), out[3] = u32(s32(stepDown));
  shift = bits;
  return true;
}

//Whether the look's texture has room on the GPU, with what the run has put there already.
auto GPU::fits(const GE::Look& look) const -> bool {
  if(!look.textured || !look.decoded) return true;
  if(auto found = slots.find(look.decoded.get()); found != slots.end()) {
    auto& slot = found->second;
    if(slot.texture.lock() == look.decoded && slot.words == look.decoded->texels.size()) return true;
  }
  return texelsUsed + look.decoded->texels.size() <= TexelWords;
}

//The look's record in the run (made the first time a job of it is taken), its texture put on the GPU.
auto GPU::lookFor(const GE::Look& look) -> u32 {
  using namespace Record;
  if(auto found = lookIndex.find(&look); found != lookIndex.end()) return found->second;
  u32 index = looks.size() / LookWords;
  looks.resize(looks.size() + LookWords);
  u32* w = &looks[index * LookWords];
  auto& p = look.pixel;
  auto& t = look.texture;
  w[LookFlags] = p.clear * FlagClear | p.clearColor * FlagClearColor | p.clearAlpha * FlagClearAlpha |
                 p.clearDepth * FlagClearDepth | p.alphaTest * FlagAlphaTest | p.colorTest * FlagColorTest |
                 p.stencilTest * FlagStencilTest | p.depthTest * FlagDepthTest | p.blend * FlagBlend |
                 p.dither * FlagDither | p.logicOp * FlagLogicOp | p.depthWrite * FlagDepthWrite |
                 p.depthRange * FlagDepthRange | p.fog * FlagFog | look.textured * FlagTextured |
                 look.withAlpha * FlagWithAlpha | look.doubled * FlagDoubled | t.clampU * FlagClampU |
                 t.clampV * FlagClampV;
  w[LookFormat] = p.format;
  w[LookAlpha] = p.alphaFunction | p.alphaReference << 8 | p.alphaMask << 16;
  w[LookColorTest] = p.colorFunction;
  w[LookColorReference] = p.colorReference;
  w[LookColorMask] = p.colorMask;
  w[LookStencil] = p.stencilFunction | p.stencilReference << 8 | p.stencilMask << 16;
  w[LookStencilOps] = p.stencilFail | p.stencilDepthFail << 4 | p.stencilPass << 8;
  w[LookDepthTest] = p.depthFunction;
  w[LookBlend] = p.blendSource | p.blendDestination << 4 | p.blendOperation << 8;
  w[LookFixedA] = p.fixedA;
  w[LookFixedB] = p.fixedB;
  for(u32 n = 0; n < 16; n++) w[LookDither + n / 8] |= (u32(p.ditherMatrix[n]) & 15) << n % 8 * 4;
  w[LookLogic] = p.logic;
  w[LookWriteMask] = p.writeMask;
  w[LookDepths] = p.minDepth | p.maxDepth << 16;
  w[LookFogColor] = p.fogColor;
  w[LookFunction] = look.function;
  w[LookEnvironment] = look.environment;
  if(look.textured) {
    auto& decoded = *look.decoded;
    u32 words = decoded.texels.size();
    auto& slot = slots[&decoded];
    if(slot.texture.lock() != look.decoded || slot.words != words) {  //(fits() made room)
      slot = {look.decoded, texelsUsed, words};
      std::memcpy(device->texels() + texelsUsed, decoded.texels.data(), words * 4);
      texelsUsed += words;
    }
    w[LookTexels] = slot.offset;
    w[LookTexelWidth] = decoded.key.width;
    w[LookTexelRows] = decoded.rows;
    w[LookWidth] = t.width;
    w[LookHeight] = t.height;
  }
  lookIndex[&look] = index;
  return index;
}

//The job into the run, if the GPU can draw it: its record, and a sprite's texel axes.
auto GPU::take(const GE::Job& job) -> bool {
  using namespace Record;
  auto& look = *job.look;
  if(job.kind == GE::Job::Kind::Line) return statistics.lines++, false;
  if(job.kind == GE::Job::Kind::Sprite && look.textured && job.sprite.divided) {
    return statistics.spriteTexels3D++, false;
  }
  if(look.textured && !look.decoded) return false;  //(only drawn at once, never in a batch: primitive())
  u32 at = jobs.size();
  jobs.resize(at + JobWords);
  u32* w = &jobs[at];
  w[JobFirstX] = job.firstX, w[JobLastX] = job.lastX, w[JobFirstY] = job.firstY, w[JobLastY] = job.lastY;
  auto& t = look.texture;
  switch(job.kind) {
  case GE::Job::Kind::Sprite: {
    auto& s = job.sprite;
    w[JobKind] = Sprite;
    w[SpriteZ] = s.z, w[SpriteColor] = s.color, w[SpriteSpecular] = s.specular;
    w[SpriteLeftFog] = s.leftFog, w[SpriteRightFog] = s.rightFog;
    w[SpriteMiddle] = s.middle, w[SpriteTurned] = s.turned;
    if(look.textured) {  //(spriteRows()'s columns and rows, each by the very expression it has)
      w[SpriteColumns] = tables.size();
      for(s32 x = job.firstX; x <= job.lastX; x++) {
        float column = s.columnFirst + f64(x * 16 + 8 - s.columnStart) / 16 * s.columnStep;
        tables.push_back(axisWord(s.turned ? texelAxis(column, t.height, t.clampV, job.linear)
                                           : texelAxis(column, t.width, t.clampU, job.linear)));
      }
      w[SpriteRows] = tables.size();
      for(s32 y = job.firstY; y <= job.lastY; y++) {
        float row = s.rowFirst + f64(y * 16 + 8 - s.rowStart) / 16 * s.rowStep;
        tables.push_back(axisWord(s.turned ? texelAxis(row, t.width, t.clampU, job.linear)
                                           : texelAxis(row, t.height, t.clampV, job.linear)));
      }
    }
    break;
  }
  case GE::Job::Kind::Triangle: {
    auto& r = job.triangle;
    w[JobKind] = Triangle;
    for(u32 k = 0; k < 3; k++) {
      u32 from = (k + 1) % 3, to = (k + 2) % 3;
      s64 a = r.y[from] - r.y[to], b = r.x[to] - r.x[from];
      w[TriangleX + k] = s32(r.x[k]), w[TriangleY + k] = s32(r.y[k]);
      w[TriangleA + k] = s32(a), w[TriangleB + k] = s32(b);
      w[TriangleLeast] |= (a > 0 || (a == 0 && b > 0) ? 0 : 1) << k;
      w[TriangleColor + k] = r.color[k], w[TriangleSpecular + k] = r.specular[k];
      w[TriangleZ + k] = bitsOf(r.z[k]), w[TriangleFog + k] = bitsOf(r.fog[k]);
      w[TriangleU + k] = bitsOf(r.u[k]), w[TriangleV + k] = bitsOf(r.v[k]);
      w[TriangleQ + k] = bitsOf(r.q[k]), w[TriangleW + k] = bitsOf(r.w[k]);
    }
    w[TriangleTotal] = bitsOf(r.total);
    w[TriangleFlags] = r.flat | r.shines << 1 | r.perspective << 2;
    w[TriangleFlatColor] = r.flatColor, w[TriangleFlatSpecular] = r.flatSpecular;
    w[TriangleStartX] = s32(r.startX), w[TriangleStartY] = s32(r.startY);
    if(look.textured && !r.perspective) {
      s64 dxLow = s64(job.firstX) * 16 + 8 - r.startX, dxHigh = s64(job.lastX) * 16 + 8 - r.startX;
      s64 dyLow = s64(job.firstY) * 16 + 8 - r.startY, dyHigh = s64(job.lastY) * 16 + 8 - r.startY;
      u32 uShift = 0, vShift = 0;
      if(!wholeCoordinate(r.uStart, r.uAcross, r.uDown, dxLow, dxHigh, dyLow, dyHigh, &w[TriangleUBase], uShift) ||
         !wholeCoordinate(r.vStart, r.vAcross, r.vDown, dxLow, dxHigh, dyLow, dyHigh, &w[TriangleVBase], vShift)) {
        jobs.resize(at);
        return statistics.coordinates2D++, false;
      }
      w[TriangleShifts] = uShift | vShift << 8;
    }
    break;
  }
  case GE::Job::Kind::Point: {
    auto& p = job.point;
    w[JobKind] = Point;
    w[PointX] = p.x, w[PointY] = p.y, w[PointZ] = p.z, w[PointColor] = p.color, w[PointSpecular] = p.specular;
    w[PointFog] = p.fog, w[PointU] = bitsOf(p.u), w[PointV] = bitsOf(p.v);
    break;
  }
  case GE::Job::Kind::Line: break;
  }
  w[JobKind] |= u32(job.linear) << 8;
  w[JobLook] = lookFor(look);
  taken.push_back(&job);
  if(!target) target = &look.pixel, left = job.firstX, top = job.firstY, right = job.lastX, bottom = job.lastY;
  left = std::min(left, job.firstX), top = std::min(top, job.firstY);
  right = std::max(right, job.lastX), bottom = std::max(bottom, job.lastY);
  return true;
}

//The run drawn on the GPU: the batch's VRAM pages copied there, the records, the stages run, the pages copied back.
//(The pages hold every byte its jobs may draw over, as the GE noted them: primitive().)
auto GPU::finish(GE& ge, GE::Batch& batch) -> void {
  using namespace Record;
  if(jobs.empty()) return;
  auto now = [] { return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()); };
  u64 began = now();
  u8* vram = device->vram();
  for(u32 page = 0; page < GE::VRAMPages; page++) {
    if(batch.pending[page]) std::memcpy(vram + page * 4096, ge.memory.vram.data() + page * 4096, 4096);
  }
  Parameters p;
  p.lookOffset = tables.size(), p.jobOffset = p.lookOffset + looks.size();
  u32* records = device->records(p.jobOffset + jobs.size());
  if(records) {
    std::memcpy(records, tables.data(), tables.size() * 4);
    std::memcpy(records + p.lookOffset, looks.data(), looks.size() * 4);
    std::memcpy(records + p.jobOffset, jobs.data(), jobs.size() * 4);
  }
  p.jobCount = jobs.size() / JobWords;
  p.wordsPerTile = (p.jobCount + 31) / 32;
  p.tileLeft = left & ~15, p.tileTop = top & ~15;
  p.tilesAcross = (right - p.tileLeft) / 16 + 1, p.tilesDown = (bottom - p.tileTop) / 16 + 1;
  p.areaLeft = left, p.areaTop = top, p.areaRight = right, p.areaBottom = bottom;
  p.frameBuffer = target->frameBuffer, p.stride = target->stride, p.format = target->format;
  p.depthBuffer = target->depthBuffer, p.depthStride = target->depthStride;
  u64 copied = now();
  bool drawn = device->bins(p.tilesAcross * p.tilesDown * p.wordsPerTile) && records && device->draw(p);
  u64 waited = now();
  statistics.copying += copied - began, statistics.waiting += waited - copied;
  if(drawn) {
    vram = device->vram();
    for(u32 page = 0; page < GE::VRAMPages; page++) {
      if(batch.pending[page]) std::memcpy(ge.memory.vram.data() + page * 4096, vram + page * 4096, 4096);
    }
    statistics.copying += now() - waited;
  } else {
    for(auto job : taken) ge.rasterize(*job, job->firstY, job->lastY);
  }
  statistics.runs++;
  tables.clear(), looks.clear(), jobs.clear(), taken.clear(), lookIndex.clear();
  target = nullptr;
}

//A batch (GE::Renderer): its jobs in order, runs of those the GPU can draw drawn there, the rest by the software
//renderer between them, each after what came before it.
auto GPU::draw(GE& ge, GE::Batch& batch) -> void {
  statistics.batches++;
  for(auto& job : batch.jobs) {
    u64 pixels = u64(job.lastX - job.firstX + 1) * u64(job.lastY - job.firstY + 1);
    if(!fits(*job.look)) {  //the GPU's textures full: the run drawn, and the textures dropped
      finish(ge, batch);
      slots.clear(), texelsUsed = 0;
    }
    if(take(job)) {
      statistics.gpuJobs++, statistics.gpuPixels += pixels;
      continue;
    }
    finish(ge, batch);
    ge.rasterize(job, job.firstY, job.lastY);
    statistics.cpuJobs++, statistics.cpuPixels += pixels;
  }
  finish(ge, batch);
}

}
