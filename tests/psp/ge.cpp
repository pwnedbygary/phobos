//The GE (ares/psp/ge) and its driver (ares/psp/kernel/ge.cpp), with the kernel pieces they need: display lists written
//here, run by the GE alone (moving about a list and stopping, registers and matrices, vertices, clearing, block
//transfers) and through the driver (the queue, stall addresses, syncing, callbacks into programs written here,
//signals, saved state); event flags; calls into the program; the picture on the screen; and, with PSP_TEST_PROGRAMS,
//a GU program of Phobos's own and pspsdk's GU sample "copy".
#include "kernel-machine.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace allegrex_test::psp {

using ares::PlayStationPortable::GeVersions;

constexpr u32 ListA = 0x0894'0000, ListB = 0x0895'0000, Vertices = 0x0896'0000, Saved = 0x0897'0000;
constexpr u32 Image = 0x0898'0000;
constexpr u32 VRAM = Memory::VRAMBase;

//The GE's time as the kernel has it, every run handed to the GE's thread counted (Kernel::geFold()).
static auto geDone(KernelMachine& m) -> u64 {
  m.kernel.geFold(true);
  return m.kernel.geDoneAt;
}

//Writes a display list into memory a command at a time.
struct ListWriter {
  Memory& memory;
  u32 address;
  auto put(u32 command, u32 argument = 0) -> void {
    memory.write(4, address, command << 24 | (argument & 0xff'ffff));
    address += 4;
  }
  //BASE with the address's top bits, then the command with the rest, as the GU library does
  auto to(u32 command, u32 target) -> void {
    put(GE::Base, target >> 8 & 0xf'0000);
    put(command, target);
  }
};

//Registers keep their commands' words; a matrix's DATA fills from where its NUMBER pointed, and no further than the
//matrix's end; vertex and index addresses take BASE's top bits.
static auto geCommands() -> void {
  System s;
  auto& ge = s.ge;
  ListWriter list{s.memory, ListA};
  list.put(GE::Base, 0x08'0000);
  list.put(GE::VertexAddress, 0x96'0010);
  list.put(GE::IndexAddress, 0x96'0020);
  list.put(GE::VertexType, 0x80'011c);
  list.put(GE::WorldMatrixNumber, 10);
  list.put(GE::WorldMatrixData, 0x3f'8000);  //1.0
  list.put(GE::WorldMatrixData, 0xc0'0000);  //-2.0
  list.put(GE::WorldMatrixData, 0x12'3456);  //past the matrix's end
  list.put(GE::ProjectionMatrixNumber, 15);
  list.put(GE::ProjectionMatrixData, 0x40'4000);
  list.put(GE::Finish, 0x1234);
  list.put(GE::End, 0x5678);
  ge.list.address = ListA;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  CHECK(ge.vertexAddress, 0x0896'0010);
  CHECK(ge.indexAddress, 0x0896'0020);
  CHECK(ge.commands[GE::VertexType], 0x1280'011c);
  CHECK(ge.world[10], 0x3f'8000);
  CHECK(ge.world[11], 0xc0'0000);
  CHECK(GE::float24(ge.world[10]) == 1.0f && GE::float24(ge.world[11]) == -2.0f, true);
  CHECK(ge.projection[15], 0x40'4000);
  CHECK(ge.finishWord, 0x0f00'1234);
  CHECK(ge.endWord, 0x0c00'5678);
  CHECK(ge.list.address, list.address);
}

//Moving about a list: CALL and RET two deep, keeping the offset; OFFSET_ADDR and ORIGIN added to JUMP's target. A
//third CALL stops the GE; a RET with no CALL is passed over, as Need for Speed: Most Wanted's lists need.
static auto geMoving() -> void {
  System s;
  auto& ge = s.ge;
  ListWriter main{s.memory, ListA};
  main.put(GE::OffsetAddress, 0x10);           //an offset of 0x1000, which the CALL keeps
  main.put(GE::Base, 0x08'0000);
  main.put(GE::Call, 0x95'0000 - 0x1000);      //ListB, less the offset
  main.put(GE::VertexType, 1);                 //back here after the RET,
  main.put(GE::VertexAddress, 0x500);          //with the offset (0x1000) as it was before the CALL
  main.put(GE::OffsetAddress, 0x08'9500);      //an offset of ListB
  main.put(GE::Base, 0);
  main.put(GE::Jump, 0x100);                   //ListB + 0x100
  ListWriter called{s.memory, ListB};
  called.put(GE::OffsetAddress, 0);
  called.put(GE::Base, 0x08'0000);
  called.put(GE::Call, 0x95'0080);             //two deep
  called.put(GE::Return);
  ListWriter deeper{s.memory, ListB + 0x80};
  deeper.put(GE::IndexAddress, 0x77);          //BASE (8) gives the top bits, the offset (0) nothing
  deeper.put(GE::Return);
  ListWriter jumped{s.memory, ListB + 0x100};
  jumped.put(GE::Origin);                      //the offset becomes this command's address
  jumped.put(GE::Base, 0);
  jumped.put(GE::Jump, 0x10);                  //ListB + 0x110
  ListWriter end{s.memory, ListB + 0x110};
  end.put(GE::Finish);
  end.put(GE::End);
  ge.list.address = ListA;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  CHECK(ge.indexAddress, 0x0800'0077);
  CHECK(ge.commands[GE::VertexType], 0x1200'0001);
  CHECK(ge.vertexAddress, 0x0800'1500);
  CHECK(ge.list.offset, ListB + 0x100);
  CHECK(ge.list.address, ListB + 0x118);
  CHECK(ge.list.depth, 0);

  ListWriter loop{s.memory, ListA};  //calls itself until the GE runs out of room
  loop.put(GE::Base, 0x08'0000);
  loop.put(GE::Call, 0x94'0000);
  ge.list = {};
  ge.list.address = ListA;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Faulted));
  CHECK(ge.list.depth, 2);
  ListWriter stray{s.memory, ListA};  //the game's: a frame buffer's settings made to be CALLed, run in place
  stray.put(GE::OffsetAddress, 0x20);
  stray.put(GE::FrameBufferPointer, 0x0c'c000);
  stray.put(GE::Return, 0x0c'c000);
  stray.put(GE::DepthBufferWidth, 0x100);
  stray.put(GE::Return);
  stray.put(GE::Finish);
  stray.put(GE::End);
  ge.list = {};
  ge.list.returnAddress[0] = ListB;  //a CALL's, returned from: no RET goes back there again
  ge.list.address = ListA;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  CHECK(ge.commands[GE::FrameBufferPointer], 0x9c0c'c000);
  CHECK(ge.commands[GE::DepthBufferWidth], 0x9f00'0100);
  CHECK(ge.commands[GE::Return], 0x0b00'0000);
  CHECK(ge.list.address, stray.address);
  CHECK(ge.list.depth, 0);
  CHECK(ge.list.offset, 0x2000);
}

//Stopping: at the stall address, and going on once it moves; at an END, saying what came before it; and out of
//budget.
static auto geStops() -> void {
  System s;
  auto& ge = s.ge;
  ListWriter list{s.memory, ListA};
  list.put(GE::Nop);
  list.put(GE::VertexType, 2);
  list.put(GE::Signal, 0x01'0007);
  list.put(GE::End, 0x1234);
  list.put(GE::End);
  list.put(GE::Finish, 9);
  list.put(GE::End);
  ge.list.address = ListA;
  ge.list.stall = ListA + 4;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Stalled));
  CHECK(ge.list.address, ListA + 4);
  CHECK(u32(ge.run(100)), u32(GE::Stop::Stalled));  //still there
  ge.list.stall = 0;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Signaled));
  CHECK(ge.signalWord, 0x0e01'0007);
  CHECK(ge.endWord, 0x0c00'1234);
  CHECK(u32(ge.run(100)), u32(GE::Stop::Ended));
  CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  CHECK(ge.finishWord, 0x0f00'0009);

  ListWriter spin{s.memory, ListB};
  spin.put(GE::Base, 0x08'0000);
  spin.put(GE::Jump, 0x95'0000);
  ge.list.address = ListB;
  CHECK(u32(ge.run(10)), u32(GE::Stop::Busy));
}

//Vertex layouts: each part at a multiple of its own size, the vertex a multiple of its largest part's, times its
//morph targets; and their numbers as stored (signed or not; through mode's depth unsigned; colors widened).
static auto geVertices() -> void {
  System s;
  auto& ge = s.ge;
  auto format = [&](u32 type) { ge.commands[GE::VertexType] = type; return ge.vertexFormat(); };
  auto clear = format(0x80'011c);  //color 8888, 16-bit position, through: the GU library's clear
  CHECK(clear.colorOffset, 0); CHECK(clear.positionOffset, 4); CHECK(clear.size, 12);
  auto sprite = format(0x80'011a);  //16-bit texture coordinates, color 4444, 16-bit position
  CHECK(sprite.textureOffset, 0); CHECK(sprite.colorOffset, 4); CHECK(sprite.positionOffset, 6); CHECK(sprite.size, 12);
  auto floats = format(0x80'0183);  //float texture coordinates and position
  CHECK(floats.positionOffset, 8); CHECK(floats.size, 20);
  auto weighted = format(0x8000 | 0x200 | 0x80);  //three 8-bit weights, 8-bit position
  CHECK(weighted.weights, 3); CHECK(weighted.positionOffset, 3); CHECK(weighted.size, 6);
  auto mixed = format(0x4000 | 0x400 | 0x20 | 0x180);  //two 16-bit weights, 8-bit normal, float position
  CHECK(mixed.normalOffset, 4); CHECK(mixed.positionOffset, 8); CHECK(mixed.size, 20);
  auto morphed = format(0x4'0000 | 0x80'011c);  //two morph targets
  CHECK(morphed.morphs, 2); CHECK(morphed.size, 24);
  auto indexed = format(0x1000 | 0x80'011c);
  CHECK(indexed.indexFormat, 2);

  auto& memory = s.memory;
  memory.write(4, Vertices, 0x8899'aabb);
  memory.write(2, Vertices + 4, 0xfffe); memory.write(2, Vertices + 6, 5); memory.write(2, Vertices + 8, 0xffff);
  auto v = ge.readVertex(Vertices, clear);
  CHECK(v.color, 0x8899'aabb);
  CHECK(v.x == -2 && v.y == 5 && v.z == 65535, true);  //through mode: x and y signed, depth not
  auto v3d = ge.readVertex(Vertices, format(0x11c));
  CHECK(v3d.z == -1.0f / 32768, true);                  //in 3D the depth is signed too, and 16-bit numbers 32768ths
  CHECK(v3d.x == -2.0f / 32768 && v3d.y == 5.0f / 32768, true);
  memory.write(2, Vertices, 0xffff);                    //5650 white
  CHECK(ge.readVertex(Vertices, format(0x10 | 0x100)).color, 0xffff'ffff);
  memory.write(2, Vertices, 0x801f);                    //5551: red at full, alpha set
  CHECK(ge.readVertex(Vertices, format(0x14 | 0x100)).color, 0xff00'00ff);
  memory.write(2, Vertices, 0x1234);                    //4444
  CHECK(ge.readVertex(Vertices, format(0x18 | 0x100)).color, 0x1122'3344);
  memory.write(1, Vertices, 200); memory.write(1, Vertices + 1, 7);  //8-bit texture coordinates are unsigned
  auto t = ge.readVertex(Vertices, format(0x80'0081));
  CHECK(t.u == 200 && t.v == 7, true);
}

//Clearing: a sprite in clear mode fills its rectangle (corners in either order) with the second vertex's color and
//depth, inside the scissor rectangle, writing color, alpha or depth as CLEAR_MODE says, in each pixel format.
static auto geClear() -> void {
  System s;
  auto& memory = s.memory;
  auto& ge = s.ge;
  auto clear = [&](u32 format, u32 mode) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 16);
    list.put(GE::FrameBufferPixelFormat, format);
    list.put(GE::DepthBufferPointer, 0x1'0000);
    list.put(GE::DepthBufferWidth, 16);
    list.put(GE::Scissor1, 1 << 10 | 2);
    list.put(GE::Scissor2, 6 << 10 | 9);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::ClearMode, mode);
    list.put(GE::VertexType, 0x80'011c);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::ClearMode, 0);
    list.put(GE::Finish);
    list.put(GE::End);
    memory.write(4, Vertices, 0x1122'3344);  //the first corner: bottom right
    memory.write(2, Vertices + 4, 12); memory.write(2, Vertices + 6, 8); memory.write(2, Vertices + 8, 0x1234);
    memory.write(4, Vertices + 12, 0xaabb'ccdd);  //the second: top left, its color and depth used
    memory.write(2, Vertices + 16, 0); memory.write(2, Vertices + 18, 0); memory.write(2, Vertices + 20, 0x4321);
    ge.list = {};
    ge.list.address = ListA;
    CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  };
  auto pixel32 = [&](u32 x, u32 y) { return memory.read(4, VRAM + (y * 16 + x) * 4); };
  auto pixel16 = [&](u32 x, u32 y) { return memory.read(2, VRAM + (y * 16 + x) * 2); };
  auto depth = [&](u32 x, u32 y) {  //as the GE has it: through VRAM's fourth copy
    return memory.read(2, VRAM + 3 * Memory::VRAMSize + 0x1'0000 + (y * 16 + x) * 2);
  };

  clear(3, 0x501);  //8888, color and depth: alpha (the stencil) is left alone
  CHECK(pixel32(2, 1), 0x00bb'ccdd);
  CHECK(pixel32(9, 6), 0x00bb'ccdd);
  CHECK(pixel32(1, 1), 0); CHECK(pixel32(10, 6), 0); CHECK(pixel32(9, 7), 0); CHECK(pixel32(2, 0), 0);
  CHECK(depth(2, 1), 0x4321); CHECK(depth(1, 1), 0);

  memory.fill(VRAM, 0, 0x1000);
  clear(0, 0x101);  //5650: 0xdd, 0xcc, 0xbb keep their top 5, 6 and 5 bits
  CHECK(pixel16(2, 1), 0xbe7b);
  CHECK(depth(2, 1), 0x4321);  //from before: depth wasn't asked for this time

  for(u32 n = 0; n < 16 * 8; n++) memory.write(2, VRAM + n * 2, 0x1234);
  clear(1, 0x201);  //5551, alpha only: the top bit from alpha 0xaa, the rest kept
  CHECK(pixel16(2, 1), 0x9234);
  for(u32 n = 0; n < 16 * 8; n++) memory.write(2, VRAM + n * 2, 0xf000);
  clear(2, 0x101);  //4444, color only: alpha 0xf kept
  CHECK(pixel16(2, 1), 0xfbcd);
}

//Block transfers: a rectangle of 4-byte or 2-byte pixels from one image to another, each with its own width and
//position; widths count in eights.
static auto geTransfer() -> void {
  System s;
  auto& memory = s.memory;
  auto& ge = s.ge;
  auto transfer = [&](u32 bytes, u32 sourceWidth) {
    ListWriter list{memory, ListA};
    list.put(GE::TransferSource, Image);
    list.put(GE::TransferSourceWidth, (Image >> 24) << 16 | sourceWidth);
    list.put(GE::TransferSourcePosition, 1 << 10 | 1);
    list.put(GE::TransferDestination, VRAM);
    list.put(GE::TransferDestinationWidth, (VRAM >> 24) << 16 | 16);
    list.put(GE::TransferDestinationPosition, 0 << 10 | 2);
    list.put(GE::TransferSize, (3 - 1) << 10 | (4 - 1));
    list.put(GE::TransferStart, bytes == 4);
    list.put(GE::Finish);
    list.put(GE::End);
    ge.list = {};
    ge.list.address = ListA;
    CHECK(u32(ge.run(100)), u32(GE::Stop::Finished));
  };
  for(u32 y = 0; y < 8; y++) {
    for(u32 x = 0; x < 8; x++) memory.write(4, Image + (y * 8 + x) * 4, y * 100 + x);
  }
  transfer(4, 8);
  CHECK(memory.read(4, VRAM + (0 * 16 + 2) * 4), 101);  //the source's (1,1) at the destination's (2,0)
  CHECK(memory.read(4, VRAM + (2 * 16 + 5) * 4), 304);  //(4,3) at (5,2)
  CHECK(memory.read(4, VRAM + (0 * 16 + 6) * 4), 0);    //past the rectangle
  CHECK(memory.read(4, VRAM + (3 * 16 + 2) * 4), 0);
  memory.fill(VRAM, 0, 0x1000);
  transfer(4, 13);  //a width of 13 counts as 8
  CHECK(memory.read(4, VRAM + (2 * 16 + 5) * 4), 304);
  memory.fill(VRAM, 0, 0x1000);
  for(u32 n = 0; n < 64; n++) memory.write(2, Image + n * 2, 0x1000 + n);
  transfer(2, 8);
  CHECK(memory.read(2, VRAM + (0 * 16 + 2) * 2), 0x1000 + 9);  //(1,1): the 9th 16-bit pixel
  CHECK(memory.read(2, VRAM + (2 * 16 + 5) * 2), 0x1000 + 28);
}

//The driver, called directly: a list stalled at its start, moved on and finished; its state as sceGeListSync and
//sceGeDrawSync tell it; a second list queued behind and taken out; the errors.
static auto geDriver() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  ListWriter list{memory, ListA};
  list.put(GE::VertexType, 0x123);
  list.put(GE::WorldMatrixNumber, 0);
  list.put(GE::WorldMatrixData, 0x3f'8000);
  u32 middle = list.address;
  list.put(GE::Finish);
  list.put(GE::End);
  u32 id = m.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0});
  CHECK(id, Kernel::GeListIDs);
  CHECK(m.call("sceGeListSync", {id, 1}), 3);  //stopped at its stall address
  CHECK(m.call("sceGeDrawSync", {1}), 3);
  u32 second = m.call("sceGeListEnQueue", {ListB, ListB, 0xffff'ffff, 0});
  CHECK(m.call("sceGeListSync", {second, 1}), 1);  //queued
  CHECK(m.call("sceGeListDeQueue", {second}), 0);
  CHECK(m.call("sceGeListSync", {second, 1}), Kernel::ErrorInvalidID);
  CHECK(m.call("sceGeListEnQueueHead", {ListB, 0, 0xffff'ffff, 0}), Kernel::ErrorInvalidValue);  //the GE has the first
  CHECK(m.call("sceGeListUpdateStallAddr", {id, middle}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 3);
  CHECK(m.call("sceGeGetCmd", {GE::VertexType}), 0x1200'0123);
  CHECK(m.call("sceGeListDeQueue", {id}), Kernel::ErrorBusy);  //the GE has started it
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);  //finished
  CHECK(m.call("sceGeDrawSync", {1}), 0);
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), Kernel::ErrorAlready);
  CHECK(m.call("sceGeDrawSync", {0}), 0);  //nothing left to wait for; finished lists are forgotten
  CHECK(m.call("sceGeListSync", {id, 1}), Kernel::ErrorInvalidID);
  CHECK(m.call("sceGeGetMtx", {8, Saved}), 0);
  CHECK(memory.read(4, Saved), 0x3f'8000);
  //the next list takes the slot freed longest ago, not the one just freed
  CHECK(m.call("sceGeListEnQueue", {ListB, ListB, 0xffff'ffff, 0}), Kernel::GeListIDs + 2);

  CHECK(m.call("sceGeListEnQueue", {ListA + 2, 0, 0xffff'ffff, 0}), Kernel::ErrorInvalidPointer);
  CHECK(m.call("sceGeListSync", {0x1234, 1}), Kernel::ErrorInvalidID);
  CHECK(m.call("sceGeListSync", {id, 2}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceGeDrawSync", {2}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceGeGetCmd", {0xff}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("sceGeGetMtx", {12, Saved}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("sceGeSaveContext", {Saved}), 0xffff'ffff);  //not while the GE has a list
  for(u32 n = 0; n < 63; n++) m.call("sceGeListEnQueue", {ListB + 16 + n * 16, 0, 0xffff'ffff, 0});
  CHECK(m.call("sceGeListEnQueue", {ListB, 0, 0xffff'ffff, 0}), Kernel::ErrorOutOfMemory);  //64 at most
}

//A list that starts as another finishes keeps the BASE that one left: the driver leaves it alone for a list that
//hasn't run (and puts it back only for one that has).
static auto geBaseKept() -> void {
  KernelMachine m;
  ListWriter first{m.system.memory, ListA};
  first.put(GE::Base, 0x08'0000);
  first.put(GE::Finish);
  first.put(GE::End);
  ListWriter second{m.system.memory, ListB};
  second.put(GE::VertexAddress, 0x96'0040);  //no BASE of its own
  second.put(GE::Finish);
  second.put(GE::End);
  u32 id = m.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0});  //stalled at its start
  m.call("sceGeListEnQueue", {ListB, 0, 0xffff'ffff, 0});               //queued behind
  m.call("sceGeListUpdateStallAddr", {id, 0});                          //the first finishes, the second runs
  CHECK(m.system.ge.vertexAddress, 0x0896'0040);
}

//Saving and restoring the GE's state, and the callbacks' numbers (16 at most).
static auto geSaved() -> void {
  KernelMachine m;
  auto& ge = m.system.ge;
  ListWriter list{m.system.memory, ListA};
  list.put(GE::VertexType, 0x456);
  list.put(GE::OffsetAddress, 0x12);
  list.put(GE::ViewMatrixNumber, 3);
  list.put(GE::ViewMatrixData, 0x40'0000);
  list.put(GE::Finish);
  list.put(GE::End);
  m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
  CHECK(m.call("sceGeSaveContext", {Saved}), 0);
  ge.commands[GE::VertexType] = 0;
  ge.view[3] = 0;
  ge.list.offset = 0;
  CHECK(m.call("sceGeRestoreContext", {Saved}), 0);
  CHECK(ge.commands[GE::VertexType], 0x1200'0456);
  CHECK(ge.view[3], 0x40'0000);
  CHECK(ge.list.offset, 0x1200);
  CHECK(m.call("sceGeSaveContext", {Saved + 2}), Kernel::ErrorInvalidPointer);

  u32 data = KernelMachine::Results;
  for(u32 n = 0; n < 16; n++) CHECK(m.call("sceGeSetCallback", {data}), n);
  CHECK(m.call("sceGeSetCallback", {data}), Kernel::ErrorOutOfMemory);
  CHECK(m.call("sceGeUnsetCallback", {5}), 0);
  CHECK(m.call("sceGeSetCallback", {data}), 5);
  CHECK(m.call("sceGeUnsetCallback", {16}), Kernel::ErrorInvalidID);
}

//Where the programs below keep what they saw.
constexpr u32 Turn = KernelMachine::Results + 0x100;     //a count each of them bumps, to see who ran when
constexpr u32 Signaled = KernelMachine::Results + 0x110; //a signal callback's arguments, gp and turn
constexpr u32 Finished = KernelMachine::Results + 0x130; //a finish callback's
constexpr u32 Callbacks = KernelMachine::Results + 0x150;  //PspGeCallbackData

//A GE callback that notes how it was called: its three arguments, its global pointer and its turn, at slot.
static auto recorder(KernelMachine& m, u32 at, u32 slot) -> u32 {
  Assembler f{m, at};
  f.li(t0, slot);
  f.put(sw(a0, 0, t0)); f.put(sw(a1, 4, t0)); f.put(sw(a2, 8, t0)); f.put(sw(gp, 12, t0));
  f.li(t1, Turn); f.put(lw(t2, 0, t1)); f.put(addiu(t2, t2, 1)); f.put(sw(t2, 0, t1)); f.put(sw(t2, 16, t0));
  f.put(jr(ra));
  f.put(nop);
  return at;
}

//Starts the program at 0x08801000 as the main thread (its global pointer gp), and runs it to its end.
static auto runMain(KernelMachine& m, bool recompile, u32 gp = 0x0812'3456) -> void {
  m.system.recompiler.enabled = recompile;
  m.system.power(0x0880'1000);
  s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, gp);
  m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  m.kernel.run(Kernel::VblankCycles * 4);
  CHECK(m.kernel.exited, true);
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

//A list that signals (SIGNAL kind 2: the GE goes on, and the callback runs) and finishes: both callbacks run, in that
//order, as soon as the program's call returns, with the ids, their arguments, where the list had got to (built with
//an SDK after 2.00.10; 0 for one saying none, as gpu/signals recorded) and the global pointer the program had; the
//program finds its result as it left it.
static auto geCallbacks() -> void {
  for(u32 version : {0x0606'0010u, 0u}) for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    memory.write(4, Callbacks, recorder(m, 0x0880'4000, Signaled));
    memory.write(4, Callbacks + 4, 0x1111);
    memory.write(4, Callbacks + 8, recorder(m, 0x0880'4100, Finished));
    memory.write(4, Callbacks + 12, 0x2222);
    ListWriter list{memory, ListA};
    list.put(GE::Signal, 0x02'0007);
    list.put(GE::End);
    list.put(GE::Finish, 42);
    list.put(GE::End);
    Assembler main{m, 0x0880'1000};
    if(version) main.li(a0, version), main.call("sceKernelSetCompiledSdkVersion");
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, v0, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.li(t1, Turn); main.put(lw(t2, 0, t1)); main.put(sw(t2, 4, t0));  //what turn it is now
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    CHECK(memory.read(4, KernelMachine::Results), Kernel::GeListIDs);
    CHECK(memory.read(4, KernelMachine::Results + 4), 2);  //both callbacks ran before the program went on
    CHECK(memory.read(4, Signaled), 7);
    CHECK(memory.read(4, Signaled + 4), 0x1111);
    CHECK(memory.read(4, Signaled + 8), version ? ListA + 8 : 0);
    CHECK(memory.read(4, Signaled + 12), 0x0812'3456);
    CHECK(memory.read(4, Signaled + 16), 1);
    CHECK(memory.read(4, Finished), 42);
    CHECK(memory.read(4, Finished + 4), 0x2222);
    CHECK(memory.read(4, Finished + 8), version ? ListA + 16 : 0);
    CHECK(memory.read(4, Finished + 16), 2);
  }
}

//A SIGNAL of kind 1 suspends the GE until its callback returns, so a callback may rewrite the list ahead; with kind 2
//the GE goes on at once, and (taking no time) is past it before the callback runs.
static auto geSuspend() -> void {
  for(u32 kind : {1u, 2u}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    Assembler patch{m, 0x0880'4000};  //writes a new command over the one after the signal
    patch.li(t0, ListA + 8); patch.li(t1, GE::VertexType << 24 | 0x222); patch.put(sw(t1, 0, t0));
    patch.put(jr(ra));
    patch.put(nop);
    memory.write(4, Callbacks, 0x0880'4000);
    ListWriter list{memory, ListA};
    list.put(GE::Signal, kind << 16);
    list.put(GE::End);
    list.put(GE::VertexType, 0x111);
    list.put(GE::Finish);
    list.put(GE::End);
    Assembler main{m, 0x0880'1000};
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, v0, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(a0, 0); main.call("sceGeDrawSync");
    main.li(a0, GE::VertexType); main.call("sceGeGetCmd");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.call("sceKernelExitGame");
    runMain(m, false);
    CHECK(memory.read(4, KernelMachine::Results), GE::VertexType << 24 | (kind == 1 ? 0x222 : 0x111));
  }
}

//A list's finish callback runs before the next list starts, so it can change what that list meets: here it rewrites
//the next list's first command.
static auto geFinishOrder() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  Assembler patch{m, 0x0880'4000};
  patch.li(t0, ListB); patch.li(t1, GE::VertexType << 24 | 0x222); patch.put(sw(t1, 0, t0));
  patch.put(jr(ra));
  patch.put(nop);
  memory.write(4, Callbacks + 8, 0x0880'4000);  //the finish function
  ListWriter first{memory, ListA};
  first.put(GE::Finish);
  first.put(GE::End);
  ListWriter second{memory, ListB};
  second.put(GE::VertexType, 0x111);
  second.put(GE::Finish);
  second.put(GE::End);
  Assembler main{m, 0x0880'1000};
  main.li(a0, Callbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  main.li(a0, ListA); main.li(a1, ListA); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");  //stalled at its start
  main.put(addu(s0, v0, zero));
  main.li(a0, ListB); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");  //queued behind
  main.put(addu(a0, s0, zero)); main.li(a1, 0);
  main.call("sceGeListUpdateStallAddr");
  main.li(a0, 0); main.call("sceGeDrawSync");
  main.li(a0, GE::VertexType); main.call("sceGeGetCmd");
  main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
  main.call("sceKernelExitGame");
  runMain(m, false);
  CHECK(memory.read(4, KernelMachine::Results), GE::VertexType << 24 | 0x222);
}

//A PAUSE signal: the list runs on to its next FINISH and stops there, paused; its signal callback gets the signal's
//id; sceGeContinue lets it go on to the end.
static auto gePause() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  memory.write(4, Callbacks, recorder(m, 0x0880'4000, Signaled));
  ListWriter list{memory, ListA};
  list.put(GE::Signal, 0x03'0055);
  list.put(GE::End);
  list.put(GE::VertexType, 7);
  list.put(GE::Finish);
  list.put(GE::End);
  list.put(GE::VertexType, 8);
  list.put(GE::Finish);
  list.put(GE::End);
  constexpr u32 Paused = KernelMachine::Results, PausedType = KernelMachine::Results + 4;
  constexpr u32 Continued = KernelMachine::Results + 8, Done = KernelMachine::Results + 12;
  Assembler main{m, 0x0880'1000};
  main.li(a0, Callbacks); main.call("sceGeSetCallback");
  main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, v0, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
  main.li(t0, Paused); main.put(sw(v0, 0, t0));
  main.li(a0, GE::VertexType); main.call("sceGeGetCmd");
  main.li(t0, PausedType); main.put(sw(v0, 0, t0));
  main.call("sceGeContinue");
  main.li(t0, Continued); main.put(sw(v0, 0, t0));
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
  main.li(t0, Done); main.put(sw(v0, 0, t0));
  main.call("sceKernelExitGame");
  runMain(m, false);
  CHECK(memory.read(4, Signaled), 0x55);  //the id alone
  CHECK(memory.read(4, Paused), 4);
  CHECK(memory.read(4, PausedType), GE::VertexType << 24 | 7);
  CHECK(memory.read(4, Continued), 0);
  CHECK(memory.read(4, Done), 0);
  CHECK(m.system.ge.commands[GE::VertexType], GE::VertexType << 24 | 8);
}

//What a callback sees, as pspautotests' gpu/ge/callbackstate, gpu/ge/queue2 and gpu/signals recorded: waiting for
//it (a SIGNAL that suspends, a FINISH) the GE has stopped, and sceGeSaveContext saves; in a finish callback the list
//is done, so sceGeDrawSync(1) says 2 with another list behind it and 0 for the last; a SIGNAL that lets the GE go on
//finds it running (here stalled: 3, and nothing saved). Each callback notes both at Slots + 8 * its id. The same with
//SDK 6.60 (callbackstate's) and with none said (the list paused for its suspending signal's callback: geOldSdk()).
static auto geCallbackState() -> void {
  for(u32 version : {0x0606'0010u, 0u}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    constexpr u32 Slots = KernelMachine::Results + 0x40, ListC = Vertices;
    Assembler callback{m, 0x0880'4000};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp)); callback.put(sw(s0, 8, sp));
    callback.put(sll(s0, a0, 3));
    callback.li(a0, 1); callback.call("sceGeDrawSync");
    callback.li(t0, Slots); callback.put(addu(t0, t0, s0)); callback.put(sw(v0, 0, t0));
    callback.li(a0, Saved); callback.call("sceGeSaveContext");
    callback.li(t0, Slots); callback.put(addu(t0, t0, s0)); callback.put(sw(v0, 4, t0));
    callback.put(lw(s0, 8, sp)); callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.put(jr(ra));
    callback.put(nop);
    memory.write(4, Callbacks, 0x0880'4000);
    memory.write(4, Callbacks + 8, 0x0880'4000);
    ListWriter a{memory, ListA};
    a.put(GE::Signal, 0x01'0001);  //suspends
    a.put(GE::End);
    a.put(GE::Finish, 2);
    a.put(GE::End);
    ListWriter b{memory, ListB};
    b.put(GE::Finish, 3);
    b.put(GE::End);
    ListWriter c{memory, ListC};
    c.put(GE::Signal, 0x02'0004);  //lets the GE go on, to the stall address
    c.put(GE::End);
    c.put(GE::Nop);
    c.put(GE::Finish, 5);
    c.put(GE::End);
    for(u32 n = 0; n < 6 * 2; n++) memory.write(4, Slots + n * 4, 0xcccc'cccc);
    Assembler main{m, 0x0880'1000};
    if(version) main.li(a0, version), main.call("sceKernelSetCompiledSdkVersion");
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.put(addu(s1, v0, zero));
    main.li(a0, ListA); main.li(a1, ListA); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");  //stalled at its start
    main.put(addu(s0, v0, zero));
    main.li(a0, ListB); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");  //queued behind
    main.put(addu(a0, s0, zero)); main.li(a1, 0);
    main.call("sceGeListUpdateStallAddr");
    main.li(a0, 0); main.call("sceGeDrawSync");
    main.li(a0, ListC); main.li(a1, ListC + 8); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(a0, 1); main.li(a1, 0); main.call("sceGeBreak");
    main.call("sceKernelExitGame");
    runMain(m, false);
    CHECK(memory.read(4, Slots + 8), 2);   //the suspending signal: list A still drawing,
    CHECK(memory.read(4, Slots + 12), 0);  //and the GE stopped
    CHECK(memory.read(4, Slots + 16), 2);  //list A's finish: list B queued behind
    CHECK(memory.read(4, Slots + 20), 0);
    CHECK(memory.read(4, Slots + 24), 0);  //list B's: the last
    CHECK(memory.read(4, Slots + 28), 0);
    CHECK(memory.read(4, Slots + 32), 3);  //list C's signal: the GE goes on and stalls
    CHECK(memory.read(4, Slots + 36), 0xffff'ffff);
    CHECK(memory.read(4, Slots + 40), 0xcccc'cccc);  //list C never finished
  }
}

//Between a PAUSE signal and the FINISH that delivers it, a list reads as paused, though the GE is still on it: here
//stalled, sceGeDrawSync(1) says 3; moved on, the list's stall address goes no further than the list, and the GE,
//still waiting where it was, reads as drawing (pspautotests' gpu/ge/queue2, "Pause window").
static auto gePauseWindow() -> void {
  KernelMachine m;
  ListWriter list{m.system.memory, ListA};
  list.put(GE::Signal, 0x03'1234);
  list.put(GE::End);
  list.put(GE::Nop);
  list.put(GE::Nop);  //stalled here
  list.put(GE::Finish);
  list.put(GE::End);
  u32 id = m.call("sceGeListEnQueue", {ListA, ListA + 12, 0xffff'ffff, 0});
  CHECK(m.call("sceGeListSync", {id, 1}), 4);
  CHECK(m.call("sceGeDrawSync", {1}), 3);
  CHECK(m.call("sceGeContinue", {}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 4);
  CHECK(m.call("sceGeDrawSync", {1}), 2);
  CHECK(m.system.ge.list.address, ListA + 12);
}

//A list at an address the queue holds already: queued again for a program built with an SDK before 2.00 (or saying
//none), refused (BUSY) from 2.00 on, by where a list starts or where it was broken off, in either of memory's views,
//and while its finish callback waits; not by where it is now (pspautotests' gpu/ge/queue and queue2).
static auto geQueuedTwice() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  ListWriter list{memory, ListA};
  list.put(GE::Nop);
  list.put(GE::Nop);  //stalled here
  list.put(GE::Finish);
  list.put(GE::End);
  for(u32 version : {0u, 0x0100'0010u}) {
    m.call("sceKernelSetCompiledSdkVersion", {version});
    CHECK(m.call("sceGeListEnQueue", {ListA, ListA + 4, 0xffff'ffff, 0}) >> 31, 0);
    CHECK(m.call("sceGeListEnQueue", {ListA, ListA + 4, 0xffff'ffff, 0}) >> 31, 0);
    m.call("sceGeBreak", {1, 0});
  }
  m.call("sceKernelSetCompiledSdkVersion", {0x0200'0000});
  u32 first = m.call("sceGeListEnQueue", {ListA, ListA + 4, 0xffff'ffff, 0});
  CHECK(first, Kernel::GeListIDs);
  CHECK(m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListEnQueue", {ListA | 0x4000'0000, 0, 0xffff'ffff, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListEnQueue", {ListA + 4, ListA + 4, 0xffff'ffff, 0}), Kernel::GeListIDs + 1);
  m.call("sceGeBreak", {1, 0});
  first = m.call("sceGeListEnQueue", {ListA, ListA + 4, 0xffff'ffff, 0});
  CHECK(m.call("sceGeBreak", {0, 0}), first);  //broken off where it stalled
  CHECK(m.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0}) >> 31, 0);
  CHECK(m.call("sceGeListEnQueue", {ListA + 4, ListA + 4, 0xffff'ffff, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListEnQueueHead", {ListA + 4, 0, 0xffff'ffff, 0}), Kernel::ErrorBusy);
  m.call("sceGeBreak", {1, 0});
  memory.write(4, Callbacks + 8, 0x0880'4000);  //a finish callback, which never runs here
  u32 callbacks = m.call("sceGeSetCallback", {Callbacks});
  CHECK(m.call("sceGeListEnQueue", {ListA + 8, 0, callbacks, 0}) >> 31, 0);
  CHECK(m.call("sceGeListEnQueue", {ListA + 8, 0, callbacks, 0}), Kernel::ErrorBusy);
}

//In a suspending SIGNAL's callback, as pspautotests' gpu/signals/handlercalls recorded: built with SDK 0x01000010,
//the list reads as paused there, so breaking it off is BUSY, and its stall address moved on there is the list's alone
//(the GE goes on to where it was to stop, the list reading as drawing), till it's moved on again; built with
//0x06060010, the list reads as drawing in the callback, and the stall address moved there takes. (Breaking it off
//there with the newer SDK hung the PSP: not recorded.) Either way the callback can't have the list go on
//(sceGeContinue: ALREADY for the newer, as gpu/signals/suspend recorded; BUSY for the older, unmeasured) nor put a
//list ahead of it (INVALID_VALUE, unmeasured).
static auto geSuspendedCallbacks() -> void {
  constexpr u32 Id = KernelMachine::Results + 0x60, Seen = KernelMachine::Results + 0x70;
  for(u32 version : {0x0100'0010u, 0x0606'0010u}) for(bool breaking : {true, false}) {
    if(breaking && version > 0x0200'0010) continue;
    KernelMachine m;
    auto& memory = m.system.memory;
    Assembler callback{m, 0x0880'4000};  //the list's state, what breaking it off or moving its stall says, the state
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.li(t0, Id); callback.put(lw(a0, 0, t0)); callback.li(a1, 1); callback.call("sceGeListSync");
    callback.li(t0, Seen); callback.put(sw(v0, 0, t0));
    if(breaking) {
      callback.li(a0, 0); callback.li(a1, 0); callback.call("sceGeBreak");
    } else {
      callback.li(t0, Id); callback.put(lw(a0, 0, t0)); callback.li(a1, 0); callback.call("sceGeListUpdateStallAddr");
    }
    callback.li(t0, Seen); callback.put(sw(v0, 4, t0));
    callback.li(t0, Id); callback.put(lw(a0, 0, t0)); callback.li(a1, 1); callback.call("sceGeListSync");
    callback.li(t0, Seen); callback.put(sw(v0, 8, t0));
    callback.call("sceGeContinue");  //nor can it go on, the GE still on it,
    callback.li(t0, Seen); callback.put(sw(v0, 36, t0));
    callback.li(a0, ListB); callback.li(a1, 0); callback.li(a2, 0xffff'ffff); callback.li(a3, 0);
    callback.call("sceGeListEnQueueHead");  //nor have a list put ahead of it
    callback.li(t0, Seen); callback.put(sw(v0, 40, t0));
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.put(jr(ra));
    callback.put(nop);
    memory.write(4, Callbacks, 0x0880'4000);
    ListWriter list{memory, ListA};
    list.put(GE::AmbientColor, 1);
    list.put(GE::Signal, 0x01'0042);
    list.put(GE::End);
    list.put(GE::AmbientColor, 2);
    list.put(GE::Nop);
    list.put(GE::Nop);  //stalled here first, moving the stall address
    list.put(GE::AmbientColor, 3);
    list.put(GE::Finish, 0x11);
    list.put(GE::End);
    auto note = [&](Assembler& a, u32 slot) { a.li(t0, Seen + slot); a.put(sw(v0, 0, t0)); };
    auto sync = [&](Assembler& a, u32 slot) {
      a.li(t0, Id); a.put(lw(a0, 0, t0)); a.li(a1, 1); a.call("sceGeListSync"); note(a, slot);
    };
    Assembler main{m, 0x0880'1000};
    main.li(a0, version); main.call("sceKernelSetCompiledSdkVersion");
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.li(a0, ListA); main.li(a1, ListA); main.put(addu(a2, v0, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");  //stalled at its start, so the callback has its ID first
    main.li(t0, Id); main.put(sw(v0, 0, t0));
    main.put(addu(a0, v0, zero)); main.li(a1, breaking ? 0 : ListA + 20); main.call("sceGeListUpdateStallAddr");
    sync(main, 12);
    main.li(a0, 1); main.call("sceGeDrawSync"); note(main, 16);
    main.li(a0, GE::AmbientColor); main.call("sceGeGetCmd"); note(main, 20);
    if(breaking) main.call("sceGeContinue");
    else main.li(t0, Id), main.put(lw(a0, 0, t0)), main.li(a1, 0), main.call("sceGeListUpdateStallAddr");
    note(main, 24);
    sync(main, 28);
    main.li(a0, GE::AmbientColor); main.call("sceGeGetCmd"); note(main, 32);
    main.call("sceKernelExitGame");
    runMain(m, false);
    bool old = version <= 0x0200'0010;
    auto seen = [&](u32 slot) { return memory.read(4, Seen + slot); };
    CHECK(seen(0), old ? 4 : 2);
    CHECK(seen(4), breaking ? Kernel::ErrorBusy : 0);
    CHECK(seen(8), old ? 4 : 2);
    bool held = old && !breaking;  //at the stall address the GE had before the callback
    CHECK(seen(12), held ? 2 : 0);
    CHECK(seen(16), held ? 2 : 0);
    CHECK(seen(20), GE::AmbientColor << 24 | (held ? 2 : 3));
    CHECK(seen(24), old ? 0 : Kernel::ErrorAlready);
    CHECK(seen(28), 0);
    CHECK(seen(32), GE::AmbientColor << 24 | 3);
    CHECK(seen(36), old ? Kernel::ErrorBusy : Kernel::ErrorAlready);  //(the newer one's recorded: gpu/signals/suspend)
    CHECK(seen(40), Kernel::ErrorInvalidValue);
  }
}

//A call into the program may use system functions but not wait in one; a thread it wakes runs once it's over; and
//calls wait while the program holds interrupts off.
static auto geCallsAndThreads() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  constexpr u32 Flag = KernelMachine::Results + 0x20, DelayResult = KernelMachine::Results + 0x24;
  constexpr u32 WaiterTurn = KernelMachine::Results + 0x28, Before = KernelMachine::Results + 0x2c;
  constexpr u32 After = KernelMachine::Results + 0x30, Previous = KernelMachine::Results + 0x34;
  Assembler callback{m, 0x0880'4000};  //tries to wait, then sets the flag's bit 1, then notes its turn
  callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
  callback.li(a0, 1); callback.call("sceKernelDelayThread");
  callback.li(t0, DelayResult); callback.put(sw(v0, 0, t0));
  callback.li(t0, Flag); callback.put(lw(a0, 0, t0)); callback.li(a1, 1); callback.call("sceKernelSetEventFlag");
  callback.li(t1, Turn); callback.put(lw(t2, 0, t1)); callback.put(addiu(t2, t2, 1)); callback.put(sw(t2, 0, t1));
  callback.li(t0, Signaled + 16); callback.put(sw(t2, 0, t0));
  callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
  callback.put(jr(ra));
  callback.put(nop);
  memory.write(4, Callbacks, 0x0880'4000);
  Assembler waiter{m, 0x0880'3000};  //waits for bit 1, then notes its turn
  waiter.li(t0, Flag); waiter.put(lw(a0, 0, t0)); waiter.li(a1, 1); waiter.li(a2, 1); waiter.li(a3, 0); waiter.li(t0, 0);
  waiter.call("sceKernelWaitEventFlag");
  waiter.li(t1, Turn); waiter.put(lw(t2, 0, t1)); waiter.put(addiu(t2, t2, 1)); waiter.put(sw(t2, 0, t1));
  waiter.li(t0, WaiterTurn); waiter.put(sw(t2, 0, t0));
  waiter.li(a0, 0); waiter.call("sceKernelExitThread");
  ListWriter list{memory, ListA};
  list.put(GE::Signal, 0x02'0001);
  list.put(GE::End);
  list.put(GE::Finish);
  list.put(GE::End);
  Assembler main{m, 0x0880'1000};
  main.li(a0, m.string("flag")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0);
  main.call("sceKernelCreateEventFlag");
  main.li(t0, Flag); main.put(sw(v0, 0, t0));
  main.li(a0, m.string("waiter")); main.li(a1, 0x0880'3000); main.li(a2, 0x10); main.li(a3, 0x1000);
  main.li(t0, 0); main.li(t1, 0);
  main.call("sceKernelCreateThread");
  main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
  main.call("sceKernelStartThread");  //the waiter runs at once (its priority is higher) and waits
  main.li(a0, Callbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  main.call("sceKernelCpuSuspendIntr");
  main.li(t0, Previous); main.put(sw(v0, 0, t0));
  main.put(addu(s0, v0, zero));
  main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.li(t1, Turn); main.put(lw(t2, 0, t1)); main.li(t0, Before); main.put(sw(t2, 0, t0));  //held back: nobody ran
  main.put(addu(a0, s0, zero)); main.call("sceKernelCpuResumeIntr");
  main.li(t1, Turn); main.put(lw(t2, 0, t1)); main.li(t0, After); main.put(sw(t2, 0, t0));
  main.call("sceKernelExitGame");
  runMain(m, false);
  CHECK(memory.read(4, Previous), 1);
  CHECK(memory.read(4, Before), 0);
  CHECK(memory.read(4, DelayResult), Kernel::ErrorIllegalContext);
  CHECK(memory.read(4, Signaled + 16), 1);  //the callback first,
  CHECK(memory.read(4, WaiterTurn), 2);     //then the thread it woke,
  CHECK(memory.read(4, After), 2);          //then the program, back where it was
}

//Event flags, called directly: polling for all or any bits, clearing what was asked for or everything, the errors
//in the order the PSP checks them, the bits told on failing (not on an error), a zero timeout, and the flag's status.
static auto eventFlags() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  constexpr u32 Seen = KernelMachine::Results, Info = KernelMachine::Results + 0x40;
  constexpr u32 Timeout = KernelMachine::Results + 0x80;
  u32 flag = m.call("sceKernelCreateEventFlag", {m.string("bits"), 0, 0x5, 0});
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x4, 0, Seen}), 0);
  CHECK(memory.read(4, Seen), 0x5);
  memory.write(4, Seen, 0xdead'beef);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x6, 0, Seen}), Kernel::ErrorEventFlagCondition);  //not all of them
  CHECK(memory.read(4, Seen), 0x5);  //told the bits even so
  memory.write(4, Seen, 0xdead'beef);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x6, 4, Seen}), Kernel::ErrorIllegalMode);
  CHECK(m.call("sceKernelPollEventFlag", {0x1234, 0, 0, Seen}), Kernel::ErrorEventFlagPattern);  //before the flag
  CHECK(m.call("sceKernelPollEventFlag", {0x1234, 0, 4, Seen}), Kernel::ErrorIllegalMode);       //the mode first
  CHECK(memory.read(4, Seen), 0xdead'beef);  //errors tell nothing
  memory.write(4, Timeout, 0);
  CHECK(m.call("sceKernelWaitEventFlag", {flag, 0x8, 0, Seen, Timeout}), Kernel::ErrorWaitTimeout);  //at once
  CHECK(memory.read(4, Seen), 0xdead'beef);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x6, 1, Seen}), 0);  //any of them
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x1, 0x20, Seen}), 0);  //and clear bit 0
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x1, 0, Seen}), Kernel::ErrorEventFlagCondition);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x4, 0x10, Seen}), 0);  //and clear them all
  CHECK(memory.read(4, Seen), 0x4);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x4, 0, Seen}), Kernel::ErrorEventFlagCondition);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0x4, 0x30, Seen}), Kernel::ErrorIllegalMode);
  CHECK(m.call("sceKernelPollEventFlag", {flag, 0, 0, Seen}), Kernel::ErrorEventFlagPattern);
  CHECK(m.call("sceKernelSetEventFlag", {flag, 0x30}), 0);
  CHECK(m.call("sceKernelClearEventFlag", {flag, 0x10}), 0);  //keeps only bit 4
  memory.write(4, Info, 52);
  CHECK(m.call("sceKernelReferEventFlagStatus", {flag, Info}), 0);
  CHECK(memory.readString(Info + 4, 31) == "bits", true);
  CHECK(memory.read(4, Info + 40), 0x5);
  CHECK(memory.read(4, Info + 44), 0x10);
  CHECK(memory.read(4, Info + 48), 0);
  CHECK(m.call("sceKernelDeleteEventFlag", {flag}), 0);
  CHECK(m.call("sceKernelSetEventFlag", {flag, 1}), Kernel::ErrorUnknownEventFlag);
  CHECK(m.call("sceKernelCreateEventFlag", {m.string("bad"), 0x1000, 0, 0}), Kernel::ErrorIllegalAttribute);
}

//Threads waiting on a flag made for several: setting bits wakes each it satisfies, first come first served, and one
//that clears its bits leaves them unset for the next. On a flag for one, a second waiter (or poller) is refused; a
//wait that times out, or whose flag is deleted, is told the bits.
static auto eventFlagWaiting() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    constexpr u32 Flags = KernelMachine::Results + 0x20;  //the shared flag, the single one, the one to delete
    constexpr u32 First = KernelMachine::Results + 0x40, Second = KernelMachine::Results + 0x50;
    constexpr u32 Refused = KernelMachine::Results + 0x60, TimedOut = KernelMachine::Results + 0x64;
    constexpr u32 PollRefused = KernelMachine::Results + 0x68, TimedOutSeen = KernelMachine::Results + 0x6c;
    constexpr u32 Deleted = KernelMachine::Results + 0x90;
    //a waiter: waits on the shared flag for bits with mode, then notes its result, the bits it saw and its turn
    auto waiter = [&](u32 at, u32 bits, u32 mode, u32 slot) {
      Assembler w{m, at};
      w.put(addiu(sp, sp, -16));
      w.li(t0, Flags); w.put(lw(a0, 0, t0)); w.li(a1, bits); w.li(a2, mode); w.put(addu(a3, sp, zero)); w.li(t0, 0);
      w.call("sceKernelWaitEventFlag");
      w.li(t0, slot); w.put(sw(v0, 0, t0)); w.put(lw(t1, 0, sp)); w.put(sw(t1, 4, t0));
      w.li(t1, Turn); w.put(lw(t2, 0, t1)); w.put(addiu(t2, t2, 1)); w.put(sw(t2, 0, t1)); w.put(sw(t2, 8, t0));
      w.li(a0, 0); w.call("sceKernelExitThread");
    };
    waiter(0x0880'3000, 0x1, 0x20, First);   //bit 0, cleared once seen
    waiter(0x0880'3400, 0x3, 0x01, Second);  //bit 0 or 1
    Assembler single{m, 0x0880'3800};         //a second waiter on the flag for one, and a poller
    single.li(t0, Flags + 4); single.put(lw(a0, 0, t0)); single.li(a1, 1); single.li(a2, 0); single.li(a3, 0);
    single.li(t0, 0);
    single.call("sceKernelWaitEventFlag");
    single.li(t0, Refused); single.put(sw(v0, 0, t0));
    single.li(t0, Flags + 4); single.put(lw(a0, 0, t0)); single.li(a1, 1); single.li(a2, 0); single.li(a3, 0);
    single.call("sceKernelPollEventFlag");
    single.li(t0, PollRefused); single.put(sw(v0, 0, t0));
    single.li(a0, 0); single.call("sceKernelExitThread");
    Assembler doomed{m, 0x0880'3c00};         //waits on the flag main deletes
    doomed.li(t0, Deleted); doomed.li(t1, 0xdead'beef); doomed.put(sw(t1, 4, t0));
    doomed.li(t0, Flags + 8); doomed.put(lw(a0, 0, t0)); doomed.li(a1, 1); doomed.li(a2, 0);
    doomed.li(a3, Deleted + 4); doomed.li(t0, 0);
    doomed.call("sceKernelWaitEventFlag");
    doomed.li(t0, Deleted); doomed.put(sw(v0, 0, t0));
    doomed.li(a0, 0); doomed.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {
      main.li(a0, m.string("waiter")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    main.li(a0, m.string("shared")); main.li(a1, 0x200); main.li(a2, 0); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, Flags); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("single")); main.li(a1, 0); main.li(a2, 0x8); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, Flags + 4); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("doomed")); main.li(a1, 0); main.li(a2, 0x10); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, Flags + 8); main.put(sw(v0, 0, t0));
    start(0x0880'3000, 0x10);
    start(0x0880'3400, 0x10);
    main.li(t0, Flags); main.put(lw(a0, 0, t0)); main.li(a1, 1);
    main.call("sceKernelSetEventFlag");  //the first takes bit 0 and clears it: the second keeps waiting
    main.li(t0, Flags); main.put(lw(a0, 0, t0)); main.li(a1, 2);
    main.call("sceKernelSetEventFlag");  //now the second
    //on the flag for one: main waits with a timeout while another tries to wait too
    start(0x0880'3800, 0x30);            //lower priority: it tries once main waits
    main.li(t0, Flags + 4); main.put(lw(a0, 0, t0)); main.li(a1, 4); main.li(a2, 0); main.li(a3, TimedOutSeen);
    main.li(t1, KernelMachine::Results + 0x70); main.li(t2, 2000); main.put(sw(t2, 0, t1)); main.put(addu(t0, t1, zero));
    main.call("sceKernelWaitEventFlag");
    main.li(t0, TimedOut); main.put(sw(v0, 0, t0));
    start(0x0880'3c00, 0x10);            //waits at once
    main.li(t0, Flags + 8); main.put(lw(a0, 0, t0));
    main.call("sceKernelDeleteEventFlag");
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    CHECK(memory.read(4, First), 0);
    CHECK(memory.read(4, First + 4), 0x1);  //it saw bit 0
    CHECK(memory.read(4, First + 8), 1);
    CHECK(memory.read(4, Second), 0);
    CHECK(memory.read(4, Second + 4), 0x2);  //bit 0 was gone by its turn
    CHECK(memory.read(4, Second + 8), 2);
    CHECK(memory.read(4, Refused), Kernel::ErrorEventFlagMulti);
    CHECK(memory.read(4, PollRefused), Kernel::ErrorEventFlagMulti);
    CHECK(memory.read(4, TimedOut), Kernel::ErrorWaitTimeout);
    CHECK(memory.read(4, TimedOutSeen), 0x8);  //the bits as they were when it gave up
    CHECK(memory.read(4, Deleted), Kernel::ErrorWaitDeleted);
    CHECK(memory.read(4, Deleted + 4), 0x10);
  }
}

//What the screen shows: the frame buffer the program set, in each pixel format, widened to 8888.
static auto displayPicture() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  std::vector<u32> pixels;
  m.kernel.picture(pixels);
  CHECK(pixels.size(), 480 * 272);
  CHECK(pixels[0], 0xff00'0000);  //no frame buffer yet: black
  m.call("sceDisplaySetFrameBuf", {VRAM + 0x1000, 512, 3, 0});
  memory.write(4, VRAM + 0x1000 + (2 * 512 + 3) * 4, 0x0011'2233);
  m.kernel.picture(pixels);
  CHECK(pixels[2 * 480 + 3], 0xff11'2233);
  m.call("sceDisplaySetFrameBuf", {VRAM, 512, 0, 0});
  memory.write(2, VRAM + (1 * 512 + 1) * 2, 0xf81f);  //5650: red and blue at full
  memory.write(2, VRAM + (1 * 512 + 2) * 2, 0x0400);  //green's top bit of six: 32 of 63
  m.kernel.picture(pixels);
  CHECK(pixels[1 * 480 + 1], 0xffff'00ff);
  CHECK(pixels[1 * 480 + 2], 0xff00'8200);
  m.call("sceDisplaySetFrameBuf", {VRAM, 512, 2, 0});
  memory.write(2, VRAM, 0x0f21);  //4444: alpha ignored
  m.kernel.picture(pixels);
  CHECK(pixels[0], 0xffff'2211);
  //through VRAM's second copy, whose rows aren't side by side in the host's memory (it rearranges each 16 KiB), a
  //pixel at a time, as that copy has them
  m.call("sceDisplaySetFrameBuf", {VRAM + Memory::VRAMSize, 512, 3, 0});
  memory.write(4, VRAM + Memory::VRAMSize + (5 * 512 + 7) * 4, 0x0044'5566);
  m.kernel.picture(pixels);
  CHECK(pixels[5 * 480 + 7], 0xff44'5566);

  //The screen's one mode and size; any other is refused, and the picture stays 480x272.
  CHECK(m.call("sceDisplaySetMode", {0, 480, 272}), 0);
  CHECK(m.call("sceDisplaySetMode", {1, 480, 272}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceDisplaySetMode", {0, 65536, 65536}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceDisplaySetMode", {0, 240, 136}), Kernel::ErrorInvalidSize);
  m.kernel.picture(pixels);
  CHECK(pixels.size(), 480 * 272);
}

//tools/psp-test-programs' GU program: pspsdk's GU library clears, signals, calls a list, finishes and copies.
static auto guProgram() -> void {
  auto program = testProgram("gu.elf");
  if(program.empty()) return;
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.system.recompiler.enabled = recompile;
    std::string error;
    CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/GU/EBOOT.PBP", error), true);
    for(u32 frame = 0; frame < 60 && !m.kernel.exited; frame++) m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.exited, true);
    std::string expected = "signal 7, finish 42\ncleared 00336699, called 0000ff00\ncopied 00000000 1f1f1f1f\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  output: [%s]\n", m.output.c_str());
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//pspsdk's GU sample "copy": it clears its frame buffer and depth buffer, then over and over copies a picture (each
//pixel x*y) from main memory into the frame buffer it draws to, swaps buffers, and prints the frame rate over the
//corner. It never waits, so it's run only until the screen has swapped four times: both buffers copied and shown.
static auto copySample() -> void {
  auto program = testProgram("copy.elf");
  if(program.empty()) return;
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.system.recompiler.enabled = recompile;
    std::string error;
    CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/COPY/EBOOT.PBP", error), true);
    u32 swaps = 0, shown = 0;
    for(u32 slice = 0; slice < 500 && swaps < 4 && !m.kernel.exited; slice++) {
      m.kernel.run(100'000);
      if(m.kernel.presentedFrameBuffer() != shown) swaps++, shown = m.kernel.presentedFrameBuffer();
    }
    CHECK(swaps, 4);
    CHECK(m.kernel.exited, false);  //it runs until the player quits
    auto& memory = m.system.memory;
    bool copied = true;
    for(u32 buffer : {0x0u, 0x8'8000u}) {  //both frame buffers, below the frame rate's line of text
      for(u32 y = 8; y < 272; y += 7) {
        for(u32 x = 0; x < 480; x += 5) copied &= memory.read(4, VRAM + buffer + (y * 512 + x) * 4) == x * y;
      }
    }
    CHECK(copied, true);
    CHECK(memory.read(2, VRAM + 0x11'0000 + (100 * 512 + 100) * 2), 0xffff);  //the depth buffer, cleared
    std::vector<u32> pixels;
    m.kernel.picture(pixels);
    CHECK(pixels[100 * 480 + 7], 0xff00'0000 | (700 & 0xff) | (700 >> 8 & 0xff) << 8);  //700 = 7 * 100
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//Display lists that never end, and no thread to run: one that JUMPs back to its own start; one that jumps back by a
//SIGNAL (kind 0x10) after every command; one that SYNCs (SIGNAL 0x08) and FINISHes over and over; one that draws a
//dot and jumps back. Each stops the GE after a million commands, however often it stops on the way, and the frame
//still ends, the GE going on as time passes (Kernel::idle()) rather than holding the clock still; each vertical blank
//gives it a million more. With a thread that wakes every 10 microseconds, giving the GE a go each time, a frame's
//goes still share its million commands. A list that SIGNALs (0x02) for a callback
//over and over has the GE wait whenever a callback still waits its turn, or runs: one that moves the stall address on
//finds the list still running at the next SIGNAL, which waits for it.
static auto geEndless() -> void {
  //should a list never let go, the tests stop here, saying why, rather than run on forever
  signal(SIGALRM, [](int) {
    const char text[] = "FAIL ge endless list: a display list never let go\n";
    if(write(2, text, sizeof(text) - 1)) {}
    _exit(1);
  });
  alarm(240);
  for(u32 kind : {0u, 1u, 2u, 3u}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    ListWriter list{memory, ListA};
    if(kind == 0) list.to(GE::Jump, ListA);
    if(kind == 1) list.put(GE::Signal, 0x10'0000 | ListA >> 16), list.put(GE::End, ListA & 0xffff);
    if(kind == 2) list.put(GE::Signal, 0x08'0000), list.put(GE::End), list.put(GE::Finish), list.put(GE::End),
                  list.to(GE::Jump, ListA);
    if(kind == 3) {  //a dot, in clear mode, into a small frame buffer: over and over
      list.put(GE::FrameBufferPointer, 0);
      list.put(GE::FrameBufferWidth, 16);
      list.put(GE::FrameBufferPixelFormat, 3);
      list.put(GE::Scissor1, 0);
      list.put(GE::Scissor2, 15 << 10 | 15);
      list.put(GE::Region2, 1023 << 10 | 1023);
      list.put(GE::ClearMode, 0x101);
      list.put(GE::VertexType, 0x80'011c);
      u32 loop = list.address;
      list.to(GE::VertexAddress, Vertices);
      list.put(GE::Primitive, GE::Sprites << 16 | 2);
      list.to(GE::Jump, loop);
      memory.write(4, Vertices, 0xff00'00ff);
      memory.write(2, Vertices + 4, 0); memory.write(2, Vertices + 6, 0); memory.write(2, Vertices + 8, 0);
      memory.write(4, Vertices + 12, 0xff00'00ff);
      memory.write(2, Vertices + 16, 1); memory.write(2, Vertices + 18, 1); memory.write(2, Vertices + 20, 0);
    }
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    CHECK(m.kernel.geBusy, true);
    u64 start = m.kernel.cycles, before = m.kernel.geCommands;
    m.kernel.run(2 * Kernel::VblankCycles);
    CHECK(m.kernel.cycles - start >= 2 * Kernel::VblankCycles, true);
    CHECK(m.kernel.geBusy, true);  //and the list goes on,
    u64 ran = m.kernel.geCommands - before;
    CHECK(ran >= Kernel::GeBudget && ran <= 2 * Kernel::GeBudget, true);  //a million commands a vertical blank
  }
  {
    KernelMachine m;
    m.system.recompiler.enabled = false;
    m.system.power(0x0880'1000);
    Assembler sleepy{m, 0x0880'1000};
    sleepy.li(a0, 10); sleepy.call("sceKernelDelayThread");
    sleepy.put(j(0x0880'1000)); sleepy.put(nop);
    ListWriter list{m.system.memory, ListA};
    list.to(GE::Jump, ListA);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    s32 uid = m.kernel.createThread("sleepy", 0x0880'1000, 0x20, 0x1000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    u64 before = m.kernel.geCommands;
    m.kernel.run(3 * Kernel::VblankCycles);
    CHECK(m.kernel.vblanks >= 3, true);
    u64 ran = m.kernel.geCommands - before;
    CHECK(ran >= 2 * Kernel::GeBudget && ran <= 4 * Kernel::GeBudget, true);  //each frame's million, used
  }
  {
    KernelMachine m;
    auto& memory = m.system.memory;
    constexpr u32 Looks = KernelMachine::Results + 0x40;  //what each callback saw of the list (sceGeListSync's word)
    Assembler look{m, 0x0880'4300};  //moves the stall address to the list's end, then looks at the list
    look.put(addiu(sp, sp, -16)); look.put(sw(ra, 12, sp)); look.put(sw(a0, 8, sp));
    look.li(a0, Kernel::GeListIDs); look.li(a1, ListB + 24); look.call("sceGeListUpdateStallAddr");
    look.li(a0, Kernel::GeListIDs); look.li(a1, 1); look.call("sceGeListSync");
    look.put(lw(t1, 8, sp)); look.put(sll(t1, t1, 2)); look.li(t0, Looks); look.put(addu(t0, t0, t1));
    look.put(sw(v0, 0, t0));
    look.put(lw(ra, 12, sp)); look.put(addiu(sp, sp, 16));
    look.put(jr(ra)); look.put(nop);
    for(u32 n = 0; n < 4; n++) memory.write(4, Callbacks + 4 * n, n ? 0 : 0x0880'4300);
    u32 id = m.call("sceGeSetCallback", {Callbacks});
    ListWriter list{memory, ListB};
    list.put(GE::Signal, 0x02'0001); list.put(GE::End);
    list.put(GE::Signal, 0x02'0002); list.put(GE::End);
    list.put(GE::Finish); list.put(GE::End);
    m.call("sceGeListEnQueue", {ListB, ListB + 8, id, 0});  //stalled at the second SIGNAL
    m.kernel.run(100'000);
    CHECK(memory.read(4, Looks + 4), 2);  //the first callback found the list still running: the GE waited for it
    CHECK(m.call("sceGeListSync", {Kernel::GeListIDs, 1}), 0);  //and once both had run, it finished
  }
  KernelMachine m;
  auto& memory = m.system.memory;
  constexpr u32 Function = 0x0880'4200;
  memory.write(4, Function, jr(ra));
  memory.write(4, Function + 4, 0);  //nop
  for(u32 n = 0; n < 4; n++) memory.write(4, Callbacks + 4 * n, n ? 0 : Function);
  u32 id = m.call("sceGeSetCallback", {Callbacks});
  ListWriter list{memory, ListA};
  list.put(GE::Signal, 0x02'0001);
  list.put(GE::End);
  list.to(GE::Jump, ListA);
  m.call("sceGeListEnQueue", {ListA, 0, id, 0});
  bool bounded = m.kernel.calls.size() <= 2 && m.kernel.geSuspended;
  CHECK(bounded, true);
  if(bounded) {  //(were they not, running on would pile up more with every go the GE has)
    m.kernel.run(100'000);
    CHECK(m.kernel.calls.size() <= 2, true);
  }
  alarm(0);
}

//Drawing on several threads (ge/threads.cpp). A frame's worth of primitives: sprites and triangles, flat, blended
//across, textured from RAM and blended over what's there, depth-tested; lines and a strip; a DXT texture; a bounding
//box skipping a sprite and one not; a picture drawn off the screen, then drawn
//with as a texture (render to texture); a block transfer of drawn pixels; a palette loaded from pixels just drawn; a
//sprite drawing over its own texture (drawn at once, in order); and a 16-bit frame buffer. It comes out the same,
//every byte of VRAM, with 1, 2, 4 and 8 threads, with batches shared out however small. Stopped at its stall address
//part way, what came before is drawn (what the CPU reads then); and a state saved there carries on, in another
//machine, to the same picture. Its last primitives still being drawn as the CPU runs on, the CPU's compiled stores
//and loads find them drawn.
static auto geThreads() -> void {
  Unthreaded unthreaded;  //(the batches looked at directly: system.hpp)
  constexpr u32 Texture = 0x0898'0000, Offscreen = 0x15'4000;  //(the off-screen picture: VRAM offset)
  u32 stall = 0;
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 64 * 64; n++) memory.write(4, Texture + n * 4, 0x8000'0000 | (n * 0x0305'0709 & 0xff'ffff));
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    auto target = [&](u32 address, u32 width, u32 format) {
      list.put(GE::FrameBufferPointer, address), list.put(GE::FrameBufferWidth, width);
      list.put(GE::FrameBufferPixelFormat, format);
    };
    target(0, 512, 3);
    list.put(GE::DepthBufferPointer, 0x11'0000), list.put(GE::DepthBufferWidth, 512);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    list.put(GE::ClearMode, 1 | 7 << 8);
    put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff20'3040, 480, 272, 0}});
    list.put(GE::ClearMode, 0);
    list.put(GE::DepthTestEnable, 1), list.put(GE::DepthTest, 7);  //greater or equal
    for(u32 n = 0; n < 24; n++) {
      float x = n * 19 % 440, y = n * 37 % 240, z = n * 2000.0f;
      put(GE::Triangles, {{0, 0, 0xff00'00ff + n * 0x0a00, x, y, z}, {0, 0, 0xff00'ff00, x + 60, y + 9, z},
                          {0, 0, 0xffff'0000, x + 21, y + 40, z}});
    }
    list.put(GE::TextureMappingEnable, 1), list.to(GE::TextureAddress0, Texture);
    list.put(GE::TextureBufferWidth0, (Texture >> 24) << 16 | 64), list.put(GE::TextureSize0, 6 << 8 | 6);
    list.put(GE::TextureFormat, 3), list.put(GE::TextureFunction, 0 | 1 << 8);
    list.put(GE::TextureFilter, 1 | 1 << 8);
    list.put(GE::AlphaBlendEnable, 1), list.put(GE::BlendMode, 2 | 3 << 4);
    for(u32 n = 0; n < 12; n++) {
      float x = n * 53 % 400, y = n * 29 % 200;
      put(GE::Sprites, {{0, 0, 0, x, y, 30000}, {64, 64, 0xc0ff'ffff, x + 77, y + 51, 30000}});
    }
    //lines (shallow and steep, a strip, colors and the texture along them), a DXT5 texture, and boxes: one out of
    //sight skipping a sprite, one in sight not
    for(u32 n = 0; n < 16; n++) {
      float x = n * 23 % 400, y = n * 17 % 220;
      put(GE::Lines, {{0, 0, 0xff00'ff00, x, y, 0}, {64, 64, 0xffff'00ff, x + 70.5f, y + 13.25f, 0},
                      {0, 0, 0xff00'00ff, x + 3.75f, y + 40.5f, 0}, {64, 0, 0xffff'ffff, x + 9.0625f, y + 2, 0}});
    }
    put(GE::LineStrip, {{0, 0, 0xffff'ffff, 10, 250, 0}, {0, 0, 0xff00'0000, 470, 10, 0},
                        {0, 0, 0xff80'8080, 20.5f, 20.5f, 0}});
    list.put(GE::TextureFormat, 10);
    put(GE::Sprites, {{0, 0, 0, 200, 150, 30000}, {64, 64, 0xffff'ffff, 264, 214, 30000}});
    list.put(GE::TextureFormat, 3);
    for(float at : {-100.0f, 300.0f}) {
      list.to(GE::VertexAddress, vertex);
      for(float value : {0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      memory.write(4, vertex, 0), vertex += 4;
      for(float value : {at, at, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      list.put(GE::BoundingBox, 1);
      list.to(GE::ConditionalJump, list.address + 2 * 4 + 3 * 4);  //past the sprite: its BASE, address and PRIM
      put(GE::Sprites, {{0, 0, 0, 380 + at / 10, 200, 30000}, {64, 64, 0xff40'80ff, 470, 260, 30000}});
    }
    stall = list.address;
    //drawn into, then drawn with, in the same render target: the texture decoded once the first is drawn
    list.put(GE::TextureMappingEnable, 0), list.put(GE::AlphaBlendEnable, 0), list.put(GE::DepthTestEnable, 0);
    put(GE::Sprites, {{0, 0, 0, 400, 200, 0}, {0, 0, 0xff11'7799, 464, 264, 0}});
    list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, (200 * 512 + 400) * 4);
    list.put(GE::TextureBufferWidth0, 0x04 << 16 | 512), list.put(GE::TextureFunction, 3);
    put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {64, 64, 0, 64, 64, 0}});
    //a picture off the screen, then drawn with
    target(Offscreen, 64, 3);
    list.put(GE::TextureMappingEnable, 0);
    for(u32 n = 0; n < 8; n++) {
      put(GE::Sprites, {{0, 0, 0, n * 8.0f, 0, 0}, {0, 0, 0xff00'0000 + n * 0x20'2020, n * 8.0f + 8, 64, 0}});
    }
    target(0, 512, 3);
    list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, Offscreen);
    list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
    put(GE::Sprites, {{0, 0, 0, 100, 100, 0}, {64, 64, 0, 228, 164, 0}});
    //that copied, before it's drawn over any more
    list.put(GE::TransferSource, 0), list.put(GE::TransferSourceWidth, 0x04 << 16 | 512);
    list.put(GE::TransferSourcePosition, 100 | 100 << 10);
    list.put(GE::TransferDestination, 0x1d'0000), list.put(GE::TransferDestinationWidth, 0x04 << 16 | 512);
    list.put(GE::TransferSize, 63 | 31 << 10), list.put(GE::TransferStart, 1);
    //a palette loaded from pixels just drawn
    list.put(GE::TextureMappingEnable, 0);
    put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff44'88cc, 16, 1, 0}});
    list.put(GE::ClutAddress, 0), list.put(GE::ClutAddressUpper, 0x04 << 16), list.put(GE::ClutLoad, 2);
    list.put(GE::ClutFormat, 3 | 0x0f << 8);
    list.put(GE::TextureMappingEnable, 1);
    list.to(GE::TextureAddress0, Texture), list.put(GE::TextureBufferWidth0, (Texture >> 24) << 16 | 64);
    list.put(GE::TextureFormat, 5);
    put(GE::Sprites, {{0, 0, 0, 300, 20, 0}, {64, 64, 0, 364, 84, 0}});
    //a sprite drawing over its own texture: the frame buffer
    list.put(GE::TextureFormat, 3), list.put(GE::TextureAddress0, 0);
    list.put(GE::TextureBufferWidth0, 0x04 << 16 | 512);
    list.put(GE::TextureSize0, 9 << 8 | 9);
    put(GE::Sprites, {{10, 10, 0, 20, 30, 0}, {110, 60, 0, 120, 80, 0}});
    //two render targets a row apart: one's row 8 is the other's 7
    list.put(GE::TextureMappingEnable, 0);
    put(GE::Sprites, {{0, 0, 0, 300, 0, 0}, {0, 0, 0xff00'ff00, 340, 20, 0}});
    target(0x800, 512, 3);
    put(GE::Sprites, {{0, 0, 0, 310, 0, 0}, {0, 0, 0xffff'00ff, 350, 20, 0}});
    //a 16-bit frame buffer
    target(0x18'0000, 512, 1);
    for(u32 n = 0; n < 6; n++) {
      put(GE::Triangles, {{0, 0, 0xffff'ffff, n * 70.0f, 0, 0}, {0, 0, 0x8000'00ff, n * 70.0f + 70, 90, 0},
                          {0, 0, 0x0000'ff00, n * 70.0f, 120, 0}});
    }
    //and much more of it, taking a while to draw, the bottom rows last
    list.put(GE::TextureMappingEnable, 1), list.to(GE::TextureAddress0, Texture);
    list.put(GE::TextureBufferWidth0, (Texture >> 24) << 16 | 64), list.put(GE::TextureFormat, 3);
    list.put(GE::TextureSize0, 6 << 8 | 6), list.put(GE::TextureFunction, 0 | 1 << 8);
    for(u32 n = 0; n < 6; n++) put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {64 + n * 3.0f, 64, 0x80ff'ffff, 480, 272, 0}});
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](u32 threads, u64 shared, bool stalled) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = shared;
    build(m.system.memory);
    m.call("sceGeListEnQueue", {ListA, stalled ? stall : 0, 0xffff'ffff, 0});
    m.system.ge.settle();  //(its last primitives may still be being drawn: VRAM itself is looked at here)
    return m.system.memory.vram;
  };
  auto whole = drawn(1, 8192, false), half = drawn(1, 8192, true);
  CHECK(whole != half, true);
  for(u32 threads : {2u, 4u, 8u}) {
    for(u64 shared : {u64(0), u64(8192)}) {
      CHECK(drawn(threads, shared, false) == whole, true);
      CHECK(drawn(threads, shared, true) == half, true);
    }
  }
  //stopped part way: the CPU reads what's drawn; a state saved there, loaded into another machine, carries on
  KernelMachine m;
  m.system.ge.setThreads(4);
  m.system.ge.drawing.shared = 0;
  build(m.system.memory);
  u32 id = m.call("sceGeListEnQueue", {ListA, stall, 0xffff'ffff, 0});
  u32 at = (100 * 512 + 50) * 4, pixel = half[at] | half[at + 1] << 8 | half[at + 2] << 16 | u32(half[at + 3]) << 24;
  CHECK(m.system.memory.read(4, VRAM + at), pixel);
  serializer saved;
  m.system.memory.serialize(saved), m.system.serialize(saved);
  m.system.ge.serialize(saved), m.kernel.serialize(saved);
  KernelMachine n;
  n.system.ge.setThreads(8);
  n.system.ge.drawing.shared = 0;
  serializer loading{saved.data(), saved.size()};
  n.system.memory.serialize(loading), n.system.serialize(loading);
  CHECK(n.system.ge.serialize(loading), true);
  CHECK(n.kernel.serialize(loading), true);
  CHECK(n.system.memory.vram == half, true);
  CHECK(n.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  n.system.ge.settle();
  CHECK(n.system.memory.vram == whole, true);

  //the CPU's compiled code just after the list, its last primitives still being drawn: a store into a pixel they
  //draw lands after them, and a load sees them drawn
  for(bool recompile : {true, false}) {
    KernelMachine c;
    c.system.ge.setThreads(4);
    c.system.ge.drawing.shared = 0;
    build(c.system.memory);
    c.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    CHECK(c.system.ge.launched(), true);
    u32 stored = VRAM + 0x18'0000 + (260 * 512 + 400) * 2, loaded = VRAM + 0x18'0000 + (250 * 512 + 300) * 2;
    c.system.runProgram(0x0890'0000, {lui(t0, stored >> 16), ori(t0, t0, stored & 0xffff), ori(t1, zero, 0x1234),
                                      sh(t1, 0, t0), lui(t0, loaded >> 16), ori(t0, t0, loaded & 0xffff),
                                      lhu(t2, 0, t0), lui(t3, 0x0892), sw(t2, 0, t3)}, recompile);
    CHECK(c.system.memory.read(2, stored), 0x1234u);
    u32 at = loaded - VRAM;
    CHECK(c.system.memory.read(4, 0x0892'0000), u32(whole[at] | whole[at + 1] << 8));
    //(what draws over those pixels drawn, and anything before it; what's after it may go on being drawn)
    CHECK(c.system.ge.drawnOver(stored - VRAM, stored - VRAM + 1) || c.system.ge.drawnOver(at, at + 1), false);
  }
}

//A load through VRAM's fourth copy (the depth buffer as the GE has it) of depths a batch still draws, and through its
//second copy: the batch is waited for, as for a load through the first copy (only a hardware renderer's depth isn't
//memory's to wait for: Memory::vramDrawnOver's depthLoad). The depths found are those drawing it all on one thread
//writes, compiled loads and the interpreter's alike. The drawing threads are held till the batch is seen still being
//drawn (on a busy host they could draw it all first, and the loads would wait for nothing), then let go as the loads
//come, or held through them, so that the loads must wait for the batch, drawing it themselves.
static auto geDepthViewLoads() -> void {
  Unthreaded unthreaded;  //(the drawing threads held and looked at directly: system.hpp)
  constexpr u32 Depth = 0x11'0000;  //(VRAM offset: 512 depths a row)
  auto build = [&](Memory& memory) {
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, GE::Sprites << 16 | vertices.size());
    };
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 512), list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::DepthBufferPointer, Depth), list.put(GE::DepthBufferWidth, 512);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    list.put(GE::DepthTestEnable, 1), list.put(GE::DepthTest, 1);  //(always: each sprite's depth written)
    for(u32 n = 0; n < 30; n++) {
      float x = n * 37 % 200, y = n * 23 % 100;
      put({{0, 0, 0, x, y, 0}, {0, 0, 0xff00'0000 + n * 0x10'2030, x + 280, y + 172, 1000.0f + n * 2000}});
    }
    list.put(GE::Finish), list.put(GE::End);
  };
  u32 middle = Depth + (136 * 512 + 240) * 2, corner = Depth + (60 * 512 + 300) * 2;
  auto loads = [&](u32 threads, bool recompile, bool heldThrough) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    auto hold = [&](bool held) {
      {
        std::lock_guard lock(m.system.ge.drawing.mutex);
        m.system.ge.drawing.held = held;
      }
      m.system.ge.drawing.wake.notify_all();
    };
    hold(true);
    build(m.system.memory);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    if(threads > 1) {  //(a batch drawing the depths still being drawn: else the loads wait for nothing)
      bool drawing = false;
      for(auto& batch : m.system.ge.drawing.batches) {
        drawing |= GE::unfinished(batch) && batch.pending[Memory::vramOffset(3, middle) >> 12];
      }
      CHECK(drawing, true);
    }
    if(!heldThrough) hold(false);
    u32 fourth = 0x0460'0000 + middle, second = 0x0420'0000 + Memory::vramSeen(1, Memory::vramOffset(3, corner));
    m.system.runProgram(0x0890'0000, {lui(t0, fourth >> 16), ori(t0, t0, fourth & 0xffff), lhu(t1, 0, t0),
                                      lui(t2, second >> 16), ori(t2, t2, second & 0xffff), lhu(t3, 0, t2),
                                      lui(t4, 0x0892), sw(t1, 0, t4), sw(t3, 4, t4)}, recompile);
    hold(false);
    m.system.ge.settle();
    return std::array<u32, 2>{m.system.memory.read(4, 0x0892'0000), m.system.memory.read(4, 0x0892'0004)};
  };
  auto one = loads(1, false, false);
  CHECK(one[0], 59000u);  //(the last sprite over each, its depth written as it was drawn)
  CHECK(one[1], 57000u);
  for(u32 threads : {2u, 4u}) {
    for(bool recompile : {false, true}) {
      for(bool heldThrough : {false, true}) CHECK(loads(threads, recompile, heldThrough) == one, true);
    }
  }
}

//A picture drawn off the screen and then drawn with (render to texture), its decode left till its batch starts
//(ge/threads.cpp): till then the texture's pages stay busy for the CPU, as the pages a batch draws over do, and the
//CPU's store into the picture just after the list lands after the sprite drew with it, its load of the sprite's pixel
//finding it drawn and its load of the texel finding what it stored. It comes out as drawing it all before the CPU runs
//on does (one thread), with batches shared out however small; the sprite is drawn four pixels at a time, the texture
//decoded by then. Into a frame buffer narrower than itself (32 pixels) the sprite can't wait in a batch (defer()):
//it's drawn at once, after the batch before it, its texture (decoded for no batch) read from memory as it draws.
static auto geDeferredBusy() -> void {
  Unthreaded unthreaded;  //(the batches looked at directly: system.hpp)
  constexpr u32 Offscreen = 0x15'4000;  //(VRAM offset)
  auto build = [&](Memory& memory, u32 width) {
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    list.put(GE::FrameBufferPointer, Offscreen), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    for(u32 n = 0; n < 8; n++) {
      put(GE::Sprites, {{0, 0, 0, 0, n * 8.0f, 0}, {0, 0, 0xff00'0000 + n * 0x10'2030, 64, n * 8.0f + 8, 0}});
    }
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, width);
    list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, Offscreen);
    list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64), list.put(GE::TextureSize0, 6 << 8 | 6);
    list.put(GE::TextureFormat, 3), list.put(GE::TextureFunction, 3);
    put(GE::Sprites, {{0, 0, 0, 100, 100, 0}, {64, 64, 0, 164, 164, 0}});
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](u32 width, u32 threads, bool recompile) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    build(m.system.memory, width);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    if(threads > 1 && width == 512) {  //(both batches launched still, the second's decode deferred, drawn in fours)
      bool deferred = false;
      for(auto& batch : m.system.ge.drawing.batches) {
        if(!batch.launched) continue;
        for(u32 page = 0; page < GE::VRAMPages; page++) {
          if(!batch.reads[page]) continue;
          deferred = true;
          CHECK(m.system.memory.vramPageBusy(page), true);
        }
        if(batch.reads.any()) for(auto& job : batch.jobs) CHECK(job.fours, true);
      }
      CHECK(deferred, true);
    }
    //the store into the picture; then loads of the sprite's pixel and of the stored texel, into RAM
    u32 stored = VRAM + Offscreen + (10 * 64 + 10) * 4, loaded = VRAM + (110 * width + 110) * 4;
    m.system.runProgram(0x0890'0000, {lui(t0, stored >> 16), ori(t0, t0, stored & 0xffff), lui(t1, 0x1234),
                                      ori(t1, t1, 0x5678), sw(t1, 0, t0), lui(t2, loaded >> 16),
                                      ori(t2, t2, loaded & 0xffff), lw(t3, 0, t2), lui(t4, 0x0892), sw(t3, 0, t4),
                                      lw(t5, 0, t0), sw(t5, 4, t4)}, recompile);
    CHECK(m.system.memory.read(4, 0x0892'0004), 0x1234'5678u);
    m.system.ge.settle();
    CHECK(m.system.memory.read(4, 0x0892'0000), m.system.memory.read(4, loaded));
    return m.system.memory.vram;
  };
  for(u32 width : {512u, 32u}) {
    auto whole = drawn(width, 1, false);
    CHECK(whole[(110 * width + 110) * 4], u8(0x30));  //(the sprite took its texel 10, 10 as drawn, not as stored)
    for(u32 threads : {2u, 4u, 8u}) {
      for(bool recompile : {false, true}) CHECK(drawn(width, threads, recompile) == whole, true);
    }
  }
}

//3D triangles drawing with a texture the batch before them still draws (render to texture), each decoded as its batch
//starts, of the texels its jobs can take only (ge/raster.cpp's texelReach()): a 64x64 picture of texels each its own
//drawn again before each quad (each time a little other), so that each quad's decode is deferred, then 3D quads (one
//w, one q) taking parts of it, nearest and filtered, held at the edges past them, magnified and shrunk, a quad
//repeating round an edge, one in perspective, one with its own CLUT32 view of the picture, a triangle by itself, and
//2D sprites (spriteReach()), two of them in one PRIM far apart; then the same with the four-pixel path off (whose fetch() checks each texel lies in the
//rectangle round what the copy decoded). It comes out as on one thread, where nothing is deferred. And Looks sharing a
//copy (ensureDecoded()) have it decoded over all of their texels.
static auto geDeferredReach() -> void {
  Unthreaded unthreaded;  //(the batches looked at directly: system.hpp)
  constexpr u32 Offscreen = 0x15'4000, Picture = Image, Palette = Image + 0x4000;  //(Offscreen: a VRAM offset)
  struct Quad { float x0, y0, x1, y1, u0, v0, u1, v1, w1; u32 filter, wrap, format; };
  constexpr Quad Quads[] = {
    {0, 0, 16, 16, 5, 7, 21, 23, 1, 0, 0x101, 3},             //nearest, every texel once
    {16, 0, 36, 20, 40.3f, 1.7f, 60.1f, 20.9f, 1, 0x101, 0x101, 3},  //filtered
    {36, 0, 52, 20, -6, 50, 10, 70, 1, 0x101, 0x101, 3},        //held at the edges, past both
    {52, 0, 64, 24, 50, 30, 74, 46, 1, 0x101, 0, 3},            //repeating round the right edge: every texel
    {0, 20, 30, 50, 20.5f, 30.25f, 28.75f, 40, 1, 0x101, 0, 3},  //magnified
    {30, 24, 40, 34, 2, 40, 62, 62, 1, 0x101, 0x101, 3},         //shrunk
    {40, 24, 64, 48, 8, 8, 40, 40, 3, 0x101, 0x101, 3},          //in perspective (w 1 to 3)
    {0, 50, 32, 64, 33, 3, 49.5f, 17, 1, 0, 0x101, 7},           //the picture's words as a CLUT32's indices
  };
  auto build = [&](Memory& memory, GE& ge) {
    std::mt19937 random{20261011};
    for(u32 n = 0; n < 64 * 64; n++) memory.write(4, Picture + n * 4, u32(random()) | 0xff00'0000);
    for(u32 n = 0; n < 256; n++) memory.write(4, Palette + n * 4, u32(random()));
    //3D: every matrix the identity, a model's x and y the screen's pixels (w 1, unless a vertex's z makes it more)
    for(u32 n = 0; n < 16; n++) ge.projection[n] = 0;
    for(u32 n : {0, 4, 8}) ge.world[n] = ge.view[n] = 0x3f'8000;
    for(u32 n : {0, 5}) ge.projection[n] = 0x3f'8000;
    ge.projection[11] = 0xbf'8000;  //w = -z, and z 0
    ge.textureMatrix[0] = ge.textureMatrix[4] = 0x3f'8000, ge.textureMatrix[11] = 0xbf'8000;  //s = u, t = v, q = -1
    auto& c = ge.commands;
    auto f24 = [](float value) { return std::bit_cast<u32>(value) >> 8; };
    c[GE::ViewportXScale] = c[GE::ViewportYScale] = c[GE::ViewportZScale] = f24(1);
    c[GE::ViewportXCenter] = c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(100);
    c[GE::OffsetX] = c[GE::OffsetY] = 2048 << 4;
    c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
    c[GE::MaxZ] = 0xffff;
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, u32 type, std::initializer_list<V> vertices) {
      list.put(GE::VertexType, type);
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    //the picture drawn into Offscreen again from RAM, in the batch being filled, each time a little other (modulated by
    //a color of its own), so that a copy decoded into an earlier one's room shows a texel left out
    u32 passes = 0;
    auto redraw = [&] {
      list.put(GE::FrameBufferPointer, Offscreen), list.put(GE::FrameBufferWidth, 64);
      list.put(GE::TextureAddress0, Picture & 0xff'ffff), list.put(GE::TextureBufferWidth0, (Picture >> 24) << 16 | 64);
      list.put(GE::TextureFormat, 3), list.put(GE::TextureFilter, 0), list.put(GE::TextureWrap, 0x101);
      list.put(GE::TextureFunction, 0);
      u32 color = 0xff80'8080 | (++passes * 0x0d'1f29 & 0x7f'7f7f);
      put(GE::Sprites, 0x80'019f, {{0, 0, color, 0, 0, 0}, {64, 64, color, 64, 64, 0}});
      list.put(GE::TextureFunction, 3);
    };
    //(a 3D primitive may draw anywhere in the scissor rectangle, which a batch's rows mustn't run past: 64 wide)
    list.put(GE::Scissor2, 63 | 63 << 10), list.put(GE::Region2, 63 | 63 << 10);
    list.put(GE::ShadeMode, 1), list.put(GE::FrameBufferPixelFormat, 3), list.put(GE::TextureFunction, 3);
    list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureSize0, 6 << 8 | 6);
    list.to(GE::ClutAddress, Palette), list.put(GE::ClutAddressUpper, Palette >> 8 & 0xf'0000);
    list.put(GE::ClutFormat, 3 | 8 << 2 | 0xff << 8), list.put(GE::ClutLoad, 32);  //(32-bit entries, index bits 8-15)
    for(auto& q : Quads) {
      redraw();
      //then a quad taking part of it, into the picture at VRAM's start
      list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 64);
      list.put(GE::TextureAddress0, Offscreen), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
      list.put(GE::TextureFormat, q.format), list.put(GE::TextureFilter, q.filter), list.put(GE::TextureWrap, q.wrap);
      float z0 = -1, z1 = -q.w1;  //(w = -z: the right edge's w is w1, and its x and y as far apart, so it lands there)
      put(GE::TriangleStrip, 0x19f, {{q.u0 / 64, q.v0 / 64, 0xffff'ffff, q.x0, q.y0, z0},
                                    {q.u1 / 64, q.v0 / 64, 0xffff'ffff, q.x1 * q.w1, q.y0 * q.w1, z1},
                                    {q.u0 / 64, q.v1 / 64, 0xffff'ffff, q.x0, q.y1, z0},
                                    {q.u1 / 64, q.v1 / 64, 0xffff'ffff, q.x1 * q.w1, q.y1 * q.w1, z1}});
    }
    //and a triangle by itself, each corner with a coordinate the others don't reach
    redraw();
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::TextureAddress0, Offscreen), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
    list.put(GE::TextureFilter, 0x101);
    put(GE::Triangles, 0x19f, {{10.0f / 64, 41.0f / 64, 0xffff'ffff, 34, 49, -1},
                              {30.0f / 64, 44.5f / 64, 0xffff'ffff, 63, 52, -1},
                              {18.0f / 64, 60.0f / 64, 0xffff'ffff, 45, 63, -1}});
    //and a quad whose q is -1 at every corner (the texture matrix's, from u and v), whose pixels all hold their
    //coordinates: the texture's last texel each way
    redraw();
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::TextureAddress0, Offscreen), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
    list.put(GE::TextureFilter, 0), list.put(GE::TextureWrap, 0), list.put(GE::TextureMapMode, 1 | 1 << 8);
    put(GE::TriangleStrip, 0x19f, {{0.1f, 0.2f, 0xffff'ffff, 4, 52, -1}, {0.3f, 0.2f, 0xffff'ffff, 20, 52, -1},
                                   {0.1f, 0.4f, 0xffff'ffff, 4, 62, -1}, {0.3f, 0.4f, 0xffff'ffff, 20, 62, -1}});
    list.put(GE::TextureMapMode, 0);
    //and 2D sprites (ge/draw.cpp's spriteReach()), nearest, filtered, held past the top edge, and one turned
    struct Sprite { float x0, y0, u0, v0, x1, y1, u1, v1; u32 filter; };
    for(Sprite s : {Sprite{0, 0, 40, 10, 16, 32, 56, 42, 0}, Sprite{16, 32, 3.5f, 20.25f, 40, 48, 27.5f, 36.25f, 0x101},
                    Sprite{40, 0, 30, -8, 63, 20, 50, 12, 0x101}, Sprite{40, 63, 10, 50, 63, 40, 30, 60, 0}}) {
      redraw();
      list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 64);
      list.put(GE::TextureAddress0, Offscreen), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
      list.put(GE::TextureFilter, s.filter);
      put(GE::Sprites, 0x80'019f, {{s.u0, s.v0, 0xffff'ffff, s.x0, s.y0, 0}, {s.u1, s.v1, 0xffff'ffff, s.x1, s.y1, 0}});
    }
    //and two sprites of one PRIM (one Look, two jobs) taking texels far apart: the Look's reach is both jobs'
    redraw();
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::TextureAddress0, Offscreen), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
    list.put(GE::TextureFilter, 0);
    put(GE::Sprites, 0x80'019f, {{2, 2, 0xffff'ffff, 0, 40, 0}, {14, 14, 0xffff'ffff, 12, 52, 0},
                                 {48, 48, 0xffff'ffff, 50, 20, 0}, {60, 60, 0xffff'ffff, 62, 32, 0}});
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](u32 threads, bool fours) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    m.system.ge.fourPixels = fours;
    build(m.system.memory, m.system.ge);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return std::vector<u8>(m.system.memory.vram.begin(), m.system.memory.vram.begin() + 64 * 64 * 4);
  };
  for(bool fours : {true, false}) {
    auto one = drawn(1, fours);
    u32 drawnPixels = 0;
    for(u32 n = 0; n < 64 * 64; n++) drawnPixels += (one[n * 4] | one[n * 4 + 1] | one[n * 4 + 2]) != 0;
    CHECK(drawnPixels > 3000, true);
    for(u32 threads : {2u, 4u, 8u}) CHECK(drawn(threads, fours) == one, true);
  }
  //Looks of one batch with the same texture and palette share one copy: decoded over all of their texels, the rows
  //and columns from the least of them to the greatest, and none past those rows. One whose jobs draw nothing adds none.
  KernelMachine m;
  auto& ge = m.system.ge;
  for(u32 n = 0; n < 64 * 64; n++) m.system.memory.write(4, VRAM + Offscreen + n * 4, n * 0x0101'0101 + 7);
  ge.commands[GE::TextureAddress0] = Offscreen, ge.commands[GE::TextureBufferWidth0] = 0x04 << 16 | 64;
  ge.commands[GE::TextureSize0] = 6 << 8 | 6, ge.commands[GE::TextureFormat] = 3;
  auto& batch = ge.drawing.batches[0];
  struct Rectangle { u32 top, bottom, left, right; };
  for(Rectangle reach : {Rectangle{2, 10, 30, 40}, Rectangle{40, 50, 3, 9}, Rectangle{0, 0, 0, 0}}) {
    auto& look = batch.looks.emplace_back();
    look.textured = true, look.texture = ge.sampler(), look.deferRows = 64;
    if(!reach.bottom && !reach.right) continue;
    look.reach = std::make_unique<GE::Reach>();
    if(reach.bottom) GE::Reach::mark(look.reach->rows, reach.top, reach.bottom);
    if(reach.right) GE::Reach::mark(look.reach->columns, reach.left, reach.right);
  }
  ge.ensureDecoded(batch);
  auto& t = batch.looks[0].texture;
  CHECK(t.decoded && t.decoded == batch.looks[1].texture.decoded && t.decoded == batch.looks[2].texture.decoded, true);
  CHECK(t.decodedRows, 51u), CHECK(t.decodedTop, 2u), CHECK(t.decodedLeft, 3u), CHECK(t.decodedRight, 41u);
  u32 wrong = 0;
  for(u32 y : {2, 3, 9, 10, 40, 41, 49, 50}) {
    for(u32 x : {3, 4, 8, 9, 30, 31, 39, 40}) wrong += t.decoded[y * 64 + x] != (y * 64 + x) * 0x0101'0101 + 7;
  }
  CHECK(wrong, 0u);
  ge.clearBatch(batch);
  //Past 16 pieces (row runs by column runs), the rectangle round them, the gaps between them too, in one go. (The
  //texture other now: the copy's room, the last one's, can't hold these texels already.)
  for(u32 n = 0; n < 64 * 64; n++) m.system.memory.write(4, VRAM + Offscreen + n * 4, n * 0x0102'0304 + 9);
  auto& many = batch.looks.emplace_back();
  many.textured = true, many.texture = ge.sampler(), many.deferRows = 64;
  many.reach = std::make_unique<GE::Reach>();
  for(u32 n = 0; n < 5; n++) {
    GE::Reach::mark(many.reach->rows, 1 + n * 10, 3 + n * 10);
    GE::Reach::mark(many.reach->columns, 2 + n * 11, 4 + n * 11);
  }
  ge.ensureDecoded(batch);
  auto& u = many.texture;
  CHECK(u.decodedRows, 44u), CHECK(u.decodedTop, 1u), CHECK(u.decodedLeft, 2u), CHECK(u.decodedRight, 49u);
  wrong = 0;
  for(u32 y : {1, 2, 7, 25, 43}) {
    for(u32 x : {2, 3, 8, 30, 48}) wrong += u.decoded[y * 64 + x] != (y * 64 + x) * 0x0102'0304 + 9;
  }
  CHECK(wrong, 0u);
  ge.clearBatch(batch);
}

//More batches than the GE has (ge/threads.cpp's Batches), each launched as the render target changes, round and
//round, three targets drawn into in turn, each also drawn with by the next (render to texture): the GE's thread fills
//each batch again only once it's drawn. It comes out as on one thread, with 2, 4 and 8 threads, every batch shared.
static auto ringList(Memory& memory) -> void {
  constexpr u32 Targets[3] = {0x10'0000, 0x11'0000, 0x12'0000};  //(VRAM offsets: 64 by 64, 8888)
  auto build = [&](Memory& memory) {
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1), list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::FrameBufferWidth, 64), list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64);
    list.put(GE::TextureSize0, 6 << 8 | 6), list.put(GE::TextureFormat, 3), list.put(GE::TextureFunction, 0);
    for(u32 n = 0; n < 3 * GE::Batches + 2; n++) {
      list.put(GE::FrameBufferPointer, Targets[n % 3]);
      list.put(GE::TextureMappingEnable, 0);
      float x = n * 7 % 40, y = n * 11 % 40;
      put(GE::Sprites, {{0, 0, 0, x, y, 0}, {0, 0, 0xff00'0000 | n * 0x0a'1b2c, x + 24, y + 20, 0}});
      list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, Targets[(n + 2) % 3]);
      put(GE::Sprites, {{0, 0, 0xff80'c0ff, 8, 8, 0}, {64, 64, 0xff80'c0ff, 56, 56, 0}});
    }
    list.put(GE::Finish), list.put(GE::End);
  };
  build(memory);
}
//(the ring's list drawn with threads, every batch shared; threaded: the GE on its own thread)
static auto ringDrawn(u32 threads, bool threaded = false) -> std::vector<u8> {
  KernelMachine m;
  m.system.threadGE(threaded || testsGeThread());
  m.system.ge.setThreads(threads);
  m.system.ge.drawing.shared = 0;
  ringList(m.system.memory);
  m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
  m.system.ge.settle();
  return {m.system.memory.vram.begin(), m.system.memory.vram.end()};
}
static auto geRing() -> void {
  auto one = ringDrawn(1);
  for(u32 threads : {2u, 4u, 8u}) CHECK(ringDrawn(threads) == one, true);
}

//With the GE on its thread: the ring's textures, render to texture from batches still being drawn, decoded as their
//batches start (deferred), on whichever thread starts each, the GE thread's among them while it does a later record:
//their reads are as of their own PRIMs' places, and lie in what the model declares those PRIMs may read (the
//G-interest check, on in every threaded machine), not the record's the thread does then. Many rounds, as which thread
//starts a batch goes by timing; each comes out as on one thread without the thread.
static auto geThreadDeferredDecodes() -> void {
  auto one = ringDrawn(1);
  u32 apart = 0;
  for(u32 round = 0; round < 10; round++) {
    for(u32 threads : {2u, 4u, 8u}) apart += ringDrawn(threads, true) != one;
  }
  CHECK(apart, 0u);
}

//With the GE on its thread: a block transfer made outside a list by the GE's transfer(), its commands set from outside
//(as a test sets them), its texels into a texture a sprite was just drawn with (decoded, its pages watched on the GE
//thread): the render GE replays the transfer's checks with those commands (the state handed over first, as a PRIM's
//outside a list is), so the texture is heard written and decoded again, and the sprite drawn again takes the new
//texels, as without the thread.
static auto geThreadTransferOutside() -> void {
  constexpr u32 Texture = VRAM + 0x8000, Source = 0x0899'0000;
  auto set = [](System& s) {
    auto& c = s.ge.commands;
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = 64, c[GE::FrameBufferPixelFormat] = 3;
    c[GE::Scissor2] = 63 | 47 << 10, c[GE::Region2] = 1023 | 1023 << 10;
    c[GE::VertexType] = 0x80'019f, c[GE::ShadeMode] = 1;
    c[GE::TextureMappingEnable] = 1, c[GE::TextureAddress0] = Texture & 0xff'ffff;
    c[GE::TextureBufferWidth0] = (Texture >> 24 & 0xf) << 16 | 16, c[GE::TextureSize0] = 4 << 8 | 4;
    c[GE::TextureFormat] = 3, c[GE::TextureFunction] = 3 | 1 << 8, c[GE::TextureFilter] = 0;
  };
  auto sprite = [](System& s) {
    u32 at = Vertices;
    for(auto [u, x] : {std::pair{0.0f, 8.0f}, std::pair{16.0f, 24.0f}}) {
      for(float value : {u, u}) s.memory.write(4, at, std::bit_cast<u32>(value)), at += 4;
      s.memory.write(4, at, 0xffff'ffff), at += 4;
      for(float value : {x, x, 0.0f}) s.memory.write(4, at, std::bit_cast<u32>(value)), at += 4;
    }
    s.ge.vertexAddress = Vertices;
    s.ge.primitive(GE::Sprites, 2);
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  for(System* s : {&plain, &threaded}) {
    s->ge.setThreads(1);
    for(u32 n = 0; n < 16 * 16; n++) s->memory.write(4, Texture + n * 4, 0xff00'0000 | n * 0x01'0305);
    for(u32 n = 0; n < 16 * 16; n++) s->memory.write(4, Source + n * 4, 0xff00'0000 | n * 0x07'0b0d);
    set(*s);
    sprite(*s);
    auto& c = s->ge.commands;
    c[GE::TransferSource] = Source & 0xff'fff0, c[GE::TransferSourceWidth] = (Source >> 24) << 16 | 16;
    c[GE::TransferSourcePosition] = 0;
    c[GE::TransferDestination] = Texture & 0xff'fff0, c[GE::TransferDestinationWidth] = (Texture >> 24) << 16 | 16;
    c[GE::TransferDestinationPosition] = 0;
    c[GE::TransferSize] = 15 | 15 << 10, c[GE::TransferStart] = 1;
    s->ge.transfer();
    sprite(*s);
    s->ge.settleAll();
  }
  CHECK(plain.memory.read(4, VRAM + (16 * 64 + 16) * 4) & 0xff'ffff, (8 * 16 + 8) * 0x07'0b0du & 0xff'ffff);  //(new)
  CHECK(plain.memory.vram == threaded.memory.vram, true);
}

//Sprites drawing over their own texture, from a copy where none takes a texel it has drawn by then (ge/draw.cpp's
//readsAhead()), in a list on several threads: a picture drawn, brightened in place twice (each from the picture as the
//last left it: its copy decoded as its batch starts, once the one before is drawn), halved in place, drawn with
//elsewhere, and a sprite over it whose blending keeps every pixel (not drawn); then, in another list once that's all
//drawn, brightened again (its copy decoded at once, the sprites drawn in bands). It comes out as reading every
//texture from memory as it's drawn (a machine without watching()) does on one thread, with 1, 2, 4 and 8 threads,
//batches shared out however small.
static auto geOwnTexture() -> void {
  constexpr u32 Picture = 0x10'0000;  //(VRAM offset: 64 by 64, 5551)
  auto build = [&](Memory& memory) {
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    list.put(GE::FrameBufferPointer, Picture), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 1);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    for(u32 n = 0; n < 12; n++) {
      float x = n * 13 % 50, y = n * 7 % 40;
      put(GE::Triangles, {{0, 0, 0xff10'2030 * (n + 1), x, y, 0}, {0, 0, 0x8000'ff00, x + 30, y + 9, 0},
                          {0, 0, 0xffff'0000, x + 11, y + 28, 0}});
    }
    list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, Picture);
    list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64), list.put(GE::TextureSize0, 8 << 8 | 8);
    list.put(GE::TextureFormat, 1), list.put(GE::TextureFilter, 1 | 1 << 8);
    list.put(GE::TextureFunction, 0 | 1 << 16);  //modulated, doubled
    list.put(GE::AlphaBlendEnable, 1), list.put(GE::BlendMode, 10 | 10 << 4);
    list.put(GE::BlendFixedA, 0xff'ffff), list.put(GE::BlendFixedB, 0xff'ffff);
    for(u32 twice = 0; twice < 2; twice++) {
      put(GE::Sprites, {{0, 0, 0xff8e'8e8e, 0, 0, 0}, {32, 64, 0xff8e'8e8e, 32, 64, 0},
                        {32, 0, 0xff8e'8e8e, 32, 0, 0}, {64, 64, 0xff8e'8e8e, 64, 64, 0}});
    }
    list.put(GE::AlphaBlendEnable, 0), list.put(GE::TextureFunction, 3);
    put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 64, 0, 8, 32, 0}, {16, 0, 0, 8, 0, 0}, {32, 64, 0, 16, 32, 0},
                      {32, 0, 0, 16, 0, 0}, {48, 64, 0, 24, 32, 0}, {48, 0, 0, 24, 0, 0}, {64, 64, 0, 32, 32, 0}});
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 512), list.put(GE::FrameBufferPixelFormat, 3);
    put(GE::Sprites, {{0, 0, 0, 100, 50, 0}, {64, 64, 0, 228, 178, 0}});
    list.put(GE::AlphaBlendEnable, 1), list.put(GE::BlendMode, 10 | 10 << 4 | 0 << 8);
    list.put(GE::BlendFixedA, 0), list.put(GE::BlendFixedB, 0xff'ffff);
    put(GE::Sprites, {{0, 0, 0xffff'ffff, 90, 40, 0}, {64, 64, 0xffff'ffff, 250, 200, 0}});
    list.put(GE::Finish), list.put(GE::End);
    list.address = ListB;
    list.put(GE::FrameBufferPointer, Picture), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 1), list.put(GE::TextureFunction, 0 | 1 << 16);
    list.put(GE::BlendFixedA, 0xff'ffff), list.put(GE::BlendFixedB, 0xff'ffff);
    put(GE::Sprites, {{0, 0, 0xff8e'8e8e, 0, 0, 0}, {32, 64, 0xff8e'8e8e, 32, 64, 0},
                      {32, 0, 0xff8e'8e8e, 32, 0, 0}, {64, 64, 0xff8e'8e8e, 64, 64, 0}});
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](u32 threads, u64 shared, bool decoding) {
    KernelMachine m;
    if(!decoding) m.system.memory.watching = nullptr;  //(nothing kept decoded: Memory::canWatch())
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = shared;
    build(m.system.memory);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    m.call("sceGeListEnQueue", {ListB, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.memory.vram;
  };
  auto read = drawn(1, 8192, false);
  for(u32 threads : {1u, 2u, 4u, 8u}) {
    for(u64 shared : {u64(0), u64(8192)}) CHECK(drawn(threads, shared, true) == read, true);
  }
}

//3D triangles drawing over their own texture, from a copy where none of their pixels can take a texel any of them may
//write (ge/draw.cpp's readsApart()): a 64x64 picture made in place, then quads with one w, as Killzone's menu blurs its
//picture, each into one half of it from the other (filtered, one reaching a texel past the right edge, which repeats
//round to column 0, where it doesn't draw), one taking columns it doesn't draw in its own rows, and those it can't: one
//taking rows it draws, two taking a row or a column it draws by a filter's second texel, one cut at the near plane, one
//in perspective (w 1 to 2), one whose q is -1 (every pixel the last texel, which it draws), and a PRIM of two
//triangles, the second taking the first's pixels. It comes out as reading every texture from memory as it's drawn (a
//machine without watching()) does on one thread, with 1, 2, 4 and 8 threads, batches shared out however small.
static auto geOwnTexture3D() -> void {
  Unthreaded unthreaded;  //(the batches looked at directly: system.hpp)
  constexpr u32 Picture = 0x10'0000;  //(VRAM offset: 64 by 64, 8888)
  //(each quad's left and right edges at a z of their own, its w -z: z above 0 is behind the camera)
  struct Quad { float x0, y0, x1, y1, u0, v0, u1, v1, z0, z1; u32 filter; };
  constexpr Quad Quads[] = {
    {0, 32, 64, 64, 0.03f, 0.5f, 64.03f, 31.5f, -1, -1, 0x101},    //the top half into the bottom
    {0, 0, 64, 32, 0.03f, 32.5f, 64.03f, 63.5f, -1, -1, 0x101},    //and back
    {40, 32, 64, 64, 40.05f, 0.5f, 64.05f, 31.5f, -1, -1, 0x101},  //(the last column's second texel: column 0)
    {0, 10, 20, 30, 30, 10, 50, 30, -1, -1, 0},                   //columns right of it in its own rows
    {8, 8, 40, 40, 10, 30, 42, 62, -1, -1, 0x101},                //rows it draws
    {0, 32, 32, 64, 0.03f, 1.5f, 32.03f, 32.5f, -1, -1, 0x101},   //its own first row, filtered, at its last
    {32, 0, 64, 32, 1.5f, 0.03f, 32.5f, 32.03f, -1, -1, 0x101},   //its own first column likewise
    {44, 0, 60, 16, 44, 20, 60, 36, -1, 0.5f, 0},                 //cut at the near plane
    {20, 40, 44, 60, 20, 4, 44, 24, -1, -2, 0x101},               //in perspective
  };
  auto build = [&](Memory& memory, GE& ge) {
    for(u32 n = 0; n < 16; n++) ge.projection[n] = 0;
    for(u32 n : {0, 4, 8}) ge.world[n] = ge.view[n] = 0x3f'8000;
    for(u32 n : {0, 5}) ge.projection[n] = 0x3f'8000;
    ge.projection[11] = 0xbf'8000;  //w = -z, and z 0: a corner past the near plane is one behind the camera
    ge.textureMatrix[0] = ge.textureMatrix[4] = 0x3f'8000, ge.textureMatrix[11] = 0xbf'8000;  //s = u, t = v, q = -1
    auto& c = ge.commands;
    auto f24 = [](float value) { return std::bit_cast<u32>(value) >> 8; };
    c[GE::ViewportXScale] = c[GE::ViewportYScale] = c[GE::ViewportZScale] = f24(1);
    c[GE::ViewportXCenter] = c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(100);
    c[GE::OffsetX] = c[GE::OffsetY] = 2048 << 4;
    c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
    c[GE::MaxZ] = 0xffff;
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    struct V { float u, v; u32 color; float x, y, z; };
    auto put = [&](u32 kind, u32 type, std::initializer_list<V> vertices) {
      list.put(GE::VertexType, type);
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    auto frame = [&] {
      list.put(GE::FrameBufferPointer, Picture), list.put(GE::FrameBufferWidth, 64);
      list.put(GE::FrameBufferPixelFormat, 3), list.put(GE::ShadeMode, 1);
      list.put(GE::Scissor2, 63 | 63 << 10), list.put(GE::Region2, 63 | 63 << 10);
    };
    auto texture = [&] {
      list.put(GE::TextureMappingEnable, 1), list.put(GE::TextureAddress0, Picture);
      list.put(GE::TextureBufferWidth0, 0x04 << 16 | 64), list.put(GE::TextureSize0, 6 << 8 | 6);
      list.put(GE::TextureFormat, 3), list.put(GE::TextureFunction, 0);  //modulated
    };
    frame();
    for(u32 n = 0; n < 12; n++) {
      float x = n * 13 % 50, y = n * 7 % 40;
      put(GE::Triangles, 0x80'019f, {{0, 0, 0xff10'2030 * (n + 1), x, y, 0}, {0, 0, 0x8000'ff00, x + 30, y + 9, 0},
                                     {0, 0, 0xffff'0000, x + 11, y + 28, 0}});
    }
    texture();
    for(auto& q : Quads) {
      list.put(GE::TextureFilter, q.filter);
      float w0 = -q.z0, w1 = -q.z1;  //(x and y as far from the middle as w is, so that they land there)
      put(GE::TriangleStrip, 0x19f, {{q.u0 / 64, q.v0 / 64, 0xffc0'e0ff, q.x0 * w0, q.y0 * w0, q.z0},
                                    {q.u1 / 64, q.v0 / 64, 0xffc0'e0ff, q.x1 * w1, q.y0 * w1, q.z1},
                                    {q.u0 / 64, q.v1 / 64, 0xffc0'e0ff, q.x0 * w0, q.y1 * w0, q.z0},
                                    {q.u1 / 64, q.v1 / 64, 0xffc0'e0ff, q.x1 * w1, q.y1 * w1, q.z1}});
    }
    //a quad whose q is -1 at every corner (the texture matrix's), each pixel taking the last texel of a 16x16 view of
    //the picture, which it draws in its row 15
    list.put(GE::TextureSize0, 4 << 8 | 4), list.put(GE::TextureFilter, 0), list.put(GE::TextureMapMode, 1 | 1 << 8);
    put(GE::TriangleStrip, 0x19f, {{0.1f, 0.55f, 0xffc0'e0ff, 0, 10, -1}, {0.3f, 0.55f, 0xffc0'e0ff, 32, 10, -1},
                                  {0.1f, 0.8f, 0xffc0'e0ff, 0, 20, -1}, {0.3f, 0.8f, 0xffc0'e0ff, 32, 20, -1}});
    list.put(GE::TextureMapMode, 0), list.put(GE::TextureSize0, 6 << 8 | 6);
    //two triangles, the second taking texels where the first draws, each apart from its own
    list.put(GE::TextureFilter, 0);
    put(GE::Triangles, 0x19f, {{40.0f / 64, 8.0f / 64, 0xffff'ffff, 0, 40, -1},
                              {56.0f / 64, 8.0f / 64, 0xffff'ffff, 16, 40, -1},
                              {40.0f / 64, 24.0f / 64, 0xffff'ffff, 0, 56, -1},
                              {1.0f / 64, 41.0f / 64, 0xffff'ffff, 20, 40, -1},
                              {9.0f / 64, 41.0f / 64, 0xffff'ffff, 36, 40, -1},
                              {1.0f / 64, 49.0f / 64, 0xffff'ffff, 20, 56, -1}});
    list.put(GE::Finish), list.put(GE::End);
    //and the first quad again, in a list of its own
    list.address = ListB;
    frame(), texture();
    auto& q = Quads[0];
    list.put(GE::TextureFilter, q.filter);
    put(GE::TriangleStrip, 0x19f, {{q.u0 / 64, q.v0 / 64, 0xffc0'e0ff, q.x0, q.y0, -1},
                                  {q.u1 / 64, q.v0 / 64, 0xffc0'e0ff, q.x1, q.y0, -1},
                                  {q.u0 / 64, q.v1 / 64, 0xffc0'e0ff, q.x0, q.y1, -1},
                                  {q.u1 / 64, q.v1 / 64, 0xffc0'e0ff, q.x1, q.y1, -1}});
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](u32 threads, u64 shared, bool decoding) {
    KernelMachine m;
    if(!decoding) m.system.memory.watching = nullptr;  //(nothing kept decoded: Memory::canWatch())
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = shared;
    build(m.system.memory, m.system.ge);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.memory.vram;
  };
  auto read = drawn(1, 8192, false);
  for(u32 threads : {1u, 2u, 4u, 8u}) {
    for(u64 shared : {u64(0), u64(8192)}) CHECK(drawn(threads, shared, true) == read, true);
  }
  //The first quad, by itself on two threads, is decoded and waits in a batch (launched, the CPU's guard over VRAM
  //keeping it so), not drawn at once. (In the list above, the two triangles drawn at once at its end draw every batch
  //first.)
  KernelMachine m;
  m.system.ge.setThreads(2);
  m.system.ge.drawing.shared = 0;
  build(m.system.memory, m.system.ge);
  m.call("sceGeListEnQueue", {ListB, 0, 0xffff'ffff, 0});
  bool batched = false;
  for(auto& batch : m.system.ge.drawing.batches) {
    if(!batch.launched) continue;
    for(auto& look : batch.looks) {
      batched |= look.textured && look.pixel.frameBuffer == Picture && look.texture.decoded &&
                 (look.texture.address & 0x1fff'ffff) == Memory::VRAMBase + Picture;
    }
  }
  CHECK(batched, true);
  m.system.ge.settle();
}

//Block transfers among batches (ge/threads.cpp's drawnBefore()): each waits only for the drawing that writes what it
//reads, or reads or writes what it writes, the rest going on being drawn or waiting in the batch being filled. A list
//uploads two textures in turn into one place in VRAM nothing draws, each drawn with between (as God of War streams
//its textures), in the batch being filled; copies pixels that batch has just drawn; draws a picture off the screen and
//then with it (render to texture, its decode left till its batch starts), and copies over that picture; copies into
//drawn pixels through VRAM's second copy (which rearranges them); copies a texture back to RAM. It comes out as on one
//thread, VRAM and RAM, with 2, 4 and 8 threads, batches shared out however small, with the CPU's guard over VRAM and
//without. And a copy reaching nothing the batch being filled draws leaves its primitives waiting, while one reading
//their pixels has them drawn first, as one landing in them through VRAM's second copy does; one reading pixels a
//batch launched before draws waits for it, and one reaching nothing a launched batch draws doesn't.
static auto geTransfers() -> void {
  Unthreaded unthreaded;  //(the batches looked at directly: system.hpp)
  constexpr u32 Texture = 0x0898'0000, Back = 0x0899'0000;  //(two 64 by 64 pictures in RAM; and where one comes back)
  constexpr u32 Offscreen = 0x15'4000, Staging = 0x18'0000, Copied = 0x1c'0000;  //(VRAM offsets)
  struct V { float u, v; u32 color; float x, y, z; };
  auto copy = [](ListWriter& list, u32 from, u32 fromWidth, u32 fromAt, u32 to, u32 toWidth, u32 toAt, u32 size) {
    list.put(GE::TransferSource, from & 0xff'fff0), list.put(GE::TransferSourceWidth, (from >> 24) << 16 | fromWidth);
    list.put(GE::TransferSourcePosition, fromAt);
    list.put(GE::TransferDestination, to & 0xff'fff0);
    list.put(GE::TransferDestinationWidth, (to >> 24) << 16 | toWidth);
    list.put(GE::TransferDestinationPosition, toAt);
    list.put(GE::TransferSize, size), list.put(GE::TransferStart, 1);
  };
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 2 * 64 * 64; n++) memory.write(4, Texture + n * 4, 0xff00'0000 | (n * 0x0102'0305 & 0xff'ffff));
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    auto put = [&](u32 kind, std::initializer_list<V> vertices) {
      list.to(GE::VertexAddress, vertex);
      for(auto& v : vertices) {
        for(float value : {v.u, v.v}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
        memory.write(4, vertex, v.color), vertex += 4;
        for(float value : {v.x, v.y, v.z}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      }
      list.put(GE::Primitive, kind << 16 | vertices.size());
    };
    auto target = [&](u32 address, u32 width) {
      list.put(GE::FrameBufferPointer, address), list.put(GE::FrameBufferWidth, width);
    };
    auto texture = [&](u32 address, u32 width) {
      list.put(GE::TextureMappingEnable, 1), list.to(GE::TextureAddress0, address);
      list.put(GE::TextureBufferWidth0, (address >> 24) << 16 | width);
    };
    list.put(GE::FrameBufferPixelFormat, 3), target(0, 512);
    list.put(GE::DepthBufferPointer, 0x11'0000), list.put(GE::DepthBufferWidth, 512);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    list.put(GE::TextureSize0, 6 << 8 | 6), list.put(GE::TextureFormat, 3), list.put(GE::TextureFunction, 3);
    list.put(GE::ClearMode, 1 | 7 << 8);
    put(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff10'2030, 480, 272, 0}});
    list.put(GE::ClearMode, 0);
    list.put(GE::DepthTestEnable, 1), list.put(GE::DepthTest, 7);
    for(u32 n = 0; n < 16; n++) {
      float x = n * 23 % 400, y = n * 41 % 200, z = n * 3000.0f;
      put(GE::Triangles, {{0, 0, 0xff00'00ff + n * 0x0c00, x, y, z}, {0, 0, 0xff00'ff00, x + 70, y + 12, z},
                          {0, 0, 0xffff'0000, x + 25, y + 60, z}});
    }
    list.put(GE::DepthTestEnable, 0);
    //two textures uploaded into one place in turn, each drawn with in between: neither copy waits for the batch
    copy(list, Texture, 64, 0, VRAM + Staging, 64, 0, 63 | 63 << 10);
    texture(VRAM + Staging, 64);
    put(GE::Sprites, {{0, 0, 0, 20, 150, 0}, {64, 64, 0, 84, 214, 0}});
    copy(list, Texture + 64 * 64 * 4, 64, 0, VRAM + Staging, 64, 0, 63 | 63 << 10);
    put(GE::Sprites, {{0, 0, 0, 100, 150, 0}, {64, 64, 0, 164, 214, 0}});
    //pixels the batch being filled has drawn, copied
    list.put(GE::TextureMappingEnable, 0);
    copy(list, VRAM, 512, 30 | 160 << 10, VRAM + Copied, 512, 0, 99 | 39 << 10);
    //a picture off the screen, drawn with (its decode left till its batch starts), then copied over
    target(Offscreen, 64);
    for(u32 n = 0; n < 4; n++) {
      put(GE::Sprites, {{0, 0, 0, n * 16.0f, 0, 0}, {0, 0, 0xff20'0000 + n * 0x30'2010, n * 16.0f + 16, 64, 0}});
    }
    target(0, 512);
    texture(VRAM + Offscreen, 64);
    put(GE::Sprites, {{0, 0, 0, 200, 20, 0}, {64, 64, 0, 264, 84, 0}});
    copy(list, Texture, 64, 0, VRAM + Offscreen, 64, 0, 31 | 31 << 10);
    put(GE::Sprites, {{0, 0, 0, 300, 20, 0}, {64, 64, 0, 364, 84, 0}});
    //into drawn pixels through VRAM's second copy, which rearranges each 16 KiB
    list.put(GE::TextureMappingEnable, 0);
    put(GE::Sprites, {{0, 0, 0, 0, 100, 0}, {0, 0, 0xff80'4020, 480, 140, 0}});
    copy(list, Texture, 64, 0, VRAM + Memory::VRAMSize + (110 * 512) * 4, 512, 0, 63 | 7 << 10);
    put(GE::Sprites, {{0, 0, 0, 40, 104, 0}, {0, 0, 0xff00'8080, 440, 136, 0}});
    //a texture back to RAM (nothing draws it), and more drawn after
    copy(list, VRAM + Staging, 64, 0, Back, 64, 0, 63 | 63 << 10);
    for(u32 n = 0; n < 6; n++) {
      put(GE::Sprites, {{0, 0, 0, n * 70.0f, 230, 0}, {0, 0, 0xff40'40ff, n * 70.0f + 60, 270, 0}});
    }
    list.put(GE::Finish), list.put(GE::End);
  };
  //(guarded: the CPU's guard over VRAM, whose busy pages would have a copy wait for what it reaches as well)
  auto drawn = [&](u32 threads, u64 shared, bool guarded) {
    KernelMachine m;
    if(!guarded) m.system.memory.vramGuard = nullptr;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = shared;
    build(m.system.memory);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    auto memory = m.system.memory.vram;
    for(u32 n = 0; n < 64 * 64 * 4; n++) memory.push_back(m.system.memory.read(1, Back + n));
    return memory;
  };
  auto one = drawn(1, 8192, true);
  auto word = [&](u32 at) { return one[at] | one[at + 1] << 8 | one[at + 2] << 16 | u32(one[at + 3]) << 24; };
  CHECK(word((160 * 512 + 30) * 4) != 0x0010'2030, true);  //(a pixel drawn over the clear, then copied)
  CHECK(word(Copied), word((160 * 512 + 30) * 4));
  for(u32 threads : {2u, 4u, 8u}) {
    for(u64 shared : {u64(0), u64(8192)}) {
      for(bool guarded : {true, false}) CHECK(drawn(threads, shared, guarded) == one, true);
    }
  }

  //a copy reaching nothing the batch draws leaves it waiting; one reading what it has drawn has it drawn first
  KernelMachine m;
  auto& ge = m.system.ge;
  ge.setThreads(4);
  ge.drawing.shared = 0;
  for(u32 n = 0; n < 64 * 64; n++) m.system.memory.write(4, Texture + n * 4, 0xff00'0000 | n);
  ListWriter list{m.system.memory, ListA};
  list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 512), list.put(GE::FrameBufferPixelFormat, 3);
  list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
  list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
  m.call("sceGeListEnQueue", {ListA, list.address, 0xffff'ffff, 0});  //(the settings, up to its stall address)
  u32 vertex = Vertices;
  auto sprite = [&](float left, float top, float right, float bottom, u32 color) {
    ge.vertexAddress = vertex;
    for(V v : {V{0, 0, 0, left, top, 0}, V{0, 0, color, right, bottom, 0}}) {
      for(float value : {v.u, v.v}) m.system.memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
      m.system.memory.write(4, vertex, v.color), vertex += 4;
      for(float value : {v.x, v.y, v.z}) m.system.memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    }
    ge.primitive(GE::Sprites, 2);
  };
  //(as while a list runs; and with no guard over VRAM for the CPU, whose busy pages would have the copy wait too)
  ge.drawing.deferring = true;
  m.system.memory.vramGuard = nullptr;
  sprite(10, 10, 30, 20, 0xff12'3456);
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  auto transfer = [&](u32 from, u32 fromWidth, u32 fromAt, u32 to, u32 toWidth) {  //(8 by 8 pixels)
    ge.commands[GE::TransferSource] = from & 0xff'fff0;
    ge.commands[GE::TransferSourceWidth] = (from >> 24) << 16 | fromWidth;
    ge.commands[GE::TransferDestination] = to & 0xff'fff0;
    ge.commands[GE::TransferDestinationWidth] = (to >> 24) << 16 | toWidth;
    ge.commands[GE::TransferSourcePosition] = fromAt, ge.commands[GE::TransferDestinationPosition] = 0;
    ge.commands[GE::TransferSize] = 7 | 7 << 10, ge.commands[GE::TransferStart] = 1;
    ge.transfer();
  };
  transfer(Texture, 64, 0, VRAM + Staging, 64);
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  CHECK(m.system.memory.read(4, VRAM + Staging + 4), 0xff00'0001u);
  transfer(VRAM, 512, 10 | 10 << 10, VRAM + Copied, 512);
  CHECK(ge.drawing.batch->jobs.empty(), true);
  CHECK(m.system.memory.read(4, VRAM + Copied), 0x0012'3456u);  //(the sprite's pixel: alpha the stencil, 0)
  //through VRAM's second copy, which moves each 32 bytes about inside their 16 KiB, a copy to 8 KiB past the row a
  //sprite draws lands in that row: drawn first
  sprite(0, 200, 64, 201, 0xff65'4321);
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  transfer(Texture, 64, 0, VRAM + Memory::VRAMSize + 200 * 512 * 4 + 0x2000, 64);
  CHECK(ge.drawing.batch->jobs.empty(), true);
  //pixels a batch launched before draws (as the render target changed), still being drawn: that batch is waited for
  for(u32 n = 0; n < 48; n++) sprite(0, 0, 480, 272, 0xff00'0000 | n * 0x05'0301);
  ge.commands[GE::FrameBufferPointer] = Offscreen;
  sprite(0, 0, 8, 8, 0xff00'ff00);
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  transfer(VRAM, 512, 100 | 100 << 10, Back, 64);
  CHECK(m.system.memory.read(4, Back), 0x00eb'8d2fu);  //(the last of the 48 sprites)
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  //a batch launched before that draws nothing the copy touches isn't waited for: it stays launched (drawn or not)
  for(u32 n = 0; n < 8; n++) sprite(0, 0, 64, 64, 0xff00'0000 | n);  //(off the screen, with the one before)
  ge.commands[GE::FrameBufferPointer] = 0;
  sprite(0, 260, 8, 261, 0xff00'00ff);  //(the render target changed: that batch launched)
  GE::Batch* offscreen = nullptr;
  for(auto& batch : ge.drawing.batches) {
    if(batch.launched) offscreen = &batch;
  }
  CHECK(offscreen != nullptr, true);
  transfer(Texture, 64, 0, VRAM + Staging, 64);
  CHECK(offscreen && offscreen->launched, true);
  CHECK(ge.drawing.batch->jobs.size(), 1u);
  ge.drawing.deferring = false;
}

//sceGeBreak, as pspautotests' gpu/ge/break and breakwait recorded on a PSP: the refusals in their order (a mode but 0
//or 1, parameters reaching the kernel's half of memory, an empty queue); mode 0 breaking off a stalled list, its ID
//returned, the GE free (sceGeSaveContext works), and sceGeContinue taking it up again; a list paused by a PAUSE
//signal busy; mode 1 throwing everything away, the next list taking the first ID again, and threads waiting for a
//list or for all drawing left waiting until a list ends.
static auto geBreak() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);
  CHECK(m.call("sceGeBreak", {1, 0}), Kernel::ErrorAlready);
  CHECK(m.call("sceGeBreak", {0xffff'ffff, 0}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceGeBreak", {2, 0}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceGeBreak", {0, ListA}), Kernel::ErrorAlready);
  CHECK(m.call("sceGeBreak", {0, 0xdead'beef}), Kernel::ErrorPrivilegeRequired);
  CHECK(m.call("sceGeBreak", {0, 0xffff'ffff}), Kernel::ErrorPrivilegeRequired);
  CHECK(m.call("sceGeBreak", {0, 0x7fff'fff0}), Kernel::ErrorPrivilegeRequired);
  CHECK(m.call("sceGeBreak", {0, 0x7fff'ffef}), Kernel::ErrorAlready);
  CHECK(m.call("sceGeContinue", {}), 0);
  //break's list: NOP, a PAUSE signal, then a FINISH twice
  ListWriter list{memory, ListA};
  list.put(GE::Nop); list.put(GE::Signal, 0x03 << 16); list.put(GE::End);
  list.put(GE::Nop); list.put(GE::Finish); list.put(GE::End);
  list.put(GE::Nop); list.put(GE::Finish); list.put(GE::End);
  u32 id = m.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0});  //stalled at its start
  CHECK(m.call("sceGeSaveContext", {Saved}), 0xffff'ffff);
  CHECK(m.call("sceGeBreak", {0, 0}), id);
  CHECK(m.kernel.geRunning, -1);
  CHECK(m.call("sceGeListSync", {id, 1}), 4);  //(paused: not recorded)
  CHECK(m.call("sceGeSaveContext", {Saved}), 0);
  CHECK(m.call("sceGeContinue", {}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 3);  //at its stall address again
  CHECK(m.call("sceGeBreak", {1, 0}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), Kernel::ErrorInvalidID);
  id = m.call("sceGeListEnQueue", {ListA, ListA + 400, 0xffff'ffff, 0});
  CHECK(m.call("sceGeListSync", {id, 1}), 4);  //paused at its FINISH by the signal
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeContinue", {}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeBreak", {1, 0}), Kernel::ErrorAlready);
  ListWriter done{memory, ListB};
  done.put(GE::Nop); done.put(GE::Finish); done.put(GE::End);
  m.call("sceGeListEnQueue", {ListB, ListB + 400, 0xffff'ffff, 0});
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);  //finished already

  //breakwait: two threads (better than main) waiting, for a stalled list and for all drawing, stay waiting through a
  //break of everything; the next list takes the first ID, and as it finishes both wake with 0
  KernelMachine w;
  constexpr u32 Woke = KernelMachine::Results;
  w.system.recompiler.enabled = false;
  w.system.power(0x0880'1000);
  for(u32 n : {0u, 1u}) {
    Assembler waiter{w, 0x0880'1000 + n * 0x100};
    if(n == 0) waiter.li(a0, Kernel::GeListIDs), waiter.li(a1, 0), waiter.call("sceGeListSync");
    else waiter.li(a0, 0), waiter.call("sceGeDrawSync");
    waiter.li(t0, Woke + n * 4); waiter.put(sw(v0, 0, t0));
    waiter.li(a0, 0); waiter.call("sceKernelExitThread");
  }
  ListWriter stalled{w.system.memory, ListA};
  stalled.put(GE::Finish); stalled.put(GE::End);
  w.system.memory.write(4, Woke, 0x1234), w.system.memory.write(4, Woke + 4, 0x1234);
  CHECK(w.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0}), Kernel::GeListIDs);
  for(u32 n : {0u, 1u}) {
    s32 uid = w.kernel.createThread(n ? "drawWaiter" : "listWaiter", 0x0880'1000 + n * 0x100, 0x10, 0x1000, 0, 0);
    w.kernel.startThread(*w.kernel.threads[uid], 0, 0);
  }
  w.kernel.run(100'000);
  auto waiting = [&] {
    u32 count = 0;
    for(auto& [uid, thread] : w.kernel.threads) count += thread->status == Kernel::Status::Waiting;
    return count;
  };
  CHECK(waiting(), 2u);
  CHECK(w.call("sceGeBreak", {1, 0}), 0);
  w.kernel.run(100'000);
  CHECK(waiting(), 2u);
  CHECK(w.call("sceGeListSync", {Kernel::GeListIDs, 1}), Kernel::ErrorInvalidID);
  CHECK(w.call("sceGeDrawSync", {1}), 0);
  w.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});  //the first ID again: the list waiter's wakes it
  w.kernel.run(100'000);
  CHECK(waiting(), 0u);
  CHECK(w.system.memory.read(4, Woke), 0u);
  CHECK(w.system.memory.read(4, Woke + 4), 0u);
}

//sceGeBreak(1) and the GE's callbacks. A FINISH whose interrupt waits (interrupts held off) when the break comes goes
//with its list, callback and all (gpu/ge/intrsuspend: none once they're back on), so the next list, which takes the
//same ID, has its own callback run once, first, the list done by then (sceGeDrawSync(1) 0, as for any last list's
//finish callback); its own FINISH waits for interrupts too, the list drawing meanwhile. And so does it when the break
//comes from a finish callback that's running, and enqueues the next list itself (unmeasured: pspautotests doesn't
//record a PSP at this). On both engines, the first with the state round trip while the new list's FINISH waits.
static auto geBreakCallbacks() -> void {
  constexpr u32 Gate = KernelMachine::Results + 0x40, Other = KernelMachine::Results + 0x44;
  for(bool fromCallback : {false, true}) for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    Assembler finish{m, 0x0880'4000};  //notes its id, whether drawing goes on (sceGeDrawSync(1)) and its turn
    finish.put(addiu(sp, sp, -16)); finish.put(sw(ra, 12, sp)); finish.put(sw(a0, 8, sp));
    finish.li(a0, 1); finish.call("sceGeDrawSync");
    finish.put(lw(a0, 8, sp));
    finish.li(t0, Finished); finish.put(sll(t1, a0, 4)); finish.put(addu(t0, t0, t1));
    finish.put(sw(a0, 0, t0)); finish.put(sw(v0, 4, t0));
    finish.li(t1, Turn); finish.put(lw(t2, 0, t1)); finish.put(addiu(t2, t2, 1)); finish.put(sw(t2, 0, t1));
    finish.put(sw(t2, 8, t0));
    finish.put(lw(ra, 12, sp)); finish.put(addiu(sp, sp, 16));
    finish.put(jr(ra));
    finish.put(nop);
    Assembler breaker{m, 0x0880'4200};  //breaks everything, then enqueues list B with the other callbacks
    breaker.put(addiu(sp, sp, -16)); breaker.put(sw(ra, 12, sp));
    breaker.li(a0, 1); breaker.li(a1, 0); breaker.call("sceGeBreak");
    breaker.li(t0, KernelMachine::Results); breaker.put(sw(v0, 0, t0));
    breaker.li(a0, ListB); breaker.li(a1, 0); breaker.li(t0, Other); breaker.put(lw(a2, 0, t0)); breaker.li(a3, 0);
    breaker.call("sceGeListEnQueue");
    breaker.li(t0, KernelMachine::Results + 4); breaker.put(sw(v0, 0, t0));
    breaker.put(lw(ra, 12, sp)); breaker.put(addiu(sp, sp, 16));
    breaker.put(jr(ra));
    breaker.put(nop);
    memory.write(4, Callbacks + 8, 0x0880'4000);
    memory.write(4, Callbacks + 0x18, 0x0880'4200);
    ListWriter a{memory, ListA};
    a.put(GE::Finish, 1); a.put(GE::End);
    ListWriter b{memory, ListB};
    b.put(GE::Finish, 2); b.put(GE::End);
    Assembler main{m, 0x0880'1000};
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.li(t0, Other); main.put(sw(v0, 0, t0));
    main.put(addu(s1, v0, zero));
    if(fromCallback) main.li(a0, Callbacks + 0x10), main.call("sceGeSetCallback"), main.put(addu(s1, v0, zero));
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");  //its FINISH met at once; its interrupt waits
    if(!fromCallback) {
      main.li(a0, 1); main.li(a1, 0); main.call("sceGeBreak");
      main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
      main.li(a0, ListB); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
      main.call("sceGeListEnQueue");  //the first ID again: its FINISH met too, its interrupt waiting
      main.li(t0, KernelMachine::Results + 4); main.put(sw(v0, 0, t0));
      u32 wait = main.here();
      main.li(t0, Gate); main.put(lw(t1, 0, t0));
      main.put(beq(t1, zero, s32(wait - main.here() - 4) / 4));
      main.put(nop);
    }
    main.put(addu(a0, s0, zero)); main.call("sceKernelCpuResumeIntr");
    main.li(a0, 0); main.call("sceGeDrawSync");
    main.li(t0, KernelMachine::Results + 8); main.put(sw(v0, 0, t0));
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0x0812'3456);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    if(!fromCallback) {
      m.kernel.run(Kernel::VblankCycles);
      CHECK(m.kernel.calls.size(), 0);  //(list A's FINISH went with it)
      CHECK(m.kernel.geFinishDue, true);
      s32 running = m.kernel.geRunning;
      bool drawing = running >= 0 && m.kernel.geLists[running].state == Kernel::GeList::State::Running;
      CHECK(drawing, true);
      CHECK(roundTrip(m), true);
      memory.write(4, Gate, 1);
    }
    m.kernel.run(Kernel::VblankCycles * 4);
    CHECK(m.kernel.exited, true);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
    CHECK(memory.read(4, KernelMachine::Results), 0);
    CHECK(memory.read(4, KernelMachine::Results + 4), Kernel::GeListIDs);
    CHECK(memory.read(4, KernelMachine::Results + 8), 0);
    CHECK(memory.read(4, Turn), 1);                //list B's callback alone ran,
    CHECK(memory.read(4, Finished + 32), 2);
    CHECK(memory.read(4, Finished + 36), 0);       //with list B done
    CHECK(memory.read(4, Finished + 40), 1);
    CHECK(memory.read(4, Finished + 16), 0);       //and list A's never
  }
}

//A display list drawing into a 512-wide 8888 frame buffer at VRAM's start, its scissor 480x272, in through mode:
//each PRIM's vertices (a color, and a 16-bit x, y and z: 12 bytes) written as it goes; then FINISH and END.
struct DrawingList {
  Memory& memory;
  ListWriter list;
  u32 vertices;
  DrawingList(Memory& memory, u32 at, u32 vertices) : memory(memory), list{memory, at}, vertices(vertices) {
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 512);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 271 << 10 | 479);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'011c);
  }
  auto draw(u32 kind, const std::vector<std::pair<s32, s32>>& corners) -> void {
    list.to(GE::VertexAddress, vertices);
    for(auto [x, y] : corners) {
      memory.write(4, vertices, 0xff00'00ff);
      memory.write(2, vertices + 4, u32(x)), memory.write(2, vertices + 6, u32(y)), memory.write(2, vertices + 8, 0);
      vertices += 12;
    }
    list.put(GE::Primitive, kind << 16 | u32(corners.size()));
  }
  auto finish(u32 id = 0) -> void { list.put(GE::Finish, id); list.put(GE::End); }
};

//A quiet function (kernel.cpp's Function::quiet) leaves the CPU's go as the kernel would have cut it: a program
//polling one (sceKernelGetSystemTimeLow) while a list (with no callback) finishes and starts the one queued behind,
//whose FINISH then falls due in a spin with no system call, sees that FINISH's callback at the same count of the spin
//as polling a function that isn't quiet but changes nothing either (sceKernelGetThreadExitStatus of no thread), on
//each engine. And the same with no list behind, the spin running to its end. (The FINISH taken at the end of a quiet
//function's syscall, which ends its compiled block, is followed by the block's check of the go's limit anyway: a
//program can't tell syscall()'s guard for it from none, so dispatch() is asked directly.)
static auto geQuietFunctions() -> void {
  constexpr u32 Count = KernelMachine::Results + 0x40, Seen = KernelMachine::Results + 0x50;
  auto spun = [&](const char* polled, bool recompile, bool behind) {
    KernelMachine m;
    auto& memory = m.system.memory;
    Assembler f{m, 0x0880'4000};  //the finish callback: the spin's count where its FINISH's argument says
    f.li(t0, Count); f.put(lw(t1, 0, t0));
    f.li(t0, Seen); f.put(sll(t2, a0, 2)); f.put(addu(t0, t0, t2)); f.put(sw(t1, 0, t0));
    f.put(jr(ra));
    f.put(nop);
    memory.write(4, Callbacks + 8, 0x0880'4000);
    DrawingList a{memory, ListA, Vertices};
    a.draw(GE::Sprites, {{0, 0}, {64, 32}});  //1536 cycles
    a.finish(1);
    DrawingList b{memory, ListB, Vertices + 0x1000};
    b.draw(GE::Sprites, {{0, 0}, {256, 64}});  //12288
    b.finish(2);
    Assembler main{m, 0x0880'1000};
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.put(addu(s1, v0, zero));
    for(u32 list : {ListA, ListB}) {  //(A without a callback: its FINISH starts no call)
      if(list == ListB && !behind) break;
      main.li(a0, list); main.li(a1, 0);
      if(list == ListA) main.li(a2, 0xffff'ffff);
      else main.put(addu(a2, s1, zero));
      main.li(a3, 0);
      main.call("sceGeListEnQueue");
    }
    main.li(s2, 400);  //(some 3200 cycles: A's FINISH taken at the end of one of these)
    u32 poll = main.here();
    main.li(a0, 0); main.call(polled);
    main.put(addiu(s2, s2, -1));
    main.put(bne(s2, zero, s32(poll - main.here() - 4) / 4));
    main.put(nop);
    main.li(t3, 0); main.li(t1, Count);
    u32 spin = main.here();
    main.put(addiu(t3, t3, 1));
    main.put(sw(t3, 0, t1));
    main.put(sltiu(t4, t3, 6000));
    main.put(bne(t4, zero, s32(spin - main.here() - 4) / 4));
    main.put(nop);
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    return std::vector<u32>{memory.read(4, Seen + 4), memory.read(4, Seen + 8), memory.read(4, Count)};
  };
  for(bool recompile : {false, true}) {
    for(bool behind : {true, false}) {
      auto quiet = spun("sceKernelGetSystemTimeLow", recompile, behind);
      CHECK(quiet == spun("sceKernelGetThreadExitStatus", recompile, behind), true);
      CHECK(quiet[0], 0u);  //A's FINISH calling nothing
      if(behind) CHECK(quiet[1] > 0 && quiet[1] < 6000, true);  //B's in the spin
      CHECK(quiet[2], 6000u);
    }
  }
  //Each quiet function is dispatched as one, and makes nothing due sooner: with a thread delayed, an alarm set, a
  //list's FINISH to come and the controller sampled on a cycle, the next thing's time is where it was after each. So
  //are the GE's looks, calm each time: sceGeDrawSync and sceGeListSync in mode 1, or a mode refused, or a list (one
  //out of range in any mode, 0 too, or a free list's ID in mode 1).
  KernelMachine m;
  auto& memory = m.system.memory;
  s32 uid = m.kernel.createThread("delayed", 0x0880'1000, 0x10, 0x1000, 0, 0);
  auto& delayed = *m.kernel.threads[uid];
  delayed.status = Kernel::Status::Waiting, delayed.wait = Kernel::Wait::Delay, delayed.wakeAt = 90'000;
  CHECK(m.call("sceKernelSetAlarm", {400, 0x0880'4000, 0}) > 0, true);
  DrawingList a{memory, ListA, Vertices};
  a.draw(GE::Sprites, {{0, 0}, {64, 32}});
  a.finish();
  u32 drawing = m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
  m.call("sceCtrlSetSamplingCycle", {5555});
  CHECK(m.kernel.geFinishDue && geDone(m) > m.kernel.cycles, true);
  CHECK(m.kernel.alarms.size(), 1u), CHECK(m.kernel.controller.cycle, 5555u);
  constexpr u32 Out = KernelMachine::Results + 0x200;
  memory.write(4, Out, 108), memory.write(4, Out + 0x80, 44);
  struct Call { const char* name; std::initializer_list<u32> arguments; };
  for(auto& call : {Call{"sceKernelGetThreadId", {}}, Call{"sceKernelReferThreadStatus", {u32(uid), Out}},
                    Call{"sceKernelReferThreadRunStatus", {u32(uid), Out + 0x80}},
                    Call{"sceKernelGetThreadCurrentPriority", {}}, Call{"sceDisplayGetVcount", {}},
                    Call{"sceKernelGetSystemTime", {Out + 0x100}}, Call{"sceKernelGetSystemTimeLow", {}},
                    Call{"sceKernelGetSystemTimeWide", {}}, Call{"sceRtcGetCurrentTick", {Out + 0x110}},
                    Call{"sceKernelSysClock2USec", {Out + 0x100, Out + 0x120, Out + 0x124}},
                    Call{"sceKernelSysClock2USecWide", {123'456'789, 0, Out + 0x128, Out + 0x12c}},
                    Call{"sceMpegRingbufferAvailableSize", {Out + 0x140}}, Call{"sceGeDrawSync", {1}},
                    Call{"sceGeListSync", {drawing, 1}}, Call{"sceGeDrawSync", {2}},
                    Call{"sceGeListSync", {drawing, 2}}, Call{"sceGeListSync", {0x7777, 1}},
                    Call{"sceGeListSync", {0x7777, 0}}, Call{"sceGeListSync", {Kernel::GeListIDs + 63, 1}}}) {
    u32 n = 0;
    for(u32 value : call.arguments) m.system.ipu.r[n < 4 ? 4 + n : 8 + n - 4] = value, n++;
    u64 next = m.kernel.cycles + m.kernel.untilNextEvent();
    bool quiet = false;
    m.kernel.dispatch(m.kernel.importCode("test", Kernel::nid(call.name)), quiet);
    if(!quiet || m.kernel.cycles + m.kernel.untilNextEvent() != next) std::printf("  %s\n", call.name);
    CHECK(quiet, true);
    CHECK(m.kernel.cycles + m.kernel.untilNextEvent(), next);
  }
  //But not one that isn't marked, nor one that took the list's FINISH as it ended, nor one that started a call into
  //the program.
  bool quiet = true;
  m.system.ipu.r[4] = 0;
  m.kernel.dispatch(m.kernel.importCode("test", Kernel::nid("sceKernelGetThreadExitStatus")), quiet);
  CHECK(quiet, false), CHECK(m.kernel.geFinishDue, true);
  auto timeLow = m.kernel.importCode("test", Kernel::nid("sceKernelGetSystemTimeLow"));
  m.kernel.cycles = geDone(m);
  quiet = true;
  m.kernel.dispatch(timeLow, quiet);
  CHECK(quiet, false), CHECK(m.kernel.geFinishDue, false);
  m.kernel.queueCall(0x0880'4000, 0, 1, 2, 3);
  quiet = true;
  m.kernel.dispatch(timeLow, quiet);
  CHECK(quiet, false), CHECK(m.kernel.interrupting, true);
  //Calls calm this time (Kernel::calm), dispatched as quiet, the next thing's time where it was: rotating the caller's
  //own line, alone in it, or another priority's, or none (an illegal priority); signalling a semaphore no thread waits
  //for, or past its most. Not rotating to an equal that takes the CPU, nor a signal waking a thread.
  KernelMachine k;
  s32 caller = k.kernel.createThread("caller", 0x0880'1000, 0x20, 0x1000, 0, 0);
  k.kernel.startThread(*k.kernel.threads[caller], 0, 0);
  k.kernel.reschedule();
  s32 sleeper = k.kernel.createThread("sleeper", 0x0880'2000, 0x30, 0x1000, 0, 0);
  auto& asleep = *k.kernel.threads[sleeper];
  asleep.status = Kernel::Status::Waiting, asleep.wait = Kernel::Wait::Delay, asleep.wakeAt = 80'000;
  CHECK(k.kernel.current == k.kernel.threads[caller].get(), true);
  s32 semaphore = s32(k.call("sceKernelCreateSema", {k.string("s"), 0, 0, 2, 0}));
  CHECK(semaphore > 0, true);
  auto calm = [&](const char* name, std::initializer_list<u32> arguments) {
    u32 n = 0;
    for(u32 value : arguments) k.system.ipu.r[n < 4 ? 4 + n : 8 + n - 4] = value, n++;
    u64 next = k.kernel.cycles + k.kernel.untilNextEvent();
    bool quiet = false;
    k.kernel.dispatch(k.kernel.importCode("test", Kernel::nid(name)), quiet);
    CHECK(k.kernel.cycles + k.kernel.untilNextEvent(), next);
    return quiet;
  };
  CHECK(calm("sceKernelRotateThreadReadyQueue", {0}), true);
  CHECK(calm("sceKernelRotateThreadReadyQueue", {0x30}), true);
  CHECK(calm("sceKernelRotateThreadReadyQueue", {0x100}), true);
  CHECK(calm("sceKernelSignalSema", {u32(semaphore), 1}), true);
  CHECK(calm("sceKernelSignalSema", {u32(semaphore), 5}), true);
  CHECK(k.kernel.current == k.kernel.threads[caller].get(), true);
  s32 equal = k.kernel.createThread("equal", 0x0880'3000, 0x20, 0x1000, 0, 0);
  k.kernel.startThread(*k.kernel.threads[equal], 0, 0);
  k.kernel.dispatchSuspended = true;  //(the caller keeps the CPU, its equal waiting)
  CHECK(calm("sceKernelRotateThreadReadyQueue", {0}), true);
  CHECK(k.kernel.current == k.kernel.threads[caller].get(), true);
  k.kernel.dispatchSuspended = false;
  CHECK(calm("sceKernelRotateThreadReadyQueue", {0}), false);
  CHECK(k.kernel.current == k.kernel.threads[equal].get(), true);
  asleep.wait = Kernel::Wait::Semaphore, asleep.waitID = semaphore, asleep.waitCount = 2, asleep.wakeAt = 0;
  CHECK(calm("sceKernelSignalSema", {u32(semaphore), 1}), false);
  CHECK(asleep.status == Kernel::Status::Ready, true);
  //A rotation handing the CPU to an equal woken from a delay only to run its callbacks: none notified, it goes back
  //to its delay at once and the caller runs again, but that delay's end now comes sooner than the go's (not calm).
  KernelMachine w;
  s32 rotating = w.kernel.createThread("rotating", 0x0880'1000, 0x20, 0x1000, 0, 0);
  w.kernel.startThread(*w.kernel.threads[rotating], 0, 0);
  w.kernel.reschedule();
  s32 woken = w.kernel.createThread("woken", 0x0880'2000, 0x20, 0x1000, 0, 0);
  auto& delayed2 = *w.kernel.threads[woken];
  delayed2.status = Kernel::Status::Ready, delayed2.readySince = 0;
  delayed2.wait = Kernel::Wait::Delay, delayed2.wakeAt = w.kernel.cycles + 5000;
  u64 next = w.kernel.cycles + w.kernel.untilNextEvent();
  w.system.ipu.r[4] = 0;
  bool still = true;
  w.kernel.dispatch(w.kernel.importCode("test", Kernel::nid("sceKernelRotateThreadReadyQueue")), still);
  CHECK(still, false);
  CHECK(w.kernel.current == w.kernel.threads[rotating].get(), true);
  CHECK(delayed2.status == Kernel::Status::Waiting, true);
  CHECK(w.kernel.cycles + w.kernel.untilNextEvent() < next, true);
}

//The GE's time (kernel/ge.cpp's geTime()): a list is done for the program only once a PSP's GE would have drawn it, by
//Sony's peak figures at the bus's clock (four pixels a clock, or 166/35 clocks a primitive, the longer), counted in
//the CPU's 333 MHz: a 64x32 sprite at the bus's first 111 MHz in 2048 * 35 * 333 / (140 * 111) = 1536 cycles, at
//166 MHz in 1027; a hundred one-pixel sprites in 100 * 664 * 333 / (140 * 166) = 951, their primitives the longer.
//The pixels are drawn at once; the list reads as drawing for sceGeListSync and sceGeDrawSync till then. A list
//queued behind is taken up as the GE is done with the one before; a list given in parts takes each up as it comes,
//or once the GE is done with the last, whichever is later. A list whose stall address is just past its END reads as
//drawing till then, not stalled. A PAUSE's list stops at the FINISH after it once that FINISH's time has come,
//keeping a stall address moved meanwhile; after a SYNC the GE goes on past the FINISH then. sceGeBreak(0) at a FINISH
//whose time hasn't come takes it first, and the FINISHes the lists after it meet as they start; and a state keeps a
//FINISH waiting.
static auto geTime() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  auto enqueue = [&](u32 at, u32 stall = 0) { return m.call("sceGeListEnQueue", {at, stall, 0xffff'ffff, 0}); };
  DrawingList a{memory, ListA, Vertices};
  a.draw(GE::Sprites, {{0, 0}, {64, 32}});
  a.finish();
  std::vector<std::pair<s32, s32>> dots;
  for(s32 n = 0; n < 100; n++) dots.push_back({n, 40}), dots.push_back({n + 1, 41});
  DrawingList b{memory, ListB, Vertices + 0x1000};
  b.draw(GE::Sprites, dots);
  b.finish();

  u64 start = m.kernel.cycles;
  u32 id = enqueue(ListA);
  CHECK(memory.read(4, VRAM + (31 * 512 + 63) * 4), 0xffu);  //drawn at once
  CHECK(memory.read(4, VRAM + (32 * 512 + 63) * 4), 0u);
  CHECK(geDone(m), start + 1536);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);  //drawing still
  CHECK(m.call("sceGeDrawSync", {1}), 2);
  CHECK(m.call("sceGeSaveContext", {Saved}), 0xffff'ffff);
  CHECK(roundTrip(m), true);
  m.kernel.run(1535);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  m.kernel.run(2);  //(the FINISH is taken as the kernel's loop goes round, past its time)
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeDrawSync", {1}), 0);
  id = enqueue(ListA, a.list.address);  //its stall address just past its END, as GU leaves it: drawing, not stalled
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  CHECK(m.call("sceGeDrawSync", {1}), 2);
  m.kernel.run(1540);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  CHECK(m.call("scePowerSetClockFrequency", {333, 333, 166}), 0);
  start = m.kernel.cycles;
  id = enqueue(ListA);
  CHECK(geDone(m), start + 1027);
  u32 behind = enqueue(ListB);
  CHECK(m.call("sceGeListSync", {behind, 1}), 1);  //queued
  m.kernel.run(1028);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeListSync", {behind, 1}), 2);  //taken up as the first was done with
  CHECK(geDone(m), start + 1027 + 951);
  m.kernel.run(951);
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);

  //in parts: the 64x32 sprite, then a 32x32 one (513 cycles), the second given before the first is drawn, then after
  DrawingList c{memory, ListA + 0x400, Vertices + 0x2000};
  c.draw(GE::Sprites, {{0, 0}, {64, 32}});
  u32 middle = c.list.address;
  c.draw(GE::Sprites, {{100, 0}, {132, 32}});
  c.finish();
  for(bool late : {false, true}) {
    id = enqueue(ListA + 0x400, ListA + 0x400);
    CHECK(m.call("sceGeListSync", {id, 1}), 3);
    u64 first = m.kernel.cycles;
    CHECK(m.call("sceGeListUpdateStallAddr", {id, middle}), 0);
    CHECK(geDone(m), first + 1027);
    m.kernel.cycles += late ? 2000 : 1000;  //(time passing, which nothing waits on while the list stalls)
    u64 second = m.kernel.cycles;
    CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
    CHECK(geDone(m), late ? second + 513 : first + 1027 + 513);
    m.kernel.run(2000);
    CHECK(m.call("sceGeListSync", {id, 1}), 0);
  }

  //a PAUSE, then the 64x32 sprite and the FINISH it pauses at, as far as the stall address; the rest after it
  DrawingList d{memory, ListA + 0x800, Vertices + 0x3000};
  d.list.put(GE::Signal, 0x03'0000);
  d.list.put(GE::End);
  d.draw(GE::Sprites, {{0, 0}, {64, 32}});
  d.finish();
  u32 pause = d.list.address;
  d.draw(GE::Sprites, {{200, 0}, {208, 8}});
  d.finish();
  id = enqueue(ListA + 0x800, pause);
  CHECK(m.kernel.geFinishDue, true);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceGeContinue", {}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  m.kernel.run(1028);
  CHECK(m.call("sceGeListSync", {id, 1}), 4);  //paused
  CHECK(m.call("sceGeContinue", {}), 0);
  CHECK(memory.read(4, VRAM + (7 * 512 + 207) * 4), 0xffu);  //the rest, past the stall address moved
  m.kernel.run(100);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  //a SYNC: the FINISH after it doesn't end the list, and the GE goes on past it once it's taken
  DrawingList s{memory, ListA + 0xc00, Vertices + 0x4000};
  s.list.put(GE::Signal, 0x08'0000);
  s.list.put(GE::End);
  s.draw(GE::Sprites, {{0, 50}, {64, 82}});
  s.finish();
  s.draw(GE::Sprites, {{300, 0}, {308, 8}});
  s.finish();
  id = enqueue(ListA + 0xc00);
  CHECK(memory.read(4, VRAM + (81 * 512 + 63) * 4), 0xffu);
  CHECK(memory.read(4, VRAM + (7 * 512 + 307) * 4), 0u);
  m.kernel.run(1028);
  CHECK(memory.read(4, VRAM + (7 * 512 + 307) * 4), 0xffu);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  m.kernel.run(100);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  id = enqueue(ListA);
  CHECK(m.kernel.geFinishDue, true);
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);  //its FINISH taken first: nothing left to break off
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.kernel.geFinishDue, false);
  id = enqueue(ListA);
  behind = enqueue(ListB);
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);  //the list behind it met its own as it started
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);
  CHECK(m.kernel.geFinishDue, false);
  id = enqueue(ListA);
  behind = enqueue(ListB);
  u32 stalled = enqueue(ListA + 0x400, ListA + 0x400);
  CHECK(m.call("sceGeBreak", {0, 0}), stalled);  //the two FINISHes before it taken, the third broken off at its stall
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);
  CHECK(m.call("sceGeListSync", {stalled, 1}), 4);
  CHECK(m.kernel.geFinishDue, false);
  CHECK(roundTrip(m), true);
}

//The GE's time with the option "GE Timing" "PSP" (kernel/ge.cpp's geTime(), GE::Cost, GE::Pipe): a list is done for
//the program only once a PSP's GE would have drawn it, as the owner's PSP took its time (docs/psp-core.md, parts 76
//and 106), in the bus's clocks, counted in the CPU's 333 MHz; and sceGeListEnQueue takes the driver's own 5200 of the
//CPU's cycles, the GE drawing meanwhile (7800 of the 333 MHz count at the CPU's first 222 MHz, 5200 at 333),
//sceGeListUpdateStallAddr 510. A list starting takes 2350 clocks of its own; then list A's front end reads its first
//10 words, takes the PRIM and fetches the sprite's two 12-byte vertices (10 * 2.34 + 60.2 + 24 * 0.513 = 95.912) and
//hands the 256x256 sprite on, which the back end draws in 256 rows * 256 * 0.542 = 35520.512 (the FINISH and END its
//front end's meanwhile): 37966.424 clocks, at the bus's first 111 MHz 113899 cycles, at 166 MHz 76161. List B's
//hundred one-pixel sprites, one PRIM of 200 vertices: the first handed on at 2445.912 clocks, each next 12.312 later
//(its share of the vertices), each drawn in a row's least, 1.82, then the FINISH and END: 3669.48, at 166 MHz 7361
//cycles. In parts, the list's start done meanwhile: a 64x32 sprite with the list's first 10 words, 95.912, then 32 rows
//* 64 * 0.542 = 1110.016, 2419 cycles; a 32x32 one after it, its 3 words, the PRIM and its vertices 79.532, then
//32 rows * 32 * 0.334 = 342.016, after the first if that's still drawing; an 8x8 one with its list's words 95.912 and
//8 rows of 8 * 0.334, 21.376 more, 235 cycles. A list a PAUSE or SYNC stops runs to the SIGNAL (9 words), then from
//when its interrupt is taken (as a FINISH's: as the call returns, the driver's 5200 cycles in) its 256x256 sprite,
//79.532 + 35520.512 clocks, 71414 cycles; what's after its FINISH, an 8x8 sprite, 202 cycles more.
//The pixels are drawn at once; the list reads as drawing for sceGeListSync and sceGeDrawSync till then. A list
//queued behind is taken up as the GE is done with the one before; a list given in parts takes each up as it comes,
//or once the GE is done with the last, whichever is later. A list whose stall address is just past its END reads as
//drawing till then, not stalled. A PAUSE's list stops at the FINISH after it once that FINISH's time has come,
//keeping a stall address moved meanwhile; after a SYNC the GE goes on past the FINISH then. A SIGNAL stops the GE
//till its front end gets there, its words and vertices done, the queue (GE::Cost::Queue's 83 entries, a sprite three)
//holding the drawing still to come: a list's SIGNAL after one sprite comes as the call returns; after 24 sprites of
//64x32 in one PRIM, 5484 cycles in, the drawing going on to 58348; without the PSP's timing the GE goes past it at
//once. sceGeBreak(0) at a FINISH whose time hasn't come takes it first, and the FINISHes the lists after it meet as
//they start; and a state keeps a FINISH or a SIGNAL waiting.
//With the PSP's timing, a list's pipeline goes on from one of its runs to the next (Kernel::geResume()): 24 sprites of
//64x32, a SIGNAL that lets the GE go on, the FINISH, given whole, as one part and then the SIGNAL's, and a part for
//each sprite (as GU moves its stall address after each draw). However it's given, the SIGNAL's interrupt comes once
//the front end gets there, long before the drawing's end (the queue holds all 24 sprites, three entries each; a
//sprite's front-end work is a fourteenth of its drawing): whole, 8585 cycles in (the list's start, its 9 words and
//each sprite's 3 words, PRIM and vertices: 4279.828 clocks), the drawing to 58347; given in parts, as late as the last
//stall update lets the front end get there (a part a sprite: as that update's call returns, 17950 cycles in), the
//drawing to 58833. Before, a run's end emptied the queue, and a list given in parts had its SIGNAL at its drawing's end.
static auto geTimeSignalAcrossRuns() -> void {
  u64 reaches[3] = {}, dones[3] = {};
  for(u32 parts : {0u, 1u, 24u}) {
    KernelMachine m;
    m.kernel.gePspTiming = true;
    m.system.threadGE(false);  //(the PSP's GE timing keeps the GE on the emulation thread: System::threadGE())
    auto& memory = m.system.memory;
    CHECK(m.call("scePowerSetClockFrequency", {333, 333, 166}), 0);
    DrawingList q{memory, ListA + 0x1800, Vertices + 0x7000};
    std::vector<u32> ends;
    for(u32 n = 0; n < 24; n++) {
      q.draw(GE::Sprites, {{0, 0}, {64, 32}});
      ends.push_back(q.list.address);
    }
    q.list.put(GE::Signal, 0x02'0007);
    q.list.put(GE::End);
    q.finish();
    u32 end = q.list.address;
    u64 start = m.kernel.cycles;
    if(parts == 0) {
      m.call("sceGeListEnQueue", {ListA + 0x1800, 0, 0xffff'ffff, 0});
    } else {
      u32 id = m.call("sceGeListEnQueue", {ListA + 0x1800, ListA + 0x1800, 0xffff'ffff, 0});
      if(parts == 1) CHECK(m.call("sceGeListUpdateStallAddr", {id, ends[23]}), 0);
      else for(u32 n = 0; n < 24; n++) CHECK(m.call("sceGeListUpdateStallAddr", {id, ends[n]}), 0);
      CHECK(m.call("sceGeListUpdateStallAddr", {id, end}), 0);
    }
    u32 at = parts == 0 ? 0 : parts == 1 ? 1 : 2;
    //(when its interrupt comes: due at the front end's reach, or taken already, by now, as the last call returned)
    reaches[at] = (m.kernel.geSignalDue ? m.kernel.geReachAt : m.kernel.cycles) - start;
    dones[at] = m.kernel.geDoneAt - start;
  }
  for(u32 n = 0; n < 3; n++) CHECK(reaches[n] * 3 < dones[n], true);  //(the lead: most of the drawing still ahead)
  CHECK(reaches[0] <= reaches[1] && reaches[1] <= reaches[2], true);
  for(u32 n : {1u, 2u}) CHECK(std::llabs(s64(dones[n] - dones[0])) < s64(dones[0] / 50), true);
  CHECK(reaches[0], 8585);
  CHECK(dones[0], 58347);
  CHECK(reaches[1], 9071);
  CHECK(dones[1], 58833);
  CHECK(reaches[2], 17950);
  CHECK(dones[2], 58833);
}

static auto geTimePsp() -> void {
  KernelMachine m;
  m.kernel.gePspTiming = true;
  m.system.threadGE(false);  //(the PSP's GE timing keeps the GE on the emulation thread: System::threadGE())
  auto& memory = m.system.memory;
  auto enqueue = [&](u32 at, u32 stall = 0) { return m.call("sceGeListEnQueue", {at, stall, 0xffff'ffff, 0}); };
  auto past = [&](u64 time) { m.kernel.run(time + 1 - m.kernel.cycles); };  //(the kernel's loop gone round past it)
  DrawingList a{memory, ListA, Vertices};
  a.draw(GE::Sprites, {{0, 0}, {256, 256}});
  a.finish();
  std::vector<std::pair<s32, s32>> dots;
  for(s32 n = 0; n < 100; n++) dots.push_back({n, 40}), dots.push_back({n + 1, 41});
  DrawingList b{memory, ListB, Vertices + 0x1000};
  b.draw(GE::Sprites, dots);
  b.finish();

  u64 start = m.kernel.cycles;
  u32 id = enqueue(ListA);
  CHECK(memory.read(4, VRAM + (255 * 512 + 255) * 4), 0xffu);  //drawn at once
  CHECK(memory.read(4, VRAM + 256 * 4), 0u);
  CHECK(m.kernel.geDoneAt, start + 113899);
  CHECK(m.kernel.cycles, start + 7800);  //(the driver's own time in the call)
  CHECK(m.call("sceGeListSync", {id, 1}), 2);  //drawing still
  CHECK(m.call("sceGeDrawSync", {1}), 2);
  CHECK(m.call("sceGeSaveContext", {Saved}), 0xffff'ffff);
  CHECK(roundTrip(m), true);
  m.kernel.run(start + 113899 - 1 - m.kernel.cycles);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  m.kernel.run(2);  //(the FINISH is taken as the kernel's loop goes round, past its time)
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeDrawSync", {1}), 0);
  start = m.kernel.cycles;
  id = enqueue(ListA, a.list.address);  //its stall address just past its END, as GU leaves it: drawing, not stalled
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  CHECK(m.call("sceGeDrawSync", {1}), 2);
  past(start + 113899);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  CHECK(m.call("scePowerSetClockFrequency", {333, 333, 166}), 0);
  start = m.kernel.cycles;
  id = enqueue(ListA);
  CHECK(m.kernel.geDoneAt, start + 76161);
  u32 behind = enqueue(ListB);
  CHECK(m.call("sceGeListSync", {behind, 1}), 1);  //queued
  past(start + 76161);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeListSync", {behind, 1}), 2);  //taken up as the first was done with
  CHECK(m.kernel.geDoneAt, start + 76161 + 7361);
  past(start + 76161 + 7361);
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);

  //in parts: a 64x32 sprite, then a 32x32 one, the second given before the first is drawn, then after
  DrawingList c{memory, ListA + 0x400, Vertices + 0x2000};
  c.draw(GE::Sprites, {{0, 0}, {64, 32}});
  u32 middle = c.list.address;
  c.draw(GE::Sprites, {{100, 0}, {132, 32}});
  c.finish();
  for(bool late : {false, true}) {
    id = enqueue(ListA + 0x400, ListA + 0x400);
    CHECK(m.call("sceGeListSync", {id, 1}), 3);
    u64 first = m.kernel.cycles;
    CHECK(m.call("sceGeListUpdateStallAddr", {id, middle}), 0);
    CHECK(m.kernel.geDoneAt, first + 2419);
    CHECK(m.kernel.cycles, first + 510);  //(the driver's own time in the call)
    m.kernel.cycles += late ? 3000 : 1000;  //(time passing, which nothing waits on while the list stalls)
    u64 second = m.kernel.cycles;
    CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
    //(late, the first drawn by then: 79.532 + 342.016 clocks; else after the first, still drawing: 2419 + 686)
    CHECK(m.kernel.geDoneAt, late ? second + 845 : first + 3105);
    m.kernel.run(4000);
    CHECK(m.call("sceGeListSync", {id, 1}), 0);
  }
  //a list stalled before its last words, moved on to its end: done inside the call, its 8x8 sprite's 235 cycles
  //within the driver's 510, so done as the call returns (Cube queues the same list again just after: busy, else)
  DrawingList e{memory, ListA + 0x1000, Vertices + 0x5000};
  e.draw(GE::Sprites, {{0, 0}, {8, 8}});
  e.finish();
  id = enqueue(ListA + 0x1000, ListA + 0x1000);
  CHECK(m.call("sceGeListSync", {id, 1}), 3);  //at its stall address
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  //a PAUSE, then the 256x256 sprite and the FINISH it pauses at, as far as the stall address; the rest after it
  DrawingList d{memory, ListA + 0x800, Vertices + 0x3000};
  d.list.put(GE::Signal, 0x03'0000);
  d.list.put(GE::End);
  d.draw(GE::Sprites, {{0, 0}, {256, 256}});
  d.finish();
  u32 pause = d.list.address;
  d.draw(GE::Sprites, {{400, 0}, {408, 8}});
  d.finish();
  start = m.kernel.cycles;
  id = enqueue(ListA + 0x800, pause);
  CHECK(m.kernel.geFinishDue, true);
  //(the PAUSE's interrupt, due 4756 cycles in, taken as the call returns after the driver's 5200, as a FINISH's
  //would be: the GE goes on from there)
  CHECK(m.kernel.geDoneAt, start + 5200 + 71414);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceGeContinue", {}), Kernel::ErrorBusy);
  CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
  past(start + 5200 + 71414);
  CHECK(m.call("sceGeListSync", {id, 1}), 4);  //paused
  CHECK(m.call("sceGeContinue", {}), 0);
  CHECK(memory.read(4, VRAM + (7 * 512 + 407) * 4), 0xffu);  //the rest, past the stall address moved
  m.kernel.run(300);  //(its 8x8 sprite's 202 cycles)
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  //a SYNC: the FINISH after it doesn't end the list, and the GE goes on past it once it's taken
  DrawingList s{memory, ListA + 0xc00, Vertices + 0x4000};
  s.list.put(GE::Signal, 0x08'0000);
  s.list.put(GE::End);
  s.draw(GE::Sprites, {{0, 0}, {256, 256}});
  s.finish();
  s.draw(GE::Sprites, {{300, 0}, {308, 8}});
  s.finish();
  start = m.kernel.cycles;
  id = enqueue(ListA + 0xc00);
  CHECK(memory.read(4, VRAM + (7 * 512 + 307) * 4), 0u);
  CHECK(m.kernel.geDoneAt, start + 5200 + 71414);  //(as the PAUSE's)
  past(start + 5200 + 71414);
  CHECK(memory.read(4, VRAM + (7 * 512 + 307) * 4), 0xffu);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  m.kernel.run(300);  //(its 8x8 sprite's 202 cycles)
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  //a SIGNAL after the 256x256 sprite (kind 2: the GE goes on as its callback runs): the front end gets there with the
  //list's start, its first 12 words, the PRIM and two vertices, 2450.592 clocks, 4916 cycles, the sprite handed on, so
  //it's taken as the call returns (round 6's time-signals had the callback 77 microseconds in, after 16 such sprites);
  //the FINISH at the sprite's time
  DrawingList g{memory, ListA + 0x1400, Vertices + 0x6000};
  g.draw(GE::Sprites, {{0, 0}, {256, 256}});
  g.list.put(GE::Signal, 0x02'0007);
  g.list.put(GE::End);
  g.finish();
  start = m.kernel.cycles;
  id = enqueue(ListA + 0x1400);
  CHECK(m.kernel.geSignalDue, false);
  CHECK(m.kernel.geFinishDue, true);
  CHECK(m.kernel.geDoneAt, start + 76161);  //(then the FINISH's two words, as the GE went on)
  past(m.kernel.geDoneAt);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  //after 24 sprites of 64x32 in one PRIM, all of them in the queue (GE::Cost::Queue), the front end gets there with
  //the list's start, its 12 words, the PRIM and each sprite's share of the vertices, 2733.768 clocks, 5484 cycles,
  //past the call's 5200; the list drawing meanwhile and a state keeping it so; then the GE goes on to the FINISH,
  //taken at the sprites' time, 58348 cycles in (Army of Two's last SIGNAL came 0.27 ms before its list's FINISH on
  //the owner's PSP)
  std::vector<std::pair<s32, s32>> tiles;
  for(u32 n = 0; n < 24; n++) tiles.push_back({0, 0}), tiles.push_back({64, 32});
  DrawingList q{memory, ListA + 0x1800, Vertices + 0x7000};
  q.draw(GE::Sprites, tiles);
  q.list.put(GE::Signal, 0x02'0007);
  q.list.put(GE::End);
  q.finish();
  start = m.kernel.cycles;
  id = enqueue(ListA + 0x1800);
  CHECK(m.kernel.geReachAt, start + 5484);
  CHECK(m.kernel.geSignalDue, true);
  CHECK(m.kernel.geFinishDue, false);
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  CHECK(roundTrip(m), true);
  CHECK(m.kernel.geSignalDue, true);
  m.kernel.run(start + 5484 - 1 - m.kernel.cycles);
  CHECK(m.kernel.geSignalDue, true);
  m.kernel.run(2);
  CHECK(m.kernel.geSignalDue, false);
  CHECK(m.kernel.geFinishDue, true);  //(the FINISH, met as the GE went on)
  CHECK(m.kernel.geDoneAt >= start + 58347, true);  //(the 24 sprites' 26640.384 clocks, after the first's handing on)
  CHECK(m.call("sceGeListSync", {id, 1}), 2);
  past(m.kernel.geDoneAt);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  {  //without the PSP's timing the GE goes past the SIGNAL at once, to the FINISH
    KernelMachine fast;
    DrawingList h{fast.system.memory, ListA, Vertices};
    h.draw(GE::Sprites, {{0, 0}, {256, 256}});
    h.list.put(GE::Signal, 0x02'0007);
    h.list.put(GE::End);
    h.finish();
    fast.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    CHECK(fast.kernel.geSignalDue, false);
    CHECK(fast.kernel.geFinishDue, true);
  }

  //a list put at the head of an empty queue waits, paused, for sceGeContinue, which starts it: a list's start too
  id = m.call("sceGeListEnQueueHead", {ListA, 0, 0xffff'ffff, 0});
  CHECK(m.call("sceGeListSync", {id, 1}), 4);
  start = m.kernel.cycles;
  CHECK(m.call("sceGeContinue", {}), 0);
  CHECK(m.kernel.geDoneAt, start + 76161);
  past(start + 76161);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);

  id = enqueue(ListA);
  CHECK(m.kernel.geFinishDue, true);
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);  //its FINISH taken first: nothing left to break off
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.kernel.geFinishDue, false);
  id = enqueue(ListA);
  behind = enqueue(ListB);
  CHECK(m.call("sceGeBreak", {0, 0}), Kernel::ErrorAlready);  //the list behind it met its own as it started
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);
  CHECK(m.kernel.geFinishDue, false);
  id = enqueue(ListA);
  behind = enqueue(ListB);
  u32 stalled = enqueue(ListA + 0x400, ListA + 0x400);
  CHECK(m.call("sceGeBreak", {0, 0}), stalled);  //the two FINISHes before it taken, the third broken off at its stall
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeListSync", {behind, 1}), 0);
  CHECK(m.call("sceGeListSync", {stalled, 1}), 4);
  CHECK(m.kernel.geFinishDue, false);
  CHECK(roundTrip(m), true);
}

//The GE's FINISH comes to the program as an interrupt (kernel/ge.cpp's geInterrupt()), as pspautotests recorded: with
//interrupts held off the list stays drawing past its time, its callback once they're back on (gpu/ge/intrsuspend);
//a SIGNAL's callback that lets the GE go on finds its list drawing, though the GE has run its FINISH (
//gpu/signals/continue); a thread waiting in sceGeDrawSync wakes when the GE's time has come, here a 480x272 clear
//at 111 MHz: 130560 * 35 * 333 / (140 * 111) = 97920 cycles, 294 microseconds; and a FINISH whose time comes as an
//interrupt's handler runs is taken once the handler has returned. On both engines.
static auto geTimeInterrupts() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    memory.write(4, Callbacks + 8, recorder(m, 0x0880'4100, Finished));
    DrawingList list{memory, ListA, Vertices};
    list.draw(GE::Sprites, {{0, 0}, {64, 32}});
    list.finish(1);
    Assembler main{m, 0x0880'1000};
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.put(addu(s1, v0, zero));
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.put(addu(s2, v0, zero));
    main.li(t3, 2000);  //three instructions a round: some 6000 cycles, past the list's 1536
    u32 spin = main.here();
    main.put(addiu(t3, t3, -1));
    main.put(bne(t3, zero, s32(spin - main.here() - 4) / 4));
    main.put(nop);
    main.put(addu(a0, s2, zero)); main.li(a1, 1); main.call("sceGeListSync");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.li(a0, 1); main.call("sceGeDrawSync");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 4, t0));
    main.li(t1, Turn); main.put(lw(t2, 0, t1)); main.put(sw(t2, 8, t0));
    main.put(addu(a0, s0, zero)); main.call("sceKernelCpuResumeIntr");
    main.put(addu(a0, s2, zero)); main.li(a1, 1); main.call("sceGeListSync");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 12, t0));
    main.li(t1, Turn); main.put(lw(t2, 0, t1)); main.put(sw(t2, 16, t0));
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    CHECK(memory.read(4, KernelMachine::Results), 2);       //drawing, interrupts held off
    CHECK(memory.read(4, KernelMachine::Results + 4), 2);
    CHECK(memory.read(4, KernelMachine::Results + 8), 0);   //no callback yet
    CHECK(memory.read(4, KernelMachine::Results + 12), 0);  //done once they're back on,
    CHECK(memory.read(4, KernelMachine::Results + 16), 1);  //its callback run
    CHECK(memory.read(4, Finished), 1);
  }
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    auto seeing = [&](u32 at, u32 slot) {  //notes sceGeListSync(the first list, 1) and sceGeDrawSync(1)
      Assembler f{m, at};
      f.put(addiu(sp, sp, -16)); f.put(sw(ra, 12, sp));
      f.li(a0, Kernel::GeListIDs); f.li(a1, 1); f.call("sceGeListSync");
      f.li(t0, slot); f.put(sw(v0, 0, t0));
      f.li(a0, 1); f.call("sceGeDrawSync");
      f.li(t0, slot); f.put(sw(v0, 4, t0));
      f.put(lw(ra, 12, sp)); f.put(addiu(sp, sp, 16));
      f.put(jr(ra));
      f.put(nop);
      return at;
    };
    memory.write(4, Callbacks, seeing(0x0880'4000, Signaled));
    memory.write(4, Callbacks + 8, seeing(0x0880'4200, Finished));
    ListWriter list{memory, ListA};
    list.put(GE::Signal, 0x02'0007);
    list.put(GE::End);
    list.put(GE::Finish);
    list.put(GE::End);
    Assembler main{m, 0x0880'1000};
    main.li(a0, 0x0606'0010); main.call("sceKernelSetCompiledSdkVersion");
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, v0, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(a0, 0); main.call("sceGeDrawSync");
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    CHECK(memory.read(4, Signaled), 2);  //the signal's callback: drawing still
    CHECK(memory.read(4, Signaled + 4), 2);
    CHECK(memory.read(4, Finished), 0);  //the finish callback: done, the last
    CHECK(memory.read(4, Finished + 4), 0);
  }
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto& memory = m.system.memory;
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 512);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor2, 271 << 10 | 479);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::ClearMode, 0x101);
    list.put(GE::VertexType, 0x80'011c);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    memory.write(2, Vertices + 16, 480), memory.write(2, Vertices + 18, 272);
    Assembler main{m, 0x0880'1000};
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(a0, ListA); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(a0, 0); main.call("sceGeDrawSync");
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s0));
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    u32 waited = memory.read(4, KernelMachine::Results);
    CHECK(waited >= 294 && waited <= 295, true);
  }
  for(bool recompile : {false, true}) {  //the clear's FINISH falling due as an alarm's handler runs
    KernelMachine m;
    auto& memory = m.system.memory;
    memory.write(4, Callbacks + 8, recorder(m, 0x0880'4100, Finished));
    DrawingList list{memory, ListA, Vertices};
    list.list.put(GE::ClearMode, 0x101);
    list.draw(GE::Sprites, {{0, 0}, {480, 272}});
    list.finish();
    Assembler handler{m, 0x0880'3000};
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    handler.li(t3, 40000);  //three instructions a round: some 120000 cycles, past the clear's 97920
    u32 spin = handler.here();
    handler.put(addiu(t3, t3, -1));
    handler.put(bne(t3, zero, s32(spin - handler.here() - 4) / 4));
    handler.put(nop);
    handler.call("sceKernelGetSystemTimeLow");
    handler.li(t0, KernelMachine::Results); handler.put(sw(v0, 16, t0));
    handler.li(a0, Kernel::GeListIDs); handler.li(a1, 1); handler.call("sceGeListSync");
    handler.li(t0, KernelMachine::Results); handler.put(sw(v0, 0, t0));
    handler.li(t1, Turn); handler.put(lw(t2, 0, t1)); handler.put(sw(t2, 4, t0));
    handler.put(lw(ra, 12, sp));
    handler.li(v0, 0);  //not again
    handler.put(jr(ra));
    handler.put(addiu(sp, sp, 16));
    Assembler main{m, 0x0880'1000};
    main.li(a0, Callbacks); main.call("sceGeSetCallback");
    main.put(addu(s1, v0, zero));
    main.call("sceKernelGetSystemTimeLow");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 12, t0));
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.li(a0, 10); main.li(a1, 0x0880'3000); main.li(a2, 0); main.call("sceKernelSetAlarm");
    main.li(a0, 2000); main.call("sceKernelDelayThread");
    main.li(a0, Kernel::GeListIDs); main.li(a1, 1); main.call("sceGeListSync");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 8, t0));
    main.call("sceKernelExitGame");
    runMain(m, recompile);
    u32 waited = memory.read(4, KernelMachine::Results + 16) - memory.read(4, KernelMachine::Results + 12);
    CHECK(waited > 294, true);                             //(the clear's 294 microseconds past)
    CHECK(memory.read(4, KernelMachine::Results), 2);      //in the handler, past the GE's time: drawing still,
    CHECK(memory.read(4, KernelMachine::Results + 4), 0);  //its callback not run
    CHECK(memory.read(4, Finished + 16), 1);               //that once the handler had returned
    CHECK(memory.read(4, KernelMachine::Results + 8), 0);
  }
}

//Vertices read from VRAM that a sprite just before them in the list draws over (two points, through mode, an 8888
//color and 16-bit positions, 12 bytes each, which the sprite's color 0x00200010 puts at (16, 32), where memory had
//them at (8, 8)): what waits to be drawn over them is drawn first, as at one thread, with batches shared out however
//small; for vertices in a row (all asked for at once: readVertices()), the first of them in the page before too, and
//by indices, also past the PRIM's count of vertices from the first. Likewise a list's words its own sprite draws over
//(run(): a page found undrawn is asked about again after a PRIM).
static auto geVerticesDrawn() -> void {
  constexpr u32 Drawn = 0x15'4000, Indices = 0x0899'0000;  //(Drawn: VRAM offset)
  u32 first = 0;  //(the first vertex's bytes before the sprite's first pixel)
  auto build = [&](Memory& memory, bool indexed) {
    for(u32 n = 0; n < 68; n++) memory.write(4, VRAM + Drawn - 16 + n * 4, 0x0008'0008);
    memory.write(1, Indices, first / 12 + 1), memory.write(1, Indices + 1, first / 12);  //(the sprite's two)
    ListWriter list{memory, ListA};
    u32 vertex = Vertices;
    for(float value : {0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    memory.write(4, vertex, 0), vertex += 4;
    for(float value : {0.0f, 0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    for(float value : {0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    memory.write(4, vertex, 0x0020'0010), vertex += 4;
    for(float value : {8.0f, 1.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    list.put(GE::FrameBufferPointer, Drawn), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::FrameBufferPointer, 0), list.put(GE::FrameBufferWidth, 512);
    list.put(GE::VertexType, 0x80'011c | (indexed ? 1 << 11 : 0));
    list.to(GE::VertexAddress, VRAM + Drawn - first);
    if(indexed) list.to(GE::IndexAddress, Indices);
    list.put(GE::Primitive, GE::Points << 16 | 2);
    list.put(GE::Finish), list.put(GE::End);
  };
  auto drawn = [&](bool indexed, u32 threads) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    build(m.system.memory, indexed);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.memory.vram;
  };
  for(bool indexed : {false, true}) {
    auto whole = drawn(indexed, 1);
    CHECK(whole[(32 * 512 + 16) * 4], u8(0x10));  //(the point drawn where the sprite put it)
    for(u32 threads : {2u, 4u, 8u}) CHECK(drawn(indexed, threads) == whole, true);
  }
  //(the first vertex in the page before, which nothing draws, at (8, 8); the second, the sprite's, in the next)
  first = 12;
  auto whole = drawn(false, 1);
  CHECK(whole[(8 * 512 + 8) * 4], u8(0x08)), CHECK(whole[(32 * 512 + 16) * 4], u8(0x10));
  for(u32 threads : {2u, 4u, 8u}) CHECK(drawn(false, threads) == whole, true);
  //(by indices 3 and 2, the sprite's: the PRIM's two vertices from the first are in the page before, not read)
  first = 24;
  whole = drawn(true, 1);
  CHECK(whole[(32 * 512 + 16) * 4], u8(0x10));
  for(u32 threads : {2u, 4u, 8u}) CHECK(drawn(true, threads) == whole, true);
  //And a list in VRAM whose own sprite draws over its words further on in the page, reached by a JUMP: AMBIENT_COLOR
  //words whose low 24 bits the sprite fills (its alpha the stencil, the frame buffer's own), run as drawn.
  auto listed = [&](u32 threads) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    auto& memory = m.system.memory;
    u32 vertex = Vertices;
    for(float value : {0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    memory.write(4, vertex, 0), vertex += 4;
    for(float value : {0.0f, 1.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    for(float value : {0.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    memory.write(4, vertex, 0xff00'ff40), vertex += 4;
    for(float value : {4.0f, 2.0f, 0.0f}) memory.write(4, vertex, std::bit_cast<u32>(value)), vertex += 4;
    ListWriter list{memory, VRAM + Drawn};
    list.put(GE::FrameBufferPointer, Drawn), list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor2, 479 | 271 << 10), list.put(GE::Region2, 479 | 271 << 10);
    list.put(GE::VertexType, 0x80'019f), list.put(GE::ShadeMode, 1);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.to(GE::Jump, VRAM + Drawn + 64 * 4);
    ListWriter after{memory, VRAM + Drawn + 64 * 4};  //(row 1, which the sprite draws)
    for(u32 n = 0; n < 4; n++) after.put(GE::AmbientColor);
    after.put(GE::Finish), after.put(GE::End);
    m.call("sceGeListEnQueue", {VRAM + Drawn, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.ge.commands[GE::AmbientColor];
  };
  for(u32 threads : {1u, 2u, 4u, 8u}) CHECK(listed(threads), u32(GE::AmbientColor << 24 | 0x00'ff40));
}

//Vertices read in a run (readVertices() reading a part at a time over all of them, from the host's bytes) are those
//read one at a time (GE::vertexRuns off), field for field and bit for bit: every vertex type but morph targets (which
//are read one at a time), through mode and 3D, at random over random bytes (a quarter of the floats infinities or not
//numbers), in a row and by indices of each size (the largest index's vertex last in RAM, then a byte past its end,
//which leaves them one at a time), and in a row in VRAM, through its plain copy and the one that rearranges it. (The
//runs counted are those readVertices() read so: GE::vertexRunsRead.)
static auto geVertexRuns() -> void {
  System s;
  auto& ge = s.ge;
  auto& memory = s.memory;
  std::mt19937 random{20261013};
  auto same = [](const std::vector<GE::Vertex>& a, const std::vector<GE::Vertex>& b) {
    if(a.size() != b.size()) return false;
    for(u32 n = 0; n < a.size(); n++) {
      auto &p = a[n], &q = b[n];
      auto bits = [](const auto& x, const auto& y) { return !std::memcmp(&x, &y, sizeof(x)); };
      if(!bits(p.weights, q.weights) || !bits(p.u, q.u) || !bits(p.v, q.v) || p.color != q.color ||
         !bits(p.normal, q.normal) || !bits(p.x, q.x) || !bits(p.y, q.y) || !bits(p.z, q.z) || !bits(p.clip, q.clip) ||
         !bits(p.q, q.q) || !bits(p.fog, q.fog) || p.outside != q.outside || p.far != q.far || p.near != q.near ||
         p.specular != q.specular) return false;
    }
    return true;
  };
  u32 differing = 0, runs = 0;
  for(u32 round = 0; round < 3000; round++) {
    u32 type = random() & 0x81'c7ff;  //(every field but the indices, set below, and morph targets)
    u32 where = random() % 4, indexFormat = where == 1 ? 1 + random() % 3 : 0;
    type |= indexFormat << 11;
    ge.commands[GE::VertexType] = type;
    ge.commands[GE::AmbientColor] = random() & 0xff'ffff, ge.commands[GE::AmbientAlpha] = random() & 0xff;
    auto format = ge.vertexFormat();
    if(!format.size) continue;
    u32 count = 1 + random() % 40;
    u32 address = where == 2 ? VRAM + 0x10'0000 : where == 3 ? VRAM + Memory::VRAMSize + 0x400 : Vertices;
    u32 indices = Vertices + 0x8000, most = 0;
    for(u32 n = 0; n < count && indexFormat; n++) {
      u32 index = random() % 64;
      if(indexFormat == 3) index |= random() << 16;  //(the GE takes the low 16 bits)
      memory.write(indexFormat == 3 ? 4 : indexFormat, indices + n * (indexFormat == 3 ? 4 : indexFormat), index);
      most = std::max(most, index & 0xffff);
    }
    u32 span = (indexFormat ? most + 1 : count) * format.size;
    bool last = indexFormat && random() % 8 == 0;  //(its vertices end where RAM ends, or a byte past it)
    if(last) address = Memory::RAMBase + memory.ram.size() - span + random() % 2;
    for(u32 at = 0; at < span; at += 4) {
      u32 word = random();
      if(random() % 4 == 0) word |= 0x7f80'0000;
      if(address + at - Memory::RAMBase + 4 <= memory.ram.size() || address < Memory::RAMBase) {
        memory.write(4, address + at, word);
      }
    }
    std::vector<GE::Vertex> read[2];
    u64 before = ge.vertexRunsRead;
    for(bool inRuns : {false, true}) {
      ge.vertexRuns = inRuns;
      ge.vertexAddress = address, ge.indexAddress = indices;
      ge.readVertices(count, format);
      read[inRuns] = ge.primitiveVertices;
    }
    runs += ge.vertexRunsRead - before;
    if(!same(read[0], read[1]) && differing++ < 4) std::printf("  round %u (type %06x): not the same\n", round, type);
  }
  ge.vertexRuns = true;
  CHECK(differing, 0u);
  CHECK(runs > 1500, true);
}

//A float in a vertex that's a signaling not-a-number reads quieted (the fraction's top bit set, its other bits and the
//sign kept), as the host's float operations leave one, in a run and one at a time, whichever compiler built it: GCC's
//builds quiet it in vertexFloat(); clang's rely on the readers' multiplication by the float's unit, 1, which GCC drops
//in runs, so on the Mac this fails if clang ever drops it too. Quiet ones, infinities and numbers read as they are.
//Each value fills every float of a 3D vertex: two weights, the texture coordinates, normal and position.
static auto geVertexFloatsQuieted() -> void {
  System s;
  auto& ge = s.ge;
  auto& memory = s.memory;
  const u32 values[][2] = {  //what's stored, what's read
    {0x7f80'0001, 0x7fc0'0001}, {0x7f84'a03e, 0x7fc4'a03e}, {0x7fbf'ffff, 0x7fff'ffff}, {0xff84'a03e, 0xffc4'a03e},
    {0x7fc0'0000, 0x7fc0'0000}, {0xffff'ffff, 0xffff'ffff}, {0x7f80'0000, 0x7f80'0000}, {0xff80'0000, 0xff80'0000},
    {0x0000'0001, 0x0000'0001}, {0x8000'0000, 0x8000'0000}, {0x3f80'0000, 0x3f80'0000}, {0x7f7f'ffff, 0x7f7f'ffff},
  };
  constexpr u32 count = std::size(values);
  ge.commands[GE::VertexType] = 3 << 0 | 3 << 5 | 3 << 7 | 3 << 9 | 1 << 14;
  auto format = ge.vertexFormat();
  CHECK(format.size, 40u);
  for(u32 n = 0; n < count; n++) {
    for(u32 at = 0; at < format.size; at += 4) memory.write(4, Vertices + n * format.size + at, values[n][0]);
  }
  auto bits = [](float x) { u32 word; std::memcpy(&word, &x, 4); return word; };
  u32 wrong = 0;
  for(bool inRuns : {false, true}) {
    u64 before = ge.vertexRunsRead;
    ge.vertexRuns = inRuns;
    ge.vertexAddress = Vertices;
    ge.readVertices(count, format);
    CHECK(ge.vertexRunsRead - before, u64(inRuns));
    CHECK(ge.primitiveVertices.size(), u64(count));
    for(u32 n = 0; n < count && n < ge.primitiveVertices.size(); n++) {
      auto& v = ge.primitiveVertices[n];
      for(float x : {v.weights[0], v.weights[1], v.u, v.v, v.normal[0], v.normal[1], v.normal[2], v.x, v.y, v.z}) {
        if(bits(x) != values[n][1] && wrong++ < 4) {
          std::printf("  %s: %08x read as %08x\n", inRuns ? "in a run" : "one at a time", values[n][0], bits(x));
        }
      }
    }
  }
  ge.vertexRuns = true;
  CHECK(wrong, 0u);
}

//The 3D settings kept from one PRIM to the next (transformKept()), taken after a few of the GE's commands (any of the
//256) and matrix elements are changed at random, round after round: field for field (floats bit for bit) the
//Transform transformState() works out afresh. (Changes to the world matrix alone, most PRIMs' in a 3D game, are as
//many as all the rest; a quarter of the values are infinities or not numbers as floats.) Asked for a PRIM that doesn't
//skin (a third of the rounds), all of it but the bones, which stay the last skinning PRIM's, then all again.
static auto sameTransform(const GE::Transform& a, const GE::Transform& b, bool bones = true) -> bool {
  static_assert(sizeof(GE::Transform) == 1292);  //(a field added is compared here too)
  auto bits = [](const auto& x, const auto& y) { return !std::memcmp(&x, &y, sizeof(x)); };
  for(u32 n = 0; n < 4; n++) {
    auto &p = a.lights[n], &q = b.lights[n];
    if(p.enabled != q.enabled || p.directional != q.directional || p.spot != q.spot || p.specular != q.specular ||
       p.powered != q.powered || !bits(p.position, q.position) || !bits(p.direction, q.direction) ||
       !bits(p.attenuation, q.attenuation) || !bits(p.cutoff, q.cutoff) || !bits(p.exponent, q.exponent) ||
       p.ambient != q.ambient || p.diffuse != q.diffuse || p.shine != q.shine || !bits(p.toLight, q.toLight) ||
       !bits(p.halfway, q.halfway) || !bits(p.products, q.products)) return false;
  }
  return bits(a.world, b.world) && bits(a.view, b.view) && bits(a.projection, b.projection) &&
         bits(a.textureMatrix, b.textureMatrix) && (!bones || bits(a.bones, b.bones)) && a.weights == b.weights &&
         bits(a.scale, b.scale) && bits(a.center, b.center) && bits(a.offsetX, b.offsetX) &&
         bits(a.offsetY, b.offsetY) && a.depthClamp == b.depthClamp && a.mapMode == b.mapMode &&
         a.mapSource == b.mapSource && bits(a.textureScale, b.textureScale) &&
         bits(a.textureOffset, b.textureOffset) && bits(a.textureWidth, b.textureWidth) &&
         bits(a.textureHeight, b.textureHeight) && a.fog == b.fog && a.fogForced == b.fogForced &&
         bits(a.fogEnd, b.fogEnd) && bits(a.fogSlope, b.fogSlope) && bits(a.fogValue, b.fogValue) &&
         a.lighting == b.lighting && a.separateSpecular == b.separateSpecular && a.normalReverse == b.normalReverse &&
         a.vertexColor == b.vertexColor && a.materialColor == b.materialColor &&
         a.materialEmissive == b.materialEmissive && a.materialAmbient == b.materialAmbient &&
         a.materialDiffuse == b.materialDiffuse && a.materialSpecular == b.materialSpecular &&
         a.ambientLight == b.ambientLight && bits(a.specularPower, b.specularPower) &&
         bits(a.viewDirection, b.viewDirection) && bits(a.unlit, b.unlit) && a.shadeU == b.shadeU &&
         a.shadeV == b.shadeV;
}
static auto geTransformKept() -> void {
  System s;
  auto& ge = s.ge;
  std::mt19937 random{20261010};
  u32* matrices[5] = {ge.bones, ge.world, ge.view, ge.projection, ge.textureMatrix};
  u32 sizes[5] = {96, 12, 12, 16, 12};
  u32 differing = 0;
  float lastBones[96] = {};  //(the last skinning PRIM's bones, as an unskinned one keeps them)
  bool skinnedOnce = false;
  for(u32 round = 0; round < 20000; round++) {
    for(u32 changes = 1 + random() % 3; changes; changes--) {
      u32 which = random() % 8, value = random() & 0xff'ffff;
      if(random() % 4 == 0) value |= 0x7f'8000;  //(as a float24, an infinity or not a number)
      if(which < 4) ge.world[random() % 12] = value;
      else if(which < 6) ge.commands[random() % 256] = value;
      else {
        u32 matrix = random() % 5;
        matrices[matrix][random() % sizes[matrix]] = value;
      }
    }
    bool skinned = random() % 3;
    auto kept = ge.transformKept(skinned);
    bool same = sameTransform(kept, ge.transformState(), skinned);
    if(skinned) std::memcpy(lastBones, kept.bones, sizeof(lastBones)), skinnedOnce = true;
    else if(skinnedOnce) same &= !std::memcmp(kept.bones, lastBones, sizeof(lastBones));
    if(!same && differing++ < 4) std::printf("  round %u: not the same\n", round);
  }
  CHECK(differing, 0u);
}

//The GE on a thread of its own (option "GE Thread", ge/thread.hpp): the same lists, run by a machine with the GE on its
//thread and by one without, leave VRAM, RAM and the GE's registers the same byte for byte. Clears, untextured and
//textured sprites (a CLUT8 texture in RAM, its palette loaded by the list), a block transfer from RAM into VRAM and one
//within VRAM, and the matrices' DATA; then the CPU's store into the texture just after the list (where the render GE
//may not have drawn it yet: its old bytes kept as a version, which the render GE decodes from), and the list again,
//which draws the new texels.
static auto geThreadSame() -> void {
  constexpr u32 Texture = 0x0899'0000, Palette = 0x0899'4000, Copied = VRAM + 0x2'0000;
  auto build = [](Memory& memory) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);  //(through: u, v, 8888 color, x, y, z, all floats but the color)
    list.put(GE::ClearMode, 0x701);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::ClearMode, 0);
    list.put(GE::WorldMatrixNumber, 0);
    for(u32 n = 0; n < 12; n++) list.put(GE::WorldMatrixData, 0x3f'8000 + n);
    list.to(GE::VertexAddress, Vertices + 48);
    list.put(GE::Primitive, GE::Sprites << 16 | 4);  //two sprites, untextured
    list.put(GE::ClutAddress, Palette & 0xff'fff0);
    list.put(GE::ClutAddressUpper, Palette >> 8 & 0xf'0000);
    list.put(GE::ClutFormat, 3 | 0xff << 8);  //8888 entries, all 8 bits of the index
    list.put(GE::ClutLoad, 32);
    list.put(GE::TextureAddress0, Texture & 0xff'fff0);
    list.put(GE::TextureBufferWidth0, (Texture >> 24 & 0xf) << 16 | 16);
    list.put(GE::TextureSize0, 4 << 8 | 4);  //16x16
    list.put(GE::TextureFormat, 5);           //CLUT8
    list.put(GE::TextureFunction, 0 | 1 << 8);  //modulate, with alpha
    list.put(GE::TextureMappingEnable, 1);
    list.to(GE::VertexAddress, Vertices + 144);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);  //textured
    list.put(GE::TextureMappingEnable, 0);
    list.put(GE::TransferSource, Texture);
    list.put(GE::TransferSourceWidth, (Texture >> 24) << 16 | 16);
    list.put(GE::TransferSourcePosition, 0);
    list.put(GE::TransferDestination, VRAM + 0x1'0000);
    list.put(GE::TransferDestinationWidth, (VRAM >> 24) << 16 | 64);
    list.put(GE::TransferDestinationPosition, 0);
    list.put(GE::TransferSize, (16 - 1) << 10 | (4 - 1));
    list.put(GE::TransferStart, 1);  //RAM into VRAM, 4-byte pixels
    list.put(GE::TransferSource, VRAM);
    list.put(GE::TransferSourceWidth, (VRAM >> 24) << 16 | 64);
    list.put(GE::TransferSourcePosition, 4 << 10 | 4);
    list.put(GE::TransferDestination, Copied);
    list.put(GE::TransferDestinationWidth, (Copied >> 24) << 16 | 64);
    list.put(GE::TransferDestinationPosition, 0);
    list.put(GE::TransferSize, (24 - 1) << 10 | (40 - 1));
    list.put(GE::TransferStart, 1);  //VRAM into VRAM: what the sprites drew
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, float u, float v, u32 color, float x, float y) {
      for(float value : {u, v}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, color), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices, 0, 0, 0xff10'2030, 0, 0), vertex(Vertices + 24, 0, 0, 0xff10'2030, 64, 48);  //the clear
    vertex(Vertices + 48, 0, 0, 0xff00'00ff, 2, 3), vertex(Vertices + 72, 0, 0, 0xff00'00ff, 20, 17);
    vertex(Vertices + 96, 0, 0, 0x8000'ff00, 10, 9), vertex(Vertices + 120, 0, 0, 0x8000'ff00, 37, 30);
    vertex(Vertices + 144, 0, 0, 0xffff'ffff, 24, 20), vertex(Vertices + 168, 16, 16, 0xffff'ffff, 56, 44);
    for(u32 n = 0; n < 256; n++) memory.write(4, Palette + n * 4, n * 0x0001'0305u | 0xff00'0000);
    for(u32 n = 0; n < 256; n++) memory.write(1, Texture + n, n * 7);
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  for(System* s : {&plain, &threaded}) {
    build(s->memory);
    s->ge.list = {}, s->ge.list.address = ListA;
    CHECK(u32(s->ge.run(1000)), u32(GE::Stop::Finished));
    for(u32 n = 0; n < 256; n += 4) s->memory.write(4, Texture + n, 0x0102'0304u * (n + 1));  //(after the list: waits)
    s->ge.list = {}, s->ge.list.address = ListA;
    CHECK(u32(s->ge.run(1000)), u32(GE::Stop::Finished));
    s->ge.settleAll();
  }
  CHECK(plain.memory.vram == threaded.memory.vram, true);
  CHECK(plain.memory.ram == threaded.memory.ram, true);
  CHECK(std::memcmp(plain.ge.commands, threaded.ge.commands, sizeof(plain.ge.commands)), 0);
  CHECK(std::memcmp(plain.ge.world, threaded.ge.world, sizeof(plain.ge.world)), 0);
  CHECK(std::memcmp(plain.ge.clut, threaded.ge.clut, sizeof(plain.ge.clut)), 0);
  CHECK(plain.ge.vertexAddress, threaded.ge.vertexAddress);
  //(and what was drawn is what the lists draw: the texture's new texels in the second, the copy of the sprites)
  CHECK(plain.memory.read(4, VRAM + (25 * 64 + 40) * 4) != 0x10'2030, true);
  CHECK(plain.memory.read(4, Copied + (6 * 64 + 6) * 4), plain.memory.read(4, VRAM + (10 * 64 + 10) * 4));
}

//The value rule (ge/model.hpp), with the GE on its thread: held back before it takes anything of a list that draws a
//sprite, the CPU's load of a word of VRAM no PRIM handed over reaches is read from memory at once, the thread still
//held, and a load of a word the sprite may draw waits for the thread to draw it (once, counted), reading what a machine
//without the thread reads there.
static auto geThreadReads() -> void {
  auto build = [](Memory& memory) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);  //(through: u, v, 8888 color, x, y, z, all floats but the color)
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, u32 color, float x, float y) {
      for(float value : {0.0f, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, color), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices, 0xff40'80c0, 2, 3), vertex(Vertices + 24, 0xff40'80c0, 20, 17);
  };
  constexpr u32 Inside = VRAM + (10 * 64 + 10) * 4, Outside = VRAM + (40 * 64 + 40) * 4;  //(row 40: page 2's)
  System plain;
  plain.threadGE(false);
  build(plain.memory);
  plain.ge.list = {}, plain.ge.list.address = ListA;
  CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
  plain.ge.settleAll();
  System s;
  s.threadGE(true);
  s.ge.setThreads(1);
  build(s.memory);
  std::atomic<bool> held{true};
  s.geThreading->drain();  //(the thread reads its gate as it takes each record)
  s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  s.ge.list = {}, s.ge.list.address = ListA;
  CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
  u64 waits = s.geThreading->waits;
  CHECK(s.memory.read(4, Outside), plain.memory.read(4, Outside));
  CHECK(s.geThreading->waits, waits);
  CHECK(held.load(), true);  //(read with the thread still held)
  std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); held = false; });
  CHECK(s.memory.read(4, Inside), plain.memory.read(4, Inside));
  release.join();
  CHECK(s.geThreading->waits, waits + 1);
  s.geThreading->drain();
  s.geThreading->gate = nullptr;
  CHECK(plain.memory.read(4, Inside) != 0, true);  //(the sprite drew there)
  s.ge.settleAll();
  CHECK(plain.memory.vram == s.memory.vram, true);
}

//With the GE on its thread, held back before it draws a sprite with the software renderer: the CPU's store onto a
//pixel the sprite may draw waits for the thread (once, counted), its record going first, so that the sprite is drawn
//before the store lands, as without the thread; a store on a page nothing handed over reaches doesn't wait.
static auto geThreadConflicts() -> void {
  auto build = [](Memory& memory) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, u32 color, float x, float y) {
      for(float value : {0.0f, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, color), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices, 0xff40'80c0, 2, 3), vertex(Vertices + 24, 0xff40'80c0, 20, 17);
  };
  constexpr u32 Inside = VRAM + (10 * 64 + 10) * 4, Outside = VRAM + (40 * 64 + 40) * 4;
  System plain;
  plain.threadGE(false);
  build(plain.memory);
  plain.ge.list = {}, plain.ge.list.address = ListA;
  CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
  plain.memory.write(4, Outside, 0x0102'0304);
  plain.memory.write(4, Inside, 0x0506'0708);
  plain.ge.settleAll();
  System s;
  s.threadGE(true);
  s.ge.setThreads(1);
  build(s.memory);
  std::atomic<bool> held{true};
  s.geThreading->drain();  //(the thread reads its gate as it takes each record)
  s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  s.ge.list = {}, s.ge.list.address = ListA;
  CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
  u64 waits = s.geThreading->waits;
  s.memory.write(4, Outside, 0x0102'0304);
  CHECK(s.geThreading->waits, waits);
  CHECK(held.load(), true);
  std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); held = false; });
  s.memory.write(4, Inside, 0x0506'0708);
  release.join();
  CHECK(s.geThreading->waits, waits + 1);
  s.geThreading->drain();
  s.geThreading->gate = nullptr;
  s.ge.settleAll();
  CHECK(plain.memory.read(4, Inside), 0x0506'0708u);  //(the store after the sprite, over it)
  CHECK(plain.memory.vram == s.memory.vram, true);
}

//With the GE on its thread, held back with thousands of software PRIMs waiting for it, each reading its texture from
//memory as it's drawn, the emulation thread's look at what the thread may still change costs no more than with a few:
//loads of VRAM no PRIM reaches, each taking the thread's reports first (those textures' pages: ge/model.hpp). Kept by
//PRIM, every report taken went through each PRIM not yet drawn, and one is taken at each load of VRAM and each list
//word there: Ghost of Sparta, its display lists in VRAM, crawled. Seen two ways: what the model keeps of those textures
//is as big with 8,000 PRIMs behind as with 500 (kept by PRIM, the list every report went through grew with them, one
//entry each); and 20,000 loads take less than four times as long with 8,000 behind as with 500, the best of three
//rounds each (a ratio, so that the machine's speed and a sanitizer's cancel out; with the list kept by PRIM, 15 times
//as long). Then drawn as without the thread.
static auto geThreadManyBehind() -> void {
  constexpr u32 Long = 0x0900'0000, Untouched = VRAM + 0x10'0000;
  auto build = [](Memory& memory) {
    for(u32 n = 0; n < 16 * 16 * 4; n += 4) memory.write(4, Image + n, 0xff00'0000 | n * 0x01'0305);
    auto vertex = [&](u32 at, float u, float v, float x, float y) {
      for(float value : {u, v}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, 0xffff'ffff), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices, 0, 0, 0, 0), vertex(Vertices + 24, 16, 16, 8, 8);
  };
  auto sprites = [](Memory& memory, u32 address, u32 count) -> u32 {  //(a list of count textured sprites: its end)
    ListWriter list{memory, address};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);
    list.put(GE::TextureAddress0, Image & 0xff'fff0);
    list.put(GE::TextureBufferWidth0, (Image >> 24 & 0xf) << 16 | 16);
    list.put(GE::TextureSize0, 4 << 8 | 4);  //16x16
    list.put(GE::TextureFormat, 3);
    list.put(GE::TextureFunction, 0 | 1 << 8);
    list.put(GE::TextureMappingEnable, 1);
    list.put(GE::Base, Vertices >> 8 & 0xf'0000);
    for(u32 n = 0; n < count; n++) {
      list.put(GE::VertexAddress, Vertices), list.put(GE::Primitive, GE::Sprites << 16 | 2);
    }
    list.put(GE::Finish);
    list.put(GE::End);
    return list.address;
  };
  auto run = [](System& s, u32 address) {
    s.ge.list = {}, s.ge.list.address = address;
    CHECK(u32(s.ge.run(100'000)), u32(GE::Stop::Finished));
  };
  System plain;
  plain.threadGE(false);
  build(plain.memory);
  u32 second = sprites(plain.memory, Long, 500);
  sprites(plain.memory, second, 7'500);
  run(plain, Long), run(plain, second);
  plain.ge.settleAll();
  System s;
  s.threadGE(true);
  s.ge.setThreads(1);
  build(s.memory);
  sprites(s.memory, Long, 500);
  sprites(s.memory, second, 7'500);
  auto loads = [&] {  //(the best of three rounds of 20,000 loads, in seconds)
    double best = 1e30;
    for(u32 round = 0; round < 3; round++) {
      u32 sum = 0;
      auto start = std::chrono::steady_clock::now();
      for(u32 n = 0; n < 20'000; n++) sum += s.memory.read(4, Untouched + n % 64 * 4);
      best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
      CHECK(sum, 0u);
    }
    return best;
  };
  std::atomic<bool> held{true};
  s.geThreading->drain();  //(the thread reads its gate as it takes each record)
  s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  u64 waits = s.geThreading->waits;
  run(s, Long);
  double few = loads();
  auto kept = s.geThreading->model.softwareTextures.size();
  run(s, second);
  double many = loads();
  CHECK(s.geThreading->model.softwareTextures.size(), kept);
  CHECK(held.load(), true);  //(all of it with the thread held)
  CHECK(s.geThreading->waits, waits);
  held = false;
  s.geThreading->drain();
  s.geThreading->gate = nullptr;
  if(many >= few * 4) std::printf("  loads with 8,000 PRIMs behind took %.1f times as long as with 500\n", many / few);
  CHECK(many < few * 4, true);
  s.ge.settleAll();
  CHECK(plain.memory.vram == s.memory.vram, true);
}

//With the GE on its thread, a PRIM's vertices by indices are copied for the thread from the least index's vertex to
//the most's: a sprite by indices 4000 and 4001 of a vertex buffer hands the thread a record no bigger than one by
//indices 0 and 1 (copied from the buffer's start, the first carried 4,002 vertices: Jeanne d'Arc's PRIMs, a big
//buffer's few vertices each, had the emulation thread copying most of the time). Drawn as without the thread, the
//render GE reading the vertices a run at a time from the copy (counted) and, with the GE's vertex runs off, one at a
//time.
static auto geThreadIndexedCopies() -> void {
  constexpr u32 Indices = Image;
  auto build = [](Memory& memory, u32 first) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'119f);  //(through, 16-bit indices: u, v, 8888 color, x, y, z)
    list.to(GE::IndexAddress, Indices);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, u32 color, float x, float y) {
      for(float value : {0.0f, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, color), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices + first * 24, 0xff40'80c0, 2, 3), vertex(Vertices + (first + 1) * 24, 0xff40'80c0, 20, 17);
    memory.write(2, Indices, first), memory.write(2, Indices + 2, first + 1);
  };
  System plain;
  plain.threadGE(false);
  build(plain.memory, 4000);
  plain.ge.list = {}, plain.ge.list.address = ListA;
  CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
  plain.ge.settleAll();
  CHECK(plain.memory.read(4, VRAM + (10 * 64 + 10) * 4) != 0, true);  //(the sprite drew there)
  auto handed = [&](u32 first, bool runs) -> u64 {  //(the bytes of the list's records, the thread held back)
    System s;
    s.threadGE(true);
    s.ge.vertexRuns = runs;
    build(s.memory, first);
    std::atomic<bool> held{true};
    s.geThreading->drain();  //(the thread reads its gate as it takes each record)
    s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
    s.ge.list = {}, s.ge.list.address = ListA;
    CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
    u64 bytes = s.geThreading->backlog();
    held = false;
    s.geThreading->drain();
    s.geThreading->gate = nullptr;
    CHECK(s.geThreading->render.vertexRunsRead, u64(runs));  //(a run of the copy: from the least's vertex)
    s.ge.settleAll();
    CHECK(plain.memory.vram == s.memory.vram, true);
    return bytes;
  };
  u64 near = handed(0, true);
  CHECK(handed(4000, true), near);
  CHECK(handed(4000, false), near);
}

//Versions of memory (ge/versions.hpp), by themselves: the GE thread reads as of its place in the stream, a version's
//bytes where one newer than its place has them; its write-backs go under the emulation thread's later writes (into
//the earliest version at or after its place), storing only the bytes that change; of two versions over the same bytes
//the earlier counts; versions are freed once the GE thread has passed them. And neither thread waits for the other
//while holding what the other waits for (R13), nor does the GE thread wait for a version not being copied: stopped
//between pinning and looking for versions, the GE thread finds the emulation thread's version published, not ready
//and not copied (the emulation thread waits for its pin), and reads past it, memory's byte as the version will have
//it; the same where the emulation thread is stopped between publishing and looking at the pin, and where it's
//stopped till the GE thread is done (as it writes on and on into a page the GE thread writes back: waiting for that
//version, as before, the GE thread would never be done); stopped as it copies, the GE thread finds the copy under
//way, lets go and waits, and reads the version once it's ready. Each order under a time limit.
static auto geVersions() -> void {
  static u8 bytes[2][4096];
  static std::atomic<bool> stopCopy{false}, copyStopped{false};
  static std::thread::id copier;
  auto host = [](void*, u32 page) -> u8* {
    if(stopCopy && std::this_thread::get_id() == copier) {  //(the emulation thread copying a version's bytes)
      copyStopped = true;
      while(stopCopy) std::this_thread::yield();
    }
    return page == 5 ? bytes[0] : page == 6 ? bytes[1] : nullptr;
  };
  for(u32 n = 0; n < 4096; n++) bytes[0][n] = n, bytes[1][n] = n ^ 0x55;
  {
    GeVersions v{host, nullptr};
    v.save(5, 100, 4, 10);
    for(u32 n = 100; n < 104; n++) bytes[0][n] = 0x99;  //(the emulation thread's write at 10)
    u8 out[8];
    {
      GeVersions::Hold hold{v, 9, false, 5, 5};
      CHECK(hold.versioned, true);
      hold.read(5, 98, 8, out);
      CHECK(out[0] | out[1] << 8 | out[2] << 16 | u32(out[3]) << 24, 98u | 99u << 8 | 100u << 16 | 101u << 24);
      CHECK(out[4] | out[5] << 8 | out[6] << 16 | u32(out[7]) << 24, 102u | 103u << 8 | 104u << 16 | 105u << 24);
    }
    {
      GeVersions::Hold hold{v, 10, false, 5, 5};  //(at the write's place: after it)
      CHECK(hold.versioned, false);
    }
    {
      GeVersions::Hold hold{v, 10, true, 5, 5};  //(a write-back at 10: before the write)
      CHECK(hold.versioned, true);
      u8 in[8] = {7, 7, 7, 7, 7, 7, 7, 7};
      hold.writeBack(5, 98, 8, in);
    }
    CHECK(bytes[0][98] | bytes[0][99] << 8 | bytes[0][104] << 16 | u32(bytes[0][105]) << 24, 0x0707'0707u);
    CHECK(bytes[0][100] | bytes[0][101] << 8 | bytes[0][102] << 16 | u32(bytes[0][103]) << 24, 0x9999'9999u);
    {
      GeVersions::Hold hold{v, 9, false, 5, 5};
      hold.read(5, 100, 4, out);
      CHECK(out[0] | out[1] << 8 | out[2] << 16 | u32(out[3]) << 24, 0x0707'0707u);
    }
    {
      GeVersions::Hold hold{v, 11, true, 5, 5};
      CHECK(hold.versioned, false);
    }
    //two versions over the same bytes: the earlier newer than the place counts
    bytes[1][0] = 1;
    v.save(6, 0, 1, 20), bytes[1][0] = 2;
    v.save(6, 0, 1, 22), bytes[1][0] = 3;
    for(auto [q, expected] : {std::pair<u64, u32>{19, 1}, {20, 2}, {21, 2}, {22, 3}}) {
      GeVersions::Hold hold{v, q, false, 6, 6};
      hold.read(6, 0, 1, out);
      CHECK(out[0], expected);
    }
    CHECK(v.versions(), 3u);
    v.free(10);
    CHECK(v.versions(), 2u);
    v.free(22);
    CHECK(v.versions(), 0u);
    GeVersions::Hold hold{v, 0, false, 5, 6};
    CHECK(hold.versioned, false);
  }
  //R13: each thread stopped between its steps, in each order, under a time limit
  for(u32 order = 0; order < 4; order++) {
    GeVersions v{host, nullptr};
    bytes[0][200] = 0x11;
    std::atomic<u32> gePinned{0}, emulationPublished{0};
    std::atomic<bool> geDone{false}, emulationDone{false};
    u8 seen = 0;
    if(order == 0) {  //(the GE thread pinned first, then the version published while it's stopped)
      v.pinned = [&] { if(!gePinned++) while(!emulationPublished) std::this_thread::yield(); };
      v.published = [&] { emulationPublished = 1; };
    } else if(order == 1) {  //(the version published first, the emulation thread stopped till the GE thread pins)
      v.published = [&] {
        emulationPublished = 1;
        while(!gePinned) std::this_thread::yield();
      };
      v.pinned = [&] { gePinned++; };
    } else if(order == 2) {  //(the emulation thread stopped as it copies the version's bytes, the GE thread pinning)
      stopCopy = true, copyStopped = false;
      v.pinned = [&] { gePinned++; };
    } else {  //(the emulation thread stopped after publishing till the GE thread is done)
      v.published = [&] {
        emulationPublished = 1;
        while(!geDone) std::this_thread::yield();
      };
      v.pinned = [&] { gePinned++; };
    }
    std::thread ge([&] {
      if(order == 1 || order == 3) while(!emulationPublished) std::this_thread::yield();
      if(order == 2) while(!copyStopped) std::this_thread::yield();
      {
        GeVersions::Hold hold{v, 40, false, 5, 5};
        hold.read(5, 200, 1, &seen);
      }
      geDone = true;
    });
    std::thread emulation([&] {
      copier = std::this_thread::get_id();
      if(order == 0) while(!gePinned) std::this_thread::yield();
      v.save(5, 200, 1, 41);
      bytes[0][200] = 0x22;
      emulationDone = true;
    });
    if(order == 2) {  //(the copy let go on once the GE thread, finding it under way, has let go of the page)
      while(!gePinned || v.pages[5].pins.load()) std::this_thread::yield();
      stopCopy = false;
    }
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(!(geDone && emulationDone) && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    if(!(geDone && emulationDone)) {
      std::fprintf(stderr, "ge versions: order %u deadlocked\n", order);
      std::abort();  //(the threads can't be joined)
    }
    ge.join(), emulation.join();
    CHECK(seen, 0x11u);  //(the GE thread at 40 reads the byte as before the write at 41)
    if(order == 2) CHECK(gePinned >= 2u, true);  //(it let go of the copy under way and pinned again)
    else CHECK(gePinned, 1u);                     //(it read past the version not copied)
  }
  //A version published while the GE thread holds its page, another version found there as it pinned: not ready till
  //the page is let go of, so its read takes memory's byte, as the version will have it, and its write-back goes to
  //memory, which the version's copy then takes (not into the version's room, where the copy would undo it)
  {
    GeVersions v{host, nullptr};
    bytes[0][200] = 0x11, bytes[0][300] = 0x44;
    v.save(5, 200, 1, 41), bytes[0][200] = 0x22;
    std::atomic<bool> held{false}, published{false}, geDone{false}, emulationDone{false};
    u8 seen = 0;
    bool versioned = false;
    v.published = [&] { published = true; };
    std::thread ge([&] {
      {
        GeVersions::Hold hold{v, 40, true, 5, 5};
        versioned = hold.versioned;
        held = true;
        while(!published) std::this_thread::yield();
        hold.read(5, 300, 1, &seen);
        u8 value = 0x66;
        hold.writeBack(5, 300, 1, &value);
      }
      geDone = true;
    });
    std::thread emulation([&] {
      while(!held) std::this_thread::yield();
      v.save(5, 300, 1, 45);
      bytes[0][300] = 0x77;
      emulationDone = true;
    });
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while(!(geDone && emulationDone) && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    if(!(geDone && emulationDone)) {
      std::fprintf(stderr, "ge versions: a version published under a hold deadlocked\n");
      std::abort();
    }
    ge.join(), emulation.join();
    CHECK(versioned, true);
    CHECK(seen, 0x44u);
    CHECK(bytes[0][300], 0x77u);
    GeVersions::Hold hold{v, 44, false, 5, 5};
    u8 kept = 0;
    hold.read(5, 300, 1, &kept);
    CHECK(kept, 0x66u);  //(the write-back at 40 is under the write at 45)
  }
}

//With the GE on its thread, more runs handed over than it keeps counts of (GeThread::RunsKept) before a FINISH is due,
//which is when the kernel folds them (a list drawn sprite by sprite through its stall address, as GU's direct lists
//are: a run each): the oldest is folded as each new one goes past that many (Kernel::geTime()), so none is lost to a
//later one's counts, and the GE's time at the FINISH is the unthreaded machine's (the review's B1: past 4,096 the
//counts of later runs overwrote earlier ones' not yet folded, a race too, and the time came out wrong, run to run).
static auto geThreadRunsKept() -> void {
  u64 done[2] = {};
  for(bool threaded : {false, true}) {
    KernelMachine m;
    m.system.threadGE(threaded);
    auto& memory = m.system.memory;
    DrawingList d{memory, ListA, Vertices};
    constexpr u32 Runs = 5000;
    std::vector<u32> stalls;
    for(u32 n = 0; n < Runs; n++) {  //(the first 4096 runs 1x1 sprites, the rest 40x40: owing far more)
      s32 size = n < 4096 ? 1 : 40;
      d.draw(GE::Sprites, {{0, 0}, {size, size}});
      stalls.push_back(d.list.address);
    }
    d.finish();
    u32 id = m.call("sceGeListEnQueue", {ListA, ListA, 0xffff'ffff, 0});
    for(u32 n = 0; n < Runs; n++) m.call("sceGeListUpdateStallAddr", {id, stalls[n]});
    if(threaded) m.system.ge.drawer();  //(the GE thread done with every run so far)
    CHECK(m.kernel.geFinishDue, false);
    m.call("sceGeListUpdateStallAddr", {id, 0});
    CHECK(m.kernel.geFinishDue, true);
    done[threaded] = geDone(m);
  }
  CHECK(done[1], done[0]);
}

//A textured sprite whose texture lies where nothing is (address 0): the software renderer reads its texels through
//memory.read(), each telling unmapped() (System::unmappedReports, which a state keeps, counts them); with the GE on its
//thread, the same (the review's S1: the render GE's memory reported nothing).
static auto geThreadUnmapped() -> void {
  size_t reports[2] = {};
  for(bool threaded : {false, true}) {
    System s;
    s.threadGE(threaded);
    ListWriter list{s.memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);  //(through: u, v, 8888 color, x, y, z, all floats but the color)
    list.put(GE::TextureAddress0, 0);
    list.put(GE::TextureBufferWidth0, 16);
    list.put(GE::TextureSize0, 4 << 8 | 4);  //16x16
    list.put(GE::TextureFormat, 3);           //8888
    list.put(GE::TextureFunction, 0 | 1 << 8);
    list.put(GE::TextureMappingEnable, 1);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, float u, float v, u32 color, float x, float y) {
      for(float value : {u, v}) { u32 word; std::memcpy(&word, &value, 4); s.memory.write(4, at, word); at += 4; }
      s.memory.write(4, at, color), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); s.memory.write(4, at, word); at += 4; }
    };
    vertex(Vertices, 0, 0, 0xffff'ffff, 4, 4), vertex(Vertices + 24, 16, 16, 0xffff'ffff, 20, 20);
    s.unmapped.clear();
    s.ge.list = {}, s.ge.list.address = ListA;
    CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
    s.ge.settleAll();
    reports[threaded] = s.unmapped.size();
  }
  CHECK(reports[0] > 0, true);
  CHECK(reports[1], reports[0]);
}

//The go split (Kernel::goThreaded()), which this case needs (the review's S2; the design's R5): a call into the program
//held back while interrupts are off (an alarm gone off), interrupts turned back on by mtic (no system call: the go
//goes on) inside a go whose limit came from the GE's time's bound (the GE thread held back after the first of two
//clears, its counts so far published). The go runs on from the bound with none of the run loop's steps between, so
//the call starts at the GE's exact time, as without the thread; ended at the bound, startCall() would start it there.
//Both engines.
static auto geThreadGoSplit() -> void {
  constexpr u32 Counter = KernelMachine::Results + 0x40;
  for(bool recompile : {false, true}) {
    u32 seen[2] = {};
    for(bool threaded : {false, true}) {
      KernelMachine m;
      m.system.threadGE(threaded);
      auto& memory = m.system.memory;
      DrawingList list{memory, ListA, Vertices};
      list.list.put(GE::ClearMode, 0x101);
      list.draw(GE::Sprites, {{0, 0}, {480, 136}});
      list.list.put(GE::ClutAddress, 0), list.list.put(GE::ClutAddressUpper, 0x04'0000);  //(VRAM, the first clear's)
      list.list.put(GE::ClutLoad, 1);
      list.draw(GE::Sprites, {{0, 136}, {480, 272}});
      list.finish();
      Assembler handler{m, 0x0880'3000};
      handler.li(t0, Counter); handler.put(lw(t1, 0, t0));
      handler.li(t0, KernelMachine::Results); handler.put(sw(t1, 0, t0));
      handler.li(v0, 0);  //(not again)
      handler.put(jr(ra));
      handler.put(nop);
      Assembler main{m, 0x0880'1000};
      main.li(s3, 1);
      main.put(mtic(zero));  //interrupts held off
      main.li(a0, 1); main.li(a1, 0x0880'3000); main.li(a2, 0); main.call("sceKernelSetAlarm");
      main.li(t3, 30000);  //(90000 cycles, past TimerLead's 215 microseconds: the alarm goes off, its call waits)
      u32 spin = main.here();
      main.put(addiu(t3, t3, -1));
      main.put(bne(t3, zero, s32(spin - main.here() - 4) / 4));
      main.put(nop);
      main.li(a0, ListA); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
      main.call("sceGeListEnQueue");
      main.put(mtic(s3));  //back on, no system call
      main.li(s2, 0); main.li(t0, Counter); main.li(t3, 60000);
      u32 count = main.here();
      main.put(addiu(s2, s2, 1));
      main.put(sw(s2, 0, t0));
      main.put(addiu(t3, t3, -1));
      main.put(bne(t3, zero, s32(count - main.here() - 4) / 4));
      main.put(nop);
      main.call("sceKernelExitGame");
      std::atomic<bool> held{true};
      u64 seenPlace = 0;
      if(threaded) {
        m.system.geThreading->gate = [&](u64 place) {
          if(!seenPlace) { if(m.system.renderGE.owedPixels) seenPlace = place; return; }
          if(place > seenPlace) while(held.load()) std::this_thread::yield();
        };
      }
      std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(500)); held = false; });
      runMain(m, recompile);
      release.join();
      if(threaded) m.system.geThreading->drain(), m.system.geThreading->gate = nullptr;
      seen[threaded] = memory.read(4, KernelMachine::Results);
    }
    if(seen[1] != seen[0]) {
      std::printf("  recompile %d: the alarm's call at count %u, with the thread %u\n", recompile, seen[0], seen[1]);
    }
    CHECK(seen[0] > 0, true);
    CHECK(seen[1], seen[0]);
  }
}

//With the GE on its thread held back before a software sprite drawn over part of its own texture (read from memory as
//it's drawn: direct()): the CPU's store onto a texel it reads but doesn't draw over (outside its pixels: only the
//model's software textures, page by page, have it) waits for it to be drawn, as without the thread the store comes
//after it.
static auto geThreadTextureConflict() -> void {
  constexpr u32 Texture = VRAM + 32 * 256;  //(rows 32-47 of the 64-wide 8888 frame buffer at VRAM's start)
  auto build = [](Memory& memory) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 63 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'019f);  //(through: u, v, 8888 color, x, y, z, all floats but the color)
    list.put(GE::TextureAddress0, Texture & 0xff'fff0);
    list.put(GE::TextureBufferWidth0, (Texture >> 24 & 0xf) << 16 | 64);
    list.put(GE::TextureSize0, 4 << 8 | 4);  //16x16
    list.put(GE::TextureFormat, 3);
    list.put(GE::TextureFunction, 0 | 1 << 8);
    list.put(GE::TextureFilter, 0);
    list.put(GE::TextureMappingEnable, 1);
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::Finish);
    list.put(GE::End);
    auto vertex = [&](u32 at, float u, float v, float x, float y) {
      for(float value : {u, v}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
      memory.write(4, at, 0xffff'ffff), at += 4;
      for(float value : {x, y, 0.0f}) { u32 word; std::memcpy(&word, &value, 4); memory.write(4, at, word); at += 4; }
    };
    //(the sprite on the texture's rows 8-11 (VRAM's 40-43), sampling its rows 6-9 (VRAM's 38-41): its rows 42-43
    //take texels it drew itself, so it reads them from memory as it draws, not a copy: readsAhead())
    vertex(Vertices, 0, 6, 0, 40), vertex(Vertices + 24, 8, 10, 8, 44);
    for(u32 y = 0; y < 16; y++) {
      for(u32 x = 0; x < 16; x++) memory.write(4, Texture + (y * 64 + x) * 4, 0xff00'0000 | (y * 16 + x) * 0x01'0203);
    }
  };
  System plain;
  plain.threadGE(false);
  build(plain.memory);
  plain.ge.list = {}, plain.ge.list.address = ListA;
  CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
  constexpr u32 Stored = Texture + 6 * 64 * 4;  //(the texel at (0, 6): VRAM's row 38, outside the sprite's region)
  plain.memory.write(4, Stored, 0xff12'3456);
  plain.ge.settleAll();
  System s;
  s.threadGE(true);
  s.ge.setThreads(1);
  build(s.memory);
  std::atomic<bool> held{true};
  s.geThreading->drain();
  s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  s.ge.list = {}, s.ge.list.address = ListA;
  CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
  u64 waits = s.geThreading->waits;
  std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); held = false; });
  s.memory.write(4, Stored, 0xff12'3456);
  release.join();
  CHECK(s.geThreading->waits, waits + 1);
  s.geThreading->drain();
  s.geThreading->gate = nullptr;
  s.ge.settleAll();
  CHECK(plain.memory.read(4, VRAM + (40 * 64 + 0) * 4) != 0, true);  //(the sprite drew there)
  CHECK(plain.memory.vram == s.memory.vram, true);
}

//With the GE on its thread held back before a software clear writing depth: the CPU's load of that depth through
//VRAM's fourth copy (an 8888 frame buffer's depth view) waits for it to be drawn (the model's software depth
//rectangles) and reads what's drawn there, as without the thread.
static auto geThreadSoftwareDepth() -> void {
  constexpr u32 Depth = 0x10'0000, Seen = VRAM + 3 * Memory::VRAMSize + Depth + (10 * 64 + 10) * 2;
  auto build = [](Memory& memory) {
    ListWriter list{memory, ListA};
    list.put(GE::FrameBufferPointer, 0);
    list.put(GE::FrameBufferWidth, 64);
    list.put(GE::FrameBufferPixelFormat, 3);
    list.put(GE::DepthBufferPointer, Depth);
    list.put(GE::DepthBufferWidth, 64);
    list.put(GE::Scissor1, 0);
    list.put(GE::Scissor2, 47 << 10 | 63);
    list.put(GE::Region2, 1023 << 10 | 1023);
    list.put(GE::VertexType, 0x80'011c);  //(through: 8888 color, 16-bit x, y, z)
    list.put(GE::ClearMode, 0x401);       //(depth only)
    list.to(GE::VertexAddress, Vertices);
    list.put(GE::Primitive, GE::Sprites << 16 | 2);
    list.put(GE::ClearMode, 0);
    list.put(GE::Finish);
    list.put(GE::End);
    for(u32 n = 0; n < 2; n++) {
      memory.write(4, Vertices + n * 12, 0xff00'00ff);
      memory.write(2, Vertices + n * 12 + 4, n ? 32 : 0), memory.write(2, Vertices + n * 12 + 6, n ? 32 : 0);
      memory.write(2, Vertices + n * 12 + 8, 0x1234);
    }
  };
  System plain;
  plain.threadGE(false);
  build(plain.memory);
  plain.ge.list = {}, plain.ge.list.address = ListA;
  CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
  u32 drawn = plain.memory.read(2, Seen);
  CHECK(drawn, 0x1234u);
  System s;
  s.threadGE(true);
  s.ge.setThreads(1);
  build(s.memory);
  std::atomic<bool> held{true};
  s.geThreading->drain();
  s.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  s.ge.list = {}, s.ge.list.address = ListA;
  CHECK(u32(s.ge.run(1000)), u32(GE::Stop::Finished));
  u64 waits = s.geThreading->waits;
  std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); held = false; });
  u32 seen = s.memory.read(2, Seen);
  release.join();
  CHECK(s.geThreading->waits, waits + 1);
  CHECK(seen, drawn);
  s.geThreading->drain();
  s.geThreading->gate = nullptr;
}

//Whether the machine's state is the other's, saying where they part if not: the part (memory, the CPU and the system,
//the GE, the kernel) and the byte in it.
static auto sameState(KernelMachine& m, const std::vector<u8>& other) -> bool {
  auto state = saveState(m);
  if(state == other) return true;
  serializer s;
  m.system.memory.serialize(s);
  size_t memory = s.size();
  m.system.serialize(s);
  size_t system = s.size();
  m.system.ge.serialize(s);
  size_t ge = s.size(), at = 0;
  while(at < state.size() && at < other.size() && state[at] == other[at]) at++;
  const char* part = at < memory ? "memory" : at < system ? "the CPU and the system" : at < ge ? "the GE" : "the kernel";
  size_t from = at < memory ? 0 : at < system ? memory : at < ge ? system : ge;
  std::printf("  the states part in %s, at its byte %zu (%zu and %zu bytes)\n", part, at - from, state.size(),
              other.size());
  return false;
}

//The GE profile (kernel/geprofile.cpp, the runner's --ge-profile): a program that sets callbacks, queues a list that
//draws a 64x32 sprite (1536 cycles of the GE's time) and finishes, waits for it, shows a frame buffer and waits for
//the vertical blank, recorded as tools/psp-ge-profile's plugin records it on a PSP: each call from where it's made to
//where it returns, a wait to where its thread goes on with what the wait gave, the thread, the GE's state, the FINISH
//callback as it ran, the blank at its own time, all in microseconds (a call ending at 0 ending at 1, 0 being the
//format's mark of a call that hasn't), and the model's charge; the file in plugin/profile.h's layout, its hooks word
//the PSP's. With no profile the program sees the same, and nothing is recorded: the profile records and
//never charges, so the machine ends as it does unprofiled, with every register as each call left it (stored after
//each), its memory and the kernel's state all the same, and to the cycle, which the same program without the
//vblank wait (where a difference in time would be taken up) shows too. A state loaded as the FINISH callback runs is
//the state's, not the profile's: the calls under way then (the callback, and the sync waiting for it) stay
//unfinished (exit 0), as a capture leaves the calls it ends in, and the rest is recorded as before. With every
//callback slot taken, sceGeSetCallback reads nothing of what it's given, and the profile doesn't either.
static auto geProfile() -> void {
  constexpr u32 Registers = KernelMachine::Results + 0x1000;  //after each call, 32 words
  enum Run : u32 { Unprofiled, Profiled, Loaded, UnprofiledUnwaited, ProfiledUnwaited };
  std::vector<u8> unprofiled[2];
  u64 unprofiledCycles[2] = {};
  for(u32 run : {Unprofiled, Profiled, Loaded, UnprofiledUnwaited, ProfiledUnwaited}) {
    bool waits = run <= Loaded, profiled = run == Profiled || run == Loaded || run == ProfiledUnwaited;
    KernelMachine m;
    m.kernel.startTime = 1'791'290'096'000'000ull;  //(the host's clock at power on, in the state: the same for each)
    auto& memory = m.system.memory;
    memory.write(4, Callbacks, 0);
    memory.write(4, Callbacks + 8, recorder(m, 0x0880'4100, Finished));
    memory.write(4, Callbacks + 12, 0x2222);
    DrawingList list{memory, ListA, Vertices};
    list.draw(GE::Sprites, {{0, 0}, {64, 32}});
    list.finish(42);
    Assembler main{m, 0x0880'1000};
    u32 calls = 0;
    auto stored = [&] {  //every register but k0, which points where they go
      main.li(k0, Registers + calls++ * 128);
      for(u32 r = 1; r < 32; r++) if(r != k0) main.put(sw(r, r * 4, k0));
    };
    main.li(a0, Callbacks); main.call("sceGeSetCallback"); stored();
    main.li(a0, ListA); main.li(a1, 0); main.put(addu(a2, v0, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue"); stored();
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.call("sceGeListSync"); stored();
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.li(a0, VRAM); main.li(a1, 512); main.li(a2, 3); main.li(a3, 1); main.call("sceDisplaySetFrameBuf");
    stored();
    if(waits) {
      main.call("sceDisplayWaitVblankStart"); stored();
      main.li(t0, KernelMachine::Results); main.put(sw(v0, 4, t0));
    }
    main.call("sceKernelExitGame");
    GeProfile profile;
    if(profiled) m.kernel.geProfileStart(profile);
    if(run == Loaded) {  //on to the FINISH callback's start, a cycle at a time; there the state saved and loaded
      m.system.recompiler.enabled = false;
      m.system.power(0x0880'1000);
      s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0x0812'3456);
      m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
      for(u32 n = 0; n < 100'000 && !m.kernel.callProfile && !m.kernel.exited; n++) m.kernel.run(1);
      CHECK(m.kernel.callProfile, 5);  //the FINISH's record
      CHECK(loadState(m, saveState(m)), true);
      m.kernel.run(Kernel::VblankCycles * 4);
      CHECK(m.kernel.exited, true);
    } else {
      runMain(m, false);
    }
    m.kernel.geProfileStop();
    CHECK(m.kernel.geProfile == nullptr, true);
    CHECK(memory.read(4, KernelMachine::Results), 0);
    CHECK(memory.read(4, KernelMachine::Results + 4), 0);
    CHECK(memory.read(4, Finished), 42);
    auto& r = profile.records;
    if(!profiled) {
      CHECK(r.size(), 0);
      unprofiled[waits] = saveState(m);
      unprofiledCycles[waits] = m.kernel.cycles;
      continue;
    }
    if(run == Loaded) {
      CHECK(r.size(), 8);
      if(r.size() != 8) return;
      CHECK(r[3].kind, GeProfile::ListSync); CHECK(r[3].exit, 0);
      CHECK(r[4].kind, GeProfile::Finish); CHECK(r[4].entry != 0, true); CHECK(r[4].exit, 0);
      CHECK(r[5].kind, GeProfile::SetFrameBuf); CHECK(r[5].exit != 0, true);
      CHECK(r[6].kind, GeProfile::WaitVblankStart); CHECK(r[6].exit != 0, true); CHECK(r[6].result, 0);
      CHECK(r[7].kind, GeProfile::Vblank);
      continue;
    }
    CHECK(m.kernel.cycles, unprofiledCycles[waits]);
    CHECK(sameState(m, unprofiled[waits]), true);
    if(!waits) continue;
    CHECK(r.size(), 8);
    if(r.size() != 8) return;
    u32 doneAt = u32(m.kernel.geDoneAt / 333), vblank = u32(Kernel::VblankCycles / 333);
    CHECK(r[0].kind, GeProfile::SetCallback);
    CHECK(r[0].thread, 1);
    CHECK(r[0].a, 0); CHECK(r[0].b, 0x0880'4100); CHECK(r[0].c, Callbacks); CHECK(r[0].result, 0);
    CHECK(r[0].entry, 0); CHECK(r[0].exit, 1);  //it ended at 0 us too: 1, as 0 is a call that hasn't ended
    CHECK(r[1].kind, GeProfile::EnQueue);
    CHECK(r[1].a, ListA); CHECK(r[1].b, 0); CHECK(r[1].c, 0); CHECK(r[1].result, Kernel::GeListIDs);
    CHECK(r[1].ge, 0);  //idle as it was called
    CHECK(r[2].kind, GeProfile::ModelWork);
    CHECK(r[2].a, 64 * 32); CHECK(r[2].b, 1); CHECK(r[2].c, 1536);
    CHECK(r[2].exit, doneAt); CHECK(r[2].result, Kernel::GeListIDs);
    CHECK(r[3].kind, GeProfile::ListSync);
    CHECK(r[3].a, Kernel::GeListIDs); CHECK(r[3].b, 0); CHECK(r[3].result, 0);
    CHECK(r[3].ge, list.list.address | 2);  //drawing, the GE past its END already
    CHECK(r[3].exit >= doneAt && r[3].exit < doneAt + 2, true);  //back once the FINISH's callback had run
    CHECK(r[4].kind, GeProfile::Finish);
    CHECK(r[4].thread, 0);
    CHECK(r[4].a, 0); CHECK(r[4].b, 42); CHECK(r[4].c, list.list.address);
    CHECK(r[4].entry, doneAt); CHECK(r[4].exit >= r[4].entry && r[4].exit <= r[3].exit, true);
    CHECK(r[5].kind, GeProfile::SetFrameBuf);
    CHECK(r[5].a, VRAM); CHECK(r[5].b, 512); CHECK(r[5].c, 0x103); CHECK(r[5].result, 0);
    CHECK(r[6].kind, GeProfile::WaitVblankStart);
    CHECK(r[6].exit, vblank); CHECK(r[6].result, 0); CHECK(r[6].thread, 1);
    CHECK(r[7].kind, GeProfile::Vblank);
    CHECK(r[7].entry, vblank); CHECK(r[7].exit, vblank); CHECK(r[7].a, 1); CHECK(r[7].vcount, 1);
    CHECK(profile.threads.size(), 2);
    CHECK(profile.threads.size() == 2 && profile.threads[1].second == "main", true);
    CHECK(profile.startTime, 0); CHECK(profile.endVcount, 1);
    CHECK(profile.cpuMhz, 222); CHECK(profile.busMhz, 111);

    auto file = profile.file("phobos test", "UCUS98653", "a title");
    auto word = [&](u32 at) { return u32(file[at] | file[at + 1] << 8 | file[at + 2] << 16 | file[at + 3] << 24); };
    auto text = [&](u32 at) { return std::string((const char*)file.data() + at); };
    CHECK(file.size(), 256 + 8 * 32 + 2 * 36);
    if(file.size() != 256 + 8 * 32 + 2 * 36) return;
    CHECK(std::string((const char*)file.data(), 8) == "PHGEPROF", true);
    CHECK(word(8), 1); CHECK(word(12), 256); CHECK(word(16), 32); CHECK(word(20), 8); CHECK(word(24), 14);
    CHECK(text(32) == "phobos test" && text(48) == "UCUS98653" && text(64) == "a title", true);
    CHECK(word(140), 1); CHECK(word(144), 222); CHECK(word(148), 111);
    CHECK(word(160), Kernel::DevkitVersion); CHECK(word(172), 0xffff'ffff); CHECK(word(176), 0x801f'fffe);
    CHECK(word(188), 8); CHECK(word(192), 256 + 8 * 32); CHECK(word(196), 2);
    u32 sync = 256 + 3 * 32;
    CHECK(file[sync], GeProfile::ListSync); CHECK(file[sync + 1], 1);
    CHECK(word(sync + 4), r[3].entry); CHECK(word(sync + 8), r[3].exit); CHECK(word(sync + 12), r[3].ge);
    CHECK(word(sync + 16), Kernel::GeListIDs); CHECK(word(sync + 28), 0);
    u32 table = 256 + 8 * 32;
    CHECK(word(table + 36), profile.threads[1].first);
    CHECK(text(table + 4) == "interrupt" && text(table + 40) == "main", true);
  }

  //the header's flag 16, Phobos's alone: the model's charges are the PSP's GE timing's (the option), not Sony's peaks'
  for(bool psp : {false, true}) {
    KernelMachine m;
    m.kernel.gePspTiming = psp;
    if(psp) m.system.threadGE(false);  //(as System::threadGE() keeps it with the PSP's GE timing)
    GeProfile profile;
    m.kernel.geProfileStart(profile);
    m.kernel.geProfileStop();
    CHECK(profile.pspTiming, psp);
    auto file = profile.file("phobos test", "UCUS98653", "a title");
    CHECK(file.size() >= 28, true);
    if(file.size() < 28) return;
    CHECK(u32(file[24] | file[25] << 8 | file[26] << 16 | file[27] << 24), psp ? 2 | 4 | 8 | 16 : 2 | 4 | 8);
  }

  KernelMachine m;
  m.system.memory.write(4, Callbacks + 8, 0x0880'4100);
  for(u32 n = 0; n < 16; n++) CHECK(m.call("sceGeSetCallback", {Callbacks}), n);
  GeProfile profile;
  m.kernel.geProfileStart(profile);
  CHECK(m.call("sceGeSetCallback", {Callbacks}), Kernel::ErrorOutOfMemory);
  CHECK(m.call("sceGeSetCallback", {0x10}), Kernel::ErrorOutOfMemory);  //(nothing there)
  m.kernel.geProfileStop();
  CHECK(profile.records.size(), 2);
  if(profile.records.size() != 2) return;
  CHECK(profile.records[0].a, 0); CHECK(profile.records[0].b, 0); CHECK(profile.records[0].c, Callbacks);
  CHECK(profile.records[1].c, 0x10);
  CHECK(m.system.unmapped.size(), 0);
}

//The GE profile's calls whose thread's callbacks run in them, recorded to where the call returns, with what it gave:
//a callback notified before sceDisplayWaitVblankStartCB runs first, in the wait, and itself waits for the blank in
//sceDisplayWaitVblankStart (a call of its own, recorded too), the wait ending as it's done; and in the blank,
//sceDisplayWaitVblankCB returns 1 at once, its callback (a thousand turns of a loop) run as it returns.
static auto geProfileCallbacks() -> void {
  KernelMachine m;
  Assembler callback{m, 0x0880'3000};  //(count, word, argument): waits for the blank the first time, then loops
  callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 0, sp));
  callback.li(t0, Turn); callback.put(lw(t1, 0, t0)); callback.put(addiu(t2, t1, 1)); callback.put(sw(t2, 0, t0));
  callback.put(bne(t1, zero, 3)); callback.put(nop);  //past the wait
  callback.call("sceDisplayWaitVblankStart");
  callback.li(t3, 1000);
  u32 loop = callback.here();
  callback.put(addiu(t3, t3, -1));
  callback.put(bne(t3, zero, int32_t(loop - (callback.here() + 4)) / 4)); callback.put(nop);
  callback.put(lw(ra, 0, sp)); callback.put(addiu(sp, sp, 16));
  callback.put(jr(ra)); callback.put(addu(v0, zero, zero));
  Assembler main{m, 0x0880'1000};
  main.li(s1, KernelMachine::Results);
  main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
  main.call("sceKernelCreateCallback");
  main.put(addu(s0, v0, zero));
  main.put(addu(a0, s0, zero)); main.li(a1, 7); main.call("sceKernelNotifyCallback");
  main.call("sceDisplayWaitVblankStartCB");
  main.put(sw(v0, 0, s1));
  main.put(addu(a0, s0, zero)); main.li(a1, 8); main.call("sceKernelNotifyCallback");
  main.call("sceDisplayWaitVblankCB");
  main.put(sw(v0, 4, s1));
  main.call("sceKernelExitGame");
  GeProfile profile;
  m.kernel.geProfileStart(profile);
  runMain(m, false);
  m.kernel.geProfileStop();
  CHECK(m.system.memory.read(4, KernelMachine::Results), 0);
  CHECK(m.system.memory.read(4, KernelMachine::Results + 4), 1);
  CHECK(m.system.memory.read(4, Turn), 2);
  auto& r = profile.records;
  CHECK(r.size(), 4);
  if(r.size() != 4) return;
  u32 vblank = u32(Kernel::VblankCycles / 333);
  CHECK(r[0].kind, GeProfile::WaitVblankStartCB);
  CHECK(r[1].kind, GeProfile::WaitVblankStart);
  CHECK(r[2].kind, GeProfile::Vblank);
  CHECK(r[3].kind, GeProfile::WaitVblankCB);
  CHECK(r[1].thread, 1); CHECK(r[1].exit, vblank); CHECK(r[1].result, 0);
  CHECK(r[0].thread, 1); CHECK(r[0].exit >= r[1].exit + 4 && r[0].exit < vblank + 100, true); CHECK(r[0].result, 0);
  CHECK(r[2].entry, vblank);
  CHECK(r[3].entry >= r[0].exit && r[3].exit >= r[3].entry + 4, true); CHECK(r[3].result, 1);
}

//The GE profile's call whose thread's callback is notified while it waits in sceDisplayWaitVblankStartCB (by a worse
//thread that runs once main waits): the callback runs in the wait, its record put aside meanwhile, and the call still
//ends at the blank with 0.
static auto geProfileNotifiedInWait() -> void {
  KernelMachine m;
  Assembler callback{m, 0x0880'3000};
  callback.put(jr(ra)); callback.put(addu(v0, zero, zero));
  Assembler other{m, 0x0880'2000};
  other.li(t0, KernelMachine::Results); other.put(lw(a0, 8, t0)); other.li(a1, 7);
  other.call("sceKernelNotifyCallback");
  other.li(a0, 0); other.call("sceKernelExitThread");
  Assembler main{m, 0x0880'1000};
  main.li(s1, KernelMachine::Results);
  main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
  main.call("sceKernelCreateCallback");
  main.put(sw(v0, 8, s1));
  main.li(a0, m.string("other")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
  main.li(t0, 0); main.li(t1, 0);
  main.call("sceKernelCreateThread");
  main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
  main.call("sceKernelStartThread");
  main.call("sceDisplayWaitVblankStartCB");
  main.put(sw(v0, 0, s1));
  main.call("sceKernelExitGame");
  GeProfile profile;
  m.kernel.geProfileStart(profile);
  runMain(m, false);
  m.kernel.geProfileStop();
  CHECK(m.system.memory.read(4, KernelMachine::Results), 0);
  auto& r = profile.records;
  CHECK(r.size(), 2);
  if(r.size() != 2) return;
  u32 vblank = u32(Kernel::VblankCycles / 333);
  CHECK(r[0].kind, GeProfile::WaitVblankStartCB);
  CHECK(r[0].exit, vblank); CHECK(r[0].result, 0);
  CHECK(r[1].kind, GeProfile::Vblank);
}

auto geTests() -> Tests {
  return {
    {"ge commands", geCommands}, {"ge moving", geMoving}, {"ge stops", geStops}, {"ge vertices", geVertices},
    {"ge clear", geClear}, {"ge transfer", geTransfer}, {"ge driver", geDriver}, {"ge base kept", geBaseKept},
    {"ge saved state", geSaved}, {"ge endless list", geEndless}, {"ge drawn on several threads", geThreads},
    {"ge deferred texture busy", geDeferredBusy}, {"ge deferred texture's texels as its jobs take them", geDeferredReach},
    {"ge loads through the depth buffer's views wait for the threads",
    geDepthViewLoads}, {"ge batches round the ring", geRing},
    {"ge drawn over its own texture", geOwnTexture}, {"ge 3D triangles over their own texture", geOwnTexture3D},
    {"ge transfers among batches", geTransfers},
    {"ge callbacks", geCallbacks}, {"ge suspend", geSuspend}, {"ge finish order", geFinishOrder},
    {"ge pause", gePause}, {"ge callbacks see the ge stopped", geCallbackState}, {"ge pause window", gePauseWindow},
    {"ge lists queued twice", geQueuedTwice}, {"ge suspending signals' callbacks", geSuspendedCallbacks},
    {"ge calls and threads", geCallsAndThreads}, {"ge break", geBreak},
    {"ge break and callbacks", geBreakCallbacks}, {"ge time", geTime}, {"ge time, the PSP's", geTimePsp},
    {"ge time, the PSP's: a SIGNAL's lead through stall updates", geTimeSignalAcrossRuns},
    {"ge time's interrupt", geTimeInterrupts},
    {"ge FINISH after a quiet function", geQuietFunctions}, {"ge profile", geProfile},
    {"ge profile: calls whose callbacks run in them", geProfileCallbacks},
    {"ge profile: a callback notified in the wait", geProfileNotifiedInWait},
    {"ge 3D settings kept from PRIM to PRIM", geTransformKept}, {"ge vertices and lists drawn first", geVerticesDrawn},
    {"ge vertices read in runs as one at a time", geVertexRuns},
    {"ge vertex floats that are signaling NaNs read quieted, in runs and one at a time", geVertexFloatsQuieted},
    {"event flags", eventFlags}, {"event flag waiting", eventFlagWaiting}, {"display picture", displayPicture},
    {"gu program", guProgram}, {"copy sample", copySample},
    {"ge on its own thread draws the same", geThreadSame},
    {"ge on its own thread: a read waits only where the thread may still change the bytes", geThreadReads},
    {"ge on its own thread: a store waits only for pixels the drawing threads may still draw", geThreadConflicts},
    {"ge on its own thread: thousands of software PRIMs behind, a load costs what it does with a few",
     geThreadManyBehind},
    {"ge on its own thread: vertices by indices copied from the least to the most", geThreadIndexedCopies},
    {"ge on its own thread: a store onto a texel a software PRIM reads but doesn't draw over waits for it",
     geThreadTextureConflict},
    {"ge on its own thread: a load of software depth through the depth views waits for it", geThreadSoftwareDepth},
    {"ge on its own thread: more runs than it keeps counts of before a FINISH, the GE's time as without it",
     geThreadRunsKept},
    {"ge on its own thread: a texture where nothing is, its reads reported as without it", geThreadUnmapped},
    {"ge on its own thread: the go split, an alarm's call held back and interrupts turned on inside a go",
     geThreadGoSplit},
    {"ge on its own thread: deferred texture decodes read as of their own PRIMs", geThreadDeferredDecodes},
    {"ge on its own thread: a block transfer outside a list after commands set from outside", geThreadTransferOutside},
    {"ge versions of memory, and neither thread waiting while it holds what the other waits for", geVersions},
  };
}

}
