#pragma once

#include <cstring>
#include <functional>
#include <string>
#include <vector>

//The PSP's memory map: what each address reaches.
//
//The Allegrex has no TLB (the hardware that remaps addresses on other MIPS chips), so an address goes straight to
//memory, through one of four windows that differ only in their top three bits: user (0x0...), user uncached
//(0x4...), kernel (0x8...) and kernel uncached (0xa...). "Uncached" means the CPU's cache is bypassed, which
//matters on the real hardware for memory the GE or the Media Engine reads; an emulator has no cache to bypass.
//Under HLE the game runs in user mode but nothing stops it reaching the kernel windows, so all four reach the
//same physical memory: the address's low 29 bits.
//
//Physical memory:
//  0x0001'0000-0x0001'3fff  the scratchpad: 16 KiB of fast RAM inside the CPU chip
//  0x0400'0000-0x041f'ffff  VRAM: 2 MiB of video memory, which the GE draws into and the display shows, seen four
//                           times in a row up to 0x047f'ffff; the second and fourth copies rearrange it for the
//                           GE's depth buffer (below)
//  0x0800'0000-0x09ff'ffff  main RAM: 32 MiB on the PSP-1000 (64 MiB, to 0x0bff'ffff, on later models). The
//                           kernel owns the first 8 MiB; games get the rest, from 0x0880'0000
//  0x1c00'0000-0x1fff'ffff  hardware registers and the boot ROM: nothing there yet, as the HLE kernel answers for
//                           the hardware
//Everything else is empty: reading it gives 0, writing it does nothing, and unmapped() is told.
//
//The bytes are kept in the host's memory as the PSP sees them, little-endian, so a word is read by copying four
//bytes (Phobos's hosts, ARM64 and x86-64, are little-endian too).
//
//VRAM's copies (measured on a PSP, docs/psp-core.md, round 3's depth-layout files). The first and third copies show
//VRAM as it is. The other two rearrange it in 32-byte pieces: the second reaches the byte at its offset with bits 6
//and 13 flipped; the fourth first turns the offset's bits 5-9 round by one place (bit 9 down to bit 5, bits 5-8 up
//to 6-9), then flips the same two. The GE reaches its depth buffer as the fourth copy shows it, so for a depth buffer
//512 pixels wide (as games have it), pixel (x, y)'s depth is, in 16-bit steps from the buffer's start:
//  - through the fourth copy, at y * 512 + x: in order;
//  - through the second, at y * 512 + (x >> 4) * 32 + (x & 15): each 16 pixels of a row 32 apart (columns 256-511 go
//    in the gaps, by these rules; only columns 0-255 were measured);
//  - through the first and third, as the second but with the pieces swapped in pairs and the rows in eights:
//    (y ^ 8) * 512 + ((x >> 4) ^ 1) * 32 + (x & 15).
//That fits every value measured for a buffer 1.5 MiB (0x180000) into VRAM. Whether the rules go by the address in
//VRAM (as here) or from the buffer's start differs only for a buffer that doesn't start on 16 KiB.

namespace ares::PlayStationPortable {

struct Memory {
  enum : u32 {
    ScratchpadBase = 0x0001'0000, ScratchpadSize = 16_KiB,
    VRAMBase       = 0x0400'0000, VRAMSize       =  2_MiB, VRAMWindow = 8_MiB,  //VRAM's 2 MiB, four times over
    RAMBase        = 0x0800'0000,
    PageSize       = 4_KiB,  //the CPU's page table has one entry per 4 KiB of physical memory
  };

  std::vector<u8> scratchpad;
  std::vector<u8> vram;
  std::vector<u8> ram;

  //Called after every change to memory, the CPU's stores and everyone else's (the loader putting a module in place,
  //an HLE function filling a buffer), with the address and size written. Code can run from any of this memory, so
  //the CPU's owner sets it to drop what the recompiler compiled from there.
  std::function<auto (u32 address, u32 size) -> void> written;

  //Called for a read or write of an address with nothing behind it: under HLE that's a bug, or a piece of the
  //hardware not emulated yet, so it's worth reporting.
  std::function<auto (u32 address, bool store) -> void> unmapped;

