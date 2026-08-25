//uPD4990A serial RTC (Neo Geo ties c0/c1/c2 high => serial mode).
//Protocol mirrors MAME's upd4990a: a 4-bit command is shifted in MSB-first on
//CLK while STB is high and latched on STB rising edge. MODE_TIME_READ(3) loads
//the calendar into the 48-bit shift register; MODE_SHIFT(1) clocks it out LSB-first.
static auto rtcClk() -> void {
  auto& r = system.io.rtc;
  u8 in = r.shiftReg[6] & 1;
  r.shiftReg[6] >>= 1;
  r.shiftReg[6] |= (r.din << 3);
  if(r.command == 1) {  //MODE_SHIFT
    const int max = 6;
    for(int i = 0; i < max; i++) {
      r.shiftReg[i] >>= 1;
      if(i == max - 1) r.shiftReg[i] |= (in << 7);
      else r.shiftReg[i] |= ((r.shiftReg[i + 1] << 7) & 0x80);
    }
    r.dataOut = r.shiftReg[0] & 1;
  }
}

static auto rtcStb() -> void {
  auto& r = system.io.rtc;
  r.command = r.shiftReg[6] & 0x0f;
  if(r.command == 3) {  //MODE_TIME_READ
    for(int i = 0; i < 6; i++) r.shiftReg[i] = r.timeCounter[i];
  } else if(r.command == 1) {  //MODE_SHIFT
    r.dataOut = r.shiftReg[0] & 1;
  }
}

auto CPU::read(n1 upper, n1 lower, n24 address, n16 data) -> n16 {
  //NEO-E0
  if(io.vectorSelect == 0) {
    if((address & 0xffff80) == 0x000000 || (address & 0xffff80) == 0xc00000) {
      address ^= 0xc00000;  //swap BIOS and cartridge interrupt vectors
    }
  }

  if(auto result = platform->cheat(address)) return *result;

  if(!NeoGeo::Model::NeoGeoCD()) {
    //cartridge program ROM
    if(address <= 0x0fffff) {
      return cartridge.readP(upper, lower, address, data);
    }

    //work RAM
    if(address <= 0x1fffff) {
      return system.wram[address >> 1];
    }

    //cartridge program ROM (banked)
    if(address <= 0x2fffff) {
      return cartridge.readP(upper, lower, address, data);
    }
  } else if(address <= 0x1fffff) {
    if(address == 0x10f3d8 || address == 0x10f781 || address == 0x10f7bf || address == 0x10f6d8 || (address & 0xfffff0) == 0x10f800 || address == 0x10f7f4 || address == 0x10f7f8 || address == 0x10f7fc) {
      static FILE* f = nullptr;
      static u32 n = 0;
      if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/staterd.txt", "w");
      if(f && n++ < 4000) { fprintf(f, "RD %06x = %04x\n", address, system.wram[address >> 1]); fflush(f); }
    }
    if(address >= 0x10ff00 && address < 0x10ff40) {
      static FILE* f2 = nullptr;
      static u32 n2 = 0;
      if(!f2) f2 = fopen("/data/user/0/com.phobos.emulator/files/queue.txt", "w");
      if(f2 && n2++ < 3000) { fprintf(f2, "QW %06x <- %04x\n", address, data); fflush(f2); }
    }
    return system.wram[address >> 1];
  }

  //I/O registers
  if(address <= 0x3fffff) {
    return readIO(upper, lower, address, data);
  }

  //palette RAM
  if(address <= 0x7fffff) {
    address = lspc.io.pramBank << 13 | n13(address);
    return lspc.pram[address >> 1];
  }

  //memory card
  if(address <= 0xbfffff) {
    data.byte(0) = cardSlot.read(address >> 1);
    data.byte(1) = 0xff;
    return data;
  }

  //BIOS
  if(address <= 0xcfffff) {
    return system.bios[address >> 1];
  }

  //backup RAM (MVS only)
  if(address <= 0xdfffff) {
    if(NeoGeo::Model::NeoGeoMVS()) return system.sram[address >> 1];
    return data;
  }

  if(NeoGeo::Model::NeoGeoCD()) {
    if(address <= 0xefffff) {
      switch(system.io.uploadZone) {
        case 0: return system.spriteRam[(address & 0xfffff) >> 1];
        case 1: return system.pcmRam[(address & 0xfffff) >> 1];
        case 4: return apu.ram[(address & 0x1ffff) + upper];
        case 5: return system.fixRam[(address & 0x3ffff) >> 1];
      }
    } else if(address <= 0xffffff) {
      return readIO(upper, lower, address, data);
    }
  }

  //CD-ROM
  if(address <= 0xffffff) {
    return data;
  }

  return data;
}

