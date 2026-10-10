#include "gpu.hpp"
#include "../../memory/memory.hpp"
#include "shaders/shaders.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

//Vulkan's types only: every function comes through vkGetInstanceProcAddr (vulkan.cpp)
#if !defined(VK_NO_PROTOTYPES)
  #define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
  #include <vulkan/vulkan_android.h>  //(presenting on the host's window: vulkan.cpp)
  #include <android/native_window.h>
#endif
#if !defined(_WIN32)
  #include <dlfcn.h>
#endif

namespace ares::PlayStationPortable {

#include "vulkan.cpp"

//A screen position as the GE holds it (draw.cpp's fixed()): sixteenths of a pixel, wild values held to its range.
static auto targetFixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

//The filter for a primitive (draw.cpp's chooseFilter()): TEXTURE_FILTER's for enlarging if a texel covers a pixel
//or more, else its for shrinking.
static auto targetFilter(u32 filter, float texelsPerPixel) -> bool {
  return texelsPerPixel <= 1.0f ? filter >> 8 & 1 : filter & 1;
}

//A frame buffer's pixel as 8888 (red in the low byte, the stencil in the alpha), and back (pixel.cpp's widenPixel()
//and narrowPixel(); a 5650 one has no stencil, read as 0).
static auto widenTarget(u32 pixel, u32 format) -> u32 {
  auto grow = [](u32 value, u32 bits) { return value << (8 - bits) | value >> (2 * bits - 8); };
  if(format == 3) return pixel;
  if(format == 0) return grow(pixel & 31, 5) | grow(pixel >> 5 & 63, 6) << 8 | grow(pixel >> 11 & 31, 5) << 16;
  if(format == 1) {
    return grow(pixel & 31, 5) | grow(pixel >> 5 & 31, 5) << 8 | grow(pixel >> 10 & 31, 5) << 16 |
           (pixel >> 15 ? 0xff00'0000 : 0);
  }
  return grow(pixel & 15, 4) | grow(pixel >> 4 & 15, 4) << 8 | grow(pixel >> 8 & 15, 4) << 16 |
         grow(pixel >> 12 & 15, 4) << 24;
}
static auto narrowTarget(u32 color, u32 format) -> u32 {
  u32 r = color & 0xff, g = color >> 8 & 0xff, b = color >> 16 & 0xff, a = color >> 24;
  if(format == 0) return r >> 3 | g >> 2 << 5 | b >> 3 << 11;
  if(format == 1) return r >> 3 | g >> 3 << 5 | b >> 3 << 10 | a >> 7 << 15;
  if(format == 2) return r >> 4 | g >> 4 << 4 | b >> 4 << 8 | a >> 4 << 12;
  return color;
}

GPU::GPU(std::unique_ptr<Backend> backend) : backend(std::move(backend)) {}

GPU::~GPU() {
  if(!backend) return;
  for(auto& t : targets) backend->dropTarget(t->id);
  for(auto& [decoded, texture] : textures) backend->dropTexture(texture.id);
}

auto GPU::tell(const std::string& what) -> void {
  if(reported) return;
  reported = true;
  if(report) report(what);
  else std::fprintf(stderr, "PSP GPU: %s\n", what.c_str());
}

//The GPU stopped answering: what it drew since the last finish is gone (memory's VRAM keeps what was there before),
//and the software renderer draws from now on (ready()).
auto GPU::lose() -> void {
  backend->lost = true;
  tell("the GPU stopped answering; the software renderer draws from now on");
}

//The target for a frame buffer, made if there's none yet (a frame buffer is its address, width and format: the
//same bytes as another format are another target, filled from memory when drawn into after the other). Its height
//is as many rows as fit in VRAM, up to 512 (a PRIM reaching further is the software renderer's: begin()). None
//where not a row fits, or the GPU has no room for it.
auto GPU::targetFor(const GE::PixelState& p) -> Target* {
  u32 bytes = p.format == 3 ? 4 : 2;
  for(auto& t : targets) {
    if(t->address == p.frameBuffer && t->stride == p.stride && t->format == p.format) {
      t->used = ++uses;
      return t.get();
    }
  }
  u32 height = std::min<u32>(512, (Memory::VRAMSize - p.frameBuffer) / (p.stride * bytes));
  if(!height) return nullptr;
  //(too many: the one unused longest goes, if it isn't drawn on the GPU still)
  if(targets.size() >= 32) {
    auto oldest = targets.end();
    for(auto t = targets.begin(); t != targets.end(); t++) {
      if(!(*t)->drawn() && (oldest == targets.end() || (*t)->used < (*oldest)->used)) oldest = t;
    }
    if(oldest != targets.end()) {
      for(auto copy = copies.begin(); copy != copies.end();) {
        if(std::get<0>(copy->first) == (*oldest)->id) {
          backend->dropTexture(copy->second.texture), copy = copies.erase(copy);
        }
        else copy++;
      }
      backend->dropTarget((*oldest)->id), targets.erase(oldest);
    }
  }
  u32 id = backend->makeTarget(p.stride, height);
  if(!id) return nullptr;
  auto t = std::make_unique<Target>();
  t->address = p.frameBuffer, t->stride = p.stride, t->format = p.format;
  t->id = id, t->height = height, t->used = ++uses;
  targets.push_back(std::move(t));
  return targets.back().get();
}

//Rows from to to of the target filled from memory's VRAM, in order with what's drawn: its pixels (the stencil
//their alpha: parts bit 0) or its depth buffer's (bit 1: the target's, VRAM's fourth copy's order, memory.hpp). Their
//bytes are then watched, so that whoever changes them is heard of (written()).
auto GPU::fill(Target& t, u32 from, u32 to, const GE::PixelState& p, u8 parts) -> void {
  if(from >= to) return;
  (void)p;
  u32 width = t.stride, height = to - from, count = width * height, bytes = t.bytes();
  auto& vram = ge->memory.vram;
  Command c{Command::Kind::Upload, t.id};
  c.x = 0, c.y = from, c.width = width, c.height = height;
  c.colors = recorded.uploads.size();
  c.stencil = c.colors + count * 4;
  c.depth = c.stencil + count;
  c.parts = parts;
  recorded.uploads.resize(c.depth + count * 2);
  u8* out = recorded.uploads.data();
  for(u32 y = 0; y < height; y++) {
    for(u32 x = 0; x < width; x++) {
      u32 n = y * width + x;
      if(parts & 1) {
        u32 at = (t.address + ((from + y) * t.stride + x) * bytes) & (Memory::VRAMSize - 1), pixel = 0;
        std::memcpy(&pixel, &vram[at], bytes);
        u32 color = widenTarget(pixel, t.format);
        std::memcpy(out + c.colors + n * 4, &color, 4);
        out[c.stencil + n] = color >> 24;
      }
      u16 depth = 0;
      if(parts & 2 && x < t.depthStride) {
        u32 offset = Memory::vramOffset(3, (t.depthBuffer + ((from + y) * t.depthStride + x) * 2) &
                                              (Memory::VRAMSize - 1));
        depth = vram[offset] | vram[offset + 1] << 8;
      }
      if(parts & 2) std::memcpy(out + c.depth + n * 2, &depth, 2);
    }
  }
  recorded.commands.push_back(c);
  if(parts & 1) changed(t, 0, from, t.stride - 1, to - 1);
  if(parts & 1) ge->memory.watch(Memory::VRAMBase + t.address + from * t.stride * bytes, height * t.stride * bytes);
  if(parts & 2 && t.depthStride) {  //(through the fourth copy, as the GE sees it)
    ge->memory.watch(Memory::VRAMBase + 3 * Memory::VRAMSize + t.depthBuffer + from * t.depthStride * 2,
                     height * t.depthStride * 2);
  }
  statistics.uploads++;
}

//The pixels left-right, top-bottom of the target are drawn on the GPU: VRAM's pages under them are the renderer's
//until it's finished (busy: whoever touches one waits for finish()), and whoever keeps a copy of those bytes (decoded
//textures, the recompiler) hears of the change, as the software renderer tells of what it draws. Not again for a
//rectangle inside one told of since the last finish: a copy of bytes the GPU has drawn over since then can only be
//made after a finish (drawnOver() has memory wait for one), so no one has those bytes yet. (A 3D game's PRIMs each
//reach the whole scissor rectangle: told once a frame, not once a PRIM.)
auto GPU::own(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(!t.drawn()) t.left = left, t.top = top, t.right = right, t.bottom = bottom;
  else {
    t.left = std::min(t.left, left), t.top = std::min(t.top, top);
    t.right = std::max(t.right, right), t.bottom = std::max(t.bottom, bottom);
  }
  auto& memory = ge->memory;
  u32 bytes = t.bytes();
  u32 low = t.address + (top * t.stride + left) * bytes, high = t.address + (bottom * t.stride + right) * bytes;
  u32 first = low / Memory::PageSize, last = std::min<u32>(high / Memory::PageSize, GE::VRAMPages - 1);
  //(told, and its pages busy still: the drawing threads' settle frees every busy page, the GPU's too, and memory
  //may copy them then without a finish)
  bool told = false;
  for(auto& r : t.told) told |= left >= r[0] && top >= r[1] && right <= r[2] && bottom <= r[3];
  for(u32 page = first; page <= last && told; page++) told = memory.vramPageBusy(page);
  if(told) return;
  if(t.told.size() < MostTold) t.told.push_back({left, top, right, bottom});
  bool added = false;
  for(u32 page = first; page <= last; page++) {
    if(owners[page] == &t && memory.vramPageBusy(page)) continue;
    if(!memory.vramPageBusy(page)) added = true, memory.busyPages[page >> 6] |= 1ull << (page & 63);
    owners[page] = &t;
  }
  if(added) {
    memory.vramBusy = true;
    if(memory.vramGuard) memory.vramGuard(true);
  }
  //(owned first: the change to a watched page of a target's isn't someone else's, written())
  memory.changed(Memory::VRAMBase + low, high + bytes - low);
}

//The target's pixels left-right, top-bottom changed (filled, drawn into): every copy taken of it is that much older,
//which the copies hear of as one is next taken (textureFor()).
static auto widen(s32 (&box)[4], s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(box[0] > box[2]) box[0] = left, box[1] = top, box[2] = right, box[3] = bottom;
  else {
    box[0] = std::min(box[0], left), box[1] = std::min(box[1], top);
    box[2] = std::max(box[2], right), box[3] = std::max(box[3], bottom);
  }
}
auto GPU::changed(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(left <= right && top <= bottom) widen(t.changes, left, top, right, bottom);
}

//The last PRIM's pixels given to its target's changes, as the next begins (gathered as the corners' extent, a float
//compared at each primitive, not worked out at each): those its corners reach, and one more each way (for a corner's
//float a hair off the GE's), inside its scissor.
auto GPU::reached() -> void {
  auto& [t, scissor, box] = reach;
  if(t && box[0] <= box[2]) {
    s32 left = std::max(s32(std::floor(box[0])) - 1, scissor[0]);
    s32 top = std::max(s32(std::floor(box[1])) - 1, scissor[1]);
    s32 right = std::min(s32(std::floor(box[2])) + 1, scissor[0] + scissor[2] - 1);
    s32 bottom = std::min(s32(std::floor(box[3])) + 1, scissor[1] + scissor[3] - 1);
    changed(*t, left, top, right, bottom);
  }
  reach = {};
}

//The PRIM's texture on the GPU: one taken from a target (holds()), copied out of it now, its texels then read as
//the target's format keeps them, at the target's resolution (scale: each texel scale x scale of the copy's); or the
//GPU's copy of a decoded texture (put there now if it isn't yet, or has fewer rows than the GE's copy), at the
//PSP's. 0 for none, where the GE has none decoded (it then draws untextured, which the design's accuracy notes
//count).
auto GPU::textureFor(const GE::Look& look, u8& texels, u32& scale) -> u32 {
  texels = 4, scale = 1;
  if(held && look.textured) {  //(copied from its target, in order with what's drawn)
    Held h = *held;
    held.reset();
    texels = h.format < 3 ? h.format : 4;
    u32 below = h.y + h.split;  //(rows the GPU hasn't drawn, memory's, filled first)
    if(below > h.target->rows) fill(*h.target, h.target->rows, below, look.pixel), h.target->rows = below;
    if(h.next && h.rows - h.split > h.next->rows) {
      fill(*h.next, h.next->rows, h.rows - h.split, look.pixel), h.next->rows = h.rows - h.split;
    }
    //(the copy of that size, copied again where it's from another place or the target has changed since: the
    //draws before it sample it as it was, as the GPU runs the commands in order. From the same place, only the
    //part changed since is copied again, into its place in the copy: the rest is as the target still has it.)
    if(h.target->changes[0] <= h.target->changes[2]) {
      auto& changes = h.target->changes;
      for(auto& [where, copied] : copies) {
        if(std::get<0>(where) == h.target->id) widen(copied.dirty, changes[0], changes[1], changes[2], changes[3]);
      }
      changes[0] = 0, changes[1] = 0, changes[2] = -1, changes[3] = -1;
    }
    auto key = std::make_tuple(h.target->id, h.width, h.rows);
    auto found = copies.find(key);
    if(found != copies.end()) found->second.used = uses;
    if(found != copies.end() && found->second.x == h.x && found->second.y == h.y && !h.next) {
      auto& copied = found->second;
      s32 left = std::max(copied.dirty[0], h.x), top = std::max(copied.dirty[1], h.y);
      s32 right = std::min({copied.dirty[2], h.x + s32(h.width) - 1, s32(h.target->stride) - 1});
      s32 bottom = std::min(copied.dirty[3], h.y + s32(h.rows) - 1);
      copied.dirty[0] = 0, copied.dirty[1] = 0, copied.dirty[2] = -1, copied.dirty[3] = -1;
      scale = backend->scale;
      if(left > right || top > bottom) return copied.texture;
      Command c{Command::Kind::Copy, h.target->id};
      c.x = left, c.y = top, c.texture = copied.texture;
      c.width = right - left + 1, c.height = bottom - top + 1, c.intoX = left - h.x, c.intoY = top - h.y;
      recorded.commands.push_back(c);
      statistics.copies++, statistics.copied += c.width * c.height;
      return copied.texture;
    }
    if(found == copies.end() && copies.size() >= MostCopies) {  //(the one unused longest let go: release())
      auto oldest = copies.begin();
      for(auto copy = copies.begin(); copy != copies.end(); copy++) {
        if(copy->second.used < oldest->second.used) oldest = copy;
      }
      backend->dropTexture(oldest->second.texture), copies.erase(oldest);
    }
    u32 id = found != copies.end() ? found->second.texture
                                   : backend->makeTexture(h.width * backend->scale, h.rows * backend->scale, nullptr);
    if(!id) return 0;
    scale = backend->scale;
    copies[key] = {id, h.x, h.y, uses};
    Command c{Command::Kind::Copy, h.target->id};
    c.x = h.x, c.y = h.y, c.texture = id;
    c.width = std::min<u32>(h.width, h.target->stride - h.x), c.height = h.split;
    recorded.commands.push_back(c);
    statistics.copies++, statistics.copied += c.width * c.height;
    if(h.next) {  //(the rest from the frame buffer below, whose changes this copy doesn't follow: taken whole again)
      c.target = h.next->id, c.y = 0, c.height = h.rows - h.split, c.intoY = h.split;
      recorded.commands.push_back(c);
      statistics.copies++, statistics.copied += c.width * c.height;
      auto& dirty = copies[key].dirty;
      dirty[0] = h.x, dirty[1] = h.y, dirty[2] = h.x + h.width - 1, dirty[3] = h.y + h.rows - 1;
    }
    return id;
  }
  auto* decoded = look.decoded.get();
  if(!look.textured || !decoded) return 0;
  auto found = textures.find(decoded);
  if(found != textures.end()) {
    if(!found->second.decoded.expired() && found->second.rows >= decoded->rows) return found->second.id;
    backend->dropTexture(found->second.id);
    textures.erase(found);
  }
  u32 id = backend->makeTexture(decoded->key.width, decoded->rows, decoded->texels.data());
  if(!id) return 0;
  textures[decoded] = {look.decoded, id, decoded->rows};
  statistics.textures++;
  return id;
}

//The GE's settings as the GPU's: a pipeline, its constants, and the dynamic state (the design's "Depth, stencil,
//blending and logic operations" says what each approximates).
auto GPU::settings(const GE::Look& look) -> void {
  auto& p = look.pixel;
  auto& c = ge->commands;
  State s{};
  Pipeline& k = s.pipeline;
  k.alphaTest = 8, k.colorTest = 4, k.quantize = 4, k.logic = 3, k.texels = 4;
  bool stencils = p.format != 0;  //(5650 has none)
  u32 maskColor = c[GE::MaskColor] & 0xff'ffff, maskAlpha = c[GE::MaskAlpha] & 0xff;
  u8 rgb = 0;
  for(u32 n = 0; n < 3; n++) rgb |= (maskColor >> n * 8 & 0xff) != 0xff ? 1 << n : 0;
  bool alphaWritable = stencils && maskAlpha != 0xff;
  //Shader blending (docs/psp-gpu-renderers.md, "Shader blending"), where the backend reads the frame buffer: a
  //write mask that keeps part of a channel is pixel.cpp's then (the GPU's masks keep whole channels), and so are
  //logic operations, and blending where the GPU's own can't come close (the absolute difference; doubled alphas,
  //which the GPU's factors hold at 1 or don't have; a 16-bit frame buffer, where the PSP narrows each blend's
  //result to the format, dithered, before the next reads it, while the GPU's target keeps 8 bits, so layered
  //blends drift by whole steps) or where every blend reads (Backend::readsBlending: in rasterization order)
  bool partial = false;
  for(u32 n = 0; n < 3; n++) partial |= (maskColor >> n * 8 & 0xff) != 0 && (maskColor >> n * 8 & 0xff) != 0xff;
  auto doubled = [](u32 factor) { return factor == 6 || factor == 8 || factor == 9; };
  bool unlike = p.blend && (p.blendOperation == 5 || p.format < 3 ||
                            (p.blendOperation < 3 && (doubled(p.blendSource) || doubled(p.blendDestination))));
  bool reading = backend->reads && (partial || (!p.clear && ((p.logicOp && p.logic != 3) || unlike ||
                                                             (p.blend && backend->readsBlending))));
  if(reading) {
    k.reads = 1, k.blending = 8, s.push.writeMask = p.writeMask;
    k.quantize = p.format < 3 ? p.format : 4;  //(each write narrowed to the format, so what's read is memory's)
  }
  s.stencilWriteMask = ~maskAlpha & 0xff;
  s.stencilCompareMask = 0xff;
  k.depthRange = p.depthRange;
  s.push.depthRange = p.minDepth | p.maxDepth << 16;
  if(p.clear) {
    k.clear = 1;
    k.colorMask = (p.clearColor ? rgb : 0) | (p.clearAlpha && alphaWritable ? 8 : 0);
    k.depthTest = 1, k.depthCompare = 1, k.depthWrite = p.clearDepth;
    if(p.format < 3) k.quantize = p.format;  //(a 16-bit frame buffer cleared to the colors it can keep)
    if(p.clearAlpha && alphaWritable) {
      k.stencilTest = 1, k.stencilCompare = 1, k.stencilPass = Replace;  //(the reference: the vertex's alpha)
    }
  } else {
    textured = look.textured && (s.texture = textureFor(look, k.texels, s.push.textureScale));
    k.textured = textured;
    k.function = look.function | look.withAlpha << 3 | look.doubled << 4;
    k.clamp = look.texture.clampU | look.texture.clampV << 1;
    if(p.alphaTest) k.alphaTest = p.alphaFunction;
    if(p.colorTest) k.colorTest = p.colorFunction;
    k.fog = p.fog;
    s.push.alphaTest = p.alphaReference | p.alphaMask << 8;
    s.push.colorReference = p.colorReference, s.push.colorMask = p.colorMask;
    s.push.fogColor = p.fogColor, s.push.environment = look.environment;
    s.push.fixedA = p.fixedA;
    k.depthTest = p.depthTest, k.depthCompare = p.depthFunction, k.depthWrite = p.depthWrite;
    k.colorMask = rgb;
    if(p.stencilTest && stencils) {
      k.stencilTest = 1, k.stencilCompare = p.stencilFunction;
      auto operation = [](u32 o) -> u8 { return o < 6 ? u8(o) : u8(Keep); };
      k.stencilFail = operation(p.stencilFail);
      k.stencilDepthFail = operation(p.stencilDepthFail);
      k.stencilPass = operation(p.stencilPass);
      s.stencilReference = p.stencilReference, s.stencilCompareMask = p.stencilMask;
      //The alpha the frame buffer keeps is the stencil; where the pass operation's result is known here, the
      //color's alpha follows it (blending by the destination's alpha reads it), else it's left (the stencil
      //itself is read back from the GPU's stencil, finish()).
      if(alphaWritable && (k.stencilPass == Replace || k.stencilPass == Clear)) {
        k.alphaOut = 1, k.colorMask |= 8;
        s.push.stencil = k.stencilPass == Replace ? p.stencilReference : 0;
      }
    }
    if(reading) {  //(blending and the logic operation pixel.cpp's, in draw.frag; dithered after blending, as there)
      if(p.blend) k.blending = p.blendOperation, k.source = p.blendSource, k.destination = p.blendDestination;
      if(p.logicOp) k.logic = p.logic;
      s.push.fixedB = p.fixedB;
    } else if(p.blend && p.blendOperation < 6) {
      k.blend = 1;
      k.operation = p.blendOperation == 5 ? u8(Maximum) : u8(p.blendOperation);  //(absolute difference: the larger)
      if(k.operation == Minimum || k.operation == Maximum) {
        k.sourceFactor = One, k.destinationFactor = One;
      } else {
        //the source's factor: by the frame buffer's color or alpha, the GPU's; by the pixel's own, here (source)
        switch(p.blendSource) {
        case 0: k.sourceFactor = DestinationColor; break;
        case 1: k.sourceFactor = OneMinusDestinationColor; break;
        case 2: case 3: case 6: case 7:
          k.sourceFactor = One, k.source = p.blendSource < 4 ? p.blendSource - 1 : p.blendSource - 3;
          break;
        case 4: case 8: k.sourceFactor = DestinationAlpha; break;  //(twice the destination's alpha: once)
        case 5: case 9: k.sourceFactor = OneMinusDestinationAlpha; break;
        default:
          if(p.fixedA == 0xff'ffff) k.sourceFactor = One;
          else if(p.fixedA == 0) k.sourceFactor = Zero;
          else k.sourceFactor = One, k.source = 5;
        }
        //the destination's: by the pixel's color or alpha, output 1 (dual-source); by its own alpha, the GPU's
        switch(p.blendDestination) {
        case 0: case 1: case 2: case 3: case 6: case 7: {
          static constexpr u8 carried[] = {0, 1, 2, 3, 0, 0, 4, 5};
          k.destination = carried[p.blendDestination];
          if(backend->dualSource) {
            k.destinationFactor = Source1Color;
          } else {  //(without it: the pixel's own color or alpha, as output 0 carries them)
            static constexpr u8 single[] = {SourceColor, OneMinusSourceColor, SourceAlpha, OneMinusSourceAlpha,
                                            0, 0, SourceAlpha, OneMinusSourceAlpha};
            k.destinationFactor = single[p.blendDestination];
            if(p.blendDestination >= 2) k.alphaOut = 0, k.colorMask &= 7;
          }
          break;
        }
        case 4: case 8: k.destinationFactor = DestinationAlpha; break;
        case 5: case 9: k.destinationFactor = OneMinusDestinationAlpha; break;
        default:
          if(p.fixedB == 0xff'ffff) k.destinationFactor = One;
          else if(p.fixedB == 0) k.destinationFactor = Zero;
          else k.destinationFactor = Constant, s.blendConstant = p.fixedB | 0xff00'0000;
        }
      }
    }
    if(p.logicOp && p.logic != 3 && !reading) {
      if(backend->logicOps) {
        k.logicOp = 1, k.logicOperation = p.logic, k.blend = 0;
      } else if(p.logic == 0 || p.logic == 12 || p.logic == 15) {
        k.logic = p.logic;
      } else if(p.logic == 5) {  //the frame buffer kept: no color written
        k.colorMask &= 8;
      } else if(p.logic == 10) {  //inverted: white, times one less the frame buffer's color
        k.logic = 15, k.blend = 1, k.operation = Add, k.source = 0, k.destination = 0;
        k.sourceFactor = OneMinusDestinationColor, k.destinationFactor = Zero;
      }
    }
    //(where the GPU's blending takes output 0 as it is and adds or subtracts, that's the source term in pixel.cpp's
    //whole numbers: draw.frag's TERM; not where output 0's color is the destination's factor too, without dual-source
    //blending)
    bool weighs = k.destinationFactor == SourceColor || k.destinationFactor == OneMinusSourceColor;
    if(k.blend && k.sourceFactor == One && k.operation <= ReverseSubtract && !weighs) {
      k.term = k.operation == Add ? 1 : 2;
    }
    k.dither = p.dither && !k.blend && !k.logicOp;
    if(!k.blend && !k.logicOp && p.format < 3) k.quantize = p.format;
    if(reading) statistics.readingDraws++;
  }
  for(u32 row = 0; row < 4; row++) {
    for(u32 column = 0; column < 4; column++) {
      s.push.dither[row >> 1] |= u32(p.ditherMatrix[row * 4 + column] & 15) << ((row & 1) * 16 + column * 4);
    }
  }
  if(textured) {
    textureWidth = std::min<u32>(look.texture.width, 512), textureHeight = std::min<u32>(look.texture.height, 512);
    s.push.textureSize = textureWidth | textureHeight << 16;
  }
  s.target = target->id;
  s.push.scale[0] = 2.0f / target->stride, s.push.scale[1] = 2.0f / target->height;
  s.push.resolution = backend->scale;
  s32 left = std::max(p.left, 0), top = std::max(p.top, 0);
  s32 right = std::min<s32>(p.right, target->stride - 1), bottom = std::min<s32>(p.bottom, target->height - 1);
  s.scissor[0] = left, s.scissor[1] = top, s.scissor[2] = right - left + 1, s.scissor[3] = bottom - top + 1;
  state = s;
  stateKept = false;
}

//A PRIM's settings: its target found (filled from memory first if need be), its pages owned, its texture on the
//GPU, its pipeline and the rest worked out, for its primitives to draw with. False where it can't be drawn here (the
//GPU lost, no target for its frame buffer, rows past the target's (past VRAM's end, where the PSP's addresses run
//round to its start, or past 512), or a texture the GE reads from memory as it draws, which the GPU can't have),
//for the software renderer to draw; true for one that draws nothing too.
auto GPU::begin(GE& ge, const GE::Look& look, bool through, const GE::Region& region) -> bool {
  this->ge = &ge;
  drawing = false;
  reached();
  auto& p = look.pixel;
  if(!ready() || (look.textured && !p.clear && !held && !look.decoded)) return held.reset(), false;
  //(a long list's draws handed over as they come, so the GPU draws while the CPU goes on)
  if(recorded.commands.size() >= SubmitEvery) flush();
  //(where it may draw: the scissor rectangle, or less, where its vertices reach, in 2D: draw.cpp)
  if(!p.stride || region.left > region.right || region.top > region.bottom || region.right < 0 || region.bottom < 0) {
    return held.reset(), true;
  }
  Target* t = targetFor(p);
  if(!t || region.bottom >= s32(t->height)) return held.reset(), false;
  s32 bottom = region.bottom;
  s32 left = std::max(region.left, 0), top = std::max(region.top, 0);
  s32 right = std::min<s32>(region.right, t->stride - 1);
  if(left > right || top > bottom) return held.reset(), true;
  //Bytes memory may have changed in its pages while the GPU drew in them (beside): the target filled afresh
  //before this PRIM draws over them, what the GPU drew put back first (below).
  s32 uLeft = left, uTop = top, uRight = right, uBottom = bottom;
  if(t->drawn()) {
    uLeft = std::min(uLeft, t->left), uTop = std::min(uTop, t->top);
    uRight = std::max(uRight, t->right), uBottom = std::max(uBottom, t->bottom);
  }
  if(t->besideReaches(uLeft, uTop, uRight, uBottom)) t->stale = true;
  //Another target drawn on the GPU in the pages this PRIM draws in, or that fill this one from memory: its pixels
  //put in memory first (which may leave this one stale: below).
  u32 rowBytes = t->stride * t->bytes();
  u32 from = t->stale ? 0 : std::min<u32>(t->rows, top), to = bottom + 1;
  u32 first = (t->address + from * rowBytes) / Memory::PageSize;
  u32 last = std::min<u32>((t->address + to * rowBytes - 1) / Memory::PageSize, GE::VRAMPages - 1);
  for(u32 page = first; page <= last; page++) {
    if(owners[page] && owners[page] != t) {
      finish(ge);
      break;
    }
  }
  if(t->stale && t->drawn()) finish(ge);  //(what the GPU drew, then memory's changes over it)
  if(!ready()) return held.reset(), false;
  //(the colors filled afresh where memory's changed, the depth where memory's depth has: rows of it, or all of it
  //for another depth buffer; neither replaces the other's newer pixels on the GPU)
  if(t->stale) t->rows = 0, t->stale = false, t->beside.clear();
  if(t->depthBuffer != p.depthBuffer || t->depthStride != p.depthStride) {
    t->depthBuffer = p.depthBuffer, t->depthStride = p.depthStride, t->depthRows = 0;
    t->depthChangedFrom = t->depthChangedTo = 0;
  }
  if(t->depthChangedFrom < t->depthChangedTo) {
    fill(*t, t->depthChangedFrom, std::min(t->depthChangedTo, t->depthRows), p, 2);
    t->depthChangedFrom = t->depthChangedTo = 0;
  }
  if(u32(bottom + 1) > t->rows) fill(*t, t->rows, bottom + 1, p, 1), t->rows = bottom + 1;
  if(u32(bottom + 1) > t->depthRows) fill(*t, t->depthRows, bottom + 1, p, 2), t->depthRows = bottom + 1;
  target = t;
  this->through = through;
  filter = ge.commands[GE::TextureFilter];
  shade = ge.commands[GE::ShadeMode];
  textured = false;
  settings(look);
  own(*t, left, top, right, bottom);
  reach = {t, {state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]}};
  drawing = true;
  return true;
}

//Vertices for the GPU, in the PRIM's draw: one with the last if its settings are the same.
auto GPU::emit(const Vertex* vertices, u32 count) -> void {
  if(recorded.vertices.size() + count > MostVertices) flush();
  if(!stateKept || recorded.states.empty()) {
    if(recorded.states.empty() || !(recorded.states.back() == state)) recorded.states.push_back(state);
    stateKept = true;
  }
  u32 index = recorded.states.size() - 1, first = recorded.vertices.size();
  auto& commands = recorded.commands;
  bool joined = !commands.empty() && commands.back().kind == Command::Kind::Draw && commands.back().state == index &&
                commands.back().first + commands.back().count == first;
  std::array<float, 4> box{vertices[0].x, vertices[0].y, vertices[0].x, vertices[0].y};
  for(u32 n = 1; n < count; n++) {
    box[0] = std::min(box[0], vertices[n].x), box[1] = std::min(box[1], vertices[n].y);
    box[2] = std::max(box[2], vertices[n].x), box[3] = std::max(box[3], vertices[n].y);
  }
  reach.box[0] = std::min(reach.box[0], box[0]), reach.box[1] = std::min(reach.box[1], box[1]);
  reach.box[2] = std::max(reach.box[2], box[2]), reach.box[3] = std::max(reach.box[3], box[3]);
  //A draw that reads the frame buffer, where the GPU doesn't keep its primitives' order (Backend::readsInOrder),
  //takes no primitive overlapping one it has: that one begins another draw, after a barrier (the backend's), so
  //it reads what the one before wrote. (Boxes in the target's pixels: a pixel's middle inside both overlaps.)
  if(state.pipeline.reads && !backend->readsInOrder) {
    if(joined) {
      bool overlaps = boxes.size() >= MostBoxes;
      for(auto& b : boxes) {
        overlaps |= box[0] < b[2] && b[0] < box[2] && box[1] < b[3] && b[1] < box[3];
      }
      if(overlaps) joined = false, statistics.splits++;
    }
    if(!joined) boxes.clear();
    boxes.push_back(box);
  }
  if(joined) {
    commands.back().count += count;
  } else {
    Command c{Command::Kind::Draw, state.target};
    c.state = index, c.first = first, c.count = count;
    commands.push_back(c);
    statistics.draws++;
  }
  recorded.vertices.insert(recorded.vertices.end(), vertices, vertices + count);
  statistics.primitives++;
}

//The fog at a vertex as 0-255 (same rule as the software path's fogAmount), then as the 0-1 the shader blends and
//turns back with × 256: so a corner's amount is what is interpolated, matching ramp-fog (docs/psp-core.md).
static auto fogAsAttribute(float fog) -> float {
  if(std::signbit(fog)) return 0;
  if(!(fog < 1)) return 255 / 256.0f;  //amount 255, not 1.0: × 256 in the shader must give 255, not 256
  return float(u32(fog * 256)) / 256.0f;
}

auto GPU::vertex(const GE::Vertex& v, bool perspective) const -> Vertex {
  Vertex o{};
  o.x = targetFixed(v.x) / 16.0f, o.y = targetFixed(v.y) / 16.0f, o.z = v.z;
  o.w = perspective ? v.clip[3] : 1.0f;
  o.u = v.u, o.v = v.v, o.q = perspective ? v.q : 1.0f;
  o.color = v.color, o.specular = v.specular;
  o.fog = fogAsAttribute(v.fog);
  return o;
}

//A triangle as the GE has it on the screen (culled already), its colors the last corner's with flat shading. In
//3D its texture coordinates follow the perspective (a corner behind the camera, which clipping leaves only at its
//edge, has them blended straight).
auto GPU::triangle(const GE::Vertex& a, const GE::Vertex& b, const GE::Vertex& c) -> void {
  if(!drawing) return;
  bool perspective = !through && a.clip[3] > 0 && b.clip[3] > 0 && c.clip[3] > 0;
  Vertex v[3] = {vertex(a, perspective), vertex(b, perspective), vertex(c, perspective)};
  if(!(shade & 1)) {
    for(auto& corner : v) corner.color = c.color, corner.specular = c.specular;
  }
  if(state.pipeline.clear && state.pipeline.stencilTest && state.stencilReference != c.color >> 24) {
    state.stencilReference = c.color >> 24, stateKept = false;
  }
  if(textured) {
    f64 area = std::abs(f64(targetFixed(b.x) - targetFixed(a.x)) * (targetFixed(c.y) - targetFixed(a.y)) -
                        f64(targetFixed(b.y) - targetFixed(a.y)) * (targetFixed(c.x) - targetFixed(a.x)));
    float texels = std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u));
    bool linear = targetFilter(filter, std::sqrt(texels / (area / 256.0f)));
    for(auto& corner : v) corner.flags = linear;
    //Above 1x a 2D triangle's texels are held inside its corners' by what half a pixel moves them (half a texel at
    //most), as far as its pixels' middles reach at 1x, so the GPU's pixels at its edges don't reach past them: two
    //quads meeting at a seam, one mirroring the other (as Ridge Racer 2's menu draws its picture), would blend in
    //the texel past the picture there
    if(through && backend->scale > 1) {
      f64 x1 = v[1].x - v[0].x, y1 = v[1].y - v[0].y, x2 = v[2].x - v[0].x, y2 = v[2].y - v[0].y;
      f64 determinant = x1 * y2 - x2 * y1;
      float low[2], high[2];
      for(u32 axis : {0u, 1u}) {
        auto at = [&](const GE::Vertex& corner) { return axis ? corner.v : corner.u; };
        f64 t1 = at(b) - at(a), t2 = at(c) - at(a);
        f64 slope = determinant ? std::hypot(t1 * y2 - t2 * y1, t2 * x1 - t1 * x2) / std::abs(determinant) : 1;
        f32 inset = 0.5 * std::min(1.0, slope);
        low[axis] = std::min({at(a), at(b), at(c)}) + inset, high[axis] = std::max({at(a), at(b), at(c)}) - inset;
        if(low[axis] > high[axis]) low[axis] = high[axis] = (low[axis] + high[axis]) / 2;
      }
      for(auto& corner : v) {
        corner.flags |= 8;
        corner.columnFirst = low[0], corner.columnStep = high[0], corner.rowFirst = low[1], corner.rowStep = high[1];
      }
    }
  }
  emit(v, 3);
}

//A sprite: two triangles over exactly the pixels the GE's rectangle() covers (its job's), with its depth, color and
//fog (each half its own: draw.cpp), the texture turned a quarter for bottom-left and top-right corners. In 2D its
//texture coordinates are stepped in the shader as the GE steps them (Vertex's first, step and start); in 3D they
//follow the perspective as the GE's do: 1 / w and the coordinate across x over w running straight across x, the
//one down y over w straight down y, which is what the GPU's interpolation does with these corners' values.
auto GPU::sprite(const GE::Job& job) -> void {
  if(!drawing || job.firstX > job.lastX || job.firstY > job.lastY) return;
  auto& s = job.sprite;
  Vertex base{};
  base.z = s.z, base.w = 1, base.q = 1, base.color = s.color, base.specular = s.specular;
  if(state.pipeline.clear && state.pipeline.stencilTest && state.stencilReference != s.color >> 24) {
    state.stencilReference = s.color >> 24, stateKept = false;
  }
  bool divided = textured && s.divided;
  if(textured) {
    base.flags = job.linear | (divided ? 0 : 2) | (s.turned ? 4 : 0);
    base.columnFirst = s.columnFirst, base.columnStep = s.columnStep, base.columnStart = s.columnStart;
    base.rowFirst = s.rowFirst, base.rowStep = s.rowStep, base.rowStart = s.rowStart;
  }
  //a corner at pixel edge (x, y): in 3D, its values where the GE's straight lines across and down reach it
  auto corner = [&](s32 x, s32 y) {
    Vertex v = base;
    v.x = x, v.y = y;
    if(divided) {
      f64 alongX = f64(x * 16 - s.left) / (s.right - s.left), alongY = f64(y * 16 - s.top) / (s.bottom - s.top);
      f64 inverse = s.leftInverse + (s.rightInverse - s.leftInverse) * alongX;
      f64 across = s.leftAcross + (s.rightAcross - s.leftAcross) * alongX;
      f64 down = s.topDown + (s.bottomDown - s.topDown) * alongY;
      v.w = 1 / inverse;
      v.u = (s.turned ? down : across) * v.w, v.v = (s.turned ? across : down) * v.w;
    }
    return v;
  };
  auto quad = [&](s32 left, s32 right, u32 fog) {  //(columns left to right, inclusive)
    if(left > right) return;
    s32 top = job.firstY, bottom = job.lastY + 1;
    Vertex v[6] = {corner(left, top), corner(right + 1, top), corner(left, bottom),
                   corner(right + 1, top), corner(right + 1, bottom), corner(left, bottom)};
    for(auto& vertex : v) vertex.fog = fog >= 255 ? 1.0f : fog / 256.0f;
    emit(v, 6);
  };
  //the right half's fog from the first column whose middle is at most a sixteenth left of the middle (fogAt())
  s32 from = s.middle - 9, split = from >= 0 ? (from + 15) / 16 : -(-from / 16);
  if(s.leftFog == s.rightFog) return quad(job.firstX, job.lastX, s.rightFog);
  quad(job.firstX, std::min(split - 1, job.lastX), s.leftFog);
  quad(std::max(split, job.firstX), job.lastX, s.rightFog);
}

//A pixel-sized square (two triangles) at pixel (x, y), with the vertex's values.
static auto squareOf(GPU::Vertex base, s32 x, s32 y, GPU::Vertex (&v)[6]) -> void {
  for(u32 n = 0; n < 6; n++) {
    static constexpr u8 right[6] = {0, 1, 0, 1, 1, 0}, down[6] = {0, 0, 1, 0, 1, 1};
    v[n] = base;
    v[n].x = f32(x + right[n]), v[n].y = f32(y + down[n]);
  }
}

auto GPU::point(const GE::Vertex& at) -> void {
  if(!drawing) return;
  Vertex base = vertex(at, false), v[6];
  base.flags = textured && targetFilter(filter, 1.0f);
  squareOf(base, targetFixed(at.x) >> 4, targetFixed(at.y) >> 4, v);
  emit(v, 6);
}

//A line: the pixels the GE's rule lights (draw.cpp's line(), raster.cpp's linePixels()), each a square of its own
//values.
auto GPU::line(const GE::Job& job, const std::vector<GE::LinePixel>& pixels) -> void {
  if(!drawing) return;
  for(auto& pixel : pixels) {
    Vertex base{}, v[6];
    base.z = pixel.z, base.w = 1, base.u = pixel.u, base.v = pixel.v, base.q = 1;
    base.color = pixel.color, base.specular = pixel.specular, base.fog = pixel.fog >= 255 ? 1.0f : pixel.fog / 256.0f;
    base.flags = textured && job.linear;
    squareOf(base, pixel.x, pixel.y, v);
    emit(v, 6);
  }
}

//Fast mode's 3D (docs/psp-gpu-renderers.md, "Vulkan (fast)"): a PRIM's triangles handed to the GPU as the vertex
//type laid them out, which transform.vert skins, transforms, lights, fogs and maps onto the screen as transform.cpp
//and lighting.cpp would; the GE has kept to its rules for which triangles are drawn, and cut and drawn those reaching
//past the near plane itself (GE::meshTriangles()). Each triangle comes with its last corner first, where the GPU
//takes a flat triangle's colors from. Its settings are a block of the run's (transformed()) each of its vertices
//names, so that PRIMs the GPU draws alike are one draw, whatever their matrices. What the GE does that this doesn't:
//step colors and fog as it steps them; choose the filter once a triangle (draw.frag chooses it at each pixel). Not
//taken: a PRIM of fewer than MeshLeast vertices (each PRIM costs its settings' block, a skinned one's bones
//differing from PRIM to PRIM, which a few vertices' transform on the CPU costs less than: Peace Walker draws 5,000
//PRIMs a frame of three triangles each); clear mode (each triangle's stencil from its own last corner: triangle());
//and one whose pipeline the driver won't make.
auto GPU::meshes(GE&, const GE::Transform&, u32 count) -> bool {
  if(!fast || !backend->transforms || count < MeshLeast) return false;
  if(!drawing) return true;
  if(state.pipeline.clear) return false;
  Pipeline k = state.pipeline;
  k.transformed = 1;
  k.flat = !(shade & 1);
  if(!(lastDrawable && *lastDrawable == k)) {
    if(!backend->drawable(k)) return false;
    lastDrawable = k;
  }
  return true;
}

auto GPU::mesh(GE& ge, const GE::Transform& t, const std::vector<GE::Vertex>& vertices,
               const std::vector<u32>& corners, bool again) -> void {
  if(!drawing) return;
  this->ge = &ge;
  u32 count = vertices.size();
  //(the PRIM's vertices once, kept for its later runs: still the recording's last models, unless it was handed over
  //between them)
  if(!again || recorded.models.size() != meshFirst + count) {
    if(recorded.models.size() + count > MostVertices) flush();
    meshFirst = recorded.models.size();
    u32 transform = transformed(t);
    recorded.models.resize(meshFirst + count);
    for(u32 n = 0; n < count; n++) {
      auto& v = vertices[n];
      auto& m = recorded.models[meshFirst + n];
      m.x = v.x, m.y = v.y, m.z = v.z;
      for(u32 k = 0; k < 3; k++) m.normal[k] = v.normal[k];
      m.u = v.u, m.v = v.v, m.color = v.color;
      for(u32 k = 0; k < 8; k++) m.weights[k] = v.weights[k];
      m.transform = transform;
    }
  }
  u32 first = recorded.indices.size();
  for(u32 corner : corners) recorded.indices.push_back(meshFirst + corner);
  //(where on the screen the GPU puts them isn't known here: anywhere inside the scissor)
  changed(*target, state.scissor[0], state.scissor[1], state.scissor[0] + state.scissor[2] - 1,
          state.scissor[1] + state.scissor[3] - 1);
  State s = state;
  s.pipeline.transformed = 1;
  s.pipeline.flat = !(shade & 1);
  if(textured) s.push.filter = filter;
  if(recorded.states.empty() || !(recorded.states.back() == s)) recorded.states.push_back(s);
  stateKept = false;
  u32 index = recorded.states.size() - 1;
  auto& commands = recorded.commands;
  if(!commands.empty() && commands.back().kind == Command::Kind::Mesh && commands.back().state == index &&
     commands.back().first + commands.back().count == first) {
    commands.back().count += corners.size();
  } else {
    Command c{Command::Kind::Mesh, s.target};
    c.state = index, c.first = first, c.count = corners.size();
    commands.push_back(c);
    statistics.draws++;
  }
  statistics.primitives += corners.size() / 3;
  if(!again) statistics.meshes++;
}

//mesh()'s block for these settings, in recorded's transforms: the last one's again where they're the same.
auto GPU::transformed(const GE::Transform& t) -> u32 {
  auto& transforms = recorded.transforms;
  if(!transforms.empty() && !std::memcmp(&t, &lastTransform, sizeof(t))) return transforms.size() - 1;
  lastTransform = t;
  Transformed b{};
  auto columns43 = [](const float* m, float (&out)[4][4]) {
    for(u32 c = 0; c < 4; c++) for(u32 r = 0; r < 3; r++) out[c][r] = m[c * 3 + r];
  };
  columns43(t.world, b.world), columns43(t.view, b.view), columns43(t.textureMatrix, b.texture);
  for(u32 c = 0; c < 4; c++) for(u32 r = 0; r < 4; r++) b.projection[c][r] = t.projection[c * 4 + r];
  for(u32 bone = 0; bone < t.weights && bone < 8; bone++) {
    float (&out)[4][4] = b.bones[bone];
    columns43(t.bones + bone * 12, out);
  }
  for(u32 n = 0; n < 3; n++) b.viewport[n] = t.scale[n], b.center[n] = t.center[n];
  b.offset[0] = t.offsetX / 16, b.offset[1] = t.offsetY / 16;
  b.offset[2] = t.textureWidth, b.offset[3] = t.textureHeight;
  b.textureScale[0] = t.textureScale[0], b.textureScale[1] = t.textureScale[1];
  b.textureScale[2] = t.textureOffset[0], b.textureScale[3] = t.textureOffset[1];
  b.fog[0] = t.fogEnd, b.fog[1] = t.fogSlope, b.fog[2] = t.fogValue, b.fog[3] = t.fogForced;
  b.modes[0] = t.mapSource, b.modes[1] = t.shadeU, b.modes[2] = t.shadeV;
  u32 mapping = t.mapMode == 1 || t.mapMode == 2 ? t.mapMode : 0;
  b.modes[3] = t.normalReverse | t.separateSpecular << 1 | t.vertexColor << 2 | t.fog << 3 | t.lighting << 4 |
               mapping << 5 | std::min(t.weights, 8u) << 8;
  b.material[0] = t.materialColor, b.material[1] = t.materialEmissive;
  b.material[2] = t.materialAmbient, b.material[3] = t.materialDiffuse;
  b.material2[0] = t.materialSpecular, b.material2[1] = t.ambientLight;
  for(u32 n = 0; n < 3; n++) b.viewDirection[n] = t.viewDirection[n];
  b.viewDirection[3] = t.specularPower;
  for(u32 n = 0; n < 4; n++) {
    auto& light = t.lights[n];
    auto& out = b.lights[n];
    for(u32 k = 0; k < 3; k++) {
      out.position[k] = light.position[k], out.direction[k] = light.direction[k];
      out.attenuation[k] = light.attenuation[k];
    }
    out.direction[3] = light.cutoff, out.attenuation[3] = light.exponent;
    out.colors[0] = light.ambient, out.colors[1] = light.diffuse, out.colors[2] = light.shine;
    out.colors[3] = light.enabled | light.directional << 1 | light.spot << 2 | light.specular << 3 |
                    light.powered << 4;
  }
  if(!transforms.empty() && !std::memcmp(&transforms.back(), &b, sizeof(b))) return transforms.size() - 1;
  transforms.push_back(b);
  return transforms.size() - 1;
}

//What's drawn so far handed to the GPU, while the CPU goes on: at a list's end, once there's enough of it (each run
//costs the CPU a submit and a wait for the run eight before it, and games may end dozens of short lists a frame:
//WipEout Pure, 209); what's less waits for the next list, the frame shown or a finish.
auto GPU::submit(GE& ge) -> void {
  this->ge = &ge;
  if(recorded.empty() || !ready() || recorded.commands.size() < SubmitEnough) return;
  flush();
}

auto GPU::flush() -> void {
  if(recorded.empty() || !ready()) return;
  if(!backend->submit(recorded)) lose();
  recorded.clear();
  statistics.submits++;
  release();
}

//What's on the GPU and no longer wanted let go (the backend destroys it once the GPU has run what's recorded with
//it): textures the GE has let go, and render-to-texture copies unused for CopyAge PRIMs. (Beyond MostCopies, the
//copy unused longest goes as another is made: textureFor().)
auto GPU::release() -> void {
  for(auto texture = textures.begin(); texture != textures.end();) {
    if(texture->second.decoded.expired()) backend->dropTexture(texture->second.id), texture = textures.erase(texture);
    else texture++;
  }
  for(auto copy = copies.begin(); copy != copies.end();) {
    if(uses - copy->second.used > CopyAge) backend->dropTexture(copy->second.texture), copy = copies.erase(copy);
    else copy++;
  }
}

//Everything drawn waited for, and the pixels the GPU drew put into memory's VRAM: each target's drawn rectangle read
//back (its stencil as the pixels' alpha) and narrowed to its format. Its pages are no longer the renderer's; a target
//over bytes another's pixels were just put into is filled from memory when next drawn into; and every target's bytes
//are watched again, to hear of whoever else changes them.
auto GPU::finish(GE& ge) -> void {
  this->ge = &ge;
  bool drawn = false;
  for(auto& t : targets) drawn |= t->drawn();
  if(recorded.empty() && !drawn) return;
  std::vector<Target*> read;
  for(auto& t : targets) {
    if(!t->drawn()) continue;
    Command c{Command::Kind::Readback, t->id};
    c.x = t->left, c.y = t->top, c.width = t->right - t->left + 1, c.height = t->bottom - t->top + 1;
    recorded.commands.push_back(c);
    recorded.readbacks++;
    read.push_back(t.get());
  }
  auto began = std::chrono::steady_clock::now();
  bool wasLost = backend->lost;
  bool finished = ready() && backend->finish(recorded);
  statistics.waiting += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - began).count();
  statistics.finishes++;
  recorded.clear();
  auto& memory = ge.memory;
  if(!finished && !wasLost) lose();
  for(u32 n = 0; n < read.size() && finished; n++) {
    Target& t = *read[n];
    const u32* colors;
    const u8* stencil;
    if(!backend->read(n, colors, stencil)) continue;
    u32 bytes = t.bytes(), width = t.right - t.left + 1;
    for(s32 y = t.top; y <= t.bottom; y++) {
      for(s32 x = t.left; x <= t.right; x++) {
        u32 at = (t.address + (y * t.stride + x) * bytes) & (Memory::VRAMSize - 1);
        u32 i = (y - t.top) * width + (x - t.left);
        u32 pixel = narrowTarget((colors[i] & 0xff'ffff) | u32(stencil[i]) << 24, t.format);
        std::memcpy(&memory.vram[at], &pixel, bytes);
      }
    }
    statistics.readbacks++;
    u32 from = t.address + t.top * t.stride * bytes, to = t.address + (t.bottom * t.stride + t.right + 1) * bytes;
    for(auto& other : targets) {
      if(other.get() != &t && other->rows && other->address < to && from < other->end()) other->stale = true;
    }
  }
  if(memory.vramBusy) {
    if(memory.vramGuard) memory.vramGuard(false);
    for(auto& pages : memory.busyPages) pages = 0;
    memory.vramBusy = false;
  }
  for(auto& owner : owners) owner = nullptr;
  for(auto& t : targets) {
    t->left = 0, t->top = 0, t->right = -1, t->bottom = -1;
    t->told.clear();
    if(!t->beside.empty()) t->stale = true;  //(memory's bytes beside what it drew, which may have changed)
    if(t->rows) memory.watch(Memory::VRAMBase + t->address, t->rows * t->stride * t->bytes());
    if(t->depthRows && t->depthStride) {
      memory.watch(Memory::VRAMBase + 3 * Memory::VRAMSize + t->depthBuffer, t->depthRows * t->depthStride * 2);
    }
  }
  release();
}

//A watched page someone else changed: a target over it, filled from memory before it's next drawn into. (A page
//the renderer owns is its own drawing's: own() tells memory of it.)
auto GPU::written(GE& ge, u32 page) -> void {
  u32 base = Memory::VRAMBase / Memory::PageSize;
  if(page < base || page >= base + GE::VRAMPages) return;
  page -= base;
  //A depth buffer's rows the change may be in (the fourth copy rearranges each 16 KiB: all of its 16 KiB's), to
  //be filled again before the next draw; the rest of the GPU's depth is kept.
  u32 block = page * Memory::PageSize & ~0x3fffu;
  for(auto& t : targets) {
    if(!t->depthRows || !t->depthStride) continue;
    u32 rowBytes = t->depthStride * 2, low = t->depthBuffer, high = low + t->depthRows * rowBytes;
    if(block + 0x4000 <= low || high <= block) continue;
    u32 from = block > low ? (block - low) / rowBytes : 0;
    u32 to = std::min<u32>((block + 0x4000 - low + rowBytes - 1) / rowBytes, t->depthRows);
    if(t->depthChangedFrom < t->depthChangedTo) {
      from = std::min(from, t->depthChangedFrom), to = std::max(to, t->depthChangedTo);
    }
    t->depthChangedFrom = from, t->depthChangedTo = to;
  }
  if(owners[page]) return;
  u32 low = page * Memory::PageSize, high = low + Memory::PageSize;
  for(auto& t : targets) {
    if(t->rows && t->address < high && low < t->end()) t->stale = true;
  }
  (void)ge;
}

//A texture whose bytes are, some of them, in pages the GPU has drawn in: taken from there for the next PRIM where
//they're all one target's, the texture's format and row width its own (a picture the GE drew, read back as it is:
//render to texture); else the GE decodes it from memory, which finishes drawing first.
auto GPU::holds(GE& ge, const GE::Sampler& texture, u32 rows, u32 columns) -> bool {
  this->ge = &ge;
  held.reset();
  if(!ready()) return false;
  u32 physical = texture.address & 0x1fff'ffff;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMSize) return false;
  u32 offset = physical - Memory::VRAMBase, bytes = texture.format == 3 ? 4 : 2;
  bool direct = !texture.swizzled && texture.format <= 3;
  u32 width = std::min<u32>({columns, texture.width, 512});
  rows = std::min<u32>({rows, texture.height, 512});
  u64 end = offset + (u64(rows - 1) * texture.bufferWidth + width) * bytes;
  if(!rows || end > Memory::VRAMSize) return false;
  //(its pages one target's, or a target's and then, from a page on, another's: next)
  Target *t = nullptr, *next = nullptr, *last = nullptr;
  u32 firstOwned = 0;  //(next's first page)
  for(u32 page = offset / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    Target* owner = owners[page];
    if(!owner || owner == last) continue;
    if(!t) t = owner;
    else if(!next && owner != t) next = owner, firstOwned = page;
    else return false;
    last = owner;
  }
  auto fits = [&](Target* t) {
    return t && !t->stale && t->format == texture.format && t->stride == texture.bufferWidth;
  };
  if(!fits(t) || !direct || offset < t->address) return false;
  //(a texture wider than the target's row, as a 3D PRIM's reach isn't known: only the columns inside it copied, those
  //past it, in memory the next rows' first, left as they are; the design's accuracy notes count it)
  u32 rowBytes = t->stride * bytes, y = (offset - t->address) / rowBytes;
  u32 x = (offset - t->address) % rowBytes / bytes;
  if(x >= t->stride) return false;
  //A texture running from one frame buffer into the next below it in memory, of the same row width and format,
  //which starts on a page and a row of the first: its rows from there on are the next one's, from its first
  //(Midnight Club 3's menu samples its frame buffer as a texture 512 rows tall: its filter's second texel for the last
  //row is the next frame buffer's first row, which the PRIM draws into, and decoded from memory it was a finish every
  //frame). Each part is copied from its own target; the first owns none of the next one's pages (whose newest pixels
  //would be its own then).
  u32 split = rows;
  if(next) {
    u32 below = next->address - t->address;
    if(!fits(next) || next->address <= t->address || next->address % Memory::PageSize || below % rowBytes) return false;
    for(u32 page = next->address / Memory::PageSize; page < firstOwned; page++) if(owners[page]) return false;
    split = below / rowBytes - y;
    if(split >= rows || rows - split > next->height) return false;
    if(next->besideReaches(x, 0, x + width - 1, rows - split - 1)) return false;
  }
  if(y + split > t->height) return false;
  if(t->besideReaches(x, y, x + width - 1, y + split - 1)) return false;  //(memory's newer bytes in it: decoded)
  held = Held{t, s32(x), s32(y), width, rows, texture.format, next, split};
  return true;
}

//Whether the pixels drawn since the last finish (each target's rows top-bottom, columns left-right: what finish()
//reads back) reach VRAM's bytes first to last. Bytes beside them, in the same pages (past the picture's right
//edge, where games keep lists), are memory's own until then: memory reaches them without waiting.
auto GPU::drawnOver(GE& ge, u32 first, u32 last) -> bool {
  (void)ge;
  for(auto& t : targets) {
    if(t->drawn() && t->reaches(first, last, t->left, t->top, t->right, t->bottom)) return true;
  }
  return false;
}

//Bytes memory changed in pages the GPU draws in, not over what it drew (or that change would have finished it
//first: Memory::pointer()): a target over them keeps them (beside) and takes them from memory again before it
//draws, shows or lends a texture over them (begin(), picture(), holds()), and after it next finishes. A change
//over what it drew is its own (own() telling memory of its pixels), and nothing to keep.
auto GPU::besideChanged(GE& ge, u32 first, u32 last) -> void {
  if(drawnOver(ge, first, last)) return;
  for(auto& t : targets) {
    if(t->rows && t->address <= last && first < t->end()) t->touch(first, last);
  }
}

auto GPU::Target::reaches(u32 first, u32 last, s32 left, s32 top, s32 right, s32 bottom) const -> bool {
  if(left > right || top > bottom) return false;
  u32 rowBytes = stride * bytes();
  u32 low = address + top * rowBytes + left * bytes(), high = address + bottom * rowBytes + (right + 1) * bytes() - 1;
  if(last < low || high < first) return false;
  u32 from = std::max<u32>(top, first > address ? (first - address) / rowBytes : 0);
  u32 to = std::min<u32>(bottom, (last - address) / rowBytes);
  for(u32 y = from; y <= to; y++) {
    u32 rowLeft = address + y * rowBytes + left * bytes(), rowRight = rowLeft + (right - left + 1) * bytes() - 1;
    if(rowLeft <= last && first <= rowRight) return true;
  }
  return false;
}

auto GPU::Target::besideReaches(s32 left, s32 top, s32 right, s32 bottom) const -> bool {
  for(auto& [first, last] : beside) {
    if(reaches(first, last, left, top, right, bottom)) return true;
  }
  return false;
}

//(a list written a word at a time makes one range, each word next to the last)
auto GPU::Target::touch(u32 first, u32 last) -> void {
  for(auto& [from, to] : beside) {
    if(first <= to + 1 && from <= last + 1) {
      from = std::min(from, first), to = std::max(to, last);
      return;
    }
  }
  if(beside.size() < 16) return beside.push_back({first, last});
  for(auto& [from, to] : beside) first = std::min(first, from), last = std::max(last, to);
  beside = {{first, last}};
}

//Memory's VRAM replaced whole (or another machine's: GE::setRenderer()): what the GPU drew since the last finish put
//back first (memory's own serialize and power have done that already, as they finish drawing), then every target,
//its colors and its depth, filled afresh when next drawn into.
auto GPU::forget(GE& ge) -> void {
  finish(ge);
  for(auto& t : targets) t->stale = true, t->depthRows = 0, t->depthChangedFrom = t->depthChangedTo = 0;
}

//The frame shown, from the GPU's target: a read back of the rectangle shown alone, after what's recorded so far.
//(The hosts take the frame as pixels in memory, so it's read back either way; presenting from the GPU spares the
//rest: every other target's read back, VRAM's pages given back, and the targets filled again from memory after.)
//The target the frame buffer at VRAM's offset address (stride pixels a row, in GE format) is shown from, width x
//height of it: none where the GPU doesn't hold the newest of every pixel shown (no such target, or rows it hasn't,
//or pages another target or the CPU has changed since), or hasn't drawn there since memory last had it all.
auto GPU::shownTarget(u32 address, u32 stride, u32 format, u32 width, u32 height) -> Target* {
  Target* t = nullptr;
  for(auto& candidate : targets) {
    if(candidate->address == address && candidate->stride == stride && candidate->format == format) {
      t = candidate.get();
    }
  }
  if(!t || t->stale || t->rows < height || width > t->stride) return nullptr;
  if(t->besideReaches(0, 0, width - 1, height - 1)) return nullptr;  //(memory's newer bytes in what's shown)
  //(every page shown the target's or no one's, and one at least the target's: else memory has it, or another)
  u32 bytes = t->bytes(), end = address + ((height - 1) * stride + width) * bytes;
  if(end > Memory::VRAMSize) return nullptr;
  bool owned = false;
  for(u32 page = address / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    if(owners[page] && owners[page] != t) return nullptr;
    owned |= owners[page] == t;
  }
  return owned ? t : nullptr;
}

auto GPU::picture(u32 address, u32 stride, u32 format, u32 width, u32 height, std::vector<u32>& pixels, u32 at)
  -> bool {
  if(!ready() || !width || !height) return false;
  at = std::clamp<u32>(at, 1, backend->scale);
  Target* t = shownTarget(address, stride, format, width, height);
  if(!t) return false;
  Command c{Command::Kind::Readback, t->id};
  c.width = width, c.height = height, c.at = at;
  recorded.commands.push_back(c);
  recorded.readbacks++;
  auto began = std::chrono::steady_clock::now();
  bool wasLost = backend->lost;
  bool finished = backend->finish(recorded);
  statistics.waiting += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - began).count();
  recorded.clear();
  release();
  const u32* colors;
  const u8* stencil;
  if(!finished || !backend->read(0, colors, stencil)) {
    if(!wasLost) lose();
    return false;
  }
  //(as memory would have them: narrowed to the frame buffer's format, then widened as the screen widens them)
  u32 count = width * height * at * at;
  pixels.resize(count);
  for(u32 n = 0; n < count; n++) {
    pixels[n] = 0xff00'0000 | (widenTarget(narrowTarget(colors[n], format), format) & 0xff'ffff);
  }
  statistics.pictures++;
  return true;
}

