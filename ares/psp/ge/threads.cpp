//Drawing on several threads.
//
//Most of the GE's time goes into drawing pixels, one after another. But no pixel of a primitive depends on another
//pixel of it (draw.cpp works each out from the primitive's setup alone), so the screen can be cut into bands of rows
//and each band drawn by a different thread at the same time. Inside a band the primitives are drawn in the order
//the display list gave them, as the PSP draws them, and a pixel is only ever drawn by the band it's in: so every
//pixel comes out exactly as drawing the primitives one after another, on one thread, makes it.
//
//How: while the GE runs a list (run()), a primitive it meets isn't drawn there and then. It's set up (its jobs) and
//waits in a batch. When the batch is done (the list stops, or the render target changes), it's handed to the workers:
//the rows its jobs cover are cut into bands of BandRows rows, and each worker takes the next band nobody has taken
//and draws every job that reaches it, in order (each band keeps a list of them as they're recorded: a batch can hold
//tens of thousands), until none is left. Meanwhile the GE's thread goes on: setting up the next batch, the next of
//its Batches in turn (which the workers start on only once those before it are done, so batches are drawn in order
//too), or, the list stopped, running the CPU. So drawing overlaps the setting up of what comes next and the program's
//own work. The GE's thread waits only to fill a batch again that's still being drawn, when every other one is
//launched too.
//
//Nobody may see a batch half drawn, so it's waited for ("settled", the GE's thread drawing bands too) first:
//  - by anyone touching VRAM it draws over while the CPU runs on: Memory::pointer(), behind every read, write and
//    copy (the CPU's, an HLE function's, the screen's picture), save states and power (memory.hpp); the CPU's
//    compiled loads and stores reach VRAM through its page table, so the CPU's owner takes those pages out of it
//    meanwhile (Memory::vramGuard; without one, a list's batches are all drawn before run() returns);
//  - by the next list, only the batch it's to fill (resume()), and by anything the GE's own thread reads from VRAM a
//    batch draws over: a palette, vertices, the list's commands (drawnFirst()); a texture to decode, unless it's
//    deferred (below);
//  - by a block transfer (it reads and writes memory, maybe drawn pixels), but only the batches drawing over what it
//    reads or writes, or decoding a texture from what it writes, and those before them: the batch being filled too,
//    if it's one (drawnBefore());
//  - by a primitive that must be drawn by itself, at once, in order (texture.cpp reads its texels from memory as it
//    draws: one drawing over its own texture, say), or whose pixels in different rows share bytes (defer()).
//Render to texture: a primitive sampling what the batch being filled (or one already launched) still draws is not
//decoded on the GE's thread (that would wait for those pixels). The source batch is launched if it's the one being
//filled; the Look waits in the next batch with its decode deferred; ensureDecoded() runs as that batch starts
//being drawn, when every batch before it is done, decoding for that batch alone. Till then the CPU waits for the
//texture's pages (the batch's reads), as for the pages a batch draws over.
//While they're drawn, jobs only read what doesn't change: their own setup and settings, decoded textures (which the
//batch keeps), and VRAM's frame and depth buffers, each pixel by its own band alone.
//
//With 'GE Threads' at 1 nothing waits: each primitive is drawn at once on the GE's thread, as it always was. A batch
//with fewer pixels than drawing.shared, and nothing being drawn, is drawn on the GE's thread at once, which is
//quicker than waking the others.

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

GE::~GE() {
  //The machine is going: drawing still going on is finished, but its owner's page table may be gone already, so
  //nothing is put back into it; and memory, which outlives the GE, hears from it no more.
  memory.vramGuard = nullptr;
  setThreads(1);
  memory.finishDrawing = nullptr;
  memory.finishDrawingOver = nullptr;
  memory.vramDrawnOver = nullptr;
  memory.vramChangedBusy = nullptr;
  memory.watchedWritten = nullptr;
}

//count threads draw from now on: the GE's own and count - 1 workers.
auto GE::setThreads(u32 count) -> void {
  count = std::max(1u, count);
  if(count == drawing.threads && drawing.workers.size() == count - 1) return;
  flush();
  {
    std::lock_guard lock(drawing.mutex);
    drawing.quit = true;
  }
  drawing.wake.notify_all();
  for(auto& worker : drawing.workers) worker.join();
  drawing.workers.clear();
  drawing.quit = false;
  drawing.threads = count;
  for(u32 n = 1; n < count; n++) drawing.workers.emplace_back([this] { worker(); });
}

