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

auto Cdd::reset() -> void {
  clock = 1;
  memory::fill(rx, sizeof(rx));
  memory::fill(tx, sizeof(tx));
  wordCount = 0;
  statusHack = 9;
  status = 0;
  curStatus = 0;
  min = sec = frame = ext = 0;
  control = 0;
  statusCdc = 0;
  curLba = 0;
  curTrack = 0;
  reg2 = 0;
  latch16 = 0;
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
  statusHack = 0x0e;
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
  if(trackIsData(curTrack)) control &= ~0x0100;
  else control |= 0x0100;
}

auto Cdd::trackIsData(u8 track) const -> bool {
  if(!track) return false;
  auto t = disc.session.track(track);
  return t ? t->isData() : false;
}
}