auto CPU::write(n1 upper, n1 lower, n24 address, n16 data) -> void {
  if(!NeoGeo::Model::NeoGeoCD()) {
    //cartridge program ROM
    if(address <= 0x0fffff) {
      return cartridge.writeP(upper, lower, address, data);
    }

    //work RAM
    if(address <= 0x1fffff) {
      if(upper) system.wram[address >> 1].byte(1) = data.byte(1);
      if(lower) system.wram[address >> 1].byte(0) = data.byte(0);
      return;
    }

    //cartridge program ROM (banked)
    if(address <= 0x2fffff) {
      return cartridge.writeP(upper, lower, address, data);
    }
  } else if(address <= 0x1fffff) {
    if(upper) system.wram[address >> 1].byte(1) = data.byte(1);
    if(lower) system.wram[address >> 1].byte(0) = data.byte(0);
    return;
  }

  //I/O registers
  if(address <= 0x3fffff) {
    return writeIO(upper, lower, address, data);
  }

  //palette RAM
  if(address <= 0x7fffff) {
    address = lspc.io.pramBank << 13 | n13(address);
    lspc.pram[address >> 1] = data;
    return;
  }

  //memory card
  if(address <= 0xbfffff) {
    if(lower) cardSlot.write(address >> 1, data);
    return;
  }

  //BIOS
  if(address <= 0xcfffff) {
    return;
  }

  //backup RAM (MVS only)
  if(address <= 0xdfffff) {
    if(NeoGeo::Model::NeoGeoMVS()) system.sram[address >> 1] = data;
    return;
  }

  if(NeoGeo::Model::NeoGeoCD()) {
    if(address <= 0xefffff) {
      switch(system.io.uploadZone) {
        case 0: address &= 0xfffff;
                address.bit(20, 21) = system.io.spriteUploadBank;
                if(upper) system.spriteRam[address >> 1].byte(1) = data.byte(1);
                if(lower) system.spriteRam[address >> 1].byte(0) = data.byte(0);
                return;
        case 1: address &= 0xfffff;
                address.bit(19) = system.io.pcmUploadBank;
                address >>= 1;
                if(lower) system.pcmRam[address] = data.byte(0);
                return;
        case 4: address >>= 1;
                if(lower) apu.ram[address & 0x1ffff] = data.byte(0);
                return;
        case 5: address >>= 1;
                if(lower) system.fixRam[address & 0x3ffff] = data.byte(0);
                return;
      }
      return;
    } else if(address <= 0xffffff) {
      return writeIO(upper, lower, address, data);
    }
  }

  //CD-ROM
  if(address <= 0xffffff) {
    return;
  }
}