//A worker: it waits for a batch to be drawn, draws bands of it, and waits again.
auto GE::worker() -> void {
  #if defined(__linux__)
  //A thread starts on the cores its maker keeps to, and the emulator's thread may keep to one (Phobos pins it to the
  //fastest core on Android): a worker may run on any.
  cpu_set_t cores;
  CPU_ZERO(&cores);
  for(long core = 0; core < sysconf(_SC_NPROCESSORS_CONF) && core < CPU_SETSIZE; core++) CPU_SET(core, &cores);
  sched_setaffinity(0, sizeof(cores), &cores);
  #endif
  u64 seen;
  {
    std::lock_guard lock(drawing.mutex);
    seen = drawing.round;
  }
  while(true) {
    Batch* batch;
    {
      std::unique_lock lock(drawing.mutex);
      drawing.wake.wait(lock, [&] { return drawing.quit || (drawing.drawn && drawing.round != seen); });
      if(drawing.quit) return;
      seen = drawing.round;
      batch = drawing.drawn;
      batch->users++;
    }
    drawBands(*batch);
    bool started;
    {
      std::lock_guard lock(drawing.mutex);
      started = drew(*batch);
    }
    if(started) drawing.wake.notify_all();
    drawing.finished.notify_all();
  }
}

//Takes bands of the batch until none are left, drawing in each every job that reaches it, in order. Deferred
//textures (render to texture) are decoded once first, outside drawing.mutex: every batch before this one is drawn,
//and reading their pages must not settle under that mutex.
auto GE::drawBands(Batch& batch) -> void {
  u32 state = batch.decodeState.load(std::memory_order_acquire);
  if(state != 2) {
    if(state == 0 && batch.decodeState.compare_exchange_strong(state, 1, std::memory_order_acq_rel)) {
      ensureDecoded(batch);
      batch.readsDone.store(true, std::memory_order_release);
      batch.decodeState.store(2, std::memory_order_release);
    } else {
      while(batch.decodeState.load(std::memory_order_acquire) != 2) std::this_thread::yield();
    }
  }
  while(true) {
    u32 band = batch.nextBand.fetch_add(1, std::memory_order_relaxed);
    if(band >= batch.bands) return;
    s32 at = batch.top / BandRows + s32(band);
    s32 top = std::max(at * BandRows, batch.top), bottom = std::min(at * BandRows + BandRows - 1, batch.bottom);
    for(u32 job : batch.bandJobs[at]) rasterize(batch.jobs[job], top, bottom);
    batch.bandsLeft.fetch_sub(1, std::memory_order_acq_rel);
  }
}

//The batch's bands to draw, and the workers due to wake for them (the caller holds the mutex, and wakes them).
auto GE::startBands(Batch& batch) -> void {
  bool defer = false;
  for(auto& look : batch.looks) {
    if(look.deferRows) { defer = true; break; }
  }
  batch.decodeState.store(defer ? 0u : 2u, std::memory_order_relaxed);
  batch.bands = batch.bottom / BandRows - batch.top / BandRows + 1;
  batch.nextBand.store(0, std::memory_order_relaxed);
  batch.bandsLeft.store(batch.bands, std::memory_order_relaxed);
  drawing.drawn = &batch;
  drawing.round++;
}

//A thread done drawing bands of the batch (holding the mutex): if that finished its last, it's done, and the one
//queued next starts. Returns whether one did (the workers are then to be woken).
auto GE::drew(Batch& batch) -> bool {
  batch.users--;
  if(drawing.drawn != &batch || batch.bandsLeft.load(std::memory_order_acquire)) return false;
  batch.finished.store(true, std::memory_order_release);  //(after every band's pixels: bandsLeft's last fetch_sub)
  drawing.drawn = nullptr;
  if(drawing.queued.empty()) return false;
  Batch* next = drawing.queued.front();
  drawing.queued.pop_front();
  startBands(*next);
  return true;
}

//Whether any batch is launched: being drawn, queued, or drawn but not yet emptied.
auto GE::launched() const -> bool {
  for(auto& batch : drawing.batches) {
    if(batch.launched) return true;
  }
  return false;
}

//Whether a batch is launched and not yet all drawn: its pixels may still change. (One all drawn stays launched, its
//pages busy, until it's emptied: reap(), reclaim(), settle().)
auto GE::unfinished(const Batch& batch) -> bool {
  return batch.launched && !batch.finished.load(std::memory_order_acquire);
}

