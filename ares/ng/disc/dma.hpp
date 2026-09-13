// Neo Geo CD LC8359-style DMA controller. The BIOS programs source/dest/
// count/mode registers, then writes $FF0061 with bit 6 set to start a transfer.
// Modes (from MAME neogeocd.cpp do_dma): self-address writes, RAM<->RAM
// copies, fills, and copies from the LC8951 external buffer to RAM.

struct Dma {
  n32 address1 = 0;
  n32 address2 = 0;
  n16 value1 = 0;
  n16 value2 = 0;
  n32 count = 0;
  n16 mode = 0;

  auto setAddress1Hi(n16 data) -> void { address1 = n32(data) << 16 | address1 & 0xffff; }
  auto setAddress1Lo(n16 data) -> void { address1 = n32(address1 >> 16) << 16 | data; }
  auto setAddress2Hi(n16 data) -> void { address2 = n32(data) << 16 | address2 & 0xffff; }
  auto setAddress2Lo(n16 data) -> void { address2 = n32(address2 >> 16) << 16 | data; }
  auto setCountHi(n16 data) -> void { count = n32(data) << 16 | count & 0xffff; }
  auto setCountLo(n16 data) -> void { count = n32(count >> 16) << 16 | data; }

  auto start() -> void;

  auto writeByte(n32 address, n8 data) -> void;
  auto writeWord(n32 address, n16 data) -> void;
  auto readByte(n32 address) -> n8;
  auto readWord(n32 address) -> n16;
};

extern Dma dma;