#pragma once

#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

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
    Stores,      //its stores one after another, no other record between them: their place (cx), how many, then each
                 //one's address, size and (checking) the VRAM bytes it wrote, a count and each first and last, for
                 //their checks in turn
    Copy,        //its copy or fill: the same, for copyIn()'s (Memory::copyChecks())
    Transfer,    //TRANSFER_START's copy, the kernel GE's: the place of its rows' writes, for transfer()'s checks
                 //(GE::transferChecks())
    RunStart,    //a kick: the render GE defers its primitives into batches; with the PSP's timing, the GE's pipeline
                 //moved on to the kick (Kernel::geResume()): whether it was, and by how many clocks
    RunEnd,      //its end: what's batched drawn, a hardware renderer's work submitted (or finished, unguarded); the
                 //run's words, the front end's last work (GE::pipeStop())
    Owe,         //with the PSP's timing, a list starting: its own clocks, the pipeline begun afresh (Kernel::geOwe())
    State,       //the GE's state put back whole (sceGeRestoreContext's): commands, matrices and their indices (GE::State)
    Frame,       //a frame's end (System::run()): what the screen shows then (Frame), its picture taken here (frameEnd)
  };
  struct Header { Kind kind; u32 size; u64 serial; };
  //A PRIM's or curved surface's copies: up to two ranges (its indices, its vertices), each its address and size, the
  //bytes after the last
  struct Copies { u32 ranges = 0, address[2] = {}, size[2] = {}; };

  //(made with the thread taking records at once; gone once it has taken them all and been joined)
  GeThread(GE& control, GE& render);
  ~GeThread();

  //(the emulation thread's) Records appended in the stream's order; words and stores wait in buffers until the next
  //record, a wait or a drain. Each returns its place where it's asked for: the record's serial. nextPlace(): the next
  //record's, the words and stores before it handed over first (a PRIM's, which the model takes before its record goes).
  //(a list word as run() read it, at its address, or ~0 for one from no list (the kernel's): runs of words one after
  //another kept as a run. In the list's loop for every word: Super Monkey Ball's spent a fourteenth of the emulation
  //thread there, called and growing vectors)
  auto word(u32 word, u32 address) -> void {
    if(wordCount == WordsMost) flushWords();
    if(address == ~0u || address != nextWordAt) wordRuns[runCount++] = {address, 0};
    wordRuns[runCount - 1].count++;
    nextWordAt = address == ~0u ? ~0u : (address + 4) & 0x0fff'ffff;
    words[wordCount++] = word;
  }
  //(with the PSP's timing, the control pass's own work for the GE's front end, which the render GE's pipeline takes
  //after the word it came with, the last handed over: a JUMP taken, a CALL, a RET, GE::pipeFront(); vertices, a
  //bounding box's, to be shared out among the next PRIM's primitives, GE::oweVertices())
  auto front(f64 clocks, bool vertices = false) -> void {
    marks.push_back({(wordCount - 1) | (vertices ? VerticesMark : 0), clocks});
  }
  static constexpr u32 VerticesMark = 0x8000'0000;  //(a mark's word's top bit: vertices' work)
  //(with the PSP's timing, the run about to start: the GE's pipeline moved on to it by clocks, Kernel::geResume())
  auto resume(f64 clocks) -> void { shifting = true, shift = clocks; }
  //(with the PSP's timing, a list starting: its own clocks, the pipeline begun afresh, Kernel::geOwe())
  auto owe(f64 clocks) -> void;
  auto nextPlace() -> u64 { flushStores(), flushWords(); return serial + 1; }
  //A PRIM's or curved surface's record, made where it goes, with the words waiting before it in it: its place
  //(primitivePlace(), given its copies' size), then the record begun (primitiveBegin(): where its copies' bytes go),
  //and handed over (primitiveEnd()). Where each PRIM's bytes were copied once to be copied into its record, and its
  //words went as a record of their own. One too big for the ring has its words go first as their own, and the
  //emulation thread draws it, once the thread has drained (primitiveBegin() nullptr; primitiveEnd() given the bytes).
  auto primitivePlace(u32 copied) -> u64;
  auto primitiveBegin(u32 word, u32 wordAddress, u32 vertexAddress, u32 indexAddress, bool hardware,
                      const Copies& copies) -> u8*;
  auto primitiveEnd(bool certain, const u8* bytes) -> void;
  auto clut(u32 word, u32 wordAddress, u32 address, const u8* bytes, u32 size) -> void;
  auto drawnFirst(u32 address, u32 size) -> u64;
  auto read(u32 address, u32 size, bool loading) -> u64;
  auto write(u32 address, u32 size, Kind kind, u64 cx) -> u64;
  auto runStart() -> void;
  auto runEnd(u64 words) -> void;  //(words: the run's, run()'s count)
  auto state(const GE& from) -> void;
  auto note(const std::string& text) -> void { notes.push_back(text); }  //(the render GE's, told as it drains)
  //A frame's end: the frame buffer the screen shows then and how it's laid out, as the emulation thread has the
  //display (Kernel::showing()), and the frame's number. The GE thread takes its picture as it comes to the record,
  //everything handed over before it done (frameEnd: System's), so the picture is the frame's, as of its place, however
  //far the emulation thread has run on; the emulation thread waits only where more than most frames handed over
  //haven't ended.
  struct Frame { u64 number; u32 frameBuffer, bufferWidth, pixelFormat, width, height; };
  auto frame(const Frame& frame) -> void;
  std::function<auto (const Frame& frame) -> void> frameEnd;
  auto framesBehind() const -> u64 { return framesHanded - framesEnded.load(std::memory_order_acquire); }
  auto waitFrames(u64 most) -> void;
  //(the GE thread's, at a frame's record, its frame buffer wholly inside a copy of VRAM that keeps its bytes in order)
  //The frame buffer's rows reached first as a picture's reads reach them with the GE on the emulation thread
  //(pointer(): the drawing over them finished or drawn first), then use(vram) given VRAM as it was at the record (the
  //emulation thread may have written it since: versions.hpp), the frame buffer's bytes at their offsets in it.
  auto frameBytes(const Frame& frame, const std::function<auto (const u8* vram) -> void>& use) -> void;
  //What each run's primitives owe a PSP's GE (GE::owedPixels, owedPrimitives, which Kernel::geTime() folds), runs
  //numbered from 1 as they're handed over (runStart()): published by the GE thread as each ends, and the one under
  //way's counts so far after each of its PRIMs, for the emulation thread's clock (kernel/ge.cpp). With the PSP's timing,
  //its pipeline's clocks too, from when its times count (Kernel::gePipeAt): as the run ends, all of its work's
  //(GE::pipeTime()) and its front end's (Pipe::front), which the kernel takes as Kernel::geTime() does; so far, the two
  //ends' (the larger, and the front end's), which only grow as primitives join the pipeline, so that the times they
  //give are no later than the run's will be.
  //(and, with the CPU's work beside the GE counted, GE::Cost::Contended, the list's clocks so far that read RAM by
  //kind, and all of them, as the run ends: GE::ramClocks, allClocks, for Kernel::geContend())
  struct Owed { u64 pixels = 0, primitives = 0; f64 time = 0, front = 0; f64 ramClocks[4] = {}, allClocks = 0; };
  //(the runs whose counts it keeps: the emulation thread folds the oldest before it hands over more than that many
  //not folded, Kernel::geTime(), and waits past that many not ended)
  static constexpr u32 RunsKept = 4096;
  auto runsHanded() const -> u64 { return runs; }
  auto owedOf(u64 run, bool wait, Owed& owed) -> bool;  //(once it has ended; with wait, waiting for that)
  auto owedSoFar(u64 run, Owed& owed) const -> bool;    //(while it's under way)
  //Every record so far taken and done by the render GE, which then waits for the next: until the emulation thread
  //appends one, it may reach the render GE and its memory itself. drain() waits as long as that takes; drainBy()
  //gives up at the deadline, false where the thread hasn't done them all by then (it may be stuck in one: a driver call
  //that never returns, System::unload()).
  auto drain() -> void;
  auto drainBy(std::chrono::steady_clock::time_point deadline) -> bool;
  auto drained() const -> bool { return done.load(std::memory_order_acquire) == serial; }
  //(a thread drainBy() gave up on) Told to take no record after the one it's in: left be, never waited for again,
  //and joined once it has come back from that one (exited()), its object kept till then (it may still use it).
  auto abandon() -> void;
  auto exited() const -> bool { return left.load(std::memory_order_acquire); }

  //(the emulation thread's) The value rule (model.hpp). model: what the PRIMs handed over may change; hardware:
  //whether the render GE draws with a hardware renderer (its begin() taking PRIMs), as far as the emulation thread
  //knows. A load from VRAM (Memory::reading), the control pass's reads of the list, a palette and vertices; and the
  //emulation thread's own writes, the places they were made at (cx).
  GeModel model;
  bool hardware = false;
  u64 waits = 0;  //(how often it waited for the render GE to pass a read: the tests')
  u64 reportWork = 0;  //(the entries taking the reports went through: the tests')
  auto backlog() const -> u64 { return ring.backlog(); }  //(the bytes of records handed over not yet taken: the tests')
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
  //(versions: with the place of the write's record, 2 per record); after it, its record (where it hasn't gone): a
  //store's joins the stores waiting, which go as one record (Stores) at the place of the next. Hot Wheels's CPU stored
  //59,000 words a frame into VRAM, each a record of its own the GE thread woke for.
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
  //so that later stores go on being heard of, their old bytes kept: protect(). The latest texture's pages are kept
  //apart, a range and its newest PRIM, put into each page's as another texture comes: Super Monkey Ball's PRIMs, one
  //texture after another, each went through all its texture's pages)
  //(the texture the last PRIM handed over read, as the words it's from have it: format, address, row width, size,
  //mode; its bytes and their pages, worked out again only where those words have changed. The thread's, so that each
  //machine's thread starts with none)
  struct HandTexture {
    u32 words[5] = {};
    u32 low = 0, high = 0, first = 1, last = 0;
    bool known = false, paged = false;
  };
  HandTexture handTexture;
  std::vector<u64> textureUntil = std::vector<u64>(512_MiB / Memory::PageSize, 0);
  u32 textureFirst = 1, textureLast = 0;
  u64 textureAt = 0;
  auto textureRead(u32 first, u32 last, u64 at) -> void;
  auto textureNewest(u32 page) const -> u64 {
    return page >= textureFirst && page <= textureLast ? std::max(textureUntil[page], textureAt) : textureUntil[page];
  }
  auto protect(u32 address, u32 size) -> void;
  std::vector<std::pair<u32, u32>> written;  //(checking: the VRAM bytes of the write under way, for its record)

  GE& control;
  GE& render;
  //(the versions of memory the emulation thread keeps for the render GE's reads and write-backs: versions.hpp)
  std::unique_ptr<GeVersions> versions;

