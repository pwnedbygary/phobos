struct System {
  Node::System node;
  VFS::Pak pak;
  Memory::Readable<n16> bios;
  Memory::Readable<n16> srom;
  Memory::Writable<n16> wram;
  Memory::Writable<n16> sram;  //MVS only

  //Neo Geo CD only
  Memory::Writable<n16> spriteRam;  // 4MiB
  Memory::Writable<n8> pcmRam;     // 1MiB
  Memory::Writable<n8> fixRam;     // 128KiB

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
    n32 rtcCounter;
    n1 rtcTimePulse;
    n1 coin = 0;        //MVS coin line held low (inserted). Driven by START (auto-credit) or a SELECT coin pulse.
    n1 coinPulse = 0;   //oneshot coin pulse (active for a few frames) triggered by a SELECT press edge.
    n32 coinPulseTimer = 0;  //counts down the active pulse duration in CPU instructions.

    //uPD4990A serial RTC (MVS). Neo Geo ties c0/c1/c2 high => serial mode.
    struct RTC {
      enum : u32 { MODE_REGISTER_HOLD = 0, MODE_SHIFT = 1, MODE_TIME_SET = 2, MODE_TIME_READ = 3 };
      u8 shiftReg[7] = {0};      //shiftReg[0..5] = 48-bit data; shiftReg[6] = latched 4-bit command
      u8 timeCounter[6] = {0x00, 0x00, 0x00, 0x01, 0x11, 0x24};  //sec,min,hour,day,(month<<4|weekday),year (BCD: 2024-01-01 Mon)
      u8 command = 0;            //latched command
      u1 din = 0, clk = 0, stb = 0;
      u1 dataOut = 0;            //serial data out (REG_STATUS_A bit 7)
    } rtc;
  } io;

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
