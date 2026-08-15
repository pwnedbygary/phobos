#if defined(__ANDROID__)
#include <android/log.h>
#endif

// Gated by the Phobos "N64 Debug Logging" toggle (defined in PhobosRunner.cpp).
namespace ares {
auto n64DebugLoggingEnabled() -> bool;
}

namespace ares::Nintendo64 {

// Called from parallel-RDP's op_set_color_image to publish the real RDP
// framebuffer width/address (see vi.hpp / rdp.hpp). The VI's CPU scanout
// fallback uses these so it strides by the RDP width, not the VI display
// width (fixes Rogue Squadron's 512-render→640-present black menu).
auto setRdpFramebuffer(unsigned width, unsigned address) -> void {
  rdp.rdpFramebufferWidth  = width;
  rdp.rdpFramebufferAddress = address;
}

// Read accessors for the parallel-RDP scanout path (CommandProcessor::scanout).
// These are written on the RDP command thread (op_set_color_image) and read on
// the screen thread — but only ever set to the RDP's actual SET_COLOR_IMAGE
// values, which change once per mode/buffer, so a torn read is impossible
// (32-bit aligned writes are atomic on ARM64). Reading the Renderer's fb here
// instead would race with set_color_framebuffer's queue flushes.
auto rdpFramebufferWidth() -> unsigned { return rdp.rdpFramebufferWidth; }
auto rdpFramebufferAddress() -> unsigned { return rdp.rdpFramebufferAddress; }

VI vi;
#include "io.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto VI::step(u32 clocks) -> void {
  auto scaled = (u64)clocks * system.frequency() + clockFraction;
  Thread::clock += scaled / system.videoFrequency();
  clockFraction = scaled % system.videoFrequency();
}

auto VI::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("VI");

  u32 width = 640;
  u32 height = 576;

  #if defined(VULKAN)
  if (vulkan.enable) {
    width *= vulkan.outputUpscale;
    height *= vulkan.outputUpscale;
  }
  #endif
  screen = node->append<Node::Video::Screen>("Screen", width, height);
  screen->setRefresh(std::bind_front(&VI::refresh, this));
  // VI Overclock: scale the host refresh hint so the emulation loop paces at
  // the overclocked frame rate (e.g. 120Hz at 2x), not the native 50/60Hz.
  s32 oc = overclockPercent.load();
  if (oc <= 0) oc = 100;
  screen->refreshRateHint((Region::PAL() ? 50 : 60) * oc / 100); // TODO: More accurate refresh rate hint
  screen->colors((1 << 24) + (1 << 15), [&](n32 color) -> n64 {
    if(color < (1 << 24)) {
      u64 a = 65535;
      u64 r = image::normalize(color >> 16 & 255, 8, 16);
      u64 g = image::normalize(color >>  8 & 255, 8, 16);
      u64 b = image::normalize(color >>  0 & 255, 8, 16);
      return a << 48 | r << 32 | g << 16 | b << 0;
    } else {
      u64 a = 65535;
      u64 r = image::normalize(color >> 10 & 31, 5, 16);
      u64 g = image::normalize(color >>  5 & 31, 5, 16);
      u64 b = image::normalize(color >>  0 & 31, 5, 16);
      return a << 48 | r << 32 | g << 16 | b << 0;
    }
  });
  
  int videoHeight = Region::PAL() ? 576 : 480;

  #if defined(VULKAN)
  if(vulkan.enable) {
    screen->setSize(vulkan.outputUpscale * 640, vulkan.outputUpscale * videoHeight);
    if(!vulkan.supersampleScanout) {
      screen->setScale(1.0 / vulkan.outputUpscale, 1.0 / vulkan.outputUpscale);
    }
  } else {
    screen->setSize(640, videoHeight);
  }
  #else
  screen->setSize(640, videoHeight);
  #endif

  // Pedantic N64 NTSC aspect ratio is 120:119, but let's keep 120:120 to avoid slight scaling.
  // Pedantic N64 PAL aspect ratio is 5900000:4965653, but let's use 12:10 to achieve the
  // same aspect ratio as NTSC.
  Region::PAL() ? screen->setAspect(12, 10) : screen->setAspect(120, 120);

  debugger.load(node);
}

