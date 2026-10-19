#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
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
//turn, and deals with whatever made one stop. The GE carries out a list's commands at once, every pixel drawn there
//and then; what they would take a PSP's GE to draw (owed) is what the driver waits for before the program hears that
//the list is done (Kernel::geTime()).
//
//Sources: pspsdk's GU library (src/gu, BSD-licensed), which writes display lists, for the commands' numbers and how
//their arguments are laid out; uOFW's reading of the GE driver for the GE's registers; PPSSPP, for what neither
//says (each such place says so).

#include "thread.hpp"
#include "versions.hpp"

namespace ares::PlayStationPortable {

struct Memory;
//(the render GE's reads as of a PRIM's place, with what the model declares it may read: GE::AsOf)
struct ReadsAsOf { u64 place = 0; bool declared = false; GeModel::Interest interest; };

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
    PatchCullEnable = 0x26, ColorTestEnable = 0x27,
    LogicOpEnable = 0x28, BoneMatrixNumber = 0x2a, BoneMatrixData = 0x2b, MorphWeight0 = 0x2c,
    PatchDivision = 0x36, PatchPrimitive = 0x37, PatchFacing = 0x38,
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
    ClutFormat = 0xc5, TextureFilter = 0xc6, TextureWrap = 0xc7, TextureLevel = 0xc8, TextureFunction = 0xc9,
    TextureEnvironmentColor = 0xca, TextureFlush = 0xcb, FogEnd = 0xcd, FogSlope = 0xce, FogColor = 0xcf,
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
    Faulted,   //a CALL deeper than the GE's two levels
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
    bool far = false, near = false;  //3D: z / w past 1 (by 2^-15), or past -1: outside the depths (transform.cpp)
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
    float toLight[3], halfway[3];  //directional: the way to it, and half way to the viewer's, each one long
    s32 products[3][4];  //its ambient, diffuse and shine times the material's, each channel as the GE multiplies
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
    s32 unlit[4];                //each channel's emissive plus the ambient light on the material's ambient
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
    //of those, the rows from decodedTop and the columns decodedLeft to decodedRight (not including) hold every texel it
    //can take (fetch() checks these); a deferred copy (ensureDecoded()) has every texel it can take decoded, but not
    //necessarily all of that rectangle
    u32 decodedTop, decodedLeft, decodedRight;
    const u8* bytes;     //read from memory: where its bytes are in the host's memory, when they're all there
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
    //The bytes it was decoded from (none where they aren't side by side in the host's memory, or would be more than
    //its texels', which alone count against the budget), and whether one of their pages was written since
    //(textureWritten()): looked at again before it's next drawn with
    std::vector<u8> source;
    bool suspect = false;
    bool deferred = false;  //decoded for one batch alone (ensureDecoded()), not kept
    //A hardware renderer's part (fillDecoded()): only the texels in rows top to rows - 1 and columns left to right -
    //1 decoded, packed (right - left to a row, from row top), the source the bytes of those rows alone. The GE never
    //draws from one: the renderer takes it, or the GE decodes the texture afresh for itself.
    bool part = false;
    u32 top = 0, left = 0, right = 0;
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

  //Four pixels side by side in a row (four.cpp), or four corners of a renderer's triangles (draw.cpp's
  //meshTriangles()), each in a lane of a vector: GCC's and Clang's own vector types, which they compile to the host's
  //SIMD instructions (NEON on ARM64, SSE on x86-64).
  using f32x4 = float __attribute__((vector_size(16)));
  using f64x2 = f64 __attribute__((vector_size(16)));
  using u64x2 = u64 __attribute__((vector_size(16)));
  using s32x4 = s32 __attribute__((vector_size(16)));
  using u32x4 = u32 __attribute__((vector_size(16)));
  struct Four {
    s32x4 live;       //the lanes still being drawn (all bits set), the others clear
    s32x4 inside;     //the lanes inside the row (all bits set)
    bool full;        //all four inside it
    s32x4 z, depth;   //the pixels' depths, and the depth buffer's there (when it's read)
    s32x4 passed;     //the lanes passing the depth test: with the stencil test, those failing it stay live
    s32x4 color[4];   //red, green, blue, alpha: 0-255
    s32x4 fog;        //0-255
  };

  //Where a primitive may draw (draw.cpp): pixels inside left-right and top-bottom (inclusive).
  struct Region { s32 left, top, right, bottom; };
  //Where a primitive's pixels can be, inside its region, and the depths they can have, lowest and highest (0-65535):
  //its region and every depth where they can't be told (draw.cpp's reachedBy()).
  struct Reached { Region region; u32 lowest, highest; };
  struct Batch;  //(threads.cpp)
  struct Renderer;

  //The texels a hardware renderer may take for a primitive (draw.cpp's texelBox()): rows top to bottom and columns
  //left to right, inclusive, inside the texture (at most 512 by 512); or, not known, any of them. (Made with {}.)
  struct TexelBox {
    bool known;
    u32 top, bottom, left, right;
  };

  //Texels of a texture inside it (at most 512 by 512), by row and by column: those in a marked row and a marked
  //column; or all of them.
  struct Reach {
    std::array<u64, 8> rows{}, columns{};
    bool all = false;
    static auto mark(std::array<u64, 8>& bits, u32 first, u32 last) -> void;  //(first to last, inclusive)
  };

