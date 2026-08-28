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
        case 5: return system.fixRam[(address >> 1) & 0x1ffff];
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
        //Transfer-area upload zones ($E00000-$EFFFFF window). Index math mirrors
        //libretro neocd memory_mapped.cpp (the working reference): each zone
        //accepts the 68K's byte stores through the low lane at odd addresses
        //(the bus is effectively byte-swapped on the CD); the 16-bit word index
        //is address>>1 masked to each RAM's size.
        case 0: address &= 0xfffff;
                address.bit(20, 21) = system.io.spriteUploadBank;
                if(upper) system.spriteRam[address >> 1].byte(1) = data.byte(1);
                if(lower) system.spriteRam[address >> 1].byte(0) = data.byte(0);
                return;
        case 1: address &= 0xfffff;
                address.bit(19) = system.io.pcmUploadBank;
                address >>= 1;
                //libretro: pcmRam[(addr >> 1) + bank*0x80000], odd byte addr only
                if(lower) system.pcmRam[(address + (system.io.pcmUploadBank << 19)) & 0xfffff] = data.byte(0);
                return;
        case 4: address >>= 1;
                if(lower) apu.ram[address & 0x1ffff] = data.byte(0);
                return;
        case 5: //FIX DRAM 128KiB — index MUST be & 0x1ffff (0x3ffff overflows the
                //array and corrupts adjacent memory → garbled text layer)
                if(lower) system.fixRam[(address >> 1) & 0x1ffff] = data.byte(0);
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
    //NEO-C1 selector gate (libretro neocd controller1Handlers): controller
    //data only appears when $380001 holds 0x00/0x12/0x1B; otherwise idle $FF.
    if(system.io.ctrlSelector == 0x00 || system.io.ctrlSelector == 0x12 || system.io.ctrlSelector == 0x1b) {
      data.byte(1) = controllerPort1.readButtons();
    } else {
      data.byte(1) = 0xff;
    }
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
    if(system.io.ctrlSelector == 0x00 || system.io.ctrlSelector == 0x12 || system.io.ctrlSelector == 0x1b) {
      data.byte(1) = controllerPort2.readButtons();
    } else {
      data.byte(1) = 0xff;
    }
  }

  //REG_STATUS_B
  if((address & 0xfe0000) == 0x380000 && upper) {
    if(NeoGeo::Model::NeoGeoCD()) {
      //libretro neocd controller3 handlers: a byte read of $380000 returns the
      //Start/Select byte in the LOW byte (bits 0-3, P1 Start/Sel = 0/1, P2 = 2/3,
      //active-low); a WORD read returns it in the HIGH byte (input3<<8 | 0xFF).
      //Both are gated by the $380001 selector (0x00/0x12/0x1B valid; byte reads
      //of odd addresses return 0xFF).
      n8 v = 0x0f;
      if(system.io.ctrlSelector == 0x00 || system.io.ctrlSelector == 0x12 || system.io.ctrlSelector == 0x1b) {
        n2 c1 = controllerPort1.readControls();
        n2 c2 = controllerPort2.readControls();
        v.bit(0) = c1.bit(0);  //P1 Start
        v.bit(1) = c1.bit(1);  //P1 Select
        v.bit(2) = c2.bit(0);  //P2 Start
        v.bit(3) = c2.bit(1);  //P2 Select
      }
      //Lanes: the m68k's byte read of even $380000 uses /UDS and takes byte(1);
      //word reads take both. libretro's word read is input3<<8|0xFF, so the
      //Start/Select byte lives in byte(1) for both access sizes; odd-address
      //byte reads ($380001) fall through with upper=0 and read 0xFF elsewhere.
      data.byte(1) = v;
      data.byte(0) = 0xff;
    } else {
      data.bit( 8, 9) = controllerPort1.readControls();
      data.bit(10,11) = controllerPort2.readControls();
      data.bit(12,13) = 0b00;  //0b00 = memory card inserted
      data.bit(14)    = cardSlot.lock != 0;
      data.bit(15)    = NeoGeo::Model::NeoGeoMVS();  //0 = AES; 1 = MVS
    }
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
    //NEO-C1 controller selector (libretro neocd controller3WriteByte): on CD
    //systems a byte write to $380001 (upper=0, lower=1) selects what
    //$300000/$340000/$380000 return (0x00/0x12/0x1B = controller data). The
    //68k presents an odd byte store as address&~1 with lower=1. On AES/MVS
    //this address is REG_POUTPUT (controller output lines) instead.
    if(NeoGeo::Model::NeoGeoCD() && !upper) {
      system.io.ctrlSelector = data.byte(0);
    } else {
      controllerPort1.writeOutputs(data.bit(0,2));
      controllerPort2.writeOutputs(data.bit(3,5));
    }
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
  if((address & 0xfffffe) == 0xff0060) {
    //LC8953 trigger is the BYTE $FF0061 (wiki: $00 = load microcode, $40 = start
    //DMA). The 68k presents an odd byte store as address&~1 with lower=1 and the
    //value in the low byte; word stores to $FF0060 keep upper+lower. The BIOS
    //(neocdz) writes move.b #$40,$FF0061 to start every upload DMA.
    if(!upper) {  //byte write to $FF0061 (odd)
      n8 b = data.byte(0);
      if(b == 0x40) {
        dma.start();
      } else {
        //$00 = load microcode; the LC8953 upload program lives in $FF0080-$FF008E
      }
    } else {
      if(data.bit(6)) dma.start();
    }
    return;
  }
  if((address & 0xfffffe) == 0xff0064) { dma.setAddress1Hi(data); return; }
  if((address & 0xfffffe) == 0xff0066) { dma.setAddress1Lo(data); return; }
  if((address & 0xfffffe) == 0xff0068) { dma.setAddress2Hi(data); return; }
  if((address & 0xfffffe) == 0xff006a) { dma.setAddress2Lo(data); return; }
  if((address & 0xfffffe) == 0xff006c) { dma.value1 = data; return; }
  if((address & 0xfffffe) == 0xff006e) { dma.value2 = data; return; }
  if((address & 0xfffffe) == 0xff0070) { dma.setCountHi(data); return; }
  if((address & 0xfffffe) == 0xff0072) { dma.setCountLo(data); return; }
  if((address & 0xfffffe) == 0xff007e) { dma.mode = data; return; }
  if((address & 0xfffffe) >= 0xff0080 && (address & 0xfffffe) <= 0xff008e) { return; }  //DMA program (no-op)

  //REG_NFF0002 (CDD/CDC control latch)
  if((address & 0xfffffe) == 0xff0002) {
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
  //The NeoCD BIOS writes $FF000F with 0x08 (type1) / 0x10 (type2) / 0x20
  //(type3) at the top of each vector stub. On hardware the CDD IRQ line
  //stays asserted while any type is unacked; the 68k then takes the next
  //unacked type on the following dispatch. Our dispatch must therefore
  //NOT consume pending flags by itself — the ack write here clears the
  //acknowledged type, and the CPU keeps dispatching while any type remains
  //pending. (Without this, a continuously-streaming type1 sector IRQ
  //starves the 75Hz type2 access IRQ, and the BIOS wait loop that needs
  //$76BC>=8 via the access handler never completes — "WAIT FOR A MOMENT"
  //forever.)
  if((address & 0xfffffe) == 0xff000e || (address & 0xfffffe) == 0xff000f) {
    if(lower) {
      if(data.byte(0) & 0x08) cdd.type1Pending = 0;
      if(data.byte(0) & 0x10) cdd.type2Pending = 0;
      if(data.byte(0) & 0x20) cdd.type3Pending = 0;
      //re-assert the CDD interrupt if any type is still pending
      if(cdd.type1Pending || cdd.type2Pending || cdd.type3Pending) {
        cpu.raise(CPU::Interrupt::CDD);
      }
    }
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