auto CPU::readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16 {
  //REG_P1CNT
  if((address & 0xfe0000) == 0x300000 && upper) {
    data.byte(1) = controllerPort1.readButtons();
  }

  //REG_DIPSW
  if((address & 0xfe0080) == 0x300000 && lower) {
    data.bit(0)   = 1;  //settings mode
    data.bit(1)   = 1;  //0 = 1 chute; 1 = 2 chutes (TODO: Verify)
    data.bit(2)   = 1;  //1 = normal controller; 0 = mahjong keyboard
    data.bit(3,4) = 1;  //communication ID code
    data.bit(5)   = 1;  //enable multiplayer
    data.bit(6)   = 1;  //freeplay
    data.bit(7)   = 1;  //freeze
  }

  //REG_SYSTYPE
  if((address & 0xfe0080) == 0x300080 && lower) {
    data.bit(6) = 0;  //0 = 2 slots; 1 = 4 or 6 slots
    data.bit(7) = 1;  //test button
  }

  //REG_SOUND
  if((address & 0xfe0000) == 0x320000 && upper) {
    data.byte(1) = apu.communication.output;
  }

  //REG_STATUS_A
  if((address & 0xfe0000) == 0x320000 && lower) {
    //Ensure coin line is up-to-date even for titles that never read REG_STATUS_B / REG_P1CNT.
    //Poll START/SELECT here (lightweight) so SELECT-as-coin works without a per-frame LSPC hook
    //that would run on the video thread and contend with audio.
    controllerPort1.pollCoin();
    controllerPort2.pollCoin();
    //coin bits are active-low: 0 = inserted. system.io.coin asserts them when
    //the player holds START (auto-credit) or SELECT (coin button).
    bool coin = NeoGeo::Model::NeoGeoMVS() && !system.io.coin;
    data.bit(0) = coin;  //coin 1 (player 1 credit)
    data.bit(1) = 1;     //coin 2 not inserted
    data.bit(2) = 0;  //service button (released)
    data.bit(3) = 1;     //coin 3 not inserted
    data.bit(4) = 1;     //coin 3 (dup) not inserted
    data.bit(5) = 0;  //0 = 4-slot; 1 = 6-slot
    data.bit(6) = system.io.rtcTimePulse;  //RTC time pulse
    data.bit(7) = system.io.rtc.dataOut;    //RTC data out (serial)
  }

  //REG_P2CNT
  if((address & 0xfe0000) == 0x340000 && upper) {
    data.byte(1) = controllerPort2.readButtons();
  }

  //REG_STATUS_B
  if((address & 0xfe0000) == 0x380000 && upper) {
    data.bit( 8, 9) = controllerPort1.readControls();
    data.bit(10,11) = controllerPort2.readControls();
    data.bit(12,13) = 0b00;  //0b00 = memory card inserted
    data.bit(14)    = cardSlot.lock != 0;
    data.bit(15)    = NeoGeo::Model::NeoGeoMVS();  //0 = AES; 1 = MVS
  }

  //REG_VRAMADDR
  if((address & 0x3e0006) == 0x3c0000) {
    data = lspc.vram[lspc.io.vramAddress];
  }

  //REG_VRAMRW
  if((address & 0x3e0006) == 0x3c0002) {
    data = lspc.vram[lspc.io.vramAddress];
  }

  //REG_VRAMMOD
  if((address & 0x3e0006) == 0x3c0004) {
    data = lspc.io.vramIncrement;
  }

  //REG_LSPCMODE
  if((address & 0x3e0006) == 0x3c0006) {
    data.bit(0, 2) = lspc.animation.frame;
    data.bit(3)    = 0;  //0 = 60hz; 1 = 50hz
    data.bit(4, 6) = 0;  //unused
    data.bit(7,15) = lspc.io.vcounter + 248;
  }

  //Neo Geo CD CDD / control registers
  if(NeoGeo::Model::NeoGeoCD()) {
    //REG_NFF0016 (CDD/CDC latch)
    if((address & 0xfffffe) == 0xff0016) {
      data = cdd.latch16;
      return data;
    }
    //CDC register address select
    if((address & 0xfffffe) == 0xff0100) {
      data = cdc.addressRead();
      return data;
    }
    //CDC register data
    if((address & 0xfffffe) == 0xff0102) {
      data = cdc.dataRead();
      return data;
    }
    //CDD serial receive (4-bit nibble + clock status)
    if((address & 0xfffffe) == 0xff0160) {
      data = cdd.rxRead();
      return data;
    }
    //REG_CDD_REGION (DIP switch; 0 = Japan)
    if((address & 0xfffffe) == 0xff011c) {
      data = ~((0x10 | (cdd.region & 3)) << 8);
      return data;
    }
  }

  return data;
}

