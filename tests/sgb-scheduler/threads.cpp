//The schedulers' threads (ares/ares/scheduler/) as the desktop program builds and links them (phobos_core's objects;
//CMakeLists.txt's target scheduler-threads, which ctest runs). A Thread runs on one scheduler for its whole life:
//ares::scheduler, or the one it's made with. The Game Boy's threads are made on GameBoy::scheduler, so that the Super
//Game Boy's ICD can run the Game Boy (GameBoy::scheduler.enter()) from inside the Super Famicom's scheduler.
//T1, stand-ins shaped like the Super Famicom's CPU and PPU and the ICD on ares::scheduler, and like the Game Boy's CPU
//and PPU on a second scheduler, which the ICD enters from inside the first: frames must come back with the Game Boy
//never started (past the 2 s a thread's u64 clock lasts beside an idle thread), started mid-run, alone with a
//Synchronize pass every 30 frames (a save state's, whose safe point is Thread::Enter's loop), and inside the Super
//Famicom with that scheduler's passes; a pass's auxiliary threads mustn't run its primary; a thread restarted (as an
//LCD write restarts the Game Boy's PPU) stays on its scheduler; a destroyed thread leaves its own.
//T2, the Game Boy core's threads: after power(), and after an LCD off and on (ppu/io.cpp makes the PPU's thread again),
//all four are on GameBoy::scheduler and none on ares::scheduler; resetting one scheduler leaves the other's threads
//that haven't started yet able to start (their entry points kept).
//A stall, a scheduler that gives no frame back, fails the program after 10 s.
#include <gb/gb.hpp>

#include "common.hpp"

namespace {

using ares::Event;
using ares::Scheduler;

Scheduler gbScheduler;  //the second scheduler, in GameBoy::scheduler's place

//stand-ins with the shapes that matter: the Game Boy's CPU synchronizes every thread on its scheduler each clock (as
//GameBoy::CPU does) and, inside the Super Game Boy, exits its scheduler after each instruction (CPU::main); its PPU
//ends a frame every 70,224 clocks; the ICD runs the Game Boy's scheduler once per step of its own
struct Machine {
  bool sgb = true;
  bool gbRunning = false;
  u64 gbStartFrame = ~0ull;
  u64 sfcFrames = 0, gbFrames = 0, gbInstructions = 0, gbCpuSteps = 0, sfcCpuSteps = 0;

  struct GBCPU : ares::Thread {
    Machine& m;
    GBCPU(Machine& m) : Thread(gbScheduler), m(m) {}
    auto main() -> void {
      for(u32 n = 0; n < 4; n++) {
        step(1);
        m.gbCpuSteps++;
        synchronize();
      }
      m.gbInstructions++;
      if(m.sgb) gbScheduler.exit(Event::Step);
    }
  } gbcpu{*this};

  struct GBPPU : ares::Thread {
    Machine& m;
    u64 dots = 0;
    GBPPU(Machine& m) : Thread(gbScheduler), m(m) {}
    auto main() -> void {
      step(4);
      if(++dots % 17'556 == 0) {
        m.gbFrames++;
        gbScheduler.exit(Event::Frame);
      }
      synchronize(m.gbcpu);
    }
  } gbppu{*this};

  struct SFCCPU : ares::Thread {
    Machine& m;
    SFCCPU(Machine& m) : m(m) {}
    auto main() -> void;
  } sfccpu{*this};

  struct SFCPPU : ares::Thread {
    Machine& m;
    u64 dots = 0;
    SFCPPU(Machine& m) : m(m) {}
    auto main() -> void {
      step(4);
      if(++dots % 89'342 == 0) {
        m.sfcFrames++;
        ares::scheduler.exit(Event::Frame);
      }
      synchronize(m.sfccpu);
    }
  } sfcppu{*this};

