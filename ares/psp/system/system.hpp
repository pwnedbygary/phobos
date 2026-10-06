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
  //stick.
  struct Controls {
    Node::Object node;
    Node::Input::Button up, down, left, right, triangle, circle, cross, square, l, r, select, start;
    Node::Input::Axis x, y;

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

  string memoryStick;          //the host folder standing for ms0: (option "Memory Stick")
  bool recompile = true;       //the CPU's recompiler on, the interpreter its fallback (option "Recompiler")
  u32 geThreads = 0;           //how many threads draw (option "GE Threads"): 0 for one fewer than the host's cores
  static constexpr u32 MostGeThreads = 64;  //and at most, nor more than twice the host's cores

  auto name() const -> string { return "PlayStation Portable"; }

  //system.cpp
  auto game() -> string;
  auto run() -> void;
  auto load(Node::System& node, string name) -> bool;
  auto unload() -> void;
  auto save() -> void;
  auto power(bool reset) -> void;
  auto serialize(bool synchronize) -> serializer;
  auto unserialize(serializer& s) -> bool;

private:
  std::vector<u8*> pageTable;  //the CPU's view of memory, page by page (Memory::buildPages)
  std::vector<u32> pixels;     //the frame the game shows
  std::vector<s16> sound;      //the frame's sound, left and right samples side by side (Kernel::audioOutput())
  f64 soundOwed = 0;           //sound frames due to the speakers: 44100 a second, so 735.7 a frame
  u32 unmappedReports = 0;     //accesses to nothing, reported (the first few only)
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
};

extern System system;
