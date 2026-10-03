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
//                           times in a row up to 0x047f'ffff (on the PSP some of those copies rearrange the bytes
//                           for the GE's depth buffer, "swizzling"; not emulated yet)
//  0x0800'0000-0x09ff'ffff  main RAM: 32 MiB on the PSP-1000 (64 MiB, to 0x0bff'ffff, on later models). The
//                           kernel owns the first 8 MiB; games get the rest, from 0x0880'0000
//  0x1c00'0000-0x1fff'ffff  hardware registers and the boot ROM: nothing there yet, as the HLE kernel answers for
//                           the hardware
//Everything else is empty: reading it gives 0, writing it does nothing, and unmapped() is told.
//
//The bytes are kept in the host's memory as the PSP sees them, little-endian, so a word is read by copying four
//bytes (Phobos's hosts, ARM64 and x86-64, are little-endian too).

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

  //memory.cpp
  auto power(u32 ramSize = 32_MiB) -> void;
  auto pointer(u32 address, u32 size = 1) -> u8*;
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
