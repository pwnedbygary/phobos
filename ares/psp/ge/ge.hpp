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
    Region1 = 0x15, Region2 = 0x16, LightingEnable = 0x17, DepthClipEnable = 0x1c, CullFaceEnable = 0x1d,
    TextureMappingEnable = 0x1e, FogEnable = 0x1f, DitherEnable = 0x20, AlphaBlendEnable = 0x21,
    AlphaTestEnable = 0x22, DepthTestEnable = 0x23, StencilTestEnable = 0x24, ColorTestEnable = 0x27,
    LogicOpEnable = 0x28, BoneMatrixNumber = 0x2a, BoneMatrixData = 0x2b, MorphWeight0 = 0x2c,
    WorldMatrixNumber = 0x3a, WorldMatrixData = 0x3b, ViewMatrixNumber = 0x3c, ViewMatrixData = 0x3d,
    ProjectionMatrixNumber = 0x3e, ProjectionMatrixData = 0x3f, TextureMatrixNumber = 0x40, TextureMatrixData = 0x41,
    ViewportXScale = 0x42, ViewportYScale = 0x43, ViewportZScale = 0x44, ViewportXCenter = 0x45,
    ViewportYCenter = 0x46, ViewportZCenter = 0x47, TextureScaleU = 0x48, TextureScaleV = 0x49,
    TextureOffsetU = 0x4a, TextureOffsetV = 0x4b, OffsetX = 0x4c, OffsetY = 0x4d,
    ShadeMode = 0x50, AmbientColor = 0x55, AmbientAlpha = 0x58, Cull = 0x9b,
    FrameBufferPointer = 0x9c, FrameBufferWidth = 0x9d, DepthBufferPointer = 0x9e, DepthBufferWidth = 0x9f,
    TextureAddress0 = 0xa0, TextureBufferWidth0 = 0xa8, ClutAddress = 0xb0, ClutAddressUpper = 0xb1,
    TransferSource = 0xb2, TransferSourceWidth = 0xb3, TransferDestination = 0xb4, TransferDestinationWidth = 0xb5,
    TextureSize0 = 0xb8, TextureMapMode = 0xc0, TextureMode = 0xc2, TextureFormat = 0xc3, ClutLoad = 0xc4,
    ClutFormat = 0xc5, TextureFilter = 0xc6, TextureWrap = 0xc7, TextureFunction = 0xc9,
    TextureEnvironmentColor = 0xca, FogEnd = 0xcd, FogSlope = 0xce, FogColor = 0xcf,
    FrameBufferPixelFormat = 0xd2, ClearMode = 0xd3, Scissor1 = 0xd4, Scissor2 = 0xd5, MinZ = 0xd6, MaxZ = 0xd7,
    ColorTest = 0xd8, ColorReference = 0xd9, ColorTestMask = 0xda, AlphaTest = 0xdb, StencilTest = 0xdc,
    StencilOperation = 0xdd, DepthTest = 0xde, BlendMode = 0xdf, BlendFixedA = 0xe0, BlendFixedB = 0xe1,
    Dither0 = 0xe2, LogicOp = 0xe6, DepthMask = 0xe7, MaskColor = 0xe8, MaskAlpha = 0xe9,
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

  //A vertex as the vertex type lays it out (vertex.cpp): in 2D, "through" mode, its numbers as stored, pixels and
  //texels; in 3D, fractions. Colors become 8888 with red in the low byte, the GE's order. In 3D, transform.cpp then
  //puts it on the screen: x and y become pixels (whole sixteenths of one), z the depth, u and v texels.
  struct Vertex {
    float weights[8] = {};
    float u = 0, v = 0;
    u32 color = 0;
    float normal[3] = {};
    float x = 0, y = 0, z = 0;
    //3D only: the position in clip space (x, y, z, w), whose w makes texture coordinates perspective-correct; the
    //texture coordinates' divisor (texture projection; 1 otherwise); how much of the color the fog leaves (1: all of
    //it); and whether the vertex is somewhere the GE can't draw, so that no primitive with it is drawn.
    float clip[4] = {0, 0, 0, 1};
    float q = 1, fog = 1;
    bool outside = false;
  };
  //Where each part of a vertex is, in bytes from its start: each part sits at a multiple of its own size, and a
  //vertex's size is a multiple of its largest part's. Formats are the vertex type's fields: 0 for none.
  struct VertexFormat {
    u32 weightFormat, weights, textureFormat, colorFormat, normalFormat, positionFormat, indexFormat, morphs;
    bool through;  //2D: positions are screen pixels, used as they are
    u32 weightOffset, textureOffset, colorOffset, normalOffset, positionOffset;
    u32 size;      //one vertex, all its morph targets included
  };

  //The 3D settings, gathered once a primitive (transform.cpp): the matrices as floats, the viewport, and the rest.
  struct Transform {
    float world[12], view[12], projection[16], textureMatrix[12], bones[96];
    u32 weights;                 //skinning: how many bone matrices each vertex mixes (0: none)
    float scale[3], center[3];   //the viewport: x, y and z
    float offsetX, offsetY;      //where the frame buffer's top left is on the screen, in sixteenths of a pixel
    bool depthClamp;             //DEPTH_CLIP_ENABLE
    u32 mapMode, mapSource;      //TEXTURE_MAP_MODE: bits 0-1, how texture coordinates are made; 8-9, from what
    float textureScale[2], textureOffset[2], textureWidth, textureHeight;
    bool fog, fogForced;         //fogForced: FOG1 isn't a number, so every vertex's fog is fogValue
    float fogEnd, fogSlope, fogValue;
  };

  //A texture as the commands describe it, gathered once a primitive (texture.cpp).
  struct Sampler {
    u32 address, bufferWidth, width, height, format;  //bufferWidth: texels from one row to the next
    bool swizzled, clampU, clampV, linear;
    u32 clutFormat, clutShift, clutMask, clutOffset;
  };

  //The pixel pipeline's settings, gathered once a primitive (pixel.cpp).
  struct PixelState {
    bool clear, clearColor, clearAlpha, clearDepth;
    u32 frameBuffer, stride, format, depthBuffer, depthStride;
    bool alphaTest, colorTest, stencilTest, depthTest, blend, dither, logicOp, depthWrite;
    u32 alphaFunction, alphaReference, alphaMask;
    u32 colorFunction, colorReference, colorMask;
    u32 stencilFunction, stencilReference, stencilMask, stencilFail, stencilDepthFail, stencilPass;
    u32 depthFunction;
    u32 blendSource, blendDestination, blendOperation, fixedA, fixedB;
    s32 ditherMatrix[16];
    u32 logic, writeMask;  //writeMask: the frame buffer bits not to touch (MASK_COLOR, MASK_ALPHA)
    s32 left, top, right, bottom;  //the scissor rectangle and drawing region, inclusive
    u32 low, high;  //the frame buffer bytes touched, for reporting the change
    bool depthRange, fog;  //3D only: the depth range test (MIN_Z to MAX_Z), and fog (FOG_ENABLE, not in clear mode)
    u32 minDepth, maxDepth, fogColor;
  };

  Memory& memory;
  u32 commands[256] = {};  //each command's last word
  u8 clut[1024] = {};      //the palette, as CLUT_LOAD copied it in: textures read it from here, not from memory
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

  //transform.cpp
  auto transformState() const -> Transform;
  auto transform(Vertex& vertex, const Transform& t) -> void;
  auto project(Vertex& vertex, const Transform& t, bool clipped) const -> void;
  auto clipTriangle(PixelState& pixel, Sampler* texture, const Transform& t, const Vertex& a, const Vertex& b,
                    const Vertex& c, s32 facing) -> void;

  //draw.cpp
  auto primitive(u32 kind, u32 count) -> void;
  auto rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to) -> void;
  auto triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                bool perspective) -> void;
  auto point(PixelState& pixel, Sampler* texture, const Vertex& at) -> void;
  auto shade(PixelState& pixel, Sampler* texture, s32 x, s32 y, u32 z, u32 color, float u, float v, u32 fog) -> void;

  //texture.cpp
  auto sampler() const -> Sampler;
  auto texel(const Sampler& texture, s32 u, s32 v) -> u32;
  auto sample(const Sampler& texture, float u, float v) -> u32;
  auto textureFunction(u32 color, u32 texel) const -> u32;
  auto loadClut() -> void;

  //pixel.cpp
  auto pixelState() const -> PixelState;
  auto drawPixel(PixelState& pixel, s32 x, s32 y, u32 z, u32 color, u32 fog = 255) -> void;

  //transfer.cpp
  auto transfer() -> void;

  Stop pending = Stop::Ended;  //what the next END means: a FINISH or SIGNAL before it changes it
  std::set<std::string> noted;
};

}
