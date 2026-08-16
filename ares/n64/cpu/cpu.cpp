#include <nall/gdb/server.hpp>

namespace ares::Nintendo64 {

CPU cpu;
#include "context.cpp"
#include "dcache.cpp"
#include "tlb.cpp"
#include "memory.cpp"
#include "exceptions.cpp"
#include "algorithms.cpp"
#include "interpreter.cpp"
#include "decoder.cpp"
#include "interpreter-ipu.cpp"
#include "interpreter-scc.cpp"
#include "interpreter-fpu.cpp"
#include "interpreter-cop2.cpp"
#include "recompiler.cpp"
#include "recompiler-fpu.cpp"
#include "recompiler-ipu.cpp"
#include "debugger.cpp"
#include "serialization.cpp"
#include "disassembler.cpp"
#include "emux.cpp"

auto CPU::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("CPU");
  debugger.load(node);
}

auto CPU::unload() -> void {
  debugger.unload();
  node.reset();
}

auto CPU::main() -> void {
  while(!vi.refreshed && GDB::server.reportPC(ipu.pc & 0xFFFFFFFF)) {
    if(instruction()) synchronize();
  }

  vi.refreshed = false;
  queue.remove(Queue::GDB_Poll);
  if(GDB::server.hasClient()) {
    queueInsert(Queue::GDB_Poll, (93750000*2)/60/240);
  }
}

auto CPU::gdbPoll() -> void {
  if(GDB::server.hasClient()) {
    GDB::server.updateLoop();
    queueInsert(Queue::GDB_Poll, (93750000*2)/60/240);
  }
}

auto CPU::queueInsert(u32 event, u32 clocks) -> void {
  if(!queue.insert(event, clocks)) return;
  s64 queueDelta = queue.timeToNextEvent();
  if(queueDelta < 0) queueDelta = 0;
  s64 queueTarget = Thread::clock + queueDelta;
  if(queueTarget < jitClockTarget) jitClockTarget = queueTarget;
}

auto CPU::forceSynchronize() -> void {
  jitClockTarget = 0;
}

auto CPU::synchronize() -> void {
  auto clocks = Thread::clock;
  Thread::clock = 0;
  jitClockTarget = 0;

  // R4300 overclock (Mupen64Plus-FZ "Overclocking Factor", 2^factor):
  // peripherals consume CPU cycles 2^factor slower, so the CPU executes
  // 2^factor more instructions per VI/AI frame — the game simulation runs
  // faster at the same rendered frame rate. The Count register keeps its
  // hardware ratio to the (overclocked) CPU clock below (full clocks), so
  // the game's own timer cadence scales coherently with the faster CPU.
  s64 peripheralClocks = clocks;
  if (s32 f = overclockFactor.load(); f > 0) peripheralClocks >>= f;

   vi.clock -= peripheralClocks;
   ai.clock -= peripheralClocks;
  rsp.clock -= peripheralClocks;
  rdp.clock -= peripheralClocks;
  pif.clock -= peripheralClocks;
  vi.main();
  ai.main();
  rsp.main();
  rdp.main();
  pif.main();

  queue.step(peripheralClocks, [](u32 event) {
    switch(event) {
    case Queue::PI_DMA_Read:   return pi.dmaFinished();
    case Queue::PI_DMA_Write:  return pi.dmaFinished();
    case Queue::PI_BUS_Write:  return pi.writeFinished();
    case Queue::SI_DMA_Read:   return si.dmaRead();
    case Queue::SI_DMA_Write:  return si.dmaWrite();
    case Queue::SI_BUS_Write:  return si.writeFinished();
    case Queue::RTC_Tick:      return cartridge.rtc.tick();
    case Queue::EEPROM_Write:  return cartridge.eepromFinish();
    case Queue::Flash_Complete: return cartridge.flash.finish();
    case Queue::DD_Clock_Tick:  return dd.rtc.tickClock();
    case Queue::DD_MECHA_Response:  return dd.mechaResponse();
    case Queue::DD_BM_Request:  return dd.bmRequest();
    case Queue::DD_Motor_Mode:  return dd.motorChange();
    case Queue::GDB_Poll:      return cpu.gdbPoll();
    }
  });

  // Count Per Operation (Mupen64Plus-FZ style): scales the CP0 Count
  // register increment. Default 2 = stock hardware rate (Count advances
  // every 2 CPU cycles, i.e. clocks/2). Lower (1) → Count advances slower
  // per instruction → compare/timer interrupt fires after more instructions
  // → game overclocked. Higher (3) → underclocked. Count is a CPU register,
  // so it scales with the full (overclocked) CPU clock.
  s64 countIncrement = clocks * countPerOp.load() / 4;
  if(countIncrement < 0) countIncrement = 0;
  if(scc.count < scc.compare && scc.count + countIncrement >= scc.compare) {
    setInterruptPending(Interrupt::Timer, 1);
  }
  scc.count += countIncrement;
  profile.cpuCycles += clocks;
  if (scc.status.exceptionLevel) profile.cpuCyclesExc += clocks;
}

