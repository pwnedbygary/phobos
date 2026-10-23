#include "gpu.hpp"
#include "../../memory/memory.hpp"
#include "shaders/shaders.hpp"

#include <bit>
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

//A 16-bit texel (or palette entry) of format 0-2 as 8888, as texture.cpp's widen16() makes it: a 5650 one opaque.
static auto widenTexel(u32 texel, u32 format) -> u32 {
  return widenTarget(texel, format) | (format == 0 ? 0xff00'0000 : 0);
}

//A stencil (or the bits of one a write may change) as a frame buffer of format keeps it, read as the GE reads it:
//a 5551 one's top bit as 0 or 255, a 4444 one's top four bits twice over, an 8888 one's as it is (5650: none). The
//GPU's stencil holds it so, as fills and moves put it there, for its tests to compare as the GE's do.
static auto keptStencil(u32 value, u32 format) -> u32 {
  return widenTarget(narrowTarget(value << 24, format), format) >> 24;
}

//The GE's comparisons (0 never, 1 always, 2 equal, 3 not equal, 4 less, 5 less or equal, 6 greater, 7 greater or
//equal), as pixel.cpp makes them
static auto comparesTrue(u32 comparison, u32 a, u32 b) -> bool {
  switch(comparison & 7) {
  case 0: return false;
  case 1: return true;
  case 2: return a == b;
  case 3: return a != b;
  case 4: return a < b;
  case 5: return a <= b;
  case 6: return a > b;
  }
  return a >= b;
}

//Rows of a target's pixels in memory's VRAM, from at (a VRAM offset, each wrapping at VRAM's end), stride pixels
//apart, as 8888s and their stencils (fill()'s), in a frame buffer of Format.
template<u32 Format>
static auto widenRows(const u8* vram, u32 at, u32 stride, u32 width, u32 height, u8* colors, u8* stencils) -> void {
  constexpr u32 bytes = Format == 3 ? 4 : 2;
  for(u32 y = 0; y < height; y++, at += stride * bytes) {
    for(u32 x = 0, from = at; x < width; x++, from += bytes) {
      u32 pixel = 0;
      std::memcpy(&pixel, vram + (from & (Memory::VRAMSize - 1)), bytes);
      u32 color = widenTarget(pixel, Format);
      std::memcpy(colors, &color, 4), colors += 4;
      *stencils++ = color >> 24;
    }
  }
}

GPU::GPU(std::unique_ptr<Backend> backend) : backend(std::move(backend)) {}

GPU::~GPU() {
  if(!backend) return;
  for(auto& t : targets) backend->dropTarget(t->id);
  for(auto& [decoded, texture] : textures) backend->dropTexture(texture.id);
  for(auto& p : palettes) backend->dropPalette(p.id);
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
  if(ge) ge->reportLost();  //(threaded: the emulation thread's model takes PRIMs as the software renderer's from then)
  tell("the GPU stopped answering; the software renderer draws from now on");
}

//The target for a frame buffer, made if there's none yet: the frame buffer of its width and format that starts
//where its page does, where a page holds whole rows of it (rows whose bytes divide 4,096, from VRAM's start), else
//where its row does; and the row and column of it the frame buffer starts at. So small frame buffers side by side in
//the rows of a wide one are parts of one target (God of War draws effects into four 5650 ones 64 pixels wide in rows
//of 1,024 pixels, and 8888 ones side by side in rows of 512), and drawing into one after another, and a texture read
//across them, need nothing put back; and every page of a frame buffer of such rows is whole rows of a target, so its
//pixels can go from one target to another on the GPU (move()). One whose PRIM reaches past its target's rows' end
//(which the PSP's addresses run on into the next row) or last row, or is drawn with its depth buffer, is a target of
//its own. The same bytes as another format are another target. Its height is as many rows as fit in VRAM, up to 512
//(a PRIM reaching further is the software renderer's: begin()). None where not a row fits, or the GPU has no room.
auto GPU::targetFor(const GE::PixelState& p, const GE::Region& region, u32& column, u32& row) -> Target* {
  u32 address, height;
  GE::targetPlace(p, region, address, row, column, height);  //(the emulation thread's model takes the same: model.cpp)
  for(auto& t : targets) {
    if(t->address == address && t->stride == p.stride && t->format == p.format) {
      t->used = ++uses;
      return t.get();
    }
  }
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
  if(!id) return roomless = true, nullptr;
  auto t = std::make_unique<Target>();
  t->address = address, t->stride = p.stride, t->format = p.format;
  t->id = id, t->height = height, t->used = ++uses;
  targets.push_back(std::move(t));
  return targets.back().get();
}

//Rows from to to of the target filled from memory's VRAM (columns left to right of them), in order with what's
//drawn: its pixels (the stencil their alpha: parts bit 0) or its depth buffer's (bit 1: the target's, in the order
//of VRAM's copy the GE keeps it in for the target's format, memory.hpp; and where other targets drew in those bytes
//on the GPU, their pixels there, as a finish would put them in memory: moveIntoDepth(); a depth they're put in all of
//isn't taken from memory first). Their bytes are then watched, so that whoever changes them is heard of (written()).
auto GPU::fill(Target& t, u32 from, u32 to, const GE::PixelState& p, u8 parts, u32 left, u32 right) -> void {
  right = std::min(right, t.stride - 1);
  if(from >= to || left > right) return;
  (void)p;
  u32 width = right - left + 1, height = to - from, count = width * height, bytes = t.bytes();
  u32 depthCopy = Memory::depthCopy(t.format);
  u8 taken = parts;  //(the parts taken from memory)
  if(parts & 2 && depthCovered(t, from, to, right)) taken &= ~2, statistics.depthsCovered++;
  if(taken) {
    Command c{Command::Kind::Upload, t.id};
    c.x = left, c.y = from, c.width = width, c.height = height, c.format = t.format;
    //(the parts it has: the colors and stencils, the depths)
    c.colors = recorded.uploads.size();
    c.stencil = c.colors + (taken & 1 ? count * 4 : 0);
    c.depth = c.stencil + (taken & 1 ? count : 0);
    c.parts = taken;
    recorded.uploads.resize(c.depth + (taken & 2 ? count * 2 : 0));
    u8* out = recorded.uploads.data();
    if(taken & 1) {  //(memory's as of the PRIM's place, threaded: GE::readingVRAM())
      u32 at = t.address + (from * t.stride + left) * bytes;
      ge->readingVRAM(at, at + u64((height - 1) * t.stride + width) * bytes - 1, [&](const u8* vram) {
        auto rows = [&](auto widen) { widen(vram, at, t.stride, width, height, out + c.colors, out + c.stencil); };
        if(t.format == 0) rows(widenRows<0>);
        if(t.format == 1) rows(widenRows<1>);
        if(t.format == 2) rows(widenRows<2>);
        if(t.format == 3) rows(widenRows<3>);
      });
    }
    if(taken & 2) {
      u64 low = (u64(t.depthBuffer) + u64(from) * t.depthStride * 2) & ~u64(0x3fff);
      u64 high = (u64(t.depthBuffer) + u64(to) * t.depthStride * 2 - 1) | 0x3fff;
      ge->readingVRAM(low, high, [&](const u8* vram) {
        for(u32 y = 0; y < height; y++) {
          for(u32 x = left; x <= right; x++) {
            u16 depth = 0;
            if(x < t.depthStride) {
              u32 offset = Memory::vramOffset(depthCopy, (t.depthBuffer + ((from + y) * t.depthStride + x) * 2) &
                                                           (Memory::VRAMSize - 1));
              depth = vram[offset] | vram[offset + 1] << 8;
            }
            std::memcpy(out + c.depth + (y * width + x - left) * 2, &depth, 2);
          }
        }
      });
    }
    recorded.commands.push_back(c);
    statistics.uploads++;
  }
  if(parts & 1) changed(t, left, from, right, to - 1);
  if(parts & 1) ge->memory.watch(Memory::VRAMBase + t.address + from * t.stride * bytes, height * t.stride * bytes);
  if(parts & 2 && t.depthStride) {  //(through that copy, as the GE sees it)
    ge->memory.watch(Memory::VRAMBase + depthCopy * Memory::VRAMSize + t.depthBuffer + from * t.depthStride * 2,
                     height * t.depthStride * 2);
  }
  if(parts & 2) moveIntoDepth(t, from, to, left, right);
}

//VRAM's pages rows from to to of a depth buffer are in, each(page): all of each 16 KiB the rows reach, which the
//second and fourth copies rearrange; a depth buffer's bytes past VRAM's end are its start's, as the GE's.
template<typename Each>
static auto depthPages(u32 depthBuffer, u32 depthStride, u32 from, u32 to, const Each& each) -> void {
  if(from >= to || !depthStride) return;
  u64 low = (u64(depthBuffer) + u64(from) * depthStride * 2) & ~u64(0x3fff);
  u64 high = (u64(depthBuffer) + u64(to) * depthStride * 2 - 1) | 0x3fff;
  u64 pages = std::min<u64>((high - low + 1) / Memory::PageSize, GE::VRAMPages);
  for(u64 n = 0; n < pages; n++) each(u32((low / Memory::PageSize + n) % GE::VRAMPages));
}

//The PSP's depth buffer is memory, which a frame buffer drawn over its bytes changes: the Kurohyou games draw their
//pre-rendered depth into the depth buffer's memory as a 5650 frame buffer and test their characters against it,
//WipEout Pure tests its HUD against the bloom it draws there, and a dozen games draw their post-processing there,
//then clear it or write over it. Depth filled from memory into t, where other targets drew those bytes on the GPU
//since the last finish, is what a finish would put there then: each target's pixels, in its drawn rectangle in the
//pages it owns, the newest of those memory has newer filled into it first (its dead ones, as a move does), put into
//t's depth on the GPU (a Move into the depth, parts 2: move.frag's DEPTH build), each depth the two bytes of the
//pixel at the depth's bytes (an 8888 one's half) narrowed to its format as a read back for memory narrows it. Not t's
//own pages, which its own depth can't take from it (begin() finishes first where those and another's meet: a finish
//would put both in memory). The pages are watched for the drawing again (depthSources, own()).
auto GPU::moveIntoDepth(Target& t, u32 from, u32 to, u32 left, u32 right) -> void {
  if(!backend->depthMoves || !t.depthStride) return;
  right = std::min({right, t.stride - 1, t.depthStride - 1});
  if(from >= to || left > right) return;
  std::vector<Moving> sources;  //(the pages' owners, a run of pages each; none where t owns one)
  bool own = false;
  depthPages(t.depthBuffer, t.depthStride, from, to, [&](u32 page) {
    Target* owner = owners[page];
    own = own || owner == &t;
    if(!owner || owner == &t) return;
    if(!sources.empty() && sources.back().from == owner && sources.back().last + 1 == page) sources.back().last = page;
    else sources.push_back({owner, page, page});
  });
  if(own) return;
  u32 rowBytes = t.depthStride * 2;
  bool wraps = u64(t.depthBuffer) + u64(to) * rowBytes > Memory::VRAMSize;
  for(auto& m : sources) {
    for(u32 page = m.first; page <= m.last; page++) depthSources[page >> 6] |= 1ull << (page & 63);
    sourcesFirst = std::min(sourcesFirst, m.first), sourcesLast = std::max(sourcesLast, m.last);
    Target& o = *m.from;
    u32 low = m.first * Memory::PageSize, high = (m.last + 1) * Memory::PageSize - 1;
    s32 fromRow = o.stride * o.bytes();
    if(!o.drawn() || high < o.address) continue;
    s32 top = std::max<s32>(o.top, low > o.address ? s32(low - o.address) / fromRow : 0);
    s32 bottom = std::min<s32>(o.bottom, s32(high - o.address) / fromRow);
    if(top > bottom) continue;
    if(o.deadReaches(o.left, top, o.right, bottom)) revive(o, o.dead);
    //(t's rows whose depths' bytes can be in those pages' 16 KiB)
    s32 first = from, last = to - 1;
    u32 blockLow = low & ~0x3fffu, blockHigh = high | 0x3fff;
    if(!wraps) {
      if(blockHigh < t.depthBuffer) continue;
      if(blockLow > t.depthBuffer) first = std::max<s32>(first, (blockLow - t.depthBuffer) / rowBytes);
      last = std::min<s32>(last, (blockHigh - t.depthBuffer) / rowBytes);
    }
    if(first > last) continue;
    Command c{Command::Kind::Move, t.id};
    c.parts = 2, c.source = o.id, c.sourceFormat = o.format, c.format = t.format;
    c.sourceX = o.left, c.sourceY = top, c.sourceWidth = o.right + 1 - o.left, c.sourceHeight = bottom + 1 - top;
    c.x = left, c.y = first, c.width = right + 1 - left, c.height = last + 1 - first;
    c.delta = s32(t.depthBuffer + (first * t.depthStride + left) * 2);
    c.rowBytes = rowBytes, c.sourceRowBytes = fromRow;
    c.address = o.address, c.low = low, c.high = high;
    recorded.commands.push_back(c);
    statistics.depthsMoved++;
  }
}

//(whether filling rows from to to of a depth buffer into t from memory leaves them as a finish then the fill would:
//no other target drew in their pages on the GPU since the last finish, or the GPU puts those pixels into its depth
//(moveIntoDepth(): where it can, and t drew in none of those pages itself))
auto GPU::depthMovable(const Target& t, u32 depthBuffer, u32 depthStride, u32 from, u32 to) const -> bool {
  bool others = false, own = false;
  depthPages(depthBuffer, depthStride, from, to, [&](u32 page) {
    others = others || (owners[page] && owners[page] != &t);
    own = own || owners[page] == &t;
  });
  return !others || (backend->depthMoves && !own);
}

//(whether moveIntoDepth() puts pixels in every depth of t's rows from to to, columns to right: all of each 16 KiB
//they reach in one other target's pages and its drawn rectangle's full rows, short of VRAM's end; each depth of
//them then the bytes a finish would put there, none of memory's)
auto GPU::depthCovered(const Target& t, u32 from, u32 to, u32 right) const -> bool {
  if(!backend->depthMoves || from >= to || right >= t.depthStride) return false;
  u64 low = (u64(t.depthBuffer) + u64(from) * t.depthStride * 2) & ~u64(0x3fff);
  u64 high = (u64(t.depthBuffer) + u64(to) * t.depthStride * 2 - 1) | 0x3fff;
  if(high >= Memory::VRAMSize) return false;
  for(u64 block = low; block < high; block += 0x4000) {
    const Target* o = owners[block / Memory::PageSize];
    if(!o || o == &t || !o->drawn() || o->left || o->right != s32(o->stride) - 1 || block < o->address) return false;
    for(u64 page = block; page < block + 0x4000; page += Memory::PageSize) {
      if(owners[page / Memory::PageSize] != o) return false;
    }
    u64 fromRow = o->stride * o->bytes();
    if(s32((block - o->address) / fromRow) < o->top || s32((block + 0x3fff - o->address) / fromRow) > o->bottom) {
      return false;
    }
  }
  return true;
}

//The newest depth drawn in t's depth buffer, where another target's depth has it (the last to draw with that depth
//buffer and row width, not t), taken into t's depth on the GPU before t draws with it, for the rows and columns that
//one has: the PSP's depth buffer is memory, whichever frame buffer it's drawn with, where each target's depth is its
//own. Hammerin' Hero, Jeanne d'Arc and Colin McRae Rally draw their scene into the screen's frame buffer, then their
//characters or shadows into a frame buffer of their own, cleared without its depth, tested against the scene's.
//Memory's changes the other hasn't filled yet are filled into it first, so what's taken is as new as memory's; t's
//own in what's taken are those. Where the other is narrower, t's columns past it are as they'd be without it.
auto GPU::newestDepth(const Target& t, u32 depthBuffer, u32 depthStride, u64 at) const -> Target* {
  Target* newest = nullptr;
  for(auto& other : targets) {
    if(other.get() == &t || other->depthBuffer != depthBuffer || other->depthStride != depthStride) continue;
    if(other->depthRows && other->depthAt > at && (!newest || other->depthAt > newest->depthAt)) newest = other.get();
  }
  return newest;
}

auto GPU::takeDepth(Target& t, Target* newest, const GE::PixelState& p) -> void {
  if(!newest) return;
  Target& from = *newest;
  if(from.depthChangedFrom < from.depthChangedTo) {
    fill(from, from.depthChangedFrom, std::min(from.depthChangedTo, from.depthRows), p, 2);
    from.depthChangedFrom = from.depthChangedTo = 0;
  }
  u32 rows = std::min(from.depthRows, t.height), width = std::min({from.stride, t.stride, t.depthStride});
  Command c{Command::Kind::Depth, t.id};
  c.source = from.id, c.width = width, c.height = rows;
  //(one of another format reads the depth buffer's bytes in another copy's order, memory.hpp, Twisted Metal's small
  //5551 frame buffers its 8888 scene's: the two orders move 32-byte pieces whole, so each run of 16 of this one's
  //depths is 16 of the other's, those its order puts at the same bytes, taken from there, a run down as many rows as
  //move alike. Where some lie past the other's rows or columns, or the pieces don't start on 32 bytes, its depth goes
  //through memory instead: put in its order, filled into this one in this one's.)
  if(Memory::depthCopy(from.format) != Memory::depthCopy(t.format)) {
    u32 across = std::min(t.stride, t.depthStride), sourceAcross = std::min(from.stride, t.depthStride);
    bool placed = !(t.depthBuffer & 31) && !(t.depthStride & 15);
    auto& runs = recorded.runs;
    c.firstRun = runs.size(), c.width = across;  //(the runs cover its columns and rows once each)
    for(u32 x = 0; x < across && placed; x += 16) {
      u16 run = std::min(16u, across - x);
      for(u32 y = 0; y < rows && placed; y++) {
        u32 seen = (t.depthBuffer + (y * t.depthStride + x) * 2) & (Memory::VRAMSize - 1);
        u32 there = Memory::vramSeen(Memory::depthCopy(from.format),
                                     Memory::vramOffset(Memory::depthCopy(t.format), seen));
        u32 at = ((there - t.depthBuffer) & (Memory::VRAMSize - 1)) / 2;
        u32 sourceX = at % t.depthStride, sourceY = at / t.depthStride;
        if(!(placed = sourceY < rows && sourceX + run <= sourceAcross)) break;
        bool down = y && runs.size() > c.firstRun && runs.back().x == x && runs.back().sourceX == sourceX &&
                    runs.back().sourceY + runs.back().height == sourceY;
        if(down) runs.back().height++;
        else runs.push_back({u16(x), u16(y), u16(sourceX), u16(sourceY), run, 1});
      }
    }
    if(!placed) {
      runs.resize(c.firstRun);
      u64 end = u64(t.depthBuffer) + u64(std::max(from.depthRows, 1u)) * t.depthStride * 2;
      return putDepth(t.depthBuffer, u32(std::min<u64>(end, Memory::VRAMSize) - 1));
    }
    c.runs = runs.size() - c.firstRun;
  } else if(width < std::min(t.stride, t.depthStride)) {
    if(t.depthChangedFrom < t.depthChangedTo) {
      fill(t, t.depthChangedFrom, std::min(t.depthChangedTo, t.depthRows), p, 2);
      t.depthChangedFrom = t.depthChangedTo = 0;
    }
    if(rows > t.depthRows) fill(t, t.depthRows, rows, p, 2), t.depthRows = rows;
  }
  recorded.commands.push_back(c);
  t.depthRows = std::max(t.depthRows, rows);
  t.depthChangedFrom = std::max(t.depthChangedFrom, rows);
  if(t.depthChangedFrom >= t.depthChangedTo) t.depthChangedFrom = t.depthChangedTo = 0;
  t.depthAt = from.depthAt, t.depthKept = from.depthKept;
  statistics.depthsTaken++;
}

//The newest depth the GPU drew in each depth buffer reaching VRAM's bytes first to last, where memory hasn't it, put
//into memory's VRAM as the GE keeps its depth (through the copy its target's format has: memory.hpp's depthCopy()),
//the older depth buffers' first where two reach the same bytes: a texture is about to be read from them through
//VRAM's depth views (holds()), which on the PSP finds the depth drawn there (Cars Race-O-Rama takes its race's depth
//through a palette into the alpha its sky is blended by, Kurohyou its depth for its depth of field). A read back of
//those depths alone, waited for (each target's own changes from memory filled into it first, so none is lost); the
//targets' pixels and pages stay the GPU's, but for a texture those in its bytes, which its decoding waits for anyway:
//put in memory first, the depth over them where it's newer. Of targets as new over one depth buffer, the
//widest's is put (not the columns of one older and wider than the newest). Memory hears of the bytes changed (decoded
//textures, the recompiler), and so do the targets over them, which fill them again, but those whose depth is what was
//put there, where no newer depth put after reaches it: their changes from memory are kept as they were. One that
//couldn't be read back isn't kept, for the next texture to take again. (The second and fourth copies rearrange each
//16 KiB: all of each 16 KiB they touch is taken; a depth buffer's bytes past VRAM's end are its start's, as the GE's.)
auto GPU::putDepth(u32 first, u32 last, bool finishing) -> void {
  first &= ~0x3fffu, last |= 0x3fff;
  auto bytes = [](const Target& t) -> std::pair<u64, u64> {  //(may run past VRAM's end)
    return {t.depthBuffer & ~0x3fffu, (u64(t.depthBuffer) + t.depthRows * t.depthStride * 2 - 1) | 0x3fff};
  };
  auto meets = [](std::pair<u64, u64> a, u64 low, u64 high) {  //(low to high inside VRAM)
    return (a.first <= high && low <= a.second) || (a.second >= Memory::VRAMSize && low <= a.second - Memory::VRAMSize);
  };
  auto overlap = [&](std::pair<u64, u64> a, std::pair<u64, u64> b) {
    if(meets(a, b.first, std::min<u64>(b.second, Memory::VRAMSize - 1))) return true;
    return b.second >= Memory::VRAMSize && meets(a, 0, b.second - Memory::VRAMSize);
  };
  auto width = [](const Target& t) { return std::min(t.stride, t.depthStride); };
  std::vector<Target*> put;  //(the newest over each depth buffer, where memory hasn't it)
  for(u32 i = 0; i < targets.size(); i++) {
    Target& t = *targets[i];
    if(!t.depthAt || t.depthKept || !t.depthRows || !t.depthStride || !overlap(bytes(t), {first, last})) continue;
    bool newest = true;  //(of those as new, the widest, then the first)
    for(u32 j = 0; j < targets.size() && newest; j++) {
      Target& other = *targets[j];
      if(j == i || other.depthBuffer != t.depthBuffer || other.depthStride != t.depthStride) continue;
      newest = other.depthAt < t.depthAt ||
               (other.depthAt == t.depthAt && (width(other) < width(t) || (width(other) == width(t) && j > i)));
    }
    if(newest) put.push_back(&t);
  }
  if(put.empty() || !ready()) return;
  //(where they were newer, the fills below take them: moveIntoDepth())
  if(finishing && drawnOver(*ge, first, last)) finish(*ge);
  std::sort(put.begin(), put.end(), [](Target* a, Target* b) { return a->depthAt < b->depthAt; });
  for(auto* t : put) {
    if(t->depthChangedFrom < t->depthChangedTo) {
      fill(*t, t->depthChangedFrom, std::min(t->depthChangedTo, t->depthRows), {}, 2);
      t->depthChangedFrom = t->depthChangedTo = 0;
    }
    Command c{Command::Kind::Readback, t->id};
    c.width = width(*t), c.height = t->depthRows, c.parts = 2;
    recorded.commands.push_back(c);
    recorded.readbacks++;
  }
  auto began = std::chrono::steady_clock::now();
  bool finished = backend->finish(recorded);
  statistics.waiting += std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now() - began).count();
  recorded.clear();
  if(!finished) return lose();
  auto& memory = ge->memory;
  std::vector<bool> done(put.size());
  for(u32 n = 0; n < put.size(); n++) {
    Target& t = *put[n];
    const u16* depths;
    u32 across = 0, down = 0;
    if(!backend->readDepth(n, depths, across, down) || across != width(t) || down != t.depthRows) continue;
    u32 depthCopy = Memory::depthCopy(t.format);
    auto [low, high] = bytes(t);
    ge->writingVRAM(low, high, [&](const auto& put) {  //(threaded, only those that change: GE::writingVRAM())
      for(u32 y = 0; y < down; y++) {
        for(u32 x = 0; x < across; x++, depths++) {
          u32 at = Memory::vramOffset(depthCopy, (t.depthBuffer + (y * t.depthStride + x) * 2) & (Memory::VRAMSize - 1));
          u8 depth[2] = {u8(*depths), u8(*depths >> 8)};
          put(at, depth, 2);
        }
      }
    });
    done[n] = true;
    ge->reportPuts(low, high);  //(threaded: its depth in memory, for the emulation thread's model)
    statistics.depthsPut++;
  }
  std::vector<std::pair<u32, u32>> changes;  //(each target's changes from memory, as they were)
  for(auto& t : targets) changes.push_back({t->depthChangedFrom, t->depthChangedTo});
  auto inParts = [](u64 at, u64 size, auto&& part) {  //(round VRAM's end)
    part(u32(at), u32(std::min<u64>(size, Memory::VRAMSize - at)));
    if(at + size > Memory::VRAMSize) part(0u, u32(at + size - Memory::VRAMSize));
  };
  for(u32 n = 0; n < put.size(); n++) {
    auto [low, high] = bytes(*put[n]);
    if(done[n]) inParts(low, high - low + 1, [&](u32 at, u32 size) { memory.changed(Memory::VRAMBase + at, size); });
  }
  for(u32 n = 0; n < put.size(); n++) {
    if(!done[n]) continue;
    Target& t = *put[n];
    bool over = false;  //(a newer depth put after reaches its bytes: those of its depth buffer fill them again)
    for(u32 later = n + 1; later < put.size(); later++) over |= done[later] && overlap(bytes(*put[later]), bytes(t));
    for(u32 i = 0; i < targets.size(); i++) {
      auto& other = *targets[i];
      if(other.depthBuffer != t.depthBuffer || other.depthStride != t.depthStride || other.depthAt != t.depthAt) {
        continue;
      }
      other.depthKept = true;
      if(!over) other.depthChangedFrom = changes[i].first, other.depthChangedTo = changes[i].second;
    }
    inParts(t.depthBuffer, u64(t.depthRows) * t.depthStride * 2, [&](u32 at, u32 size) {
      memory.watch(Memory::VRAMBase + Memory::depthCopy(t.format) * Memory::VRAMSize + at, size);
    });
  }
  release();
}

