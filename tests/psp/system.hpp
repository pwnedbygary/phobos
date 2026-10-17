//The PSP system tests' machine: the Allegrex with the real memory map (ares/psp/memory) behind it, as the PSP
//system has it (its page table, and the pages whose entries never change, so compiled code reaches main RAM as the
//system's does), and the GE, plus the Allegrex tests' instruction encoders and checks.
#pragma once

#include "../allegrex/harness.hpp"
#include "../../ares/psp/memory/memory.hpp"
#include "../../ares/psp/ge/ge.hpp"

#include <cstdlib>
#include <memory>

//Inside the Allegrex tests' namespace, so its register names (s0, s1...) hide ares's integer types of the same
//names.
namespace allegrex_test::psp {

using ares::PlayStationPortable::Memory;
using ares::PlayStationPortable::GE;
using ares::PlayStationPortable::GeThread;

//Whether the tests' machines run the GE on a thread of its own (PHOBOS_TESTS_GE_THREAD=On, run-tests.sh), as the
//system's option "GE Thread" has it (ge/thread.hpp): every result must be the same either way. Tests that look at the
//GE's own drawing machinery (its batches, the CPU's page table) keep their machines' GE on the emulation thread while
//an Unthreaded lives: the render GE has that machinery, run the same.
inline int unthreadedTests = 0;
struct Unthreaded {
  Unthreaded() { unthreadedTests++; }
  ~Unthreaded() { unthreadedTests--; }
};
//(every test machine with the GE on its thread, for drainThreads())
struct System;
inline std::vector<System*> threadedSystems;
inline auto testsGeThread() -> bool {
  const char* value = std::getenv("PHOBOS_TESTS_GE_THREAD");
  return !unthreadedTests && value && (std::string{value} == "On" || std::string{value} == "1");
}

struct System : Allegrex {
  Memory memory;
  GE ge{memory};
  std::vector<u8*> table;
  std::vector<u8> steadyTable;  //the pages whose entries in table never change (Memory::buildSteady)
  std::vector<std::pair<u32, bool>> unmapped;  //accesses with nothing behind them: the address, and whether a store
  std::mutex unmappedMutex;
  std::vector<std::pair<Exception, u32>> exceptions;

  System(u32 ramSize = 32_MiB) {
    memory.power(ramSize);
    memory.buildPages(table);
    pages = table.data();
    memory.buildSteady(steadyTable);
    steady = steadyTable.data();
    watched = memory.watched.data();
    memory.watching = [this](u32 page) { recompiler.protect(page); };
    memory.unwatched = [this](u32 page) { recompiler.unwatched(page); };
    memory.vramGuard = [this](bool busy) {
      for(u32 offset = 0; offset < Memory::VRAMSize; offset += Memory::PageSize) {
        if(!memory.vramPageBusy(offset / Memory::PageSize)) continue;
        u32 page = (Memory::VRAMBase + offset) / Memory::PageSize;
        table[page] = busy ? nullptr : &memory.vram[offset];
        recompiler.writable(page);
      }
    };
    memory.written = [this](u32 address, u32 size) { recompiler.invalidateRange(address, size); };
    memory.unmapped = [this](u32 address, bool store) {  //(the drawing threads and the GE's thread report too)
      std::lock_guard lock{unmappedMutex};
      unmapped.push_back({address, store});
    };
    exceptionHook = [this](Exception exception, u32 address) {
      exceptions.push_back({exception, address});
      scc.halted = 1;
    };
    if(testsGeThread()) threadGE(true);
  }
  ~System() { threadGE(false); }