auto CPU::setInterruptPending(u32 bit, bool value) -> void {
  scc.cause.interruptPending.bit(bit) = value;
  // [Phobos diag] Mischief Makers: trace the RCP (bit 2) interrupt — is it
  // being set by the RSP BREAK and then cleared, or never set? Logs on
  // every RCP change (gated behind the debug toggle).
  if (bit == Interrupt::RCP && ::ares::n64DebugLoggingEnabled()) {
    __android_log_print(ANDROID_LOG_WARN, "PhobosCPU",
      "setInterruptPending: RCP=%d cause.pend=%02x IE=%d EXL=%d",
      (int)value, (int)(u32)scc.cause.interruptPending,
      (int)scc.status.interruptEnable, (int)scc.status.exceptionLevel);
  }
  interruptPoll();
}

auto CPU::interruptPoll() -> void {
  if(auto interrupts = scc.cause.interruptPending & scc.status.interruptMask) {
    if(scc.status.interruptEnable && !scc.status.exceptionLevel && !scc.status.errorLevel) {
      forceSynchronize();
    }
  }
}

auto CPU::instruction() -> bool {
  if(auto interrupts = scc.cause.interruptPending & scc.status.interruptMask) {
    if(scc.status.interruptEnable && !scc.status.exceptionLevel && !scc.status.errorLevel) {
      debugger.interrupt(scc.cause.interruptPending);
      step(1 * 2);
      exception.interrupt();
      return true;
    }
  }

  if (scc.nmiPending) {
    debugger.nmi();
    step(1 * 2);
    exception.nmi();
    return true;
  }
  if (scc.sysadFrozen) {
    step(1 * 2);
    return true;
  }

  auto access = devirtualize<Read, Word>(ipu.pc);
  if(!access) return true;

  if(Accuracy::CPU::Recompiler && recompiler.enabled && access.cache) {
    if(vaddrAlignedError<Word>(access.vaddr, false)) return true;
    auto block = recompiler.block(ipu.pc, access.paddr);
    if(block) {
      if(Thread::clock >= jitClockTarget) {
        s64 timerDelta = (s64)scc.compare - (s64)scc.count;
        if(timerDelta < 0) timerDelta = 0;
        s64 queueDelta = queue.timeToNextEvent();
        if(queueDelta < 0) queueDelta = 0;
        s64 capBudget = min<s64>(Accuracy::CPU::JitInterleaving, min(timerDelta, queueDelta));
        jitClockTarget = Thread::clock + capBudget;
      }
      block->execute(*this);
      return Thread::clock >= jitClockTarget;
    }
  }

  auto data = fetch(access);
  if (!data) return true;
  pipeline.begin();
  instructionPrologue(ipu.pc, *data);
  decoderEXECUTE(*data);
  instructionEpilogue<0>();
  pipeline.end();
  return true;
}

auto CPU::instructionPrologue(u64 address, u32 instruction) -> void {
  debugger.instruction(address, instruction);
}

auto CPU::icacheFillLine(u64 vaddr, u32 paddr) -> void {
  icache.line(vaddr).fill(paddr, *this);
}

template<bool Recompiled>
auto CPU::instructionEpilogue() -> void {
  if constexpr(!Recompiled) {
    ipu.r[0].u64 = 0;
  }
}

auto CPU::raiseCoprocessor1Exception() -> void {
  exception.coprocessor1();
}

auto CPU::power(bool reset) -> void {
  Thread::reset();

  context.endian = Context::Endian::Big;
  context.mode = Context::Mode::Kernel;
  context.bits = 64;
  for(auto& segment : context.segment) segment = Context::Segment::Unused;
  icache.power(reset);
  dcache.power(reset);
  for(auto& entry : tlb.entry) entry = {}, entry.synchronize();
  tlb.physicalAddress = 0;
  for(auto& r : ipu.r) r.u64 = 0;
  ipu.lo.u64 = 0;
  ipu.hi.u64 = 0;
  ipu.r[29].u64 = 0xffff'ffff'a400'1ff0ull;  //stack pointer
  pipeline.setPc(0xffff'ffff'bfc0'0000ull);
  scc = {};
  for(auto& r : fpu.r) r.u64 = 0;
  fpu.csr = {};
  cop2 = {};
  emuxState = {};
  fenv.setRound(float_env::toNearest);
  context.setMode();

  if constexpr(Accuracy::CPU::Recompiler) {
    auto buffer = ares::Memory::FixedAllocator::get().tryAcquire(63_MiB);
    recompiler.allocator.resize(63_MiB, bump_allocator::executable, buffer);
    recompiler.reset();
  }
}

}
