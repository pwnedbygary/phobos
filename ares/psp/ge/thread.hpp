#pragma once

#include <atomic>
#include <bitset>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ring.hpp"
#include "model.hpp"
#include "../memory/memory.hpp"

//The GE on a thread of its own (option "GE Thread": docs/psp-core.md). The kernel's GE, on the emulation
//thread, runs each list's commands as it always has (the control pass: every command word, JUMP and CALL, SIGNAL,
//FINISH and END, the bounding boxes and the block transfers), and hands what draws to a second GE, the render GE, on
//this thread, as records: the state words in the order they came, with their addresses, and each PRIM, curved surface
//and palette with the bytes it reads copied as of its place in the list. The render GE draws them, with the renderer
//and the drawing threads, into the same memory seen through a view of its own (Memory::view(): its watches and busy
//pages its own), and makes the checks the synchronous GE makes as it reads (drawnFirst(): the drawing over what it
//reads finished or drawn first) where they come, from the addresses handed over: so its finishes come where the
//synchronous GE's do.
//
//The emulation thread waits for it only where it may still change bytes the emulation thread reads (the value rule:
//model.hpp, a model of what each PRIM handed over may change, and what the render GE reports done); where the render
//GE's memory may be busy, the read is handed over too, for its checks there (Read). Where the emulation thread writes
//VRAM, or a page of a texture the render GE may still decode, it writes at once and hands the write over (Store,
//Copy, Transfer), the render GE making the write's checks from its record where the synchronous GE would have made
//them; the old bytes kept first where the render GE may still read them or write them back as of an earlier place
//(versions.hpp), and a write over pixels the drawing threads may still draw, or a texture they may still read, made
//only once the render GE has passed its record: so each of the GE's results is the synchronous GE's.

namespace ares::PlayStationPortable {

struct GE;
struct Memory;
struct GeVersions;

struct GeThread : Memory::Writer {
  //Records: a header, then the kind's own.
  enum class Kind : u32 {
    Words,       //runs of words: how many runs, each its first word's address and count, then the words (their state
                 //taken as the synchronous GE takes them: commands, matrices)
    Primitive,   //PRIM's word, its own address (~0: none, outside a list), the vertex and index addresses, whether the
                 //model took a hardware renderer as drawing it and the renderer as surely taking it, the copies (Copies)
    Patch,       //BEZIER's or SPLINE's word, the same
    Clut,        //CLUT_LOAD's word, its own address, the palette's address, its bytes
    DrawnFirst,  //the kernel's GE's drawnFirst(): an address and size it reads (a bounding box's vertices; bytes it
                 //waits for, the render GE first finishing or drawing what's over them)
    Read,        //a read of VRAM where the render GE's memory may be busy (the CPU's, an HLE function's): its address,
                 //size and whether it's a load, for pointer()'s check (Memory::busyCheck(): a load through VRAM's depth
                 //views waits for the drawing threads alone, not a hardware renderer's pixels)
    Wrote,       //(checking) the emulation thread's write to VRAM: its first and last byte, and its place
    Store,       //the emulation thread's store the GE hears of (Memory::Writer): its address, size and place (cx), for
                 //writeElsewhere()'s checks (Memory::storeChecks())
    Copy,        //its copy or fill: the same, for copyIn()'s (Memory::copyChecks())
    Transfer,    //TRANSFER_START's copy, the kernel GE's: the place of its rows' writes, for transfer()'s checks
                 //(GE::transferChecks())
    RunStart,    //a kick: the render GE defers its primitives into batches
    RunEnd,      //its end: what's batched drawn, a hardware renderer's work submitted (or finished, unguarded)
    State,       //the GE's state put back whole (sceGeRestoreContext's): commands, matrices and their indices (GE::State)
  };
  struct Header { Kind kind; u32 size; u64 serial; };
  //A PRIM's or curved surface's copies: up to two ranges (its indices, its vertices), each its address and size, the
  //bytes after the last
  struct Copies { u32 ranges = 0, address[2] = {}, size[2] = {}; };

  //(made with the thread taking records at once; gone once it has taken them all and been joined)
  GeThread(GE& control, GE& render);
  ~GeThread();

