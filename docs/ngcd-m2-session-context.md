# Neo Geo CD (NGCD) — Session Context for openCode handoff

> Drop-in briefing for a fresh LLM/engineer continuing this work. Read this first, then
> `docs/handoff.md` and `docs/implementation-plan.md` (Task NGCD-M2 = RESOLVED, Task NGCD-M3 = residual
> title-menu text glitch) for the always-current repo state.
> Public hardware reference: https://wiki.neogeodev.org/index.php/Main_Page

## Objective — ✅ COMPLETE (2026-08-27 boot; 2026-08-28 rendering round committed `9230e4d60` + `783a6cc27`)
Fix **Neo Geo CD** boot on Phobos Android so the BIOS boots the game. Game code must reach `0x100000`.

**2026-08-28 rendering round (residual = Task NGCD-M3):** all rendering verified correct EXCEPT the
title-menu text tile families (see the "Rendering residual" appendix at the end of this file).

**Verified on-device (final clean build):** the disc boots past the CD Player/menu gate and the game
loads — the title/attract screen renders and animates, the CPU executes game code (stable main-loop PC
distribution, vblank/secRd/decode IRQs all climbing at correct rates). The BIOS sets the disc-ready bit
(`$10F656` bit 7) **itself** via its natural boot-init once real sector data flows; pressing Start then
latches `$76B9=0x80` and dispatches to the game (`$C0CE72` path).

## Environment
- Device: **Retroid Pocket 6**, serial **49016109**. ⚠ If the NotificationShade has stable focus, key events
  never reach the app — reboot the device, unlock, then relaunch (adb keyevents only work with the app focused).
- Test disc: **Samurai Shodown RPG** (TRACK 29 / TIME 68:12).
- ROM: `/storage/EBFF-F6C0/ROMs/neo-geo-cd/Samurai Spirits ~ Samurai Shodown (Japan) (En,Ja).chd`
  (dir is `neo-geo-cd`, not `neogeocd`). **Do NOT copy the ROM to /data/local/tmp — the app gets EACCES.**
- Build (⚠ /home is mounted read-only; gradle wrapper lock fails):
  `GRADLE_USER_HOME=/home/garyb/LLM-Projects/phobos/android/.gradle-home ./gradlew app:assembleModernDebug`
  → `app/build/outputs/apk/modern/debug/app-modern-debug.apk`.
- **ALWAYS verify the native lib rebuilt**: `unzip -p <apk> lib/arm64-v8a/libphobos_android.so > /tmp/x.so &&
  grep -c "<new marker string>" /tmp/x.so` — string present = native picked up the change. (Burned twice.)
- Deploy: `adb shell am force-stop com.phobos.emulator && adb install -r <apk> && adb logcat -c &&`
  `adb shell "am start -n com.phobos.emulator/.MainActivity --es load_uri 'file:///...shodown...chd' --es load_name SamShodown --es load_system 'Neo Geo CD'"` (~30-45 s to CD Player, ~60-90 s to title screen).
- BIOS: `neocdz.bin` from `/home/garyb/Mounts/Emulation/Emulation/APKs/neocd.zip`, 524288 bytes. Working copies:
  `ngcd_work/ngcd_bios.bin` (raw file) and **`ngcd_work/bios_swapped.bin` (the file the 68K actually executes —
  ALWAYS disassemble THIS one)**. Helper: `/tmp/disasm_bios.py <start> <end>` (capstone m68k on swapped).

## THE #1 GOTCHA: the BIOS file is byte-swapped
`ares/ng/system/system.cpp:146` loads the BIOS with `fp->readl(2L)` (little-endian 16-bit word). The 68K
therefore executes **each file word with its byte lanes reversed** (matches MAME `ROM_GROUPWORD|ROM_REVERSE`).
**Disassembling the raw file gives complete garbage.** Always work from `ngcd_work/bios_swapped.bin`.

