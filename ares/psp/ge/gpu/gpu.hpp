#pragma once

#include "../ge.hpp"

#include <bitset>
#include <deque>
#include <functional>

//The GE's GPU renderer (docs/psp-gpu-renderers.md): the software renderer's arithmetic, done on the GPU in compute
//shaders, so that its pictures are the software renderer's, pixel for pixel, but faster.
//
//What stays on the CPU: the display lists, the vertices, the transform, clipping, lighting, and setting each
//primitive up into its jobs (draw.cpp), exactly as for the software renderer: the GE hands the renderer its batches
//of jobs (GE::Renderer, ge.hpp), the very jobs the software renderer would draw. What the GPU does: binning the
//jobs into tiles, and every pixel of them, from the jobs' numbers with the same arithmetic (shaders/: whole numbers
//where the software renderer has whole numbers, and floats rounded step by step as the host's CPU rounds them).
//
//Its batches are drawn on the GE's renderer thread (an asynchronous renderer: ge/threads.cpp) while the GE goes on,
//and on the GPU while that thread goes on to the next (docs/psp-core.md, part 35): each run of jobs is submitted and
//not waited for. VRAM's pages a run draws over are copied to the GPU before it (those the GPU doesn't have newer:
//owned) and stay there, the GPU's, until the GE needs them back (finish(): every run waited for, its pages copied
//back), so batch after batch is drawn without the GPU ever waiting for the CPU, nor the CPU for it. Textures are
//taken from the software renderer's decoded copies. A line is drawn as points, a textured 3D sprite with a texel
//axis worked out on the CPU for each pixel (take()). A job it can't draw yet (a very large textured 3D sprite, a 2D
//triangle whose texture coordinates aren't exact as whole numbers, one outside the exact range) is drawn by the
//software renderer in its turn (GE::rasterize()), once the runs before it are finished.
//
//The shaders are one set for every backend (shaders/compile.sh); a backend (Device) only makes the buffers and runs
//the stages. Vulkan's is vulkan.cpp; OpenGL's comes next.

namespace ares::PlayStationPortable {

struct GPU : GE::Renderer {
  //What the shaders are told about a run of jobs: common.glsl's Parameters, word for word.
  struct Parameters {
    u32 jobCount = 0, jobOffset = 0, lookOffset = 0, wordsPerTile = 0;
    s32 tileLeft = 0, tileTop = 0;
    u32 tilesAcross = 0, tilesDown = 0;
    s32 areaLeft = 0, areaTop = 0, areaRight = 0, areaBottom = 0;
    u32 frameBuffer = 0, stride = 0, format = 0, depthBuffer = 0;
    u32 depthStride = 0, probeKind = 0, probeCount = 0, spare = 0;
  };

  //A backend: the four buffers the shaders share (common.glsl's bindings 0-3), each where the host can write and
  //read it, and running the stages. It knows nothing of the PSP. A run is submitted and not waited for (draw()):
  //the next run's records and bins are other buffers, the ones of a run done with (records() and bins() wait for
  //one if need be), and finish() waits for every run. VRAM and the texels are the GPU's alike for every run: the
  //host writes there only what no run submitted reads or writes, and reads only once finished.
  struct Device {
    virtual ~Device() = default;
    //The GPU stopped answering (a run took far too long, or the driver lost the device): nothing more runs on it,
    //and the software renderer draws everything from then on.
    bool lost = false;
    virtual auto name() const -> std::string = 0;
    virtual auto vram() -> u8* = 0;                 //VRAM: 2 MiB
    virtual auto records(u32 words) -> u32* = 0;    //the next run's, at least words long (what it held may go)
    virtual auto texels() -> u32* = 0;              //TexelWords long, kept from one run to the next
    virtual auto bins(u32 words) -> u32* = 0;       //the next run's, at least words long (what it held may go)
    //The GPU's own time spent on runs, in nanoseconds, where it can tell (its timestamps), counted as each run is
    //waited for: what the design's measurements report
    u64 busy = 0;
    //the shaders' constants (common.glsl's fused and nativeFma)
    virtual auto configure(bool fused, bool nativeFma) -> bool = 0;
    virtual auto draw(const Parameters& parameters) -> bool = 0;   //bin.comp, then raster.comp: submitted
    virtual auto finish() -> bool = 0;                             //every run submitted done (false: lost)
    virtual auto probe(const Parameters& parameters) -> bool = 0;  //probe.comp, waited for
  };
  static constexpr u32 TexelWords = 16 << 20;  //64 MiB of decoded textures kept on the GPU

