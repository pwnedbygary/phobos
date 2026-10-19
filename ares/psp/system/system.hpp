//The PSP as an ares system: the node tree Phobos's front ends see (the screen, the sound, the controls, and the drive
//the game goes in), and the parts underneath (the Allegrex CPU, the memory map, the GE and the HLE kernel) put
//together and run a frame at a time.
//
//The game: the front end puts a medium in the "UMD Drive": a homebrew program (EBOOT.PBP, an ELF or a PRX) or a disc
//image (ISO or CSO), which goes in the drive as disc0: and umd0:. A program's own folder stands for the disc it would
//come on, so it finds the files beside it; or, if the program is on the memory stick already, it runs from there
//(ms0:), as on a PSP. The memory stick, ms0:, is a folder the front end gives (option "Memory Stick").
//
//The model: a PSP-2000/3000, with 64 MiB of RAM (PSP-1000s have 32), set to English with X as the button that
//confirms.
//
//Time: each run() is one frame of the PSP's, 1/59.94 of a second. The controls are read, the kernel runs the game for
//that long, then the frame the game shows goes to the screen and the frame's sound, as the kernel's sound channels
//made it (audio.cpp), to the speakers.
struct System {
  Node::System node;
  Node::Video::Screen screen;
  Node::Audio::Stream stream;
  Node::Port drive;           //where the game goes
  Node::Peripheral disc;      //the game in it
  VFS::Pak pak;               //the system's own files (it needs none yet)
  VFS::Pak gamePak;           //the game's

  //The PSP's controls: the d-pad, the four face buttons, the two shoulder buttons, Select, Start and the analog
  //stick; and a right stick, which no PSP has (ctrl.cpp's convention).
  struct Controls {
    Node::Object node;
    Node::Input::Button up, down, left, right, triangle, circle, cross, square, l, r, select, start;
    Node::Input::Axis x, y;
    Node::Input::Axis rightX, rightY;

    auto load(Node::Object parent) -> void;
    auto poll() -> void;
    auto buttons() const -> u32;     //as the PSP's controller reports them (PSP_CTRL_* bits)
    auto stick(const Node::Input::Axis& axis) const -> u8;  //0 to 255, 128 in the middle
  } controls;

  //The Allegrex with the memory map behind it.
  struct Processor : Allegrex {
    Memory& memory;
    Processor(Memory& memory) : memory(memory) {}
    auto read(u32 size, u32 address) -> u32 override { return memory.read(size, address); }
    auto write(u32 size, u32 address, u32 data) -> void override { memory.write(size, address, data); }
  };
  Memory memory;
  Processor cpu{memory};
  GE ge{memory};
  Kernel kernel{cpu, memory, ge};
  //The GE on a thread of its own (option "GE Thread": ge/thread.hpp): the render GE draws there, through its own
  //view of memory, what the kernel's GE hands it as it runs the lists.
  string geThread = "Off";     //"Off" or "On"
  Memory renderMemory;
  GE renderGE{renderMemory};
  std::unique_ptr<GeThread> geThreading;
  auto threadGE(bool on) -> void;
  auto renderFromState() -> void;

  string memoryStick;          //the host folder standing for ms0: (option "Memory Stick")
  string fonts;                //the host folder with the PSP's system fonts, from the owner's flash0 (option "Fonts")
  bool recompile = true;       //the CPU's recompiler on, the interpreter its fallback (option "Recompiler")
  u32 geThreads = 0;           //how many threads draw (option "GE Threads"): 0 for one fewer than the host's cores
  static constexpr u32 MostGeThreads = 64;  //and at most, nor more than twice the host's cores
  string renderer = "Software";  //who draws the GE's pictures (option "Renderer"): "Software", "Vulkan" (accurate)
                                 //or "Vulkan (fast)"
  u32 resolution = 1;            //the GPU's internal resolution (option "Resolution"): 1, the PSP's, to 10 times it
  bool failCheck = false;        //the start-up check made to fail (option "Renderer Check": a debug switch)
  bool lateFrames = false;       //the GPU's frames shown a frame or so late, not waited for (option "Late Frames")
  string pipelineCache;          //the host's file the GPU's pipelines are kept in (option "Pipeline Cache")
  bool dialogs = false;          //the host shows the system's dialogs itself (option "Dialogs")
  bool gePspTiming = true;       //the GE takes a PSP's time, not Sony's peaks (option "GE Timing": "PSP", the
                                 //default, or "Fast")
  void* vulkanLoader = nullptr;  //the host's vkGetInstanceProcAddr (vulkanLoader()), none for the system's loader
  //The screen's picture: shown times the PSP's 480x272 each way, the GPU's read back at that size where it draws at
  //a higher resolution (at most MostShown: 1920x1088, a big window's), the software renderer's enlarged to it.
  u32 shown = 1;
  static constexpr u32 MostShown = 4;
  //The hardware renderer (ge/gpu), when the owner chose one: made at the first power on, checked against the
  //software renderer (GPU::check()) and kept while the game runs; none, said once, where it couldn't start.
  std::unique_ptr<GPU> gpu;

  auto name() const -> string { return "PlayStation Portable"; }

