namespace ares::NeoGeo {

Cdc cdc;

//register indices
enum : u8 { W_SBOUT, W_IFCTRL, W_DBCL, W_DBCH, W_DACL, W_DACH, W_DTTRG, W_DTACK, W_WAL, W_WAH, W_CTRL0, W_CTRL1, W_PTL, W_PTH, W_CTRL2, W_RESET };
enum : u8 { R_COMIN, R_IFSTAT, R_DBCL, R_DBCH, R_HEAD0, R_HEAD1, R_HEAD2, R_HEAD3, R_PTL, R_PTH, R_WAL, R_WAH, R_STAT0, R_STAT1, R_STAT2, R_STAT3 };

auto Cdc::addressWrite(n8 data) -> void {
  reg0 = data;
}

auto Cdc::addressRead() -> n16 {
  return reg0;
}

auto Cdc::dataRead() -> n8 {
  auto reg = reg0 & 0xf;
  n8 ret = 0;

  n16 decoderegs = 0x73f2;
  if(decoderegs >> reg & 1) decode |= 1 << reg;

  reg0 = (reg0 & 0xfff0) | ((reg + 1) & 0xf);

  switch(reg) {
  case R_COMIN:  ret = 0;                          break;
  case R_IFSTAT: ret = rreg[R_IFSTAT];             break;
  case R_DBCL:   ret = wreg[W_DBCL];               break;
  case R_DBCH:   ret = wreg[W_DBCH];               break;
  case R_HEAD0:  ret = rreg[R_HEAD0];              break;
  case R_HEAD1:  ret = rreg[R_HEAD1];              break;
  case R_HEAD2:  ret = rreg[R_HEAD2];              break;
  case R_HEAD3:  ret = rreg[R_HEAD3];              break;
  case R_PTL:    ret = wreg[W_PTL];                break;
  case R_PTH:    ret = wreg[W_PTH];                break;
  case R_WAL:    ret = wreg[W_WAL];                break;
  case R_WAH:    ret = wreg[W_WAH];                break;
  case R_STAT0:  ret = rreg[R_STAT0];              break;
  case R_STAT1:  ret = rreg[R_STAT1];              break;
  case R_STAT2:  ret = rreg[R_STAT2];              break;
  case R_STAT3:  ret = rreg[R_STAT3];              break;
  }

  __android_log_print(ANDROID_LOG_INFO, "NGCD", "CDC read reg %02x -> %02x", reg, ret);
  return ret;
}

auto Cdc::dataWrite(n8 data) -> void {
  auto reg = reg0 & 0xf;
  {
  }

  static const n1 changers0[16] = {1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0};
  if(changers0[reg]) reg0 = (reg0 & 0xfff0) | ((reg + 1) & 0xf);

  __android_log_print(ANDROID_LOG_INFO, "NGCD", "CDC write reg %02x <- %02x", reg, data);

  switch(reg) {
  case W_SBOUT:
    wreg[W_SBOUT] = data;
    break;
  case W_IFCTRL:
    wreg[W_IFCTRL] = data;
    if(!(wreg[W_IFCTRL] & 0x02)) {
      wreg[W_DBCL] = 0;
      wreg[W_DBCH] = 0;
      cdd.statusCdc &= ~0x08;
      rreg[R_IFSTAT] |= 0x08;
    }
    break;
  case W_DBCL: wreg[W_DBCL] = data; break;
  case W_DBCH: wreg[W_DBCH] = data; break;
  case W_DACL: wreg[W_DACL] = data; break;
  case W_DACH: wreg[W_DACH] = data; break;
  case W_DTTRG:
    //NeoCD: no DMA engine behind DTTRG; the host pulls the buffer directly.
    wreg[W_DTTRG] = 0xff;
    rreg[R_IFSTAT] &= ~0x08;
    break;
  case W_DTACK:
    rreg[R_IFSTAT] |= 0x40;
    break;
  case W_WAL: wreg[W_WAL] = data; break;
  case W_WAH: wreg[W_WAH] = data; break;
  case W_CTRL0: wreg[W_CTRL0] = data; break;
  case W_CTRL1: wreg[W_CTRL1] = data; break;
  case W_PTL: wreg[W_PTL] = data; break;
  case W_PTH: wreg[W_PTH] = data; break;
  case W_CTRL2: wreg[W_CTRL2] = data; break;
  case W_RESET: reset(); break;
  }
}

auto Cdc::initTransfer(u32 words) -> n8* {
  if(!wreg[W_DTTRG]) return nullptr;
  if(!(wreg[W_IFCTRL] & 0x02)) return nullptr;
  auto dac = (wreg[W_DACH] << 8) | wreg[W_DACL];
  if(dac + (words << 1) > BufferSize) return nullptr;
  return buffer + dac;
}

auto Cdc::endTransfer() -> void {
  wreg[W_DTTRG] = 0x00;
  rreg[R_IFSTAT] |= 0x48;  //set DTEI & DTBSY
  //the Neo Geo CD does not use the DTE interrupt
}

auto Cdc::reset() -> void {
  memory::fill(buffer, BufferSize);
  memory::fill(wreg, sizeof(wreg));
  memory::fill(rreg, sizeof(rreg));

  rreg[R_IFSTAT] = 0xff;
  u16 wa = 2352 * 2;
  wreg[W_WAL] = wa & 0xff;
  wreg[W_WAH] = wa >> 8;
  rreg[R_HEAD0] = 0x01;
  rreg[R_STAT3] = 0x80;
  decode = 0;

  updateHeader();
}

auto Cdc::updateHeader() -> void {
  if(wreg[W_CTRL1] & 1) {
    //sub-header mode
    rreg[R_HEAD0] = 0;
    rreg[R_HEAD1] = 0;
    rreg[R_HEAD2] = 0;
    rreg[R_HEAD3] = 0;
  } else {
    //header mode: BCD absolute MSF of the current position
    auto msf = CD::MSF::fromLBA(cdd.curLba);
    auto bcd = [](u8 value) -> n8 { return ((value / 10) << 4) | (value % 10); };
    rreg[R_HEAD0] = bcd(msf.minute);
    rreg[R_HEAD1] = bcd(msf.second);
    rreg[R_HEAD2] = bcd(msf.frame);
    rreg[R_HEAD3] = 0x01;
  }
}

}