auto GPU::depthOf(u32 address, u32 stride, u32 format, std::vector<u16>& depths, u32& width, u32& height) -> bool {
  depths.clear(), width = height = 0;
  Target* t = nullptr;
  for(auto& candidate : targets) {
    if(candidate->address == address && candidate->stride == stride && candidate->format == format) {
      t = candidate.get();
    }
  }
  if(!ready() || !t || !t->depthRows || !t->depthStride) return false;
  Command c{Command::Kind::Readback, t->id};
  c.width = std::min(t->stride, t->depthStride), c.height = t->depthRows, c.parts = 2;
  u32 n = recorded.readbacks++;
  recorded.commands.push_back(c);
  bool finished = backend->finish(recorded);
  recorded.clear();
  if(!finished) return lose(), false;
  const u16* read;
  if(!backend->readDepth(n, read, width, height)) return false;
  depths.assign(read, read + u64(width) * height);
  release();
  return true;
}

//A rectangle (left, top, right, bottom; none: left past right) grown to take in another
static auto widen(s32 (&box)[4], s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(box[0] > box[2]) box[0] = left, box[1] = top, box[2] = right, box[3] = bottom;
  else {
    box[0] = std::min(box[0], left), box[1] = std::min(box[1], top);
    box[2] = std::max(box[2], right), box[3] = std::max(box[3], bottom);
  }
}

//Ranges of the target's bytes memory has the newest of (its dead ones, block transfers' or the CPU's over pixels it
//drew: overwritten(); or those beside them: Target::beside) filled from memory, and forgotten: the whole pixels
//they're in, in its filled rows (a range's first row from its first byte, its last to its last). Bytes beside its
//pixels all in a rectangle that reaches none it drew are filled as that rectangle, in one go: its other pixels are
//memory's too (Brave Story's list, written beside its picture a row at a time, is 16 ranges of 32 pixels).
auto GPU::revive(Target& t, std::vector<std::pair<u32, u32>>& ranges) -> void {
  if(&ranges == &t.dead) drawnSerial++;  //(drawn bytes again)
  if(&ranges == &t.beside) t.besideFull = false;
  u32 bytes = t.bytes(), rowBytes = t.stride * bytes;
  //(many ranges, or ranges close together, as one rectangle round them where it reaches none of its pixels)
  if(&ranges == &t.beside && ranges.size() > 1 && t.rows) {
    s32 box[4] = {0, 0, -1, -1};
    u64 kept = 0;  //(the ranges' pixels)
    for(auto [first, last] : ranges) {
      first = std::max(first, t.address), last = std::min(last, t.end() - 1);
      if(first > last) continue;
      s32 top = (first - t.address) / rowBytes, bottom = (last - t.address) / rowBytes;
      s32 left = (first - t.address) % rowBytes / bytes, right = (last - t.address) % rowBytes / bytes;
      if(top >= s32(t.rows)) continue;
      kept += (last - first) / bytes + 1;
      if(top != bottom) left = 0, right = t.stride - 1;
      widen(box, left, top, right, std::min<s32>(bottom, t.rows - 1));
    }
    bool apart = !t.drawn() || box[2] < t.left || t.right < box[0] || box[3] < t.top || t.bottom < box[1];
    u64 area = u64(box[2] - box[0] + 1) * (box[3] - box[1] + 1);
    if(box[0] <= box[2] && box[1] <= box[3] && apart && (ranges.size() > 8 || area <= kept * 4)) {
      fill(t, box[1], box[3] + 1, {}, 1, box[0], box[2]);
      return ranges.clear();
    }
  }
  for(auto [first, last] : ranges) {
    if(!t.rows) break;
    first = std::max(first, t.address), last = std::min(last, t.end() - 1);
    if(first > last) continue;
    u32 top = (first - t.address) / rowBytes, bottom = std::min((last - t.address) / rowBytes, t.rows - 1);
    u32 left = (first - t.address) % rowBytes / bytes, right = (last - t.address) % rowBytes / bytes;
    if(top > bottom) continue;
    if(top == bottom) { fill(t, top, top + 1, {}, 1, left, right); continue; }
    fill(t, top, top + 1, {}, 1, left), fill(t, top + 1, bottom, {}, 1);
    fill(t, bottom, bottom + 1, {}, 1, 0, (last - t.address) / rowBytes == bottom ? right : ~0u);
  }
  ranges.clear();
}