## THE #2 GOTCHA: raw-sector LBA offset (FIX DEPLOYED — keep it)
`nall::vfs::cdrom` (the CHD/CUE image the app attaches as `cd.rom`) stores a raw 2448-byte-per-sector
stream that **begins at the disc lead-in**: logical LBA N lives at physical sector
**(`CD::LeadInSectors` + `CD::LBAtoABA(N)`) = 7500 + N + 150** (nall/cd/session.hpp constants).
`Disc::readSectorRaw(lba)` must seek `2448 * (LeadInSectors + LBAtoABA(lba))` — the old `lba*2448` read the
blank lead-in and returned **all-zero sectors**, which stalled the whole BIOS boot (directory compare at
`$C0D4C4` failed; loader looped "WAIT FOR A MOMENT"). PS1's `Drive` uses the same mapping
(`ares/ps1/disc/drive.cpp:20,132`). The subchannel/TOC decode in `Disc::connect()` reads at
`sector*2448+2352` (physical), which is correct because `vfs::cdrom::loadSub` synthesizes subchannel for
every physical sector.

## THE #3 GOTCHA: FIX (text) upload zone index (FIX DEPLOYED — keep it)
The transfer-area upload zone for FIX DRAM (`case 5` in `CPU::write`, window `$E00000-$EFFFFF`) must map
to the 128KiB `fixRam` as `(address >> 1) & 0x1FFFF` (accepting the lower lane / odd-byte stores, per
libretro neocd `memory_mapped.cpp`). The old code masked with `0x3FFFF` — a **heap overrun** (writes
past the 128KiB array into adjacent RAM) on top of wrong indexing, which garbled the in-game text layer.
Sprite/PCM/Z80 zones mirror the same `>>1` word-index model (verified against libretro; SPR renders
correctly, leave it).

## BIOS boot / state machine (all addresses are the swapped/executed view)
- Reset vector (emulator interpretation, LE words): `SP=$10F300`, `PC=$C00402` → `jmp $C0A5B6`.
- **Top-level state dispatcher `$C00108`**: `move.b $73d8(a5),d0` ×4 → table at `$C00116`:
  state 0 → `$C14852` (boot), 1 → `$C14C2A` (disc check), 2 → `$C0C966` (CD Player), **3 → `$C0CA74`
  (game load)** — the Start gate is here, 4 → `$C05122` (game boot).
- Game-load gate `$C0CA86`: `tst.b $7656; bpl → skip; move.b $7dad,d0; andi #$5; beq → skip; move.b #$80,$76b9`
  — needs **bit 7 (disc-ready) of `$10F656`** plus P2 edge bits; then `$C0CA9E: tst $76b9; bmi → $C0CE72` (dispatch).
- Boot-init `$C0CEC4`: requires `$10F656` **bit 0** (`btst #0` at `$C0CEDE`-family) before running the
  disc/TOC/position phase; completes by setting the disc-ready bit itself at **`$C0D2E4: bset #7, $7656`**
  (gated on `$7679==0`).
- CDD command dispatcher `$C0BBAA` (reads tx[0] from `$76FA(a5)`), jump table at `$C0BC44`:
  cmd 2 → `$C0C2B6` (TOC), cmd 3 → `$C0C30A` (READ), cmd 4 → `$C0C4C2` (seek), cmd 6 → pause,
  cmd 7 → `$C0C5EE` (resume = builds a PLAY `0x30` packet), cmd 10 → init, cmd 11-15 → unknown.
- Boot wait loop `$C0CF56` (0x100000-iteration timeout): dispatches cmd 6/9 into the TX FIFO every 65536
  iterations via `$C0C6D6`; exits to the directory parse (`lea $c0d523,a0; lea $111204,a1; bsr $C0D4C4` —
  byte-compares the loaded directory against "CD001"/"LOGO_J.PRG"/"LOGO_U.PRG"/"LOGO_E.PRG"/"TITLE_J.SYS"...
  tables) when `$76B6==0` (cleared by `$C0EC40` when the `$7688` block counter reaches 0) or `$76BC>=8`.