  //(as the system's System::threadGE(): the render GE takes the GE's renderer, drawing threads and state, its
  //memory sees memory's bytes, VRAM leaves the page table, and memory's drawer has the thread done first)
  Memory renderMemory;
  GE renderGE{renderMemory};
  std::unique_ptr<GeThread> geThreading;
  auto threadGE(bool on) -> void {
    if(on == (bool)geThreading) return;
    auto vramPages = [&](bool out) {
      for(u32 offset = 0; offset < Memory::VRAMSize; offset += Memory::PageSize) {
        u32 page = (Memory::VRAMBase + offset) / Memory::PageSize;
        table[page] = out ? nullptr : &memory.vram[offset];
        recompiler.writable(page);
      }
    };
    if(on) {
      ge.settleAll();
      auto* renderer = ge.renderer;
      u32 threads = ge.drawing.threads;
      ge.setRenderer(nullptr), ge.setThreads(1);
      renderMemory.view(memory);
      renderMemory.vramGuard = [](bool) {};
      renderGE.power();
      renderGE.takeState(ge.stateOf());
      std::memcpy(renderGE.clut, ge.clut, sizeof(ge.clut)), renderGE.paletteChanged();
      renderGE.setThreads(threads), renderGE.setRenderer(renderer);
      geThreading = std::make_unique<GeThread>(ge, renderGE);
      geThreading->checking = true;  //(the value rule's check of every byte a finish or put-back changes: ge/thread.hpp)
      renderGE.log = [this](const std::string& text) { geThreading->note(text); };
      ge.render = &renderGE, ge.thread = geThreading.get();
      geThreading->hook(memory, renderMemory);
      vramPages(true);
      threadedSystems.push_back(this);
    } else {
      std::erase(threadedSystems, this);
      geThreading->drain();
      renderGE.settleAll();
      auto* renderer = renderGE.renderer;
      u32 threads = renderGE.drawing.threads;
      GeThread::unhook(memory);
      ge.render = nullptr, ge.thread = nullptr;
      geThreading.reset();
      renderGE.log = nullptr;
      renderGE.setRenderer(nullptr), renderGE.setThreads(1);
      renderMemory.unwatchAll();
      vramPages(false);
      ge.setThreads(threads), ge.setRenderer(renderer);
    }
  }

  auto read(u32 size, u32 address) -> u32 override { return memory.read(size, address); }
  auto write(u32 size, u32 address, u32 data) -> void override { memory.write(size, address, data); }

  //Places the code at address with a halt after it, and runs it on the engine picked until it halts.
  auto runProgram(u32 address, const std::vector<u32>& code, bool recompile) -> void {
    for(u32 i = 0; i < code.size(); i++) memory.write(Word, address + i * 4, code[i]);
    memory.write(Word, address + code.size() * 4, halt);
    recompiler.enabled = recompile;
    power(address);
    run(100000);
    if(!scc.halted) std::fprintf(stderr, "program didn't halt\n");
  }
};

//Every threaded test machine's GE thread done with what it was handed (drain()): before a test looks at the render
//GE's own state (a hardware renderer's statistics), which is that thread's while it runs.
inline auto drainThreads() -> void {
  for(auto* s : threadedSystems) s->geThreading->drain();
}

auto memoryTests() -> Tests;
auto loaderTests() -> Tests;
auto kernelTests() -> Tests;
auto callbackTests() -> Tests;
auto powerTests() -> Tests;
auto audioTests() -> Tests;
auto utilityTests() -> Tests;
auto dialogTests() -> Tests;
auto installTests() -> Tests;
auto poolTests() -> Tests;
auto fileTests() -> Tests;
auto geTests() -> Tests;
auto drawTests() -> Tests;
auto draw3dTests() -> Tests;
auto curvesTests() -> Tests;
auto measureTests() -> Tests;
auto measured6Tests() -> Tests;
auto gpuTests() -> Tests;
auto discTests() -> Tests;
auto discFormatTests() -> Tests;
auto discInfoTests() -> Tests;
auto desktopDiscInfoTests() -> Tests;
auto stateTests() -> Tests;
auto cryptoTests() -> Tests;
auto decryptTests() -> Tests;
auto pgdTests() -> Tests;
auto moduleTests() -> Tests;
auto asyncTests() -> Tests;
auto sasTests() -> Tests;
auto messageTests() -> Tests;
auto mutexTests() -> Tests;
auto threadmanTests() -> Tests;
auto spinTests() -> Tests;
auto timerTests() -> Tests;
auto mediaTests() -> Tests;
auto atracTests() -> Tests;
auto mp3Tests() -> Tests;
auto movieTests() -> Tests;
auto psmfTests() -> Tests;
auto fontTests() -> Tests;
auto cccTests() -> Tests;
auto jpegTests() -> Tests;

}
