#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "ring.hpp"

//The GE on a thread of its own (option "GE Thread": docs/psp-core.md). The kernel's GE, on the emulation
//thread, runs each list's commands as it always has (the control pass: every command word, JUMP and CALL, SIGNAL,
//FINISH and END, the bounding boxes and the block transfers), and hands what draws to a second GE, the render GE, on
//this thread, as records: the state words in the order they came, and each PRIM, curved surface and palette with the
//bytes it reads copied as of its place in the list. The render GE draws them, with the renderer and the drawing
//threads, into the same memory seen through a view of its own (Memory::view(): its watches and busy pages its own).
//
//Where the emulation thread reaches memory the render GE may still draw or decode from (VRAM, and pages of textures it
//may decode), it waits for the render GE to take every record so far (drain()), then has the render GE's own checks
//made as the synchronous GE would have made them there (finishes, the drawing threads' batches): so the two run one
//after another wherever they meet, and each of the GE's results is the synchronous GE's.

namespace ares::PlayStationPortable {

struct GE;
struct Memory;
struct GeVersions;

struct GeThread {
  //Records: a header, then the kind's own.
  enum class Kind : u32 {
    Words,     //count, the words (their state taken as the synchronous GE takes them: commands, matrices)
    Primitive, //PRIM's word, the vertex and index addresses, the copies (Copies)
    Patch,     //BEZIER's or SPLINE's word, the same
    Clut,      //CLUT_LOAD's word, its address, the palette's bytes
    RunStart,  //a kick: the render GE defers its primitives into batches
    RunEnd,    //its end: what's batched drawn, a hardware renderer's work submitted (or finished, unguarded)
    State,     //the GE's state put back whole (sceGeRestoreContext's): commands, matrices and their indices (GE::State)
  };
  struct Header { Kind kind; u32 size; u64 serial; };
  //A PRIM's or curved surface's copies: up to two ranges (its indices, its vertices), each its address and size, the
  //bytes after the last
  struct Copies { u32 ranges = 0, address[2] = {}, size[2] = {}; };

  //(made with the thread taking records at once; gone once it has taken them all and been joined)
  GeThread(GE& control, GE& render);
  ~GeThread();

  //(the emulation thread's) Records appended in the stream's order; words wait in a buffer until the next record or
  //a drain.
  auto word(u32 word) -> void { if(words.size() == WordsMost) flushWords(); words.push_back(word); }
  auto primitive(u32 word, u32 vertexAddress, u32 indexAddress, const Copies& copies, const u8* bytes) -> void;
  auto patch(u32 word, u32 vertexAddress, u32 indexAddress, const Copies& copies, const u8* bytes) -> void;
  auto clut(u32 word, u32 address, const u8* bytes, u32 size) -> void;
  auto runStart() -> void;
  auto runEnd() -> void;
  auto state(const GE& from) -> void;
  auto note(const std::string& text) -> void { notes.push_back(text); }  //(the render GE's, told as it drains)
  //What each run's primitives owe a PSP's GE (GE::owedPixels, owedPrimitives, which Kernel::geTime() folds), runs
  //numbered from 1 as they're handed over (runStart()): published by the GE thread as each ends, and the one under
  //way's counts so far after each of its PRIMs, for the emulation thread's clock (kernel/ge.cpp).
  struct Owed { u64 pixels = 0, primitives = 0; };
  auto runsHanded() const -> u64 { return runs; }
  auto owedOf(u64 run, bool wait, Owed& owed) -> bool;  //(once it has ended; with wait, waiting for that)
  auto owedSoFar(u64 run, Owed& owed) const -> bool;    //(while it's under way)
  //Every record so far taken and done by the render GE, which then waits for the next: until the emulation thread
  //appends one, it may reach the render GE and its memory itself.
  auto drain() -> void;
  auto drained() const -> bool { return done.load(std::memory_order_acquire) == serial; }

  GE& control;
  GE& render;
  //(the versions of memory the emulation thread keeps for the render GE's reads and write-backs: versions.hpp)
  std::unique_ptr<GeVersions> versions;

private:
  static constexpr u32 WordsMost = 1024;
  static constexpr u32 RunsKept = 4096;  //(runs handed over and not yet ended: the emulation thread waits past that)
  auto append(Kind kind, u32 size) -> u8*;
  auto flushWords() -> void;
  auto take() -> void;  //the thread's loop
  auto replay(const Header& header, const u8* data) -> void;

  RecordRing ring;
  std::vector<u32> words;
  //(the render GE's watches and changes since the last drain, which the kernel's GE's memory hears of then)
  std::vector<u32> watches;
  std::vector<std::pair<u32, u32>> changes;
  std::vector<std::string> notes;
  u64 serial = 0;                     //(the emulation thread's) records appended
  alignas(64) std::atomic<u64> done{0};  //(the GE thread's) records taken and done
  std::thread thread;
  //(runs: those handed over, the emulation thread's; ended, the GE thread's count of those done, each run's counts kept
  //at its number's place in owedRuns from then on; the run under way's counts so far, its number and a version that's
  //odd while they're written)
  u64 runs = 0, takenRuns = 0;
  Owed owedRuns[RunsKept];
  alignas(64) std::atomic<u64> runsEnded{0};
  alignas(64) std::atomic<u64> soFarVersion{0};
  std::atomic<u64> soFarRun{0}, soFarPixels{0}, soFarPrimitives{0};
  auto publishSoFar() -> void;
};

}