//Draws the batch being filled (see the top of this file) and waits for it, after any being drawn; then empties it.
auto GE::flush() -> void {
  settle();
  Batch& batch = *drawing.batch;
  if(batch.jobs.empty()) return clearBatch(batch);
  if(drawing.workers.empty() || batch.work < drawing.shared) {
    ensureDecoded(batch);
    for(auto& job : batch.jobs) rasterize(job, job.firstY, job.lastY);
    return clearBatch(batch);
  }
  {
    std::lock_guard lock(drawing.mutex);
    batch.launched = true;
    startBands(batch);
  }
  drawing.wake.notify_all();
  settle();
}

//The batch being filled is done (returning: the list stops; else its render target changes): it's handed to the
//workers, to be drawn after any before it while the GE's thread goes on, which fills the next batch in turn (once
//that's drawn, if it's still launched: the oldest). Unless there's nothing to share it with, or it's small and
//nothing else is being drawn, or the list stops and the CPU's owner can't keep the CPU off VRAM meanwhile: then it's
//drawn, and waited for.
auto GE::launch(bool returning) -> void {
  Batch& batch = *drawing.batch;
  bool busy = launched();
  if(batch.jobs.empty() && !(returning && busy && !memory.vramGuard)) return clearBatch(batch);
  if(drawing.workers.empty() || (returning && !memory.vramGuard) || (batch.work < drawing.shared && !busy)) {
    return flush();
  }
  {
    std::lock_guard lock(drawing.mutex);
    batch.launched = true;
    if(!drawing.drawn) startBands(batch);  //(nothing being drawn: nothing queued either)
    else drawing.queued.push_back(&batch);
  }
  drawing.wake.notify_all();
  if(memory.vramGuard) {  //(its pages are the workers' until it's settled)
    memory.vramBusy = true;
    for(u32 page = 0; page < VRAMPages; page++) {
      if(batch.pending[page] || batch.reads[page]) memory.busyPages[page >> 6] |= 1ull << (page & 63);
    }
    memory.vramGuard(true);
  }
  Batch& next = drawing.batches[(&batch - drawing.batches + 1) % Batches];
  drawing.batch = &next;
  if(returning || !next.launched) return;
  reclaim(next);  //(the oldest launched, maybe still being drawn: done with before it's filled again)
}

//Waits for a batch handed to the workers to be drawn, the GE's thread drawing bands of it (or of those queued
//before it) too; then empties it, to be filled again. Batches are drawn in order, so any before it is drawn too;
//those queued after it go on being drawn.
auto GE::reclaim(Batch& batch) -> void {
  std::unique_lock lock(drawing.mutex);
  auto queued = [&] { return std::find(drawing.queued.begin(), drawing.queued.end(), &batch) != drawing.queued.end(); };
  while(drawing.drawn == &batch || batch.users || queued()) {
    Batch* drawn = drawing.drawn;
    bool ahead = drawn == &batch || queued();  //(it, or one it waits behind)
    if(drawn && ahead && drawn->nextBand.load(std::memory_order_relaxed) < drawn->bands) {
      drawn->users++;
      lock.unlock();
      drawBands(*drawn);
      lock.lock();
      if(drew(*drawn)) drawing.wake.notify_all();
      continue;
    }
    drawing.finished.wait(lock);
  }
  lock.unlock();
  clearBatch(batch);
}

//A list starts (run()): what the last left being drawn goes on being drawn, all but the batch it's to fill first,
//which is waited for; those all drawn already are emptied (reap()). What's drawn is no different: the new list's
//batches are drawn after the old's, and anything the GE's thread reads from VRAM they draw over is drawn first
//(drawnFirst()), as is a block transfer's. VRAM's busy pages are anyone's again but those a batch still launched
//draws over (or reads deferred textures from), which stay busy till it's emptied (it may be drawn already: whoever
//touches one then reads it as it is, drawnOver()). A hardware renderer shares those busy pages, so with one
//everything is settled, as ever.
auto GE::resume() -> void {
  if(renderer) return settle();
  reap();
  if(!drawing.batch->launched) return;
  reclaim(*drawing.batch);
  if(!launched()) freeVRAM();
}