//(what's recorded handed to the GPU with the Present after it, nothing waited for)
auto GPU::show(u32 address, u32 stride, u32 format, u32 width, u32 height) -> bool {
  if(!presents() || !width || !height) return false;
  Target* t = shownTarget(address, stride, format, width, height);
  if(!t) return false;
  Command c{Command::Kind::Present, t->id};
  c.width = width, c.height = height, c.format = format;
  recorded.commands.push_back(c);
  bool wasLost = backend->lost;
  if(!backend->submit(recorded) && !wasLost) lose();
  recorded.clear();
  statistics.submits++, statistics.presents++;
  release();
  return true;
}

auto GPU::shoot(u32 address, u32 stride, u32 format, u32 width, u32 height) -> bool {
  if(!ready() || !width || !height) return false;
  Target* t = shownTarget(address, stride, format, width, height);
  if(!t) return false;
  Command c{Command::Kind::Shot, t->id};
  c.width = width, c.height = height, c.format = format;
  recorded.commands.push_back(c);
  bool wasLost = backend->lost, submitted = backend->submit(recorded);
  if(!submitted && !wasLost) lose();
  recorded.clear();
  statistics.submits++, statistics.pictures++;
  release();
  return submitted;
}

auto GPU::show(const std::vector<u32>& pixels, u32 width, u32 height) -> void {
  if(!presents() || !width || !height || pixels.size() < u64(width) * height) return;
  Command c{Command::Kind::Present, 0};
  c.width = width, c.height = height, c.colors = recorded.uploads.size();
  auto bytes = (const u8*)pixels.data();
  recorded.uploads.insert(recorded.uploads.end(), bytes, bytes + u64(width) * height * 4);
  recorded.commands.push_back(c);
  bool wasLost = backend->lost;
  if(!backend->submit(recorded) && !wasLost) lose();
  recorded.clear();
  statistics.submits++, statistics.presentsFromMemory++;
  release();
}

auto GPU::drop() -> void {
  recorded.clear();
  held.reset();
  reach = {};
  target = nullptr, drawing = false;
  for(auto& owner : owners) owner = nullptr;
  if(!backend) return;
  for(auto& t : targets) backend->dropTarget(t->id);
  for(auto& [decoded, texture] : textures) backend->dropTexture(texture.id);
  for(auto& [where, copied] : copies) backend->dropTexture(copied.texture);
  targets.clear(), textures.clear(), copies.clear();
  std::vector<u32> pixels;
  u32 width, height, format;
  while(backend->shot(pixels, width, height, format)) {}  //(the machine before's last shot)
}

auto GPU::resolution(u32 scale) -> void {
  drop();
  if(backend) backend->scale = std::clamp<u32>(scale, 1, backend->mostScale);
}

#include "check.cpp"

}