auto CPU::writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void {
  //REG_DIPSW
  if((address & 0xfe0080) == 0x300000 && lower) {
    //todo: kick watchdog
  }

  //REG_SOUND
  if((address & 0xfe0000) == 0x320000 && upper) {
    apu.communication.input = data.byte(1);
    apu.nmi.pending = 1;
  }

  //REG_POUTPUT
  if((address & 0xfe0070) == 0x380000 && lower) {
    controllerPort1.writeOutputs(data.bit(0,2));
    controllerPort2.writeOutputs(data.bit(3,5));
  }

  //REG_CRDBANK
  if((address & 0xfe0070) == 0x380010 && lower) {
    cardSlot.bank = data.bit(0,2);
  }

  //REG_POUTPUT (mirror) (AES only)
  //REG_SLOT (MVS only)
  if((address & 0xfe00f0) == 0x380020 && lower) {
    if(NeoGeo::Model::NeoGeoAES()) {
      controllerPort1.writeOutputs(data.bit(0,2));
      controllerPort2.writeOutputs(data.bit(3,5));
    }
    if(NeoGeo::Model::NeoGeoMVS()) {
      system.io.slotSelect = data.bit(0,2);
    }
  }

  //REG_LEDLATCHES
  if((address & 0xfe00f0) == 0x380030 && lower) {
    system.io.ledMarquee = data.bit(3);
    system.io.ledLatch1  = data.bit(4);
    system.io.ledLatch2  = data.bit(5);
  }

  //REG_LEDDATA
  if((address & 0xfe00f0) == 0x380040 && lower) {
    system.io.ledData = data.bit(0,7);
  }

  //REG_RTCCTRL (uPD4990A serial RTC control: DIN/CLK/STB)
  if((address & 0xfe00f0) == 0x380050 && lower) {
    u8 din = data.bit(0);
    u8 clk = data.bit(1);
    u8 stb = data.bit(2);
    if(clk && !system.io.rtc.clk) rtcClk();   //rising edge of CLK
    if(stb && !system.io.rtc.stb) rtcStb();   //rising edge of STB latches command
    system.io.rtc.din = din;
    system.io.rtc.clk = clk;
    system.io.rtc.stb = stb;
  }

  //REG_RESETCC1
  if((address & 0xfe00f6) == 0x380060 && lower) {
    //todo: float coin counter 1
  }

  //REG_RESETCC2
  if((address & 0xfe00f6) == 0x380062 && lower) {
    //todo: float coin counter 2
  }

  //REG_RESETCL1
  if((address & 0xfe00f6) == 0x380064 && lower) {
    //todo: float coin lockout 1
  }

  //REG_RESETCL2
  if((address & 0xfe00f6) == 0x380066 && lower) {
    //todo: float coin lockout 2
  }

  //REG_SETCC1
  if((address & 0xfe00f6) == 0x3800e0 && lower) {
    //todo: sink coin counter 1
  }

  //REG_SETCC2
  if((address & 0xfe00f6) == 0x3800e2 && lower) {
    //todo: sink coin counter 2
  }

  //REG_SETCL1
  if((address & 0xfe00f6) == 0x3800e4 && lower) {
    //todo: sink coin lockout 1
  }

  //REG_SETCL2
  if((address & 0xfe00f6) == 0x3800e6 && lower) {
    //todo: sink coin lockout 2
  }

  //REG_NOSHADOW
  if((address & 0xfe001e) == 0x3a0000 && lower) {
    lspc.io.shadow = 0;
  }

  //REG_SWPBIOS
  if((address & 0xfe001e) == 0x3a0002 && lower) {
    io.vectorSelect = 0;
  }

  //REG_CRDUNLOCK1
  if((address & 0xfe001e) == 0x3a0004 && lower) {
    cardSlot.lock.bit(0) = 0;
  }

  //REG_CRDLOCK2
  if((address & 0xfe001e) == 0x3a0006 && lower) {
    cardSlot.lock.bit(1) = 1;
  }

  //REG_CRDREGSEL
  if((address & 0xfe001e) == 0x3a0008 && lower) {
    cardSlot.select = 1;
  }

  //REG_BRDFIX
  if((address & 0xfe001e) == 0x3a000a && lower) {
    io.fixSelect = 0;
  }

  //REG_SRAMLOCK
  if((address & 0xfe001e) == 0x3a000c && lower) {
    system.io.sramLock = 1;
  }

  //REG_PALBANK0
  if((address & 0xfe001e) == 0x3a000e && lower) {
    lspc.io.pramBank = 0;
  }

  //REG_SHADOW
  if((address & 0xfe001e) == 0x3a0010 && lower) {
    lspc.io.shadow = 1;
  }

  //REG_SWPROM
  if((address & 0xfe001e) == 0x3a0012 && lower) {
    io.vectorSelect = 1;
  }

  //REG_CRDLOCK1
  if((address & 0xfe001e) == 0x3a0014 && lower) {
    cardSlot.lock.bit(0) = 1;
  }

  //REG_CRDUNLOCK2
  if((address & 0xfe001e) == 0x3a0016 && lower) {
    cardSlot.lock.bit(1) = 0;
  }

  //REG_CRDNORMAL
  if((address & 0xfe001e) == 0x3a0018 && lower) {
    cardSlot.select = 0;
  }

  //REG_CRTFIX
  if((address & 0xfe001e) == 0x3a001a && lower) {
    io.fixSelect = 1;
  }

  //REG_SRAMUNLOCK
  if((address & 0xfe001e) == 0x3a001c && lower) {
    system.io.sramLock = 0;
  }

  //REG_PALBANK1
  if((address & 0xfe001e) == 0x3a001e && lower) {
    lspc.io.pramBank = 1;
  }

  //REG_VRAMADDR
  if((address & 0x3e000e) == 0x3c0000) {
    if(upper) lspc.io.vramAddress.byte(1) = data.byte(1);
    if(lower) lspc.io.vramAddress.byte(0) = data.byte(0);
  }

  //REG_VRAMRW
  if((address & 0x3e000e) == 0x3c0002) {
    if(upper) lspc.vram[lspc.io.vramAddress].byte(1) = data.byte(1);
    if(lower) lspc.vram[lspc.io.vramAddress].byte(0) = data.byte(0);
    lspc.io.vramAddress.bit(0,14) += lspc.io.vramIncrement;
  }

  //REG_VRAMMOD
  if((address & 0x3e000e) == 0x3c0004) {
    if(upper) lspc.io.vramIncrement.byte(1) = data.byte(1);
    if(lower) lspc.io.vramIncrement.byte(0) = data.byte(0);
  }

  //REG_LSPCMODE
  if((address & 0x3e000e) == 0x3c0006) {
    if(lower) {
      lspc.animation.disable     = data.bit(3);
      lspc.timer.interruptEnable = data.bit(4);
      lspc.timer.reloadOnChange  = data.bit(5);
      lspc.timer.reloadOnVblank  = data.bit(6);
      lspc.timer.reloadOnZero    = data.bit(7);
    }
    if(upper) {
      lspc.animation.speed       = data.bit(8,15);
    }
  }

  //REG_TIMERHIGH
  if((address & 0x3e000e) == 0x3c0008) {
    if(upper) lspc.timer.reload.byte(3) = data.byte(1);
    if(lower) lspc.timer.reload.byte(2) = data.byte(0);
  }

  //REG_TIMERLOW
  if((address & 0x3e000e) == 0x3c000a) {
    if(upper) lspc.timer.reload.byte(1) = data.byte(1);
    if(lower) lspc.timer.reload.byte(0) = data.byte(0);
    if(lspc.timer.reloadOnChange) {
      lspc.timer.counter = lspc.timer.reload;
    }
  }

  //REG_IRQACK
  if((address & 0x3e000e) == 0x3c000c) {
    if(lower) {
      lspc.irq.powerAcknowledge  = data.bit(0);
      lspc.irq.timerAcknowledge  = data.bit(1);
      lspc.irq.vblankAcknowledge = data.bit(2);
    }
  }

  //REG_TIMERSTOP
  if((address & 0x3e000e) == 0x3c000e) {
    if(lower) {
      lspc.timer.stopPAL = data.bit(0);
    }
  }

  //Neo Geo CD upload control registers
  if(!NeoGeo::Model::NeoGeoCD()) return;

//DMA controller
  auto logdma = [](n24 address, n16 data) {
    static FILE* f = nullptr;
    static u32 n = 0;
    if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/dmalog.txt", "a");
    if(f && n++ < 3000) fprintf(f, "REG %06x <- %04x\n", (u32)address, (u32)data);
  };
  if((address & 0xfffffe) == 0xff0060) {
    static FILE* f = nullptr;
    static u32 n = 0;
    if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/dmalog.txt", "w");
    if(f && n++ < 3000) fprintf(f, "FF0060 <- %04x (bit6=%d)\n", (u32)data, data.bit(6));
    if(data.bit(6)) dma.start();
    return;
  }
  if((address & 0xfffffe) == 0xff0064) { dma.setAddress1Hi(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff0066) { dma.setAddress1Lo(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff0068) { dma.setAddress2Hi(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff006a) { dma.setAddress2Lo(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff006c) { dma.value1 = data; logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff006e) { dma.value2 = data; logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff0070) { dma.setCountHi(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff0072) { dma.setCountLo(data); logdma(address, data); return; }
  if((address & 0xfffffe) == 0xff007e) { dma.mode = data; logdma(address, data); return; }
  if((address & 0xfffffe) >= 0xff0080 && (address & 0xfffffe) <= 0xff008e) { return; }  //DMA program (no-op)

  //REG_NFF0002 (CDD/CDC control latch)
  if((address & 0xfffffe) == 0xff0002) {
    static FILE* f = nullptr;
    static u32 n = 0;
    if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/cddlog.txt", "a");
    if(f && n++ < 2000) { fprintf(f, "REG2 %04x\n", data); fflush(f); }
    cdd.reg2 = data;
    return;
  }

  //CDC register address select
  if((address & 0xfffffe) == 0xff0100) {
    cdc.addressWrite(data.byte(0));
    return;
  }

  //CDC register data
  if((address & 0xfffffe) == 0xff0102) {
    cdc.dataWrite(data.byte(0));
    return;
  }

  //REG_IRQACK: bits 3/4/5 acknowledge the CDD type3/type2/type1 IRQs.
  //The NeoCD BIOS writes $FF000F (0x08/0x10/0x20). The pending flags are
  //consumed only by the CPU dispatch; the acknowledge just records the
  //acknowledged state so late-arriving acks cannot swallow sector events.
  if((address & 0xfffffe) == 0xff000e || (address & 0xfffffe) == 0xff000f) {
    return;
  }

  //REG_CDC_GATE / CDD reset: writing 0x00 resets the CDD serial link
  //(active low) and prohibits CDD/CDC interrupts; 0x01 re-enables.
  if((address & 0xfffffe) == 0xff0180) {
    if(data == 0x00) {
      cdd.serialReset();
      cdd.prohibitIrq = true;
    } else {
      cdd.prohibitIrq = false;
    }
    return;
  }

  //REG_NFF0016 (CDD/CDC latch)
  if((address & 0xfffffe) == 0xff0016) {
    cdd.latch16 = data;
    return;
  }

  //CDD serial transmit (4-bit nibble)
  if((address & 0xfffffe) == 0xff0162) {
    cdd.txWrite(data.byte(0));
    return;
  }

  //CDD serial clock / send strobe
  if((address & 0xfffffe) == 0xff0164) {
    cdd.commsControl(data.bit(0), data.bit(1));
    return;
  }

  //REG_TRANSAREA (written at $FF0105)
  if((address & 0xfffffe) == 0xff0104) {
    system.io.uploadZone = data;
    return;
  }

  //REG_Z80RST
  if((address & 0xfffffe) == 0xff0182) {
    apu.restart();
    return;
  }

  //REG_SPRBANK
  if((address & 0xfffffe) == 0xff01a0) {
    system.io.spriteUploadBank = data.bit(0,1);
    return;
  }

  //REG_PCMBANK
  if((address & 0xfffffe) == 0xff01a2) {
    system.io.pcmUploadBank = data.bit(0);
    return;
  }

  return;
}
