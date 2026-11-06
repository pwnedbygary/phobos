struct RTC : uPD4990A, Thread {
  auto main() -> void;
  auto step(u32 clocks) -> void;
  auto power() -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;
};

struct System {
  Node::System node;
  VFS::Pak pak;
  Arcade::Dip dips;
  Memory::Readable<n16> bios;
  Memory::Readable<n16> srom;
  Memory::Writable<n16> wram;
  Memory::Writable<n16> sram;  //MVS only
  //Neo Geo CD only
  Memory::Writable<n16> spriteRam;  //4MiB
  Memory::Writable<n8> pcmRam;      //1MiB
  Memory::Writable<n8> fixRam;      //128KiB
  RTC rtc;

  struct ArcadeControls {
    Node::Object port, device;
    Node::Input::Button coin[2], service, test;
    auto load(Node::Object) -> void;
    auto unload() -> void;
    auto poll() -> void;
  } arcadeControls;

  struct Debugger {
    //debugger.cpp
    auto load(Node::Object) -> void;
    auto unload(Node::Object) -> void;

    struct Memory {
      Node::Debugger::Memory bios;
      Node::Debugger::Memory srom;
      Node::Debugger::Memory wram;
      Node::Debugger::Memory sram;
    } memory;
  } debugger;

  enum class Model : u32 { NeoGeoAES, NeoGeoMVS, NeoGeoCD };

  auto name() const -> string { return information.name; }
  auto model() const -> Model { return information.model; }

  //system.cpp
  auto game() -> string;
  auto run() -> void;
  auto readC(n32) -> n8;
  auto readS(n32) -> n8;
  auto readVA(n32) -> n8;
  auto readVB(n32) -> n8;
  auto dumpNgGfx(const string& dir) -> void;

  auto load(Node::System& node, string name) -> bool;
  auto unload() -> void;
  auto save() -> void;
  auto power(bool reset = false) -> void;

  //serialization.cpp
  auto serialize(bool synchronize) -> serializer;
  auto unserialize(serializer&) -> bool;

  struct IO {
    n1 sramLock = 1;
    n3 slotSelect;
    n1 ledMarquee;
    n1 ledLatch1;
    n1 ledLatch2;
    n8 ledData;

    //NEO-C1 controller selector (written via $380001). Controller data at
    //$300000/$340000/$380000 is only returned when the selector is 0x00/0x12/0x1B
    //(libretro neocd controller1/2/3Handlers). This is what the CD BIOS sets.
    n8 ctrlSelector = 0;
    //Neo Geo CD upload control
    n3 uploadZone;
    n2 spriteUploadBank;
    n1 pcmUploadBank;
    //Neo Geo CD $FF0004: VBL (0x030) and raster timer (0x300) interrupt enables.
    n16 irqMask2;
    n1 coin = 0;        //MVS coin line held low (inserted), driven by START/SELECT (ArcadeStick::pollCoin).
    n1 coinPulse = 0;
    n32 coinPulseTimer = 0;
  } io;

  bool watchdogReset = false;

private:
  struct Information {
    string name;
    Model model = Model::NeoGeoAES;
  } information;

  //serialization.cpp
  auto serialize(serializer&, bool synchronize) -> void;
};

extern System system;

auto Model::NeoGeoAES() -> bool { return system.model() == System::Model::NeoGeoAES; }
auto Model::NeoGeoMVS() -> bool { return system.model() == System::Model::NeoGeoMVS; }
auto Model::NeoGeoCD() -> bool { return system.model() == System::Model::NeoGeoCD; }
