// Neo Geo CD deck driver (CDD). The BIOS communicates with the CD mechanism
// over a 4-bit serial link: 10-nibble command blocks written via $FF0162,
// clocked through $FF0164, with 10-nibble status replies read via $FF0160.
// Ported faithfully from MAME's lc89510_temp_device NeoCD path.

struct Cdd {
  //serial link
  n8  rx[10];
  n8  tx[10];
  n8  wordCount = 0;
  n1  clock = 1;
  n8  statusHack = 9;

  //cd deck state
  n16 status = 0;      //CDD_STATUS
  n16 curStatus = 0;   //SCD_STATUS
  n16 min = 0;
  n16 sec = 0;
  n16 frame = 0;
  n16 ext = 0;
  n16 control = 0;     //CDD_CONTROL
  n16 statusCdc = 0;   //SCD_STATUS_CDC
  s32 curLba = 0;
  n8  curTrack = 0;

  //misc latches
  n16 reg2 = 0;        //nff0002 (CDD/CDC control latch)
  n16 latch16 = 0;     //nff0016
  n2  region = 1;      //1 = US (English BIOS menus); 0 = Japan

  //interrupt state (level 2, vectors 0x15/0x16/0x17)
  n1 type1Pending = 0;
  n1 type2Pending = 0;
  n1 type3Pending = 0;
  n1 type1Ack = 0;
  n1 type2Ack = 0;
  n1 type3Ack = 0;
  n1 prohibitIrq = 0;

  //serial
  auto rxRead() -> n8;
  auto txWrite(n8 data) -> void;
  auto commsControl(n1 clockEdge, n1 send) -> void;
  auto reset() -> void;
  auto serialReset() -> void;

  //75Hz drive tick: raises the CDD type2 interrupt (MAME nff0002 & 0x0050)
  //and advances the sector pipeline.
  auto tick() -> void;

  //sector pipeline
  auto readLbaToBuffer() -> void;
  auto advanceReadPos() -> void;
  auto ctrlChecks() -> void;
  auto raiseType1() -> void;

  //commands
  auto import() -> bool;
  auto export() -> void;
  auto doChecksum() -> void;
  auto checkTxChecksum() -> bool;
  auto getStatus() -> void;
  auto stop() -> void;
  auto handleTocCommands() -> void;
  auto getPos() -> void;
  auto getTrackPos() -> void;
  auto getTrack() -> void;
  auto length() -> void;
  auto firstLast() -> void;
  auto getTrackAdr() -> void;
  auto getTrackType() -> void;
  auto read() -> void;
  auto seek() -> void;
  auto pause() -> void;
  auto resume() -> void;
  auto init() -> void;
  auto unknown() -> void;

  //helpers
  auto hasDisc() const -> bool;
  auto getMsfFromRegs() -> u32;
  auto msfToLba(u32 msf) const -> s32;
  auto toBcd(n8 value) const -> n16;
  auto clearResult() -> void;
  auto setDataAudioMode() -> void;
  auto trackIsData(u8 track) const -> bool;
};

extern Cdd cdd;