namespace ares::NeoGeo {

Cdd cdd;

auto Cdd::rxRead() -> n8 {
  n8 ret = 0;
  if(wordCount < 10) ret = rx[wordCount] & 0x0f;
  if(clock) ret |= 0x10;
  return ret;
}

auto Cdd::txWrite(n8 data) -> void {
  if(wordCount < 10) tx[wordCount] = data & 0x0f;
}

auto Cdd::commsControl(n1 clockEdge, n1 send) -> void {
  if(clockEdge && !clock) {
    wordCount++;
    if(wordCount >= 10) {
      wordCount = 0;
      if(send) {
        if(tx[0]) {
          if(!import()) return;
          export();
        }
      }
    }
  }
  clock = clockEdge;
}

auto Cdd::tick() -> void {
  //CDD settle: after a Stop the tray mechanism takes ~2s to settle on real
  //hardware. Report 0x0E ("tray moving") during that window — the BIOS boot
  //disc-check requires it — then transition to 0x09 (stopped) so the game
  //loader's status poll sees a settled drive and proceeds to issue Play/read.
  //(MAME's lc89510 NeoCD path never settles — its neocd driver is
  //MACHINE_NOT_WORKING. libretro neocd uses CdStopped=0x90 = nibble 9.)
  if(settleCounter) {
    if(--settleCounter == 0) {
      bool disc = hasDisc();
      if(disc) {
        statusHack = 9;       //Reading TOC (disc present, drive settled)
        curStatus = 0x0900;   //CDD_STOPPED — disc present
        status = 0x0900;
        control |= 0x0100;    //data mode
        //Pragmatic HLE: on real hardware the CDD's disc-detect report path
        //sets $10F656 bit 0 ("disc detected", BIOS site $C0BD98).  From
        //there the BIOS's own boot-init ($C0CEC4: btst #0,$10F656) runs
        //the full disc/TOC phase — which populates the loader's position
        //regs and sets bit 7 itself ($C0D2E4) before the auto-boot gate
        //($C14C40).  Our CDD never delivers that report, so we poke bit 0
        //at settle (drive settled + disc present) and let the BIOS take it
        //from there.  Poking bit 7 directly was wrong: it auto-booted the
        //loader BEFORE the TOC phase, so the loader's READ carried $FF
        //positions (lba=754887 garbage → DISC I/O ERROR).
        //
        //Only poke while bit 7 (disc-ready) is still clear: once the BIOS's
        //boot-init completes it sets bit 7 itself and clears bit 0, and any
        //further settles (the loader menu keeps issuing STOPs) must NOT
        //re-trigger the whole disc check — otherwise the screen cycles
        //"WAIT FOR A MOMENT" every ~2s forever.
        {
          auto& w = system.wram[(0x108000 + 0x7656) >> 1];
          if(!(w.byte(1) & 0x80)) w.byte(1) |= 0x01;
        }
      } else {
        statusHack = 0;       //Stop mode (no disc)
        curStatus = 0x0000;
        status = 0x0000;
      }
      export();               //refresh the reply the loader polls
    }
  }

  //On real hardware the CDD autonomously fills rx[] with its current status
  //every ~64Hz (~75Hz here). The BIOS reads rx[] via $FF0160 at any time.
  //MAME calls CDD_Export() on every CDD timer tick for this reason.
  export();

  //CDD "access" interrupt: fires continuously at 75Hz once enabled by the
  //BIOS writing nff0002 (REG_FF0002) with bits 4+6 set (MAME: nff0002 & 0x0050).
  if(reg2 & 0x0050 && !prohibitIrq) {
    if(!type2Pending) {
      type2Pending = 1;
      cpu.raise(CPU::Interrupt::CDD);
    }
  }

  //sector pipeline: stream one sector per tick while a read is active.
  //The tick itself runs at 75 * readSpeed Hz (lspc.cpp), so the loader
  //consumes sectors N× faster while keeping the exact per-sector protocol
  //(one decoder IRQ per sector; header always matches the sector being
  //consumed). Ring-buffer safety net: never write a sector that would wrap
  //onto data the BIOS hasn't DMA'd out of the 0x8000-byte PT/DAC ring.
  if(statusCdc & 0x01) {
    u16 pt  = (u32)cdc.wreg[0xc] | (u32)cdc.wreg[0xd] << 8;
    u16 dac = (u32)cdc.wreg[0x4] | (u32)cdc.wreg[0x5] << 8;
    u32 ahead = (pt + 2352 - dac) & 0x7fff;
    if(ahead <= 0x7fff - 2352) readLbaToBuffer();
  }
}

auto Cdd::readLbaToBuffer() -> void {
  bool dataTrack = control & 0x0100;
  u8 sector[2352] = {};
  auto raw = disc.readSectorRaw(curLba);
  bool rawOk = !raw.empty();
  if(dataTrack) {
    if(rawOk) memory::copy(sector, raw.data(), (u32)raw.size() < 2352 ? (u32)raw.size() : 2352u);
    //mode1: 12 sync + 4 header + 2048 data + 288 ecc
  } else {
    if(rawOk) memory::copy(sector, raw.data(), (u32)raw.size() < 2352 ? (u32)raw.size() : 2352u);
  }

  cdc.updateHeader();

  if(!dataTrack) {
    advanceReadPos();
  }

  if(cdc.wreg[0xa] & 0x80) {  //REG_W_CTRL0
    if(cdc.wreg[0xa] & 0x04) {
      if(dataTrack) {
        advanceReadPos();
        u16 pt = (u32)cdc.wreg[0xc] | (u32)cdc.wreg[0xd] << 8;
        memory::copy(&cdc.buffer[pt + 4], sector + 16, 2048);
        cdc.buffer[pt + 0] = cdc.rreg[4];  //HEAD0
        cdc.buffer[pt + 1] = cdc.rreg[5];  //HEAD1
        cdc.buffer[pt + 2] = cdc.rreg[6];  //HEAD2
        cdc.buffer[pt + 3] = cdc.rreg[7];  //HEAD3
        //NeoCDZ protection: some titles (e.g. samsprg) are not recognized
        //unless the "Copyright by SNK" marker is patched (MAME hack)
        if(cdc.buffer[pt + 4 + 64] == 'g' && !memcmp(&cdc.buffer[pt + 4], "Copyright by SNK", 16)) {
          cdc.buffer[pt + 4 + 64] = 'f';
        }
      } else {
        u16 pt = (u32)cdc.wreg[0xc] | (u32)cdc.wreg[0xd] << 8;
        memory::copy(&cdc.buffer[pt], sector, 2352);
      }
    }
    ctrlChecks();
  }
}

auto Cdd::advanceReadPos() -> void {
  curLba++;
  u16 pt = (u32)cdc.wreg[0xc] | (u32)cdc.wreg[0xd] << 8;
  u16 wa = (u32)cdc.wreg[0x8] | (u32)cdc.wreg[0x9] << 8;
  wa += 2352;
  pt += 2352;
  wa &= 0x7fff;
  pt &= 0x7fff;
  cdc.wreg[0xc] = pt & 0xff;
  cdc.wreg[0xd] = (pt >> 8) & 0xff;
  cdc.wreg[0x8] = wa & 0xff;
  cdc.wreg[0x9] = (wa >> 8) & 0xff;
}

auto Cdd::ctrlChecks() -> void {
  cdc.rreg[0xc] = 0x80;  //STAT0
  cdc.rreg[0xe] = (cdc.wreg[0xa] & 0x10) ? (cdc.wreg[0xb] & 0x08) : (cdc.wreg[0xb] & 0x0c);  //STAT2
  cdc.rreg[0xf] = (cdc.wreg[0xa] & 0x02) ? 0x20 : 0x00;  //STAT3

  if(cdc.wreg[1] & 0x20) {  //IFCTRL
    raiseType1();
    cdc.rreg[1] &= ~0x20;   //IFSTAT
    cdc.decode = 0;
  }
}

auto Cdd::raiseType1() -> void {
  //LC8951 decoder-complete → BIOS vector $15 (0xC0A40A), NOT $17.
  //MAME: scd_ctrl_checks() → m_type1_interrupt_callback() → irq_update picks
  //vector 0x15 when ack bit 0x20 is unset.  The BIOS stub at $C0A40A acks
  //$FF000F=0x20 and calls the CDD access machine $C0E99E — which drives
  //$7688/$76B6/$76BC (the boot wait-loop exit counters).  Sending this
  //event to vector $17 (stub $C0A44E, "unused") starves the access machine
  //and the loader sits in its wait loop forever.  The pending flag mapping
  //to vectors is: type3=21(0x15)=$C0A40A, type2=22(0x16)=$C0A42C,
  //type1=23(0x17)=$C0A44E (matches the BIOS ROM vector table).
  type3Pending = 1;
  cpu.raise(CPU::Interrupt::CDD);
}

auto Cdd::serialReset() -> void {
  //CDD serial link reset ($FF0181 active low): re-sync the nibble handshake
  clock = 1;
  memory::fill(rx, sizeof(rx));
  memory::fill(tx, sizeof(tx));
  wordCount = 0;
  statusHack = 9;
}

auto Cdd::reset() -> void {
  serialReset();
  status = 0;
  curStatus = 0;
  min = sec = frame = ext = 0;
  control = 0;
  statusCdc = 0;
  curLba = 0;
  curTrack = 0;
  reg2 = 0;
  latch16 = 0;
  type1Pending = 0;
  type2Pending = 0;
  type3Pending = 0;
  prohibitIrq = 0;
}

auto Cdd::import() -> bool {
  if(!checkTxChecksum()) return false;
  switch(tx[0]) {
  case 0x0: getStatus();      break;  //status
  case 0x1: stop();           break;  //stop all
  case 0x2: handleTocCommands(); break;  //get TOC
  case 0x3: read();           break;  //read (seek + play)
  case 0x4: seek();           break;  //seek
  case 0x6: pause();          break;  //stop
  case 0x7: resume();         break;  //resume
  case 0xa: init();           break;  //init
  default:  unknown();        break;
  }
  return true;
}

auto Cdd::export() -> void {
  rx[0] = statusHack;
  rx[1] = status & 0x00ff;
  rx[2] = (min & 0xff00) >> 8;
  rx[3] = min & 0x00ff;
  rx[4] = (sec & 0xff00) >> 8;
  rx[5] = sec & 0x00ff;
  rx[6] = (frame & 0xff00) >> 8;
  rx[7] = frame & 0x00ff;
  rx[8] = ext & 0x00ff;
  doChecksum();
}

auto Cdd::doChecksum() -> void {
  n8 checksum = rx[0] + rx[1] + rx[2] + rx[3] + rx[4] + rx[5] + rx[6] + rx[7] + rx[8];
  checksum += 0x5;  //NeoCD quirk
  checksum &= 0x0f;
  checksum ^= 0x0f;
  rx[9] = checksum;
}

auto Cdd::checkTxChecksum() -> bool {
  n8 checksum = tx[0] + tx[1] + tx[2] + tx[3] + tx[4] + tx[5] + tx[6] + tx[7] + tx[8];
  checksum += 0x5;  //NeoCD quirk
  checksum &= 0x0f;
  checksum ^= 0x0f;
  return (checksum & 0x0f) == (tx[9] & 0x0f);
}

auto Cdd::getStatus() -> void {
  n8 s = status & 0x0f00;
  if(s == 0x0200 || s == 0x0700 || s == 0x0e00) {
    status = (curStatus & 0xff00) | (status & 0x00ff);
  }
}

auto Cdd::stop() -> void {
  clearResult();
  statusCdc &= ~0x01;   //stop CDC read
  curStatus = 0x0900;   //CDD_STOPPED
  status = 0x0000;
  control |= 0x0100;    //data mode
  statusHack = 0x0e;    //"tray moving" — required by the boot disc-check
  //~2s of tray settling in wall time: ticks come at 75*readSpeed Hz, so the
  //counter scales inversely with the speed multiplier.
  settleCounter = (n16)(150 * (readSpeed ? readSpeed : 1));
}

auto Cdd::handleTocCommands() -> void {
  n8 subcmd = tx[3];
  status = (status & 0xff00) | subcmd;
  switch(subcmd) {
  case 0x0: getPos();       break;  //current position
  case 0x1: getTrackPos();  break;  //position within track
  case 0x2: getTrack();     break;  //current track
  case 0x3: length();       break;  //disc length
  case 0x4: firstLast();    break;  //first/last track
  case 0x5: getTrackAdr();  break;  //track address
  case 0x6: getTrackType(); break;  //track type (NeoCD)
  case 0x7: getDiscRecognition(); break;  //CDZ disc recognition
  default:  getStatus();    break;
  }
}

auto Cdd::getPos() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  auto msf = CD::MSF::fromLBA(curLba);
  min   = toBcd(msf.minute);
  sec   = toBcd(msf.second);
  frame = toBcd(msf.frame);
}

