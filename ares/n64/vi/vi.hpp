//Video Interface

struct VI : Thread, Memory::RCP<VI> {
  Node::Object node;
  Node::Video::Screen screen;

  struct Debugger {
    //debugger.cpp
    auto load(Node::Object) -> void;
    auto io(bool mode, u32 address, u32 data) -> void;

    struct Tracer {
      Node::Debugger::Tracer::Notification io;
    } tracer;
  } debugger;

  //vi.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto step(u32 clocks) -> void;

  auto main() -> void;
  auto refresh() -> void;
  auto power(bool reset) -> void;
  auto active() -> bool { return io.colorDepth != 0; }

  //io.cpp
  auto readWord(u32 address, Thread& thread) -> u32;
  auto writeWord(u32 address, u32 data, Thread& thread) -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  struct IO {
    n2  colorDepth;
    n1  gammaDither;
    n1  gamma;
    n1  divot;
    n1  serrate;  //interlace
    n2  antialias;
    n32 reserved;
    n24 dramAddress;
    n12 width;
    n10 coincidence = 256;
    n8  hsyncWidth;
    n8  colorBurstWidth;
    n4  vsyncWidth;
    n10 colorBurstHsync;
    n10 halfLinesPerField;
    n12 quarterLineDuration;
    n5  leapPattern;
    n12 hsyncLeap[2];
    n10 hend;
    n10 hstart;
    n10 vend;
    n10 vstart;
    n10 colorBurstEnd;
    n10 colorBurstStart;
    n12 xscale;
    n12 xsubpixel;
    n12 yscale;
    n12 ysubpixel;

  //internal:
    n9  vcounter;
    n1  field;
    n3  leapCounter;
    // Set when the VI's CPU RDRAM scanout fallback rendered this frame (wide
    // modes where parallel-RDP clamps to 640 — e.g. Rogue Squadron's 1024
    // menu). video() uses this to present the CPU-written screen buffer
    // instead of the (black) Vulkan scanout.
    n1  cpuScanoutActive;
  } io;

  u32 clockFraction;
  u32 inactiveCounter;

//unserialized:
  bool refreshed;

  //VI Overclock (Mupen64Plus-FZ style): 100 = native 50/60Hz. >100 makes the
  // VI generate frames faster so the game's frame logic (VI-interrupt driven)
  // runs at a genuinely higher FPS — not host-side fast forward. Written by
  // the platform before load/reset; read live by VI::main(). Unserialized:
  // it's a user setting, not emulated state.
  std::atomic<s32> overclockPercent{100};

  #if defined(VULKAN)
  bool gpuOutputValid = false;
  #endif
};

extern VI vi;