//The batches all drawn already are emptied (no thread draws them any more, nor will), and VRAM's pages no batch still
//launched draws over, or has yet to read deferred textures from, go back to the CPU's page table, as with nothing
//launched (freeVRAM()).
auto GE::reap() -> void {
  Batch* done[Batches];
  u32 count = 0;
  {
    std::lock_guard lock(drawing.mutex);
    for(auto& batch : drawing.batches) {
      if(batch.launched && batch.finished.load(std::memory_order_acquire) && !batch.users) done[count++] = &batch;
    }
  }
  for(u32 n = 0; n < count; n++) clearBatch(*done[n]);
  if(!launched()) return freeVRAM();
  if(!memory.vramBusy) return;
  u64 kept[VRAMPages / 64] = {}, freed[VRAMPages / 64];
  for(auto& batch : drawing.batches) {
    if(!batch.launched) continue;
    bool reading = !batch.readsDone.load(std::memory_order_acquire);
    for(u32 page = 0; page < VRAMPages; page++) {
      if(batch.pending[page] || (reading && batch.reads[page])) kept[page >> 6] |= 1ull << (page & 63);
    }
  }
  bool any = false;
  for(u32 n = 0; n < VRAMPages / 64; n++) {
    freed[n] = memory.busyPages[n] & ~kept[n], kept[n] &= memory.busyPages[n];
    any |= freed[n] != 0;
  }
  if(!any) return;
  //(the guard puts back the pages marked busy: those freed, then the rest stay marked)
  for(u32 n = 0; n < VRAMPages / 64; n++) memory.busyPages[n] = freed[n];
  if(memory.vramGuard) memory.vramGuard(false);
  for(u32 n = 0; n < VRAMPages / 64; n++) memory.busyPages[n] = kept[n];
}

//Waits for every batch handed to the workers (the GE's thread drawing bands too), and empties them: VRAM is
//anyone's again.
auto GE::settle() -> void {
  if(!launched()) return;
  std::unique_lock lock(drawing.mutex);
  auto users = [&] {
    for(auto& batch : drawing.batches) {
      if(batch.users) return true;
    }
    return false;
  };
  while(drawing.drawn || !drawing.queued.empty() || users()) {
    Batch* batch = drawing.drawn;
    if(batch && batch->nextBand.load(std::memory_order_relaxed) < batch->bands) {
      batch->users++;
      lock.unlock();
      drawBands(*batch);
      lock.lock();
      if(drew(*batch)) drawing.wake.notify_all();
      continue;
    }
    drawing.finished.wait(lock);
  }
  lock.unlock();
  for(auto& batch : drawing.batches) {
    if(batch.launched) clearBatch(batch);
  }
  freeVRAM();
}

//Nothing is being drawn: VRAM's busy pages go back to the CPU's page table.
auto GE::freeVRAM() -> void {
  if(!memory.vramBusy) return;
  memory.vramBusy = false;
  if(memory.vramGuard) memory.vramGuard(false);
  for(auto& pages : memory.busyPages) pages = 0;
}

//Everything drawn put in memory's VRAM: the batches settled, and what a hardware renderer owns put back (ge.hpp's
//Renderer). What a save state and the power wait for (Memory::finishDrawing).
auto GE::settleAll() -> void {
  settle();
  if(renderer) renderer->finish(*this);
}

//What anyone touching VRAM's bytes first to last (offsets in VRAM) on busy pages waits for (Memory::finishDrawingOver):
//the batches still being drawn drawn, in order, up to the last of them that draws over those bytes' pages, or has yet
//to read a deferred texture from them (the waiting thread drawing bands too), while those after it go on being drawn
//and stay launched, their pages busy. Batches are drawn in order, so every one before it is drawn too, and none after
//it reaches those pages. Those drawn are then emptied, their pages given back (reap()). With a hardware renderer,
//everything (settleAll()). It's the emulation thread's (Memory::pointer(), for the CPU and HLE functions).
auto GE::settleOver(u32 first, u32 last) -> void {
  if(renderer) return settleAll();
  bool waited = settleThrough([&](const Batch& batch) {
    bool reading = !batch.readsDone.load(std::memory_order_acquire);
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      if(batch.pending[page] || (reading && batch.reads[page])) return true;
    }
    return false;
  });
  if(!waited) reap();
}