  //(the emulation thread's) Records appended in the stream's order; words wait in a buffer until the next record or
  //a drain. Each returns its place where it's asked for: the record's serial. nextPlace(): the next record's, the
  //words before it handed over first (a PRIM's, which the model takes before its record goes).
  auto word(u32 word, u32 address) -> void;
  auto nextPlace() -> u64 { flushWords(); return serial + 1; }
  auto primitive(u32 word, u32 wordAddress, u32 vertexAddress, u32 indexAddress, bool hardware, bool certain,
                 const Copies& copies, const u8* bytes) -> u64;
  auto clut(u32 word, u32 wordAddress, u32 address, const u8* bytes, u32 size) -> void;
  auto drawnFirst(u32 address, u32 size) -> u64;
  auto read(u32 address, u32 size, bool loading) -> u64;
  auto write(u32 address, u32 size, Kind kind, u64 cx) -> u64;
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

  //(the emulation thread's) The value rule (model.hpp). model: what the PRIMs handed over may change; hardware:
  //whether the render GE draws with a hardware renderer (its begin() taking PRIMs), as far as the emulation thread
  //knows. A load from VRAM (Memory::reading), the control pass's reads of the list, a palette and vertices; and the
  //emulation thread's own writes, the places they were made at (cx).
  GeModel model;
  bool hardware = false;
  u64 waits = 0;  //(how often it waited for the render GE to pass a read: the tests')
  auto place() const -> u64 { return serial; }
  //(the emulation thread's memory hooked up to the thread, as the system's machine and the tests' both have it: its
  //drawer drains the thread, its loads from VRAM go by the value rule, its writes to VRAM and to watched pages are
  //handed over (writer) and are the model's; unhooked as the thread goes)
  auto hook(Memory& memory, Memory& renderMemory) -> void;
  static auto unhook(Memory& memory) -> void;
  //(the render GE's renderer as the model takes it, read with the thread drained: as it starts, and when it changes)
  auto rendererChanged() -> void;
  auto load(u32 address, u32 size, bool loading) -> void;
  auto listWord(u32 address) -> bool;  //(whether its page is all clean: else the word, waited for where it's dirty)
  auto readClean(u32 address, u32 size) -> void;  //(bytes the control pass reads after the render GE's drawnFirst())
  auto controlRead(u32 address, u32 size, u32 first, u32 last) -> void;  //(the control pass's own drawnFirst())
  auto dirty(u32 first, u32 last) -> bool;  //(VRAM's bytes, offsets: the reports taken first)
  auto waitFor(u64 at) -> void;  //(until the render GE has done the record at that place: its reports taken)
  auto wroteVRAM(u32 first, u32 last) -> void;
  auto takeReports() -> void;
  //(the render GE's) What it reports done: the place of its last finish known as one before the record it made it
  //in (every PRIM before that record's pixels in memory), of the last record whose software pixels are all drawn, of
  //its depth put back over 16 KiBs (likewise), and whether its hardware renderer was lost.
  auto reportFinish(u64 place) -> void { finishReported.store(place ? place - 1 : 0, std::memory_order_release); }
  auto reportPuts(const std::bitset<GeModel::Blocks>& blocks, u64 place) -> void;
  auto reportLost() -> void { lostReported.store(true, std::memory_order_release); }
  //Checking (the tests, PHOBOS_GE_CHECK): the render GE keeps the emulation thread's model itself, from the same
  //records in the same order (the emulation thread's writes to VRAM handed over too), and at every byte a finish or a
  //depth put-back changes, asserts it's dirty there, every finish and put-back before it known: a byte changing that
  //the emulation thread may have read clean fails loudly.
  bool checking = false;
  GeModel replica;
  auto checkChanged(u32 offset) -> void;
  //(the tests': called on the GE thread before it does each record, with its place: one that waits holds the thread
  //back there)
  std::function<auto (u64 place) -> void> gate;
  //Schedule fuzzing (the tests' and the runner's: PHOBOS_GE_FUZZ=seed,mode,n; DESIGN.md section 9), which changes how
  //far apart the two threads run and never a result: delay, the GE thread pausing up to n microseconds (seeded) before
  //one record in four; behind, the GE thread holding each record back till the emulation thread has appended up to n
  //more (seeded), or waits for it, or has filled a quarter of the ring, or 2 ms have gone by; ahead, the emulation
  //thread waiting for the GE thread to take each record it appends (but where a test holds that thread back: gate).
  enum class Fuzz : u32 { None, Delay, Behind, Ahead };
  Fuzz fuzz = Fuzz::None;
  u32 fuzzMost = 0;

