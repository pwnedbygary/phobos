namespace ares::NeoGeo {

#include "slot.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

Card::Card(Node::Port parent) {
  node = parent->append<Node::Peripheral>("Memory Card");
  ram.allocate(2_KiB);
  // A freshly allocated card is blank SRAM. The UniBIOS/BIOS boot rejects an
  // unformatted card ("INSERT 1 MEMORY CARD" / warm-boot loop), so format it
  // with the "NEO-GEO" header the boot checks:
  //  - byte 0x08: format/directory marker (0x20-0x2F)
  //  - bytes 0x20..0x2E (even): "NEO-GEO" + 0x80
  //  - byte 0x3A: directory count / slot id (nonzero)
  ram.fill(0x00);
  static const n8 header[8] = {0x4e, 0x45, 0x4f, 0x2d, 0x47, 0x45, 0x4f, 0x80};
  ram[0x08] = 0x20;
  for(n8 i : range(8)) ram[0x20 + i * 2] = header[i];
  ram[0x3a] = 0x01;
  debugger.load(node);
}

Card::~Card() {
  debugger.unload(node);
  ram.reset();
  node.reset();
}

auto Card::power(bool reset) -> void {
}

}