//A block transfer's write over VRAM's bytes first to last, or the CPU's store: the whole pixels of them in each
//target's drawn rows are dead (memory's from now on), where it has room to say so; the write then waits for
//nothing for those (drawnOver()), as for bytes beside its pixels (besideChanged()), and a finish leaves them be.
auto GPU::overwritten(GE& ge, u32 first, u32 last) -> void {
  this->ge = &ge;
  for(auto& t : targets) {
    if(!t->drawn() || !t->reaches(first, last, t->left, t->top, t->right, t->bottom)) continue;
    u32 bytes = t->bytes(), rowBytes = t->stride * bytes;
    u32 low = std::max(first, t->address + t->top * rowBytes);
    u32 high = std::min(last, t->address + (t->bottom + 1) * rowBytes - 1);
    low = t->address + (low - t->address + bytes - 1) / bytes * bytes;  //(whole pixels)
    if(high + 1 < low + bytes) continue;
    high = t->address + (high + 1 - t->address) / bytes * bytes - 1;
    auto& dead = t->dead;
    u32 joined = 0;
    for(auto& [from, to] : dead) {
      if(low > to + 1 || from > high + 1) continue;
      from = std::min(from, low), to = std::max(to, high), joined++;
    }
    if(!joined && dead.size() < 16) dead.push_back({low, high});
    if(joined > 1) {  //(those it joined together made one)
      std::sort(dead.begin(), dead.end());
      std::vector<std::pair<u32, u32>> merged;
      for(auto& range : dead) {
        if(!merged.empty() && range.first <= merged.back().second + 1) {
          merged.back().second = std::max(merged.back().second, range.second);
        } else merged.push_back(range);
      }
      dead = merged;
    }
  }
}

//The pixels left-right, top-bottom of the target are drawn on the GPU: VRAM's pages under them are the renderer's
//until it's finished (busy: whoever touches one waits for finish()), and whoever keeps a copy of those bytes (decoded
//textures, the recompiler) hears of the change, as the software renderer tells of what it draws. Not again for a
//rectangle inside one told of since the last finish: a copy of bytes the GPU has drawn over since then can only be
//made after a finish (drawnOver() has memory wait for one), so no one has those bytes yet. (A 3D game's PRIMs each
//reach the whole scissor rectangle: told once a frame, not once a PRIM.)
auto GPU::own(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(!t.drawn() || left < t.left || top < t.top || right > t.right || bottom > t.bottom) drawnSerial++;
  if(!t.drawn()) t.left = left, t.top = top, t.right = right, t.bottom = bottom;
  else {
    t.left = std::min(t.left, left), t.top = std::min(t.top, top);
    t.right = std::max(t.right, right), t.bottom = std::max(t.bottom, bottom);
  }
  auto& memory = ge->memory;
  u32 bytes = t.bytes();
  u32 low = t.address + (top * t.stride + left) * bytes, high = t.address + (bottom * t.stride + right) * bytes;
  u32 first = low / Memory::PageSize, last = std::min<u32>(high / Memory::PageSize, GE::VRAMPages - 1);
  //(told, and its pages busy still: memory may copy a page that isn't without a finish. A told rectangle's pages are
  //made busy as it's told, and stay so while memory.busyCleared stays as it was then, toldBusy: where it's gone up
  //since, they're told afresh)
  bool told = false;
  if(t.toldBusy != memory.busyCleared) t.told.clear(), t.toldBusy = memory.busyCleared;
  for(auto& r : t.told) told |= left >= r[0] && top >= r[1] && right <= r[2] && bottom <= r[3];
  //(where a depth took the pixels of pages it draws in since (moveIntoDepth()), the depths over them filled again
  //before they're used, as memory's change would have them: told or not)
  u32 from = std::max(first, sourcesFirst), to = std::min(last, sourcesLast);
  for(u32 page = from, block = ~0u; page <= to; page++) {
    if(!(depthSources[page >> 6] >> (page & 63) & 1) || page * Memory::PageSize >> 14 == block) continue;
    block = page * Memory::PageSize >> 14;
    depthChanged(block << 14);
  }
  if(told) return;
  if(t.told.size() < MostTold) t.told.push_back({left, top, right, bottom});
  //(not looked through where they're all its own and busy already: inside those it last went through, no page's owner
  //changed since, which drawnSerial would say, nor any busy page cleared, which busyCleared would. Ghost of Sparta's
  //menu fills told with small 2D PRIMs before its 3D ones, 17,000 a frame, each reaching 136 pages)
  bool held = t.heldSerial == drawnSerial && t.heldBusy == memory.busyCleared && first >= t.heldFirst &&
              last <= t.heldLast;
  bool added = false;
  for(u32 page = first; page <= last && !held; page++) {
    if(owners[page] == &t && memory.vramPageBusy(page)) continue;
    if(!memory.vramPageBusy(page)) added = true, memory.busyPages[page >> 6] |= 1ull << (page & 63);
    owners[page] = &t, drawnSerial++;
  }
  if(!held) t.heldFirst = first, t.heldLast = last, t.heldSerial = drawnSerial, t.heldBusy = memory.busyCleared;
  if(added) {
    memory.vramBusy = true;
    if(memory.vramGuard) memory.vramGuard(true);
  }
  //(owned first: the change to a watched page of a target's isn't someone else's, written())
  memory.changed(Memory::VRAMBase + low, high + bytes - low);
}

//Whether another target's pixels in pages first to last, which it has drawn in, can move into t on the GPU (move():
//as putting them in memory and filling t from it would leave t): pages of whole rows of both, each starting a row,
//every pixel it drew there in t's rows; not where memory has newer bytes among them (from's beside). Its rows may be
//another number of bytes than t's: the same memory drawn as frame buffers of two row widths (Colin McRae draws its
//480-pixel picture's memory as a 256-pixel one too, every frame). box: the rectangle moved, in from's pixels: its
//drawn columns (a 16-bit one's going into an 8888 one's widened to whole pixels of that: the newest of the pixels
//beside them are its too, in the pages it owns), its drawn rows in those pages.
auto GPU::movable(const Target& from, const Target& t, u32 first, u32 last, s32 (&box)[4]) const -> bool {
  box[0] = 0, box[1] = 0, box[2] = -1, box[3] = -1;
  u32 rowBytes = t.stride * t.bytes(), fromRow = from.stride * from.bytes();
  if(!backend->moves || Memory::PageSize % rowBytes || Memory::PageSize % fromRow || from.address % fromRow ||
     t.address % rowBytes) {
    return false;
  }
  //(rows of another width take whole rows of t, its pixels the box's bytes don't reach left as they are: their
  //stencils too, which needs the shader to give the stencil, where t has one)
  if(rowBytes != fromRow && t.format && !(backend->stencilExport && backend->moveExport)) return false;
  box[0] = from.left, box[2] = from.right;
  if(from.bytes() < t.bytes()) box[0] &= ~1, box[2] |= 1;
  box[1] = std::max<s32>(from.top, (s32(first * Memory::PageSize) - s32(from.address)) / s32(fromRow));
  box[3] = std::min<s32>(from.bottom, (s32((last + 1) * Memory::PageSize) - s32(from.address)) / s32(fromRow) - 1);
  if(box[1] > box[3]) return true;
  auto [top, bottom] = intoRows(from, t, box);
  if(top < 0 || bottom >= s32(t.height)) return false;
  return !from.besideReaches(box[0], box[1], box[2], box[3]);
}

//The rows of t that from's rows top to bottom of box (from's pixels) are the same bytes as, first and last.
auto GPU::intoRows(const Target& from, const Target& t, const s32 (&box)[4]) const -> std::pair<s32, s32> {
  s32 rowBytes = t.stride * t.bytes(), fromRow = from.stride * from.bytes();
  s32 low = s32(from.address) + box[1] * fromRow - s32(t.address);
  s32 high = s32(from.address) + (box[3] + 1) * fromRow - 1 - s32(t.address);
  auto rowOf = [&](s32 offset) { return offset >= 0 ? offset / rowBytes : -((-offset + rowBytes - 1) / rowBytes); };
  return {rowOf(low), rowOf(high)};
}

//from's pixels in the pages m names put into t on the GPU, as memory would have them (Command::Move): the pages are
//t's from now on, and so is putting those pixels back.
auto GPU::move(Target& from, Target& t, const Moving& m) -> void {
  s32 box[4];
  movable(from, t, m.first, m.last, box);
  for(u32 page = m.first; page <= m.last; page++) owners[page] = &t;
  drawnSerial++;
  if(box[1] > box[3]) return;
  u32 rowBytes = t.stride * t.bytes(), fromRow = from.stride * from.bytes();
  Command c{Command::Kind::Move, t.id};
  c.source = from.id, c.sourceFormat = from.format, c.format = t.format;
  c.sourceX = box[0], c.sourceY = box[1], c.sourceWidth = box[2] + 1 - box[0], c.sourceHeight = box[3] + 1 - box[1];
  if(rowBytes == fromRow) {  //(the rectangle the box is in t)
    c.x = box[0] * from.bytes() / t.bytes(), c.width = c.sourceWidth * from.bytes() / t.bytes();
    c.y = box[1] + (s32(from.address) - s32(t.address)) / s32(rowBytes), c.height = c.sourceHeight;
  } else {  //(rows of another width: the box's bytes are spans of whole rows of t's)
    auto [top, bottom] = intoRows(from, t, box);
    c.x = 0, c.width = t.stride, c.y = top, c.height = bottom - top + 1;
  }
  c.rowBytes = rowBytes, c.sourceRowBytes = fromRow;
  c.delta = s32(t.address + c.y * rowBytes + c.x * t.bytes()) - s32(from.address);
  s32 right = c.x + c.width - 1, bottom = c.y + c.height - 1;
  //(t's bytes memory has the newest of, over its older pixels, filled first where the move goes: it puts from's
  //newer ones over those, and the ranges no longer say memory's of them; and those beside its pixels in what its
  //drawn rectangle grows to, which a finish would put back over)
  s32 uLeft = c.x, uTop = c.y, uRight = right, uBottom = bottom;
  if(t.drawn()) {
    uLeft = std::min(uLeft, t.left), uTop = std::min(uTop, t.top);
    uRight = std::max(uRight, t.right), uBottom = std::max(uBottom, t.bottom);
  }
  if(t.besideReaches(uLeft, uTop, uRight, uBottom)) revive(t, t.beside);
  if(t.deadReaches(c.x, c.y, right, bottom)) revive(t, t.dead);
  recorded.commands.push_back(c);
  statistics.moves++;
  if(!t.drawn()) t.left = c.x, t.top = c.y, t.right = right, t.bottom = bottom;
  else {
    t.left = std::min(t.left, c.x), t.top = std::min(t.top, c.y);
    t.right = std::max(t.right, right), t.bottom = std::max(t.bottom, bottom);
  }
  changed(t, c.x, c.y, right, bottom);
}