auto VI::unload() -> void {
  debugger = {};
  node->remove(screen);
  screen.reset();
  node.reset();
}

auto VI::main() -> void {
  while(Thread::clock < 0) {
    if(active()) {
      ++io.vcounter;
      int halfline = io.vcounter << 1 | io.field;
      if(halfline >= io.halfLinesPerField+1) {
        io.vcounter = 0;
        // Fix: properly toggle field for interlaced (even halfLinesPerField)
        // and progressive (odd halfLinesPerField). Previously bit(0) check
        // was inverted: odd→field locked 0→30fps+freeze (Conker).
        io.field += io.halfLinesPerField.bit(0);
        if(++io.leapCounter == 5) io.leapCounter = 0;
      }

      if(io.vcounter == io.vstart >> 1) {
        #if defined(VULKAN)
        if (vulkan.enable) {
          gpuOutputValid = vulkan.scanoutAsync(io.field);
          vulkan.frame();
          // Bounded diagnostic: log VI state + scanout validity ~2x/sec so we
          // can see whether Rogue Squadron's framebuffer is reachable. Remove
          // once the black-screen investigation is done.
          static u64 viDiagCount = 0;
          if (::ares::n64DebugLoggingEnabled() && viDiagCount++ % 60 == 0) {
            __android_log_print(ANDROID_LOG_INFO, "PhobosVI",
              "scanoutValid=%d field=%d vc=%d dramAddr=0x%08x width=%u io.width=%u depth=%u hstart=%u vstart=%u xscale=%u xsub=%u",
              (int)gpuOutputValid, (int)io.field, (int)io.vcounter,
              (unsigned)io.dramAddress, (unsigned)io.width, (unsigned)io.width,
              (unsigned)io.colorDepth, (unsigned)io.hstart, (unsigned)io.vstart,
              (unsigned)io.xscale, (unsigned)io.xsubpixel);
          }
        }
        #endif
        refreshed = true;
        screen->frame();
        ri.checkRefresh();
      }

      if(io.halfLinesPerField.bit(0)) { // progressive
        if(io.vcounter == io.coincidence >> 1) {
          mi.raise(MI::IRQ::VI);
        }
      } else { // interlaced
        if(io.coincidence.bit(0)) {
          if(io.vcounter == io.coincidence >> 1)
            mi.raise(MI::IRQ::VI);
        }
        if(!io.coincidence.bit(0)) {
          // Fix: check coincidence against vcounter (field-agnostic) rather
          // than halfline (field-aware). When coincidence==0, halfline
          // on field 0 is always even, so halfline==coincidence is unreachable
          // → VI interrupt never fires → Conker's BFD freezes at menu.
          if(!io.field && io.vcounter == io.coincidence)
            mi.raise(MI::IRQ::VI);
          if(io.field && io.vcounter+1 == io.coincidence)
            mi.raise(MI::IRQ::VI);
          if(!io.field && io.vcounter == io.halfLinesPerField && io.coincidence == 0)
            mi.raise(MI::IRQ::VI);
        }
      }

      u32 lineDuration = io.quarterLineDuration+1;
      if(io.vcounter == 1)
        lineDuration = io.hsyncLeap[io.leapPattern.bit(io.leapCounter)];
      // VI Overclock: scale the line duration so the VI generates frames
      // faster than native. The game's frame logic (tied to the VI interrupt
      // / vcounter coincidence) then runs at the higher rate — genuine
      // higher-FPS emulation, not host-side fast forward. Host pacing follows
      // via refreshRateHint in VI::load/VI::power.
      if (s32 oc = overclockPercent.load(); oc != 100) {
        if (oc <= 0) oc = 100;
        lineDuration = lineDuration * 100 / oc;
      }
      step(lineDuration);
    } else {
      // Arbitrarily call screen->frame() every once in a while to keep the UI responsive.
      // We do that every 200 simulated lines of 0x800 quarter-clocks. This is just arbitrary,
      // the real VI is not clocking at all when inactive.
      io.vcounter = 0;
      if(++inactiveCounter >= 200) {
        inactiveCounter = 0;
        refreshed = true;
      }
      step(0x800);
    }
  }
}