auto Cdd::getTrackPos() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  auto track = disc.session.inTrack(curLba);
  if(!track) return;
  auto startLba = disc.session.track(*track)->index(1)->lba;
  auto msf = CD::MSF::fromLBA(curLba - startLba - 150);
  min   = toBcd(msf.minute);
  sec   = toBcd(msf.second);
  frame = toBcd(msf.frame);
}

auto Cdd::getTrack() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  auto track = disc.session.inTrack(curLba);
  curTrack = track ? *track : 0;
  min = toBcd(curTrack);
}

auto Cdd::length() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  auto startLba = disc.session.track(disc.session.lastTrack)->index(1)->lba;
  auto msf = CD::MSF::fromLBA(startLba - 150);
  min   = toBcd(msf.minute);
  sec   = toBcd(msf.second);
  frame = toBcd(msf.frame);
}

auto Cdd::firstLast() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  min = 1;
  sec = toBcd(disc.session.lastTrack);
}

auto Cdd::getTrackAdr() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  n8 track = (tx[5] & 0x0f) + (tx[4] & 0x0f) * 10;
  n8 last = disc.session.lastTrack;
  if(track > last) track = last;
  if(track < 1) track = 1;
  status |= curStatus;
  auto startLba = disc.session.track(track)->index(1)->lba;
  auto msf = CD::MSF::fromLBA(startLba);
  min   = toBcd(msf.minute);
  sec   = toBcd(msf.second);
  frame = toBcd(msf.frame);
  ext = track % 10;
  if(trackIsData(track)) frame |= 0x0800;
}

