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
  if(NeoGeo::Model::NeoGeoCD()) {
    static FILE* f = nullptr;
    static u32 n = 0;
    if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/cputrace.txt", "w");
    if(f && n++ < 400000) fprintf(f, "%06x %04x\n", (u32)r.pc, (u32)r.irc);
    if(n == 400000 && f) { fflush(f); fclose(f); }
  }
  if(io.interruptPending) {
    if(NeoGeo::Model::NeoGeoCD()) {
      static FILE* f = nullptr;
      static u32 n = 0;
      if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/pendlog.txt", "w");
      if(f && n++ < 3000) { fprintf(f, "PEND ipl=%d t1=%d t2=%d t3=%d pc=%06x\n", (u32)r.i, (u32)cdd.type1Pending, (u32)cdd.type2Pending, (u32)cdd.type3Pending, (u32)r.pc); fflush(f); }
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
    //type1/type3. The BIOS masks to IPL 7 during the boot processing, so the
    //CDD IRQs are taken regardless of the CPU IPL (mirrors the hardware where
    //the sector/CDC completion must interrupt the loader).
    if(NeoGeo::Model::NeoGeoCD()) {
      if(lower(Interrupt::CDD)) {
        debugger.interrupt("CDD");
        if(cdd.type3Pending) { cdd.type3Pending = 0; cdd.type3Ack = 0; return interrupt(Vector::CDDType3, 2); }
        if(cdd.type1Pending) { cdd.type1Pending = 0; cdd.type1Ack = 0; return interrupt(Vector::CDDType1, 2); }
        if(cdd.type2Pending) { cdd.type2Pending = 0; cdd.type2Ack = 0; return interrupt(Vector::CDDType2, 2); }
        return interrupt(Vector::CDDType2, 2);
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