  //What a primitive is drawn with, shared by the jobs it makes: the pixel pipeline's settings, and the texture
  //with the texture function's (or none), all taken from the commands as the primitive met them.
  struct Look {
    PixelState pixel;
    bool textured = false;
    Sampler texture{};       //its filter (linear) is each job's own
    u32 function = 0, environment = 0;  //TEXTURE_FUNCTION's bits 0-2, and TEXTURE_ENVIRONMENT_COLOR
    bool withAlpha = false, doubled = false;
    std::shared_ptr<Decoded> decoded;  //its texels, kept while a job may draw with them
    //Render to texture from a batch still being drawn (or filled): decode later, when this Look's batch starts
    //(ensureDecoded), once the batches before it have been drawn; the palette as it was when the primitive was set
    //up; and the texture's bytes, first to last (offsets in VRAM), which the CPU waits for till then (Batch::reads).
    u32 deferRows = 0;
    std::vector<u8> deferClut;
    u32 deferFirst = 0, deferLast = 0;
    //The texels of that deferred texture its jobs can take (record(): texelReach()), in the rows and columns marked;
    //or any of them (reach->all), where a job's can't be told. None until a job is recorded. (Kept apart: a Look is
    //made and moved for every primitive, and few are deferred.)
    std::unique_ptr<Reach> reach;
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
    //Each kind's own (only the job's kind's is kept: the union below)
    struct Sprite {
      u32 z, color, specular, leftFog, rightFog;
      s32 middle;                      //(sixteenths) where the fog's halves meet
      bool turned, divided;            //divided: 3D, texture coordinates across the perspective
      //2D: the coordinate running across x (u, or v when turned) and the one running down y, each from where it
      //starts (in sixteenths), by a step a pixel
      f64 columnFirst, columnStep, rowFirst, rowStep;
      s32 columnStart, rowStart;
      //3D: the corners' sixteenths; s (the coordinate across x over w) and q (its divisor) at the left and right
      //corners, and t (the one down y over w) at the top and bottom ones, each as 15-bit integers on its unit, as a
      //triangle's corners take them (draw.cpp)
      s32 left, right, top, bottom;
      s32 leftS, rightS, leftQ, rightQ, topT, bottomT;
      float sUnit, qUnit, tUnit;
    };
    //A color channel, the fog or the depth stepped across a triangle (draw.cpp's stepped()): its value at (startX,
    //startY), and its steps a sixteenth of a pixel across and down, all in 16384ths of a level (or a depth).
    struct Stepped { s64 across, down; s32 start; };
    struct Triangle {
      s64 x[3], y[3];                  //the corners (sixteenths), turned clockwise
      bool flat, shines, perspective;
      u32 flatColor, flatSpecular;
      Stepped colors[4], shine[3], fog;  //red, green, blue, alpha; the shine's red, green and blue; the fog (0-255)
      Stepped depth;                     //0-65535
      f64 uStart, uAcross, uDown, vStart, vAcross, vDown;  //2D texture coordinates, stepped from (startX, startY)
      s64 startX, startY;
      //3D texture coordinates (draw.cpp): s, t and q stepped as colors are, in 16384ths of a 15-bit unit, and the
      //size of that unit. A pixel's coordinate is the floored unit times the reciprocal of q (raster.cpp).
      Stepped texS, texT, texQ;
      float sUnit, tUnit, qUnit;
    };
    struct Point {
      s32 x, y;
      u32 z, color, specular, fog;
      float u, v;
    };
    struct Line {
      //Its ends (sixteenths), the left one first, turned so that x runs along it (steep: more rows than columns,
      //x and y swapped); the pixels it lights along x, first to last; at each, the one across is y's at the
      //pixel's middle, rounded down: (y[0] * along + rise * (16x + 8 - x[0])) / (16 * along).
      s64 x[2], y[2], along, rise;  //along: x[1] - x[0], never 0; rise: y[1] - y[0]
      bool steep;
      //Anti-aliased (ANTI_ALIAS_ENABLE, not in clear mode): each pixel's alpha its coverage, its texel its own place
      //(raster.cpp's linePixels()); slope: rise / along in 65536ths, cut toward zero, what the coverage steps by
      bool smooth;
      s64 slope;
      s32 first, last;
      bool flat, shines, perspective;
      u32 flatColor, flatSpecular;
      u32 color[2], specular[2];
      float z[2], fog[2], u[2], v[2], q[2], w[2];
      f64 uStep, vStep;  //2D texture coordinates: a step a pixel along x, from the left end's
    };
    //The job's kind's, set up afresh by what makes it (draw.cpp): the triangle's first, the largest, so that Job{}
    //zeroes all of them. (Kept apart, every job would carry, zero and copy all four.)
    union { Triangle triangle; Sprite sprite; Point point; Line line; };
  };

  Memory& memory;
  u32 commands[256] = {};  //each command's last word
  u8 clut[1024] = {};      //the palette, as CLUT_LOAD copied it in: textures read it from here, not from memory
  u64 clutHash = 0;        //its contents' hash, and a version that goes up whenever they change
  u32 clutVersion = 0;
  TextureCache textures;   //textures kept decoded (texture.cpp)
  //The texels' room of deferred copies done with (clearBatch()), to decode into again (ensureDecoded(), on whichever
  //thread starts a batch), rather than taking and clearing a texture's worth each time.
  struct Spares { std::mutex mutex; std::vector<std::vector<u32>> texels; };
  std::unique_ptr<Spares> spares = std::make_unique<Spares>();
  static constexpr u32 SpareCopies = 4;
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
  template<bool Threaded> auto runList(u64 budget, u64& ran) -> Stop;
  auto relative(u32 argument) const -> u32;

  //vertex.cpp
  auto vertexFormat() const -> VertexFormat;
  auto readVertex(u32 address, const VertexFormat& format) -> Vertex;
  template<typename Read> auto readVertexWith(const Read& read, u32 address, const VertexFormat& format) -> Vertex;
  auto readVerticesFrom(const u8* bytes, const u32* offsets, u32 count, const VertexFormat& format, Vertex* out)
    -> void;
  auto readIndex(u32 n, const VertexFormat& format) -> u32;

  //lighting.cpp
  auto lightingState(Transform& t) const -> void;
  auto light(Vertex& vertex, const float world[3], const float normal[3], const Transform& t) const -> void;

  //transform.cpp
  auto transformState() const -> Transform;
  auto settingsState(Transform& t) const -> void;
  auto transformKept(bool skinned = true) -> Transform&;
  auto transform(Vertex& vertex, const Transform& t) -> void;
  auto transformAll(std::vector<Vertex>& vertices, const Transform& t) -> void;
  //(transform()'s stages: a vertex's place in its model, skinned, in the world and its depth in the view, from the
  //first to the last)
  struct Placing { float model[3], world[3], viewZ; };
  inline auto transformPlace(Vertex& vertex, const Transform& t, Placing& placing) -> void;
  inline auto transformRest(Vertex& vertex, const Transform& t, const Placing& placing) -> void;
  auto project(Vertex& vertex, const Transform& t, bool clipped) const -> void;
  auto clipTriangle(const Look& look, const Transform& t, const Vertex& a, const Vertex& b, const Vertex& c,
                    s32 facing) -> void;
  auto clipLine(const Look& look, const Transform& t, const Vertex& a, const Vertex& b) -> void;
  auto clipPosition(const Vertex& vertex, const Transform& t, float clip[4]) const -> void;

  //draw.cpp
  auto lookFor(const PixelState& pixel, const Sampler* texture) const -> Look;
  auto primitive(u32 kind, u32 count) -> void;
  auto drawVertices(u32 kind, const VertexFormat& format, std::vector<Vertex>& vertices, u32 strip) -> void;
  auto reachedBy(bool through, const std::vector<Vertex>& vertices, const Transform& t, const Region& region)
    -> Reached;
  auto meshTriangles(const Look& look, const Transform& t, u32 kind, u32 strip, s32 facing,
                     std::vector<Vertex>& vertices) -> void;
  auto submit(Job& job) -> bool;
  auto spriteJob(const Look& look, const Vertex& from, const Vertex& to, bool perspective, Job& job) const -> bool;
  auto readsAhead(const Look& look, const std::vector<Vertex>& vertices, u32 count) const -> bool;
  auto readsApart(const Look& look, const Transform& t, const std::vector<Vertex>& vertices, u32 kind, u32 strip,
                  s32 facing) const -> bool;
  auto rectangle(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto triangle(const Look& look, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing, bool perspective)
    -> void;
  auto placedTriangle(const Look& look, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                      bool perspective, const s64 (&x)[3], const s64 (&y)[3]) -> void;
  auto triangleJob(Job& job, const Look& look, const Vertex& a, const Vertex& b, const Vertex& c, const s64 (&x)[3],
                   const s64 (&y)[3], s64 area, bool perspective) const -> void;
  auto point(const Look& look, const Vertex& at) -> void;
  auto line(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto readVertices(u32 count, const VertexFormat& format) -> void;
  auto boundingBox(u32 count) -> bool;
  auto rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to, bool perspective) -> void;
  auto triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                bool perspective) -> void;

