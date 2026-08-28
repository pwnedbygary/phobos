namespace ares::NeoGeo {

CPU cpu;

#include "memory.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto CPU::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("CPU");
  debugger.load(node);
}

auto CPU::unload() -> void {
  debugger = {};
  node.reset();
}

auto CPU::main() -> void {
  if(io.interruptPending) {
    if(NeoGeo::Model::NeoGeoCD()) {
    }
    if(lower(Interrupt::Reset)) {
      r.a[7] = read(1, 1, 0) << 16 | read(1, 1, 2) << 0;
      r.pc   = read(1, 1, 4) << 16 | read(1, 1, 6) << 0;
      prefetch();
      prefetch();
      debugger.interrupt("Reset");
    }

    if(3 > r.i && lower(Interrupt::Power)) {
      debugger.interrupt("Power");
      return interrupt(Vector::Level3, 3);
    }

    if(2 > r.i && lower(Interrupt::Timer)) {
      debugger.interrupt("Timer");
      if(NeoGeo::Model::NeoGeoCD()) return interrupt(Vector::Level1, 1);
      return interrupt(Vector::Level2, 2);
    }

    if(1 > r.i && lower(Interrupt::Vblank)) {
      debugger.interrupt("Vblank");
      if(NeoGeo::Model::NeoGeoCD()) return interrupt(Vector::Level2, 2);
      return interrupt(Vector::Level1, 1);
    }

    //Neo Geo CD: CDD interrupts (level 2, distinct vectors 0x15/0x16/0x17).
    //The 75Hz CDD tick asserts type2 while the read/DMA completion asserts
    //type1/type3. This is a normal level-2 IRQ (MAME: set_input_line(2)):
    //accepted only while SR IPL < 2 — the 68k masks the level once taken
    //(r.i = 2 in exception()) and only re-takes it after the handler's RTE
    //restores SR, by which time the BIOS has acked the type via $FF000F.
    //Hardware semantics (MAME ngcd_state::irq_update): the IRQ line stays
    //asserted while ANY type is unacked; the BIOS acknowledges each type
    //(bits 5/4/3 = 0x20/0x10/0x08 = type3/type2/type1), which clears ONLY
    //that type, and the next dispatch takes the next unacked type.  Pending
    //flags are therefore consumed by the ACK write, NOT by the dispatch —
    //otherwise a continuously-streaming type3 sector IRQ starves the 75Hz
    //type2 access IRQ and the BIOS's boot wait loop ($76BC>=8, advanced by
    //the access handler) never completes.
    if(NeoGeo::Model::NeoGeoCD() && 2 > r.i) {
      if(io.interruptPending.bit((u32)Interrupt::CDD)) {
        if(cdd.type3Pending) { cdd.type3Ack = 0; return interrupt(Vector::CDDType3, 2); }
        if(cdd.type1Pending) { cdd.type1Ack = 0; return interrupt(Vector::CDDType1, 2); }
        if(cdd.type2Pending) { cdd.type2Ack = 0; return interrupt(Vector::CDDType2, 2); }
        //line asserted but no type pending (late ack / spurious): deassert
        io.interruptPending.bit((u32)Interrupt::CDD) = 0;
      }
    }
  }

  debugger.instruction();
  instruction();

  //MVS coin pulse: keep the SELECT-generated coin line asserted for a few
  //frames so the BIOS coin counter samples a clean high->low->high transition.
  if(system.io.coinPulseTimer > 0) {
    system.io.coinPulseTimer--;
    system.io.coinPulse = 1;
  } else {
    system.io.coinPulse = 0;
  }
}

auto CPU::idle(u32 clocks) -> void {
  Thread::step(clocks);
}

auto CPU::wait(u32 clocks) -> void {
  Thread::step(clocks);
  system.io.rtcCounter += clocks;
  if(system.io.rtcCounter >= 6'000'000) {
    system.io.rtcCounter -= 6'000'000;
    system.io.rtcTimePulse ^= 1;
  }
  Thread::synchronize();
}

auto CPU::raise(Interrupt interrupt) -> void {
  io.interruptPending.bit((u32)interrupt) = 1;
}

auto CPU::lower(Interrupt interrupt) -> bool {
  if(!io.interruptPending.bit((u32)interrupt)) return false;
  return io.interruptPending.bit((u32)interrupt) = 0, true;
}

auto CPU::power(bool reset) -> void {
  M68000::power();
  Thread::create(12'000'000, std::bind_front(&CPU::main, this));
  io = {};
  raise(Interrupt::Reset);
}

}