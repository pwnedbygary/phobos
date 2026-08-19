#include <android/log.h>
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

  u64 hbits = 0x5b1d7f390a6e2c48ULL;
  memory::fill<n1>(hscale, sizeof(hscale), 0x00);
  for(u8 y : range(16)) {
    for(u8 x : reverse(range(y + 1))) {
      n4 value = hbits >> x * 4;
      hscale[y][value] = 1;
    }
  }
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
