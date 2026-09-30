# New systems: candidates and routes

**Written 2026-09-29, at the user's request.** The user's direction is for Phobos to become a
better RetroArch over time: the popular retro systems in one app, each emulated as accurately as
the best available emulator allows, behind Phobos's own interface. This page lists the popular
retro systems Phobos doesn't run yet, how each could be added, and the decisions to take first.
The [implementation plan](implementation-plan.md#open-task-inventory-and-disposition) has a row
for each; none of this work has started.

## Scope

- Retro systems only. The PlayStation 2 / GameCube generation (with the Wii and the original
  Xbox) is out of scope: the user judged it too complex to add properly (2026-09-29). The
  Dreamcast, from that generation, is in at the user's request, as are the handhelds of the
  PSP's generation.
- Newer systems (3DS, Vita, Wii U, Switch) are out.
- Phobos runs 27 systems today (the table in `android/app/src/main/cpp/PhobosJNI.cpp`): Atari
  2600, ColecoVision, Famicom (with the Disk System), Super Famicom, Nintendo 64 (with the 64DD),
  Game Boy, Game Boy Color, Game Boy Advance, SG-1000, Master System, Mega Drive, Game Gear, Mega
  CD, PlayStation, Neo Geo, Neo Geo CD, Neo Geo Pocket, Neo Geo Pocket Color, ZX Spectrum, ZX
  Spectrum 128, PC Engine, PC Engine CD, SuperGrafx, WonderSwan, WonderSwan Color, MSX and MSX2.

## Two ways to add a system

1. **A native core in the ares style.** Phobos's cores come from ares, and ares already
   implements many of the chips other systems use (`ares/component`): the 68000, Z80, SH-2,
   ARM7TDMI, 6502, V30 and HuC6280 CPUs, and the AY-3-8910, YM2149, YM2413, YM2612, SN76489 and
   MSM5205 sound chips. A core written this way works like the others (settings, save states, the
   pause menu, the performance work) and stays under ares's ISC license, but a large system takes
   years.
2. **An established open-source emulator.** For the large systems the best emulators already
   exist, and each took a decade or more to reach its compatibility: PPSSPP for the PSP, Flycast
   for the Dreamcast, Mednafen for the Saturn. Most maintain a libretro port (the interface
   RetroArch loads its cores through) in their own repositories. Phobos could host libretro cores
   built from source into the APK and implement the host side once: video (Vulkan or OpenGL ES
   rendering handed to Phobos's surface, and software frames), audio (into Phobos's AAudio output
   and its rate control), input (Phobos's mappings, hotkeys and touch layouts), core options (in
   Phobos's settings and the pause menu), saves and save states (Phobos's folders and slots, with
   previews), firmware (the Firmware screen) and frame pacing. Each emulator then stays close to
   its upstream, so updating one means moving a submodule, and accuracy fixes can be sent upstream.
   The same interface exposes the memory maps RetroAchievements needs (its rcheevos library is
   MIT-licensed). Integrating each emulator directly instead ties Phobos to its internals and
   costs more per emulator.

## Licensing

Nothing in Phobos puts it under the GPL today: ares is ISC-licensed, the bundled libraries are
under permissive licenses or the MPL (see `LICENSE`), and the one kernel header in them
(`src/hook/kgsl.h` in the libadrenotools submodule) carries the Linux syscall exception. The
emulators the large systems need are GPL,
all "version 2 or any later version": PPSSPP (its README), Flycast (its source headers) and
Mednafen (Debian's copyright file for it), checked 2026-09-29. melonDS, for the DS, is
GPL-3.0-or-later. ISC code may be combined with GPL code, but the app that bundles them is then
distributed under the GPL as a whole, and since some of their dependencies are Apache-2.0
(SPIRV-Cross in PPSSPP, for example), which version 2 of the GPL doesn't accept, that means
GPL-3.0-or-later in practice. For Phobos:

- The source of every release stays public, as it already is; each release ships the license
  and points to its exact source (the release tag).
- Anyone may fork and redistribute Phobos under the same license, and no closed-source code can
  be linked into the app.
- The ares-derived files keep their ISC notices; the GPL covers the combined app.

**Decided 2026-09-29:** the user accepts GPL-3.0-or-later for the whole app, switched when the
first GPL core lands (with the license text and notices updated in that change). Other licenses:
Handy (Lynx) is zlib-licensed and fits either way; FinalBurn Neo's non-commercial license is
incompatible with the GPL; most of MAME's drivers and devices are BSD-3-Clause, so they can be
ported into a native core under either license, although MAME as a whole is GPL-2.0-or-later.

## Keys

Phobos's rule is never to publish ROMs, firmware or keys. PPSSPP's source contains the keys for
decrypting commercial PSP games' executables, so bundling it puts them in the APK (the repository
would only reference PPSSPP's). **Decided 2026-09-29:** the user allows shipping PPSSPP as
upstream does, keys included, rather than asking users for a key file or decrypted games, which
would shut out most of them. That exception covers PPSSPP only: MAME's CPS-3 driver keeps
per-game keys in its source, and Flycast's NAOMI support should be checked for the same question,
before either is used.

## Systems ares already has

These are native, ISC and raise no licensing question: the ares tree Phobos builds already
contains them, but Phobos's system table doesn't list them.

- **Mega 32X and Mega CD 32X** (`ares/md/m32x`): the 32X's library (Virtua Racing Deluxe,
  Knuckles' Chaotix, Star Wars Arcade, Doom) and the handful of CD 32X games. Two SH-2s run beside
  the Mega Drive's 68000 and Z80, so measure on the RP6 first. **Added 2026-09-29** (branch
  `feature/sega-32x-2026-09`), with the SH-2 recompiler on and the 32X BIOS from the Firmware
  screen; not yet measured with a game.
- **Super Game Boy** (`ares/sfc/coprocessor/icd` with the Game Boy core): Game Boy games with the
  SGB's borders and palettes. Needs the Super Game Boy cartridge ROM as firmware, loaded with the
  game the way ares loads a second cartridge.
- **Arcade**: ares's N64-based Aleck64 boards (`ares/n64`) and Sega's SG-1000A (`ares/sg`), with
  MAME-format sets read by `mia/medium/mame.cpp`. A small set of games; the runner still picks the
  Aleck64 controls. Phobos listed Arcade until Task 35 removed it (2026-08-09; the reason wasn't
  recorded), so bringing it back is the user's call.
- **LaserActive** (Mega LD and PC Engine LD): niche, with very large disc images.
- **Pocket Challenge V2**: a WonderSwan variant for educational software; niche.

Each needs an entry in the system table, its loader checked on Android, firmware entries, a
touch layout, Library art and launch aliases, and afterwards Argosy's platform list.

## PSP

**Route: PPSSPP** (GPL-2.0-or-later), the most compatible and fastest PSP emulator. The user
first asked for a PSP core to rival PPSSPP, then accepted adopting PPSSPP itself, or a version
tuned for accuracy, if the licensing allows (2026-09-29). PPSSPP reimplements the PSP's operating
system instead of running Sony's, so it needs no firmware; it recompiles the Allegrex CPU to
ARM64 and draws the GE's output with Vulkan or OpenGL ES; its libretro port is in its main
repository. For a high-level emulator, more accuracy means compatibility fixes (sent upstream) and
preferring its more exact settings by default: native resolution with upscaling as an option,
buffered rendering, no frame skipping, the default CPU clock, fast memory off.

Phobos's side: the libretro host, a PSP touch layout (d-pad, analog nub, four face buttons, L and
R, Start and Select), a PSP folder for the memory stick (PPSSPP keeps saves as a memory stick
directory tree), and loading the image formats PPSSPP accepts (ISO, CSO, PBP and others); the
keys ship with PPSSPP (see Keys). First milestone: the core builds into both flavors, boots homebrew and then
a game from the user's library, and matches the standalone PPSSPP app's frame rate on the RP6.

