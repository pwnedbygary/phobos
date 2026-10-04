//The display: where the program's frame is in memory and how it's laid out (the screen shows it; the system reads
//it from here), and the vertical blank, when a new frame starts.

//(mode, width, height)
auto Kernel::sceDisplaySetMode() -> void {
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

//Waits for the next vertical blank.
auto Kernel::sceDisplayWaitVblankStart() -> void {
  if(!mayWait()) return;
  result(0);
  block(Wait::Vblank, 0, 0);
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
    for(u32 x = 0; x < width; x++) {
      u32 c = memory.read(bytes, display.frameBuffer + (y * display.bufferWidth + x) * bytes), r, g, b;
      if(format == 0) r = widen(c & 31, 5), g = widen(c >> 5 & 63, 6), b = widen(c >> 11 & 31, 5);
      else if(format == 1) r = widen(c & 31, 5), g = widen(c >> 5 & 31, 5), b = widen(c >> 10 & 31, 5);
      else if(format == 2) r = widen(c & 15, 4), g = widen(c >> 4 & 15, 4), b = widen(c >> 8 & 15, 4);
      else r = c & 0xff, g = c >> 8 & 0xff, b = c >> 16 & 0xff;
      pixels[y * width + x] = 0xff00'0000 | b << 16 | g << 8 | r;
    }
  }
}