  //curves.cpp
  auto patch(bool spline, u32 argument) -> void;

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
  auto texelSpan(const Job& job, s32 (&rows)[2], s32 (&columns)[2]) const -> bool;
  static auto texelRuns(const s32 (&span)[2], u32 size, bool clamp, s32 (&runs)[2][2]) -> u32;
  auto texelReach(const Job& job, Reach& reach) const -> void;
  struct LinePixel { s32 x, y; u32 z, color, specular, fog; float u, v; };
  auto linePixels(const Job& job, s32 fromY, s32 toY, std::vector<LinePixel>& pixels) -> void;
  template<u32 Format> auto lineRows(const Job& job, s32 fromY, s32 toY) -> void;
  template<u32 Format> auto rasterizeAs(const Job& job, s32 fromY, s32 toY) -> void;
  auto rasterize(const Job& job, s32 fromY, s32 toY) -> void;

  //texture.cpp
  auto sampler() const -> Sampler;
  auto texel(const Sampler& texture, s32 u, s32 v) -> u32;
  auto direct(const Sampler& texture) -> const u8*;
  auto sample(const Sampler& texture, float u, float v) -> u32;
  auto sampleWith(const Sampler& texture, bool linear, float u, float v) -> u32;
  struct TexelAxis { s32 first, second, fraction; };
  static auto texelAxis(float coordinate, u32 size, bool clamp, bool linear) -> TexelAxis;
  struct TexelSpot { s32 first, fraction; };
  static auto texelSpot(float coordinate, bool linear) -> TexelSpot;
  auto fetch(const Sampler& texture, s32 x, s32 y) -> u32;
  auto filtered(const Sampler& texture, TexelAxis u, TexelAxis v) -> u32;
  auto textureFunction(u32 color, u32 texel) const -> u32;
  auto combine(const Look& look, u32 color, u32 texel) const -> u32;
  auto loadClut() -> void;
  auto paletteChanged() -> void;
  auto textureBytes(const Sampler& texture, u32 rows, u32& low, u32& high) const -> void;
  //Decodes look's texture, or (allowDefer, workers drawing) leaves it for ensureDecoded once a batch that draws
  //over it has been drawn (render to texture: threads.cpp).
  //copyDraws: whether a primitive drawing over its texture draws the same from a copy taken first (readsAhead()).
  auto decode(Look& look, const Region& region, u32 rows, bool allowDefer = false,
              const std::function<auto () -> bool>& copyDraws = {},
              const std::function<auto () -> TexelBox>& boxOf = {}) -> void;
  auto ensureDecoded(Batch& batch) -> void;
  auto fillDecoded(Look& look, u32 rows, const std::function<auto () -> TexelBox>& boxOf = {}) -> void;
  auto decodeTexels(const Sampler& t, const u8* palette, u32 low, u32 high, u32 width, u32 from, u32 rows,
                    u32* texels, u32 left = 0, u32 right = ~0u, u32 outTop = 0, u32 outLeft = 0) -> void;
  auto textureWritten(u32 page) -> void;
  auto forget(Decoded* entry) -> void;
  auto dropTextures() -> void;

  //pixel.cpp
  auto pixelState() const -> PixelState;
  template<u32 Format> auto drawPixelAs(const PixelState& pixel, s32 x, s32 y, u32 z, u32 color, u32 fog) -> void;

  //transfer.cpp
  auto transfer() -> void;
  struct Transfer {
    u32 source = 0, destination = 0, sourceStride = 0, destinationStride = 0, sourceX = 0, sourceY = 0;
    u32 destinationX = 0, destinationY = 0, width = 0, height = 0, bytes = 0;
    std::bitset<(2 << 20) / 4096> read, written;  //(VRAM's pages: VRAMPages)
  };
  auto transferAsked() const -> Transfer;
  auto transferOverwrite(u32 to, u32 size) -> void;
  auto transferChecks() -> void;    //(the render GE's, threaded: thread.cpp)
  auto transferThreaded() -> void;  //(the kernel's GE's, threaded: thread.cpp)

  //threads.cpp
  static constexpr u32 VRAMPages = (2 << 20) / 4096;  //VRAM's 2 MiB in 4 KiB pages (memory.hpp)
  auto setThreads(u32 count) -> void;
  auto flush() -> void;
  auto launch(bool returning) -> void;
  auto startBands(Batch& batch) -> void;
  auto drew(Batch& batch) -> bool;
  auto launched() const -> bool;
  static auto unfinished(const Batch& batch) -> bool;
  auto reclaim(Batch& batch) -> void;
  auto reap() -> void;
  auto resume() -> void;
  auto settle() -> void;
  auto freeVRAM() -> void;
  auto settleAll() -> void;
  auto settleOver(u32 first, u32 last) -> void;
  template<typename Reaches> auto settleThrough(const Reaches& reaches) -> bool;
  auto clearBatch(Batch& batch) -> void;
  auto drawnFirst(u32 address, u32 size) -> void;
  auto undrawnPage(u32 address) -> bool;
  auto drawnBefore(const std::bitset<VRAMPages>& read, const std::bitset<VRAMPages>& written) -> void;
  auto drawnOver(u32 first, u32 last, bool rendered = true) -> bool;
  auto defer(const PixelState& pixel, const Region& region) -> bool;
  auto record(const Job& job) -> void;
  auto drawBands(Batch& batch) -> void;
  auto worker() -> void;

  //transformState()'s parts as they were last worked out (transformKept()), and the words they came from: the
  //matrices', and the commands settingsState() and lightingState() read, in transform.cpp's ranges' order. (A PRIM
  //sets its own on it too: its weights, its texture's size, whether its vertices have colors.)
  struct KeptTransform {
    static constexpr u32 Settings = 17, Lighting = 77;
    bool valid = false, bonesValid = false;
    u32 bones[96], world[12], view[12], projection[16], textureMatrix[12];
    u32 settings[Settings], lighting[Lighting];
    Transform transform{};
  } keptTransform;

  std::vector<Vertex> primitiveVertices;  //the primitive being drawn's (draw.cpp); a curved surface's points
  std::vector<u32> vertexOffsets;         //(readVertices()': where each of them is, read in a run)
  bool vertexRuns = true;  //vertices read in runs where they may be (readVertices(); tests compare without)
  u64 vertexRunsRead = 0;  //(the PRIMs whose vertices were, for the tests)
  std::vector<Vertex> patchVertices;      //the vertices a curved surface is cut into (curves.cpp)

  //The bytes of VRAM the primitive being drawn may draw over (draw.cpp): its frame buffer's, and its depth buffer's.
  struct Touched { u32 low = ~0u, high = 0; };
  std::array<Touched, 2> touched;

