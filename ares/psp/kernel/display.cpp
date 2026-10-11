//The display: where the program's frame is in memory and how it's laid out (the screen shows it; the system reads
//it from here), and the vertical blank, when a new frame starts.

//(mode, width, height). The PSP's screen is the only mode (0) and its one size, 480x272: anything else is refused,
//and the display left as it was (PPSSPP's notes on the hardware).
auto Kernel::sceDisplaySetMode() -> void {
  if(arg(0) != 0) return result(ErrorInvalidMode);
  if(arg(1) != 480 || arg(2) != 272) return result(ErrorInvalidSize);
  display.mode = arg(0);
  display.width = arg(1);
  display.height = arg(2);
  result(0);
}

//(address, buffer width in pixels, pixel format (0: 16-bit 5650, 1: 5551, 2: 4444, 3: 32-bit 8888), when to switch)
auto Kernel::sceDisplaySetFrameBuf() -> void {
  display.frameBuffer = arg(0);
  display.bufferWidth = arg(1);
  display.pixelFormat = arg(2);
  result(0);
}

//(where to put the address, the buffer width and the pixel format; which: 0 the one shown now, 1 the next)
auto Kernel::sceDisplayGetFrameBuf() -> void {
  if(arg(0)) memory.write(4, arg(0), display.frameBuffer);
  if(arg(1)) memory.write(4, arg(1), display.bufferWidth);
  if(arg(2)) memory.write(4, arg(2), display.pixelFormat);
  result(0);
}

//The display's timing (PPSSPP's notes on the hardware): a frame is 286 lines of 525 dots at 9 MHz, 59.94 a second,
//of which 272 lines are shown. The vertical blank starts each frame (the kernel's nextVblank is when the next starts)
//and lasts about 0.77 ms (pspautotests' display/vblanklen measured 730 to 770 microseconds from the end of a wait).

//Whether the display is in its vertical blank now.
auto Kernel::inVblank() const -> bool {
  return cycles - (nextVblank - VblankCycles) < VblankLength;
}

//Waits for the next vertical blank to start, or the count-th from now (and, with callbacks, runs the thread's
//callbacks meanwhile). The count of blanks it wakes at goes with it: callbacks that run across that blank end the
//wait when they're done (resumeWait()).
auto Kernel::waitVblank(bool callbacks, u32 count) -> void {
  if(!mayWait()) return;
  result(0);
  if(current) current->waitCount = vblanks + count;
  block(Wait::Vblank, 0, 0, 0, callbacks);
}

auto Kernel::sceDisplayWaitVblankStart() -> void {
  waitVblank(false);
}

auto Kernel::sceDisplayWaitVblankStartCB() -> void {
  waitVblank(true);
}

//Waits for the vertical blank: not at all if it's in it now (returning 1), else until the next starts (0).
auto Kernel::sceDisplayWaitVblank() -> void {
  if(inVblank()) return result(1);
  waitVblank(false);
}

auto Kernel::sceDisplayWaitVblankCB() -> void {
  if(inVblank()) {
    result(1);
    return callbacksOnReturn(true);
  }
  waitVblank(true);
}

//(count): waits for the count-th vertical blank to start from now, as pspautotests' display/vblankmulti recorded (3
//returning at the third, every time); a count of 0 or less is INVALID_VALUE, before the refusals of waiting where
//nothing may wait (intr/waits). 0x40f1469c is the one without callbacks, 0x77ed8b3a the CB one; vblankmulti's titles
//have them the other way round, as its own source calls them.
auto Kernel::sceDisplayWaitVblankStartMulti() -> void {
  if(s32(arg(0)) <= 0) return result(ErrorInvalidValue);
  waitVblank(false, arg(0));
}

auto Kernel::sceDisplayWaitVblankStartMultiCB() -> void {
  if(s32(arg(0)) <= 0) return result(ErrorInvalidValue);
  waitVblank(true, arg(0));
}

auto Kernel::sceDisplayIsVblank() -> void {
  result(inVblank());
}

//Whether a frame buffer is shown (pspdisplay.h): 1 with one set, 0 with none (0, the display off), as pspautotests'
//display/isstate recorded. (It recorded 1 still just after a frame buffer of 0 was set for the next frame; frame
//buffers here change at once.) Power Stone Collection asks.
auto Kernel::sceDisplayIsForeground() -> void {
  result(display.frameBuffer != 0);
}