  struct ICD : ares::Thread {
    Machine& m;
    ICD(Machine& m) : m(m) {}
    auto main() -> void {
      if(m.gbRunning) {
        gbScheduler.enter();
        step(4);
      } else {
        step(2);
      }
      synchronize(m.sfccpu);
    }
    auto power() -> void {
      create(4'294'967.0, [this] {
        while(true) {
          ares::scheduler.synchronize();
          main();
        }
      });
    }
  } icd{*this};

  //as GameBoy::System::power(): the threads made again, the CPU the primary
  auto gameBoyPower() -> void {
    gbcpu.create(4'194'304.0, std::bind_front(&GBCPU::main, &gbcpu));
    gbppu.create(4'194'304.0, std::bind_front(&GBPPU::main, &gbppu));
    gbScheduler.power(gbcpu);
  }

  //as the BIOS's write to $6003: the ICD and the Game Boy powered again, from inside the Super Famicom's CPU
  auto startGameBoy() -> void {
    icd.power();
    gameBoyPower();
    gbRunning = true;
  }
};

auto Machine::SFCCPU::main() -> void {
  step(6);
  m.sfcCpuSteps++;
  synchronize(m.sfcppu, m.icd);
  if(!m.gbRunning && m.sfcFrames >= m.gbStartFrame) m.startGameBoy();
}

auto fresh() -> void {
  ares::scheduler.reset();
  gbScheduler.reset();
}

//a Synchronize pass (what serialize(true) does first) on `scheduler`, whose primary thread's steps are `steps`: true
//when the pass comes back and the primary didn't run after reaching its safe point
auto pass(Scheduler& scheduler, const u64& steps) -> bool {
  u64 atSafePoint = ~0ull;
  ares::testPlatform.onEvent = [&](Event event) {
    if(event == Event::Synchronize && atSafePoint == ~0ull) atSafePoint = steps;  //the primary's, the pass's first
  };
  scheduler.enter(Scheduler::Mode::Synchronize);
  ares::testPlatform.onEvent = {};
  test::progress++;
  return atSafePoint == steps;
}

//the Super Game Boy's shape: the Game Boy's threads made at load (as ICD::load and power() make them), started at
//`startFrame` if ever; with `passes`, a pass on the Super Famicom's scheduler every 30 frames from frame 90
auto sgb(const char* name, u64 startFrame, bool passes) -> void {
  test::current = name;
  printf("%s\n", name);
  fresh();
  Machine m;
  m.gbStartFrame = startFrame;
  m.sfccpu.create(21'477'272.0, std::bind_front(&Machine::SFCCPU::main, &m.sfccpu));
  m.sfcppu.create(21'477'272.0, std::bind_front(&Machine::SFCPPU::main, &m.sfcppu));
  m.icd.power();
  m.gameBoyPower();
  ares::scheduler.power(m.sfccpu);
  u32 frames = 0, done = 0, clean = 0;
  while(frames < 300) {
    if(ares::scheduler.enter() == Event::Frame) frames++, test::progress++;
    if(passes && frames >= 90 && frames % 30 == 0 && frames / 30 - 2 > done) {
      clean += pass(ares::scheduler, m.sfcCpuSteps);
      done++;
    }
  }
  test::check(frames == 300, "300 frames came back (5 emulated seconds)");
  if(startFrame != ~0ull) {
    test::check(m.gbInstructions > 0 && m.gbFrames > 0, std::string{"the Game Boy ran: "} +
      std::to_string(m.gbInstructions) + " instructions, " + std::to_string(m.gbFrames) + " frames");
  }
  if(passes) test::check(done == 8 && clean == 8, std::to_string(done) + " passes came back, " +
    std::to_string(clean) + " without the primary running after its safe point");
  test::check(ares::scheduler.threads() == 3 && gbScheduler.threads() == 2, "threads: " +
    std::to_string(ares::scheduler.threads()) + " on ares::scheduler, " + std::to_string(gbScheduler.threads()) +
    " on the second");
}

//the Game Boy alone, as a Game Boy or Game Boy Color game runs: the runner's root->run() is its scheduler's enter();
//a pass every 30 frames; with `restartAt`, its PPU restarted at that frame (as ppu/io.cpp does on an LCD write)
auto alone(const char* name, u32 restartAt) -> void {
  test::current = name;
  printf("%s\n", name);
  fresh();
  Machine m;
  m.sgb = false;
  m.gameBoyPower();
  u32 frames = 0, done = 0, clean = 0;
  while(frames < 300) {
    if(gbScheduler.enter() == Event::Frame) frames++, test::progress++;
    if(frames == restartAt) {
      m.gbppu.restart(std::bind_front(&Machine::GBPPU::main, &m.gbppu));
      restartAt = ~0u;
    }
    if(frames % 30 == 0 && frames / 30 > done) {
      clean += pass(gbScheduler, m.gbCpuSteps);
      done++;
    }
  }
  test::check(frames == 300, "300 frames came back");
  test::check(done == 10 && clean == 10, std::to_string(done) + " passes came back, " + std::to_string(clean) +
    " without the primary running after its safe point");
  test::check(ares::scheduler.threads() == 0 && gbScheduler.threads() == 2, "threads: " +
    std::to_string(ares::scheduler.threads()) + " on ares::scheduler, " + std::to_string(gbScheduler.threads()) +
    " on the second");
}

auto destroyed() -> void {
  test::current = "T1: a destroyed thread leaves its own scheduler";
  printf("%s\n", test::current.load());
  fresh();
  struct Idle : ares::Thread {
    Idle() : Thread(gbScheduler) {}
  } idle;
  idle.create(1'000'000.0, [] {});
  bool joined = gbScheduler.threads() == 1 && ares::scheduler.threads() == 0;
  idle.destroy();
  test::check(joined, "made, it is on the second scheduler and not on ares::scheduler");
  test::check(gbScheduler.threads() == 0, "destroyed, it is off the second scheduler");
}

//a synchronized state (the runner's) holds a thread's clocks alone; an unsynchronized one its stack too
auto serialized() -> void {
  test::current = "T1: a thread's state follows its own scheduler's synchronize flag";
  printf("%s\n", test::current.load());
  fresh();
  struct Idle : ares::Thread {
    Idle() : Thread(gbScheduler) {}
  } idle;
  idle.create(1'000'000.0, [] {});
  gbScheduler.setSynchronize(true);  //ares::scheduler's flag left false
  serializer synchronized;
  idle.serialize(synchronized);
  gbScheduler.setSynchronize(false);
  serializer unsynchronized;
  idle.serialize(unsynchronized);
  test::check(synchronized.size() < 64, "synchronized: " + std::to_string(synchronized.size()) + " bytes (no stack)");
  test::check(unsynchronized.size() > ares::Thread::Size, "unsynchronized: " + std::to_string(unsynchronized.size()) +
    " bytes (the stack too)");
  idle.destroy();
}

//how many entry points of threads not yet started belong to `scheduler` (a thread made again before it started keeps
//its first one too: Thread::Enter takes the first it finds)
auto pending(const Scheduler& scheduler) -> u32 {
  u32 count = 0;
  for(auto& entry : ares::Thread::EntryPoints()) count += entry.scheduler == &scheduler;
  return count;
}

//whether each of the Game Boy's four threads (not yet started) has its entry point, and each it has is on
//GameBoy::scheduler
auto gameBoyEntries() -> bool {
  ares::Thread* threads[] = {&ares::GameBoy::cpu, &ares::GameBoy::ppu, &ares::GameBoy::apu, &ares::GameBoy::cartridge};
  for(auto thread : threads) {
    u32 own = 0, other = 0;
    for(auto& entry : ares::Thread::EntryPoints()) {
      if(entry.handle != thread->handle()) continue;
      (entry.scheduler == &ares::GameBoy::scheduler ? own : other)++;
    }
    if(!own || other) return false;
  }
  return true;
}

auto gameBoy() -> void {
  auto& scheduler = ares::GameBoy::scheduler;
  auto& bus = ares::GameBoy::bus;
  auto& cpu = ares::GameBoy::cpu;
  test::current = "T2: the Game Boy core's threads";
  printf("%s\n", test::current.load());
  fresh();
  ares::Node::System root;
  if(!test::check(test::loadGameBoy(root), "a Game Boy loads and powers on")) return;
  test::check(scheduler.threads() == 4 && ares::scheduler.threads() == 0, "after power(): " +
    std::to_string(scheduler.threads()) + " threads on GameBoy::scheduler (the CPU, PPU, APU and cartridge), " +
    std::to_string(ares::scheduler.threads()) + " on ares::scheduler");
  test::check(gameBoyEntries() && pending(ares::scheduler) == 0, "each one's entry point is GameBoy::scheduler's (" +
    std::to_string(pending(scheduler)) + " entry points on it, " + std::to_string(pending(ares::scheduler)) +
    " on ares::scheduler)");

  //a thread on ares::scheduler that hasn't started, through a reset of the Game Boy's
  struct Other : ares::Thread {
    bool started = false;
  } other;
  other.create(1'000'000.0, [&] {
    other.started = true;
    ares::scheduler.exit(Event::Step);
  });
  scheduler.reset();
  bool kept = pending(ares::scheduler) == 1;
  if(kept) {
    ares::scheduler.power(other);
    ares::scheduler.enter();
  }
  test::check(kept && other.started, "GameBoy::scheduler reset: ares::scheduler's thread not yet started still starts");
  other.destroy();
  ares::scheduler.reset();
  test::unload(root);
  scheduler.reset();

  //and the Game Boy's threads, not yet started, through a reset of ares::scheduler
  if(!test::check(test::loadGameBoy(root), "a Game Boy loads and powers on again")) return;
  ares::scheduler.reset();
  if(!test::check(gameBoyEntries(), "ares::scheduler reset: the Game Boy's threads keep their entry points (" +
    std::to_string(pending(scheduler)) + ")")) {
    test::unload(root);  //(its threads can't start: Thread::Enter would find no entry point)
    scheduler.reset();
    return;
  }
  test::runFrames(root, 10);
  test::check(cpu.r.pc.word >= 0x0100, "the Game Boy runs (10 frames, its CPU in the cartridge)");

  bus.write(0xff40, 0x00);  //the LCD off, then on: the PPU's thread is made again (ppu/io.cpp)
  bus.write(0xff40, 0x91);
  test::check(scheduler.threads() == 4 && ares::scheduler.threads() == 0, "after an LCD off and on: " +
    std::to_string(scheduler.threads()) + " threads on GameBoy::scheduler, " +
    std::to_string(ares::scheduler.threads()) + " on ares::scheduler");
  u8 before = cpu.wram[0];
  test::runFrames(root, 30);
  u8 counted = (u8)cpu.wram[0] - before;
  test::check(counted >= 25, "the PPU still ends frames: the cartridge counted " + std::to_string(counted) + " in 30");
  test::unload(root);
  scheduler.reset();
}

}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  test::watchdog("scheduler-threads", 10);
  sgb("T1: the Super Game Boy's shape, the Game Boy never started", ~0ull, false);
  sgb("T1: the Super Game Boy's shape, the Game Boy started at frame 60", 60, false);
  sgb("T1: the Super Game Boy's shape, passes on the Super Famicom's scheduler", 60, true);
  alone("T1: the Game Boy alone, a pass every 30 frames", ~0u);
  alone("T1: the Game Boy alone, its PPU restarted at frame 100", 100);
  destroyed();
  serialized();
  gameBoy();
  printf("scheduler-threads: %u failed\n", test::failures);
  return test::failures ? 1 : 0;
}