- Access/status machine `$C0E99E` (dispatches on `$76B7` via table at `$C0E9B2`): state 0 `$C0E9BA` reads
  CDC status (`$FF0101`/`$FF0103`, `btst #5` → error `$C0ED4E`); state 1 `$C0EA0E` reads the sector header
  (CDC reg `$C0EA6A`: 4 bytes into `$76D0`), compares against the expected position `$76C8`, reads data
  (reg 0xC), ticks `$76BA/$76BB/$76BC/$7688` and eventually clears `$76B6` → boot-init proceeds.

## CDD IRQ/vector model (ALL FIXED THIS SESSION — keep them)
1. **75Hz access IRQ** (`tick()`, `reg2 & 0x0050 && !prohibitIrq` → `type2Pending`) → vector **`$16`**
   → stub `$C0A42C` (acks `$FF000F=0x10`, serial RX path `$C0BA6A`).
2. **Decoder-complete IRQ** (`ctrlChecks()`, IFCTRL bit5 → previously `type1Pending`) → **MUST be vector `$15`**
   → stub `$C0A40A` (acks `$FF000F=0x20`, calls the **access machine `$C0E99E`**). ⚠ Hooked as `type3Pending`
   (CDDType3=21=0x15); sending it to vector `$17` (stub `$C0A44E`, "unused") starved the access machine and
   the boot wait loop never exited. This was THE fix that let the boot-init complete.
3. `$C0A44E` (vector `$17`, ack `$08`) is unused by the loader.
4. **IRQ acceptance is IPL-gated**: `CPU::main` dispatches CDD only when `2 > r.i` (normal level-2 IRQ;
   MAME `set_input_line(2)`). Without the gate, type2 interrupts re-enter before the handler RTE and the
   CPU wedges (IRQ storm — the "PCE-like flicker" from earlier sessions).
5. **Pending flags are consumed by the $FF000F ACK write, NOT by the dispatch** (`memory.cpp` REG_IRQACK):
   ack 0x20/0x10/0x08 clears type3/type2/type1 respectively; the line re-asserts if another type is still
   pending (`cpu.raise(CDD)`). Consuming at dispatch starved type2 under sector streaming.