//The batches still being drawn waited for, in order, up to the last of them that reaches what the caller is about to
//touch (reaches(batch)), the waiting thread drawing bands too; those after it go on being drawn and stay launched.
//Batches are drawn in order, so every one before it is drawn too. Those drawn are then emptied (reap()). Returns
//whether any batch reached it (with none, nothing is waited for or emptied).
template<typename Reaches> auto GE::settleThrough(const Reaches& reaches) -> bool {
  std::unique_lock lock(drawing.mutex);
  Batch* target = drawing.drawn && reaches(*drawing.drawn) ? drawing.drawn : nullptr;
  for(auto* batch : drawing.queued) {
    if(reaches(*batch)) target = batch;
  }
  if(!target) return false;
  while(!target->finished.load(std::memory_order_acquire)) {
    Batch* batch = drawing.drawn;
    if(batch && batch->nextBand.load(std::memory_order_relaxed) < batch->bands) {
      batch->users++;
      lock.unlock();
      drawBands(*batch);
      lock.lock();
      if(drew(*batch)) drawing.wake.notify_all();
      continue;
    }
    drawing.finished.wait(lock);
  }
  lock.unlock();
  reap();
  return true;
}

//A batch done with.
auto GE::clearBatch(Batch& batch) -> void {
  if(batch.top <= batch.bottom) {
    for(s32 band = batch.top / BandRows; band <= batch.bottom / BandRows; band++) batch.bandJobs[band].clear();
  }
  batch.jobs.clear();
  batch.looks.clear();
  batch.work = 0;
  batch.top = 0, batch.bottom = -1;
  batch.targeted = false;
  batch.pending.reset();
  batch.reads.reset();
  batch.readsDone.store(false, std::memory_order_relaxed);
  batch.launched = false;
  batch.finished.store(false, std::memory_order_relaxed);
  batch.decodeState.store(2, std::memory_order_relaxed);
}

//The size bytes from address are about to be read by the GE's own thread (a texture, a palette, vertices, the list's
//commands): whatever is waiting to be drawn over them is drawn first. A batch being filled is drawn, after those
//before it; batches being drawn are waited for (those all drawn already needn't be); what a hardware renderer drew
//over them is put back in memory.
auto GE::drawnFirst(u32 address, u32 size) -> void {
  u32 first, last;
  if(!vramSpan(address, size, first, last)) return;
  auto over = [&](const Batch& batch) {
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      if(batch.pending[page]) return true;
    }
    return false;
  };
  if(!drawing.batch->jobs.empty() && over(*drawing.batch)) return flush();
  for(auto& batch : drawing.batches) {
    if(unfinished(batch) && over(batch)) return settle();
  }
  //(what a hardware renderer drew, still on the GPU: its pixels' bytes, not all of the pages they're in)
  if(renderer && memory.vramBusy && renderer->drawnOver(*this, first, last)) renderer->finish(*this);
}

//A block transfer (transfer.cpp) is about to read VRAM's pages read and write its pages written, the GE's thread
//copying: what it must come after is drawn first. That's every batch drawing over any of those pages (its pending
//ones), or yet to decode a texture from those it writes (its reads: render to texture), and so every batch before
//it; the batch being filled, if it's one, after all of them (flush()). The rest go on being drawn, or waiting in the
//batch being filled, while the GE's thread copies: none of them reads or writes what the copy writes, nor writes
//what it reads, so they and the copy come out the same whichever goes first. (But for one yet to decode a texture
//from pages the copy only reads: with the CPU's guard over VRAM those pages are busy, and the copy's reads wait for
//it there, as anyone's would.) With a hardware renderer, everything waiting is drawn first, as ever (the copy then
//waits for what the renderer drew, by memory's busy pages).
auto GE::drawnBefore(const std::bitset<VRAMPages>& read, const std::bitset<VRAMPages>& written) -> void {
  if(renderer) return flush();
  auto reaches = [&](const Batch& batch) {
    if((batch.pending & (read | written)).any()) return true;
    return !batch.readsDone.load(std::memory_order_acquire) && (batch.reads & written).any();
  };
  if(!drawing.batch->jobs.empty() && reaches(*drawing.batch)) return flush();
  settleThrough(reaches);
}

