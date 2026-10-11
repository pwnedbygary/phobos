//The memory map (ares/psp/memory): what each address reaches, the hooks, the page table, and the CPU running on it.
#include "system.hpp"

namespace allegrex_test::psp {

//The four windows (user, uncached, kernel, kernel uncached) reach the same byte, little-endian.
static auto windows() -> void {
  System s;
  s.memory.write(Allegrex::Word, 0x0880'0100, 0x1122'3344);
  for(u32 window : {0x0000'0000u, 0x4000'0000u, 0x8000'0000u, 0xa000'0000u}) {
    CHECK(s.memory.read(Allegrex::Word, window | 0x0880'0100), 0x1122'3344);
    CHECK(s.memory.read(Allegrex::Half, window | 0x0880'0102), 0x1122);
    CHECK(s.memory.read(Allegrex::Byte, window | 0x0880'0100), 0x44);
  }
  s.memory.write(Allegrex::Byte, 0xa880'0101, 0xee);
  s.memory.write(Allegrex::Half, 0x4880'0102, 0xbeef);
  CHECK(s.memory.read(Allegrex::Word, 0x0880'0100), 0xbeef'ee44);
  CHECK(s.unmapped.size(), 0);
}

//Each area from its first word to its last; VRAM's four copies; what's past the end of each.
static auto areas() -> void {
  System s;
  auto roundTrip = [&](u32 address, u32 value) {
    s.memory.write(Allegrex::Word, address, value);
    return s.memory.read(Allegrex::Word, address);
  };
  CHECK(roundTrip(0x0001'0000, 1), 1);
  CHECK(roundTrip(0x0001'3ffc, 2), 2);
  CHECK(roundTrip(0x0400'0000, 3), 3);
  CHECK(roundTrip(0x041f'fffc, 4), 4);
  CHECK(roundTrip(0x0800'0000, 5), 5);
  CHECK(roundTrip(0x09ff'fffc, 6), 6);
  CHECK(s.unmapped.size(), 0);
  s.memory.write(Allegrex::Word, 0x0400'0010, 0xcafe'f00d);
  //the third copy shows VRAM as it is; the second and fourth rearrange it (memory.hpp), so see the word elsewhere
  CHECK(s.memory.read(Allegrex::Word, 0x0440'0010), 0xcafe'f00d);
  CHECK(s.memory.read(Allegrex::Word, 0x0420'2050), 0xcafe'f00d);  //offset bits 6 and 13 flipped
  CHECK(s.memory.read(Allegrex::Word, 0x0460'2030), 0xcafe'f00d);  //bits 5-9 turned round by one, then those two
  CHECK(s.memory.read(Allegrex::Word, 0x0420'0010), 0);
  //an HLE function's word at a game's unaligned address, across one of those pieces: a byte at a time, each where the
  //fourth copy puts it (offsets 0x1e-0x21 at 0x205e, 0x205f, 0x2000 and 0x2001)
  s.memory.write(Allegrex::Word, 0x0460'001e, 0x4433'2211);
  CHECK(s.memory.read(Allegrex::Word, 0x0460'001e), 0x4433'2211);
  CHECK(s.memory.read(Allegrex::Half, 0x0400'205e), 0x2211);
  CHECK(s.memory.read(Allegrex::Half, 0x0400'2000), 0x4433);

  //nothing behind these: reads give 0, writes go nowhere, and unmapped() hears of each
  for(u32 address : {0x0000'0000u, 0x0000'fffcu, 0x0001'4000u, 0x0480'0000u, 0x0a00'0000u, 0x1c00'0000u, 0x1fc0'0000u}) {
    s.unmapped.clear();
    s.memory.write(Allegrex::Word, address, 0x1234'5678);
    CHECK(s.memory.read(Allegrex::Word, address), 0);
    CHECK(s.unmapped.size(), 2);
    if(s.unmapped.size() == 2) {
      CHECK(s.unmapped[0].first, address);
      CHECK(s.unmapped[0].second, true);
      CHECK(s.unmapped[1].second, false);
    }
  }

  //later models' 64 MiB
  System big(64_MiB);
  CHECK(big.memory.ram.size(), 64_MiB);
  big.memory.write(Allegrex::Word, 0x0bff'fffc, 7);
  CHECK(big.memory.read(Allegrex::Word, 0x0bff'fffc), 7);
  CHECK(big.unmapped.size(), 0);
  big.memory.read(Allegrex::Word, 0x0c00'0000);
  CHECK(big.unmapped.size(), 1);
}

//written() hears of every change, whoever makes it, with its address and size; VRAM's for all four copies, where
//each sees it (a longer change through a copy that rearranges VRAM: every 16 KiB it touches). Reading tells it
//nothing.
static auto written() -> void {
  System s;
  std::vector<std::pair<u32, u32>> changes;
  s.memory.written = [&](u32 address, u32 size) { changes.push_back({address, size}); };
  s.memory.write(Allegrex::Half, 0x8880'0002, 1);
  u8 data[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  s.memory.copyIn(0x0001'0010, data, sizeof(data));
  s.memory.fill(0x0880'1000, 0xff, 256);
  CHECK(changes.size(), 3);
  if(changes.size() == 3) {
    CHECK(changes[0].first, 0x8880'0002);
    CHECK(changes[0].second, 2);
    CHECK(changes[1].first, 0x0001'0010);
    CHECK(changes[1].second, 12);
    CHECK(changes[2].first, 0x0880'1000);
    CHECK(changes[2].second, 256);
  }
  changes.clear();
  s.memory.write(Allegrex::Word, 0x4440'0020, 9);  //VRAM's third copy, uncached
  const u32 seen[4] = {0x0400'0020, 0x0420'2060, 0x0440'0020, 0x0460'2220};
  CHECK(changes.size(), 4);
  for(u32 i = 0; i < changes.size() && i < 4; i++) {
    CHECK(changes[i].first, seen[i]);
    CHECK(changes[i].second, 4);
  }
  changes.clear();
  s.memory.fill(0x0460'4010, 7, 64);  //through the fourth copy, across 32-byte pieces
  CHECK(changes.size(), 4);
  for(u32 i = 0; i < changes.size() && i < 4; i++) {
    CHECK(changes[i].first, 0x0400'4000 + i * 0x20'0000);
    CHECK(changes[i].second, 0x4000);
  }
  changes.clear();
  s.memory.fill(0x0400'4010, 7, 64);  //through the first: the third copy sees it there too, the others in 16 KiB
  const std::pair<u32, u32> reported[4] = {
    {0x0400'4010, 64}, {0x0420'4000, 0x4000}, {0x0440'4010, 64}, {0x0460'4000, 0x4000},
  };
  CHECK(changes.size(), 4);
  for(u32 i = 0; i < changes.size() && i < 4; i++) {
    CHECK(changes[i].first, reported[i].first);
    CHECK(changes[i].second, reported[i].second);
  }
  changes.clear();
  u8 out[12];
  s.memory.copyOut(out, 0x0001'0010, sizeof(out));
  s.memory.read(Allegrex::Word, 0x0880'0000);
  s.memory.readString(0x0880'1000, 16);
  CHECK(changes.size(), 0);
  CHECK(out[11], 12);
}

//The loader's and HLE functions' copies: inside an area they work; a range that crosses an area's end (or starts
//where there's nothing) fails and changes nothing.
static auto bulk() -> void {
  System s;
  const char text[] = "ms0:/PSP/GAME";
  CHECK(s.memory.copyIn(0x0880'0000, text, sizeof(text)), true);
  CHECK(s.memory.readString(0x0880'0000, 64) == "ms0:/PSP/GAME", true);
  CHECK(s.memory.readString(0x0880'0000, 4) == "ms0:", true);
  CHECK(s.memory.readString(0x0a00'0000, 64).empty(), true);  //nothing there
  s.memory.fill(0x09ff'fffc, 'x', 4);  //a string running into the end of RAM stops there
  CHECK(s.memory.readString(0x09ff'fffc, 64) == "xxxx", true);

  u8 data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(s.memory.copyIn(0x09ff'fffc, data, 8), false);  //runs past the end of RAM
  CHECK(s.memory.read(Allegrex::Word, 0x09ff'fffc), 0x7878'7878);
  CHECK(s.memory.copyIn(0x0001'3ffc, data, 8), false);  //past the scratchpad
  CHECK(s.memory.copyIn(0x041f'fffc, data, 8), false);  //past one copy of VRAM into the next
  CHECK(s.memory.fill(0x0000'0000, 1, 4), false);
  u8 out[8] = {};
  CHECK(s.memory.copyOut(out, 0x0a00'0000, 8), false);
  CHECK(s.memory.copyIn(0x0001'3ff8, data, 8), true);
  CHECK(s.memory.copyOut(out, 0x4001'3ff8, 8), true);
  CHECK(out[7], 8);
  CHECK(s.memory.copyIn(0x0880'0000, data, 0), true);  //nothing to copy is fine anywhere

  //through VRAM's fourth copy, piece by piece (it rearranges 32-byte pieces): back the same way, rearranged in VRAM
  u8 many[100], back[100] = {};
  for(u32 n = 0; n < 100; n++) many[n] = n + 1;
  CHECK(s.memory.copyIn(0x0460'0010, many, 100), true);
  CHECK(s.memory.copyOut(back, 0x0460'0010, 100), true);
  CHECK(std::memcmp(many, back, 100) == 0, true);
  CHECK(s.memory.read(Allegrex::Byte, 0x0400'2050), 1);  //the first byte, where the fourth copy's offset 0x10 is
  CHECK(s.memory.copyIn(0x047f'fff0, many, 32), false);  //past the fourth copy's end
}

//The page table: each area's pages point at its bytes (VRAM's at its first copy only), the rest at nothing; and
//power() keeps the buffers (and so the table) while the sizes stay the same.
static auto pageTable() -> void {
  System s;
  CHECK(s.table.size(), 512_MiB / 4_KiB);
  CHECK(s.table[0x0001'0000 >> 12] == s.memory.scratchpad.data(), true);
  CHECK(s.table[0x0001'3000 >> 12] == s.memory.scratchpad.data() + 0x3000, true);
  CHECK(s.table[0x0001'4000 >> 12] == nullptr, true);
  CHECK(s.table[0x0400'0000 >> 12] == s.memory.vram.data(), true);
  CHECK(s.table[0x041f'f000 >> 12] == s.memory.vram.data() + 0x1f'f000, true);
  CHECK(s.table[0x0420'0000 >> 12] == nullptr, true);
  CHECK(s.table[0x0460'1000 >> 12] == nullptr, true);
  CHECK(s.table[0x0480'0000 >> 12] == nullptr, true);
  CHECK(s.table[0x0880'0000 >> 12] == s.memory.ram.data() + 0x80'0000, true);
  CHECK(s.table[0x09ff'f000 >> 12] == s.memory.ram.data() + 0x1ff'f000, true);
  CHECK(s.table[0x0a00'0000 >> 12] == nullptr, true);
  CHECK(s.table[0x1c00'0000 >> 12] == nullptr, true);
  u8* before = s.memory.ram.data();
  s.memory.write(Allegrex::Word, 0x0880'0000, 1);
  s.memory.power();
  CHECK(s.memory.ram.data() == before, true);
  CHECK(s.memory.read(Allegrex::Word, 0x0880'0000), 0);
}

//The CPU on the memory map, on both engines: loads and stores through the windows and in each area, and code that
//rewrites a function it already ran (the store goes through write(), whose written() drops the compiled code),
//also when the function is in VRAM and rewritten through another copy of it.
static auto cpu() -> void {
  for(bool recompile : {false, true}) {
    System s;
    s.runProgram(0x0880'0000, {
      lui(s0, 0x0890),                //s0 = 0x08900000
      lui(s1, 0x4890),                //the same, uncached
      lui(s2, 0x0001),                //the scratchpad
      lui(s3, 0x0420),                //VRAM's second copy
      addiu(t0, zero, 0x1234),
      sw(t0, 0, s0),
      lw(v0, 0, s1),                  //v0 = 0x1234
      sw(t0, 0x10, s2),
      lw(v1, 0x10, s2),               //v1 = 0x1234
      sh(t0, 2, s3),
      lui(t1, 0x0400),
      lw(a0, 0x2040, t1),             //a0 = 0x12340000, through the first copy (memory.hpp: bits 6 and 13)
      lui(t2, 0x0a00),
      lw(a1, 0, t2),                  //nothing there: a1 = 0
    }, recompile);
    CHECK(s.ipu.r[v0], 0x1234);
    CHECK(s.ipu.r[v1], 0x1234);
    CHECK(s.ipu.r[a0], 0x1234'0000);
    CHECK(s.ipu.r[a1], 0);
    CHECK(s.unmapped.size(), 1);
    CHECK(s.exceptions.size(), 0);

    //function: addiu v0, zero, 1; jr ra; nop. Called, rewritten to return 2, called again.
    System t;
    constexpr u32 Function = 0x0880'0100;
    t.memory.write(Allegrex::Word, Function + 0, addiu(v0, zero, 1));
    t.memory.write(Allegrex::Word, Function + 4, jr(ra));
    t.memory.write(Allegrex::Word, Function + 8, nop);
    t.runProgram(0x0880'0000, {
      jal(Function), nop,
      addu(s1, v0, zero),
      lui(t0, 0x2402), ori(t0, t0, 0x0002),  //t0 = addiu v0, zero, 2
      lui(s0, 0x0880),
      sw(t0, 0x100, s0),
      jal(Function), nop,
      addu(s2, v0, zero),
    }, recompile);
    CHECK(t.ipu.r[s1], 1);
    CHECK(t.ipu.r[s2], 2);

    //The same function in VRAM, rewritten through another copy: compiled from the first copy and rewritten
    //through the second (a store through write(), reported for every copy), then the other way around (code in
    //the second copy is interpreted, so a compiled store through the first can't leave it stale). The second copy
    //sees VRAM's offset 0x100 at 0x2140 (memory.hpp).
    for(auto [runAt, writeAt] : {std::pair{0x0400'0100u, 0x0420'2140u}, std::pair{0x0420'0100u, 0x0400'2140u}}) {
      System v;
      v.memory.write(Allegrex::Word, runAt + 0, addiu(v0, zero, 1));
      v.memory.write(Allegrex::Word, runAt + 4, jr(ra));
      v.memory.write(Allegrex::Word, runAt + 8, nop);
      v.runProgram(0x0880'0000, {
        jal(runAt), nop,
        addu(s1, v0, zero),
        lui(t0, 0x2402), ori(t0, t0, 0x0002),
        lui(s0, writeAt >> 16),
        sw(t0, int32_t(writeAt & 0xffff), s0),
        jal(runAt), nop,
        addu(s2, v0, zero),
      }, recompile);
      CHECK(v.ipu.r[s1], 1);
      CHECK(v.ipu.r[s2], 2);
      CHECK(v.unmapped.size(), 0);
    }
  }
}

//VRAM's copies (memory.hpp): each way there and back is the other's reverse, keeps an offset's 16 KiB and its low
//five bits, and the GE's depth buffer is seen through them as a PSP showed it (round 3's depth-layout files: a buffer
//512 pixels wide at 0x18'0000, columns 0-255 drawn).
static auto vramCopies() -> void {
  u32 mismatched = 0;
  for(u32 copy = 0; copy < 4; copy++) {
    for(u32 offset = 0; offset < Memory::VRAMSize; offset++) {
      u32 there = Memory::vramOffset(copy, offset);
      bool good = Memory::vramSeen(copy, there) == offset && (there & ~0x3fe0u) == (offset & ~0x3fe0u);
      mismatched += !good;
    }
  }
  CHECK(mismatched, 0);
  u32 wrong = 0;
  for(u32 y = 0; y < 64; y++) {
    for(u32 x = 0; x < 256; x++) {
      u32 inOrder = 0x18'0000 + (y * 512 + x) * 2;  //where the GE has pixel (x, y): the fourth copy's view
      u32 there = Memory::vramOffset(3, inOrder);
      //the first and third copies' view, then the second's and the fourth's
      wrong += there != 0x18'0000 + ((y ^ 8) * 512 + ((x >> 4) ^ 1) * 32 + (x & 15)) * 2;
      wrong += Memory::vramSeen(1, there) != 0x18'0000 + (y * 512 + (x >> 4) * 32 + (x & 15)) * 2;
      wrong += Memory::vramSeen(3, there) != inOrder;
    }
  }
  CHECK(wrong, 0);
}

auto memoryTests() -> Tests {
  return {
    {"memory windows", windows}, {"memory areas", areas}, {"memory written", written}, {"memory bulk", bulk},
    {"memory page table", pageTable}, {"memory and the cpu", cpu}, {"memory vram copies", vramCopies},
  };
}

}