auto Cdd::getTrackType() -> void {
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  n8 track = (tx[5] & 0x0f) + (tx[4] & 0x0f) * 10;
  n8 last = disc.session.lastTrack;
  if(track > last) track = last;
  if(track < 1) track = 1;
  status |= curStatus;
  if(trackIsData(track)) {
    ext = 0x08;
    frame |= 0x0800;
  }
}

auto Cdd::read() -> void {
  clearResult();
  curLba = msfToLba(getMsfFromRegs());
  if(!hasDisc()) return;
  auto track = disc.session.inTrack(curLba);
  curTrack = track ? *track : 0;
  curStatus = 0x0100;   //CDD_PLAYINGCDDA
  status = 0x0102;
  setDataAudioMode();
  min = toBcd(curTrack);
  statusCdc |= 0x01;    //set CDC read
  statusHack = 1;
}

auto Cdd::seek() -> void {
  clearResult();
  curLba = msfToLba(getMsfFromRegs());
  if(!hasDisc()) return;
  auto track = disc.session.inTrack(curLba);
  curTrack = track ? *track : 0;
  statusCdc &= ~0x01;   //stop CDC read
  curStatus = 0x0400;   //CDD_READY
  status = 0x0200;
  setDataAudioMode();
}

auto Cdd::pause() -> void {
  clearResult();
  statusCdc &= ~0x01;
  curStatus = 0x0400;
  status = curStatus;
  control |= 0x0100;
  statusHack = 4;
}