  //(the emulation thread's) Its writes the GE hears of (Memory::Writer): before one, where it lands on pixels the
  //drawing threads may still draw or a texture a PRIM drawn by the software renderer may still read (a conflict), its
  //record goes first and the write waits for the render GE to pass it; else, where the render GE may still read or
  //write back the bytes at an earlier place (it hasn't passed every record), their old contents are kept first
  //(versions: with the place of the write's record, 2 per record); after it, its record (where it hasn't gone).
  auto before(u32 address, u32 size, Memory::Writer::Kind kind) -> void override;
  auto after(u32 address, u32 size, Memory::Writer::Kind kind) -> void override;
  auto conflicts(u32 address, u32 size) -> bool;
  auto dirtyRange(u32 address, u32 size) -> bool;  //(the value rule over any of memory's bytes: VRAM's)
  auto behind() const -> bool { return done.load(std::memory_order_acquire) < serial; }
  auto gpuBusy(u32 address, u32 size) -> bool;  //(a write's bytes on pages the GPU may have drawn in since a finish)
  auto save(u32 address, u32 size, u64 at) -> void;  //(versions of the bytes, at a version place)
  auto transfer(u64 cx) -> u64;                     //(the record of TRANSFER_START's copy)
  auto wrote(u64 at) -> void;
  u64 writePlace = 0;  //(the record of the write under way, where it went first)
  //(each page's newest PRIM handed over whose texture is in it, as Memory::watched numbers pages: a store there unwatches
  //it, as ever, but while the render GE hasn't passed that PRIM, which may still decode from it, it's watched again,
  //so that later stores go on being heard of, their old bytes kept: protect())
  std::vector<u64> textureUntil = std::vector<u64>(512_MiB / Memory::PageSize, 0);
  auto protect(u32 address, u32 size) -> void;
  std::vector<std::pair<u32, u32>> written;  //(checking: the VRAM bytes of the write under way, for its record)

  GE& control;
  GE& render;
  //(the versions of memory the emulation thread keeps for the render GE's reads and write-backs: versions.hpp)
  std::unique_ptr<GeVersions> versions;

private:
  static constexpr u32 WordsMost = 1024;
  static constexpr u32 RunsKept = 4096;  //(runs handed over and not yet ended: the emulation thread waits past that)
  auto append(Kind kind, u32 size) -> u8*;
  auto publish() -> void;  //(the record appended, handed over: ring.publish(), and the fuzzing's)
  auto flushWords() -> void;
  auto take() -> void;  //the thread's loop
  auto replay(const Header& header, const u8* data) -> void;
  auto listWordDrawn(u32 address) -> void;  //(the render GE: run()'s drawnFirst() for a list word, replayed)

  RecordRing ring;
  std::vector<u32> words;
  std::vector<std::pair<u32, u32>> wordRuns;  //(the emulation thread's: each run's first address and count)
  u32 undrawn = ~0u;  //(the render GE's: run()'s page found with nothing drawn over it, as run() keeps it)
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
  //(reports: the GE thread's, taken by the emulation thread; puts counts the put-backs, so that the 128 places are
  //taken only where one was made)
  alignas(64) std::atomic<u64> finishReported{0}, drawnReported{0}, puts{0};
  std::atomic<u64> putReported[GeModel::Blocks] = {};
  std::atomic<bool> lostReported{false}, uncertain{false};
  u64 putsTaken = 0;     //(the emulation thread's)
  //(the fuzzing's: the records published, the emulation thread's waits for the GE thread under way, the seeded
  //sequence the GE thread draws from)
  alignas(64) std::atomic<u64> appended{0};
  std::atomic<u32> blocked{0};
  u64 fuzzState = 0;
  auto fuzzBefore(u64 place) -> void;
  auto fuzzNext() -> u64;
  struct Blocked {
    std::atomic<u32>& count;
    explicit Blocked(std::atomic<u32>& count) : count(count) { count.fetch_add(1, std::memory_order_acq_rel); }
    ~Blocked() { count.fetch_sub(1, std::memory_order_acq_rel); }
  };
  u32 dirtyPage = ~0u;  //(the emulation thread's: a list page found not all clean in this run, its words looked at)
};

}
