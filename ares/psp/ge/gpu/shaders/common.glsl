//What every stage of the GPU renderer shares: its buffers, the batch's parameters, and how looks and jobs are laid
//out in the records (ares/psp/ge/gpu/gpu.cpp writes them; the two must agree word for word).
//
//The backend's prelude comes first (compile.sh puts it there): #version, and BUFFER(n), UNIFORMS(n) and CONSTANT()
//for its way of naming bindings and constants (Vulkan's sets and specialization constants; OpenGL's bindings, and
//the #defines the program puts before the source). Everything below is the same GLSL for both: GLSL ES 3.20's
//compute shaders (or 3.10 with GL_OES_gpu_shader5) and Vulkan's GLSL 4.50, using only what both have (no 64-bit or
//16-bit numbers, no subgroups, at most four storage buffers).

//The host's rounding (exact.glsl): 1 where its compiler fuses multiply-adds (ARM64), 0 where it doesn't (x86-64).
CONSTANT(0, fused, FUSED, 1u)
//1 where the GPU's own fma() was found to be fused (one rounding); 0: exact.glsl builds one.
CONSTANT(1, nativeFma, NATIVE_FMA, 1u)

//The PSP's VRAM, 2 MiB, as words: the frame and depth buffers a batch draws into. (Its textures are in texels.)
BUFFER(0) buffer VRAMBuffer { uint vram[]; };
//The batch's records: its looks (LookWords each), its jobs (JobWords each) and its jobs' tables.
BUFFER(1) readonly buffer RecordBuffer { uint records[]; };
//Textures, decoded: 8888 texels, red in the low byte.
BUFFER(2) readonly buffer TexelBuffer { uint texels[]; };
//Which jobs reach each tile: a bit a job, 32 to a word, wordsPerTile words a tile (bin.comp).
BUFFER(3) buffer BinBuffer { uint bins[]; };

UNIFORMS(4) uniform Parameters {
  uint jobCount, jobOffset, lookOffset, wordsPerTile;
  int tileLeft, tileTop;  //the first tile's top left pixel; tiles are 16 pixels square
  uint tilesAcross, tilesDown;
  int areaLeft, areaTop, areaRight, areaBottom;  //the pixels the batch may draw (inclusive)
  //the batch's frame buffer and depth buffer (in VRAM, as PixelState has them: pixels a row)
  uint frameBuffer, stride, format, depthBuffer;
  uint depthStride, probeKind, probeCount, spare;
} parameters;

const uint VRAMSize = 2u * 1024u * 1024u;
const int TileSize = 16;

//A look (GE::Look): the pixel pipeline's settings and the texture's. Words:
const uint LookWords = 32u;
const uint LookFlags = 0u;        //Flag... below
const uint LookFormat = 1u;       //the frame buffer's: 0 5650, 1 5551, 2 4444, 3 8888
const uint LookAlpha = 2u;        //the alpha test's comparison, reference << 8, mask << 16
const uint LookColorTest = 3u;    //the color test's comparison
const uint LookColorReference = 4u;
const uint LookColorMask = 5u;
const uint LookStencil = 6u;      //the stencil test's comparison, reference << 8, mask << 16
const uint LookStencilOps = 7u;   //fail, depth fail << 4, pass << 8
const uint LookDepthTest = 8u;
const uint LookBlend = 9u;        //source factor, destination factor << 4, operation << 8
const uint LookFixedA = 10u;
const uint LookFixedB = 11u;
const uint LookDither = 12u;      //two words: DITHER0-3's 4-bit values, a row in each 16 bits
const uint LookLogic = 14u;
const uint LookWriteMask = 15u;   //the frame buffer's bits not to touch
const uint LookDepths = 16u;      //MIN_Z, MAX_Z << 16
const uint LookFogColor = 17u;
const uint LookFunction = 18u;    //the texture function (0-7)
const uint LookEnvironment = 19u;
const uint LookTexels = 20u;      //where the texture starts in texels
const uint LookTexelWidth = 21u;  //texels from one row of it to the next there
const uint LookTexelRows = 22u;   //the rows of it there
const uint LookWidth = 23u;       //the texture's own size (a power of two)
const uint LookHeight = 24u;
const uint FlagClear = 1u, FlagClearColor = 2u, FlagClearAlpha = 4u, FlagClearDepth = 8u;
const uint FlagAlphaTest = 16u, FlagColorTest = 32u, FlagStencilTest = 64u, FlagDepthTest = 128u;
const uint FlagBlend = 256u, FlagDither = 512u, FlagLogicOp = 1024u, FlagDepthWrite = 2048u;
const uint FlagDepthRange = 4096u, FlagFog = 8192u, FlagTextured = 16384u, FlagWithAlpha = 32768u;
const uint FlagDoubled = 65536u, FlagClampU = 131072u, FlagClampV = 262144u;

