#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <vector>

//What the GE thread may still change in VRAM, as the emulation thread keeps track of it (option "GE Thread":
//ge/thread.hpp, docs/psp-core.md): whether a read of VRAM's bytes now is clean (memory holds them as of now) or must
//wait for the GE thread to pass the point it's at.
//
//The GE thread changes memory four ways: the software renderer's batches (inside each PRIM's rectangle and depth), a
//hardware renderer's finish (finish(): only bytes a GPU PRIM rasterized since memory last had them, and that no store
//marked dead since), its depth put back (putDepth(): at a put point, a PRIM reading a texture through the depth views
//or taking depth across the two orders through memory), and nothing else. So a byte is clean unless
//  - a GPU PRIM's rectangle newer than both the emulation thread's last write to the byte (cx) and the last finish
//    the GE thread has reported reaches it;
//  - a software PRIM's rectangle newer than cx and than the last PRIM it has reported drawn reaches it;
//  - a put point it hasn't passed covers its 16 KiB, after a GPU PRIM's depth there newer than cx and than the last
//    put-back reported there;
//  - above 1x, it was rasterized since cx, and a target's hull newer than cx and the last finish covers it (a
//    finish's read back may take another of a pixel's samples), or a put point it hasn't passed covers its 16 KiB (a
//    pixel moved into a depth, put back with another sample than a finish took).
//A read that waited makes nothing clean: only the emulation thread's writes (cx) and what the GE thread reports.
//
//Places are the GE thread's records' serials (GeThread): a PRIM's is its record's; a write's (cx) the last record
//handed over before it, so a PRIM is newer than a write where its place is past the write's. A finish or put-back the
//GE thread makes as it does the record at place q is known as q - 1: it has every PRIM before that record, none after.
//cx is kept per 32-byte piece: the place of the last write over all of it, and of the last that wrote part of it with
//which of its bytes; a byte that one missed takes the older place, which only makes it dirtier.

namespace ares::PlayStationPortable {

struct GeModel {
  static constexpr u32 VRAMBytes = 2u << 20, Pieces = VRAMBytes / 32, Pages = VRAMBytes / 4096, Blocks = VRAMBytes / 0x4000;

  //A rectangle of VRAM's bytes: rows top to bottom, rowBytes apart from address, bytes left * bytes to
  //(right + 1) * bytes of each (offsets in VRAM, running on round its end as the GE's do).
  struct Rect {
    u32 address = 0, rowBytes = 0, bytes = 1;
    s32 left = 0, top = 0, right = -1, bottom = -1;
    auto operator==(const Rect&) const -> bool = default;
    auto empty() const -> bool { return left > right || top > bottom || !rowBytes || !bytes; }
    //(its first byte and how far past it its last is: all of VRAM where that's as far)
    auto low() const -> u64 { return (u64(address) + u64(top) * rowBytes + u64(left) * bytes) % VRAMBytes; }
    auto span() const -> u64 { return u64(bottom - top) * rowBytes + u64(right - left + 1) * bytes; }
    //Whether it reaches any of bytes first to last (inside VRAM, first <= last: a piece's, a few rows at most).
    auto reaches(u32 first, u32 last) const -> bool {
      if(empty()) return false;
      if(span() >= VRAMBytes && rowBytes <= u64(right - left + 1) * bytes) return true;
      //(as offsets past its address; its rows running on past VRAM's end come round to its start)
      u64 from = (u64(first) + VRAMBytes - address % VRAMBytes) % VRAMBytes;
      for(u64 base : {u64(0), u64(VRAMBytes)}) {
        u64 low = from + base, high = low + (last - first);
        u64 y0 = std::max<u64>(low / rowBytes, top), y1 = std::min<u64>(high / rowBytes, bottom);
        for(u64 y = y0; y <= y1 && y1 >= y0; y++) {
          u64 rowLow = y * rowBytes + u64(left) * bytes, rowHigh = y * rowBytes + u64(right + 1) * bytes - 1;
          if(rowLow <= high && low <= rowHigh) return true;
        }
      }
      return false;
    }
    auto reaches(u32 byte) const -> bool { return reaches(byte, byte); }
    //The pages it reaches, first to last (both inside VRAM; last before first: round its end).
    template<typename Each> auto pages(const Each& each) const -> void {
      if(empty()) return;
      u64 from = low(), size = span();
      if(size >= VRAMBytes) { for(u32 page = 0; page < Pages; page++) each(page); return; }
      for(u64 page = from / 4096; page <= (from + size - 1) / 4096; page++) each(u32(page % Pages));
    }
  };

