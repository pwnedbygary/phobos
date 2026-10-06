//Drawing on several threads.
//
//Most of the GE's time goes into drawing pixels, one after another. But no pixel of a primitive depends on another
//pixel of it (draw.cpp works each out from the primitive's setup alone), so the screen can be cut into bands of rows
//and each band drawn by a different thread at the same time. Inside a band the primitives are drawn in the order
//the display list gave them, as the PSP draws them, and a pixel is only ever drawn by the band it's in: so every
//pixel comes out exactly as drawing the primitives one after another, on one thread, makes it.
//
//How: while the GE runs a list (run()), a primitive it meets isn't drawn there and then. It's set up (its jobs) and
//waits in a batch. When the list stops, or something needs what the batch will draw (below), the batch is drawn
//("flushed"): the rows its jobs cover are cut into bands of BandRows rows; the GE's thread and the workers each take
//the next band nobody has taken, and draw every job that reaches it, in order, until no band is left; and the GE's
//thread waits for the last one before anything else happens. The CPU only runs again once the list has stopped, so
//nothing a program does can see a frame half drawn.
//
//A batch is also drawn first, before:
//  - the list stopping (run() returns), so the CPU, the kernel, the screen and save states see finished drawing;
//  - a block transfer (it reads and writes memory, maybe drawn pixels), and a palette loaded from VRAM it draws over;
//  - a texture decoded, a display list command or vertices read, from VRAM it draws over;
//  - a primitive into another render target (frame buffer, depth buffer, their formats and widths), or one that with
//    the batch's others would have pixels in different rows share bytes (defer());
//  - a primitive that must be drawn by itself, at once, in order: one reading its texels from memory as it draws
//    (texture.cpp: one drawing over its own texture, say), or whose pixels in different rows share bytes.
//While they wait, jobs only read what doesn't change: their own setup and settings, decoded textures (which the batch
//keeps), and VRAM's frame and depth buffers, each pixel by its own band alone.
//
//With 'GE Threads' at 1 nothing waits: each primitive is drawn at once on the GE's thread, as it always was. A batch
//with fewer pixels than drawing.shared is drawn on the GE's thread alone, which is quicker than waking the others.

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

GE::~GE() {
  setThreads(1);
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

//A worker: it waits for a batch to be drawn, draws bands of it as the GE's thread does, and waits again.
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
    {
      std::unique_lock lock(drawing.mutex);
      drawing.wake.wait(lock, [&] { return drawing.quit || drawing.round != seen; });
      if(drawing.quit) return;
      seen = drawing.round;
      drawing.active++;
    }
    drawBands();
    {
      std::lock_guard lock(drawing.mutex);
      drawing.active--;
    }
    drawing.finished.notify_all();
  }
}

//Takes bands of the batch until none are left, drawing in each every job that reaches it, in order.
auto GE::drawBands() -> void {
  while(true) {
    u32 band = drawing.nextBand.fetch_add(1, std::memory_order_relaxed);
    if(band >= drawing.bands) return;
    s32 top = drawing.top + s32(band) * BandRows, bottom = std::min(top + BandRows - 1, drawing.bottom);
    for(auto& job : drawing.jobs) {
      if(job.lastY >= top && job.firstY <= bottom) rasterize(job, top, bottom);
    }
    drawing.bandsLeft.fetch_sub(1, std::memory_order_release);
  }
}

//Draws the batch (see the top of this file), and empties it.
auto GE::flush() -> void {
  if(!drawing.jobs.empty()) {
    if(drawing.workers.empty() || drawing.work < drawing.shared) {
      for(auto& job : drawing.jobs) rasterize(job, job.firstY, job.lastY);
    } else {
      {
        std::unique_lock lock(drawing.mutex);
        drawing.finished.wait(lock, [&] { return drawing.active == 0; });  //nobody still in the last round
        drawing.bands = (drawing.bottom - drawing.top + BandRows) / BandRows;
        drawing.nextBand.store(0, std::memory_order_relaxed);
        drawing.bandsLeft.store(drawing.bands, std::memory_order_relaxed);
        drawing.round++;
      }
      drawing.wake.notify_all();
      drawBands();
      std::unique_lock lock(drawing.mutex);
      drawing.finished.wait(lock, [&] {
        return drawing.bandsLeft.load(std::memory_order_acquire) == 0 && drawing.active == 0;
      });
    }
  }
  drawing.jobs.clear();
  drawing.looks.clear();
  drawing.work = 0;
  drawing.top = 0, drawing.bottom = -1;
  drawing.targeted = false;
  drawing.pending.reset();
}

//Whether the batch may draw over any of the size bytes from address (in VRAM, by its 4 KiB pages).
auto GE::pendingOver(u32 address, u32 size) const -> bool {
  u32 first, last;
  if(drawing.jobs.empty() || !vramSpan(address, size, first, last)) return false;
  for(u32 page = first >> 12; page <= last >> 12; page++) {
    if(drawing.pending[page]) return true;
  }
  return false;
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
  if(drawing.targeted) {
    bool same = p.frameBuffer == drawing.frameBuffer && p.stride == drawing.stride && p.format == drawing.format &&
                p.depthBuffer == drawing.depthBuffer && p.depthStride == drawing.depthStride;
    s32 left = std::min(drawing.left, region.left), right = std::max(drawing.right, region.right);
    s32 top = std::min(drawing.upper, region.top), bottom = std::max(drawing.lower, region.bottom);
    if(same && fits(left, right, top, bottom, drawing.depth || depth)) {
      drawing.left = left, drawing.right = right, drawing.upper = top, drawing.lower = bottom;
      drawing.depth |= depth;
      return true;
    }
    flush();
  }
  if(!fits(region.left, region.right, region.top, region.bottom, depth)) return false;
  drawing.targeted = true;
  drawing.frameBuffer = p.frameBuffer, drawing.stride = p.stride, drawing.format = p.format;
  drawing.depthBuffer = p.depthBuffer, drawing.depthStride = p.depthStride;
  drawing.left = region.left, drawing.right = region.right, drawing.upper = region.top, drawing.lower = region.bottom;
  drawing.depth = depth;
  return true;
}

//A job into the batch.
auto GE::record(const Job& job) -> void {
  drawing.jobs.push_back(job);
  drawing.work += u64(job.lastX - job.firstX + 1) * u64(job.lastY - job.firstY + 1);
  if(drawing.top > drawing.bottom) drawing.top = job.firstY, drawing.bottom = job.lastY;
  drawing.top = std::min(drawing.top, job.firstY), drawing.bottom = std::max(drawing.bottom, job.lastY);
}