## CDD settle + disc-detect HLE (KEEP — pragmatic, documented)
- `stop()` keeps `statusHack=0x0e` ("tray moving") — boot disc-check requires it — and arms
  `settleCounter` (scaled: `150 × readSpeed`, ~2 s wall time). `tick()` then transitions to `0x09`
  (CdStopped), and **pokes `$10F656` bit 0** (`w.byte(1) |= 0x01`) when a disc is present. That bit is
  what triggers the BIOS's own boot-init (`$C0CEC4`), which runs the TOC/position phase and sets bit 7
  itself. ⚠ Do NOT poke bit 7 directly: it auto-boots the loader before the TOC exists → garbage READ
  (`0xFF` MSF → DISC I/O ERROR). (The natural setter `$C007CA`/`$C00858` never executes in this
  emulator's path; bit-0 poke is the standing HLE.)
- **2026-08-27 fix**: the poke is gated on **bit 7 clear** (`if(!(w.byte(1) & 0x80))`). Previously every
  STOP re-armed the settle, and every settle re-poked bit 0 → the BIOS's boot-init re-ran forever →
  the CD player menu blipped "WAIT FOR A MOMENT" every ~2s. Once the BIOS sets bit 7 (disc-ready) it
  also clears bit 0; gating the poke on bit 7 means the disc check runs once and stays done.

## CD read speed option (per-core, pause menu — ✳ "Instant" is safe)
- Setting `cdd.readSpeed` scales the CDD tick to `75 × readSpeed` Hz (lspc.cpp), so the sector pipeline
  and decoder IRQs run N× faster. One sector + one decoder IRQ per tick is preserved at every speed —
  the BIOS sees the exact same protocol, just clocked faster (like a CD drive that spins N× up).
- **Compatibility analysis**: NGCD has no drive-speed register and no wall-clock reference; the loader
  paces on instruction-counted loops and block counters ($7688/$76BC) that simply drain N× sooner.
  Seeks clear `statusCdc` bit 0 (delivery stops until the next READ), so there's no position drift —
  unlike PS1, where fast drives can break seek/Q-subchannel timing. The one real hazard (ring overflow:
  PT wrapping over data the BIOS hasn't DMA'd from the 0x8000-byte PT/DAC ring) is guarded in `tick()`
  — a sector is only written while `(PT + 2352 - DAC) & 0x7fff` leaves room. So 96x "Instant" is safe
  by construction; if a title ever misbehaves at a high multiplier, dropping the option a notch is the
  fallback.
- Distribution: pause menu → "CD Speed" dropdown (1x..96x) → SettingsStore `cd_speed_<system>` →
  `PhobosCore.setCdSpeed` → `ares::setCdSpeed` → `cdd.readSpeed`. Applied per-core (Map keyed by
  system name) and re-pushed on load in `MainViewModel.loadRom`.

## DMA model (working — keep as-is)
- LC8953 trigger is byte `$FF0061`: `$00` = load microcode, `$40` = start DMA. 68K byte store arrives as
  `CPU::write(0,1,$FF0060,$4040)` → `!upper && data.byte(0)==0x40` → `dma.start()`.
- `Dma::start()` modes match MAME `neogeo.cpp do_dma` (0xcffd, 0xe2dd, 0xfc2d, 0xfe3d/0xfe6d, 0xfef5,
  0xffc5/0xffcd/0xffdd). The loader streams directory/game sectors via repeated
  `ffc5 count=1024 a1=$111204` (CDC buffer → RAM) plus `fe6d` (word copy); the ffc5 source is
  `cdc.initTransfer()` (buffer+DAC, gated on DTTRG/IFCTRL). `$FF0080-$FF008E` (microcode upload) is a no-op — fine.

## Input model (implemented per libretro neocd — KEEP IT)
libretro `src/memory_input.cpp` is the authoritative controller model:
- **Selector**: byte write to **`$380001`** selects the controller bank. Data at `$300000`/`$340000`/`$380000`
  is only valid when selector ∈ {0x00, 0x12, 0x1B}; otherwise `$FF`/`$0F`.
  ⚠ 68K byte store to odd `$380001` arrives as `CPU::write(0,1,$380000,data<<8|data)` — detect via
  `!upper` on the `$380000` writeIO handler, store `data.byte(0)`.
- `$300000` P1CNT = `readButtons()`: bits 0-3 D-pad, 4-7 A/B/C/D, **active-low**.
- `$380000` STATUS_B (CD): **Start/Select live in byte(1)** (68K `move.b $380000` = even addr → `/UDS` →
  byte(1); libretro word read = `input3<<8 | 0xFF`). Low nibble = P1 Start(0)/P1 Sel(1)/P2 Start(2)/
  P2 Sel(3), active-low, idle 0x0F. Do NOT put Start/Select in byte(0).
- Controller-struct type seeding in `System::power()`: both byte lanes of `$10FD90-$10FDAF` set to 3
  (joystick) so the BIOS's `cmpi.b #$3,(a0)` detection at `$C09584` doesn't mask input to zero. Byte-lane
  gotcha: even address N reads byte 1 of word N>>1 — seed BOTH lanes.

## Verified on-device flow (final clean build)
1. CD player/menu renders (~30-45 s after launch); commands flow (GETTOC subs, status polls).
2. On Start (or with disc-ready set): boot-init runs — READ lba=13 (MSF 00:02:13) streams real directory
   sectors (`secRd` climbing at 75 Hz), decoder IRQs fire, DMA `ffc5`→`$111204` + `fe6d` copies land; the
   `$C0D4C4` compare passes.
3. BIOS sets `$10F656` bit 7 itself → Start gate latches `$76B9=0x80` → dispatch to game load
   (state 3 `$C0CA74`); game loader streams via ffc5 DMA; title screen renders.

## Reference implementations (use these, do not re-derive)
- **libretro neocd (WORKING)**: `/tmp/neocd_ref` — `src/memory_input.cpp` (selector + controller handlers),
  `src/cdromcontroller.cpp` + `lc8951.cpp` (status machine: Stop→Idle, Query→Stopped 0x90, Play→Playing),
  `src/hlebios.cpp` (`pollInput()`: BIOS RAM pad struct layout). NOTE: libretro uses a byte-packet LC8951
  serial protocol; the real CDZ BIOS uses the 10-nibble protocol with `$FF0160-$FF0167` — implement per MAME.
- **MAME**: `/tmp/opencode/mame/src/mame/shared/megacdcd.cpp` (lc89510 CDD — nibble protocol, CDD_Import
  tx[0] dispatch, getmsf_from_regs, CDD_Export status layout) and `/tmp/opencode/mame/src/mame/snk/neogeocd.cpp`
  (irq_update: ack bits 0x08/0x10/0x20 → vectors 0x17/0x16/0x15, DMA do_dma modes, register map).
  ⚠ MAME's neocd is MACHINE_NOT_WORKING (its type1/"decoder" IRQ is wired to scd_ctrl_checks, but its
  NeoCD_StatusHack never settles).
