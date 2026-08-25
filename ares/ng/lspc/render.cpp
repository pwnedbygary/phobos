auto LSPC::render(n9 y) -> void {
  auto output = screen->pixels().data() + y * 320;
  auto backdrop = io.shadow << 16 | pram[io.pramBank << 12 | 0xfff];
  for(u32 x : range(320)) {
    output[x] = backdrop;
  }

  n9 sx = 0;
  n9 sy = 0;
  n6 sh = 0;
  n4 hshrink = ~0;
  n8 vshrink = ~0;

  for(u32 sprite : range(381)) {
    n16 sattributes = vram[0x8000 | sprite];
    n16 yattributes = vram[0x8200 | sprite];
    n16 xattributes = vram[0x8400 | sprite];

    if(auto sticky = yattributes.bit(6)) {
      sx += hshrink + 1;
    } else {
      vshrink = sattributes.bit(0,7);
      sx = xattributes.bit(7,15);
      sh = yattributes.bit(0, 5);
      sy = yattributes.bit(7,15);
    }
    hshrink = sattributes.bit(8,11);

    n9 ry = y - (496 + 16 - sy);
    if(sh == 0) continue;
    if(sh >= 33) sh = 32;  //todo: loop borders when shrinking
    if(sx >= 320 && sx + 15 <= 511) continue;
    if(ry >= sh * 16) continue;

    // LSPC vertical zoom: the 000-lo.lo table maps (vshrink, line) -> (tile index << 4 |
    // row within tile). Bit 8 of ry is the inverted (bottom) line for a vertically-flipped sprite.
    bool invert = ry.bit(8);
    n8  entry = vscale[vshrink][ invert ? n8(~ry) : n8(ry) ];
    n5  tile  = entry >> 4;
    n4  row   = entry & 0xf;
    if(invert) { tile ^= 0x1f; row ^= 0xf; }

    n20 tileNumber = vram[sprite << 6 | tile << 1 | 0];
    n16 attributes = vram[sprite << 6 | tile << 1 | 1];
    n4  hflip      = attributes.bit(0) ? 15 : 0;
    n1  vflip      = attributes.bit(1);
    n2  animate    = attributes.bit(2,3);
    n8  palette    = attributes.bit(8,15);

    tileNumber.bit(16,19) = attributes.bit(4,7);
    if(auto mask = cartridge.cromMask()) tileNumber &= mask >> 7;  //wrap tile numbers like hardware (MAME masks the 26-bit gfx address)
    if(vflip) row ^= 0xf;
    switch(animate * !animation.disable) {
    case 0: break;
    case 1: tileNumber.bit(0,1) = animation.frame.bit(0,1); break;
    case 2: tileNumber.bit(0,2) = animation.frame.bit(0,2); break;
    case 3: tileNumber.bit(0,2) = animation.frame.bit(0,2); break;
    }

    n13 pramAddress = io.pramBank << 12 | palette << 4;
    n27 tileAddress = (tileNumber << 5 | row) << 2;

    n16 d0 = system.readC(tileAddress + 0) << 8 | system.readC(tileAddress + 64 + 0) << 0;
    n16 d1 = system.readC(tileAddress + 2) << 8 | system.readC(tileAddress + 64 + 2) << 0;
    n16 d2 = system.readC(tileAddress + 1) << 8 | system.readC(tileAddress + 64 + 1) << 0;
    n16 d3 = system.readC(tileAddress + 3) << 8 | system.readC(tileAddress + 64 + 3) << 0;

    n9  px = 0;
    n4  bx = hflip;
    for(u32 x : range(16)) {
      if(hscale[hshrink][x]) {
        n9 rx = sx + px++;
        // MVS horizontal wrap-around: sprites positioned past the right edge
        // (x >= 497) re-enter from the left.
        if (rx >= 320) {
          if (rx >= 512) rx -= 512;
          else continue;
        }

        n4 color;
        color.bit(0) = d0.bit(bx);
        color.bit(1) = d1.bit(bx);
        color.bit(2) = d2.bit(bx);
        color.bit(3) = d3.bit(bx);
        if (color) {
          output[rx] = io.shadow << 16 | pram[pramAddress | color];
        }
      }
      bx += hflip ? -1 : 1;
    }
  }

  auto fixBank = (u32)cartridge.fixBankType() * (u32)cpu.io.fixSelect;
  u32 garouOffsets[32] = {};
  if(fixBank == 1) {
    u32 garouBank = 0;
    u32 k = 0;
    u32 gy = 0;
    while(gy < 32) {
      if(vram[0x7500 + k] == 0x0200 && (u32)(vram[0x7580 + k] & 0xff00) == 0xff00) {
        garouBank = (u32)vram[0x7580 + k] & 3;
        garouOffsets[gy++] = garouBank;
      }
      garouOffsets[gy++] = garouBank;
      k += 2;
    }
  }

  u32 row = y >> 3;
  for(u32 x : range(320)) {
    u32 col = x >> 3;
    n16 attributes  = vram[0x7000 + col * 0x20 + row];
    n14 tileNumber  = attributes.bit( 0,11);
    n4  palette     = attributes.bit(12,15);
    n13 pramAddress = io.pramBank << 12 | palette << 4;
    if(fixBank == 1) {
      tileNumber += 0x1000 * (garouOffsets[(row - 2) & 31] ^ 3);
    }
    if(fixBank == 2) {
      auto bankWord = (u32)vram[0x7500 + ((row - 1) & 31) + 32 * (col / 6)];
      auto bank = ((bankWord >> ((5 - (col % 6)) * 2)) & 3) ^ 3;
      tileNumber += 0x1000 * bank;
    }
    n19 tileAddress = tileNumber << 5 | x << 2 & 24 ^ 16 | y & 7;
    n8  tileData    = system.readS(tileAddress);
    n4  color       = tileData >> (x & 1) * 4;
    if(color) {
      output[x] = io.shadow << 16 | pram[pramAddress | color];
    }
  }
}
