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
  t.version++;
  if(parts & 1) ge->memory.watch(Memory::VRAMBase + t.address + from * t.stride * bytes, height * t.stride * bytes);
  if(parts & 2 && t.depthStride) {  //(through the fourth copy, as the GE sees it)
    ge->memory.watch(Memory::VRAMBase + 3 * Memory::VRAMSize + t.depthBuffer + from * t.depthStride * 2,
                     height * t.depthStride * 2);
  }
  statistics.uploads++;
}

//The pixels left-right, top-bottom of the target are drawn on the GPU: VRAM's pages under them are the renderer's
//until it's finished (busy: whoever touches one waits for finish()), and whoever keeps a copy of those bytes (decoded
//textures, the recompiler) hears of the change now, as the software renderer tells of what it draws.
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
  bool added = false;
  for(u32 page = first; page <= last; page++) {
    if(owners[page] == &t) continue;
    if(!owners[page]) added = true, memory.busyPages[page >> 6] |= 1ull << (page & 63);
    owners[page] = &t;
  }
  if(added) {
    memory.vramBusy = true;
    if(memory.vramGuard) memory.vramGuard(true);
  }
  //(owned first: the change to a watched page of a target's isn't someone else's, written())
  memory.changed(Memory::VRAMBase + low, high + bytes - low);
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
    u32 below = h.y + h.rows;  //(rows the GPU hasn't drawn, memory's, filled first)
    if(below > h.target->rows) fill(*h.target, h.target->rows, below, look.pixel), h.target->rows = below;
    //(the copy of that size, copied again where it's from another place or the target has changed since: the
    //draws before it sample it as it was, as the GPU runs the commands in order)
    auto key = std::make_tuple(h.target->id, h.width, h.rows);
    auto found = copies.find(key);
    if(found != copies.end()) {
      auto& copied = found->second;
      copied.used = uses;
      if(copied.version == h.target->version && copied.x == h.x && copied.y == h.y) {
        return scale = backend->scale, copied.texture;
      }
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
    copies[key] = {id, h.target->version, h.x, h.y, uses};
    Command c{Command::Kind::Copy, h.target->id};
    c.x = h.x, c.y = h.y, c.texture = id;
    c.width = std::min<u32>(h.width, h.target->stride - h.x), c.height = h.rows;
    recorded.commands.push_back(c);
    statistics.copies++;
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
  s.stencilWriteMask = ~maskAlpha & 0xff;
  s.stencilCompareMask = 0xff;
  k.depthRange = p.depthRange;
  s.push.depthRange = p.minDepth | p.maxDepth << 16;
  if(p.clear) {
    k.clear = 1;
    k.colorMask = (p.clearColor ? rgb : 0) | (p.clearAlpha && alphaWritable ? 8 : 0);
    k.depthTest = 1, k.depthCompare = 1, k.depthWrite = p.clearDepth;
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
    if(p.blend && p.blendOperation < 6) {
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
    if(p.logicOp && p.logic != 3) {
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
    k.dither = p.dither && !k.blend && !k.logicOp;
    if(!k.blend && !k.logicOp && p.format < 3) k.quantize = p.format;
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
}

//A PRIM's settings: its target found (filled from memory first if need be), its pages owned, its texture on the
//GPU, its pipeline and the rest worked out, for its primitives to draw with. False where it can't be drawn here (the
//GPU lost, no target for its frame buffer, rows past the target's (past VRAM's end, where the PSP's addresses run
//round to its start, or past 512), or a texture the GE reads from memory as it draws, which the GPU can't have),
//for the software renderer to draw; true for one that draws nothing too.
auto GPU::begin(GE& ge, const GE::Look& look, bool through, const GE::Region& region) -> bool {
  this->ge = &ge;
  drawing = false;
  auto& p = look.pixel;
  if(!ready() || (look.textured && !p.clear && !held && !look.decoded)) return held.reset(), false;
  //(a long list's draws handed over as they come, so the GPU draws while the CPU goes on)
  if(recorded.commands.size() >= SubmitEvery) submit(ge);
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
  t->version++;
  drawing = true;
  return true;
}

//Vertices for the GPU, in the PRIM's draw: one with the last if its settings are the same.
auto GPU::emit(const Vertex* vertices, u32 count) -> void {
  if(recorded.vertices.size() + count > MostVertices) submit(*ge);
  if(recorded.states.empty() || !(recorded.states.back() == state)) recorded.states.push_back(state);
  u32 index = recorded.states.size() - 1, first = recorded.vertices.size();
  auto& commands = recorded.commands;
  if(!commands.empty() && commands.back().kind == Command::Kind::Draw && commands.back().state == index &&
     commands.back().first + commands.back().count == first) {
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

auto GPU::vertex(const GE::Vertex& v, bool perspective) const -> Vertex {
  Vertex o{};
  o.x = targetFixed(v.x) / 16.0f, o.y = targetFixed(v.y) / 16.0f, o.z = v.z;
  o.w = perspective ? v.clip[3] : 1.0f;
  o.u = v.u, o.v = v.v, o.q = perspective ? v.q : 1.0f;
  o.color = v.color, o.specular = v.specular;
  o.fog = v.fog;
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
  if(state.pipeline.clear && state.pipeline.stencilTest) state.stencilReference = c.color >> 24;
  if(textured) {
    f64 area = std::abs(f64(targetFixed(b.x) - targetFixed(a.x)) * (targetFixed(c.y) - targetFixed(a.y)) -
                        f64(targetFixed(b.y) - targetFixed(a.y)) * (targetFixed(c.x) - targetFixed(a.x)));
    float texels = std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u));
    bool linear = targetFilter(filter, std::sqrt(texels / (area / 256.0f)));
    for(auto& corner : v) corner.flags = linear;
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
  if(state.pipeline.clear && state.pipeline.stencilTest) state.stencilReference = s.color >> 24;
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

//What's drawn so far handed to the GPU, while the CPU goes on.
auto GPU::submit(GE& ge) -> void {
  this->ge = &ge;
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
  bool finished = ready() && backend->finish(recorded);
  statistics.waiting += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - began).count();
  statistics.finishes++;
  recorded.clear();
  auto& memory = ge.memory;
  if(!finished && !backend->lost) lose();
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
  Target* t = nullptr;
  for(u32 page = offset / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    if(!owners[page]) continue;
    if(t && owners[page] != t) return false;
    t = owners[page];
  }
  if(!t || t->stale || !direct || t->format != texture.format || t->stride != texture.bufferWidth) return false;
  if(offset < t->address) return false;
  //(a texture wider than the target's row, as a 3D PRIM's reach isn't known: only the columns inside it copied, those
  //past it, in memory the next rows' first, left as they are; the design's accuracy notes count it)
  u32 rowBytes = t->stride * bytes, y = (offset - t->address) / rowBytes;
  u32 x = (offset - t->address) % rowBytes / bytes;
  if(x >= t->stride || y + rows > t->height) return false;
  if(t->besideReaches(x, y, x + width - 1, y + rows - 1)) return false;  //(memory's newer bytes in it: decoded)
  held = Held{t, s32(x), s32(y), width, rows, texture.format};
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
  bool finished = backend->finish(recorded);
  statistics.waiting += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - began).count();
  recorded.clear();
  const u32* colors;
  const u8* stencil;
  if(!finished || !backend->read(0, colors, stencil)) {
    if(!backend->lost) lose();
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
  if(!backend->submit(recorded) && !backend->lost) lose();
  recorded.clear();
  statistics.submits++, statistics.presents++;
  return true;
}

auto GPU::show(const std::vector<u32>& pixels, u32 width, u32 height) -> void {
  if(!presents() || !width || !height || pixels.size() < u64(width) * height) return;
  Command c{Command::Kind::Present, 0};
  c.width = width, c.height = height, c.colors = recorded.uploads.size();
  auto bytes = (const u8*)pixels.data();
  recorded.uploads.insert(recorded.uploads.end(), bytes, bytes + u64(width) * height * 4);
  recorded.commands.push_back(c);
  if(!backend->submit(recorded) && !backend->lost) lose();
  recorded.clear();
  statistics.submits++, statistics.presentsFromMemory++;
}

auto GPU::drop() -> void {
  recorded.clear();
  held.reset();
  target = nullptr, drawing = false;
  for(auto& owner : owners) owner = nullptr;
  if(!backend) return;
  for(auto& t : targets) backend->dropTarget(t->id);
  for(auto& [decoded, texture] : textures) backend->dropTexture(texture.id);
  for(auto& [where, copied] : copies) backend->dropTexture(copied.texture);
  targets.clear(), textures.clear(), copies.clear();
}

auto GPU::resolution(u32 scale) -> void {
  drop();
  if(backend) backend->scale = std::clamp<u32>(scale, 1, backend->mostScale);
}

#include "check.cpp"

}
