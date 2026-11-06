#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace ares::NeoGeo {

LSPC lspc;
#include "color.cpp"
#include "render.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto LSPC::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("LSPC");

  screen = node->append<Node::Video::Screen>("Screen", 320, 256);
  screen->colors(1 << 17, std::bind_front(&LSPC::color, this));
  screen->setSize(320, 256);
  screen->setScale(1.0, 1.0);
  screen->setAspect(1.0, 1.0);
  screen->refreshRateHint(6'000'000, 384, 264);

  vram.allocate(68_KiB >> 1);
  pram.allocate(16_KiB >> 1);

  debugger.load(node);

  //vertical zoom table: the LSPC's real 256x256 zoom table (MAME "000-lo.lo").
  //The default construction below is a placeholder; the system loader fills
  //the real table from the BIOS set via loadZoomy().
  memory::fill<n8>(vscale, sizeof(vscale), 0xff);

  // Horizontal zoom table (MVS) — MAME's authoritative zoom_x_tables
  // (src/mame/snk/neogeo_spr.cpp, license:BSD-3-Clause; see "MAME (portions)" in LICENSE).
  // hscale[zoom][x] = 1 selects source pixel x for that horizontal shrink level.
  // (The previous hbits-derived table diverged from hardware for zoom 9..14,
  //  garbling horizontally-zoomed sprites such as scaled sports pitches.)
  static const u8 zoomXData[16][16] = {
    {0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0},
    {0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0},
    {0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0},
    {0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0},
    {0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
    {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0},
    {1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0},
    {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0},
    {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1},
    {1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1},
    {1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1},
    {1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1},
    {1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  };
  memory::fill<n1>(hscale, sizeof(hscale), 0x00);
  for(u8 y : range(16))
    for(u8 x : range(16))
      hscale[y][x] = zoomXData[y][x];
}

auto LSPC::loadZoomy(const u8* data, u32 size) -> void {
  //MAME "000-lo.lo": 0x20000 bytes (only the first 0x10000 = [zoom][line] table is used)
  if(!data || size < 0x10000) return;
  nall::memory::copy<n8>(vscale, data, 0x10000);
}

auto LSPC::unload() -> void {
  debugger.unload(node);
  vram.reset();
  pram.reset();
  screen->quit();
  node->remove(screen);
  screen.reset();
  node.reset();
}

auto LSPC::step(u32 clocks) -> void {
  if(timer.counter && !--timer.counter) {
    if(timer.reloadOnZero) {
      timer.counter = timer.reload;
    }
    if(irq.timerAcknowledge && timer.interruptEnable && (!NeoGeo::Model::NeoGeoCD() || (system.io.irqMask2 & 0x300) == 0x300)) {
      irq.timerAcknowledge = 0;
      cpu.raise(CPU::Interrupt::Timer);
    }
  }
  Thread::step(clocks);
  Thread::synchronize();
}

auto LSPC::main() -> void {
  step(1);
  if(NeoGeo::Model::NeoGeoCD()) {
    //CDD/CDC tick: one sector and one decoder IRQ per tick, at the drive's
    //own speed (Cdd::tickPeriod): 75 a second, or 150 while the CDZ's
    //double-speed drive reads data, as libretro neocd has it.
    if(io.cddCounter += 1, io.cddCounter >= cdd.tickPeriod()) {
      io.cddCounter = 0;
      cdd.tick();
#if defined(__ANDROID__)
      static u32 ticks = 0;
      if((++ticks & 255) == 0) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "cdd tick %u period=%u t1=%d t2=%d t3=%d", ticks, (unsigned)cdd.tickPeriod(), (int)cdd.type1Pending, (int)cdd.type2Pending, (int)cdd.type3Pending);
#endif
    }
  }
  if(++io.hcounter == 384) {
    io.hcounter = 0;
    if(++io.vcounter == 264) {
      io.vcounter = 0;
      if(!animation.counter--) {
        animation.counter = animation.speed;
        animation.frame++;
      }
#if defined(__ANDROID__)
      if(NeoGeo::Model::NeoGeoCD() && irq.vblankAcknowledge && (system.io.irqMask2 & 0x030) != 0x030) {
        static u32 blocked = 0;
        if(++blocked <= 5) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "vblank blocked %u mask2=%04x ack=%d", blocked, (unsigned)system.io.irqMask2, (int)irq.vblankAcknowledge);
      }
#endif
      if(irq.vblankAcknowledge && (!NeoGeo::Model::NeoGeoCD() || (system.io.irqMask2 & 0x030) == 0x030)) {
        irq.vblankAcknowledge = 0;
        cpu.raise(CPU::Interrupt::Vblank);
#if defined(__ANDROID__)
        static u32 vb = 0; if((++vb & 127) == 0) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "vblank raise %u mask2=%04x", vb, (unsigned)system.io.irqMask2);
#endif
      }
      if(timer.reloadOnVblank) {
        timer.counter = timer.reload;
      }
      frame();
    }
  }

  // 8 lines of vblank, 16px top border, 16px bottom border
  if(io.vcounter >= 24 && io.vcounter <= 247 && io.hcounter == 56) {
    render(io.vcounter - 8);
  }
}

auto LSPC::frame() -> void {
#if defined(__ANDROID__)
  static u32 frames = 0;
  if((++frames & 63) == 0) __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "lspc f%u: vram0=%04x vram1000=%04x pram0=%04x pramBank=%u shadow=%u", frames,
    (unsigned)vram[0], (unsigned)vram[0x1000], (unsigned)pram[0], (unsigned)io.pramBank, (unsigned)io.shadow);
#endif
  //the picture is lines 16-239 (see main()); the lines above and below it are border
  if(screen->overscan()) screen->setViewport(0, 0, 320, 256);
  else screen->setViewport(0, 16, 320, 224);
  screen->frame();
  scheduler.exit(Event::Frame);
}

auto LSPC::power(bool reset) -> void {
  Thread::create(6'000'000, std::bind_front(&LSPC::main, this));
  screen->power();
  animation = {};
  timer = {};
  irq = {};
  io = {};
  cpu.raise(CPU::Interrupt::Power);
}

}
