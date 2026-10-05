//The GE (ares/psp/ge) and its driver (ares/psp/kernel/ge.cpp), with the kernel pieces they need: display lists written
//here, run by the GE alone (moving about a list and stopping, registers and matrices, vertices, clearing, block
//transfers) and through the driver (the queue, stall addresses, syncing, callbacks into programs written here,
//signals, saved state); event flags; calls into the program; the picture on the screen; and, with PSP_TEST_PROGRAMS,
//a GU program of Phobos's own and pspsdk's GU sample "copy".
#include "kernel-machine.hpp"

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace allegrex_test::psp {

constexpr u32 ListA = 0x0894'0000, ListB = 0x0895'0000, Vertices = 0x0896'0000, Saved = 0x0897'0000;
constexpr u32 Image = 0x0898'0000;
constexpr u32 VRAM = Memory::VRAMBase;

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
//third CALL, or a RET with no CALL, stops the GE.
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
  ListWriter stray{s.memory, ListA};
  stray.put(GE::Return);
  ge.list = {};
  ge.list.address = ListA;
  CHECK(u32(ge.run(100)), u32(GE::Stop::Faulted));
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
//order, as soon as the program's call returns, with the ids, their arguments, where the list had got to and the
//global pointer the program had; the program finds its result as it left it.
static auto geCallbacks() -> void {
  for(bool recompile : {false, true}) {
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
    CHECK(memory.read(4, Signaled + 8), ListA + 8);
    CHECK(memory.read(4, Signaled + 12), 0x0812'3456);
    CHECK(memory.read(4, Signaled + 16), 1);
    CHECK(memory.read(4, Finished), 42);
    CHECK(memory.read(4, Finished + 4), 0x2222);
    CHECK(memory.read(4, Finished + 8), ListA + 16);
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
      if(m.kernel.display.frameBuffer != shown) swaps++, shown = m.kernel.display.frameBuffer;
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

auto geTests() -> Tests {
  return {
    {"ge commands", geCommands}, {"ge moving", geMoving}, {"ge stops", geStops}, {"ge vertices", geVertices},
    {"ge clear", geClear}, {"ge transfer", geTransfer}, {"ge driver", geDriver}, {"ge base kept", geBaseKept},
    {"ge saved state", geSaved}, {"ge endless list", geEndless},
    {"ge callbacks", geCallbacks}, {"ge suspend", geSuspend}, {"ge finish order", geFinishOrder},
    {"ge pause", gePause},
    {"ge calls and threads", geCallsAndThreads},
    {"event flags", eventFlags}, {"event flag waiting", eventFlagWaiting}, {"display picture", displayPicture},
    {"gu program", guProgram}, {"copy sample", copySample},
  };
}

}
