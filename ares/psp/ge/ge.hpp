#pragma once

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

//The GE (the "graphics engine"): the PSP's graphics chip.
//
//The CPU doesn't hand the GE its work a piece at a time. It writes a display list in memory, a run of 32-bit
//commands, and the GE reads them and carries them out by itself while the CPU gets on with the next frame. A
//command's top 8 bits say which it is; its low 24 bits are its argument. Most commands only set a piece of the GE's
//state (where the frame buffer is, how to blend, a texture's size), and the GE keeps the last word of each, which the
//system can read back. Some act: PRIM draws, TRANSFER_START copies a block of pixels, JUMP, CALL and RET move about
//the list, and FINISH, SIGNAL and END stop it.
//
//A list needn't be finished before the GE starts on it: the CPU says where the GE must stop, the "stall address",
//and moves it on as it writes more. The GE waits there meanwhile.
//
//Addresses: 24 bits of argument can't hold a whole address (the GE's are 28 bits wide), so BASE supplies the top
//four (bits 24-27), and an offset, set by OFFSET_ADDR or ORIGIN, is added; JUMP, CALL and the vertex and index
//addresses all work that way.
//
//The HLE kernel's version of the GE driver (kernel/ge.cpp) runs the GE: it keeps the queue of lists, starts each in
//turn, and deals with whatever made one stop. The GE's work takes no time yet.
//
//Sources: pspsdk's GU library (src/gu, BSD-licensed), which writes display lists, for the commands' numbers and how
//their arguments are laid out; uOFW's reading of the GE driver for the GE's registers; PPSSPP, for what neither
//says (each such place says so).

namespace ares::PlayStationPortable {

struct Memory;

struct GE {
  //The commands the code here refers to, by pspsdk's names for them.
  enum : u32 {
    Nop = 0x00, VertexAddress = 0x01, IndexAddress = 0x02, Primitive = 0x04, Bezier = 0x05, Spline = 0x06,
    BoundingBox = 0x07, Jump = 0x08, ConditionalJump = 0x09, Call = 0x0a, Return = 0x0b, End = 0x0c,
    Signal = 0x0e, Finish = 0x0f, Base = 0x10, VertexType = 0x12, OffsetAddress = 0x13, Origin = 0x14,
    Region1 = 0x15, Region2 = 0x16,
    BoneMatrixNumber = 0x2a, BoneMatrixData = 0x2b, MorphWeight0 = 0x2c,
    WorldMatrixNumber = 0x3a, WorldMatrixData = 0x3b, ViewMatrixNumber = 0x3c, ViewMatrixData = 0x3d,
    ProjectionMatrixNumber = 0x3e, ProjectionMatrixData = 0x3f, TextureMatrixNumber = 0x40, TextureMatrixData = 0x41,
    FrameBufferPointer = 0x9c, FrameBufferWidth = 0x9d, DepthBufferPointer = 0x9e, DepthBufferWidth = 0x9f,
    TransferSource = 0xb2, TransferSourceWidth = 0xb3, TransferDestination = 0xb4, TransferDestinationWidth = 0xb5,
    FrameBufferPixelFormat = 0xd2, ClearMode = 0xd3, Scissor1 = 0xd4, Scissor2 = 0xd5,
    TransferStart = 0xea, TransferSourcePosition = 0xeb, TransferDestinationPosition = 0xec, TransferSize = 0xee,
  };

  //PRIM's kinds of primitive (pspgu.h's GU_POINTS and on).
  enum : u32 { Points, Lines, LineStrip, Triangles, TriangleStrip, TriangleFan, Sprites };

  //Why run() stopped.
  enum class Stop : u32 {
    Stalled,   //at the stall address: the CPU hasn't written further yet
    Finished,  //at an END after a FINISH: the list is done
    Signaled,  //at an END after a SIGNAL: the driver does what the signal asks, then lets the GE go on
    Ended,     //at an END with neither before it: the GE just stops
    Busy,      //out of budget, still going
    Faulted,   //a CALL deeper than the GE's two levels, or a RET with nothing to return to
  };

  //Where the GE is in its list: the registers the driver saves while another list runs, and restores.
  struct Registers {
    u32 address = 0;  //the next command
    u32 stall = 0;    //where to stop before (0: nowhere)
    u32 offset = 0;   //added to addresses (OFFSET_ADDR, ORIGIN)
    u32 depth = 0;    //CALLs not yet returned from; the GE has room for two
    u32 returnAddress[2] = {}, returnOffset[2] = {};
  };

  //A vertex as the vertex type lays it out, its numbers as stored: integers keep their values (the 3D path will
  //scale them; in 2D, "through" mode, they're pixels and texels as they are), colors become 8888 with red in the
  //low byte, the GE's order.
  struct Vertex {
    float weights[8] = {};
    float u = 0, v = 0;
    u32 color = 0;
    float normal[3] = {};
    float x = 0, y = 0, z = 0;
  };
  //Where each part of a vertex is, in bytes from its start: each part sits at a multiple of its own size, and a
  //vertex's size is a multiple of its largest part's. Formats are the vertex type's fields: 0 for none.
  struct VertexFormat {
    u32 weightFormat, weights, textureFormat, colorFormat, normalFormat, positionFormat, indexFormat, morphs;
    bool through;  //2D: positions are screen pixels, used as they are
    u32 weightOffset, textureOffset, colorOffset, normalOffset, positionOffset;
    u32 size;      //one vertex, all its morph targets included
  };

  Memory& memory;
  u32 commands[256] = {};  //each command's last word
  Registers list;
  u32 vertexAddress = 0, indexAddress = 0;  //where the next vertex and index are read
  u32 signalWord = 0, finishWord = 0, endWord = 0;  //what made run() stop: the SIGNAL or FINISH before it, and the END
  //The matrices, as their DATA commands give them: each element a float's top 24 bits (its sign, its exponent and
  //the top 15 bits of its fraction), as sceGeGetMtx hands them back. Bones are eight 4x3 matrices; world, view and
  //texture are 4x3, projection 4x4. Each DATA goes where the matrix's NUMBER command pointed, then on to the next.
  u32 bones[96] = {}, world[12] = {}, view[12] = {}, projection[16] = {}, textureMatrix[12] = {};
  u32 boneIndex = 0, worldIndex = 0, viewIndex = 0, projectionIndex = 0, textureIndex = 0;
  //What's worth reporting: a command or mode not emulated yet, a list that went wrong. Each is reported once.
  std::function<auto (const std::string& text) -> void> log;

  GE(Memory& memory) : memory(memory) {}

  //ge.cpp
  auto power() -> void;
  auto note(const std::string& text) -> void;
  static auto float24(u32 argument) -> float;

  //list.cpp
  auto run(u64 budget) -> Stop;
  auto relative(u32 argument) const -> u32;

  //vertex.cpp
  auto vertexFormat() const -> VertexFormat;
  auto readVertex(u32 address, const VertexFormat& format) -> Vertex;
  auto readIndex(u32 n, const VertexFormat& format) -> u32;

  //draw.cpp
  auto primitive(u32 kind, u32 count) -> void;
  auto clearRectangle(const Vertex& from, const Vertex& to) -> void;
  auto frameBufferAddress() const -> u32;
  auto depthBufferAddress() const -> u32;

  //transfer.cpp
  auto transfer() -> void;

  Stop pending = Stop::Ended;  //what the next END means: a FINISH or SIGNAL before it changes it
  std::set<std::string> noted;
};

}
