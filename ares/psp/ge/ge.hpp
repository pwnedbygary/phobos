#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
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
    Region1 = 0x15, Region2 = 0x16, LightingEnable = 0x17, LightEnable0 = 0x18, DepthClipEnable = 0x1c,
    CullFaceEnable = 0x1d,
    TextureMappingEnable = 0x1e, FogEnable = 0x1f, DitherEnable = 0x20, AlphaBlendEnable = 0x21,
    AlphaTestEnable = 0x22, DepthTestEnable = 0x23, StencilTestEnable = 0x24, AntiAliasEnable = 0x25,
    ColorTestEnable = 0x27,
    LogicOpEnable = 0x28, BoneMatrixNumber = 0x2a, BoneMatrixData = 0x2b, MorphWeight0 = 0x2c,
    WorldMatrixNumber = 0x3a, WorldMatrixData = 0x3b, ViewMatrixNumber = 0x3c, ViewMatrixData = 0x3d,
    ProjectionMatrixNumber = 0x3e, ProjectionMatrixData = 0x3f, TextureMatrixNumber = 0x40, TextureMatrixData = 0x41,
    ViewportXScale = 0x42, ViewportYScale = 0x43, ViewportZScale = 0x44, ViewportXCenter = 0x45,
    ViewportYCenter = 0x46, ViewportZCenter = 0x47, TextureScaleU = 0x48, TextureScaleV = 0x49,
    TextureOffsetU = 0x4a, TextureOffsetV = 0x4b, OffsetX = 0x4c, OffsetY = 0x4d,
    ShadeMode = 0x50, NormalReverse = 0x51, MaterialColor = 0x53, MaterialEmissive = 0x54, AmbientColor = 0x55,
    MaterialDiffuse = 0x56, MaterialSpecular = 0x57, AmbientAlpha = 0x58, MaterialSpecularCoefficient = 0x5b,
    AmbientLightColor = 0x5c, AmbientLightAlpha = 0x5d, LightMode = 0x5e, LightType0 = 0x5f, Light0X = 0x63,
    Light0DirectionX = 0x6f, Light0ConstantAttenuation = 0x7b, Light0ExponentAttenuation = 0x87,
    Light0CutoffAttenuation = 0x8b, Light0Ambient = 0x8f, Cull = 0x9b,
    FrameBufferPointer = 0x9c, FrameBufferWidth = 0x9d, DepthBufferPointer = 0x9e, DepthBufferWidth = 0x9f,
    TextureAddress0 = 0xa0, TextureBufferWidth0 = 0xa8, ClutAddress = 0xb0, ClutAddressUpper = 0xb1,
    TransferSource = 0xb2, TransferSourceWidth = 0xb3, TransferDestination = 0xb4, TransferDestinationWidth = 0xb5,
    TextureSize0 = 0xb8, TextureMapMode = 0xc0, TextureShadeMapping = 0xc1, TextureMode = 0xc2, TextureFormat = 0xc3,
    ClutLoad = 0xc4,
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

    auto serialize(serializer& s) -> void {
      s(address);
      s(stall);
      s(offset);
      s(depth);
      s(returnAddress);
      s(returnOffset);
    }
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
    u32 specular = 0;  //lighting's shine, when LIGHT_MODE keeps it apart: added after texturing (lighting.cpp)
  };
  //Where each part of a vertex is, in bytes from its start: each part sits at a multiple of its own size, and a
  //vertex's size is a multiple of its largest part's. Formats are the vertex type's fields: 0 for none.
  struct VertexFormat {
    u32 weightFormat, weights, textureFormat, colorFormat, normalFormat, positionFormat, indexFormat, morphs;
    bool through;  //2D: positions are screen pixels, used as they are
    u32 weightOffset, textureOffset, colorOffset, normalOffset, positionOffset;
    u32 size;      //one vertex, all its morph targets included
  };

  //One of the four lights, as its commands set it (lighting.cpp).
  struct Light {
    bool enabled, directional, spot, specular, powered;  //specular: it shines; powered: its diffuse is sharpened
    float position[3], direction[3], attenuation[3], cutoff, exponent;
    u32 ambient, diffuse, shine;  //its colors (24-bit)
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
    //lighting (lighting.cpp)
    bool lighting, separateSpecular, normalReverse, vertexColor;  //vertexColor: the vertex type has a color
    u32 materialColor;           //MATERIAL_COLOR: bits 0-2, the vertex's color stands for the ambient, diffuse, shine
    u32 materialEmissive, materialAmbient, materialDiffuse, materialSpecular, ambientLight;
    float specularPower, viewDirection[3];
    Light lights[4];
    u32 shadeU, shadeV;          //TEXTURE_SHADE_MAPPING: the lights environment mapping takes u and v from
  };

  //A texture as the commands describe it, gathered once a primitive (texture.cpp).
  struct Sampler {
    u32 address, bufferWidth, width, height, format;  //bufferWidth: texels from one row to the next
    bool swizzled, clampU, clampV, linear;
    u32 clutFormat, clutShift, clutMask, clutOffset;
    const u32* decoded;  //its texels already decoded (Decoded), decodedWidth to a row; or none: read from memory
    u32 decodedWidth;
    u32 decodedRows;     //the rows the primitive may take texels from (draw.cpp), all of them decoded
  };

  //A texture decoded: every texel inside it as 8888, exactly as texel() would read it from memory, so that drawing
  //looks each up in one step instead of reading memory, unswizzling, widening and looking up the palette at every
  //pixel (texture.cpp). It's good only while the memory it came from stays as it was, so the GE watches those
  //pages (Memory::watch()), and a write to any of them, by anyone, throws it away.
  struct TextureKey {
    //everything texel() reads for a texel inside the texture, but how many rows; the palette's settings and
    //contents (by its hash) only for palette indices, 0 otherwise
    u32 address, bufferWidth, format, width, swizzled;  //width: the texels kept a row (at most 512)
    u32 clutFormat, clutShift, clutMask, clutOffset;
    u64 clutHash;
    auto operator==(const TextureKey&) const -> bool = default;
    struct Hash {
      auto operator()(const TextureKey& k) const -> size_t {
        u64 h = k.clutHash ^ u64(k.address) << 32 ^ k.bufferWidth << 20 ^ k.format << 16 ^ k.width << 4;
        h ^= u64(k.swizzled) << 31 ^ u64(k.clutFormat) << 40 ^ u64(k.clutShift) << 44 ^ u64(k.clutMask) << 50;
        h ^= u64(k.clutOffset) << 58;
        h *= 0x9e37'79b9'7f4a'7c15ull;
        return size_t(h ^ h >> 29);
      }
    };
  };
  struct Decoded {
    TextureKey key;
    u32 rows = 0;             //the texture's rows kept, from its top (at most 512): the most a primitive has reached
    std::vector<u8> palette;  //for palette indices: the palette it was decoded with (clut, all of it)
    u32 paletteChecked = 0;   //the palette's version (clutVersion) last found to be that palette
    u32 firstPage = 0, lastPage = 0;  //the pages it came from (Memory::pagesOf())
    std::list<Decoded*>::iterator place;  //where it is in the cache's list, the last used first
    std::vector<u32> texels;  //key.width x rows
  };
  struct TextureCache {
    std::unordered_map<TextureKey, std::shared_ptr<Decoded>, TextureKey::Hash> entries;
    std::unordered_map<u32, std::vector<Decoded*>> pages;  //the decoded textures that came from each page
    std::list<Decoded*> recent;                            //all of them, the last used first
    std::shared_ptr<Decoded> last;                         //the last one found, looked at first
    u64 bytes = 0;
    u64 budget = TextureCacheBudget;                       //(tests may make it smaller)
  };
  static constexpr u64 TextureCacheBudget = 64 << 20;  //the texels kept, in bytes, before those unused longest go

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
    bool depthRange, fog;  //3D only: the depth range test (MIN_Z to MAX_Z), and fog (FOG_ENABLE, not in clear mode)
    u32 minDepth, maxDepth, fogColor;
  };

  //Four pixels side by side in a row, each in a lane of a vector (four.cpp): GCC's and Clang's own vector types,
  //which they compile to the host's SIMD instructions (NEON on ARM64, SSE on x86-64).
  using f32x4 = float __attribute__((vector_size(16)));
  using f64x2 = f64 __attribute__((vector_size(16)));
  using s32x4 = s32 __attribute__((vector_size(16)));
  using u32x4 = u32 __attribute__((vector_size(16)));
  struct Four {
    s32x4 live;       //the lanes still being drawn (all bits set), the others clear
    s32x4 z, depth;   //the pixels' depths, and the depth buffer's there (when it's read)
    s32x4 color[4];   //red, green, blue, alpha: 0-255
    s32x4 fog;        //0-255
  };

  //Where a primitive may draw (draw.cpp): pixels inside left-right and top-bottom (inclusive).
  struct Region { s32 left, top, right, bottom; };
  struct Batch;  //(threads.cpp)

  //What a primitive is drawn with, shared by the jobs it makes: the pixel pipeline's settings, and the texture
  //with the texture function's (or none), all taken from the commands as the primitive met them.
  struct Look {
    PixelState pixel;
    bool textured = false;
    Sampler texture{};       //its filter (linear) is each job's own
    u32 function = 0, environment = 0;  //TEXTURE_FUNCTION's bits 0-2, and TEXTURE_ENVIRONMENT_COLOR
    bool withAlpha = false, doubled = false;
    std::shared_ptr<Decoded> decoded;  //its texels, kept while a job may draw with them
  };

  //A primitive set up for drawing (draw.cpp): everything about it worked out once, so that any of its rows can be
  //drawn on its own (raster.cpp), the same pixels with the same arithmetic as drawing it all at once would. Each
  //kind keeps what its pixels are worked out from.
  struct Job {
    enum class Kind : u32 { Sprite, Triangle, Point, Line } kind;
    const Look* look;
    s32 firstX, lastX, firstY, lastY;  //the pixels it may cover, inside the scissor rectangle
    bool linear;                       //textured: filtered (TEXTURE_FILTER's choice for its size)
    bool fours;                        //drawn four pixels at a time (four.cpp; submit() decides)
    struct Sprite {
      u32 z, color, specular, leftFog, rightFog;
      s32 middle;                      //(sixteenths) where the fog's halves meet
      bool turned, divided;            //divided: 3D, texture coordinates across the perspective
      //2D: the coordinate running across x (u, or v when turned) and the one running down y, each from where it
      //starts (in sixteenths), by a step a pixel
      f64 columnFirst, columnStep, rowFirst, rowStep;
      s32 columnStart, rowStart;
      //3D: the corners' sixteenths, 1 / w across x, the coordinate across x over w, the one down y over w
      s32 left, right, top, bottom;
      f64 leftInverse, rightInverse, leftAcross, rightAcross, topDown, bottomDown;
    } sprite;
    struct Triangle {
      s64 x[3], y[3];                  //the corners (sixteenths), turned clockwise
      float total;                     //twice its area
      bool flat, shines, perspective;
      u32 flatColor, flatSpecular;
      u32 color[3], specular[3];
      float z[3], fog[3], u[3], v[3], q[3], w[3];
      f64 uStart, uAcross, uDown, vStart, vAcross, vDown;  //2D texture coordinates, stepped from (startX, startY)
      s64 startX, startY;
    } triangle;
    struct Point {
      s32 x, y;
      u32 z, color, specular, fog;
      float u, v;
    } point;
    struct Line {
      //Its ends (sixteenths), the left one first, turned so that x runs along it (steep: more rows than columns,
      //x and y swapped); the pixels it lights along x, first to last; at each, the one across is y's at the
      //pixel's middle, rounded down: (y[0] * along + rise * (16x + 8 - x[0])) / (16 * along).
      s64 x[2], y[2], along, rise;  //along: x[1] - x[0], never 0; rise: y[1] - y[0]
      bool steep;
      s32 first, last;
      bool flat, shines, perspective;
      u32 flatColor, flatSpecular;
      u32 color[2], specular[2];
      float z[2], fog[2], u[2], v[2], q[2], w[2];
      f64 uStep, vStep;  //2D texture coordinates: a step a pixel along x, from the left end's
    } line;
  };

  Memory& memory;
  u32 commands[256] = {};  //each command's last word
  u8 clut[1024] = {};      //the palette, as CLUT_LOAD copied it in: textures read it from here, not from memory
  u64 clutHash = 0;        //its contents' hash, and a version that goes up whenever they change
  u32 clutVersion = 0;
  TextureCache textures;   //textures kept decoded (texture.cpp)
  bool fourPixels = true;  //rows drawn four pixels at a time where they may be (four.cpp; tests compare without)
  Registers list;
  u32 vertexAddress = 0, indexAddress = 0;  //where the next vertex and index are read
  bool boxOutside = false;  //the last BOUNDING_BOX was out of sight: BJUMP jumps (list.cpp)
  u32 signalWord = 0, finishWord = 0, endWord = 0;  //what made run() stop: the SIGNAL or FINISH before it, and the END
  //The matrices, as their DATA commands give them: each element a float's top 24 bits (its sign, its exponent and
  //the top 15 bits of its fraction), as sceGeGetMtx hands them back. Bones are eight 4x3 matrices; world, view and
  //texture are 4x3, projection 4x4. Each DATA goes where the matrix's NUMBER command pointed, then on to the next.
  u32 bones[96] = {}, world[12] = {}, view[12] = {}, projection[16] = {}, textureMatrix[12] = {};
  u32 boneIndex = 0, worldIndex = 0, viewIndex = 0, projectionIndex = 0, textureIndex = 0;
  //What's worth reporting: a command or mode not emulated yet, a list that went wrong. Each is reported once.
  std::function<auto (const std::string& text) -> void> log;

  //ge.cpp
  GE(Memory& memory);
  ~GE();
  auto power() -> void;
  auto note(const std::string& text) -> void;
  auto serialize(serializer& s) -> bool;
  static auto float24(u32 argument) -> float;

  //list.cpp
  auto run(u64 budget) -> Stop;
  auto run(u64 budget, u64& ran) -> Stop;  //and how many commands ran
  auto relative(u32 argument) const -> u32;

  //vertex.cpp
  auto vertexFormat() const -> VertexFormat;
  auto readVertex(u32 address, const VertexFormat& format) -> Vertex;
  template<typename Read> auto readVertexWith(const Read& read, u32 address, const VertexFormat& format) -> Vertex;
  auto readIndex(u32 n, const VertexFormat& format) -> u32;

  //lighting.cpp
  auto lightingState(Transform& t) const -> void;
  auto light(Vertex& vertex, const float world[3], const float normal[3], const Transform& t) const -> void;

  //transform.cpp
  auto transformState() const -> Transform;
  auto transform(Vertex& vertex, const Transform& t) -> void;
  auto project(Vertex& vertex, const Transform& t, bool clipped) const -> void;
  auto clipTriangle(const Look& look, const Transform& t, const Vertex& a, const Vertex& b, const Vertex& c,
                    s32 facing) -> void;
  auto clipLine(const Look& look, const Transform& t, const Vertex& a, const Vertex& b) -> void;
  auto clipPosition(const Vertex& vertex, const Transform& t, float clip[4]) const -> void;

  //draw.cpp
  auto lookFor(const PixelState& pixel, const Sampler* texture) const -> Look;
  auto primitive(u32 kind, u32 count) -> void;
  auto submit(Job& job) -> void;
  auto rectangle(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto triangle(const Look& look, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing, bool perspective)
    -> void;
  auto point(const Look& look, const Vertex& at) -> void;
  auto line(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto readVertices(u32 count, const VertexFormat& format) -> void;
  auto boundingBox(u32 count) -> bool;
  auto rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                bool perspective) -> void;

  //four.cpp
  auto fourFriendly(const Job& job) const -> bool;
  template<u32 Format> auto depthFirst(const PixelState& p, s32 x, s32 y, Four& four) -> bool;
  auto texelsFour(const Look& look, bool linear, s32x4 live, const s32x4 (&u)[3], const s32x4 (&v)[3],
                  s32x4 (&texel)[4]) const -> void;
  auto combineFour(const Look& look, s32x4 (&color)[4], const s32x4 (&texel)[4]) const -> void;
  template<u32 Format> auto pixelsFour(const PixelState& p, s32 x, s32 y, Four& four) -> void;
  template<u32 Format> auto spriteFours(const Job& job, s32 fromY, s32 toY) -> void;
  template<u32 Format> auto triangleFours(const Job& job, s32 fromY, s32 toY) -> void;

  //raster.cpp
  template<u32 Format> auto shadeAs(const Look& look, bool linear, s32 x, s32 y, u32 z, u32 color, u32 specular,
                                    float u, float v, u32 fog) -> void;
  template<u32 Format> auto spriteRows(const Job& job, s32 fromY, s32 toY) -> void;
  template<u32 Format> auto triangleRows(const Job& job, s32 fromY, s32 toY) -> void;
  template<u32 Format> auto lineRows(const Job& job, s32 fromY, s32 toY) -> void;
  template<u32 Format> auto rasterizeAs(const Job& job, s32 fromY, s32 toY) -> void;
  auto rasterize(const Job& job, s32 fromY, s32 toY) -> void;

  //texture.cpp
  auto sampler() const -> Sampler;
  auto texel(const Sampler& texture, s32 u, s32 v) -> u32;
  auto sample(const Sampler& texture, float u, float v) -> u32;
  auto sampleWith(const Sampler& texture, bool linear, float u, float v) -> u32;
  struct TexelAxis { s32 first, second, fraction; };
  static auto texelAxis(float coordinate, u32 size, bool clamp, bool linear) -> TexelAxis;
  auto fetch(const Sampler& texture, s32 x, s32 y) -> u32;
  auto filtered(const Sampler& texture, TexelAxis u, TexelAxis v) -> u32;
  auto textureFunction(u32 color, u32 texel) const -> u32;
  auto combine(const Look& look, u32 color, u32 texel) const -> u32;
  auto loadClut() -> void;
  auto paletteChanged() -> void;
  auto textureBytes(const Sampler& texture, u32 rows, u32& low, u32& high) const -> void;
  auto decode(Sampler& texture, const PixelState& pixel, const Region& region, u32 rows)
    -> std::shared_ptr<Decoded>;
  auto textureWritten(u32 page) -> void;
  auto forget(Decoded* entry) -> void;
  auto dropTextures() -> void;

  //pixel.cpp
  auto pixelState() const -> PixelState;
  template<u32 Format> auto drawPixelAs(const PixelState& pixel, s32 x, s32 y, u32 z, u32 color, u32 fog) -> void;

  //transfer.cpp
  auto transfer() -> void;

  //threads.cpp
  auto setThreads(u32 count) -> void;
  auto flush() -> void;
  auto launch(bool returning) -> void;
  auto startBands(Batch& batch) -> void;
  auto drew(Batch& batch) -> bool;
  auto settle() -> void;
  auto clearBatch(Batch& batch) -> void;
  auto drawnFirst(u32 address, u32 size) -> void;
  auto defer(const PixelState& pixel, const Region& region) -> bool;
  auto record(const Job& job) -> void;
  auto drawBands(Batch& batch) -> void;
  auto worker() -> void;

  std::vector<Vertex> primitiveVertices;  //the primitive being drawn's (draw.cpp)

  //The bytes of VRAM the primitive being drawn may draw over (draw.cpp): its frame buffer's, and its depth buffer's.
  struct Touched { u32 low = ~0u, high = 0; };
  std::array<Touched, 2> touched;

  static constexpr u32 VRAMPages = (2 << 20) / 4096;  //VRAM's 2 MiB in 4 KiB pages (memory.hpp)

  //Drawing on several threads (threads.cpp). While the GE runs a list, the primitives it meets wait in a batch,
  //set up, and are drawn together, in bands of rows shared out among the threads, before anything could see them.
  struct Batch {
    std::deque<Look> looks;   //its primitives' settings, and their jobs in order
    std::vector<Job> jobs;
    u64 work = 0;             //its pixels, roughly (its jobs' boxes)
    s32 top = 0, bottom = -1;           //its rows
    bool targeted = false;              //its render target: every primitive in a batch draws into the same one
    u32 frameBuffer = 0, stride = 0, format = 0, depthBuffer = 0, depthStride = 0;
    s32 left = 0, right = 0, upper = 0, lower = 0;  //its area: its primitives' scissor rectangles together
    bool depth = false;                 //some of them reach the depth buffer
    std::bitset<VRAMPages> pending;     //VRAM's 4 KiB pages it may draw over
    bool launched = false;              //handed to the workers: being drawn, or next once the one before is done
    u32 users = 0;                      //threads drawing its bands now (under the mutex)
    u32 bands = 0;
    std::atomic<u32> nextBand{0}, bandsLeft{0};
  };
  struct Drawing {
    u32 threads = 1;          //how many threads draw ('GE Threads'): 1, each primitive at once on the GE's own
    bool deferring = false;   //run() is running: primitives wait in the batch
    bool recording = false;   //the primitive being set up waits in the batch
    Batch batches[2];         //one waiting for its primitives, the other maybe still being drawn
    Batch* batch = &batches[0];         //the one primitives go into
    Batch* drawn = nullptr;             //the one the workers draw (under the mutex)
    Batch* queued = nullptr;            //the one to draw once that's done (under the mutex)

    std::vector<std::thread> workers;   //the threads besides the GE's own
    std::mutex mutex;
    std::condition_variable wake, finished;
    u64 round = 0;            //goes up as each batch starts being drawn, which the workers wake for
    bool quit = false;
    u64 shared = 8192;        //a batch with fewer pixels is drawn on the GE's thread alone (tests may make it 0)
  } drawing;
  static constexpr s32 BandRows = 8;  //the rows in a band

  Stop pending = Stop::Ended;  //what the next END means: a FINISH or SIGNAL before it changes it
  std::set<std::string> noted;
};

}