  //Drawing on several threads (threads.cpp). While the GE runs a list, the primitives it meets wait in a batch,
  //set up, and are drawn together, in bands of rows shared out among the threads, before anything could see them.
  //The rows in a band: rows 0-3, 4-7 and on. Smaller bands share a batch out more evenly: a thread on a slow core (the
  //RP6's little ones) taking one of its last keeps the others waiting less, though a job is set up again for each
  //band it reaches. (On the RP6 at 7 threads, against bands of 8 rows, both listed and with block transfers waiting
  //only for what they touch: God of War's battle drew 36% faster, LCS's city 13%, its woods 6%, MC3 the same:
  //docs/psp-core.md, part 61.)
  static constexpr s32 BandRows = 4;
  struct Batch {
    std::deque<Look> looks;   //its primitives' settings, and their jobs in order
    std::vector<Job> jobs;
    std::vector<std::vector<u32>> bandJobs;  //for each band (of rows 0-1023), its jobs reaching it, in order
    u64 work = 0;             //its pixels, roughly (its jobs' boxes)
    s32 top = 0, bottom = -1;           //its rows
    bool targeted = false;              //its render target: every primitive in a batch draws into the same one
    u32 frameBuffer = 0, stride = 0, format = 0, depthBuffer = 0, depthStride = 0;
    s32 left = 0, right = 0, upper = 0, lower = 0;  //its area: its primitives' scissor rectangles together
    bool depth = false;                 //some of them reach the depth buffer
    std::bitset<VRAMPages> pending;     //VRAM's 4 KiB pages it may draw over
    std::bitset<VRAMPages> reads;       //those its deferred textures are read from as it starts (Look::deferRows)
    //(threaded, on the GE thread: each of its Looks deferred, with the place of its PRIM and what the model declares
    //that PRIM may read, for the decode's reads as of then, whichever record the thread does as the batch starts: AsOf)
    std::vector<std::pair<const Look*, ReadsAsOf>> asOf;
    std::atomic<bool> readsDone{false}; //and they have been, decoded (ensureDecoded)
    bool launched = false;              //handed to the workers: being drawn, or next once those before are done
    std::atomic<bool> finished{false};  //launched, and every band drawn: its pixels are all in VRAM
    u32 users = 0;                      //threads drawing its bands now (under the mutex)
    u32 bands = 0;
    std::atomic<u32> nextBand{0}, bandsLeft{0};
    //0: deferred textures not decoded yet; 1: one thread is decoding; 2: ready (drawBands)
    std::atomic<u32> decodeState{2};
  };
  static constexpr u32 Batches = 8;  //filled in turn, round and round: one waiting for its primitives
  struct Drawing {
    u32 threads = 1;          //how many threads draw ('GE Threads'): 1, each primitive at once on the GE's own
    bool deferring = false;   //run() is running: primitives wait in the batch
    bool recording = false;   //the primitive being set up waits in the batch
    Batch batches[Batches];   //one waiting for its primitives, those after it in turn maybe still being drawn
    Batch* batch = &batches[0];         //the one primitives go into
    Batch* drawn = nullptr;             //the one the workers draw (under the mutex)
    std::deque<Batch*> queued;          //those to draw once that's done, in turn (under the mutex)

    std::vector<std::thread> workers;   //the threads besides the GE's own
    std::mutex mutex;
    std::condition_variable wake, finished;
    u64 round = 0;            //goes up as each batch starts being drawn, which the workers wake for
    bool quit = false;
    bool held = false;        //the workers take no batch (tests: one kept being drawn till they've looked)
    u64 shared = 8192;        //a batch with fewer pixels is drawn on the GE's thread alone (tests may make it 0)
  } drawing;

