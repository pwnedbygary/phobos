namespace ares::NeoGeo {

APU apu;
#include "memory.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto APU::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("APU");
  //the Neo Geo CD's Z80 has 64KiB of RAM, which the BIOS loads with the sound program, in place of
  //the cartridge's M1 ROM and the 2KiB of work RAM
  ram.allocate(NeoGeo::Model::NeoGeoCD() ? 64_KiB : 2_KiB);
  debugger.load(node);
}

auto APU::unload() -> void {
  debugger.unload(node);
  ram.reset();
  node.reset();
}

auto APU::main() -> void {
  if(held) return step(64);

  if(nmi.pending && nmi.enable) {
    Z80::nmi();
    nmi.pending = 0;
    debugger.interrupt("NMI");
  }

  if(irq.pending) {
    Z80::irq();
    debugger.interrupt("IRQ");
  }

  debugger.instruction();
  Z80::instruction();
}

auto APU::step(u32 clocks) -> void {
  Thread::step(clocks);
  Thread::synchronize();
}

auto APU::power(bool reset) -> void {
  Z80::bus = this;
  Z80::power();
  Thread::create(4'000'000, std::bind_front(&APU::main, this));
  communication = {};
  nmi = {};
  irq = {};
  rom.bankA = 0x02;
  rom.bankB = 0x06;
  rom.bankC = 0x0e;
  rom.bankD = 0x1e;
  //the Neo Geo CD's Z80 waits for the BIOS to load its RAM and release it (setReset)
  held = NeoGeo::Model::NeoGeoCD();
}

auto APU::setReset(bool line) -> void {
  held = line;
  if(line) return;
  Z80::reset();
  communication = {};
  nmi = {};
  irq = {};
  rom.bankA = 0x02;
  rom.bankB = 0x06;
  rom.bankC = 0x0e;
  rom.bankD = 0x1e;
  opnb.reset();
}

}