private:
  static constexpr u32 WordsMost = 1024, StoresMost = 16384;
  auto append(Kind kind, u32 size) -> u8*;  //(the stores waiting handed over first)
  auto publish() -> void;  //(the record appended, handed over: ring.publish(), and the fuzzing's)
  auto flushWords() -> void;
  auto flushStores() -> void;
  auto addStore(u32 address, u32 size) -> void;
  auto wordsBytes() const -> u32;  //(the words waiting, as a record holds them: their runs, the words, the marks)
  auto putWords(u8* at) -> void;   //(and put there, none left waiting)
  auto take() -> void;  //the thread's loop
  auto replay(const Header& header, const u8* data) -> void;
  auto replayWords(const u8* data) -> void;
  auto listWordDrawn(u32 address) -> void;  //(the render GE: run()'s drawnFirst() for a list word, replayed)
  //(the PRIM record under way, between primitiveBegin() and primitiveEnd(): its data, or nullptr with what it says,
  //for the emulation thread to draw it)
  struct Building { u32 word, wordAddress, vertexAddress, indexAddress; bool hardware; Copies copies; };
  u8* building = nullptr;
  Building built{};

  RecordRing ring;
  //(the emulation thread's: the words, and each run's first address and count; where the last run goes on, or ~0)
  struct WordRun { u32 first, count; };
  std::array<u32, WordsMost> words;
  std::array<WordRun, WordsMost> wordRuns;
  u32 wordCount = 0, runCount = 0, nextWordAt = ~0u;
  //(and the control pass's front-end work among them: after which word, and its clocks; the run about to start's
  //shift, with the PSP's timing: front(), resume())
  struct Mark { u32 word; f64 clocks; };
  std::vector<Mark> marks;
  bool shifting = false;
  f64 shift = 0;
  u64 runWords = 0;  //(the render GE's: the run's words so far, as run() counts them, for its pipeline: GE::wordsRun)
  //(the emulation thread's: the stores waiting, as their record holds them after its count, and how many; the place
  //the versions were last freed as of)
  std::vector<u32> stores;
  u32 storeCount = 0;
  u64 freedAt = 0;
  u32 undrawn = ~0u;  //(the render GE's: run()'s page found with nothing drawn over it, as run() keeps it)
  std::vector<std::string> notes;
  u64 serial = 0;                     //(the emulation thread's) records appended
  alignas(64) std::atomic<u64> done{0};  //(the GE thread's) records taken and done
  std::atomic<bool> abandoned{false}, left{false};  //(abandon(); the GE thread's loop ended)
  auto afterDrain() -> void;  //(drain()'s and drainBy()'s once it has done them all)
  std::thread thread;
  //(runs: those handed over, the emulation thread's; ended, the GE thread's count of those done, each run's counts kept
  //at its number's place in owedRuns from then on; the run under way's counts so far, its number and a version that's
  //odd while they're written)
  u64 runs = 0, takenRuns = 0;
  Owed owedRuns[RunsKept];
  alignas(64) std::atomic<u64> runsEnded{0};
  u64 framesHanded = 0;                     //(the emulation thread's)
  alignas(64) std::atomic<u64> framesEnded{0};  //(the GE thread's)
  alignas(64) std::atomic<u64> soFarVersion{0};
  std::atomic<u64> soFarRun{0}, soFarPixels{0}, soFarPrimitives{0};
  std::atomic<f64> soFarTime{0}, soFarFront{0};
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
  //(and pages found all clean, with what would have to change for one not to be: its pages' newest rectangles, the
  //model's other dirtyings; reports taken and the emulation thread's writes since only make it cleaner. The model may
  //answer coarser later, rectangles painted into pieces or an older part's bytes given the whole's place, but the
  //bytes are as clean as they were. Kept by page number, 64 of them: a list's calls jump from page to page)
  struct CleanPage {
    u32 page = ~0u;
    u64 gpu = 0, software = 0, dirtyings = 0;
    bool staleDepth = false, scaled = false;
    auto operator==(const CleanPage&) const -> bool = default;
  };
  std::array<CleanPage, 64> cleanPages;
  CleanPage cleanPiece;  //(the last 32-byte piece of a list word found clean on a page not all clean)
};

}