  //A hardware renderer (ge/gpu, docs/psp-gpu-renderers.md): the GPU's own rasterizer, texture units and blending
  //drawing the primitives in place of the software renderer, or none. The GE goes on doing everything up to the
  //pixels, as it does for the software renderer: it reads the list and the vertices, transforms, lights and clips
  //them, and works out each primitive's settings (a Look); then, instead of drawing each primitive's jobs, it hands
  //the renderer the settings once a PRIM (begin()) and each primitive on the screen: a triangle's or a point's
  //vertices, a sprite's job (its pixels, and how its texture coordinates step), a line as the pixels it lights.
  //The renderer draws them into VRAM's frame buffers kept on the GPU, and owns the pages it has drawn over until it
  //puts them back in memory's VRAM (finish()): it marks them busy (Memory::busyPages), so anyone touching one waits
  //for that first (Memory::finishDrawing, settleAll()), as for the drawing threads' batches. written() hears of
  //every watched page someone changes (Memory::watch()), so that the renderer knows when its copy of a frame buffer
  //is older than memory's. When ready() is false (the GPU lost) the software renderer draws; and a PRIM begin()
  //refuses (the renderer can't draw it) is drawn by the software renderer, at once, once the renderer has put back
  //what it drew. It's set with setRenderer(), which has it forget any machine's VRAM it saw before, and taken away
  //with it before it goes, settled.
  struct Renderer {
    virtual ~Renderer() = default;
    virtual auto ready() const -> bool = 0;
    virtual auto scale() const -> u32 { return 1; }  //(its pixels per the PSP's each way: a GPU's resolution)
    //(whether a depth filled from memory takes the pixels other targets drew over its bytes since the last finish, as
    //a finish would put them, or memory's bytes as they are: a GPU's Backend::depthMoves)
    virtual auto depthMoves() const -> bool { return true; }
    //(whether the PRIM begin() last refused had no room to go in: a GPU out of memory, which only the host decides;
    //asked after a refusal, and forgotten then)
    virtual auto refusedForRoom() -> bool { return false; }
    //(vertices, t: the PRIM's, for GE::reachedBy() where it needs where the PRIM's pixels can be and their depths: a
    //3D PRIM's are its vertices on the screen, worked out only then)
    virtual auto begin(GE& ge, const Look& look, bool through, const Region& region,
                       const std::vector<Vertex>& vertices, const Transform& t) -> bool = 0;
    virtual auto triangle(const Vertex& a, const Vertex& b, const Vertex& c) -> void = 0;
    //(triangle() with its corners' places as the GE holds them, in sixteenths, which it worked out: draw.cpp's fixed())
    virtual auto triangleAt(const Vertex& a, const Vertex& b, const Vertex& c, const s64 (&)[3], const s64 (&)[3])
      -> void {
      triangle(a, b, c);
    }
    virtual auto sprite(const Job& job) -> void = 0;
    virtual auto point(const Vertex& at) -> void = 0;
    virtual auto line(const Job& job, const std::vector<LinePixel>& pixels) -> void = 0;
    virtual auto submit(GE& ge) -> void = 0;  //what's drawn so far handed to the GPU, not waited for (a list's end)
    virtual auto finish(GE& ge) -> void = 0;  //every page it owns put back in memory's VRAM, and no longer busy
    virtual auto written(GE& ge, u32 page) -> void = 0;
    virtual auto forget(GE& ge) -> void = 0;  //VRAM replaced whole (a state loaded, the power): its copies are old
    //A texture (its first rows and columns, those the PRIM can reach) in a frame buffer whose newest pixels are on
    //the GPU: the renderer takes it from there for the PRIM (begin()), so the GE doesn't decode it, and memory's
    //pages under it stay the renderer's. takes: the texels (columns and rows, inclusive) the PRIM may take, asked
    //for where it holds the texture.
    virtual auto holds(GE& ge, const Sampler& texture, u32 rows, u32 columns,
                       const std::function<auto () -> Region>& takes) -> bool = 0;
    //Of a texture it doesn't hold, whose PRIM's rows aren't known (a 3D one's), the rows memory gives: all, or those
    //before the pages of a frame buffer it has drawn since, the rest held at the last of them (its copies' way),
    //where the texels the PRIM may take (takes) allow.
    virtual auto memoryRows(GE&, const Sampler& /*texture*/, const std::function<auto () -> Region>& /*takes*/)
      -> u32 { return ~0u; }
    //Whether what it has drawn since it last finished reaches any of VRAM's bytes first to last (offsets in VRAM,
    //inclusive): its pixels' own bytes, row by row, not the unused columns right of a picture in the same pages,
    //where games keep lists and textures (as drawsOver() has it for the drawing threads, texture.cpp).
    virtual auto drawnOver(GE& ge, u32 first, u32 last) -> bool = 0;
    //Memory changed VRAM's bytes first to last while pages were busy, without its waiting: bytes it hasn't drawn
    //over (drawnOver()), which its copies of frame buffers take from memory again before they're used.
    virtual auto besideChanged(GE& ge, u32 first, u32 last) -> void = 0;
    //A block transfer (having read what it copies) or the CPU's store is about to write VRAM's bytes first to last
    //whole: pixels it drew there are memory's from then on, not put back, and no longer drawn over (drawnOver()),
    //where it can keep track of them; the rest it still has drawn over (the writes wait for a finish, as anyone's).
    virtual auto overwritten(GE&, u32 /*first*/, u32 /*last*/) -> void {}
    //A renderer that transforms and lights 3D vertices itself (Vulkan's fast mode, the GPU's vertex shader): whether
    //it takes a 3D PRIM of count corners with these settings (t), after begin(); then (meshTriangles()) the
    //triangles the GE would draw whole, as the vertex type laid them out, untransformed: corners, three to a
    //triangle, each triangle's last corner first, in runs between those the GE cuts and draws (again: the same
    //PRIM's vertices as the run before's). Where it won't, the GE transforms, lights and clips them itself, as for
    //any renderer.
    virtual auto meshes(GE&, const Transform& /*t*/, u32 /*count*/) -> bool { return false; }
    virtual auto mesh(GE&, const Transform& /*t*/, const std::vector<Vertex>&, const std::vector<u32>& /*corners*/,
                      bool /*again*/) -> void {}
  };
  Renderer* renderer = nullptr;
  auto setRenderer(Renderer* next) -> void;  //ge.cpp
  bool hardware = false;  //the primitive being drawn goes to the renderer (drawVertices())
  bool skipping = false;  //the primitive being drawn changes no pixel: set up, not drawn (drawVertices())
  bool boxes = true;      //a renderer's textures decoded only as far as its triangles take them (texelBox()); tests
                          //turn it off to compare
  //(the stress mode, in a build with PHOBOS_GE_STRESS_HOOK, for the GE thread's tests: DESIGN.md section 9. With
  //PHOBOS_GE_STRESS=n a hardware renderer is finished at every kick's end and before every n-th PRIM, as the model
  //probe's was, in the same places with the GE on its thread or not)
#if defined(PHOBOS_GE_STRESS_HOOK)
  static inline const u32 stressEvery = [] {
    const char* every = std::getenv("PHOBOS_GE_STRESS");
    return every ? u32(std::strtoul(every, nullptr, 10)) : 0u;
  }();
  u64 stressPrims = 0;
  static auto stressed() -> bool { return stressEvery; }
#else
  static constexpr auto stressed() -> bool { return false; }
#endif
  std::vector<LinePixel> hardwareLine;
  //A PRIM's corners on the screen, meshTriangles()' or drawVertices()' (each with its own bits, draw.cpp's): each
  //one's place in sixteenths, as fixed() has it, and what its triangles ask of it; and the run of triangles
  //meshTriangles() hands to the renderer
  struct MeshCorner { s32 x, y; u32 bits; };
  std::vector<MeshCorner> placed;
  std::vector<u32> handed, handing;  //(handing: room for a PRIM's corners as they're found, handed over in runs)

