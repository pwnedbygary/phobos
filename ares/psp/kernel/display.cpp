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
  result(0);
  block(Wait::Vblank, 0, 0);
}

//How many vertical blanks since power on.
auto Kernel::sceDisplayGetVcount() -> void {
  result(vblanks);
}

//The GE's memory: VRAM, 2 MiB at 0x04000000.
auto Kernel::sceGeEdramGetAddr() -> void { result(Memory::VRAMBase); }
auto Kernel::sceGeEdramGetSize() -> void { result(Memory::VRAMSize); }