//Whether drawing still going on reaches VRAM's bytes first to last (offsets in VRAM): all of a page a batch being
//drawn draws in, or has yet to read a deferred texture from (not one all drawn already), or a hardware renderer's
//pixels themselves (Memory::vramDrawnOver, for whoever touches busy pages).
auto GE::drawnOver(u32 first, u32 last) -> bool {
  for(auto& batch : drawing.batches) {
    if(!unfinished(batch)) continue;
    bool reading = !batch.readsDone.load(std::memory_order_acquire);
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      if(batch.pending[page] || (reading && batch.reads[page])) return true;
    }
  }
  return renderer && renderer->drawnOver(*this, first, last);
}

//Whether a primitive drawn with these settings, inside region, may wait in the batch (see the top of this file):
//drawing the batch in bands must come out as drawing its primitives one after another would. So no two pixels in
//different rows may share a byte: inside the batch's area (every primitive's region together), one row of its frame
//buffer mustn't reach the next, nor run round VRAM's end; likewise its depth buffer's, if any of them reaches it;
//and the frame and depth buffers mustn't overlap. A primitive into another render target than the batch's, or that
//would break that, has the batch drawn first; one that breaks it by itself is drawn at once.
auto GE::defer(const PixelState& p, const Region& region) -> bool {
  if(!drawing.deferring || drawing.workers.empty()) return false;
  if(region.left > region.right || region.top > region.bottom) return true;  //draws nothing
  auto fits = [&](s32 left, s32 right, s32 top, s32 bottom, bool depth) {
    u32 bytes = p.format == 3 ? 4 : 2, columns = right - left;  //(less one)
    u32 colorLow = p.frameBuffer + (top * p.stride + left) * bytes;
    u32 colorHigh = p.frameBuffer + (bottom * p.stride + right) * bytes + bytes - 1;
    if(p.stride <= columns || colorHigh >= Memory::VRAMSize) return false;
    if(!depth) return true;
    u32 depthLow = p.depthBuffer + (top * p.depthStride + left) * 2;
    u32 depthHigh = p.depthBuffer + (bottom * p.depthStride + right) * 2 + 1;
    if(p.depthStride <= columns || depthHigh >= Memory::VRAMSize) return false;
    return (depthLow & ~0x3fffu) > colorHigh || colorLow > (depthHigh | 0x3fff);  //(16 KiB rearranged: memory.hpp)
  };
  bool depth = p.clear ? p.clearDepth : p.depthTest;  //it reaches its depth buffer
  if(drawing.batch->targeted) {
    Batch& batch = *drawing.batch;
    bool same = p.frameBuffer == batch.frameBuffer && p.stride == batch.stride && p.format == batch.format &&
                p.depthBuffer == batch.depthBuffer && p.depthStride == batch.depthStride;
    s32 left = std::min(batch.left, region.left), right = std::max(batch.right, region.right);
    s32 top = std::min(batch.upper, region.top), bottom = std::max(batch.lower, region.bottom);
    if(same && fits(left, right, top, bottom, batch.depth || depth)) {
      batch.left = left, batch.right = right, batch.upper = top, batch.lower = bottom;
      batch.depth |= depth;
      return true;
    }
    launch(false);  //(the next batch, this one's, is drawn after it)
  }
  if(!fits(region.left, region.right, region.top, region.bottom, depth)) return false;
  Batch& batch = *drawing.batch;
  batch.targeted = true;
  batch.frameBuffer = p.frameBuffer, batch.stride = p.stride, batch.format = p.format;
  batch.depthBuffer = p.depthBuffer, batch.depthStride = p.depthStride;
  batch.left = region.left, batch.right = region.right, batch.upper = region.top, batch.lower = region.bottom;
  batch.depth = depth;
  return true;
}

//A job into the batch, and into the list of each band it reaches (its rows are inside the scissor rectangle: 0-1023),
//so that a band's thread looks only at the jobs reaching it, not at every job of the batch.
auto GE::record(const Job& job) -> void {
  Batch& batch = *drawing.batch;
  if(batch.bandJobs.empty()) batch.bandJobs.resize(1024 / BandRows);  //(made with its first job, kept after)
  u32 index = batch.jobs.size();
  for(s32 band = job.firstY / BandRows; band <= job.lastY / BandRows; band++) batch.bandJobs[band].push_back(index);
  batch.jobs.push_back(job);
  batch.work += u64(job.lastX - job.firstX + 1) * u64(job.lastY - job.firstY + 1);
  if(batch.top > batch.bottom) batch.top = job.firstY, batch.bottom = job.lastY;
  batch.top = std::min(batch.top, job.firstY), batch.bottom = std::max(batch.bottom, job.lastY);
}
