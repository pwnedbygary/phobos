// Neo Geo CD LC8951-compatible CD controller (CDC). The 68K accesses its
// register file through two ports: $FF0100 (register address select, with
// auto-advance on most accesses) and $FF0102 (register data). Ported from
// MAME's lc89510_temp_device NeoCD path.

struct Cdc {
  static constexpr u32 BufferSize = (32 * 1024 * 2) + 2352;

  n16 reg0 = 0;          //CDC_REG0 (address select + mode)
  n16 reg1 = 0;          //CDC_REG1
  n8  wreg[16];          //LC8951RegistersW
  n8  rreg[16];          //LC8951RegistersR
  n8  buffer[BufferSize];
  n16 decode = 0;

  //registers
  auto addressWrite(n8 data) -> void;
  auto addressRead() -> n16;
  auto dataWrite(n8 data) -> void;
  auto dataRead() -> n8;
  auto reset() -> void;
  auto updateHeader() -> void;
  auto setDataAudioMode() -> void;

  //buffer access for the DMA controller
  auto initTransfer(u32 words) -> n8*;
  auto endTransfer() -> void;
};

extern Cdc cdc;