auto Cdd::resume() -> void {
  clearResult();
  statusCdc &= ~0x01;
  if(!hasDisc()) return;
  auto track = disc.session.inTrack(curLba);
  curTrack = track ? *track : 0;
  curStatus = 0x0100;
  status = 0x0102;
  setDataAudioMode();
  min = toBcd(curTrack);
  statusCdc |= 0x01;
  statusHack = 1;
}

auto Cdd::init() -> void {
  clearResult();
  statusCdc &= ~0x01;
  curStatus = 0x0400;
  status = curStatus;
  sec = 1;
  frame = 1;
}

auto Cdd::getDiscRecognition() -> void {
  //CDZ copy-protection / disc recognition (libretro neocd: "subcommand 7").
  //Possible values: 2, 5, E, F. Most games want 2 (Samurai Shodown RPG
  //included); Twinkle Star Sprites wants F.
  clearResult();
  status &= 0xff;
  if(!hasDisc()) return;
  status |= curStatus;
  min = toBcd(2);
}

auto Cdd::unknown() -> void {
  clearResult();
  status = curStatus;
  statusHack = 9;
}

auto Cdd::hasDisc() const -> bool {
  return (bool)disc.fd && disc.fd->size() >= 2448 && disc.session.lastTrack >= 1;
}

auto Cdd::getMsfFromRegs() -> u32 {
  u32 msf = 0;
  msf |= ((tx[3] & 0x0f) + (tx[2] & 0x0f) * 10) << 16;
  msf |= ((tx[5] & 0x0f) + (tx[4] & 0x0f) * 10) << 8;
  msf |= ((tx[7] & 0x0f) + (tx[6] & 0x0f) * 10) << 0;
  return msf;
}

auto Cdd::msfToLba(u32 msf) const -> s32 {
  return ((msf >> 16) * 60 + ((msf >> 8) & 0xff)) * 75 + (msf & 0xff) - 150;
}

auto Cdd::toBcd(n8 value) const -> n16 {
  if(value > 99) value = 99;
  return (value / 10) << 8 | value % 10;
}

auto Cdd::clearResult() -> void {
  min = sec = frame = ext = 0;
}

auto Cdd::setDataAudioMode() -> void {
  if(trackIsData(curTrack)) control |= 0x0100;
  else control &= ~0x0100;
}

auto Cdd::trackIsData(u8 track) const -> bool {
  if(!track) return false;
  auto t = disc.session.track(track);
  return t ? t->isData() : false;
}

}