//A job (GE::Job): a primitive set up, as draw.cpp set it up. Words:
const uint JobWords = 64u;
const uint JobKind = 0u;          //0 sprite, 1 triangle, 2 point; filtered (linear) << 8
const uint JobLook = 1u;
const uint JobFirstX = 2u, JobLastX = 3u, JobFirstY = 4u, JobLastY = 5u;
//a sprite's
const uint SpriteZ = 6u, SpriteColor = 7u, SpriteSpecular = 8u, SpriteLeftFog = 9u, SpriteRightFog = 10u;
const uint SpriteMiddle = 11u, SpriteTurned = 12u;
const uint SpriteColumns = 13u, SpriteRows = 14u;  //where its texel axes are in the records, a word a column (row)
const uint SpriteDivided = 15u;  //1: 3D, the axes down y a word a pixel (row by row) at SpriteRows
//a point's
const uint PointX = 6u, PointY = 7u, PointZ = 8u, PointColor = 9u, PointSpecular = 10u, PointFog = 11u;
const uint PointU = 12u, PointV = 13u;
//a triangle's
const uint TriangleX = 6u, TriangleY = 9u;  //its corners, in sixteenths, turned clockwise
const uint TriangleA = 12u, TriangleB = 15u;  //edge k's function: a[k] (x - x[k + 1]) + b[k] (y - y[k + 1])
const uint TriangleLeast = 18u;  //bit k: edge k's least is 1 (a right or bottom edge), else 0
const uint TriangleTotal = 19u;  //twice its area, as a float
const uint TriangleFlags = 20u;  //1 flat, 2 shines, 4 perspective
const uint TriangleFlatColor = 21u, TriangleFlatSpecular = 22u;
const uint TriangleColor = 23u, TriangleSpecular = 26u;
const uint TriangleZ = 29u, TriangleFog = 32u, TriangleU = 35u, TriangleV = 38u, TriangleQ = 41u, TriangleW = 44u;
const uint TriangleStartX = 47u, TriangleStartY = 48u;
//2D texture coordinates as whole numbers (gpu.cpp says how): u's base (two words), step across, step down; v's;
//and the power of two each is over (u's, v's << 8)
const uint TriangleUBase = 49u, TriangleUAcross = 51u, TriangleUDown = 52u;
const uint TriangleVBase = 53u, TriangleVAcross = 55u, TriangleVDown = 56u;
const uint TriangleShifts = 57u;

uint lookWord(uint look, uint word) { return records[parameters.lookOffset + look * LookWords + word]; }
uint jobWord(uint job, uint word) { return records[parameters.jobOffset + job * JobWords + word]; }
int jobInt(uint job, uint word) { return int(jobWord(job, word)); }
float jobFloat(uint job, uint word) { return uintBitsToFloat(jobWord(job, word)); }

//An 8888 color's channel n (0 red, 1 green, 2 blue, 3 alpha); four channels into one, each held to 0-255.
int channel(uint color, int n) { return int(color >> uint(n * 8) & 0xffu); }
uint pack(int r, int g, int b, int a) {
  return uint(clamp(r, 0, 255)) | uint(clamp(g, 0, 255)) << 8 | uint(clamp(b, 0, 255)) << 16 |
         uint(clamp(a, 0, 255)) << 24;
}