  //Rectangles, each with the newest place of a PRIM it's the rectangle of: the last Kept distinct ones as they are
  //(found by the pages they reach), older ones painted into the 32-byte pieces they reach (the newest place of any).
  struct Rects {
    static constexpr u32 Kept = 32;
    struct Slot { Rect rect; u64 at = 0, used = 0; bool on = false; };
    std::vector<u64> painted = std::vector<u64>(Pieces, 0);
    std::array<Slot, Kept> slots{};
    std::array<u32, Pages> pageSlots{};  //(the slots reaching each page, a bit each)
    std::array<u64, Pages> pageNewest{};  //(the newest place of any rectangle reaching each page)
    u64 uses = 0;
    auto paint(const Slot& slot) -> void {
      u64 from = slot.rect.low(), size = std::min<u64>(slot.rect.span(), VRAMBytes);
      for(u64 offset = from; offset < from + size; offset += 32 - offset % 32) {
        u32 piece = offset % VRAMBytes / 32;
        if(painted[piece] < slot.at && slot.rect.reaches(u32(offset % VRAMBytes / 32 * 32),
                                                         u32(offset % VRAMBytes / 32 * 32 + 31))) {
          painted[piece] = slot.at;
        }
      }
    }
    auto add(const Rect& rect, u64 at) -> void {
      if(rect.empty()) return;
      uses++;
      rect.pages([&](u32 page) { pageNewest[page] = std::max(pageNewest[page], at); });
      for(auto& slot : slots) {
        if(slot.on && slot.rect == rect) { slot.at = at, slot.used = uses; return; }
      }
      u32 pick = 0;
      for(u32 n = 0; n < Kept; n++) {
        if(!slots[n].on) { pick = n; break; }
        if(slots[n].used < slots[pick].used) pick = n;
      }
      auto& slot = slots[pick];
      if(slot.on) {
        paint(slot);
        slot.rect.pages([&](u32 page) { pageSlots[page] &= ~(1u << pick); });
      }
      slot = {rect, at, uses, true};
      rect.pages([&](u32 page) { pageSlots[page] |= 1u << pick; });
    }
    //The newest place of a rectangle reaching byte (0: none).
    auto newest(u32 byte) const -> u64 {
      u64 at = painted[byte >> 5];
      for(u32 bits = pageSlots[byte >> 12]; bits; bits &= bits - 1) {
        auto& slot = slots[__builtin_ctz(bits)];
        if(slot.at > at && slot.rect.reaches(byte)) at = slot.at;
      }
      return at;
    }
    //Whether a rectangle newer than floor reaches any of the piece's bytes first to last.
    auto newerThan(u32 first, u32 last, u64 floor) const -> bool {
      if(pageNewest[first >> 12] <= floor) return false;
      if(painted[first >> 5] > floor) {
        //(painted pieces are those an older rectangle reached some byte of: taken as all of them)
        return true;
      }
      for(u32 bits = pageSlots[first >> 12]; bits; bits &= bits - 1) {
        auto& slot = slots[__builtin_ctz(bits)];
        if(slot.at > floor && slot.rect.reaches(first, last)) return true;
      }
      return false;
    }
    auto clear() -> void {
      std::fill(painted.begin(), painted.end(), 0);
      slots = {}, pageSlots = {}, pageNewest = {}, uses = 0;
    }
  };

  //cx: the emulation thread's writes.
  struct Piece { u64 whole = 0, part = 0; u32 mask = 0; };
  std::vector<Piece> written = std::vector<Piece>(Pieces);
  auto write(u32 first, u32 last, u64 at) -> void {
    for(u32 piece = first >> 5; piece <= last >> 5; piece++) {
      u32 low = std::max(first, piece << 5) & 31, high = std::min(last, (piece << 5) + 31) & 31;
      u32 bits = high - low == 31 ? ~0u : ((1u << (high - low + 1)) - 1) << low;
      auto& p = written[piece];
      if(bits == ~0u) p.whole = at, p.part = 0, p.mask = 0;
      else if(p.part == at) p.mask |= bits;
      else p.part = at, p.mask = bits;  //(an older part's bytes take the whole's older place)
      if(p.mask == ~0u) p.whole = p.part, p.part = 0, p.mask = 0;
    }
  }
  auto cx(u32 byte) const -> u64 {
    auto& p = written[byte >> 5];
    return p.mask >> (byte & 31) & 1 ? p.part : p.whole;
  }

  Rects gpu, software;
  //(the GE thread's reports, as last taken)
  u64 finishKnown = 0, drawnKnown = 0, passed = 0;
  std::array<u64, Blocks> putKnown{};
  //Depth: the newest depth-writing GPU PRIM over each 16 KiB; the put points the GE thread hasn't passed, each with
  //the 16 KiBs it may put back and the newest of those over each as it came.
  std::array<u64, Blocks> gpuDepth{};
  struct PutPoint { u64 at; std::bitset<Blocks> covers; std::array<u64, Blocks> before; };
  std::vector<PutPoint> putPoints;