- Wiki: https://wiki.neogeodev.org/index.php/Main_Page (68k memory map, Memory_mapped_registers, DMA, LC8953).

## Tooling notes
- Static disasm: `ngcd_work/bios_swapped.bin` + `/tmp/disasm_bios.py` (capstone CS_ARCH_M68K). Beware data
  interleave — spammy regex scans over data pages produce false positives.
- All TEMP diagnostics (ST/WR/TYPWR/WRX/P1RD/SB1RD/SELW traces, cputrace2.txt, RPL/import/GETSTAT/dbgPrint,
  SECT/RAWREAD/initTransfer dumps, rdCtrl probe, SETTLE EXPIRE/HLE logs, DMA logs, IRQ counter print) have
  been **removed** from the final build. Markers to verify absence in the .so: `disc-detected`, `ST %d`,
  `DMA ffc5 dump`, `RAWREAD`, `cddBlock`.
- On-device visual checks: `adb shell screencap -p /sdcard/x.png && adb pull` then ASCII-render with PIL
  (model can't view images directly). The NotificationShade keeping focus blocks key events — reboot+unlock
  before testing input (adb `keyevent 108` = BUTTON_START works when the app has focus).

## Work state
- Committed & pushed (2026-09-01): NGCD M1 BIOS-boot skeleton (`2c5f7f444`); M2a disc plumbing
  (`9511cea73`); M2b CDD/CDC/DMA (`cbcf013a3`, `a7d4cdd82`, `1fd485e7e`, `8b4909a0f`, `aa6335cd3`);
  boot polish + CD-speed cap (`917c94eb2`, `d9830757e`, `2bc5a5e12`); rendering round (`9230e4d60`) +
  work-RAM dump in dumpNgGfx (`783a6cc27`).
- The bit-0 disc-detect poke remains as the documented HLE (`cdd.cpp settle → $10F656 bit 0`, gated on
  bit-7 clear).
- Open items: flag the unrelated AGP 9.3.1→9.3.2 bump in `android/gradle/libs.versions.toml`; verify NGCD
  audio on-device.

---

## 2026-08-28 appendix — rendering round (`9230e4d60` + `783a6cc27`, committed & pushed 2026-09-01)

**Fixed & user-verified (all playable/rendering at 59.2 FPS, SamSho RPG on RP6):** BIOS menu, NEO-GEO CD logo,
title, character select, level-select (was black), in-fight HUD (life bars, POW bars, KO counter, text),
characters, backgrounds.

1. **CD sprite DRAM plane order [1,0,3,2]** vs MVS cart [0,2,1,3] (Geolith `neo_spr` cdmode; libretro same)
   — `lspc/render.cpp:87-90`.
2. **`System::readC` lane**: `spriteRam.read(address >> 1).byte(!(address & 1))` (upload lane model from
   libretro `memory_mapped.cpp` byte@A → sprRam[A]) — was `.byte(address & 1)` (inverted).
3. **Sprite slots 1..381** on CD (MVS 0..380).
4. **Tile-number MSB**: CD `attributes.bit(4,7) << 12` masked `& 0x7fff` (Geolith `(attr & 0x00f0) << 12`
   then crommask=0x7fff); MVS keeps bits 16..19.
5. **DMA `0xe2dd`/`0xfc2d` odd-byte modes** — the HUD root cause: MAME heuristics zero-extend bytes into
   separate words `[b,0,b,0]`, halving the 4bpp planes on word-aligned tile data (black boxes, "88888888"
   glyphs, half-detail sprites). Rewritten to libretro's mirrored-word layout `[data, swap(data)]` =
   4 dest bytes per 16-bit source word — `dma.cpp:37-67`.
6. **OPNB ADPCM → `pcmRam` via `system.readVA`** (was 0xff on CD); PCM upload bank math fixed.
7. **CD read-speed feature removed** (user directive): 75 Hz 1x fixed. Unsound at >1x — the BIOS access
   machine reads CDC regs the pipeline hasn't written (DISC I/O ERROR ID=0000/0002, seen even at 2x).
8. **`dumpNgGfx` harness** kept: `adb shell am start -n com.phobos.emulator/.MainActivity --ez dump_ng_gfx
   true` → `ng_{spr,fix,vram,pram,wram}.raw` in `filesDir` (`PhobosCore.dumpNgGfx`, JNI → `System::dumpNgGfx`).

### Rendering residual → Task NGCD-M3 (open)
Title-menu text glitch. On-device VRAM/sprite-RAM dumps: the menu/HUD text tile families **`0x2000` /
`0x0c00` / `0x4376`** (+ `0x3400`, `0x4c60`) are **blank at base and populated at +0x80** (data at
tile*128+0x80). Populated-at-base tiles (`0x4667`, `0x5dcd`, `0x0c59`, `0x225e`) = characters/HUD parts
that render correctly.

- RULED OUT: global +0x80 tile fetch (broke BIOS menu/characters — block-specific, not universal).
- RULED OUT: 0xe2dd source-header skip +0x80 (title went full-screen-shifted; work-RAM source
  `0x115E06` is already plain tiles).
- Known: boot-phase direct transfer writes (`case 0`) first non-zero word at offset `0x82` — the game's
  own uploads carry a `[128-byte header][tiles]` shape for SOME blocks; characters/menu don't.
- Open hypothesis: the 0xe2dd dest bank at DMA time ≠ bank where 0x2000 lives; needs the game's CD-side
  file format (2026-09-01 static-analysis round underway: `chdman extractcd` of the SamSho RPG CHD +
  libretro/MAME/wiki refs + 68K disasm of the game's loader).

#### 2026-09-01 static round — NGCD disc file format (conclusions)

1. **Disc layout** (SamSho RPG EXTract): `chdman extractcd` → raw **2352-byte sectors** (MODE1/2352; LBA N at
   offset N*2352; user data at +16; the emulator's `vfs::cdrom` mapping at 2448*[LeadIn+LBAtoABA] is the
   IMAGE-side mapping — unrelated). PVD at LBA 16 (`CD001`).
2. **FS record format is NOT vanilla ISO9660**: dir records = length(1), LBA **32-bit LE at +2**, 8 bytes
   unknown at +4..+11 (BIOS file-header area?), **size 32-bit LE at +10**, name-len at **+32**, name at +33;
   len=0 → skip to next 2048 boundary. Matches libretro neocd `findFile()`/`readVolumeDescriptor()` exactly
   (they should be the working reference, not a generic ISO9660 parser).
3. **File table (84 entries)** for this disc: `045_P1.PRG` (lba 23, 1MB — main program), `F.FIX` (128KB),
   `IPL.TXT` (128B boot list!), `JC_0..3.SPR` (4 × 1MB, banks 0-3 = the title/menu/intro sprites!),
   `BK_0..C.SPR` (backgrounds), `PL_0..E.SPR/PAT/PCM` (players), `P045.Z80`, `JOCHU.PCM/PAT`, text .TXT
   files. **IPL.TXT** = `F.FIX,0,0 / 045_P1.PRG,0,0 / JC_0.SPR,0,0 / JC_1.SPR,1,0 / JC_2.SPR,2,0 /
   JC_3.SPR,3,0 / P045.Z80,0,0 / JOCHU.PCM,0,0 / JOCHU.PAT,0,0` — the boot list the BIOS loads.
4. **THE 128-BYTE PER-FILE HEADER IS REAL (on disc!):** every `.SPR` file begins with **128 zero bytes**,
   then the tile data (verified: PL_0.SPR data starts at +0x80; JC_*.SPR first non-zero byte at 0x80).
   The observed device-side "tile data at +0x80" = the game uploaded `[header][tiles]` — the +0x80 lead-in
   is a property of the game's DATA FILES, NOT an emulator artifact. A tilemap referencing tile N where the
   data lives at N+1 is therefore fully explained IF the game's tile numbering counts the header — the
   remaining question is which side (game numbering vs. emulator DMA landing) is off.
5. **DMA ground truth (Geolith `geo_cd.c` — the working reference, byte-for-byte):**
   - `0xe2dd`: `write(swapped_word)` then `write(word)` per source word = dest [s1,s0,s0,s1] (matches our
     `9230e4d60` rewrite through ares' LE word arrays — verified numerically).
   - `0xfc2d` (LC8951 buffer → dest): `write(data>>8)` then `write(data)` → dest bytes **[0x00, d0, d0, d1]**
     for word destinations. **OUR `9230e4d60` implementation produces [d0, 0x00, d1, d0] — ONE BYTE
     PHASE-SHIFTED vs the ground truth.** For BYTE-mapped dests (FIX/Z80/PCM, lane-1 writes) ours matches
     (only the low byte is taken). 0xfc2d-to-SPR (word dest) is therefore the **strongest candidate root
     cause for the residual title-menu text corruption** (a one-byte phase shift in the 4bpp stream = the
     observed slight misalignment/overlap, while the 0xe2dd-path HUD/characters are correct).
6. **MAME `neogeocd.cpp`** held no loader/fs logic (BIOS-side); used as docs-only for the register map.
   Geolith `geo_lspc.c` sprite fetch confirms NO +0x80 in the fetch path: `toffset = (tnum << 7) % csz`,
   `tpix` reads c[tbase+0..3] with the [1,0,3,2] CD order — our committed fetch matches.

**Next (on-device, instrumented):** log the 68K's 0xfc2d setups (dest address/count) + re-dump the title
menu; if a 0xfc2d→SPR transfer is confirmed, flip our 0xfc2d to the Geolith byte layout and verify the menu
text; if the write-side still doesn't explain tile 0x2000's +0x80 data, instrument the game's own upload
loop (68K disasm region-selected from `045_P1.PRG` at the upload sites found at 0x2314c/0x38001/0x99a3b/
0xc6625 fc2d references + device trace of the e2dd site).