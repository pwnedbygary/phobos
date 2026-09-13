namespace ares::MSX {

CPU cpu;
#include "memory.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto CPU::load(Node::Object parent) -> void {
  if(Model::MSX()) ram.allocate(64_KiB);
  if(Model::MSX2()) ram.allocate(512_KiB);

  node = parent->append<Node::Object>("CPU");

  debugger.load(node);
}

auto CPU::unload() -> void {
  ram.reset();
  node = {};
  debugger = {};
}

auto CPU::main() -> void {
  if(io.irqLine) {
    debugger.interrupt("IRQ");
    irq();
  }

  debugger.instruction();
  instruction();
}

auto CPU::step(u32 clocks) -> void {
  Thread::step(clocks);
  Thread::synchronize();
}

auto CPU::power() -> void {
  Z80::bus = this;
  Z80::power();
  Thread::create(system.colorburst(), std::bind_front(&CPU::main, this));

  PC = 0x0000;  //reset vector address

  // MSX boot state: slot 0 (BIOS ROM) expanded across pages 0-1 and 3
  // so the Z80 fetches the reset vector from ROM at 0x0000. The BIOS
  // init code will reconfigure to running layout (page 0→RAM).
  // Without this, rAM zeros cause an infinite NOP-loop — black screen.
  slot[0] = {0, 0, {0, 0, 0, 0}};
  slot[1] = {0, 1, {0, 0, 0, 0}};
  slot[2] = {1, 2, {0, 0, 0, 0}};
  slot[3] = {0, 3, {0, 0, 0, 0}};

  io = {};
}

auto CPU::setIRQ(bool line) -> void {
  io.irqLine = line;
}

}