## Dreamcast, NAOMI and Atomiswave

**Route: Flycast** (GPL-2.0-or-later), the most accurate and compatible open-source Dreamcast
emulator, actively developed, with Android and libretro builds. Redream and Demul are
closed-source, lxdream is abandoned, and MAME's Dreamcast driver is far too slow. Flycast
recompiles the SH-4 to ARM64, renders with Vulkan or OpenGL ES, and also runs the NAOMI, NAOMI 2
and Atomiswave arcade boards. Accuracy-leaning defaults: per-pixel transparency sorting where the
GPU keeps up, native resolution with upscaling as an option, and full MMU emulation for the games
that need it. The BIOS is optional (Flycast's built-in replacement boots most games; the real
`dc_boot.bin` and `dc_flash.bin` through the Firmware screen; NAOMI and Atomiswave BIOS sets for
the arcade games).

Phobos's side: the libretro host, a Dreamcast touch layout with analog triggers, VMU saves (and
the VMU's screen, drawn beside the game or on the HUD), GDI, CDI and CHD images, arcade sets, and
light guns later.

## Saturn

The ares tree has only a Saturn stub (`ares/saturn`, 190 lines). The user wants the Saturn
working eventually (2026-09-28, again 2026-09-29). Routes:

1. **Mednafen's Saturn emulation** (Beetle Saturn in libretro, GPL-2.0-or-later): the most
   accurate open-source Saturn emulator, but CPU-heavy (interpreted SH-2s with cache emulation,
   software rendering), so it needs measuring on the RP6, where the profiling approach used for
   the N64 applies. Recommended first, for accuracy.
2. **Yaba Sanshiro** (GPL; built for Android, with an OpenGL ES renderer) is faster and less
   accurate. **Kronos** (GPL-2.0-or-later) renders with compute shaders written for desktop
   OpenGL, a poor fit for Android.
3. **A native core** grown from the stub. ares already has the SH-2 (from the 32X) and the 68000
   (the sound CPU); VDP1, VDP2, the SCU and its DSP, the SCSP, the SMPC and the CD block would be
   new. Several years of work.

Needs the Saturn BIOS (Firmware screen), backup RAM and cartridge saves, the RAM cartridges some
games need, and a touch layout (six face buttons, L and R, Start).

