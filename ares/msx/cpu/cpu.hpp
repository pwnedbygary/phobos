struct CPU : Z80, Z80::Bus, Thread {
  Node::Object node;
  Memory::Writable<n8> ram;  //MSX = 64KB, MSX2 = 256KB

  struct Debugger {
    //debugger.cpp
    auto load(Node::Object) -> void;
    auto instruction() -> void;
    auto interrupt(string_view) -> void;

    struct Memory {
      Node::Debugger::Memory ram;
    } memory;

    struct Tracer {
      Node::Debugger::Tracer::Instruction instruction;
      Node::Debugger::Tracer::Notification interrupt;
    } tracer;
  } debugger;

  auto synchronizing() const -> bool override { return scheduler.synchronizing(); }

  //cpu.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;

  auto main() -> void;
  auto step(u32 clocks) -> void override;

  auto power() -> void;
  auto setIRQ(bool) -> void;

  //memory.cpp
  auto read(n16 address) -> n8 override;
  auto write(n16 address, n8 data) -> void override;

  auto in(n16 address) -> n8 override;
  auto out(n16 address, n8 data) -> void override;

  auto readPrimarySlot() -> n8;
  auto writePrimarySlot(n8 data) -> void;

  auto readSecondarySlot() -> n8;
  auto writeSecondarySlot(n8 data) -> void;

  auto writePortC(n8 data) -> void;
  auto writePPIControl(n8 data) -> void;
  auto cassetteMotor() const -> bool { return !io.portC.bit(4); }

  //serialization.cpp
  auto serialize(serializer&) -> void;

private:
  struct Slot {
    n8 memory;
    n2 primary;
    n2 secondary[4];
  } slot[4];

  struct IO {
    n1 irqLine;
    //[Phobos] The PPI's port C: the keyboard row (bits 0-3) and the cassette motor relay (bit 4, low
    //runs the motor). Until the BIOS sets the PPI up, its pins are inputs and the motor stays off.
    n8 portC = 0xff;
  } io;
};

extern CPU cpu;