  //Watched pages: memory someone must hear about the moment it's written. The GE keeps textures decoded (ge/
  //texture.cpp), and a decoded copy is only good until its bytes change, by whoever changes them: the CPU, an HLE
  //function, the GE drawing or copying. So the GE watches the pages its textures come from. One byte per 4 KiB
  //page, numbered by the address's low 29 bits as the CPU's page table numbers them (VRAM by where its bytes are:
  //its first copy's pages). changed() tells watchedWritten() of each watched page a change touches, and stops
  //watching it.
  //
  //The CPU's compiled stores skip write() and changed() (they go straight through the page table), so whoever
  //owns the CPU sets watching(), which watch() calls for each newly watched page: the owner then sends compiled
  //stores to that page through write() from then on, as it does for pages holding compiled code. Without
  //watching() nobody promises that, and the GE keeps nothing decoded (canWatch()).
  std::vector<u8> watched;
  u32 watchedPages = 0;  //how many are watched: none, and changed() needn't look
  std::function<auto (u32 page) -> void> watching;
  std::function<auto (u32 page) -> void> watchedWritten;

  //VRAM while the GE's workers still draw into it (ge/threads.cpp: a list's last primitives go on being drawn while
  //the CPU runs on): the pages they draw over (busyPages, of VRAM's 512) are theirs, and anyone else touching one
  //(pointer(), behind every read, write and copy) first waits for them to finish (finishDrawingOver(), the GE's), as
  //serialize() and power() wait for all of it (finishDrawing()). The CPU's compiled loads and stores reach VRAM
  //through its page table instead, so the GE only lets drawing go on that way when the CPU's owner has set
  //vramGuard(), which takes the busy pages out of the CPU's page tables (busy) and puts them back (not).
  bool vramBusy = false;
  u64 busyPages[VRAMSize / PageSize / 64] = {};
  std::function<auto () -> void> finishDrawing;
  //What pointer() waits for, touching VRAM's bytes first to last (offsets in VRAM) drawn over (vramDrawnOver()): only
  //the drawing that reaches them, and what's drawn before it (the GE's settleOver()). None: finishDrawing().
  std::function<auto (u32 first, u32 last) -> void> finishDrawingOver;
  //Set while a thread reads VRAM it knows is drawn already (the GE's own reads, of bytes it has seen drawn first; a
  //batch's deferred textures, decoded as it starts): pointer() neither waits for drawing for it nor looks at the busy
  //pages, which the emulation thread changes meanwhile.
  static thread_local bool knownDrawn;
  struct KnownDrawn {  //knownDrawn while it lives
    bool was = knownDrawn;
    KnownDrawn() { knownDrawn = true; }
    ~KnownDrawn() { knownDrawn = was; }
  };
  std::function<auto (bool busy) -> void> vramGuard;
  auto vramPageBusy(u32 page) const -> bool { return busyPages[page >> 6] >> (page & 63) & 1; }
  //Whether the drawing that keeps pages busy reaches VRAM's bytes first to last (offsets in VRAM, inclusive): the
  //GE's (GE::drawnOver()). A hardware renderer's pixels may share their pages with bytes no one draws over (a list
  //kept in the unused columns beside a frame buffer's picture), which pointer() then reaches without waiting. None:
  //all of a busy page is drawn over.
  std::function<auto (u32 first, u32 last) -> bool> vramDrawnOver;
  //A change to VRAM's bytes first to last while pages are busy, reached without waiting (changed()): for the GE to
  //hear of, as a hardware renderer keeps its own copies of the pages.
  std::function<auto (u32 first, u32 last) -> void> vramChangedBusy;

  //memory.cpp
  static auto vramOffset(u32 copy, u32 seen) -> u32;
  static auto vramSeen(u32 copy, u32 offset) -> u32;
  auto power(u32 ramSize = 32_MiB) -> void;
  auto canWatch() const -> bool { return (bool)watching && !watched.empty(); }
  auto watch(u32 address, u32 size) -> void;
  auto unwatchAll() -> void;
  auto pagesOf(u32 address, u32 size, u32& first, u32& last) const -> bool;
  auto serialize(serializer& s) -> void;
  auto pointer(u32 address, u32 size = 1) -> u8*;
  auto reaches(u32 address, u32 size) -> bool;
  auto read(u32 size, u32 address) -> u32;
  auto write(u32 size, u32 address, u32 data) -> void;
  auto changed(u32 address, u32 size) -> void;
  auto buildPages(std::vector<u8*>& table) -> void;
  auto copyIn(u32 address, const void* data, u32 size) -> bool;
  auto copyOut(void* data, u32 address, u32 size) -> bool;
  auto fill(u32 address, u8 value, u32 size) -> bool;
  auto readString(u32 address, u32 limit) -> std::string;
};

}