  //What a PSP's GE takes over its work, in its clocks (the bus's, half the PLL's), as tools/psp-measure timed lists of
  //known work on the owner's PSP: rounds 6, 8, 9 and 10 (docs/psp-core.md, parts 76, 106, 110 and 112), at 222/111 and
  //333/166 MHz (and some at 333/222/166 and 266/133 too: the GE's time follows the bus's clock alone). The GE reads its
  //list and fetches, transforms and lights vertices (its front end), and hands each primitive on to be set up and drawn
  //(its back end): the two go on at once as far as the queue between them lets them (Pipe).
  //A list also takes some time of its own as it starts. Used with the option "GE Timing" "PSP", the default; "Fast"
  //charges Sony's peak rates instead (owedPixels, owedPrimitives). The clocks are tools/psp-measure/ge-fit.py's fit of
  //rounds 6, 8, 9 and 10 (to three figures; part 106 has the command, and the lists it holds out), but where a comment
  //says otherwise.
  struct Cost {
    static constexpr f64 List = 2350;           //a list, as it starts
    //the front end
    static constexpr f64 Command = 2.23;        //a word of the list
    static constexpr f64 Jump = 42.4;           //JUMP (or BJUMP taken): the list fetched afresh
    static constexpr f64 Call = 22.5;           //CALL, or RET
    static constexpr f64 Prim = 57.3;           //PRIM, BEZIER or SPLINE: its own, however many primitives it draws
    static constexpr f64 VertexByte = 0.512;    //a vertex's byte, fetched in order (round 9's time-formats: from 12
    static constexpr f64 VertexFloor = 5.02;    //bytes up), at least this a vertex,
    static constexpr f64 Vertex3 = 9.09;        //and the 3-byte format (8-bit positions alone) this
    static constexpr f64 IndexedVertex = 35.0;  //a vertex fetched through its index (one used again costs as much)
    static constexpr f64 Weight = 4.25;         //a vertex's skinning weight
    static constexpr f64 PointLight = 10.6;     //a vertex lit by a point light (a directional one's light costs
                                                //nothing to speak of)
    static constexpr f64 SpotLight = 10.4;      //by a spotlight
    //and by a light's shine, a directional light's and a point light's or spotlight's; and more for a vertex with one
    //light shining (round 10's time-lights: one directional light shining 3.9 clocks a vertex, four 0.9 each; round
    //8 had 20.7 and 1.2).
    static constexpr f64 DirectionalShine = 0.918, PointShine = 7.54, OneShine = 2.96;
    static constexpr f64 Cut = 26.0;            //a triangle cut at the near plane
    static constexpr f64 Patch = 96.5, PatchVertex = 13.8;  //a curved surface's patch, and each vertex made
    //the back end
    static constexpr f64 Primitive = 0;         //a primitive set up (the fit leaves it out: it hides behind its rows)
    static constexpr f64 Row = 2.23;            //the least a row of a primitive's pixels takes
    static constexpr s64 NarrowColumns = 32;    //a primitive at most this many pixels wide takes NarrowPixel (round
                                                //6's time-sizes: 32 wide and narrower alike a pixel)
    static constexpr f64 NarrowPixel = 0.342;   //a pixel of a narrow primitive
    static constexpr f64 Pixel32 = 0.540, Pixel16 = 0.331;  //of a wider one, into a 32- or 16-bit frame buffer
    static constexpr f64 DepthPixel = 0.195;    //more for the depth test (or clear mode's depth), into 32-bit only
    //A texture is fetched into the GE's cache as it's read after a TEX_FLUSH (round 9's time-loads): the texels a
    //primitive may read (its box, in whole fetches of 16 bytes across and, swizzled, 8 rows down: an 8-texel CLUT8
    //strip fetches 16), not the texture's size, at FetchFixed for the first fetch (round 10 finds none) and FetchByte a
    //byte [from RAM swizzled, RAM not, VRAM swizzled, VRAM not]. The cache keeps what it has fetched, up to CacheBytes,
    //till the next TEX_FLUSH: a primitive pays for what it reads outside that alone (a texture set again, or another at
    //the same texels, without a TEX_FLUSH, costs nothing: the cache doesn't look at the address). A primitive reading
    //more than CacheBytes reads as its pixels take the texels, at TexelByte a byte [8- and 4-bit indices][from RAM
    //swizzled, RAM not (up to SmallBytes), RAM not (more), VRAM][wide]. A row reads wide past WideTexels distinct
    //texels (WideIndices for indices): a sprite's rows all span its width; a triangle's rows split it, so 1 - (T/W)^2
    //of its pixels lie in rows past T (round 9's time-tristrips: T 139 to 141 for 32-bit texels, 133 to 137.5 for 5650,
    //about 270 for CLUT8), read at a wide sprite's rate, the rest at TriangleTexelByte's [indices][from] narrow rate
    //(into a 16-bit frame buffer, TriangleWide16 of those read wide too). Read narrow, a pixel takes its texels up to
    //ShrunkMost (shrunk 2:1, 2.8 to 3.4 times the 1:1 time: round 9's time-tristrips 84 to 87), the square root's
    //growth past that (unmeasured); wide, their square root (round 8's 512-wide sprite, 1.9 times); at the mipmap level
    //TEX_LEVEL picks, whose texels alone are fetched. (No list reads wide from an unswizzled RAM texture of 64 KiB or
    //less: taken as over it. No triangle reads narrow from one, nor indices from unswizzled RAM: a sprite's rates.)
    static constexpr f64 TexelByte[2][4][2] = {{{0.366, 3.14}, {1.41, 13.7}, {1.72, 13.7}, {0.267, 1.94}},
                                               {{0.326, 3.14}, {1.32, 11.9}, {1.46, 11.9}, {0.360, 2.20}}};
    static constexpr f64 TriangleTexelByte[2][4] = {{0.438, 1.41, 1.87, 0.328}, {0.433, 1.32, 1.46, 0.367}};
    static constexpr f64 CacheBytes = 8192;     //(time-loads: a 4 KiB texture kept, a 16 KiB one fetched each time)
    static constexpr f64 SmallBytes = 65536;    //(round 8: 64x64, 16 KiB, against 512 wide, 1 MiB)
    static constexpr f64 WideTexels = 140, WideIndices = 270;
    static constexpr f64 TexelsMost = 64;       //texels a pixel counted at most
    static constexpr f64 ShrunkMost = 4;
    static constexpr f64 FetchFixed = 0;
    static constexpr f64 FetchByte[4] = {0.549, 1.58, 0.394, 0.480};
    //A CLUT32 texture's fetches, at rates of their own (Activision Hits' menu reads its 8888 picture back as CLUT32:
    //round 10's time-clut32; none of its CLUT32 is swizzled in RAM, taken as the others' rate).
    static constexpr f64 Clut32FetchByte[4] = {0.549, 2.11, 0.307, 0.358};
    //Triangles' rows into a 16-bit frame buffer: the share of round 9's narrow rows (those under the wide reads'
    //texels) read wide instead, as a sprite's (Activision Hits' copyright screen's triangles, round 10's
    //time-widetri: into 5650, 5551 and 4444 alike).
    static constexpr f64 TriangleWide16 = 0.489;
    static constexpr f64 CompressedByte = 20.3; //a DXT texture's byte
    static constexpr f64 PaletteLoad = 0, PaletteByte = 0.206;  //CLUT_LOAD's, and a byte of it (time-loads)
    static constexpr f64 RamShare = 0;          //of a RAM-textured primitive's vertices' work, the back end's too (the
                                                //two read the RAM: time-tristrips' meshes; round 10's fit finds none)
    static constexpr f64 TransferRow = 35.2;    //a block transfer's row
    static constexpr f64 TransferByte[2][2] = {{0.0719, 0.616}, {0.337, 0.380}};  //from RAM or VRAM, to RAM or VRAM
    //The queue between the two ends, in entries, measured rather than fitted: a primitive takes one and one for each
    //of its vertices (a sprite three, a triangle four), a transfer or a palette's load one. Round 8's time-signals2: a
    //SIGNAL's callback after 128 sprites of 64x64 came with 28 of them left to draw, after 128 triangles 20.5 (84.6 and
    //82 entries); its time-overlap: front-end and back-end work in turn, 16 of each at a time, went on together, 256
    //at a time one after the other.
    static constexpr f64 Queue = 83;
    //The driver's own time on the CPU, in its cycles (Kernel::geDriven()), measured: sceGeListEnQueue's about 5200
    //(round 6's timed lists: it returned 23.5 microseconds after it was called at 222 MHz, 15.5 at 333),
    //sceGeListUpdateStallAddr's about 510 (ge-statuses: nine of them took the PSP 21 microseconds more than the core
    //at 222 MHz).
    static constexpr u64 EnqueueCycles = 5200, StallCycles = 510;
    //The CPU beside the GE (Kernel::geContend()): a list's reads of RAM, of each kind [the front end's vertices, its
    //words, the back end's texels, transfers], make its work take 1 + Contention[kind] x that kind's share of its
    //clocks x f as long, f the share of the time the CPU works beside it as a PSP's would: CpuScale times the emulated
    //CPU's (the PSP's CPU takes 3.9 to 9.1 times as long a frame in the captures), at most all of it; a stretch of
    //fewer than PollInstructions between syscalls is polling, not work (a loop waiting reads the cache). Round 8's
    //cpu-contention: a thread streaming RAM beside the GE made its RAM-textured drawing 2.12 times as long; round 10's
    //time-contention, by kind: vertices 0.20, words 0.16, texels 0.77, transfers 0.34 times the share for a thread
    //streaming RAM flat out (contention10.py). None till the CPU's RAM traffic is known: the PSP's GE draws beside a
    //CPU busy with arithmetic or the cache as fast as alone, and a game's CPU waiting without syscalls (Activision
    //Hits' menu) looks to the emulated CPU as busy as one working (part 106): Contended false, the kernel's part not
    //built.
    static constexpr f64 Contention[4] = {0, 0, 0, 0};
    static constexpr f64 CpuScale = 5;
    static constexpr u64 PollInstructions = 300;
    static constexpr bool Contended = Contention[0] > 0 || Contention[1] > 0 || Contention[2] > 0 || Contention[3] > 0;
  };
  //The list's clocks so far that read RAM, by Cost::Contention's kinds, and all of them (pipeStart() clears them; with
  //Cost::Contended alone).
  f64 ramClocks[4] = {}, allClocks = 0;
  auto ramRead(u32 kind, f64 clocks) -> void {
    if constexpr(Cost::Contended) ramClocks[kind] += clocks, allClocks += clocks;
  }
  auto otherWork(f64 clocks) -> void {
    if constexpr(Cost::Contended) allClocks += clocks;
  }
  //The work since the driver last looked (Kernel::geTime()), as the two ends take it, with the PSP's timing: the front
  //end works till it hands an item on (a primitive, a transfer, a palette's load), as soon as the queue has room for
  //it; the back end takes each in turn, once it's handed on and the last is done. Counted as each part is met,
  //whichever renderer then draws it, so the time is the same for all of them (but for a renderer transforming 3D
  //triangles itself, whose corners, meshTriangles()'s, are the GE's but for a float's rounding).
  struct Pipe {
    f64 front = 0, back = 0;  //clocks from Kernel::gePipeAt: the front end done with its last item, the back end with
                              //all of them
    f64 pending = 0;          //the front end's work since its last item (words, state, a PRIM's own): with the next
    u64 words = 0;            //the run's words charged so far (run()'s count: the rest are the next item's)
    f64 vertices = 0;         //a PRIM's vertices' work, shared out among its primitives as they come,
    u32 left = 0;             //as many as it may still draw
    static constexpr u32 Room = 128;  //(more items than Cost::Queue's entries can hold, each taking one at least)
    struct Item { f64 entries, done; } items[Room];  //those in the queue: their entries, and when the back end is done
    u32 first = 0, count = 0;                        //with each
    f64 entries = 0;
  } pipe;
  bool pspTiming = false;  //the option "GE Timing" "PSP", as the kernel runs the GE (Kernel::geRun())
  const u64* wordsRun = nullptr;  //the words run() has run (its ran), while it runs
  auto pipeStart() -> void {
    pipe.front = pipe.back = pipe.pending = pipe.vertices = pipe.entries = 0;
    pipe.left = pipe.first = pipe.count = 0, pipe.words = 0;
    if constexpr(Cost::Contended) ramClocks[0] = ramClocks[1] = ramClocks[2] = ramClocks[3] = allClocks = 0;
  }
  auto pipeFront(f64 clocks) -> void { if(pspTiming) pipe.pending += clocks; }
  auto pipeItem(f64 back, f64 entries) -> void;  //draw.cpp
  auto pipePrimitive(u32 kind, u32 count) -> void;
  auto pipeReach() const -> f64 { return pipe.front + pipe.pending + pipe.vertices; }  //the front end's, all of it
  auto pipeTime() const -> f64 { return std::max(pipeReach(), pipe.back); }
  //A run's end (words: run()'s count): the front end's work in hand, done; what it has handed on goes on being drawn,
  //into the list's next run (Kernel::geResume()).
  auto pipeStop(u64 words) -> void {
    pipe.front += pipe.pending + f64(words - pipe.words) * Cost::Command + pipe.vertices;
    pipe.pending = pipe.vertices = 0, pipe.left = 0, pipe.words = 0;
  }
  //Its times moved on by clocks: what was done by then gone from the queue.
  auto pipeShift(f64 clocks) -> void {
    pipe.front = std::max(pipe.front - clocks, 0.0), pipe.back = std::max(pipe.back - clocks, 0.0);
    while(pipe.count && pipe.items[pipe.first].done <= clocks) {
      pipe.entries -= pipe.items[pipe.first].entries;
      pipe.first = (pipe.first + 1) % Pipe::Room, pipe.count--;
    }
    for(u32 n = 0; n < pipe.count; n++) pipe.items[(pipe.first + n) % Pipe::Room].done -= clocks;
  }
  //And what Sony's peak rates count of the same (the GE timing "Fast", the opt-in): the primitives set up (a
  //triangle cut at the near plane, its pieces) and their pixels inside the scissor rectangle.
  u64 owedPixels = 0, owedPrimitives = 0;
  auto owe(u64 pixels) -> void { owedPixels += pixels, owedPrimitives++; }  //(those alone: owePrimitive() does both)
  //The GE's texture cache (texelClocks()): the texels it holds since the last TEX_FLUSH, a box at the level read (from
  //u0 to u1 across, v0 to v1 down, in whole fetches: none where u1 is at most u0), and whether it has fetched since;
  //saved in states (layout 34 on).
  struct TexelCache {
    u16 u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool fetched = false;
  } texelCache;
  //The texels a primitive may read, at the texture's first level: u0 to u1 across, v0 to v1 down (u1 below u0: not
  //known, so the whole texture).
  struct TexelsRead { f64 u0 = 0, v0 = 0, u1 = -1, v1 = -1; };
  //A primitive met: its pixels, rows and widest row in pixels, its texels a pixel and those it may read, its entries in
  //the queue between the GE's two ends (Cost::Queue), and whether it's a sprite.
  auto owePrimitive(const Look& look, u64 pixels, s64 rows, s64 columns, f64 texels, const TexelsRead& box,
                    f64 entries, bool sprite = false) -> void;
  auto texelClocks(const Look& look, u64 pixels, f64 texels, const TexelsRead& box, bool sprite) -> f64;
  auto pipeDrain() -> void;  //draw.cpp
  //The GE's reciprocal (draw.cpp's geReciprocal()), and one of its chords' start and slope, for the tests.
  static auto reciprocal(float x) -> float;
  static auto reciprocalChord(u32 chord) -> std::pair<s32, s32>;
  auto oweTransfer(u32 width, u32 height, u32 bytes, u32 source, u32 destination) -> void;