//The lines the display has gone through since this frame's vertical blank started.
auto Kernel::hcountLines() const -> u32 {
  return u32((cycles - (nextVblank - VblankCycles)) / LineCycles);
}

//The line the display is on, counted from the start of the vertical blank: pspautotests' display/vblankphase
//recorded the blank's interrupt at the end of line 285, a handler reading line 0. (display/hcount's lowest line just
//after a wait for a blank, 1, and highest inside the blank, 14, are a waiting thread getting the CPU some 81
//microseconds after the interrupt and the blank lasting some 818 from it, as vblankphase measured; here a waiter runs
//at once and the blank lasts 0.77 ms, so 0 and 13.)
auto Kernel::sceDisplayGetCurrentHcount() -> void {
  result(hcountLines());
}

//The lines the display has gone through since power on: 286 a frame, and those of this one; counted on from where
//sceDisplayAdjustAccumulatedHcount set it, if it did, and 31 bits wide.
auto Kernel::sceDisplayGetAccumulatedHcount() -> void {
  result((display.hcountBase + u32(vblanks * 286 + hcountLines())) & 0x7fff'ffff);
}

//(count): the accumulated count of lines is this now, and counts on from it at the next line. As pspautotests'
//display/hcount and hcountwrap recorded: a negative count is INVALID_VALUE (0x7fffffff the largest taken), and from
//0x7fffffff the count goes on to 0 as the next line starts (read straight after setting it, sometimes 0 already).
auto Kernel::sceDisplayAdjustAccumulatedHcount() -> void {
  if(s32(arg(0)) < 0) return result(ErrorInvalidValue);
  display.hcountBase = arg(0) - u32(vblanks * 286 + hcountLines());
  result(0);
}

//The display's frame rate, a float: 59.94.
auto Kernel::sceDisplayGetFramePerSec() -> void {
  resultFloat(59.94f);
}

//How many vertical blanks since power on.
auto Kernel::sceDisplayGetVcount() -> void {
  result(vblanks);
}

//What the screen shows: display.width x display.height pixels (480x272) of the frame buffer the program set, as 8888
//with red in the low byte and alpha 255 (the LCD has no alpha). 16-bit pixels widen by repeating each channel's top
//bits. Black if the program hasn't set a frame buffer, or has turned the display off (a frame buffer of 0).
auto Kernel::picture(std::vector<u32>& pixels) -> void {
  u32 width = display.width ? display.width : 480, height = display.height ? display.height : 272;
  pixels.assign(width * height, 0xff00'0000);
  if(!display.frameBuffer) return;
  u32 format = display.pixelFormat & 3, bytes = format == 3 ? 4 : 2;
  auto widen = [](u32 value, u32 bits) { return value << (8 - bits) | value >> (2 * bits - 8); };
  for(u32 y = 0; y < height; y++) {
    //(a row's bytes straight from where they are when they're all in one place, else a pixel at a time)
    u32 row = display.frameBuffer + y * display.bufferWidth * bytes;
    const u8* bytesOf = memory.pointer(row, width * bytes);
    for(u32 x = 0; x < width; x++) {
      u32 c, r, g, b;
      if(!bytesOf) {
        c = memory.read(bytes, row + x * bytes);
      } else {
        const u8* at = bytesOf + x * bytes;
        c = bytes == 4 ? at[0] | at[1] << 8 | at[2] << 16 | u32(at[3]) << 24 : at[0] | at[1] << 8;
      }
      if(format == 0) r = widen(c & 31, 5), g = widen(c >> 5 & 63, 6), b = widen(c >> 11 & 31, 5);
      else if(format == 1) r = widen(c & 31, 5), g = widen(c >> 5 & 31, 5), b = widen(c >> 10 & 31, 5);
      else if(format == 2) r = widen(c & 15, 4), g = widen(c >> 4 & 15, 4), b = widen(c >> 8 & 15, 4);
      else r = c & 0xff, g = c >> 8 & 0xff, b = c >> 16 & 0xff;
      pixels[y * width + x] = 0xff00'0000 | b << 16 | g << 8 | r;
    }
  }
}