  //system.cpp
  auto game() -> string;
  auto run() -> void;
  auto speak() -> void;  //(the frame's sound to the speakers)
  auto load(Node::System& node, string name) -> bool;
  auto unload() -> void;
  ~System();  //(the process ending with a GE thread unload() left stuck: not waited for)
  auto save() -> void;
  auto power(bool reset) -> void;
  auto serialize(bool synchronize) -> serializer;
  auto unserialize(serializer& s) -> bool;
  u64 frames = 0;            //run so far (never reset: frame pictures are told apart by them)
  bool exactFrame = false;   //the screen given this frame's own picture at its end, the GE thread waited for (a
                             //front end's capture: the runner's PNGs); else the newest it has taken, a frame behind

private:
  std::vector<u8*> pageTable;  //the CPU's view of memory, page by page (Memory::buildPages)
  std::vector<u8> steadyPages;  //the pages whose entries in it never change (Memory::buildSteady)
  //A frame's picture, taken at its end: presented on the host's window by the GPU, or read back, width x height times
  //at of the PSP's pixels (8888, red in the low byte); its frame's number. The screen's (shownPicture); with the GE
  //on its thread, the newest that thread has taken (readyPicture, frameMutex's) and the one it's taking.
  struct FramePicture {
    u64 number = 0;
    bool presented = false;
    u32 width = 480, height = 272, at = 1;
    std::vector<u32> pixels;
  };
  FramePicture shownPicture, readyPicture, threadPicture;
  std::mutex frameMutex;
  auto endFrame(const Kernel::Shown& showing, FramePicture& picture, GE& drawing,
                const std::function<auto (std::vector<u32>&) -> void>& memoryPicture) -> void;
  auto show(const FramePicture& picture) -> void;
  auto onThread(const Kernel::Shown& showing) const -> bool;
  auto frameEnded(const GeThread::Frame& frame) -> void;
  auto pictureOnThread(const GeThread::Frame& frame, std::vector<u32>& pixels) -> void;
  std::vector<u32> latePixels; //the newest late frame the GPU has finished ("Late Frames"), its size and GE format
  u32 lateWidth = 0, lateHeight = 0, lateFormat = 3;
  auto late(u32 offset, u32 stride, u32 format, u32 width, u32 height, std::vector<u32>& pixels) -> bool;
  std::vector<s16> sound;      //the frame's sound, left and right samples side by side (Kernel::audioOutput())
  f64 soundOwed = 0;           //sound frames due to the speakers: 44100 a second, so 735.7 a frame
  std::atomic<u32> unmappedReports{0};  //accesses to nothing, reported (the first few only, from any thread)
  u64 programHash = 0;         //the program that started (hash()): states carry it, to load into it alone

  auto allocate(Node::Port port) -> Node::Peripheral;
  auto connect() -> void;
  auto disconnect() -> void;
  auto startProgram() -> void;
  auto startDisc(std::shared_ptr<vfs::file> fp) -> void;
  auto startDiscProgram(std::shared_ptr<Disc> image) -> void;
  static auto hash(std::span<const u8> bytes) -> u64;
  auto snapshot() -> serializer;
  auto header(serializer& s) -> bool;
  auto restore(serializer& s, u32 length) -> bool;
  auto report(bool problem, const std::string& text) -> void;

  bool gpuFailed = false;  //(the hardware renderer couldn't start: not tried again until the next game)
  //Presenting (window()): the host's window, as handed over (a reference held: windowMutex's), and the one the GPU
  //was last given (by the thread whose the GPU is: the emulation thread, or the GE thread at frames' ends); whether
  //the GPU presents the frames (the host's to read); the picture presented last, for shot() (shotMutex's: its pixels
  //as they were in the target, and its GE format).
  std::mutex windowMutex;
  void* hostWindow = nullptr;
  void* gpuWindow = nullptr;
  std::atomic<bool> presents{false};
  std::mutex shotMutex;
  std::vector<u32> shotPixels;
  u32 shotWidth = 0, shotHeight = 0, shotFormat = 3;
  auto present(const Kernel::Shown& showing, GE& drawing, std::vector<u32>& pixels,
               const std::function<auto (std::vector<u32>&) -> void>& memoryPicture) -> bool;
  auto handWindow() -> void;
  static auto hold(void* window, bool held) -> void;
  friend auto window(void* window) -> void;
  friend auto presenting() -> bool;
  friend auto shot(std::vector<u32>& pixels, u32& width, u32& height) -> bool;
  //(unload()) A GE thread left stuck, kept till it has come back (unwedge()); the machine under it left as it was
  std::unique_ptr<GeThread> stuckGE;
  auto unwedge(u32 milliseconds) -> bool;
  auto drainWithin() -> bool;  //(the GE thread done with everything handed to it, within two seconds)
  auto unloadMachine() -> void;
  auto unloadNodes() -> void;
  friend auto wedged() -> bool;
  friend auto unwedge(u32 milliseconds) -> bool;
  std::mutex noticeLock;
  std::string pendingNotice;  //(notice())
  auto startRenderer() -> void;
  auto keepPipelines() -> void;
  static constexpr u64 MostPipelineBytes = 32 << 20;  //(a kept pipeline cache's)
  u64 pipelinesKept = 0;     //(the GPU's pipelines count as they were last kept)
  u64 pipelineBytes = 0;     //(the kept cache's size, as read or last written)
  u32 framesSinceKept = 0;
  bool pipelinesUnkept = false;  //(its file couldn't be written: said once)
  auto tell(const std::string& text) -> void;
  friend auto notice() -> string;
};

extern System system;
