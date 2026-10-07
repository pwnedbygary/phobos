#include "gpu.hpp"
#include "../../memory/memory.hpp"
#include "shaders/shaders.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

//Vulkan's types only: every function comes through vkGetInstanceProcAddr (vulkan.cpp)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>

namespace ares::PlayStationPortable {

#include "vulkan.cpp"

//A screen position as the GE holds it (draw.cpp's fixed()): sixteenths of a pixel, wild values held to its range.
static auto fixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

//The filter for a primitive (draw.cpp's chooseFilter()): TEXTURE_FILTER's for enlarging if a texel covers a pixel
//or more, else its for shrinking.
static auto chooseFilter(u32 filter, float texelsPerPixel) -> bool {
  return texelsPerPixel <= 1.0f ? filter >> 8 & 1 : filter & 1;
}

//A frame buffer's pixel as 8888 (red in the low byte, the stencil in the alpha), and back (pixel.cpp's widenPixel()
//and narrowPixel(); a 5650 one has no stencil, read as 0).
static auto widen(u32 pixel, u32 format) -> u32 {
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
static auto narrow(u32 color, u32 format) -> u32 {
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
//is as many rows as fit in VRAM, up to 512; rows: how many the PRIM reaches.
auto GPU::targetFor(const GE::PixelState& p, u32 rows) -> Target* {
  u32 bytes = p.format == 3 ? 4 : 2;
  for(auto& t : targets) {
    if(t->address == p.frameBuffer && t->stride == p.stride && t->format == p.format) {
      t->used = ++uses;
      return t.get();
    }
  }
  u32 height = std::min<u32>(512, (Memory::VRAMSize - p.frameBuffer) / (p.stride * bytes));
  if(height < rows || !height) return nullptr;
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
//their alpha) and the depth buffer's (VRAM's fourth copy's order, memory.hpp), which the GE has with the frame
//buffer (p). Its bytes are then watched, so that whoever changes them is heard of (written()).
auto GPU::fill(Target& t, u32 from, u32 to, const GE::PixelState& p) -> void {
  if(from >= to) return;
  u32 width = t.stride, height = to - from, count = width * height, bytes = t.bytes();
  auto& vram = ge->memory.vram;
  Command c{Command::Kind::Upload, t.id};
  c.x = 0, c.y = from, c.width = width, c.height = height;
  c.colors = recorded.uploads.size();
  c.stencil = c.colors + count * 4;
  c.depth = c.stencil + count;
  recorded.uploads.resize(c.depth + count * 2);
  u8* out = recorded.uploads.data();
  for(u32 y = 0; y < height; y++) {
    for(u32 x = 0; x < width; x++) {
      u32 at = (t.address + ((from + y) * t.stride + x) * bytes) & (Memory::VRAMSize - 1), pixel = 0;
      std::memcpy(&pixel, &vram[at], bytes);
      u32 color = widen(pixel, t.format);
      u32 n = y * width + x;
      std::memcpy(out + c.colors + n * 4, &color, 4);
      out[c.stencil + n] = color >> 24;
      u16 depth = 0;
      if(x < p.depthStride) {
        u32 offset = Memory::vramOffset(3, (p.depthBuffer + ((from + y) * p.depthStride + x) * 2) &
                                              (Memory::VRAMSize - 1));
        depth = vram[offset] | vram[offset + 1] << 8;
      }
      std::memcpy(out + c.depth + n * 2, &depth, 2);
    }
  }
  recorded.commands.push_back(c);
  t.version++;
  ge->memory.watch(Memory::VRAMBase + t.address + from * t.stride * bytes, height * t.stride * bytes);
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
  if(!added) return;
  memory.vramBusy = true;
  if(memory.vramGuard) memory.vramGuard(true);
  //(owned first: the change to a watched page of a target's isn't someone else's, written())
  memory.changed(Memory::VRAMBase + low, high + bytes - low);
}

//The PRIM's texture on the GPU: one taken from a target (holds()), copied out of it now, its texels then read as
//the target's format keeps them; or the GPU's copy of a decoded texture (put there now if it isn't yet, or has
//fewer rows than the GE's copy). 0 for none, where the GE has none decoded (it then draws untextured, which the
//design's accuracy notes count).
auto GPU::textureFor(const GE::Look& look, u8& texels) -> u32 {
  texels = 4;
  if(held && look.textured) {  //(copied from its target, in order with what's drawn)
    Held h = *held;
    held.reset();
    texels = h.format < 3 ? h.format : 4;
    u32 below = h.y + h.rows;  //(rows the GPU hasn't drawn, memory's, filled first)
    if(below > h.target->rows) fill(*h.target, h.target->rows, below, look.pixel), h.target->rows = below;
    auto key = std::make_tuple(h.target->id, h.x, h.y, h.width, h.rows);
    auto found = copies.find(key);
    if(found != copies.end() && found->second.version == h.target->version) return found->second.texture;
    u32 id = found != copies.end() ? found->second.texture : backend->makeTexture(h.width, h.rows, nullptr);
    if(!id) return 0;
    copies[key] = {id, h.target->version};
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
    textured = look.textured && (s.texture = textureFor(look, k.texels));
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
      auto operation = [](u32 o) -> u8 { return o < 6 ? u8(o) : Keep; };
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
      k.operation = p.blendOperation == 5 ? Maximum : p.blendOperation;  //(absolute difference: the larger)
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
  s32 left = std::max(p.left, 0), top = std::max(p.top, 0);
  s32 right = std::min<s32>(p.right, target->stride - 1), bottom = std::min<s32>(p.bottom, target->height - 1);
  s.scissor[0] = left, s.scissor[1] = top, s.scissor[2] = right - left + 1, s.scissor[3] = bottom - top + 1;
  state = s;
}

//A PRIM's settings: its target found (filled from memory first if need be), its pages owned, its texture on the
//GPU, its pipeline and the rest worked out, for its primitives to draw with.
auto GPU::begin(GE& ge, const GE::Look& look, bool through, const GE::Region& region) -> void {
  this->ge = &ge;
  drawing = false;
  if(!ready()) return;
  //(a long list's draws handed over as they come, so the GPU draws while the CPU goes on)
  if(recorded.commands.size() >= SubmitEvery) submit(ge);
  auto& p = look.pixel;
  //(where it may draw: the scissor rectangle, or less, where its vertices reach, in 2D: draw.cpp)
  if(!p.stride || region.left > region.right || region.top > region.bottom || region.right < 0 || region.bottom < 0) {
    return;
  }
  s32 bottom = region.bottom;
  Target* t = targetFor(p, std::min<u32>(bottom + 1, 512));
  if(!t) return;
  bottom = std::min<s32>(bottom, t->height - 1);
  s32 left = std::max(region.left, 0), top = std::max(region.top, 0);
  s32 right = std::min<s32>(region.right, t->stride - 1);
  if(left > right || top > bottom) return;
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
  if(!ready()) return;
  if(t->stale) t->rows = 0, t->stale = false;
  if(u32(bottom + 1) > t->rows) fill(*t, t->rows, bottom + 1, p), t->rows = bottom + 1;
  target = t;
  this->through = through;
  filter = ge.commands[GE::TextureFilter];
  shade = ge.commands[GE::ShadeMode];
  textured = false;
  settings(look);
  own(*t, left, top, right, bottom);
  t->version++;
  drawing = true;
}

//Vertices for the GPU, in the PRIM's draw: one with the last if its settings are the same.
auto GPU::emit(const Vertex* vertices, u32 count) -> void {
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
  o.x = fixed(v.x) / 16.0f, o.y = fixed(v.y) / 16.0f, o.z = v.z;
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
    f64 area = std::abs(f64(fixed(b.x) - fixed(a.x)) * (fixed(c.y) - fixed(a.y)) -
                        f64(fixed(b.y) - fixed(a.y)) * (fixed(c.x) - fixed(a.x)));
    float texels = std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u));
    bool linear = chooseFilter(filter, std::sqrt(texels / (area / 256.0f)));
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
static auto square(GPU::Vertex base, s32 x, s32 y, GPU::Vertex (&v)[6]) -> void {
  for(u32 n = 0; n < 6; n++) {
    static constexpr u8 right[6] = {0, 1, 0, 1, 1, 0}, down[6] = {0, 0, 1, 0, 1, 1};
    v[n] = base;
    v[n].x = f32(x + right[n]), v[n].y = f32(y + down[n]);
  }
}

auto GPU::point(const GE::Vertex& at) -> void {
  if(!drawing) return;
  Vertex base = vertex(at, false), v[6];
  base.flags = textured && chooseFilter(filter, 1.0f);
  square(base, fixed(at.x) >> 4, fixed(at.y) >> 4, v);
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
    square(base, pixel.x, pixel.y, v);
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
        u32 pixel = narrow((colors[i] & 0xff'ffff) | u32(stencil[i]) << 24, t.format);
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
    if(t->rows) memory.watch(Memory::VRAMBase + t->address, t->rows * t->stride * t->bytes());
  }
  for(auto texture = textures.begin(); texture != textures.end();) {  //(those the GE has let go)
    if(texture->second.decoded.expired()) backend->dropTexture(texture->second.id), texture = textures.erase(texture);
    else texture++;
  }
}

//A watched page someone else changed: a target over it, filled from memory before it's next drawn into. (A page
//the renderer owns is its own drawing's: own() tells memory of it.)
auto GPU::written(GE& ge, u32 page) -> void {
  u32 base = Memory::VRAMBase / Memory::PageSize;
  if(page < base || page >= base + GE::VRAMPages) return;
  page -= base;
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
  held = Held{t, s32(x), s32(y), width, rows, texture.format};
  return true;
}

//Memory's VRAM replaced whole: what the GPU drew since the last finish put back first (memory's own serialize and
//power have done that already, as they finish drawing), then every target filled afresh when next drawn into.
auto GPU::forget(GE& ge) -> void {
  finish(ge);
  for(auto& t : targets) t->stale = true;
}

}
