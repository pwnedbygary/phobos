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

  // Horizontal zoom table (MVS) — MAME's authoritative zoom_x_tables.
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
    if(irq.timerAcknowledge && timer.interruptEnable) {
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
    //CDD/CDC tick: 75Hz at 1x read speed; scales with the per-core CD speed
    //multiplier (75 * readSpeed) so disc loads complete faster while keeping
    //the exact per-sector protocol (one decoder IRQ per sector).
    u32 cddHz = 75 * (cdd.readSpeed ? cdd.readSpeed : 1);
    if(io.cddCounter += 1, io.cddCounter >= 6'000'000 / cddHz) {
      io.cddCounter = 0;
      cdd.tick();
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
      if(irq.vblankAcknowledge) {
        irq.vblankAcknowledge = 0;
        cpu.raise(CPU::Interrupt::Vblank);
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
  screen->setViewport(0, 0, 320, 256);
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
