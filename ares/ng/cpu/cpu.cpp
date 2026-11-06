#include <ng/ng.hpp>
#if defined(__ANDROID__)
#include <android/log.h>
#endif

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
#if defined(__ANDROID__)
  static u64 steps = 0, samples = 0;
  if((++steps & 0x3ffff) == 0 && samples < 60) { samples++; u32 sp=(unsigned)r.a[7]; u32 ra = read(1,1,sp)<<16 | read(1,1,sp+2); __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "pc=%06x ra=%06x", (unsigned)r.pc, ra); }
#endif
  if(io.interruptPending) {
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
      return interrupt(Vector::Level2, 2);
    }

    if(1 > r.i && lower(Interrupt::Vblank)) {
      debugger.interrupt("Vblank");
#if defined(__ANDROID__)
      static u32 vd = 0; if((++vd & 127) == 0) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "68k vblank dispatch %u", vd);
#endif
      return interrupt(Vector::Level1, 1);
    }

    //Neo Geo CD: CDD interrupts (level 2, vectors 0x15/0x16/0x17). The line stays asserted while
    //any type is unacked; the BIOS acknowledges each type via $FF000F, which clears only that type.
    if(NeoGeo::Model::NeoGeoCD() && 2 > r.i) {
      if(io.interruptPending.bit((u32)Interrupt::CDD)) {
        static u32 cdds = 0;
        if((++cdds & 31) == 0) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "68k CDD dispatch %u t1=%d t2=%d t3=%d", cdds, (int)cdd.type1Pending, (int)cdd.type2Pending, (int)cdd.type3Pending);
        if(cdd.type3Pending) { cdd.type3Ack = 0; return interrupt(Vector::CDDType3, 2); }
        if(cdd.type1Pending) { cdd.type1Ack = 0; return interrupt(Vector::CDDType1, 2); }
        if(cdd.type2Pending) { cdd.type2Ack = 0; return interrupt(Vector::CDDType2, 2); }
        io.interruptPending.bit((u32)Interrupt::CDD) = 0;
      }
    }
  }

  debugger.instruction();
  instruction();

  //MVS coin pulse: hold the SELECT-generated coin line for a few frames so the counter samples it.
  if(system.io.coinPulseTimer > 0) {
    system.io.coinPulseTimer--;
    system.io.coinPulse = 1;
  } else {
    system.io.coinPulse = 0;
  }
}

auto CPU::idle(u32 clocks) -> void {
  Thread::step(clocks);
  watchdog(clocks);
}

auto CPU::wait(u32 clocks) -> void {
  Thread::step(clocks);
  watchdog(clocks);
  Thread::synchronize();
}

auto CPU::watchdog(u32 clocks) -> void {
  //the Neo Geo CD's BIOS does not kick the MVS watchdog at the MVS rate
  return;  //TEMP: watchdog off for the AES corruption bisect
  if((io.watchdog += clocks) < 1'622'015) return;
  io.watchdog = 0;
  system.watchdogReset = true;
  scheduler.exit(Event::Step);
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