  //How the jobs went, for the tests and the design's measurements.
  struct Statistics {
    u64 batches = 0, runs = 0;     //runs: the GPU's dispatches
    u64 gpuJobs = 0, gpuPixels = 0;  //pixels: the jobs' boxes
    u64 cpuJobs = 0, cpuPixels = 0;
    u64 lines = 0;  //(drawn as points: take())
    u64 spriteTexels3D = 0, coordinates2D = 0;  //why the CPU drew them
    u64 pastRange = 0;  //(triangles whose floats leave the range where the GPU's arithmetic is exact: take())
    u64 lostJobs = 0;   //(drawn by the CPU once the GPU was lost)
    u64 copying = 0, waiting = 0;  //nanoseconds: VRAM's pages and the records copied; the GPU waited for
    u64 finishes = 0;              //times the runs were waited for and VRAM's pages copied back
    u64 largestRun = 0;            //(jobs)
  } statistics;

  std::unique_ptr<Device> device;
  bool fused = false, nativeFma = false;  //what configure() last set (detect() finds them)
  bool denormals = false;  //the GPU keeps numbers below the normal floats as the host does (detect() finds it)
  //Where the renderer says what went wrong while drawing (once: the GPU lost); stderr's "PSP GPU:" if none. It may
  //be called on the GE's renderer thread (the batches are drawn there: ge/threads.cpp).
  std::function<void(const std::string&)> report;
  bool reported = false;

  //gpu.cpp
  GPU(std::unique_ptr<Device> device);
  auto detect(std::string& error) -> bool;
  auto configure(bool fused, bool nativeFma) -> bool;
  auto probe(u32 kind, const std::vector<u32>& cases) -> std::vector<u32>;
  auto draw(GE& ge, GE::Batch& batch) -> void override;
  auto finish(GE& ge) -> void override;
  auto asynchronous() const -> bool override { return true; }
  static auto hostFuses() -> int;

  //vulkan.cpp
  static auto vulkan(std::string& error) -> std::unique_ptr<GPU>;

  //The run of jobs being gathered for the GPU (draw()): its records, as the shaders read them (tables, then looks,
  //then jobs), and where they draw.
  std::vector<u32> tables, looks, jobs;
  std::vector<const GE::Job*> taken;
  std::unordered_map<const GE::Look*, u32> lookIndex;
  std::vector<GE::LinePixel> linePixels;
  //A textured 3D sprite's (GE::Job::Sprite::divided) texels are found by the CPU, a word each pixel (take()): up
  //to this many pixels; a larger one is drawn by the CPU.
  static constexpr u64 DividedPixels = 512 * 512;
  std::vector<f64> dividedInverses;
  //What the GPU has drawn since it was last finished: VRAM's pages it has newer than memory's (owned), and the
  //batches' jobs (held: the GE's batch is emptied once draw() returns), the GPU's in order (drawn), which the
  //software renderer draws instead, over memory's pages as they were, should the GPU be lost before it finishes.
  std::bitset<GE::VRAMPages> owned;
  struct Held { std::vector<GE::Job> jobs; std::deque<GE::Look> looks; };
  std::vector<Held> held, spare;  //(spare: emptied, their memory kept for the next batches)
  std::vector<const GE::Job*> drawn;
  const GE::PixelState* target = nullptr;  //the frame and depth buffers (every job of a batch has the same)
  s32 left = 0, top = 0, right = 0, bottom = 0;  //the jobs' boxes together
  //Decoded textures on the GPU, by the software renderer's copy they came from (kept while it lives).
  struct Slot { std::weak_ptr<GE::Decoded> texture; u32 offset = 0, words = 0; };
  std::unordered_map<const GE::Decoded*, Slot> slots;
  u32 texelsUsed = 0;

  auto fits(const GE::Look& look) const -> bool;
  auto lookFor(const GE::Look& look) -> u32;
  auto take(GE& ge, const GE::Job& job) -> bool;
  auto run(GE& ge, const std::bitset<GE::VRAMPages>& pending) -> void;
  auto lose(GE& ge) -> void;
  auto tell() -> void;
};

}