  Stop pending = Stop::Ended;  //what the next END means: a FINISH or SIGNAL before it changes it
  std::set<std::string> noted;

  //The GE on a thread of its own (thread.hpp, option "GE Thread"). The kernel's GE then runs the lists (the control
  //pass) and hands what draws to the render GE through the thread; the checks of drawing state the control pass makes
  //(drawnFirst(), drawnBefore(), settling) are the render GE's, once the thread has done everything handed to it
  //(drawer()). The render GE reads a handed-over PRIM's vertices and indices, and a palette, from the copies made as
  //they came (copied).
  GE* render = nullptr;        //(the kernel's GE, threaded) the render GE
  GeThread* thread = nullptr;  //(the kernel's GE, threaded) the thread it hands over through
  bool running = false;        //(the kernel's GE) inside run(): a PRIM handed over outside it is drawn at once
  struct Copied {
    u32 ranges = 0, address[2] = {}, size[2] = {};
    const u8* bytes[2] = {};
    auto at(u32 address, u32 size) const -> const u8*;
  } copied;
  //The state the commands set, as a context saves it (GE::State: commands, the matrices and their indices).
  struct State {
    u32 commands[256], bones[96], world[12], view[12], projection[16], textureMatrix[12];
    u32 boneIndex, worldIndex, viewIndex, projectionIndex, textureIndex;
  };
  //thread.cpp
  auto drawer() -> GE&;
  auto stateOf() const -> State;
  auto takeState(const State& state) -> void;
  auto apply(u32 word) -> void;
  auto runStart() -> void;
  auto runEnd() -> void;
  auto replayPrimitive(u32 word, u32 vertexAt, u32 indexAt, const GeThread::Copies& copies, const u8* bytes,
                       bool patching) -> void;
  auto replayClut(u32 word, u32 address, const u8* bytes, u32 size) -> void;
  auto copyVertices(u32 count, const VertexFormat& format, GeThread::Copies& copies) -> void;
  auto copyRanges(const GeThread::Copies& copies, u8* to) -> void;
  auto handPrimitive(u32 word, u32 count, bool patching) -> void;
  auto handClut() -> void;
  auto readCopiedVertices(u32 count, const VertexFormat& format) -> void;
  auto handState() -> void;
  auto handSwitches() -> void;
  std::vector<u8> handCopies;  //(the kernel's GE) a PRIM's copies where its record is too big for the ring
  //(the kernel's GE) its state as the render GE has it once it has taken everything handed over: the words it ran are
  //handed over as they run, but its state set from outside (a context restored, a test's) goes as a whole (handState())
  State handedState{};
  bool handedStateValid = false;
  u32 listWordAt = ~0u;           //(the kernel's GE) the address of the list word run() is at
  GeThread* reporting = nullptr;  //(the render GE, threaded) the thread it reports its finishes and put-backs to
  //model.cpp: what a PRIM handed over may change in memory (GeModel), from its commands and copies as the renderer
  //will take them; where a frame buffer's target starts (GPU::targetFor()'s, shared)
  //(returns whether it takes the renderer as surely drawing it: no software renderer's pixels or depth then)
  auto modelPrimitive(GeModel& model, u32 word, u32 vertexAt, u32 indexAt, const GeThread::Copies& copies,
                      const u8* bytes, bool hardware, u64 at) -> bool;
  //(PRIMs a hardware renderer, still ready, refused: begin(), draw.cpp; those for want of room, apart)
  u64 refusals = 0, roomRefusals = 0;
  static auto targetPlace(const PixelState& p, const Region& region, u32& address, u32& row, u32& column,
                          u32& height) -> void;
  //(the render GE) What its renderer did, told to the thread (thread.cpp): a finish, its depth put back over VRAM's
  //bytes low to high, the GPU lost; and whether every software pixel handed to it is drawn.
  auto reportFinish() -> void;
  auto reportPuts(u64 low, u64 high) -> void;
  //(the render GE, checking: the pages the PRIM it does may read, as the model declares them, GeModel::Interest; each
  //of its reads of memory there is asked about: VRAM's bytes low to high, offsets, or size bytes from address)
  const GeModel::Interest* declared = nullptr;
  auto readDeclared(u64 low, u64 high) -> void;
  auto readDeclared(u32 address, u32 size) -> void;
  //(the place the render GE's reads are as of, and what's declared it may read then: the record it does, or, while an
  //AsOf lives on this thread, a deferred texture's PRIM's as its decode runs as its batch starts: ensureDecoded())
  static thread_local const ReadsAsOf* readingAsOf;
  auto readPlace() const -> u64;
  auto readInterest() const -> const GeModel::Interest*;
  struct AsOf {
    const ReadsAsOf* was = readingAsOf;
    AsOf(const ReadsAsOf* asOf) { if(asOf) readingAsOf = asOf; }
    ~AsOf() { readingAsOf = was; }
  };
  auto reportLost() -> void;
  auto drawnAll() const -> bool;
  //(the render GE, threaded) Its reads of memory and its write-backs as of the record it does (place), through the
  //versions the emulation thread keeps of what it writes meanwhile (versions.hpp); on its own thread alone
  //(onGeThread): drained, the emulation thread does as the synchronous GE does.
  GeVersions* versions = nullptr;
  u64 place = 0;
  static thread_local bool onGeThread;
  std::vector<u8> versionedVRAM;  //(VRAM's pages as of place where versions are to be taken over them)
  //versions.cpp
  static auto hostPage(void* context, u32 page) -> u8*;
  auto versioned() const -> bool { return versions && onGeThread; }
  //VRAM's bytes low to high (offsets: those past its end its start's) held while use(vram) reads them as of place:
  //vram VRAM's 2 MiB as memory has them, or, on the GE thread where versions newer than place are over them, a copy of
  //their pages with those versions in. (Elsewhere, and with the thread off, memory's as ever, at no cost.)
  template<typename Use> auto readingVRAM(u64 low, u64 high, const Use& use) -> void {
    if(!versioned()) return use((const u8*)vramBytes());
    readingVersions(low, high, use);
  }
  auto vramBytes() -> u8*;  //(memory's VRAM: versions.cpp)
  auto readingVersions(u64 low, u64 high, const std::function<auto (const u8* vram) -> void>& use) -> void;
  //VRAM's bytes low to high written back as of place: write(put) has put(offset, bytes, size) store them, on the GE
  //thread each byte that changes, into the earliest version at or after place that has it, else memory (Writes);
  //elsewhere, and with the thread off, memory's as ever, at no cost.
  struct Writes {
    GE& ge;
    GeVersions::Hold* hold;
    auto put(u32 offset, const u8* bytes, u32 size) const -> void;
  };
  template<typename Write> auto writingVRAM(u64 low, u64 high, const Write& write) -> void {
    if(!versioned()) {
      u8* vram = vramBytes();
      return write([vram](u32 offset, const u8* bytes, u32 size) { std::memcpy(vram + offset, bytes, size); });
    }
    writingVersions(low, high, [&](const Writes& writes) {
      write([&writes](u32 offset, const u8* bytes, u32 size) { writes.put(offset, bytes, size); });
    });
  }
  auto writingVersions(u64 low, u64 high, const std::function<auto (const Writes&) -> void>& use) -> void;
  //Memory's size bytes from address held while it lives: pointer() and read() as memory's, as of place.
  struct Reading {
    Reading(GE& ge, u32 address, u32 size);
    auto pointer(u32 address, u32 size) -> const u8*;
    auto read(u32 size, u32 address) -> u32;
    GE& ge;
    std::optional<GeVersions::Hold> hold;
    std::vector<u8> copy;
  };
};

}
