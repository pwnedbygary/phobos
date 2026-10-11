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

auto GE::transfer() -> void {
  auto stride = [](u32 word) -> u32 { u32 width = word & 0x7f8; return width > 0x400 ? 0 : width; };
  u32 source = (commands[TransferSource] & 0xff'fff0) | (commands[TransferSourceWidth] << 8 & 0xff00'0000);
  u32 destination = (commands[TransferDestination] & 0xff'fff0) | (commands[TransferDestinationWidth] << 8 & 0xff00'0000);
  u32 sourceStride = stride(commands[TransferSourceWidth]), destinationStride = stride(commands[TransferDestinationWidth]);
  u32 sourceX = commands[TransferSourcePosition] & 0x3ff, sourceY = commands[TransferSourcePosition] >> 10 & 0x3ff;
  u32 destinationX = commands[TransferDestinationPosition] & 0x3ff;
  u32 destinationY = commands[TransferDestinationPosition] >> 10 & 0x3ff;
  u32 width = (commands[TransferSize] & 0x3ff) + 1, height = (commands[TransferSize] >> 10 & 0x3ff) + 1;
  u32 bytes = commands[TransferStart] & 1 ? 4 : 2;

  //every row of each image lies between its first row's first byte and its last row's last
  std::bitset<VRAMPages> read, written;
  vramPages(source + (sourceY * sourceStride + sourceX) * bytes,
            u64(height - 1) * sourceStride * bytes + width * bytes, read);
  vramPages(destination + (destinationY * destinationStride + destinationX) * bytes,
            u64(height - 1) * destinationStride * bytes + width * bytes, written);
  drawnBefore(read, written);
  //(a hardware renderer's pixels a row lands on whole, after it's read, are memory's from then on: overwritten())
  auto overwrite = [&](u32 to, u32 size) {
    u32 physical = to & 0x1fff'ffff, copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
    if(!renderer || physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return;
    if(copy & 1) return;
    u32 offset = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    if(offset + size <= Memory::VRAMSize) renderer->overwritten(*this, offset, offset + size - 1);
  };
  std::vector<u8> row(width * bytes);
  for(u32 y = 0; y < height; y++) {
    u32 from = source + ((sourceY + y) * sourceStride + sourceX) * bytes;
    u32 to = destination + ((destinationY + y) * destinationStride + destinationX) * bytes;
    if(memory.copyOut(row.data(), from, row.size())) {
      overwrite(to, row.size());
      if(memory.copyIn(to, row.data(), row.size())) continue;
    }
    for(u32 n = 0; n < row.size(); n++) memory.write(1, to + n, memory.read(1, from + n));  //across a piece's end
  }
}
