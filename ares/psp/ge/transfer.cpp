//Block transfers: TRANSFER_START copies a rectangle of pixels from one image in memory to another (the GU library's
//sceGuCopyImage), such as a picture the CPU made into the frame buffer. The commands before it give each image's
//address and width in pixels (TRANSFER_SRC/_DST and _SRC_W/_DST_W: the address's top 8 bits ride in the width's bits
//16-23), where the rectangle starts in each (bits 0-9 x, bits 10-19 y), and its size less one. TRANSFER_START's bit
//0 says the pixels' size: 4 bytes if set, else 2. (The masks: addresses on 16 bytes, widths a multiple of 8 pixels and
//a width over 1024 taken as 0, are as PPSSPP has them, from tests on the PSP.)
//
//The copy comes after the primitives before it in the list, and drawing is batched (threads.cpp): what of that drawing
//reaches what the copy reads or writes is drawn first (drawnBefore()), and the rest goes on being drawn meanwhile.

//The pages of VRAM's 2 MiB (as a batch's pending pages count them) that size bytes from address reach, into pages:
//each access takes an address's low 29 bits. None for bytes all outside VRAM (only VRAM is drawn into); those it
//reaches in one of its four copies (every 16 KiB touched, through a copy that rearranges each 16 KiB: memory.hpp);
//and every page for bytes running from one copy into the next, or past either end of VRAM's window, or round the end
//of the addresses (no game copies so, and no page is missed).
static auto vramPages(u32 address, u64 size, std::bitset<GE::VRAMPages>& pages) -> void {
  u64 low = address & 0x1fff'ffff, high = low + size;  //(past the last)
  u64 window = Memory::VRAMBase + Memory::VRAMWindow;
  if(high <= 0x2000'0000 && (high <= Memory::VRAMBase || low >= window)) return;
  u64 copy = (low - Memory::VRAMBase) / Memory::VRAMSize;
  if(low < Memory::VRAMBase || high > Memory::VRAMBase + (copy + 1) * Memory::VRAMSize) return void(pages.set());
  u32 first = low - Memory::VRAMBase - copy * Memory::VRAMSize, last = first + size - 1;
  if(copy & 1) first &= ~0x3fffu, last |= 0x3fff;
  for(u32 page = first >> 12; page <= last >> 12; page++) pages.set(page);
}

//The transfer TRANSFER_START's commands ask for (Transfer): each row's bytes from (from(y)) and to (to(y)), and the
//pages of VRAM it reads and writes (every row of each image lies between its first row's first byte and its last
//row's last).
auto GE::transferAsked() const -> Transfer {
  auto stride = [](u32 word) -> u32 { u32 width = word & 0x7f8; return width > 0x400 ? 0 : width; };
  Transfer t;
  t.source = (commands[TransferSource] & 0xff'fff0) | (commands[TransferSourceWidth] << 8 & 0xff00'0000);
  t.destination = (commands[TransferDestination] & 0xff'fff0) | (commands[TransferDestinationWidth] << 8 & 0xff00'0000);
  t.sourceStride = stride(commands[TransferSourceWidth]);
  t.destinationStride = stride(commands[TransferDestinationWidth]);
  t.sourceX = commands[TransferSourcePosition] & 0x3ff, t.sourceY = commands[TransferSourcePosition] >> 10 & 0x3ff;
  t.destinationX = commands[TransferDestinationPosition] & 0x3ff;
  t.destinationY = commands[TransferDestinationPosition] >> 10 & 0x3ff;
  t.width = (commands[TransferSize] & 0x3ff) + 1, t.height = (commands[TransferSize] >> 10 & 0x3ff) + 1;
  t.bytes = commands[TransferStart] & 1 ? 4 : 2;
  vramPages(t.source + (t.sourceY * t.sourceStride + t.sourceX) * t.bytes,
            u64(t.height - 1) * t.sourceStride * t.bytes + t.width * t.bytes, t.read);
  vramPages(t.destination + (t.destinationY * t.destinationStride + t.destinationX) * t.bytes,
            u64(t.height - 1) * t.destinationStride * t.bytes + t.width * t.bytes, t.written);
  return t;
}

//(a hardware renderer's pixels a row lands on whole, after it's read, are memory's from then on: overwritten(). The
//GE drawing's: threaded, only the render GE's transferChecks() asks)
alwaysinline auto GE::transferOverwrite(u32 to, u32 size) -> void {
  u32 physical = to & 0x1fff'ffff, copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
  if(!renderer || physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return;
  if(copy & 1) return;
  u32 offset = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  if(offset + size <= Memory::VRAMSize) renderer->overwritten(*this, offset, offset + size - 1);
}

auto GE::transfer() -> void {
  if(thread) return transferThreaded();  //(threaded: the copy here, the GE's checks the render GE's: thread.cpp)
  auto t = transferAsked();
  u32 source = t.source, destination = t.destination, sourceStride = t.sourceStride;
  u32 destinationStride = t.destinationStride, sourceX = t.sourceX, sourceY = t.sourceY;
  u32 destinationX = t.destinationX, destinationY = t.destinationY, width = t.width, height = t.height;
  u32 bytes = t.bytes;
  if(pspTiming) oweTransfer(width, height, bytes, source, destination);  //(the GE's time: Cost)
  drawnBefore(t.read, t.written);
  std::vector<u8> row(width * bytes);
  for(u32 y = 0; y < height; y++) {
    u32 from = source + ((sourceY + y) * sourceStride + sourceX) * bytes;
    u32 to = destination + ((destinationY + y) * destinationStride + destinationX) * bytes;
    if(memory.copyOut(row.data(), from, row.size())) {
      transferOverwrite(to, row.size());
      if(memory.copyIn(to, row.data(), row.size())) continue;
    }
    for(u32 n = 0; n < row.size(); n++) memory.write(1, to + n, memory.read(1, from + n));  //across a piece's end
  }
}

//(the render GE) transfer()'s checks, in its order, the copy the kernel's GE's (thread.cpp): what's drawn first, and
//for each row its source's pointer() checks, the renderer's overwritten(), its destination's and what changed() tells
//the GE, or a byte at a time where the row's bytes aren't all reached.
auto GE::transferChecks() -> void {
  auto t = transferAsked();
  drawnBefore(t.read, t.written);
  u32 size = t.width * t.bytes;
  for(u32 y = 0; y < t.height; y++) {
    u32 from = t.source + ((t.sourceY + y) * t.sourceStride + t.sourceX) * t.bytes;
    u32 to = t.destination + ((t.destinationY + y) * t.destinationStride + t.destinationX) * t.bytes;
    if(memory.copyChecks(from, size, true)) {
      transferOverwrite(to, size);
      if(memory.copyChecks(to, size)) continue;
    }
    for(u32 n = 0; n < size; n++) {
      (void)memory.pointer(from + n, 1, true);  //(read()'s check: none in main RAM)
      if(u32 offset; memory.ramOffset(to + n, 1, offset)) memory.heard(to + n, 1);
      else memory.storeChecks(1, to + n);
    }
  }
}