//The target's pixels left-right, top-bottom changed (filled, drawn into): every copy taken of it is that much older,
//which the copies hear of as one is next taken (textureFor()).
auto GPU::changed(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void {
  if(left <= right && top <= bottom) widen(t.changes, left, top, right, bottom);
}

//The frame buffer's alpha is the stencil (pixel.cpp), which the GPU keeps apart from the colors' alpha: a PRIM
//writes the colors' alpha where its stencil operation's result is known (settings()), not where a stencil or depth
//test fails (MX vs. ATV Untamed's race zeroes the stencil behind the hills so, before taking the sky over the far
//pixels by the frame buffer's alpha), nor an increment's. Where they differ (Target::lags), before a PRIM blends by
//the frame buffer's alpha or a copy is taken over (left, top, right, bottom), the colors' alpha is set from the
//stencil there (Command::Alpha), a change the target's copies take again.
auto GPU::catchAlpha(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void {
  auto& r = t.lags;
  if(r[0] > r[2] || left > right || top > bottom) return;
  if(left > r[2] || r[0] > right || top > r[3] || r[1] > bottom) return;
  Command c{Command::Kind::Alpha, t.id};
  c.x = r[0], c.y = r[1], c.width = r[2] - r[0] + 1, c.height = r[3] - r[1] + 1, c.format = t.format;
  recorded.commands.push_back(c);
  changed(t, r[0], r[1], r[2], r[3]);
  r[0] = 0, r[1] = 0, r[2] = -1, r[3] = -1;
  statistics.alphas++;
}

//The last PRIM's pixels given to its target's changes, as the next begins (gathered as the corners' extent, a float
//compared at each primitive, not worked out at each), inside its scissor: those with a sample of the GPU's inside
//the extent (each of the PSP's pixels has scale x scale, the first and last half of one of them in from its edges),
//a 64th of a pixel more each way for the GPU's rounding of a sample on an edge. (The corners are the GE's sixteenths
//exactly. A pixel more each way, as it was, reached into the next of God of War's small frame buffers side by side
//in one target, and the copies of it were taken again for nothing, each a break in the render pass.)
auto GPU::reached() -> void {
  auto& [t, scissor, box] = reach;
  if(t && box[0] <= box[2]) {
    f32 in = 0.5f / backend->scale - 1.0f / 64;
    s32 left = std::max(s32(std::ceil(box[0] + in)) - 1, scissor[0]);
    s32 top = std::max(s32(std::ceil(box[1] + in)) - 1, scissor[1]);
    s32 right = std::min(s32(std::ceil(box[2] - in)) - 1, scissor[0] + scissor[2] - 1);
    s32 bottom = std::min(s32(std::ceil(box[3] - in)) - 1, scissor[1] + scissor[3] - 1);
    changed(*t, left, top, right, bottom);
  }
  reach = {};
}

//The PRIM's texture on the GPU: one taken from a target (holds()), copied out of it now, its texels then read as
//the target's format keeps them, at the target's resolution (scale: each texel scale x scale of the copy's), held
//inside those the PRIM takes where the copy has more (taken: Vertex::taken()); or the GPU's copy of a decoded texture
//(put there now if it isn't yet, or has fewer rows than the GE's copy), at the PSP's. 0 for none, where the GE has
//none decoded (it then draws untextured, which the design's accuracy notes count). A texture of palette indices
//taken from a target (CLUT16 from a 16-bit one, CLUT32 from an 8888 one: holds()) is the same copy, its texels the
//indices (indexed: Pipeline::indexed) looked up in the palette (palette: Push::palette, CLUT_FORMAT's with it).
auto GPU::textureFor(const GE::Look& look, u8& texels, u32& scale, u32& taken, u8& indexed, u32& palette) -> u32 {
  texels = 4, scale = 1, taken = 0, indexed = 0, palette = 0;
  if(held && look.textured) {  //(copied from its target, in order with what's drawn)
    Held h = *held;
    held.reset();
    //(its colors' alpha caught up with the stencil first where it lags, a change its copies take again, below; not
    //where the PRIM can't see the copy's alpha: palette indices take none of the stencil's bits (take()), a texture
    //function without the texel's alpha leaves it out, and a pixel's alpha counts only in the alpha test and blending
    //by the source's alpha, the alpha written being the stencil (pixel.cpp). A later PRIM that sees it catches it up
    //then, its copy taken again from the change that makes)
    auto& p = look.pixel;
    auto bySource = [](u32 factor) { return factor == 2 || factor == 3 || factor == 6 || factor == 7; };
    bool blendsBySource = p.blend && p.blendOperation < 3 && (bySource(p.blendSource) || bySource(p.blendDestination));
    if(!h.palette && look.withAlpha && (p.alphaTest || blendsBySource)) {
      catchAlpha(*h.target, h.x, h.y, h.x + h.width - 1, h.y + h.split - 1);
      if(h.next) catchAlpha(*h.next, h.x, 0, h.x + h.width - 1, h.rows - h.split - 1);
    }
    texels = h.target->format < 3 ? h.target->format : 4;
    if(h.palette) {
      auto& t = look.texture;
      indexed = h.target->format < 3 ? 1 : 2;
      palette = h.palette | (t.clutShift & 31) << 10 | (t.clutMask & 255) << 15 | (t.clutOffset >> 4 & 31) << 23 |
                (t.clutFormat != 3) << 28;
    }
    u32 below = h.y + h.split;  //(rows the GPU hasn't drawn, memory's, filled first)
    if(below > h.target->rows) fill(*h.target, h.target->rows, below, look.pixel), h.target->rows = below;
    if(h.next && h.rows - h.split > h.next->rows) {
      fill(*h.next, h.next->rows, h.rows - h.split, look.pixel), h.next->rows = h.rows - h.split;
    }
    //(the copy taken from that place, copied again in part where the target has changed since: the draws before it
    //sample it as it was, as the GPU runs the commands in order, and the rest is as the target still has it. One
    //with as many rows and columns as the PRIM takes, or more, serves it; one with fewer is made again larger, to
    //the next power of two each way, as far as the target's filled rows: God of War draws a picture in 272 tiles a
    //time, each taking more of the frame buffer it shrank the screen into than the one before, and copied for each
    //it was 2,184 copies a frame shown. With MostCopies kept, one of the size taken from another place of the target
    //serves a new place, copied whole, before the one unused longest is let go.)
    if(h.target->changes[0] <= h.target->changes[2]) {
      auto& changes = h.target->changes;
      for(auto& [where, copied] : copies) {
        if(std::get<0>(where) == h.target->id) widen(copied.dirty, changes[0], changes[1], changes[2], changes[3]);
      }
      changes[0] = 0, changes[1] = 0, changes[2] = -1, changes[3] = -1;
    }
    scale = backend->scale;
    auto key = std::make_tuple(h.target->id, h.x, h.y);
    auto found = copies.find(key);
    if(found != copies.end()) found->second.used = uses;
    if(found != copies.end() && !h.next && found->second.width >= h.width && found->second.rows >= h.rows) {
      auto& copied = found->second;
      if(copied.width != h.width || copied.rows != h.rows) taken = Vertex::taken(h.width, h.rows);
      auto& d = copied.dirty;
      if(d[0] > h.takes[2] || h.takes[0] > d[2] || d[1] > h.takes[3] || h.takes[1] > d[3]) return copied.texture;
      s32 left = std::max(copied.dirty[0], h.x), top = std::max(copied.dirty[1], h.y);
      s32 right = std::min({copied.dirty[2], h.x + s32(copied.width) - 1, s32(h.target->stride) - 1});
      s32 bottom = std::min(copied.dirty[3], h.y + s32(copied.rows) - 1);
      copied.dirty[0] = 0, copied.dirty[1] = 0, copied.dirty[2] = -1, copied.dirty[3] = -1;
      if(left > right || top > bottom) return copied.texture;
      Command c{Command::Kind::Copy, h.target->id};
      c.x = left, c.y = top, c.texture = copied.texture;
      c.width = right - left + 1, c.height = bottom - top + 1, c.intoX = left - h.x, c.intoY = top - h.y;
      recorded.commands.push_back(c);
      statistics.copies++, statistics.copied += c.width * c.height;
      return copied.texture;
    }
    u32 width = h.width, rows = h.rows, id = 0;
    if(found != copies.end()) {  //(too small: let go, and made again larger; or two targets': copied whole again)
      if(h.next && found->second.width == width && found->second.rows == rows) {
        id = found->second.texture;
      } else {
        if(!h.next) {
          width = std::min(std::bit_ceil(std::max(width, found->second.width)),
                           std::max(width, std::min<u32>(512, h.target->stride - h.x)));
          rows = std::min(std::bit_ceil(std::max(rows, found->second.rows)), std::max(rows, h.target->rows - h.y));
        }
        backend->dropTexture(found->second.texture);
      }
      copies.erase(found);
    } else if(copies.size() >= MostCopies) {
      for(auto copy = copies.begin(); copy != copies.end(); copy++) {
        auto& [where, copied] = *copy;
        if(std::get<0>(where) != h.target->id || copied.width != width || copied.rows != rows) continue;
        id = copied.texture, copies.erase(copy);
        break;
      }
    }
    if(!id && copies.size() >= MostCopies) {  //(the one unused longest let go: release())
      auto oldest = copies.begin();
      for(auto copy = copies.begin(); copy != copies.end(); copy++) {
        if(copy->second.used < oldest->second.used) oldest = copy;
      }
      backend->dropTexture(oldest->second.texture), copies.erase(oldest);
    }
    if(!id) id = backend->makeTexture(width * scale, rows * scale, nullptr);
    if(!id) return 0;
    if(width != h.width || rows != h.rows) taken = Vertex::taken(h.width, h.rows);
    auto& copied = copies[key] = {id, width, rows, uses};
    Command c{Command::Kind::Copy, h.target->id};
    c.x = h.x, c.y = h.y, c.texture = id;
    c.width = std::min<u32>(width, h.target->stride - h.x), c.height = h.next ? h.split : rows;
    recorded.commands.push_back(c);
    statistics.copies++, statistics.copied += c.width * c.height;
    if(h.next) {  //(the rest from the frame buffer below, whose changes this copy doesn't follow: taken whole again)
      c.target = h.next->id, c.y = 0, c.height = h.rows - h.split, c.intoY = h.split;
      recorded.commands.push_back(c);
      statistics.copies++, statistics.copied += c.width * c.height;
      auto& dirty = copied.dirty;
      dirty[0] = h.x, dirty[1] = h.y, dirty[2] = h.x + h.width - 1, dirty[3] = h.y + h.rows - 1;
    }
    return id;
  }
  auto* decoded = look.decoded.get();
  if(!look.textured || !decoded) return 0;
  if(decoded == lastDecoded && !lastTexture.decoded.expired()) return lastTexture.id;
  auto found = textures.find(decoded);
  if(found != textures.end()) {
    if(!found->second.decoded.expired() && found->second.rows >= decoded->rows) {
      lastDecoded = decoded, lastTexture = found->second;
      return found->second.id;
    }
    backend->dropTexture(found->second.id);
    textures.erase(found);
  }
  u32 id = decoded->part
    ? backend->makeTexturePart(decoded->key.width, decoded->rows, decoded->texels.data(), decoded->left, decoded->top,
                               decoded->right - decoded->left, decoded->rows - decoded->top, look.decoded)
    : backend->makeTexture(decoded->key.width, decoded->rows, decoded->texels.data(), look.decoded);
  if(!id) return 0;
  textures[decoded] = {look.decoded, id, decoded->rows};
  lastDecoded = decoded, lastTexture = textures[decoded];
  statistics.textures++;
  return id;
}

//A PRIM whose settings, texture and frame buffer are the last one's takes its state as it is (begin()), its draws
//joining the last's without comparing them again where it's still the last recorded: Super Monkey Ball draws 50,000
//PRIMs a frame, a world matrix between each two and its settings changed for one in fifty. Not in clear mode, where
//each primitive sets the stencil (clearStencil()), nor for a copy of a target, which textureFor() takes again for
//each. (Every input compared where it is, the pixel pipeline's bytes all of them, as they're laid out, with a branch
//at the end alone: a copy of them, a call to compare it and a branch a field cost the RP6's Cortex-X3 more.)
alwaysinline auto GPU::kept(const GE::Look& look) const -> bool {
  auto& p = look.pixel;
  auto& t = look.texture;
  auto& c = ge->commands;
  auto& k = keptSettings;
  static_assert(sizeof(GE::PixelState) % 16 == 0);
  GE::u64x2 bytes{};
  for(u32 n = 0; n < sizeof(GE::PixelState); n += 16) {
    GE::u64x2 now, then;
    std::memcpy(&now, reinterpret_cast<const u8*>(&p) + n, 16);
    std::memcpy(&then, reinterpret_cast<const u8*>(&k.pixel) + n, 16);
    bytes |= now ^ then;
  }
  u32 words = (t.width ^ k.width) | (t.height ^ k.height) | (look.function ^ k.function) |
              (look.environment ^ k.environment) | (c[GE::MaskColor] ^ k.maskColor) |
              (c[GE::MaskAlpha] ^ k.maskAlpha) | (target->id ^ k.target) | (target->stride ^ k.stride) |
              (target->height ^ k.rows) | (column ^ k.column) | (row ^ k.row) | (shade ^ k.shade) |
              (filter ^ k.filter);
  u32 flags = u32(look.textured != k.textured) | u32(t.clampU != k.clampU) | u32(t.clampV != k.clampV) |
              u32(look.withAlpha != k.withAlpha) | u32(look.doubled != k.doubled) | u32(backend->reads != k.reads) |
              u32(backend->readsBlending != k.readsBlending) | u32(backend->dualSource != k.dualSource) |
              u32(backend->logicOps != k.logicOps);
  if((bytes[0] | bytes[1] | words | flags) || !k.valid || p.clear || held) return false;
  return !look.textured || (look.decoded.get() == lastDecoded && !lastTexture.decoded.expired());
}

//The GE's settings as the GPU's: a pipeline, its constants, and the dynamic state (the design's "Depth, stencil,
//blending and logic operations" says what each approximates).
auto GPU::settings(const GE::Look& look) -> void {
  auto& p = look.pixel;
  auto& c = ge->commands;
  auto& t = look.texture;
  keptSettings = {p, t.width, t.height, look.function, look.environment, c[GE::MaskColor], c[GE::MaskAlpha],
                  target->id, target->stride, target->height, column, row, shade, filter, !p.clear && !held,
                  look.textured, t.clampU, t.clampV, look.withAlpha, look.doubled, backend->reads,
                  backend->readsBlending, backend->dualSource, backend->logicOps};
  textured = false;
  State& s = state;
  s = {};
  Pipeline& k = s.pipeline;
  takenFlags = 0;
  k.alphaTest = 8, k.colorTest = 4, k.quantize = 4, k.logic = 3, k.texels = 4;
  bool stencils = p.format != 0;  //(5650 has none)
  u32 maskColor = c[GE::MaskColor] & 0xff'ffff, maskAlpha = c[GE::MaskAlpha] & 0xff;
  u8 rgb = 0;
  for(u32 n = 0; n < 3; n++) rgb |= (maskColor >> n * 8 & 0xff) != 0xff ? 1 << n : 0;
  //(MASK_ALPHA keeping every bit of a 16-bit one's stencil leaves the color's alpha too)
  bool alphaWritable = stencils && keptStencil(~maskAlpha & 0xff, p.format);
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
  s.stencilWriteMask = keptStencil(~maskAlpha & 0xff, p.format);  //(a 16-bit one's bits and their copies together)
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
    textured = look.textured &&
               (s.texture = textureFor(look, k.texels, s.push.textureScale, takenFlags, k.indexed, s.push.palette));
    //(a texture the backend couldn't make leaves lastDecoded another's, which the next PRIM's mustn't be taken for)
    if(look.textured && !textured) keptSettings.valid = false;
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
      //A REPLACE writes the reference as a 16-bit frame buffer keeps it (keptStencil()), which the GPU's writes
      //as the reference it compares with: where the comparison takes the two apart (by the bits it takes), a 5551
      //one's comparison (its stencil 0 or 255) is made on the top bit instead, against the kept reference, which
      //comes out the same at either stencil; a 4444 one's stays the GE's, its REPLACE writing all eight bits.
      //A 5551 one's stencil is a bit, which an INCREMENT fills and a DECREMENT empties (pixel.cpp), where the GPU's
      //would step its 0 or 255 by one, the color's alpha left: those are a REPLACE of 255 and a ZERO, and a REPLACE
      //of 0 a ZERO, so one reference serves them all. Hammerin' Hero and Jeanne d'Arc mark their characters' pixels
      //so in a frame buffer of their own, then take them onto the screen by the alpha.
      u32 kept = keptStencil(p.stencilReference, p.format), replaced = kept;
      for(u8* o : {&k.stencilFail, &k.stencilDepthFail, &k.stencilPass}) {
        if(p.format == 1 && (*o == Decrement || (*o == Replace && !kept))) *o = Clear;
        if(p.format == 1 && *o == Increment) *o = Replace, replaced = 0xff;
      }
      bool replaces = k.stencilFail == Replace || k.stencilDepthFail == Replace || k.stencilPass == Replace;
      if(replaces && replaced != p.stencilReference) {
        u32 reference = p.stencilReference & p.stencilMask;
        if(p.stencilFunction < 2 || (replaced & p.stencilMask) == reference) {  //(never, always: no reference)
          s.stencilReference = replaced;
        } else if(p.format == 1) {
          bool zero = comparesTrue(p.stencilFunction, reference, 0);
          bool full = comparesTrue(p.stencilFunction, reference, 0xff & p.stencilMask);
          s.stencilReference = replaced, s.stencilCompareMask = 0x80;
          k.stencilCompare = zero == full ? zero : zero == !replaced ? 2 : 3;  //(always or never; equal, not equal)
        }
      }
      //The alpha the frame buffer keeps is the stencil; where the pass operation's result is known here, the
      //color's alpha follows it (blending by the destination's alpha reads it), else it's left (the stencil
      //itself is read back from the GPU's stencil, finish()).
      if(alphaWritable && (k.stencilPass == Replace || k.stencilPass == Clear)) {
        k.alphaOut = 1, k.colorMask |= 8;
        s.push.stencil = k.stencilPass == Replace ? replaced : 0;
      }
    } else if(p.stencilTest && !comparesTrue(p.stencilFunction, p.stencilReference & p.stencilMask, 0)) {
      //A 5650 frame buffer's stencil reads as 0: a test that fails against it drops every pixel (and keeps the
      //stencil it doesn't have); one that passes is no test
      k.stencilTest = 1, k.stencilCompare = 0;
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
      if(backend->logicOps) {  //(the GPU's takes the alpha too, the GE's leaves the stencil: its alpha left behind)
        k.logicOp = 1, k.logicOperation = p.logic, k.blend = 0;
        k.alphaOut = 0, k.colorMask &= 7;
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
    //whole numbers: draw.frag's TERM, which takes the dither too, as the PSP dithers what it blended; not where output
    //0's color is the destination's factor too, without dual-source blending)
    bool weighs = k.destinationFactor == SourceColor || k.destinationFactor == OneMinusSourceColor;
    if(k.blend && k.sourceFactor == One && k.operation <= ReverseSubtract && !weighs) k.term = k.operation + 1;
    k.dither = p.dither && !k.logicOp && (!k.blend || k.term);
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
  s32 left = std::max(p.left, 0) + column, top = std::max(p.top, 0) + row;
  s32 right = std::min<s32>(p.right, target->stride - 1 - column) + column;
  s32 bottom = std::min<s32>(p.bottom, target->height - 1 - row) + row;
  s.push.origin = row;
  s.scissor[0] = left, s.scissor[1] = top, s.scissor[2] = right - left + 1, s.scissor[3] = bottom - top + 1;
  //(the stencil, the PSP's alpha, changed where the colors' alpha isn't, which then differs from it: by a stencil or
  //depth test failing, or a pass whose alpha isn't written, or is written whole where the write mask keeps bits.
  //Target::lags. A PRIM whose settings are kept, above, keeps it as it is: it's theirs alone.)
  lagging = k.stencilTest && s.stencilWriteMask &&
            ((k.stencilFail != Keep && k.stencilCompare != 1) ||
             (k.stencilDepthFail != Keep && k.depthTest && k.depthCompare != 1) ||
             (k.stencilPass != Keep && (!(k.colorMask & 8) || s.stencilWriteMask != 0xff)));
  auto byAlpha = [](u32 factor) { return factor == 4 || factor == 5 || factor == 8 || factor == 9; };
  blendsByAlpha = !p.clear && p.blend && p.blendOperation < 3 &&
                  (byAlpha(p.blendSource) || byAlpha(p.blendDestination));
  stateKept = false, meshKept = false, screenable = true, drawableKnown = false;
}

//A PRIM's settings: its target found (filled from memory first if need be), its pages owned, its texture on the
//GPU, its pipeline and the rest worked out, for its primitives to draw with. False where it can't be drawn here (the
//GPU lost, no target for its frame buffer, rows past the target's (past VRAM's end, where the PSP's addresses run
//round to its start, or past 512), or a texture the GE reads from memory as it draws, which the GPU can't have),
//for the software renderer to draw; true for one that draws nothing too.
auto GPU::begin(GE& ge, const GE::Look& look, bool through, const GE::Region& region,
                const std::vector<GE::Vertex>& vertices, const GE::Transform& transform) -> bool {
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
  u32 column = 0, row = 0;
  Target* t = targetFor(p, region, column, row);
  if(!t || region.bottom + s32(row) >= s32(t->height)) return held.reset(), false;
  s32 bottom = region.bottom + row;
  s32 left = std::max(region.left, 0) + column, top = std::max(region.top, 0) + row;
  s32 right = std::min<s32>(region.right, t->stride - 1 - column) + column;
  if(left > right || top > bottom) return held.reset(), true;
  //Bytes memory may have changed in its pages while the GPU drew in them, or since in pages it hasn't drawn in
  //(beside): filled again from memory before this PRIM draws over them.
  s32 uLeft = left, uTop = top, uRight = right, uBottom = bottom;
  if(t->drawn()) {
    uLeft = std::min(uLeft, t->left), uTop = std::min(uTop, t->top);
    uRight = std::max(uRight, t->right), uBottom = std::max(uBottom, t->bottom);
  }
  if(!t->stale && t->besideReaches(uLeft, uTop, uRight, uBottom)) revive(*t, t->beside);  //(a stale one's filled anew)
  //Another target drawn on the GPU in the pages this PRIM draws in, or that fill this one from memory: its pixels
  //there moved into this one on the GPU where they're the same bytes in its rows (move(): God of War's effects draw
  //the same memory as 8888 and 5650 frame buffers in turn, Colin McRae and Dirt 2 as frame buffers of two widths),
  //else put in memory first (which may leave this one stale: below).
  u32 rowBytes = t->stride * t->bytes();
  u32 from = t->stale ? 0 : std::min<u32>(t->rows, top), to = bottom + 1;
  u32 first = (t->address + from * rowBytes) / Memory::PageSize;
  u32 last = std::min<u32>((t->address + to * rowBytes - 1) / Memory::PageSize, GE::VRAMPages - 1);
  moving.clear();
  //(pages found with no other target's pixels, and owners[] the same since, as drawnSerial says: a 3D game's PRIMs
  //each reach their target's whole scissor rectangle, 136 pages of a screen, one after another)
  bool alone = t->alone.serial == drawnSerial && first >= t->alone.first && last <= t->alone.last;
  for(u32 page = first; page <= last && !alone; page++) {
    Target* owner = owners[page];
    if(!owner || owner == t) continue;
    if(!moving.empty() && moving.back().from == owner && moving.back().last + 1 == page) moving.back().last = page;
    else moving.push_back({owner, page, page});
  }
  if(!alone && moving.empty()) t->alone = {first, last, drawnSerial};
  for(auto& m : moving) {
    s32 box[4];
    if(m.from->besideFull) revive(*m.from, m.from->beside);
    if(movable(*m.from, *t, m.first, m.last, box)) continue;
    finish(ge), moving.clear();
    break;
  }
  if(t->stale && t->drawn()) finish(ge), moving.clear();  //(what the GPU drew, then memory's changes over it)
  //Depth filled from memory below (its own new rows and those memory changed, and those memory changed of the target
  //it takes depth from), in pages another target has drawn in on the GPU since the last finish: as a finish then the
  //fill would leave it, the other's pixels moved into it on the GPU (moveIntoDepth()); else, where the target filled
  //drew in those pages itself (a finish would put both in memory), put in memory first. Where the GPU can't move
  //pixels into a depth (Backend::depthMoves), put in memory first only where this PRIM's depth test reads what's
  //filled: not a clear's, nor a test always or never true, nor one its depths decide (nothing's greater than 65535,
  //nor less than 0: Chili Con Carnage and Dirt 2 test 2D sprites at 65535 "greater or equal" over what they drew into
  //their depth buffer's memory), in the rows its pixels can be in (a 3D one's vertices on the screen, worked out only
  //where those rows are asked about: GTA Chinatown Wars' radar is tested below the rows its bloom drew over). The rest
  //is filled from memory as it is, as before the moves: a PRIM after that finds those rows filled reads them so (the
  //stale depth master has), where finishing for each would have the dozen games that draw their post-processing over
  //their depth buffer wait for the GPU once to three times a frame.
  bool depth = !column && !row && (p.clear ? p.clearDepth : p.depthTest);
  Target* taken = nullptr;
  if(depth) {
    bool same = t->depthBuffer == p.depthBuffer && t->depthStride == p.depthStride;
    u32 rows = same ? t->depthRows : 0, changedFrom = same ? t->depthChangedFrom : 0;
    u32 changedTo = same ? t->depthChangedTo : 0;
    taken = newestDepth(*t, p.depthBuffer, p.depthStride, same ? t->depthAt : 0);
    auto filled = [&](const auto& each) {  //(each(target filled, from, to) of the rows the fills below take)
      if(taken) {  //(as takeDepth() fills them: the other's changes, and where it's narrower, this one's rows to its)
        each(*taken, taken->depthChangedFrom, std::min(taken->depthChangedTo, taken->depthRows));
        bool narrower = Memory::depthCopy(taken->format) == Memory::depthCopy(t->format) &&
                        taken->stride < std::min(t->stride, p.depthStride);
        if(narrower) each(*t, rows, std::min(taken->depthRows, t->height));
      }
      each(*t, changedFrom, std::min(changedTo, rows));
      each(*t, rows, u32(bottom + 1));
    };
    if(backend->depthMoves) {
      bool onGPU = true;
      filled([&](const Target& into, u32 from, u32 to) {
        onGPU = onGPU && depthMovable(into, p.depthBuffer, p.depthStride, from, to);
      });
      if(!onGPU) statistics.depthFinishes++, finish(ge), moving.clear();
    } else {
      auto owned = [&](s32 first, s32 last) {  //(another target's pages in the rows filled from first to last)
        bool found = false;
        filled([&](const Target&, u32 from, u32 to) {
          from = std::max<u32>(from, first), to = std::min<u32>(to, last + 1);
          found = found || !depthMovable(*t, p.depthBuffer, p.depthStride, from, to);
        });
        return found;
      };
      if(!p.clear && p.depthFunction > 1 && owned(top, bottom)) {
        GE::Reached r = ge.reachedBy(through, vertices, transform, region);
        u32 f = p.depthFunction;
        bool decided = (f == 4 || f == 7) ? r.lowest == 65535 : (f == 5 || f == 6) && r.highest == 0;
        s32 first = std::max(top, r.region.top), last = std::min(bottom, r.region.bottom);
        if(!decided && r.region.left <= r.region.right && first <= last && owned(first, last)) {
          statistics.depthFinishes++, finish(ge), moving.clear();
        }
      }
    }
  }
  if(!ready()) return held.reset(), false;
  //(the colors filled afresh where memory's changed, the depth where memory's depth has: rows of it, or all of it
  //for another depth buffer; neither replaces the other's newer pixels on the GPU. A frame buffer that's part of
  //another's target, which doesn't reach its depth buffer, leaves the target's depth as it is: God of War's four
  //side by side don't fill it afresh each in turn. So does a PRIM that neither tests nor writes depth, its changes
  //kept for the next that does: Darkstalkers' CPU writes beside its depth buffer between its 2D PRIMs.)
  if(t->stale) {
    t->rows = 0, t->stale = false, t->beside.clear(), t->dead.clear();
    t->lags[0] = 0, t->lags[1] = 0, t->lags[2] = -1, t->lags[3] = -1;  //(colors and stencil both memory's again)
  }
  if(depth && (t->depthBuffer != p.depthBuffer || t->depthStride != p.depthStride)) {
    t->depthBuffer = p.depthBuffer, t->depthStride = p.depthStride, t->depthRows = 0;
    t->depthChangedFrom = t->depthChangedTo = 0;
    t->depthAt = 0, t->depthKept = false;
  }
  if(depth) takeDepth(*t, taken, p);  //(the newest drawn in its depth buffer, where another target drew it)
  if(depth && t->depthChangedFrom < t->depthChangedTo) {
    fill(*t, t->depthChangedFrom, std::min(t->depthChangedTo, t->depthRows), p, 2);
    t->depthChangedFrom = t->depthChangedTo = 0;
  }
  u32 filled = bottom + 1;  //(the rows moved into too, filled first)
  for(auto& m : moving) {
    s32 box[4];
    movable(*m.from, *t, m.first, m.last, box);
    if(box[1] <= box[3]) filled = std::max<u32>(filled, intoRows(*m.from, *t, box).second + 1);
    if(m.from->deadReaches(box[0], box[1], box[2], box[3])) revive(*m.from, m.from->dead);  //(moved as memory has)
  }
  if(filled > t->rows) fill(*t, t->rows, filled, p, 1), t->rows = filled;
  if(t->deadReaches(left, top, right, bottom)) revive(*t, t->dead);  //(where it draws, as memory has it)
  if(depth && u32(bottom + 1) > t->depthRows) fill(*t, t->depthRows, bottom + 1, p, 2), t->depthRows = bottom + 1;
  if(depth && (p.clear || p.depthWrite)) t->depthAt = ++depthSerial, t->depthKept = false;
  for(auto& m : moving) move(*m.from, *t, m);
  //(bytes beside its pixels the moves and this PRIM together take into its drawn rectangle, which neither did alone:
  //memory's, filled now, where they're no moved pixels')
  if(!moving.empty() && t->besideReaches(std::min(uLeft, t->left), std::min(uTop, t->top),
                                         std::max(uRight, t->right), std::max(uBottom, t->bottom))) {
    revive(*t, t->beside);
  }
  target = t, this->column = column, this->row = row;
  this->through = through;
  filter = ge.commands[GE::TextureFilter];
  shade = ge.commands[GE::ShadeMode];
  if(!kept(look)) settings(look);
  if(!state.pipeline.clear && state.pipeline.reads) statistics.readingDraws++;  //(each PRIM, its settings kept or not)
  //(where it blends by the frame buffer's alpha, the colors' alpha caught up with the stencil first; then where its
  //own stencil operations leave it behind: each PRIM, its settings kept or not, before any of its primitives is
  //recorded, a draw of its own or among fast mode's meshes)
  if(blendsByAlpha) catchAlpha(*t, left, top, right, bottom);
  if(lagging) widen(t->lags, left, top, right, bottom);
  own(*t, left, top, right, bottom);
  reach = {t, {state.scissor[0], state.scissor[1], state.scissor[2], state.scissor[3]}};
  drawing = true;
  return true;
}

//Vertices for the GPU, in the PRIM's draw: one with the last if its settings are the same.
auto GPU::emit(const Vertex* vertices, u32 count) -> void {
  if(recorded.vertices.size() + count > MostVertices) flush();
  //(a corner's depth past the GE's where the depth decides: another pipeline, Pipeline::outside)
  bool outside = false;
  if(state.pipeline.depths()) {
    for(u32 n = 0; n < count; n++) outside |= !(vertices[n].z >= 0 && vertices[n].z <= 65535);
  }
  if(outside != bool(state.pipeline.outside)) state.pipeline.outside = outside, stateKept = false;
  if(!stateKept || recorded.states.empty()) {
    if(recorded.states.empty() || !(recorded.states.back() == state)) recorded.states.push_back(state);
    stateKept = true, meshKept = false;
  }
  u32 index = recorded.states.size() - 1, first = recorded.vertices.size();
  auto& commands = recorded.commands;
  bool joined = !commands.empty() && commands.back().kind == Command::Kind::Draw && commands.back().state == index &&
                commands.back().first + commands.back().count == first;
  auto box = reachOver(vertices, count);
  //A primitive that reads the frame buffer, where the GPU doesn't keep the primitives' order (Backend::readsInOrder),
  //sees what was drawn before it in the pass only past a barrier (the backend's: Command::barrier). One goes before
  //it where it overlaps a primitive drawn since the last (beginning another draw), none where it overlaps nothing
  //drawn since. (Boxes in the target's pixels: a pixel's middle inside both overlaps. Another target begins another
  //pass, after whose start the one before's are seen: the render pass's dependency. Fast mode's meshes, which enter
  //no box, read nothing: Backend::reads is off there.)
  bool barrier = false;
  if(backend->reads && !backend->readsInOrder) {
    if(boxesTarget != state.target) boxes.clear(), boxesTarget = state.target;
    if(state.pipeline.reads) {
      for(auto& b : boxes) barrier |= box[0] < b[2] && b[0] < box[2] && box[1] < b[3] && b[1] < box[3];
    }
    if(barrier) statistics.splits += joined, joined = false, boxes.clear();
    if(boxes.size() < MostBoxes) boxes.push_back(box);
    else {
      auto& b = boxes.back();
      b = {std::min(b[0], box[0]), std::min(b[1], box[1]), std::max(b[2], box[2]), std::max(b[3], box[3])};
    }
  }
  if(joined) {
    commands.back().count += count;
  } else {
    Command c{Command::Kind::Draw, state.target};
    c.state = index, c.first = first, c.count = count, c.barrier = barrier;
    commands.push_back(c);
    statistics.draws++, statistics.barriers += barrier;
  }
  recorded.vertices.insert(recorded.vertices.end(), vertices, vertices + count);
  statistics.primitives++;
}

auto GPU::reachOver(const Vertex* vertices, u32 count) -> std::array<float, 4> {
  std::array<float, 4> box{vertices[0].x, vertices[0].y, vertices[0].x, vertices[0].y};
  for(u32 n = 1; n < count; n++) {
    box[0] = std::min(box[0], vertices[n].x), box[1] = std::min(box[1], vertices[n].y);
    box[2] = std::max(box[2], vertices[n].x), box[3] = std::max(box[3], vertices[n].y);
  }
  reach.box[0] = std::min(reach.box[0], box[0]), reach.box[1] = std::min(reach.box[1], box[1]);
  reach.box[2] = std::max(reach.box[2], box[2]), reach.box[3] = std::max(reach.box[3], box[3]);
  return box;
}

//The fog at a vertex as 0-255 (same rule as the software path's fogAmount), then as the 0-1 the shader blends and
//turns back with × 256: so a corner's amount is what is interpolated, matching ramp-fog (docs/psp-core.md).
static auto fogAsAttribute(float fog) -> float {
  if(std::signbit(fog)) return 0;
  if(!(fog < 1)) return 255 / 256.0f;  //amount 255, not 1.0: × 256 in the shader must give 255, not 256
  return float(u32(fog * 256)) / 256.0f;
}

//A vertex for the GPU, at x and y, its place in sixteenths (targetFixed()).
auto GPU::vertex(const GE::Vertex& v, bool perspective, s32 x, s32 y) const -> Vertex {
  Vertex o{};
  o.x = x / 16.0f + column, o.y = y / 16.0f + row, o.z = v.z;
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
  triangleAt(a, b, c, {targetFixed(a.x), targetFixed(b.x), targetFixed(c.x)},
             {targetFixed(a.y), targetFixed(b.y), targetFixed(c.y)});
}

//(its corners' places in sixteenths, x and y, the GE's: targetFixed())
auto GPU::triangleAt(const GE::Vertex& a, const GE::Vertex& b, const GE::Vertex& c, const s64 (&x)[3],
                     const s64 (&y)[3]) -> void {
  if(!drawing) return;
  bool perspective = !through && a.clip[3] > 0 && b.clip[3] > 0 && c.clip[3] > 0;
  Vertex v[3] = {vertex(a, perspective, s32(x[0]), s32(y[0])), vertex(b, perspective, s32(x[1]), s32(y[1])),
                 vertex(c, perspective, s32(x[2]), s32(y[2]))};
  if(!(shade & 1)) {
    for(auto& corner : v) corner.color = c.color, corner.specular = c.specular;
  }
  clearStencil(c.color);
  if(textured) {
    f64 area = std::abs(f64(s32(x[1]) - s32(x[0])) * (s32(y[2]) - s32(y[0])) -
                        f64(s32(y[1]) - s32(y[0])) * (s32(x[2]) - s32(x[0])));
    float texels = std::abs((b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u));
    bool linear = targetFilter(filter, std::sqrt(texels / (area / 256.0f)));
    for(auto& corner : v) corner.flags = linear | takenFlags;
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
  if(fast && screenMeshes && screenable && !through && backend->transforms && !backend->reads &&
     !state.pipeline.clear && screenTriangle(v)) {
    return;
  }
  emit(v, 3);
}

//Fast mode's 3D triangles the GE transformed, lit and clipped itself (a PRIM of fewer than MeshLeast vertices, or a
//mesh's triangle cut at the near plane), drawn among the meshes: as transform.vert takes a vertex the GE put on the
//screen (Model::OnScreen), which is draw.vert's arithmetic on the same numbers, with the PRIM's state as its meshes
//take it (FLAT's colors are the last corner's, as triangleAt() made every corner's). So a PRIM's meshes and the small
//PRIMs between them are one draw where their settings are the same: Army of Two and Ghost of Sparta alternate between
//the two 2,000 times a frame.
auto GPU::screenTriangle(const Vertex (&v)[3]) -> bool {
  if(recorded.models.size() + 3 > MostVertices) flush();
  u32 index = meshState();
  if(index == ~0u) return screenable = false;
  u32 base = recorded.models.size();
  Model* models = recorded.models.append(3);
  for(u32 n = 0; n < 3; n++) {
    auto& m = models[n];
    m.x = v[n].x, m.y = v[n].y, m.z = v[n].z;
    m.normal[0] = v[n].w, m.normal[1] = v[n].q, m.normal[2] = v[n].fog;
    m.u = v[n].u, m.v = v[n].v, m.color = v[n].color;
    std::memcpy(&m.weights[0], &v[n].specular, 4), std::memcpy(&m.weights[1], &v[n].flags, 4);
    std::fill_n(m.weights + 2, 6, 0.0f);
    m.transform = Model::OnScreen;
  }
  reachOver(v, 3);
  u32 first = recorded.indices.size();
  u32* indices = recorded.indices.append(3);
  for(u32 n = 0; n < 3; n++) indices[n] = base + n;
  meshDraw(index, first, 3);
  statistics.primitives++, statistics.screened++;
  return true;
}

//A sprite: two triangles over exactly the pixels the GE's rectangle() covers (its job's), with its depth, color and
//fog (each half its own: draw.cpp), the texture turned a quarter for bottom-left and top-right corners. In 2D its
//texture coordinates are stepped in the shader as the GE steps them (Vertex's first, step and start); in 3D they
//follow the perspective as the GE's do: q (about 1 / w) and s (the coordinate across x over w) running straight
//across x, t (the one down y over w) straight down y (the GE's corners' values, spriteJob()), which is what the
//GPU's interpolation does with these corners' values, dividing by q with its own divide.
auto GPU::sprite(const GE::Job& job) -> void {
  if(!drawing || job.firstX > job.lastX || job.firstY > job.lastY) return;
  auto& s = job.sprite;
  Vertex base{};
  base.z = s.z, base.w = 1, base.q = 1, base.color = s.color, base.specular = s.specular;
  clearStencil(s.color);
  bool divided = textured && s.divided;
  if(textured) {
    base.flags = job.linear | (divided ? 0 : 2) | (s.turned ? 4 : 0) | takenFlags;
    base.columnFirst = s.columnFirst, base.columnStep = s.columnStep;
    base.columnStart = s.columnStart + s32(column) * 16;
    base.rowFirst = s.rowFirst, base.rowStep = s.rowStep, base.rowStart = s.rowStart + s32(row) * 16;
    //Above 1x a 2D sprite's pixels (and a 3D one's with a corner behind the camera, stepped as 2D's) are held between
    //the middles of its first and last (draw.frag), where the PSP's pixels sample, as a 2D triangle's are
    //(triangleAt()): at its edges the GPU's would sample past them, a filter's into the texel past its picture
    //(Burnout Legends' banner, repeating, its dark top row along its bottom). Not from a copy of a target at its
    //resolution, which has the texels between the PSP's: the GPU's pixels at a texel-for-texel sprite's edges sample
    //inside the texel the PSP's pixel takes. Its right and bottom edges go in u and v.
    if(!divided && backend->scale > 1 && state.push.textureScale == 1) {
      base.flags |= 8, base.u = s.right + s32(column) * 16, base.v = s.bottom + s32(row) * 16;
    }
  }
  //a corner at pixel edge (x, y): in 3D, its values where the GE's straight lines across and down reach it
  auto corner = [&](s32 x, s32 y) {
    Vertex v = base;
    v.x = x + s32(column), v.y = y + s32(row);
    if(divided) {
      f64 alongX = f64(x * 16 - s.left) / (s.right - s.left), alongY = f64(y * 16 - s.top) / (s.bottom - s.top);
      f64 q = (s.leftQ + (s.rightQ - s.leftQ) * alongX) * s.qUnit;
      f64 across = (s.leftS + (s.rightS - s.leftS) * alongX) * s.sUnit;
      f64 down = (s.topT + (s.bottomT - s.topT) * alongY) * s.tUnit;
      v.w = q > 0 ? 1 / q : 1;
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

//Clear mode's stencil, where it's written: the color's alpha (a triangle's last corner's, a sprite's, a point's, a
//line's pixel's), as the frame buffer keeps it (keptStencil()); the draws after a change of it are another draw.
auto GPU::clearStencil(u32 color) -> void {
  if(!state.pipeline.clear || !state.pipeline.stencilTest) return;
  u32 stencil = keptStencil(color >> 24, target->format);
  if(state.stencilReference != stencil) {
    state.stencilReference = stencil, stateKept = false, meshKept = false, keptSettings.valid = false;
  }
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
  clearStencil(at.color);
  Vertex base = vertex(at, false, targetFixed(at.x), targetFixed(at.y)), v[6];
  base.flags = textured ? targetFilter(filter, 1.0f) | takenFlags : 0u;
  squareOf(base, (targetFixed(at.x) >> 4) + s32(column), (targetFixed(at.y) >> 4) + s32(row), v);
  emit(v, 6);
}

//A line: the pixels the GE's rule lights (draw.cpp's line(), raster.cpp's linePixels()), each a square of its own
//values.
auto GPU::line(const GE::Job& job, const std::vector<GE::LinePixel>& pixels) -> void {
  if(!drawing) return;
  for(auto& pixel : pixels) {
    clearStencil(pixel.color);
    Vertex base{}, v[6];
    base.z = pixel.z, base.w = 1, base.u = pixel.u, base.v = pixel.v, base.q = 1;
    base.color = pixel.color, base.specular = pixel.specular, base.fog = pixel.fog >= 255 ? 1.0f : pixel.fog / 256.0f;
    base.flags = textured ? job.linear | takenFlags : 0u;
    squareOf(base, pixel.x + s32(column), pixel.y + s32(row), v);
    //An anti-aliased line's texel is the one at its pixel (x + 0.5, y + 0.5): above 1x each of the GPU's pixels in
    //the square takes its own, as a frame buffer's copy at that resolution has them
    if(job.line.smooth && textured && resolution() > 1) {
      for(auto& corner : v) corner.u = corner.x - f32(column), corner.v = corner.y - f32(row);
    }
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
  return meshDrawable();
}

//(whether the driver makes the PRIM's pipeline as fast mode's 3D takes it; found once for its settings while they're
//kept, as only settings() changes them)
auto GPU::meshDrawable() -> bool {
  if(drawableKnown) return true;
  Pipeline k = state.pipeline;
  k.transformed = 1;
  k.flat = !(shade & 1);
  k.outside = 0;  //(3D: the corners' depths held in 0-65535)
  auto& known = drawables[Pipeline::Hash{}(k) % drawables.size()];
  if(!(known && *known == k)) {
    if(!backend->drawable(k)) return false;
    known = k;
  }
  return drawableKnown = true;
}

//(the PRIM's state as fast mode's 3D draws take it: transform.vert's pipeline, draw.frag's flat shading, the filter
//chosen at each pixel; the last recorded while it's kept, else recorded now: recordMeshState(). Each of fast mode's
//triangles the GE put on the screen asks, a hundred thousand a frame in Super Monkey Ball: no call while it's kept.)
alwaysinline auto GPU::meshState() -> u32 {
  if(meshKept && !recorded.states.empty()) return recorded.states.size() - 1;
  return recordMeshState();
}

//(its pipeline asked for as the state is recorded: one at the back already was then)
auto GPU::recordMeshState() -> u32 {
  State s = state;
  s.pipeline.transformed = 1;
  s.pipeline.flat = !(shade & 1);
  s.pipeline.outside = 0;
  if(textured) s.push.filter = filter;
  if(recorded.states.empty() || !(recorded.states.back() == s)) {
    if(!meshDrawable()) return ~0u;
    recorded.states.push_back(s);
  }
  stateKept = false, meshKept = true;
  return recorded.states.size() - 1;
}

alwaysinline auto GPU::meshDraw(u32 index, u32 first, u32 count) -> void {
  auto& commands = recorded.commands;
  if(!commands.empty() && commands.back().kind == Command::Kind::Mesh && commands.back().state == index &&
     commands.back().first + commands.back().count == first) {
    commands.back().count += count;
  } else {
    newMeshDraw(index, first, count);
  }
}

auto GPU::newMeshDraw(u32 index, u32 first, u32 count) -> void {
  Command c{Command::Kind::Mesh, state.target};
  c.state = index, c.first = first, c.count = count;
  recorded.commands.push_back(c);
  statistics.draws++;
}

auto GPU::mesh(GE& ge, const GE::Transform& t, const std::vector<GE::Vertex>& vertices,
               const std::vector<u32>& corners, bool again) -> void {
  if(!drawing) return;
  this->ge = &ge;
  u32 count = vertices.size();
  //(the PRIM's vertices once, kept for its later runs: where they are in the recording until it's handed over,
  //whatever comes after them, a triangle cut at the near plane among the runs: screenTriangle())
  if(!again || recorded.generation != meshGeneration) {
    if(recorded.models.size() + count > MostVertices) flush();
    meshFirst = recorded.models.size(), meshGeneration = recorded.generation;
    u32 transform = transformed(t);
    Model* models = recorded.models.append(count);
    for(u32 n = 0; n < count; n++) {
      auto& v = vertices[n];
      auto& m = models[n];
      m.x = v.x, m.y = v.y, m.z = v.z;
      std::memcpy(m.normal, v.normal, sizeof(m.normal));
      m.u = v.u, m.v = v.v, m.color = v.color;
      std::memcpy(m.weights, v.weights, sizeof(m.weights));
      m.transform = transform;
    }
  }
  u32 first = recorded.indices.size();
  u32* indices = recorded.indices.append(corners.size());
  for(u32 n = 0; n < corners.size(); n++) indices[n] = meshFirst + corners[n];
  //(where on the screen the GPU puts them isn't known here: anywhere inside the scissor)
  changed(*target, state.scissor[0], state.scissor[1], state.scissor[0] + state.scissor[2] - 1,
          state.scissor[1] + state.scissor[3] - 1);
  u32 index = meshState();  //(meshes() found its pipeline drawable)
  if(index == ~0u) return;
  meshDraw(index, first, corners.size());
  statistics.primitives += corners.size() / 3;
  if(!again) statistics.meshes++;
}

//mesh()'s block for these settings (and the texels its PRIM takes), in recorded's transforms: the last one's again
//where they're the same.
auto GPU::transformed(const GE::Transform& t) -> u32 {
  auto& transforms = recorded.transforms;
  u32 taken = textured ? takenFlags : 0;
  if(!transforms.empty() && !std::memcmp(&t, &lastTransform, sizeof(t)) && column == lastColumn && row == lastRow &&
     taken == lastTaken) {
    return transforms.size() - 1;
  }
  lastTransform = t, lastColumn = column, lastRow = row, lastTaken = taken;
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
  b.offset[0] = t.offsetX / 16 - f32(column), b.offset[1] = t.offsetY / 16 - f32(row);
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
  b.material2[0] = t.materialSpecular, b.material2[1] = t.ambientLight, b.material2[2] = taken;
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
//back (its stencil as the pixels' alpha) and narrowed to its format, in the pages it owns: a page inside the
//rectangle it didn't draw in since the last finish (between two PRIMs far apart) wasn't busy, and memory's bytes there
//are as new as its pixels, or newer (the CPU's). Its pages are no longer the renderer's; a target over bytes
//another's pixels were just put into is filled from memory when next drawn into; and every target's bytes are watched
//again, to hear of whoever else changes them.
auto GPU::finish(GE& ge) -> void {
  this->ge = &ge;
  bool drawn = false;
  for(auto& t : targets) drawn |= t->drawn();
  if(recorded.empty() && !drawn) return ge.reportFinish();
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
    //(threaded, only the bytes that change, into the versions of the emulation thread's later writes where it has made
    //some: GE::writingVRAM())
    u64 low = t.address + (u64(t.top) * t.stride + t.left) * bytes;
    u64 high = t.address + (u64(t.bottom) * t.stride + t.right + 1) * bytes - 1;
    ge.writingVRAM(low, high, [&](const auto& put) {
      for(s32 y = t.top; y <= t.bottom; y++) {
        for(s32 x = t.left, next = 0; x <= t.right; x = next) {  //(the row a page at a time)
          u32 at = (t.address + (y * t.stride + x) * bytes) & (Memory::VRAMSize - 1);
          next = std::min<s32>(t.right + 1, x + (Memory::PageSize - at % Memory::PageSize + bytes - 1) / bytes);
          if(owners[at / Memory::PageSize] != &t) continue;
          u32 i = (y - t.top) * width + (x - t.left);
          for(s32 column = x; column < next; column++, i++, at += bytes) {
            if(!t.dead.empty() && t.covered(at, at + bytes - 1)) continue;  //(memory's)
            u32 pixel = narrowTarget((colors[i] & 0xff'ffff) | u32(stencil[i]) << 24, t.format);
            put(at, (const u8*)&pixel, bytes);
          }
        }
      }
    });
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
    memory.busyCleared++;
  }
  for(auto& owner : owners) owner = nullptr;
  for(auto& pages : depthSources) pages = 0;
  sourcesFirst = ~0u, sourcesLast = 0;
  drawnSerial++;
  for(auto& t : targets) {
    t->left = 0, t->top = 0, t->right = -1, t->bottom = -1;
    t->told.clear();
    if(!t->beside.empty() || !t->dead.empty()) t->stale = true;  //(memory's bytes beside or over what it drew)
    t->dead.clear();
    if(t->rows) memory.watch(Memory::VRAMBase + t->address, t->rows * t->stride * t->bytes());
    if(t->depthRows && t->depthStride) {
      memory.watch(Memory::VRAMBase + Memory::depthCopy(t->format) * Memory::VRAMSize + t->depthBuffer,
                   t->depthRows * t->depthStride * 2);
    }
  }
  release();
  ge.reportFinish();  //(threaded: every PRIM before this record's in memory, for the emulation thread's model)
}

//A watched page someone else changed: a target over it, filled from memory before it's next drawn into. (A page
//the renderer owns is its own drawing's: own() tells memory of it.)
auto GPU::written(GE& ge, u32 page) -> void {
  u32 base = Memory::VRAMBase / Memory::PageSize;
  if(page < base || page >= base + GE::VRAMPages) return;
  page -= base;
  depthChanged(page * Memory::PageSize & ~0x3fffu);
  if(owners[page]) return;
  //(a page of whole rows of it, which it hasn't drawn in since the last finish: those bytes taken from memory again
  //before they're used (beside); else all of it, filled afresh)
  u32 low = page * Memory::PageSize, high = low + Memory::PageSize;
  for(auto& t : targets) {
    if(!t->rows || t->address >= high || low >= t->end()) continue;
    u32 rowBytes = t->stride * t->bytes();
    if(Memory::PageSize % rowBytes || t->address % rowBytes) t->stale = true;
    else keepBeside(*t, std::max(low, t->address), std::min(high, t->end()) - 1);
  }
  (void)ge;
}

//A depth buffer's rows a change in VRAM's 16 KiB from block may be in (the copies the GE keeps it in rearrange each 16
//KiB: all of its 16 KiB's), to be filled again before the next draw; the rest of the GPU's depth is kept.
auto GPU::depthChanged(u32 block) -> void {
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
}

//A texture whose bytes are, some of them, in pages the GPU has drawn in: taken from there for the next PRIM where
//they're all one target's, the texture's format and row width its own (a picture the GE drew, read back as it is:
//render to texture), or palette indices of as many bits as its pixels, none of them its stencil's (CLUT16 of a
//16-bit frame buffer, CLUT32 of an 8888 one: Death Jr. II takes each of its picture's colors through a palette so),
//looked up on the GPU; else the GE decodes it from memory, which finishes drawing first.
auto GPU::holds(GE& ge, const GE::Sampler& texture, u32 rows, u32 columns,
                const std::function<auto () -> GE::Region>& takes) -> bool {
  this->ge = &ge;
  held.reset();
  if(!ready()) return false;
  u32 physical = texture.address & 0x1fff'ffff;
  //(through VRAM's second or fourth copy, the views the GE keeps its depth buffer in: the depth drawn there, the
  //GPU's put into memory first, which the GE decodes it from)
  if(physical - Memory::VRAMBase < Memory::VRAMWindow && ((physical - Memory::VRAMBase) / Memory::VRAMSize & 1) &&
     texture.format <= 7) {
    static constexpr u32 bits[8] = {16, 16, 16, 32, 4, 8, 16, 32};
    u32 offset = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    u64 tall = (std::min<u32>({rows, texture.height, 512}) + 7) & ~7u;  //(a swizzled one's in rows of eight)
    u64 end = offset + tall * texture.bufferWidth * bits[texture.format] / 8;
    if(end > offset) putDepth(offset, std::min<u64>(end, Memory::VRAMSize) - 1, true);
    return false;
  }
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMSize) return false;
  u32 offset = physical - Memory::VRAMBase, bytes = texture.format == 3 || texture.format == 7 ? 4 : 2;
  bool direct = !texture.swizzled && texture.format <= 3;
  bool indexed = !texture.swizzled && (texture.format == 6 || texture.format == 7) && backend->palettes;
  bool exact = rows != ~0u;  //(the rows a 2D PRIM's vertices reach; a 3D one's, and others', aren't known)
  std::optional<GE::Region> texels;
  std::function<auto () -> GE::Region> taking = [&] { if(!texels) texels = takes(); return *texels; };
  u32 width = std::min<u32>({columns, texture.width, 512});
  rows = std::min<u32>({rows, texture.height, 512});
  //(a texture declared past VRAM's end, its rows not known: its rows inside VRAM taken, those past it, where the
  //PSP's addresses run round to VRAM's start, held at the last of them, as holdable() allows)
  u64 rowBytes64 = u64(texture.bufferWidth) * bytes;
  if(!exact && rows && rowBytes64 && offset + u64(width) * bytes <= Memory::VRAMSize &&
     offset + (rows - 1) * rowBytes64 + u64(width) * bytes > Memory::VRAMSize) {
    u32 inside = (Memory::VRAMSize - offset - u64(width) * bytes) / rowBytes64 + 1;
    if(!holdable(inside, taking)) return false;
    rows = inside;
  }
  u64 end = offset + (u64(rows - 1) * texture.bufferWidth + width) * bytes;
  if(!rows || end > Memory::VRAMSize) return false;
  //(the pixels other targets drew in its pages moved first into the target of its format and width that a frame
  //buffer there would draw into (move()), where they can all be: God of War samples the four shadows it drew as
  //5650 over a page of the 8888 it drew before and a row the frame buffer below drew. But not where it can be taken
  //as it is, from one target or from two frame buffers one below the other, no row held at another's (take(), not
  //cutting), which leaves every page its target's: Burnout Legends' car select samples its frame buffer as a texture
  //of 1024 rows, whose rows past its picture are the frame buffer shown, and moved, that one's pages were the
  //texture's target's, and above 1x it was shown as memory's picture (shownTarget()). Nor pages the PRIM draws in,
  //from which its own drawing would move them again: Midnight Club 3's menu samples its frame buffer as a texture
  //512 rows tall, whose last rows are the frame buffer below, which the PRIM draws into, and moved, all of that one's
  //pixels went into the texture's target and back each frame. Those are taken in two parts first, below, and moved
  //only where they can't be.)
  Target* home = nullptr;
  moving.clear();
  u32 rowBytes = texture.bufferWidth * bytes;
  if(direct && backend->moves && texture.bufferWidth) {
    u32 address = offset - offset % (Memory::PageSize % rowBytes ? rowBytes : Memory::PageSize);
    for(auto& t : targets) {
      if(t->address == address && t->stride == texture.bufferWidth && t->format == texture.format) home = t.get();
    }
    for(u32 page = offset / Memory::PageSize; home && page <= (end - 1) / Memory::PageSize; page++) {
      Target* owner = owners[page];
      u32 low = std::max<u32>(offset, page * Memory::PageSize);
      u32 high = std::min<u64>(end - 1, (page + 1) * Memory::PageSize - 1);
      if(!owner || owner == home || !owner->drawnBytes(low, high)) continue;
      if(!moving.empty() && moving.back().from == owner && moving.back().last + 1 == page) moving.back().last = page;
      else moving.push_back({owner, page, page});
    }
    for(auto& m : moving) {
      s32 box[4];
      if(m.from->besideFull) revive(*m.from, m.from->beside);
      if(home->stale || !movable(*m.from, *home, m.first, m.last, box)) { moving.clear(); break; }
    }
  }
  bool drawnIn = false;  //(the PRIM's frame buffer's rows, in its scissor, reach pages moved)
  if(!moving.empty()) {
    auto p = ge.pixelState();
    u32 drawnBytes = p.stride * (p.format == 3 ? 4 : 2);
    u32 first = (p.frameBuffer + std::max(p.top, 0) * drawnBytes) / Memory::PageSize;
    u32 last = (p.frameBuffer + (std::max(p.bottom, p.top) + 1) * drawnBytes - 1) / Memory::PageSize;
    for(auto& m : moving) drawnIn |= first <= m.last && m.first <= last;
  }
  auto moveIn = [&] {
    u32 filled = home->rows;
    for(auto& m : moving) {
      s32 box[4];
      movable(*m.from, *home, m.first, m.last, box);
      if(box[1] <= box[3]) filled = std::max<u32>(filled, intoRows(*m.from, *home, box).second + 1);
    }
    if(filled > home->rows) fill(*home, home->rows, filled, {}, 1), home->rows = filled;
    for(auto& m : moving) {
      s32 box[4];
      movable(*m.from, *home, m.first, m.last, box);
      if(m.from->deadReaches(box[0], box[1], box[2], box[3])) revive(*m.from, m.from->dead);
      move(*m.from, *home, m);
    }
    moving.clear();
  };
  //(palette indices with their palette on the GPU, where it has room for it; cutting: rows past a frame buffer that
  //can't be a second part held at the last before it, where they aren't known exactly, as take() may)
  auto taken = [&](bool cutting) {
    if(!take(texture, offset, end, width, rows, direct || indexed, exact || !cutting, taking)) return false;
    auto& h = *held;  //(texels past those held are the last held's)
    auto r = taking();
    s32 right = h.width - 1, bottom = h.rows - 1;
    h.takes[0] = h.x + std::min(r.left, right), h.takes[1] = h.y + std::min(r.top, bottom);
    h.takes[2] = h.x + std::min(r.right, right), h.takes[3] = h.y + std::min(r.bottom, bottom);
    if(direct) return true;
    held->palette = paletteFor(texture);
    if(!held->palette) held.reset();
    return held.has_value();
  };
  if(!moving.empty() && !drawnIn) {
    if(taken(false)) return true;
    moveIn();
  }
  if(taken(true)) return true;
  if(moving.empty()) return false;
  moveIn();
  return taken(true);
}

//(a texture it doesn't hold, the GE decodes from memory, which finishes first where its rows reach pixels the GPU
//has drawn since. Where the PRIM's rows aren't known and the first of them are memory's, it gets those before the
//first such page alone, as take() cuts, where holdable() allows: Death Jr. II's bloom samples its picture, read back
//for the CPU, as a texture 512 rows tall, which runs on into the smaller frame buffer the bloom is drawing)
auto GPU::memoryRows(GE& ge, const GE::Sampler& texture, const std::function<auto () -> GE::Region>& takes)
  -> u32 {
  static constexpr u32 bits[8] = {16, 16, 16, 32, 4, 8, 16, 32};
  this->ge = &ge;
  u32 physical = texture.address & 0x1fff'ffff;
  if(!ready() || texture.swizzled || texture.format > 7 || !texture.bufferWidth) return ~0u;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMSize) return ~0u;
  u32 offset = physical - Memory::VRAMBase, rowBytes = texture.bufferWidth * bits[texture.format] / 8;
  u64 end = std::min<u64>(offset + u64(std::min<u32>(texture.height, 512)) * rowBytes, Memory::VRAMSize);
  for(u32 page = offset / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    Target* owner = owners[page];
    u32 low = std::max<u32>(offset, page * Memory::PageSize);
    u32 high = std::min<u64>(end - 1, (page + 1) * Memory::PageSize - 1);
    if(!owner || !owner->drawnBytes(low, high)) continue;
    u32 before = low > offset ? (low - offset) / rowBytes : 0;
    return before && holdable(before, takes) ? before : ~0u;
  }
  return ~0u;
}

//The GE's palette as the GPU has it, for a texture of palette indices (holds()): its 512 entries as 8888, a 16-bit
//palette's widened as texels are, an 8888 one's 256 and the same again (its indices are taken to 255); found among
//those kept by the GE's hash and its bytes, or made (the one unused longest let go past MostPalettes).
auto GPU::paletteFor(const GE::Sampler& texture) -> u32 {
  u32 format = texture.clutFormat & 3;
  if(lastPalette.id && lastPalette.version == ge->clutVersion && lastPalette.format == format) return lastPalette.id;
  Palette* found = nullptr;
  for(auto& p : palettes) {
    if(p.hash == ge->clutHash && p.format == format && !std::memcmp(p.bytes.data(), ge->clut, p.bytes.size())) {
      found = &p;
      break;
    }
  }
  if(!found) {
    if(palettes.size() >= MostPalettes) {
      auto oldest = palettes.end();  //(not the last found, used since without a search)
      for(auto p = palettes.begin(); p != palettes.end(); p++) {
        if(p->id != lastPalette.id && (oldest == palettes.end() || p->used < oldest->used)) oldest = p;
      }
      backend->dropPalette(oldest->id);
      palettes.erase(oldest);
    }
    const u8* clut = ge->clut;
    u32 entries[512];
    for(u32 n = 0; n < 512; n++) {
      u32 e = n & 0xff;
      entries[n] = format == 3 ? clut[e * 4] | clut[e * 4 + 1] << 8 | clut[e * 4 + 2] << 16 | u32(clut[e * 4 + 3]) << 24
                               : widenTexel(clut[n * 2] | clut[n * 2 + 1] << 8, format);
    }
    u32 id = backend->makePalette(entries);
    if(!id) return 0;
    statistics.palettes++;
    found = &palettes.emplace_back(Palette{id, ge->clutHash, format, 0, {}});
    std::memcpy(found->bytes.data(), clut, found->bytes.size());
  }
  found->used = ++paletteUses;
  lastPalette = {found->id, ge->clutVersion, format};
  return found->id;
}

//The texture held from the targets that drew its pages (holds()), where its pages are one target's, or a target's
//and then, from a page on, another's: next. (A page's owner that drew none of the texture's bytes in it, a page
//holding rows of both, hasn't the newest of them: God of War's shadows, 64 rows each, end in the page the frame
//buffer below them starts in.) Where the rows its PRIM takes aren't known exactly (exact: a 3D one's, a 2D one's whose
//coordinates may repeat round), and its rows run on into other frame buffers' that can't be taken as a second part,
//only those before the first of their pages are taken, those past held at the last of them, where holdable() allows:
//Death Jr. II's palette passes take its picture as a texture 512 rows tall, which runs on into its bloom's.
auto GPU::take(const GE::Sampler& texture, u32 offset, u64 end, u32 width, u32 rows, bool direct, bool exact,
               const std::function<auto () -> GE::Region>& takes) -> bool {
  u32 bytes = texture.format == 3 || texture.format == 7 ? 4 : 2;
  Target *t = nullptr, *next = nullptr, *last = nullptr;
  u32 firstOwned = 0;  //(next's first page)
  bool more = false;   //(another target's pages past next's, or the first's again)
  for(u32 page = offset / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    Target* owner = owners[page];
    u32 low = std::max<u32>(offset, page * Memory::PageSize);
    u32 high = std::min<u64>(end - 1, (page + 1) * Memory::PageSize - 1);
    if(!owner || owner == last || !owner->drawnBytes(low, high)) continue;
    if(!t) t = owner;
    else if(!next && owner != t) next = owner, firstOwned = page;
    else { more = true; break; }
    last = owner;
  }
  //(palette indices: as many bits as the target's pixels, none of them its stencil's, which the GPU's copy of its
  //colors doesn't hold)
  auto fits = [&](Target* t) {
    if(!t || t->stale || t->stride != texture.bufferWidth) return false;
    if(texture.format <= 3) return t->format == texture.format;
    u64 bits = u64(texture.clutMask) << texture.clutShift;
    if(texture.format == 7) return t->format == 3 && !(bits & 0xff00'0000);
    if(texture.format == 6) return t->format < 3 && !(bits & (t->format == 1 ? 0x8000 : t->format == 2 ? 0xf000 : 0));
    return false;
  };
  if(!fits(t) || !direct || offset < t->address) return false;
  //(a texture wider than the target's row, as a 3D PRIM's reach isn't known: only the columns inside it copied, those
  //past it, in memory the next rows' first, left as they are; the design's accuracy notes count it)
  u32 rowBytes = t->stride * bytes, y = (offset - t->address) / rowBytes;
  u32 x = (offset - t->address) % rowBytes / bytes;
  if(x >= t->stride) return false;
  auto cut = [&] {  //(the rows before next's first page alone, where they aren't known exactly)
    u32 before = (firstOwned * Memory::PageSize - offset) / rowBytes;
    if(exact || !next || !before || !holdable(before, takes)) return false;
    rows = std::min(rows, before), next = nullptr;
    return true;
  };
  if(more && !cut()) return false;
  //A texture running from one frame buffer into the next below it in memory, of the same row width and format,
  //which starts on a page and a row of the first: its rows from there on are the next one's, from its first
  //(Midnight Club 3's menu samples its frame buffer as a texture 512 rows tall: its filter's second texel for the last
  //row is the next frame buffer's first row, which the PRIM draws into, and decoded from memory it was a finish every
  //frame). Each part is copied from its own target; the first owns none of the next one's pages (whose newest pixels
  //would be its own then).
  u32 split = rows;
  if(next) {
    u32 below = next->address - t->address;
    bool apart = fits(next) && next->address > t->address && !(next->address % Memory::PageSize) && !(below % rowBytes);
    for(u32 page = next->address / Memory::PageSize; apart && page < firstOwned; page++) apart = !owners[page];
    if(apart) split = below / rowBytes - y, apart = split < rows && rows - split <= next->height;
    if(!apart && !cut()) return false;
    if(!apart) split = rows;
    else if(next->besideReaches(x, 0, x + width - 1, rows - split - 1)) revive(*next, next->beside);
  }
  if(y + split > t->height) return false;
  if(t->besideReaches(x, y, x + width - 1, y + split - 1)) revive(*t, t->beside);  //(memory's newer bytes in it)
  if(t->deadReaches(x, y, x + width - 1, y + split - 1)) revive(*t, t->dead);  //(a transfer's: memory's again)
  if(next && next->deadReaches(x, 0, x + width - 1, rows - split - 1)) revive(*next, next->dead);
  held = Held{t, s32(x), s32(y), width, rows, texture.format, next, split};
  return true;
}

//Whether the pixels drawn since the last finish (each target's rows top-bottom, columns left-right: what finish()
//reads back), in the pages each owns and but those a block transfer or the CPU has written since (dead), reach
//VRAM's bytes first to last. Bytes beside them, in the same pages (past the picture's right edge, where games keep
//lists), are memory's own until then: memory reaches them without waiting. (Asked of each page's owner alone: God of
//War reads its vertices from VRAM, a page or two each, every time the GE needs one.)
auto GPU::drawnOver(GE& ge, u32 first, u32 last) -> bool {
  (void)ge;
  for(u32 page = first / Memory::PageSize; page <= last / Memory::PageSize && page < GE::VRAMPages; page++) {
    Target* owner = owners[page];
    if(!owner || clean[page] == drawnSerial) continue;
    u32 low = page * Memory::PageSize, high = low + Memory::PageSize - 1;
    if(!owner->drawnBytes(low, high)) { clean[page] = drawnSerial; continue; }
    if(owner->drawnBytes(std::max(first, low), std::min(last, high))) return true;
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
    if(t->rows && t->address <= last && first < t->end() && !t->covered(first, last)) keepBeside(*t, first, last);
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
  if(besideFull) return true;
  for(auto& [first, last] : beside) {
    if(reaches(first, last, left, top, right, bottom)) return true;
  }
  return false;
}

auto GPU::Target::deadReaches(s32 left, s32 top, s32 right, s32 bottom) const -> bool {
  for(auto& [first, last] : dead) {
    if(reaches(first, last, left, top, right, bottom)) return true;
  }
  return false;
}

auto GPU::Target::covered(u32 first, u32 last) const -> bool {
  while(first <= last) {
    bool on = false;
    for(auto& [from, to] : dead) {
      if(from > first || first > to) continue;
      if(to >= last) return true;
      first = to + 1, on = true;
    }
    if(!on) return false;
  }
  return true;
}

auto GPU::Target::drawnBytes(u32 first, u32 last) const -> bool {
  if(!drawn() || !reaches(first, last, left, top, right, bottom)) return false;
  if(dead.empty()) return true;
  u32 rowBytes = stride * bytes();
  u32 from = std::max<u32>(top, first > address ? (first - address) / rowBytes : 0);
  u32 to = std::min<u32>(bottom, (last - address) / rowBytes);
  for(u32 y = from; y <= to; y++) {
    u32 rowLeft = address + y * rowBytes + left * bytes(), rowRight = rowLeft + (right - left + 1) * bytes() - 1;
    if(rowLeft > last || first > rowRight) continue;
    if(!covered(std::max(first, rowLeft), std::min(last, rowRight))) return true;
  }
  return false;
}

//(a list written a word at a time makes one range, each word next to the last)
auto GPU::Target::touch(u32 first, u32 last) -> bool {
  for(auto& [from, to] : beside) {
    if(first <= to + 1 && from <= last + 1) {
      from = std::min(from, first), to = std::max(to, last);
      return true;
    }
  }
  if(beside.size() >= 16) return false;
  beside.push_back({first, last});
  return true;
}

//Bytes first to last of the target memory has the newest of (Target::beside), kept; where there are too many ranges
//already, those are filled from memory first (memory has every change to them: the bytes are changed as they're
//heard of). Brave Story writes its display list beside its picture, at times in more than 16 pieces; taken together
//as one range, as they were, it reached the picture's pixels, and the frame buffer was put back and filled afresh.
//(not filled here: memory tells of the change while it goes through its pages, and a fill watches them again. At
//MostBeside the target is filled afresh instead.)
auto GPU::keepBeside(Target& t, u32 first, u32 last) -> void {
  if(t.touch(first, last)) return;
  if(t.beside.size() >= MostBeside) return void(t.stale = true);
  t.beside.push_back({first, last}), t.besideFull = true;
}

//Memory's VRAM replaced whole (or another machine's: GE::setRenderer()): what the GPU drew since the last finish put
//back first (memory's own serialize and power have done that already, as they finish drawing), then every target,
//its colors and its depth, filled afresh when next drawn into.
auto GPU::forget(GE& ge) -> void {
  finish(ge);
  lastPalette = {}, keptSettings.valid = false;  //(another GE's palette versions, and settings)
  for(auto& t : targets) {
    t->stale = true, t->depthRows = 0, t->depthChangedFrom = t->depthChangedTo = 0;
    t->depthAt = 0, t->depthKept = false;
  }
}

//The frame shown, from the GPU's target: a read back of the rectangle shown alone, after what's recorded so far.
//(The hosts take the frame as pixels in memory, so it's read back either way; presenting from the GPU spares the
//rest: every other target's read back, VRAM's pages given back, and the targets filled again from memory after.)
//The target the frame buffer at VRAM's offset address (stride pixels a row, in GE format) is shown from, width x
//height of it: none where the GPU doesn't hold the newest of every pixel shown (no such target, or rows it hasn't,
//or pages another target or the CPU has changed since), or, at 1x, hasn't drawn there since memory last had it all.
//Above 1x a target memory has had all of since (a finish, which another frame buffer's PRIM may ask for) is still
//the picture: memory has its pixels narrowed, and memory's changes since are heard (written()) and revived first.
auto GPU::shownTarget(u32 address, u32 stride, u32 format, u32 width, u32 height) -> Target* {
  Target* t = nullptr;
  for(auto& candidate : targets) {
    if(candidate->address == address && candidate->stride == stride && candidate->format == format) {
      t = candidate.get();
    }
  }
  if(!t || t->stale || t->rows < height || width > t->stride) return nullptr;
  if(t->besideReaches(0, 0, width - 1, height - 1)) revive(*t, t->beside);  //(memory's newer bytes in what's shown)
  if(t->deadReaches(0, 0, width - 1, height - 1)) revive(*t, t->dead);
  //(every page shown the target's or no one's, and, at 1x, one at least the target's: else memory has it, or another)
  u32 bytes = t->bytes(), end = address + ((height - 1) * stride + width) * bytes;
  if(end > Memory::VRAMSize) return nullptr;
  bool owned = false;
  for(u32 page = address / Memory::PageSize; page <= (end - 1) / Memory::PageSize; page++) {
    if(owners[page] && owners[page] != t) return nullptr;
    owned |= owners[page] == t;
  }
  return owned || backend->scale > 1 ? t : nullptr;
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

//(nothing new run: the runs before waited for, each one's shot kept, as Backend::finish() does with nothing recorded)
auto GPU::settle() -> void {
  if(!ready()) return;
  Recorded none;
  bool wasLost = backend->lost;
  if(!backend->finish(none) && !wasLost) lose();
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
  for(auto& pages : depthSources) pages = 0;
  sourcesFirst = ~0u, sourcesLast = 0;
  drawnSerial++;
  if(!backend) return;
  for(auto& t : targets) backend->dropTarget(t->id);
  for(auto& [decoded, texture] : textures) backend->dropTexture(texture.id);
  for(auto& [where, copied] : copies) backend->dropTexture(copied.texture);
  for(auto& p : palettes) backend->dropPalette(p.id);
  targets.clear(), textures.clear(), copies.clear(), palettes.clear();
  lastPalette = {}, lastDecoded = nullptr, lastTexture = {}, keptSettings.valid = false;
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