## Nintendo DS

Not requested; suggested because it is among the most-played systems of the PSP's generation and
suits touch screens. **Route: melonDS** (GPL-3.0-or-later), the most accurate open-source DS
emulator, with an ARM64 recompiler and a libretro port (melonDS DS); DeSmuME (GPL-2.0-or-later) is
older and less accurate. Needs two screens (stacked, side by side, or one large and one small,
swapped by a hotkey; on dual-screen handhelds such as the AYN Thor, the second display through
Android's Presentation API), touch on the lower screen by tapping the game's picture directly,
microphone input, and optional firmware (melonDS has free replacements; DSi mode is out of scope).

## Arcade beyond Neo Geo

Capcom's CPS-1 and CPS-2, Sega's System 16 and 18, and CPS-3 are among the most played arcade
boards on handhelds.
**Recommended route: native boards in the ares style**, reusing ares's CPUs (68000, Z80, the SH-2
for CPS-3, the V30 for Irem's boards) with new sound chips (YM2151, OKI MSM6295, QSound, UPD7759),
ported from MAME's drivers, which are mostly BSD-3-Clause (see Licensing). CPS-1 and CPS-2 first.
MAME itself (GPL-2.0-or-later) through libretro is very large and slow on phones; FinalBurn Neo is
fast and popular, but its non-commercial license rules it out under the GPL. Sets follow one MAME
version's names (to be chosen); CPS-2 sets carry their decryption keys in the set, CPS-3's are in
MAME's source (see Keys).

## Smaller consoles and handhelds

One task each, roughly in order of demand:

- **Atari 7800**: native, with ares's 6502, the TIA from the Atari 2600 core for sound, and new
  MARIA graphics plus the POKEY some cartridges carry; no BIOS needed. References: ProSystem and
  a7800 (GPL).
- **Atari Lynx**: Handy (zlib, so no license change; it boots without the BIOS through its own
  replacement) or a native core (its 65SC02 has CMOS instructions ares's 6502 lacks). Its
  rotated games suit handhelds.
- **Virtual Boy**: Mednafen's (GPL-2.0-or-later) or native (NEC V810 CPU, VIP video, VSU sound);
  shown to one eye, as a red/cyan anaglyph, or side by side for 3D viewers.
- **Intellivision**: native. The CP1610 CPU and STIC video are new, and the AY-3-8914 is in ares's
  AY-3-8910 family. Needs the EXEC and GROM ROMs; a touch keypad with each game's overlay.
  References: jzIntv and FreeIntv (GPL).
- **Vectrex**: native. The 6809 CPU and 6522 VIA are new, and the sound chip is ares's AY-3-8910.
  A vector renderer with phosphor persistence and the game overlays; needs the system ROM.
  References: vecx and MAME.
- **Sega Pico**: native on ares's Mega Drive core, with the storyware pages and the pen as touch
  input, and a UPD7759 (shared with Sega's System 16).
- **Atari 5200** (and the Atari 8-bit computers): Atari800 (GPL-2.0-or-later) or native (the 6502
  with ANTIC, GTIA and the 7800's POKEY).
- **3DO**: Opera (libretro; license to check; needs a BIOS) or a large native core (the ARM60 CPU,
  close to ares's ARM7TDMI, with MADAM and CLIO).
- **Atari Jaguar**: large. The 68000 is ares's; the Tom and Jerry RISC processors, the blitter and
  the object processor are new. Virtual Jaguar (GPL-3.0) is weak, and the best Jaguar emulator
  (BigPEmu) is closed-source.
- **PC-FX**: Mednafen's (GPL-2.0-or-later) or native, reusing ares's PC Engine video and CD parts
  with the Virtual Boy's V810; a small, Japan-only library.
- **Odyssey² / Videopac, Pokémon Mini, Game & Watch**: small libraries. References: O2EM, PokeMini
  and MAME.

## Retro computers

Lower priority on a handheld, and they need an on-screen keyboard and physical keyboard support:

- **Commodore 64**: VICE (GPL-2.0-or-later), or native with ares's 6502 family plus the VIC-II,
  SID, CIAs and the 1541 drive.
- **Amiga**: PUAE or WinUAE (GPL), or native with ares's 68000 plus the OCS/ECS/AGA chipset (large).
- **Amstrad CPC**: Caprice32, or native with ares's Z80 and AY-3-8910 plus the CRTC and gate array.
- **Atari ST**: Hatari (GPL-2.0-or-later), or native with ares's 68000 and YM2149 plus the MFP,
  shifter and floppy controller.
- **DOS**: DOSBox Pure or DOSBox-X (GPL-2.0-or-later).
- **MSX2+ and turbo R**: extending ares's MSX core.

## Suggested order

A suggestion; the user sets the order.

1. The systems ares already has (native, no license question), starting with the 32X and the Super
   Game Boy.
2. The libretro host with the PSP (PPSSPP), then the Dreamcast (Flycast), then the Saturn
   (Mednafen); the licensing and keys questions were settled on 2026-09-29.
3. The DS (melonDS), if wanted.
4. Arcade boards (CPS-1, CPS-2), then the smaller systems by demand.
