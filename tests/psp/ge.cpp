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

//A picture drawn off the screen and then drawn with (render to texture), its decode left till its batch starts
//(ge/threads.cpp): till then the texture's pages stay busy for the CPU, as the pages a batch draws over do, and the
//CPU's store into the picture just after the list lands after the sprite drew with it, its load of the sprite's pixel
//finding it drawn and its load of the texel finding what it stored. It comes out as drawing it all before the CPU runs
//on does (one thread), with batches shared out however small; the sprite is drawn four pixels at a time, the texture
//decoded by then. Into a frame buffer narrower than itself (32 pixels) the sprite can't wait in a batch (defer()):
//it's drawn at once, after the batch before it, its texture (decoded for no batch) read from memory as it draws.
static auto geDeferredBusy() -> void {
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

//More batches than the GE has (ge/threads.cpp's Batches), each launched as the render target changes, round and
//round, three targets drawn into in turn, each also drawn with by the next (render to texture): the GE's thread fills
//each batch again only once it's drawn. It comes out as on one thread, with 2, 4 and 8 threads, every batch shared.
static auto geRing() -> void {
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
  auto drawn = [&](u32 threads) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = 0;
    build(m.system.memory);
    m.call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.memory.vram;
  };
  auto one = drawn(1);
  for(u32 threads : {2u, 4u, 8u}) CHECK(drawn(threads) == one, true);
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
  CHECK(m.kernel.geDoneAt, start + 1536);
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
  CHECK(m.kernel.geDoneAt, start + 1027);
  u32 behind = enqueue(ListB);
  CHECK(m.call("sceGeListSync", {behind, 1}), 1);  //queued
  m.kernel.run(1028);
  CHECK(m.call("sceGeListSync", {id, 1}), 0);
  CHECK(m.call("sceGeListSync", {behind, 1}), 2);  //taken up as the first was done with
  CHECK(m.kernel.geDoneAt, start + 1027 + 951);
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
    CHECK(m.kernel.geDoneAt, first + 1027);
    m.kernel.cycles += late ? 2000 : 1000;  //(time passing, which nothing waits on while the list stalls)
    u64 second = m.kernel.cycles;
    CHECK(m.call("sceGeListUpdateStallAddr", {id, 0}), 0);
    CHECK(m.kernel.geDoneAt, late ? second + 513 : first + 1027 + 513);
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

auto geTests() -> Tests {
  return {
    {"ge commands", geCommands}, {"ge moving", geMoving}, {"ge stops", geStops}, {"ge vertices", geVertices},
    {"ge clear", geClear}, {"ge transfer", geTransfer}, {"ge driver", geDriver}, {"ge base kept", geBaseKept},
    {"ge saved state", geSaved}, {"ge endless list", geEndless}, {"ge drawn on several threads", geThreads},
    {"ge deferred texture busy", geDeferredBusy}, {"ge batches round the ring", geRing},
    {"ge drawn over its own texture", geOwnTexture}, {"ge transfers among batches", geTransfers},
    {"ge callbacks", geCallbacks}, {"ge suspend", geSuspend}, {"ge finish order", geFinishOrder},
    {"ge pause", gePause}, {"ge callbacks see the ge stopped", geCallbackState}, {"ge pause window", gePauseWindow},
    {"ge lists queued twice", geQueuedTwice}, {"ge suspending signals' callbacks", geSuspendedCallbacks},
    {"ge calls and threads", geCallsAndThreads}, {"ge break", geBreak},
    {"ge break and callbacks", geBreakCallbacks}, {"ge time", geTime}, {"ge time's interrupt", geTimeInterrupts},
    {"event flags", eventFlags}, {"event flag waiting", eventFlagWaiting}, {"display picture", displayPicture},
    {"gu program", guProgram}, {"copy sample", copySample},
  };
}

}
