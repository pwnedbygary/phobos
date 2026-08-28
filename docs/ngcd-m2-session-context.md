# Neo Geo CD (NGCD) — Session Context for openCode handoff

> Drop-in briefing for a fresh LLM/engineer continuing this work. Read this first, then
> `docs/handoff.md` and `docs/implementation-plan.md` (Task NGCD-M2) for the always-current repo state.
> Public hardware reference: https://wiki.neogeodev.org/index.php/Main_Page

## Objective — ✅ COMPLETE (2026-08-27): SamSho RPG boots
Fix **Neo Geo CD** boot on Phobos Android so the BIOS boots the game. Game code must reach `0x100000`.

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
  `settleCounter=150` (~2 s @ 75 Hz). `tick()` then transitions to `0x09` (CdStopped), and **pokes
  `$10F656` bit 0** (`w.byte(1) |= 0x01`) when a disc is present. That bit is what triggers the BIOS's own
  boot-init (`$C0CEC4`), which runs the TOC/position phase and sets bit 7 itself. ⚠ Do NOT poke bit 7
  directly: it auto-boots the loader before the TOC exists → garbage READ (`0xFF` MSF → DISC I/O ERROR).
  (The natural setter `$C007CA`/`$C00858` never executes in this emulator's path; bit-0 poke is the standing HLE.)

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
- Committed: NGCD M1 BIOS-boot skeleton (`2c5f7f444`); M2a disc plumbing (`9511cea73`); M2b CDD/CDC/DMA.
- Uncommitted (this session, all verified): raw-sector LBA offset in `Disc::readSectorRaw`; decoder IRQ
  vector fix (`raiseType1`→`type3Pending` = vector `$15`); ack-based type pending + line re-raise in
  REG_IRQACK; IPL-gated CDD dispatch (`2 > r.i`); `prohibitIrq`-aware type2 tick; strip of ALL temp
  diagnostics. The bit-0 disc-detect poke remains as the documented HLE.
- Open items (non-blocking): decide C→X/D→Y remap for the 4-face-button layout on the Retroid;
  flag the unrelated AGP 9.3.1→9.3.2 bump in `android/gradle/libs.versions.toml`.