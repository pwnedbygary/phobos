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

//Waits for the next vertical blank to start (and, with callbacks, runs the thread's callbacks meanwhile). Its count
//as the wait starts goes with it: callbacks that run across the blank end the wait when they're done (resumeWait()).
auto Kernel::waitVblank(bool callbacks) -> void {
  if(!mayWait()) return;
  result(0);
  if(current) current->waitCount = vblanks;
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

auto Kernel::sceDisplayIsVblank() -> void {
  result(inVblank());
}

//The line the display is on, counted from the start of the vertical blank (as the PSP counts: up to 14 inside it).
auto Kernel::sceDisplayGetCurrentHcount() -> void {
  result(u32((cycles - (nextVblank - VblankCycles)) / LineCycles));
}

//The lines the display has gone through since power on: 286 a frame, and those of this one.
auto Kernel::sceDisplayGetAccumulatedHcount() -> void {
  result(u32(vblanks * 286 + (cycles - (nextVblank - VblankCycles)) / LineCycles));
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