  //The depth buffers PRIMs have drawn with (address, row width): the most rows any reached in one, and the class of
  //the frame buffer format its newest GPU depth writer drew (Memory::depthCopy(): ~0 for none).
  struct DepthBuffer { u32 address = 0, stride = 0, rows = 0, newestClass = ~0u; };
  std::vector<DepthBuffer> depthBuffers;
  auto depthBuffer(u32 address, u32 stride) -> DepthBuffer& {
    for(auto& d : depthBuffers) {
      if(d.address == address && d.stride == stride) return d;
    }
    return depthBuffers.emplace_back(DepthBuffer{address, stride});
  }
  //(each 16 KiB a depth buffer's rows reach, to rows: all of each, which the GE's depth views rearrange)
  template<typename Each> static auto depthBlocks(u32 address, u32 stride, u32 rows, const Each& each) -> void {
    if(!stride || !rows) return;
    u64 low = address & ~0x3fffu, high = (u64(address) + u64(rows) * stride * 2 - 1) | 0x3fff;
    for(u64 block = low; block <= high && block < low + VRAMBytes; block += 0x4000) each(u32(block % VRAMBytes >> 14));
  }

  //Above 1x: the targets (GPU::targetFor()'s address, row width and format), each with the hull of its GPU rectangles
  //since the last finish it knows of (its place the newest's).
  struct Target { u32 address = 0, stride = 0, format = 0; Rect hull; u64 hullAt = 0; };
  std::vector<Target> targets;
  bool scaled = false;
  bool unsure = false;  //(the renderer refused a PRIM taken as surely its own: from then on none is taken so)

  //Whether bytes first to last of VRAM (offsets, first <= last, inside VRAM) are dirty now.
  auto dirty(u32 first, u32 last) const -> bool {
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      u32 block = page >> 2;
      bool gpuMay = gpu.pageNewest[page] > finishKnown, softwareMay = software.pageNewest[page] > drawnKnown;
      bool depthMay = false, putMay = false;
      for(auto& point : putPoints) {
        if(point.at <= passed || !point.covers[block]) continue;
        putMay = true;
        depthMay = depthMay || point.before[block] > putKnown[block];
      }
      bool hullMay = false;
      if(scaled) {
        for(auto& t : targets) hullMay = hullMay || (t.hullAt > finishKnown && !t.hull.empty());
      }
      if(!gpuMay && !softwareMay && !depthMay && !(scaled && (putMay || hullMay))) continue;
      u32 from = std::max(first, page << 12), to = std::min(last, (page << 12) + 4095);
      for(u32 piece = from >> 5; piece <= to >> 5; piece++) {
        u32 low = std::max(from, piece << 5), high = std::min(to, (piece << 5) + 31);
        auto& p = written[piece];
        if(!p.mask) {
          if(dirtyBytes(low, high, p.whole, block, gpuMay, softwareMay, depthMay, putMay, hullMay)) return true;
          continue;
        }
        for(u32 byte = low; byte <= high; byte++) {
          if(dirtyBytes(byte, byte, cx(byte), block, gpuMay, softwareMay, depthMay, putMay, hullMay)) return true;
        }
      }
    }
    return false;
  }

  //(bytes first to last of one piece, all last written at cx)
  auto dirtyBytes(u32 first, u32 last, u64 cx, u32 block, bool gpuMay, bool softwareMay, bool depthMay, bool putMay,
                  bool hullMay) const -> bool {
    if(gpuMay && gpu.newerThan(first, last, std::max(cx, finishKnown))) return true;
    if(softwareMay && software.newerThan(first, last, std::max(cx, drawnKnown))) return true;
    if(depthMay) {
      for(auto& point : putPoints) {
        if(point.at > passed && point.covers[block] && point.before[block] > std::max(cx, putKnown[block])) return true;
      }
    }
    if(!scaled || !(putMay || hullMay)) return false;
    for(u32 byte = first; byte <= last; byte++) {
      if(gpu.newest(byte) <= cx) continue;  //(not rasterized since it was written)
      if(putMay) return true;
      for(auto& t : targets) {
        if(t.hullAt > std::max(cx, finishKnown) && t.hull.reaches(byte)) return true;
      }
    }
    return false;
  }

  //Whether a page may be busy in the GE thread's memory now (drawn on the GPU since the last finish reported, or by
  //software not yet reported drawn): an access to it needs its record, for today's checks there.
  auto busy(u32 page) const -> bool {
    return gpu.pageNewest[page] > finishKnown || software.pageNewest[page] > drawnKnown;
  }

  auto clear() -> void {
    gpu.clear(), software.clear();
    std::fill(written.begin(), written.end(), Piece{});
    finishKnown = drawnKnown = passed = 0;
    putKnown = {}, gpuDepth = {};
    putPoints.clear(), depthBuffers.clear(), targets.clear();
  }
};

}