auto VI::refresh() -> void {
  #if defined(VULKAN)
  if(vulkan.enable && gpuOutputValid) {
    // Rogue Squadron menu: VI_WIDTH=1024 (the game lies; it renders 512-wide).
    // parallel-RDP's VI scanout always produces a 640-wide buffer and does the
    // full VI scaling (XStart/XAdd) in its fragment shader — exactly like
    // desktop. The old "clamps to 640 → black" premise was based on the OOB
    // copy-loop bug below (now fixed with the downscale), so wide modes are
    // handled by the Vulkan path here; the CPU RDRAM fallback below is only
    // reached when Vulkan is disabled.
    io.cpuScanoutActive = 0;
    const u8* rgba = nullptr;
    u32 width = 0, height = 0;
    vulkan.mapScanoutRead(rgba, width, height);
    if(rgba) {
      screen->setViewport(0, 0, width, height);
      // [Phobos] Diagnostic: map the CONTENT bounding box of the scanout buffer
      // (where the non-black pixels are). Tells us if the menu content is
      // positioned/scaled correctly in the 640-wide output.
      static u64 viPixCount = 0;
      if (::ares::n64DebugLoggingEnabled() && viPixCount++ % 60 == 0) {
        u32 minX = width, maxX = 0, minY = height, maxY = 0, cnt = 0;
        for(u32 y = 0; y < height; y += 8) {
          for(u32 x = 0; x < width; x += 8) {
            u32 p = rgba[(y * width + x) * 4];
            if(p > 0x20) {
              cnt++;
              minX = min(minX, x); maxX = max(maxX, x);
              minY = min(minY, y); maxY = max(maxY, y);
            }
          }
        }
        __android_log_print(ANDROID_LOG_INFO, "PhobosVI",
          "pixbox: w=%u h=%u box=[%u..%u]x[%u..%u] cnt=%u",
          width, height, minX, maxX, minY, maxY, cnt);
      }
      for(u32 y : range(height)) {
        auto source = rgba + width * y * sizeof(u32);
        auto target = screen->pixels(1).data() + y * vulkan.outputUpscale * 640;
        // The ares Screen node is fixed at 640 wide. parallel-RDP scanout can
        // produce WIDER frames (e.g. Rogue Squadron's menu uses a 1024-wide VI
        // mode — width=1024). The old code copied `width` pixels into the
        // 640-wide target with a 640 stride → out-of-bounds writes past each
        // row + garbage/black output. Fix: horizontal downscale (box filter)
        // from `width` → 640 so every source pixel lands correctly.
        if(width <= 640) {
          for(u32 x : range(width)) {
            target[x] = source[x * 4 + 0] << 16 | source[x * 4 + 1] << 8 | source[x * 4 + 2] << 0;
          }
        } else {
          // Box-filter downscale width→640 (average each source block).
          const u32 dstW = 640;
          for(u32 x : range(dstW)) {
            u32 sx0 = (u64)x * width / dstW;
            u32 sx1 = max(sx0 + 1, ((u64)(x + 1) * width / dstW));
            u32 r = 0, g = 0, b = 0, n = 0;
            for(u32 sx = sx0; sx < sx1; sx++) {
              r += source[sx * 4 + 0];
              g += source[sx * 4 + 1];
              b += source[sx * 4 + 2];
              n++;
            }
            if(n) { r /= n; g /= n; b /= n; }
            target[x] = r << 16 | g << 8 | b;
          }
        }
      }
    }
    // endScanout() must run before unmapScanoutRead() so it stays inside the
    // lock window held by mapScanoutRead()/unmapScanoutRead(); otherwise the
    // Vulkan implementation could be destroyed by a concurrent reset while
    // endScanout() dereferences it.
    vulkan.endScanout();
    vulkan.unmapScanoutRead();

    if(Model::Aleck64()) aleck64.vdp.render(screen); //aleck64 supports overlay graphics
    return;
  }
  #endif

  if(io.serrate == 0) screen->setProgressive(0);
  if(io.serrate == 1) screen->setInterlace(!io.field);

  u32 hscan_start = Region::NTSC() ? 108 : 128;
  u32 vscan_start = Region::NTSC() ?  34 :  44;
  u32 hscan_len   = Region::NTSC() ? 640 : 640;
  u32 vscan_len   = Region::NTSC() ? 480 : 576;
  u32 hscan_stop  = hscan_start + hscan_len;
  u32 vscan_stop  = vscan_start + vscan_len;
  screen->setViewport(0, 0, hscan_len, vscan_len);

  i32 dy0 = vi.io.vstart;
  i32 dy1 = vi.io.vend;   if (dy1 < dy0) dy1 = vscan_stop;
  i32 dx0 = vi.io.hstart;
  i32 dx1 = vi.io.hend;

  dy0 = max(vscan_start, dy0);
  dy1 = min(vscan_stop,  dy1);
  dx0 = max(hscan_start, dx0);
  dx1 = min(hscan_stop,  dx1);

  // Undocumented VI guard-band "hardware bug" (match parallel-RDP)
  if(dx0 >= hscan_start) dx0 += 8;
  if(dx1 <  hscan_stop)  dx1 -= 7;

  // The scanline stride must be the RDP's FRAMEBUFFER width, not the VI's
  // display WIDTH register. Rogue Squadron renders 512-wide but sets the VI
  // WIDTH=1024 (presented scaled to 640 via XScale) — striding by 1024 reads
  // the wrong rows (black). Fall back to vi.io.width when the RDP hasn't set
  // a color image yet.
  u32 pitch = rdp.rdpFramebufferWidth ? (u32)rdp.rdpFramebufferWidth : (u32)vi.io.width;
  u32 fbBase = rdp.rdpFramebufferAddress ? (u32)rdp.rdpFramebufferAddress : (u32)vi.io.dramAddress;
  // Mark that the CPU fallback rendered this frame so video() presents the
  // CPU screen buffer, not the (black) Vulkan scanout for wide modes.
  io.cpuScanoutActive = 1;
  // Diagnostic: log mode switches (Rogue Squadron menu UI investigation).
  static u32 lastLogWidth = 0;
  if (::ares::n64DebugLoggingEnabled() && vi.io.width != lastLogWidth) {
    lastLogWidth = vi.io.width;
    __android_log_print(ANDROID_LOG_INFO, "PhobosVI",
      "mode: width=%u pitch=%u fbBase=0x%06x dispAddr=0x%06x xscale=%u yscale=%u ysub=%u serrate=%u",
      (unsigned)vi.io.width, (unsigned)pitch, (unsigned)fbBase,
      (unsigned)vi.io.dramAddress,
      (unsigned)vi.io.xscale, (unsigned)vi.io.yscale, (unsigned)vi.io.ysubpixel,
      (unsigned)vi.io.serrate);
  }
  if(vi.io.colorDepth == 2) {
    //15bpp
    // Diagnostic: read actual RDRAM pixels at the RDP framebuffer to see if
    // the RDP rendered there (non-zero) vs the buffer being empty (black).
    static u64 cpuDiagCount = 0;
    if (::ares::n64DebugLoggingEnabled() && cpuDiagCount++ % 60 == 0) {
      u16 p0 = rdram.ram.read<Half>(fbBase, RBusDevice::VI_DMA);
      u16 p1 = rdram.ram.read<Half>(fbBase + pitch * 2, RBusDevice::VI_DMA);
      u16 p2 = rdram.ram.read<Half>(fbBase + pitch * 2 * (240/2), RBusDevice::VI_DMA);
      // Scan for the brightest pixel in the framebuffer — if any bright
      // content exists, the RDP rendered the menu and the issue is the copy.
      u16 maxPix = 0;
      u32 maxAddr = 0;
      // [Phobos] Map the content bounding box: where is the non-black content
      // (Luke/menu) in the framebuffer? Tells us the real layout the VI must
      // sample — width, height, and horizontal/vertical position.
      u32 minX = 512, maxX = 0, minY = 480, maxY = 0;
      u32 count = 0;
      for(u32 sy = 0; sy < 480; sy += 8) {
        for(u32 sx = 0; sx < 512; sx += 8) {
          u16 d = rdram.ram.read<Half>(fbBase + (sy * pitch + sx) * 2, RBusDevice::VI_DMA);
          if((d & 0x7fff) > 0x20) {  // non-black threshold
            count++;
            minX = min(minX, sx); maxX = max(maxX, sx);
            minY = min(minY, sy); maxY = max(maxY, sy);
            if((d & 0x7fff) > (maxPix & 0x7fff)) { maxPix = d; maxAddr = (sy * pitch + sx) * 2; }
          }
        }
      }
      __android_log_print(ANDROID_LOG_INFO, "PhobosVI",
        "cpu15: addr=0x%06x pitch=%u box=[%u..%u]x[%u..%u] cnt=%u max=%04x@0x%06x",
        (unsigned)fbBase, (unsigned)pitch,
        (unsigned)minX, (unsigned)maxX, (unsigned)minY, (unsigned)maxY, (unsigned)count,
        (unsigned)maxPix, (unsigned)maxAddr);
    }
    u32 y0 = vi.io.ysubpixel + vi.io.yscale * (dy0 - vi.io.vstart);
    for(i32 dy = dy0; dy < dy1; dy++) {
      if(!io.serrate || (dy & 1) == !io.field) {
        u32 address = fbBase + (y0 >> 11) * pitch * 2;
        auto line = screen->pixels(1).data() + (dy - vscan_start) * hscan_len;
        u32 x0 = vi.io.xsubpixel + vi.io.xscale * (dx0 - vi.io.hstart);
        for(i32 dx = dx0; dx < dx1; dx++) {
          u16 data = rdram.ram.read<Half>(address + (x0 >> 10) * 2, RBusDevice::VI_DMA);
          line[dx - hscan_start] = 1 << 24 | data >> 1;
          x0 += vi.io.xscale;
        }
      }
      y0 += vi.io.yscale;
    }
  }

  if(vi.io.colorDepth == 3) {
    //24bpp
    u32 y0 = vi.io.ysubpixel + vi.io.yscale * (dy0 - vi.io.vstart);
    for(i32 dy = dy0; dy < dy1; dy++) {
      if(!io.serrate || (dy & 1) == !io.field) {
        u32 address = vi.io.dramAddress + (y0 >> 11) * pitch * 4;
        auto line = screen->pixels(1).data() + (dy - vscan_start) * hscan_len;
        u32 x0 = vi.io.xsubpixel + vi.io.xscale * (dx0 - vi.io.hstart);
        for(i32 dx = dx0; dx < dx1; dx++) {
          u32 data = rdram.ram.read<Word>(address + (x0 >> 10) * 4, RBusDevice::VI_DMA);
          line[dx - hscan_start] = data >> 8;
          x0 += vi.io.xscale;
        }
      }
      y0 += vi.io.yscale;
    }
  }
}

auto VI::power(bool reset) -> void {
  Thread::reset();
  screen->power();
  io = {};
  refreshed = false;
  clockFraction = 0;

  // VI Overclock: refresh the host pacing hint on (re)boot so a changed
  // overclock takes effect at the next reset/load.
  s32 oc = overclockPercent.load();
  if (oc <= 0) oc = 100;
  screen->refreshRateHint((Region::PAL() ? 50 : 60) * oc / 100);

  #if defined(VULKAN)
  gpuOutputValid = false;
  // Drop the stale scanout fence from the frame that was in flight when
  // the reset fired. If we keep it, the first scanoutAsync/mapScanoutRead
  // after reset waits on a fence Turnip may never signal → black screen
  // at ~2 fps (500ms timeout loop). Clearing it here lets the new frame
  // submit fresh scanout work on the current (kept) device.
  vulkan.resetScanoutFence();
  #endif
}

}
