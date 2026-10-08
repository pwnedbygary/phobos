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
//and draws every job that reaches it, in order, until none is left. Meanwhile the GE's thread goes on: setting up the
//next batch in the other of its two (which the workers start on only once the first is done, so batches are drawn
//in order too), or, the list stopped, running the CPU. So drawing overlaps the setting up of what comes next and the
//program's own work.
//
//Nobody may see a batch half drawn, so it's waited for ("settled", the GE's thread drawing bands too) first:
//  - by anyone touching VRAM it draws over while the CPU runs on: Memory::pointer(), behind every read, write and
//    copy (the CPU's, an HLE function's, the screen's picture), save states and power (memory.hpp); the CPU's
//    compiled loads and stores reach VRAM through its page table, so the CPU's owner takes those pages out of it
//    meanwhile (Memory::vramGuard; without one, a list's batches are all drawn before run() returns);
//  - by the next list (run() starts by settling), and by anything the GE's own thread reads from VRAM a batch draws
//    over: a texture to decode, a palette, vertices, the list's commands (drawnFirst());
//  - by a block transfer (it reads and writes memory, maybe drawn pixels), which draws the batch being filled too;
//  - by a primitive that must be drawn by itself, at once, in order (texture.cpp reads its texels from memory as it
//    draws: one drawing over its own texture, say), or whose pixels in different rows share bytes (defer()).
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

//Takes bands of the batch until none are left, drawing in each every job that reaches it, in order.
auto GE::drawBands(Batch& batch) -> void {
  while(true) {
    u32 band = batch.nextBand.fetch_add(1, std::memory_order_relaxed);
    if(band >= batch.bands) return;
    s32 top = batch.top + s32(band) * BandRows, bottom = std::min(top + BandRows - 1, batch.bottom);
    for(auto& job : batch.jobs) {
      if(job.lastY >= top && job.firstY <= bottom) rasterize(job, top, bottom);
    }
    batch.bandsLeft.fetch_sub(1, std::memory_order_acq_rel);
  }
}

//The batch's bands to draw, and the workers due to wake for them (the caller holds the mutex, and wakes them).
auto GE::startBands(Batch& batch) -> void {
  batch.bands = (batch.bottom - batch.top + BandRows) / BandRows;
  batch.nextBand.store(0, std::memory_order_relaxed);
  batch.bandsLeft.store(batch.bands, std::memory_order_relaxed);
  drawing.drawn = &batch;
  drawing.round++;
}

//A thread done drawing bands of the batch (holding the mutex): if that finished its last, it's done, and the one
//queued after it starts. Returns whether one did (the workers are then to be woken).
auto GE::drew(Batch& batch) -> bool {
  batch.users--;
  if(drawing.drawn != &batch || batch.bandsLeft.load(std::memory_order_acquire)) return false;
  drawing.drawn = nullptr;
  if(!drawing.queued) return false;
  startBands(*drawing.queued);
  drawing.queued = nullptr;
  return true;
}

//Draws the batch being filled (see the top of this file) and waits for it, after any being drawn; then empties it.
auto GE::flush() -> void {
  settle();
  Batch& batch = *drawing.batch;
  if(batch.jobs.empty()) return clearBatch(batch);
  if(drawing.workers.empty() || batch.work < drawing.shared) {
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
//workers, to be drawn after any before it while the GE's thread goes on, which fills the other batch next (once
//that's drawn). Unless there's nothing to share it with, or it's small and nothing else is being drawn, or the list
//stops and the CPU's owner can't keep the CPU off VRAM meanwhile: then it's drawn, and waited for.
auto GE::launch(bool returning) -> void {
  Batch& batch = *drawing.batch;
  bool busy = drawing.batches[0].launched || drawing.batches[1].launched;
  if(batch.jobs.empty() && !(returning && busy && !memory.vramGuard)) return clearBatch(batch);
  if(drawing.workers.empty() || (returning && !memory.vramGuard) || (batch.work < drawing.shared && !busy)) {
    return flush();
  }
  {
    std::lock_guard lock(drawing.mutex);
    batch.launched = true;
    if(!drawing.drawn) startBands(batch);
    else drawing.queued = &batch;  //(the other batch, being drawn: two at most)
  }
  drawing.wake.notify_all();
  if(memory.vramGuard) {  //(its pages are the workers' until it's settled)
    memory.vramBusy = true;
    for(u32 page = 0; page < VRAMPages; page++) {
      if(batch.pending[page]) memory.busyPages[page >> 6] |= 1ull << (page & 63);
    }
    memory.vramGuard(true);
  }
  Batch& other = &batch == &drawing.batches[0] ? drawing.batches[1] : drawing.batches[0];
  drawing.batch = &other;
  if(returning || !other.launched) return;
  //the other is still being drawn: wait for it to be done with (drawing its bands too) before filling it again
  std::unique_lock lock(drawing.mutex);
  while(drawing.drawn == &other || drawing.queued == &other || other.users) {
    if(drawing.drawn == &other && other.nextBand.load(std::memory_order_relaxed) < other.bands) {
      other.users++;
      lock.unlock();
      drawBands(other);
      lock.lock();
      if(drew(other)) drawing.wake.notify_all();
      continue;
    }
    drawing.finished.wait(lock);
  }
  lock.unlock();
  clearBatch(other);
}

//Waits for every batch handed to the workers (the GE's thread drawing bands too), and empties them: VRAM is
//anyone's again.
auto GE::settle() -> void {
  if(!drawing.batches[0].launched && !drawing.batches[1].launched) return;
  std::unique_lock lock(drawing.mutex);
  while(drawing.drawn || drawing.queued || drawing.batches[0].users || drawing.batches[1].users) {
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
  if(memory.vramBusy) {
    memory.vramBusy = false;
    if(memory.vramGuard) memory.vramGuard(false);
    for(auto& pages : memory.busyPages) pages = 0;
  }
}

//Everything drawn put in memory's VRAM: the batches settled, and what a hardware renderer owns put back (ge.hpp's
//Renderer). What anyone touching VRAM's busy pages waits for (Memory::finishDrawing).
auto GE::settleAll() -> void {
  settle();
  if(renderer) renderer->finish(*this);
}

//A batch done with.
auto GE::clearBatch(Batch& batch) -> void {
  batch.jobs.clear();
  batch.looks.clear();
  batch.work = 0;
  batch.top = 0, batch.bottom = -1;
  batch.targeted = false;
  batch.pending.reset();
  batch.launched = false;
}

//The size bytes from address are about to be read by the GE's own thread (a texture, a palette, vertices, the list's
//commands): whatever is waiting to be drawn over them is drawn first. A batch being filled is drawn, after those
//before it; batches being drawn are waited for.
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
    if(batch.launched && over(batch)) return settle();
  }
  if(renderer && memory.vramBusy) {  //(pages a hardware renderer owns, still on the GPU)
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      if(memory.vramPageBusy(page)) return renderer->finish(*this);
    }
  }
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

//A job into the batch.
auto GE::record(const Job& job) -> void {
  Batch& batch = *drawing.batch;
  batch.jobs.push_back(job);
  batch.work += u64(job.lastX - job.firstX + 1) * u64(job.lastY - job.firstY + 1);
  if(batch.top > batch.bottom) batch.top = job.firstY, batch.bottom = job.lastY;
  batch.top = std::min(batch.top, job.firstY), batch.bottom = std::max(batch.bottom, job.lastY);
}
