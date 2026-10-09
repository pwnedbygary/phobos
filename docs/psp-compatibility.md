# PSP Compatibility Report

**Date:** 2026-10-09
**Tested commit:** `a6042ccde` (branch `cursor/psp-hle-games10-2b67`, on top of #183's `cursor/psp-hle-games9-2b67`)
**Games tested:** 266
**Runner:** `tools/psp-runner/` (headless; 3600 frames, Start at frame 120 and Cross at 1800, 4 PNG captures, 1 WAV)
**Updated:** 2026-10-09, part 55 (`f30eefe09`, branch `cursor/psp-hle-games11-2b67`, on #184's
`cursor/psp-vk-play-2b67`): the rows its fixes changed, run again the same way beside the base commit's runner on 53
games (docs/psp-core.md, part 55); the other rows are the report's.

## Method

Each game was run for 3600 frames (60 s of PSP time), as the last report's run went:

- `--frames 3600`
- `--press "120:Start,123:Start!,1800:Cross,1803:Cross!"`
- `--png-at 60,300,1200,3600`
- `--wav dir/sound.wav`
- `--ge-threads 7` (the software renderer)
- `--fonts <fonts>`
- `--memory-stick <a fresh folder each>`

Three runs are compared, row for row:
- **Before**: the last report, `b27f084a5` (2026-10-07).
- **Run 1**: the stack's tip as this branch began, `a2b48f8f8` (#183: the Vulkan renderer, the speedups, the VFPU
  carry fix, Ridge Racer 2's keyboard), run as the library was copied in.
- **Now**: this branch's tip (`a6042ccde`), with the fixes below.

Each game's four frames were judged by eye: **menu** (a usable screen: title, menu, prompt, notice waiting for a
button), **gameplay** (the game itself, or its attract demo), **movie** (an intro, cutscene or logos playing),
**loading**, **black** (nothing on screen by the end, the program running) and **hang** (black, the CPU stopped or
every thread waiting); a game killed at the 1200 s limit is **timed out**. A game whose last frame happened to fall
between two scenes of a sequence was run again with a frame every 300 to judge it. A second run part way through the
branch (run 2) was judged the same way; in Now (run 3, the branch's end), a game whose frames came out the same as in
run 2, or only a little apart (a movie's or a fade's timing: under 1 of 255 per channel), keeps run 2's judgement,
and the other 60 were judged again by eye. For the comparison with Before, gameplay counts as menu and hang as
black, as the last report's categories had them.

The last report's runs pressed nothing: the runner's presses were broken then (fixed since). Some games that showed
a menu then now take the presses and go on to a movie or a demo, or to a screen the old runs never reached. Every
game that looked worse was run again without presses on both commits before calling it a regression.

## Summary

| Category | Before (`b27f084a5`) | Run 1 (#183's tip) | Now | Part 55 | Change since Before |
|---|---|---|---|---|---|
| Menu or gameplay | 141 | 148 | 175 | 178 | +37 |
| Movie | 35 | 43 | 63 | 63 | +28 |
| Stuck loading | 13 | 8 | 5 | 4 | -9 |
| Black or hang | 73 | 64 | 21 | 19 | -54 |
| Timed out | 4 | 3 | 2 | 2 | -2 |

Part 55 moves three rows: The King of Fighters - Orochi Saga (sceCcc) and God Eater 2 (thread-local storage pools and
sceKernelTryLockLwMutex_600) from black to a menu or notice, and Persona 3 Portable, judged again with a frame every
300, from loading to its title (its frame 3600 falls on its attract loop's loading screen). Dragon Ball Z: Tenkaichi Tag
Team (sceKernelExtendThreadStack) shows its autosave notice now, but is black again at 3600, waiting on PGD-encrypted
data.

In the comparison, gameplay counts as menu and a hang as black, as the last report's categories had them. Against
the last report, 81 games are further along and 26 rank lower. None of the 26 is a regression left unfixed:
- **The runner's presses** (13): the last report's runs pressed nothing, and these go on from a menu or notice when
  Start and Cross come: 50 Cent: Bulletproof, Daxter, Guilty Gear Judgment, IL-2 Sturmovik, MACH, MediEvil
  Resurrection, Micro Machines V4, Pursuit Force, Riviera, Star Wars: The Force Unleashed, Street Supremacy (to a
  loading screen), ZHP, and Persona 3 Portable (to a loading screen it doesn't leave: scesupPreAcc is missing). Run
  again without presses, each shows its menu or notice at frame 3600, as before. Need for Speed: ProStreet is one too,
  but what it does after the press is a crash (the CPU stops in its main thread): a real bug, listed below.
- **Further than before** (5): Fuuun Shinsengumi, Spectral Souls and Valhalla Knights 2 stopped at a "no Memory Stick"
  or "not enough space" notice and now go on into their intros (Spectral Souls to its main menu); Ultimate Ghosts 'n
  Goblins stopped at a "no game data" prompt; Soulcalibur: Broken Destiny lacked scePsmfPlayer and skipped its intro
  movie (it plays now).
- **Mega Man Maverick Hunter X** skipped its movie while scePsmf was missing; it plays it now, to its anime cutscene
  (one of the regressions fixed above).
- **The same at the last report's commit** (6): a runner built there and run without presses shows Ape Escape: On the
  Loose and Monster Kingdom: Jewel Summoner in the same intro at frame 3600 (the last report judged those frames a
  menu), and Metal Gear Solid: Portable Ops and Portable Ops Plus loading, PaRappa the Rapper and Valhalla Knights
  black.

## What changed since the last run

The last report was made at `b27f084a5`. Since then the stack under this branch added scePsmf and scePsmfPlayer
(#160), the Vulkan renderer, speedups, the VFPU carry fix and Ridge Racer 2's keyboard (#183's stack), and this
branch added the fixes below (docs/psp-core.md, part 51, has the evidence for each; each is its own commit with
tests).

**Regressions found and fixed.** Three games were worse than in the last report because of code, not the runner:
- *Mega Man Maverick Hunter X* and *Juiced: Eliminator* had skipped their movies while scePsmf was missing; once
  #160 added it they played them through sceMpeg and stalled on a black screen. Three fixes: a ring takes nothing
  past the movie's end (Mega Man's ring callback reads a file that goes on past its movie), a frame width of 0
  means the library's (Juiced), and an empty sound unit decodes to silence once the movie has had sound (Juiced
  drains its sound as each movie ends). Both play their movies again and go on.
- *Patapon 2* deadlocked at boot in this branch's own second run, after the memory stick callback began to be
  notified as it's registered: a semaphore's CB wait now runs its notified callbacks first when it has no timeout
  and nobody else is in line, as the game needs. It reaches its save prompt.

The other rows that looked worse aren't regressions: games that now get the runner's presses (the last report's
runs pressed nothing) go on from a menu to a movie, a demo or a screen the old runs never reached, and every such
game was run again without presses on both commits to check; others were black at the last report's commit too.

**Fixes, by what they unblocked:**
- *The memory stick*: a stick callback is told the stick is in as it's registered (pspautotests' mstick). 50 Cent,
  Dirt 2, Crisis Core, Hexyz Force, the Patapons, Parodius, Sonic Rivals 1 and 2, Ys: The Ark of Napishtim, Fuuun
  Shinsengumi, Valhalla Knights 2 and Spectral Souls said no stick was inserted, or that it was full.
- *Loading and starting programs*: Sony's static EBOOTs without sections find their module info; the program's
  first thread is made as a module's start thread; a PRX program goes 16 KiB into user memory, where pspautotests'
  PRXs have their code (fan translations call their added code by address: Persona 2: Eternal Punishment, both
  Tales of Phantasia translations, Fate/Extra's patch, two Armored Core True Analogs patches, Growlanser IV).
- *Movies*: the three regression fixes above; a ring may have more than 4096 packets (Sega Rally Revo's 4800);
  sceMpegAvcDecodeDetail gives the picture's width and height (Spectral Souls drew its movies by them); a ring made
  for a game's own copy of the library is 44 bytes (Miami Vice kept its movie file's descriptor after it, Crash Tag
  Team Racing something it crashed without); and a
  movie has ended once its whole stream has been given (Cars: Race-O-Rama, Pursuit Force: Extreme Justice, MX vs.
  ATV Untamed, Blitz: Overtime, both Death Jr. games, Disaster Report 3 and Dead Head Fred waited for good for their
  movies' last pictures).
- *Missing functions*: sceUtilityLoadUsbModule/UnloadUsbModule, network modules 0x107 and 0x108 (Macross: Triangle
  Frontier), sceRtc's day of the week, 64-bit time_t and clock-set times, sceKernelStopUnloadSelfModule,
  sceIoAssign, sceImposeSetUMDPopup, sceUsb start/stop/activate/deactivate, sceDisplayIsForeground,
  sceKernelReferSystemStatus, sceUtilitySetSystemParamInt.
- *Odds and ends*: a file without PGD's header is read as it is once its key is given (7th Dragon 2020); a fixed
  pool's alignment of 0 is the default (Brothers in Arms); writing save data needs the save to be there (Hot Shots
  Golf 2); the savedata sizes mode reads no names of its own (Valhalla Knights 2).

## The black-screen games from the last report

The last report listed 22 games that had gone from a menu to black (the RP6 showed about 21 black). Where they are now:

| Game | Before | Run 1 | Now | Now: what shows, or why it's still black |
|---|---|---|---|---|
| Black Wolves Saga - Last Hope | black | black | black | black throughout: its CRI file system opens PSP_GAME/INSDIR/INSTALL.DNS, PGD-encrypted, gives it its key (ioctl 0x04100001) and closes it, 31,000 times by frame 2400; the kernel can't decrypt PGD yet and refuses the key |
| Blitz - Overtime | black | black | menu | title screen (black from 1200 before: its logo movies never ended) |
| Castlevania - The Dracula X Chronicles | black | movie | movie | intro movie |
| Crisis Core - Final Fantasy VII | black | black | menu | title menu |
| Disney-Pixar Cars - Race-O-Rama | black | black | gameplay | racing attract demo (black after its logos before: its movie never ended) |
| Fat Princess Fistful of Cake | black | menu | menu | autosave prompt |
| Fate Extra CCC [English v1.0] | black | black | menu | title menu |
| Gitaroo Man Lives! | black | movie | movie | intro |
| Guilty Gear XX Accent Core Plus | black | movie | movie | intro |
| Gungnir | black | menu | menu | title menu |
| Hexyz Force [UNDUB v1.2b] | black | black | menu | story select |
| Juiced 2 - Hot Import Nights | black | timeout | movie | intro and a 3D club scene (159 fps) |
| Killzone - Liberation | black | gameplay | gameplay | in the first level (4.8 fps) |
| King of Fighters, The - Orochi Saga | black | black | menu | autosave notice, after its logo and intro (part 55: its first call, sceCccDecodeUTF8, was missing, and it spun there at 8 fps) |
| Macross - Triangle Frontier [Japan] | black | black | menu | autosave notice, then a soundtrack folder prompt (black before: utility module 0x108 refused) |
| Melodie (Prototype) | black | black | black | black throughout: it asks for one 40 MiB block of user memory (sceKernelAllocPartitionMemory), more than a 32 MB PSP gives, and gets nothing (a prototype expecting a 64 MB PSP's memory) |
| Miami Vice - The Game | black | black | menu | title, Press START (black before: its ring's 12th word over its movie file) |
| Midnight Club - L.A. Remix | black | movie | menu | title (its attract scene at 3600 before) |
| Pangya Fantasy Golf [Black Screen Fix] | black | menu | menu | title menu |
| Ridge Racer | black | black | black | the Pac-Man loading game until frame 3000, then its opening movie (two pictures, at 3100) and black frames after it, drawn every frame on both renderers; cause not found |
| Super Stardust Portable | black | menu | menu | main menu |
| Toca Race Driver 2 | black | menu | menu | profile select |

19 of the 22 show something now: 15 a menu, a title or a game, 4 a movie (The King of Fighters since part 55's
sceCcc). Three are still black, each for a reason of its own:
- **Black Wolves Saga - Last Hope** reads its install data through CRI's file system: it opens
  `PSP_GAME/INSDIR/INSTALL.DNS`, gives it its PGD key and closes it, over and over (31,000 times by frame 2400). The
  file really is PGD-encrypted, and the kernel can't decrypt PGD yet, so it refuses the key (four more games wait on
  PGD the same way: part 55).
- **Melodie (Prototype)** asks for one 40 MiB block of user memory, more than a 32 MB PSP gives, and stops when it
  gets nothing: a prototype expecting a 64 MB PSP's memory without saying so in its PARAM.SFO.
- **Ridge Racer** shows its Pac-Man loading game until frame 3000, then plays its opening movie (two pictures) and
  draws only black frames after it, every frame, on both renderers. Not found yet.

## Speed distribution

| Range | Games |
|---|---|
| < 30 fps | 8 |
| 30–60 fps | 15 |
| 60–200 fps | 77 |
| > 200 fps | 164 |

These are the runner's own frames per second: unthrottled, `-O1`, `BUILD_DEBUG`, 7 GE threads, on a Mac shared with
other work (load averages of 40 to 140 during this run), so treat them as relative. The 8 under 30 are mostly 3D games
keeping the GE busy: Killzone (3 fps), Army of Two (4), Super Monkey Ball Adventure (8), Activision Hits Remixed (9),
The King of Fighters - Orochi Saga (9, black then; 390 and its autosave notice since part 55), Need for Speed: Carbon -
Own the City (10) and Underground Rivals (14), Death Jr. II (17). God of War: Chains of Olympus (its main menu by frame
1200) and Ghost of Sparta (barely past frame 60) were killed at the 1200 s limit.

## Top missing functions

12 of 266 games hit at least one function not implemented yet (since part 55: 14 in the report's run).

| Games | Function | Which |
|---|---|---|
| 3 | `scesupPreAcc 86debd66` | Dissidia 012 - Duodecim Final Fantasy, Dissidia Final Fantasy, Shin Megami Tensei - Persona 3 Portable |
| 3 | `scePower a85880d0` | Dragon Ball Z - Tenkaichi Tag Team, Final Fantasy Type 0, Kingdom Hearts Birth by Sleep Final Mix [English] |
| 1 | `sceAtrac3plus sceAtracLowLevelInitDecoder` | Corpse Party - Sweet Sachikos Hysteric Birthday Bash [English 04-29-2026] |
| 1 | `sceMt19937 sceMt19937Init` | Genso Suikoden Tsumugareshi Hyakunen no Toki |
| 1 | `sceJpeg sceJpegCsc` | Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] |
| 1 | `sceMpeg sceMpegAvcConvertToYuv420` | Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] |
| 1 | `scePauth 98b83b5d` | Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] |
| 1 | `sceP3da 374500a5` | Sol Trigger |
| 1 | `sceHprm sceHprmRegisterCallback` | Soulcalibur - Broken Destiny |
| 1 | `sceRtc e6605bca` | Tekken 6 |
| 1 | `sceRtc f2a4afe5` | Tekken 6 |

In the last report 77 games stopped at a function not implemented yet; now 12 do, and no function stops more than
three. Part 55 added sceCcc (The King of Fighters - Orochi Saga, Genso Suikoden), the thread-local storage pools and
sceKernelTryLockLwMutex_600 (God Eater 2) and sceKernelExtendThreadStack (Dragon Ball Z: Tenkaichi Tag Team, which
then calls scePower a85880d0 and goes on). The ones that leave a game black: scePauth, sceJpegCsc and
sceMpegAvcConvertToYuv420 (Monster Hunter Portable 3rd).

## Top remaining blockers

What still stops games, by cause (the per-game table has each game's note; updated for part 55):
1. **PGD decryption**, five games: Black Wolves Saga's install data, Dragon Ball Z: Tenkaichi Tag Team (past its
   autosave notice), Shining Blade and Valkyria Chronicles III (`PSP_GAME/INSDIR/DATA.BIN`, through CRI's file system)
   and Naruto Shippuden Ultimate Ninja Impact. Each gives its file's key and, refused, tries again for good. The kernel
   reads unencrypted data but can't decrypt PGD: AMCTRL's cipher isn't described in a source this project uses, and its
   checks need keys not committed (docs/psp-core.md, part 55).
2. **Libraries the kernel doesn't have yet**: scePauth and sceJpeg (Monster Hunter Portable 3rd).
3. **Black screens with the game running and no missing function**, cause not found: Ridge Racer (black frames after
   its opening movie), Crush, Dead or Alive - Paradise, Def Jam - Fight for NY (its file thread waits for requests that
   never come), Jak and Daxter - The Lost Frontier, Tekken - Dark Resurrection (after its autosave notice), Valhalla
   Knights (idle), PaRappa the Rapper (black at the last report's commit too). Need for Speed - Most Wanted 5-1-0 waits
   for good in sceGeDrawSync, the GE having stopped a list at a RET with no CALL (a matter for the GE).
4. **Crashes**: Need for Speed - ProStreet (the CPU stops in its main thread after its notice).
5. **Stuck loading**: Metal Gear Solid - Portable Ops and Portable Ops Plus, MX vs. ATV - On the Edge, Street Supremacy
   (a map loading screen, after the run's presses).
6. **Too slow to judge**: God of War: Chains of Olympus and Ghost of Sparta.
7. **Memory**: Melodie (Prototype) wants a 64 MB PSP's memory; Monster Hunter Portable 3rd HD's program is 26.5 MiB,
   more than the 24 MiB user partition (made for the PS3), and given more it stops at sceJpeg as the PSP version does.

## Vulkan spot checks

The 11 games that reached gameplay in the first run, run on this branch's code to frame 2400 with the run's presses,
once with the software renderer and once with `--renderer Vulkan` (MoltenVK on the M1):

| Game | Software fps | Vulkan fps | Mean difference at 300 / 1200 / 2400 |
|---|---|---|---|
| Grand Theft Auto - Sindacco Chronicles [v2.0 English] | 72 | killed at the limit | 0.11 / 0.08 / — |
| Harvest Moon - Hero of Leaf Valley | 173 | 34 | 0.00 / 0.00 / 1.59 |
| Killzone - Liberation | killed at the limit | 4 | 0.00 / 0.01 / — |
| Lumines - Puzzle Fusion | 276 | 179 | 0.00 / 0.00 / 0.00 |
| Lumines II | 357 | 62 | 0.00 / 0.00 / 0.02 |
| Manhunt 2 [Uncensored] | 213 | 69 | 0.00 / 0.00 / 0.00 |
| Mega Man Powered Up [UNDUB v1.3] | 215 | 299 | 0.00 / 1.89 / 0.00 |
| Seen in Liberty City | 83 | 3 | 0.13 / 0.11 / 0.05 |
| Star Wars - Battlefront II - Remastered Edition [Hack v8] | 344 | 27 | 0.04 / 0.09 / 0.05 |
| The Sims 2 | 158 | 18 | 6.19 / 0.00 / 0.02 |
| Warriors of the Lost Empire | 434 | 355 | 0.00 / 0.00 / 0.00 |

Vulkan draws the same pictures as the software renderer: the mean difference per channel is under 0.2 of 255 for
most frames, and the few larger ones (Harvest Moon at 2400, Mega Man Powered Up at 1200, The Sims 2's animated logo
at 300) are the same scene a frame of animation apart. Its speed is the difference: 3D gameplay is much slower than
software (Seen in Liberty City 3 fps against 83, Star Wars Battlefront II 27 against 344, The Sims 2 18 against 158,
GTA: Sindacco Chronicles under 3 against 72, killed at the limit), while Mega Man Powered Up runs faster.

## Per-game results

Each row: title, NPID, the runner's speed in run 3 (fps), the state in the last report (Before), in run 1
and now, and what the last frames showed or why a game stops.

| Game | NPID | fps | Before | Run 1 | Now | Notes |
|---|---|---|---|---|---|---|
| 007 - From Russia with Love | ULUS10080 | 212 | menu | menu | menu | profile prompt |
| 50 Cent - Bulletproof - G-Unit Edition | ULUS10128 | 463 | menu | menu | movie | autosave notice, then legal and logo movies (black only between them at 3600) |
| 7th Dragon 2020 [English v0.91] | NPJH50459 | 271 | black | black | menu | "System data will be saved" prompt |
| 7th Dragon 2020-II [English Patched v0.91] | NPJH50716 | 347 | black | black | menu | "System data will be saved" prompt |
| Ace Combat - Joint Assault | ULUS10511 | 193 | black | movie | movie | legal notice after autosave notice |
| Ace Combat X - Skies of Deception | ULUS10176 | 117 | loading | movie | movie | intro movie |
| Aces of War [Europe] | ULES00590 | 241 | movie | menu | menu | title, press start |
| Activision Hits Remixed | ULUS10186 | 9 | menu | menu | menu | game menu (8.7 fps) |
| After Burner - Black Falcon | ULUS10244 | 394 | movie | movie | movie | attract demo |
| Angus Hates Aliens | NPUZ00374 | 488 | menu | menu | menu | title menu |
| Ape Escape - On the Loose | UCUS98609 | 247 | menu | movie | movie | intro cutscene |
| Ape Escape Academy | UCUS98619 | 342 | menu | menu | menu | title, press start |
| Archer Maclean's Mercury | ULUS10017 | 871 | menu | menu | menu | main menu |
| Armored Core - Formula Front International [True Analogs v1.0] | ULJS19001 | 66 | loading | loading | movie | intro with credits (81 fps) |
| Armored Core 3 Portable [True Analogs Mod v1.02] | NPUH10023 | 77 | movie | movie | movie | intro movie |
| Armored Core Last Raven Portable [True Analogs Mod v1.03] | NPUH10024 | 63 | movie | movie | movie | intro movie |
| Armored Core Silent Line Portable [True Analogs v1.02] | NPUH10025 | 67 | movie | hang | movie | logo, intro, white at 3600 |
| Army of Two - The 40th Day | ULUS10472 | 4 | movie | menu | menu | main menu (4.6 fps) |
| ATV Offroad Fury - Blazin' Trails | UCUS98603 | 337 | menu | menu | menu | autosave warning |
| ATV Offroad Fury Pro | UCUS98648 | 118 | black | black | menu | autosave warnings |
| Battle vs. Chess [Europe Proto] | ULES01517 | 157 | menu | menu | menu | title, press start |
| Black Wolves Saga - Last Hope | ULJM06220 | 392 | black | black | black | black throughout: its CRI file system opens PSP_GAME/INSDIR/INSTALL.DNS, PGD-encrypted, gives it its key (ioctl 0x04100001) and closes it, 31,000 times by frame 2400; the kernel can't decrypt PGD yet and refuses the key |
| BlazBlue - Calamity Trigger | ULUS10519 | 330 | menu | menu | menu | name entry |
| BlazBlue - Continuum Shift II | ULUS10579 | 412 | menu | menu | menu | name entry |
| Blitz - Overtime | ULUS10200 | 56 | black | black | menu | title screen (black from 1200 before: its logo movies never ended) |
| Brandish - The Dark Revenant | NPUH10195 | 235 | black | menu | menu | title menu |
| Brave Story New Traveler | ULUS10279 | 50 | movie | menu | menu | title emblem |
| Brothers in Arms - D-Day | ULUS10193 | 87 | menu | black | menu | title, press start |
| Bubble Bobble Evolution | ULUS10143 | 115 | movie | movie | movie | intro movie |
| Burnout Dominator | ULUS10236 | 58 | menu | menu | menu | title, press start |
| Burnout Legends | ULUS10025 | 67 | menu | menu | menu | title, press start |
| Castlevania - The Dracula X Chronicles | ULUS10277 | 385 | black | movie | movie | intro movie |
| Chili Con Carnage | ULUS10216 | 152 | menu | menu | menu | profile menu |
| Cladun - This Is An RPG | NPUH10072 | 393 | menu | menu | menu | title menu |
| Colin McRae Rally 2005 Plus [Europe] | ULES00111 | 50 | menu | menu | menu | auto-save prompt |
| Corpse Party - Sweet Sachikos Hysteric Birthday Bash [English 04-29-2026] | ULJM06114 | 295 | menu | menu | menu | title, press start; sceAtracLowLevelInitDecoder missing |
| Crash Tag Team Racing | ULUS10044 | 134 | black | black | menu | Memory Stick check notice (black after the legal screen before) |
| Crazy Taxi - Fare Wars | ULUS10273 | 299 | menu | menu | menu | title |
| Crimson Gem Saga | ULUS10400 | 129 | menu | menu | menu | system data notice |
| Crisis Core - Final Fantasy VII | ULUS10336 | 138 | black | black | menu | title menu |
| Crush | ULUS10238 | 154 | black | black | black | black throughout (11.7 fps) |
| Cube | ULUS10223 | 121 | menu | menu | menu | title menu |
| Dante's Inferno | ULUS10469 | 50 | black | black | menu | title |
| Darkstalkers Chronicle - The Chaos Tower | ULUS10005 | 190 | menu | menu | menu | no save data prompt |
| Daxter | UCUS98618 | 84 | menu | movie | movie | title menu at 1200, cutscene after Cross |
| Dead Head Fred | ULUS10288 | 165 | loading | movie | menu | main menu (publisher logos before) |
| Dead or Alive - Paradise | ULUS10521 | 683 | black | black | black | black throughout |
| Dead to Rights - Reckoning | ULUS10023 | 188 | menu | menu | menu | profile prompt |
| Death Jr. | ULUS10027 | 39 | black | black | menu | title, Press START (black after its 3D intro before) |
| Death Jr. II - Root of Evil | ULUS10157 | 17 | black | black | menu | main menu (black after the legal screen before) |
| Def Jam - Fight for NY - The Takeover | ULUS10100 | 725 | black | black | black | black throughout |
| Dirt 2 (En,Fr,Es) | ULUS10471 | 251 | menu | menu | menu | "create save file" prompt |
| Disaster Report 3 [English] | ULJS00191 | 105 | black | black | menu | main menu (the title dim at 3600 before) |
| Disgaea - Afternoon of Darkness | ULUS10308 | 302 | menu | menu | menu | title menu |
| Disgaea 2 - Dark Hero Days | ULUS10461 | 389 | menu | loading | menu | title menu |
| Disgaea Infinite | ULUS10522 | 254 | movie | menu | menu | title menu |
| Disney-Pixar Cars | ULUS10073 | 286 | menu | menu | menu | main menu |
| Disney-Pixar Cars - Race-O-Rama | ULUS10428 | 107 | black | black | gameplay | racing attract demo (black after its logos before: its movie never ended) |
| Disney-Pixar Cars 2 | UCUS98766 | 304 | menu | menu | menu | memory stick notice |
| Dissidia 012 - Duodecim Final Fantasy | ULUS10566 | 220 | black | black | menu | "Create New Data" |
| Dissidia Final Fantasy | ULUS10437 | 253 | menu | menu | menu | nickname entry; scesupPreAcc 86debd66 missing |
| Dragon Ball Z - Tenkaichi Tag Team | ULUS10537 | 800 | black | black | black | autosave notice at 300 and 1200, then black after Cross: it opens a PGD-encrypted file (disc0:/sce_lbn0xe98b_size0x4A0), gives its key and, refused, tries again for good (part 55: sceKernelExtendThreadStack is here; scePower a85880d0 missing) |
| Dragonball Z Shin Budokai | ULUS10081 | 156 | menu | menu | menu | title, press start |
| Dragonball Z Shin Budokai - Another Road | ULUS10234 | 98 | movie | movie | movie | intro movie |
| Driver 76 | ULUS10235 | 142 | black | menu | menu | memory stick notice |
| Fat Princess Fistful of Cake | UCUS98740 | 30 | black | menu | menu | autosave prompt |
| Fate Extra CCC [English v1.0] | NPJH50505 | 94 | black | black | menu | title menu |
| Fate Extra Perfect Patch | ULUS10576 | 191 | black | black | menu | title, press start |
| Final Fantasy - 20th Anniversary Edition | ULUS10251 | 454 | movie | menu | menu | title |
| Final Fantasy II - 20th Anniversary Edition | ULUS10263 | 367 | black | menu | menu | title, press start |
| Final Fantasy III | NPUH10125 | 324 | movie | movie | movie | intro movie |
| Final Fantasy IV | ULUS10560 | 428 | movie | movie | movie | white fade after the title movie |
| Final Fantasy Tactics - War of the Lions Tweak [v2.52] | ULUS10297 | 224 | menu | menu | menu | title |
| Final Fantasy Type 0 | NPJH50443 | 254 | menu | menu | menu | autosave notice; scePower a85880d0 missing |
| Full Auto 2 - Battlelines | ULUS10220 | 215 | menu | menu | menu | profile menu |
| Fuuun Shinsengumi Bakumatsu den Portable [japan] | ULJM05561 | 436 | menu | menu | movie | memory stick check, then the intro |
| Genso Suikoden Tsumugareshi Hyakunen no Toki | NPJH50535 | 666 | black | black | movie | anime intro with its subtitles (sceMt19937Init missing; sceCcc here since part 55) |
| Ghost in the Shell - Stand Alone Complex | ULUS10020 | 498 | menu | menu | menu | title menu |
| Ghostbusters - The Video Game | ULUS10486 | 413 | black | black | movie | intro |
| Gitaroo Man Lives! | ULUS10207 | 516 | black | movie | movie | intro |
| God Eater 2 [English v2.0 RedArtz] | NPJH50832 | 442 | black | black | menu | the fan translation's notice that its DLC file (ms0:/PSP/GAME/NPJH50832/SYSTEM_UPDATE.EDAT, the patch's, not on the stick) is missing (part 55: thread-local storage pools and sceKernelTryLockLwMutex_600 were missing before) |
| God of War - Chains of Olympus | UCUS98653 | timeout | timeout | timeout | timeout | main menu by frame 1200; too slow to reach 3600 in 1200 s |
| God of War - Ghost of Sparta | UCUS98737 | timeout | timeout | timeout | timeout | past frame 60 only in 1200 s |
| Gradius Collection | ULUS10103 | 432 | loading | menu | menu | title, press start |
| Gran Turismo | UCUS98632 | 217 | menu | menu | menu | main menu |
| Grand Knights History | ULJS00394 | 686 | menu | menu | menu | "No saves detected" prompt |
| Grand Theft Auto - Chinatown Wars | ULUS10490 | 158 | menu | menu | menu | notice dialog |
| Grand Theft Auto - Liberty City Stories | ULUS10041 | 117 | black | movie | movie | intro |
| Grand Theft Auto - Sindacco Chronicles [v2.0 English] | ULUS01826 | 49 | menu | gameplay | gameplay | in the city |
| Grand Theft Auto - Vice City Stories | ULUS10160 | 111 | black | movie | movie | opening cutscene |
| Growlanser IV - Wayfarer of Time [UNDUB v1.3] [HQ OPED] | ULUS10593 | 198 | black | black | menu | title menu |
| Guilty Gear Judgment | ULUS10104 | 353 | menu | movie | movie | attract demo after the menu |
| Guilty Gear XX Accent Core Plus | ULUS10409 | 445 | black | movie | movie | intro |
| Gungnir | ULUS10592 | 163 | black | menu | menu | title menu |
| Gurumin - A Monstrous Adventure | ULUS10228 | 328 | menu | menu | menu | main menu |
| Hammerin Hero | ULUS10392 | 76 | menu | menu | menu | title menu |
| Harvest Moon - Hero of Leaf Valley | ULUS10458 | 127 | menu | gameplay | gameplay | in the village |
| Hexyz Force [UNDUB v1.2b] | ULUS10506 | 457 | black | black | menu | story select |
| Hot Shots Golf - Open Tee | UCUS98614 | 407 | menu | menu | menu | name entry |
| Hot Shots Golf - Open Tee 2 | UCUS98693 | 477 | menu | menu | menu | "game data corrupted, overwrite?" prompt |
| Hot Shots Tennis - Get a Grip | UCUS98701 | 553 | menu | menu | menu | username prompt |
| Hot Wheels - Ultimate Racing | ULUS10239 | 174 | menu | menu | menu | title |
| IL-2 Sturmovik - Birds of Prey | ULUS10476 | 459 | menu | movie | movie | intro |
| Innocent Life - A Futuristic Harvest Moon | ULUS10219 | 601 | menu | menu | menu | title, press start |
| Jak and Daxter - The Lost Frontier | UCUS98634 | 39 | black | black | black | black throughout (29 fps) |
| Jeanne d'Arc | UCUS98700 | 172 | menu | menu | menu | title menu |
| Juiced - Eliminator | ULUS10090 | 254 | loading | black | movie | loading screen, THQ movie, then the intro movie |
| Juiced 2 - Hot Import Nights | ULUS10312 | 123 | black | timeout | movie | intro and a 3D club scene (159 fps) |
| Kenka Bancho - Badass Rumble | ULUS10442 | 364 | black | movie | movie | intro scenes |
| Key Of Heaven | UCES00178 | 184 | menu | menu | menu | language select |
| Kidou Senshi Gundam Gundam vs. Gundam NEXT PLUS [English] | NPJH50107 | 238 | menu | menu | menu | pilot name notice |
| Killzone - Liberation | UCUS98646 | 3 | black | gameplay | gameplay | in the first level (4.8 fps) |
| King of Fighters, The - Orochi Saga | ULUS10360 | 391 | black | black | menu | "Loading...", the SNK Playmore logo and its intro, then its autosave notice (part 55: black and 9 fps before, spinning on sceCccDecodeUTF8, missing then) |
| Kingdom Hearts Birth by Sleep Final Mix [English] | ULJM05775 | 442 | menu | menu | menu | autosave notice; scePower a85880d0 missing |
| Kisou Ryouhei Gunhound EX | NPJH50723 | 589 | menu | menu | menu | title, press start |
| Kurohyou 2 [English v1.0] | NPJH50562 | 158 | black | black | menu | autosave notices |
| Kurohyou Ryu ga Gotoku Shinshou [English v1.2 TeamK4L] | NPJH50333 | 270 | menu | menu | menu | title menu; sceRtcGetLastReincarnatedTime missing |
| La Pucelle Ragnarok | ULJS00244 | 347 | loading | loading | movie | intro |
| Last Ranker | ULJM05676 | 99 | menu | menu | menu | title menu |
| LittleBigPlanet | UCUS98744 | 407 | menu | menu | menu | important-information notice; sceRtcGetTime64_t missing |
| LocoRoco | UCUS98662 | 235 | menu | menu | menu | title |
| LocoRoco 2 | UCUS98731 | 215 | menu | menu | menu | title |
| Lumines - Puzzle Fusion | ULUS10002 | 183 | menu | gameplay | gameplay | demo play |
| Lumines II | ULUS10183 | 394 | menu | gameplay | gameplay | demo play |
| Lunar - Silver Star Harmony | ULUS10482 | 312 | menu | menu | menu | autosave notice |
| MACH | ULES00565 | 245 | menu | black | black | logos, black at 3600 after Cross (the same without presses as before) |
| Macross - Ace Frontier [Japan] | ULJS00158 | 579 | menu | menu | menu | title |
| Macross - Triangle Frontier [Japan] | ULJS00321 | 604 | black | black | menu | autosave notice, then a soundtrack folder prompt (black before: utility module 0x108 refused) |
| Macross - Ultimate Frontier [Japan] | NPJH50050 | 683 | menu | menu | menu | soundtrack folder prompt |
| Manhunt 2 [Uncensored] | ULUS10280 | 205 | movie | gameplay | gameplay | in the asylum |
| Me & My Katamari | ULUS10094 | 364 | menu | menu | menu | save select |
| MediEvil Resurrection | UCES00006 | 138 | menu | movie | movie | intro scene; sceKernelStopUnloadSelfModule missing |
| Mega Man Maverick Hunter X [UNDUB v1.1] | ULUS10068 | 136 | menu | black | movie | intro movie, to the anime cutscene |
| Mega Man Powered Up [UNDUB v1.3] | ULUS10091 | 396 | menu | gameplay | gameplay | demo at 1200, loading at 3600 |
| Melodie (Prototype) | PETR00010 | 199 | black | black | black | black throughout: it asks for one 40 MiB block of user memory (sceKernelAllocPartitionMemory), more than a 32 MB PSP gives, and gets nothing (a prototype expecting a 64 MB PSP's memory) |
| Mercury Meltdown | ULUS10133 | 292 | menu | menu | menu | title |
| Metal Gear Acid | ULUS10006 | 529 | movie | movie | movie | static picture |
| Metal Gear Acid 2 | ULUS10077 | 789 | movie | movie | movie | intro art |
| Metal Gear Solid - Digital Graphic Novel | ULUS10108 | 260 | black | movie | movie | intro |
| Metal Gear Solid - Peace Walker [v2.00] | ULUS10509 | 201 | movie | movie | movie | opening quote |
| Metal Gear Solid - Portable Ops | ULUS10202 | 759 | movie | loading | loading | loading icon |
| Metal Gear Solid - Portable Ops Plus | ULUS10290 | 465 | movie | loading | loading | loading icon |
| Metal Slug Anthology | ULUS10154 | 209 | menu | menu | menu | game menu |
| Metal Slug XX | ULUS10495 | 366 | menu | menu | menu | main menu |
| Miami Vice - The Game | ULUS10109 | 77 | black | black | menu | title, Press START (black before: its ring's 12th word over its movie file) |
| Micro Machines V4 | ULUS10129 | 309 | menu | movie | movie | autosave warning, then the publisher logo |
| Midnight Club - L.A. Remix | ULUS10383 | 46 | black | movie | menu | title (its attract scene at 3600 before) |
| Midnight Club 3 - DUB Edition [v2.02] | ULUS10021 | 72 | movie | menu | menu | title, press start |
| ModNation Racers | UCUS98741 | 184 | menu | menu | menu | title, press any button |
| Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] | ULJM05800 | 593 | black | black | black | Capcom logo, then sceJpegCsc, sceMpegAvcConvertToYuv420 and scePauth 98b83b5d missing |
| Monster Hunter Portable 3rd HD ver [English v6.1.0 Team Maverick One] | NPJB40001 | 1977 | black | black | black | black throughout: its program (26.5 MiB from 0x08804000) doesn't fit the 24 MiB user partition, so it never starts (part 55: given 64 MiB it shows a Dolby logo and stops at sceJpegCsc, as the PSP version does) |
| Monster Kingdom Jewel Summoner | ULUS10211 | 53 | menu | movie | movie | intro |
| Moto GP | ULUS10153 | 325 | menu | menu | menu | new save prompt |
| MotorStorm - Arctic Edge | UCUS98743 | 185 | black | black | movie | its intro, a 3D scene (black after the notice before) |
| MX vs. ATV - On the Edge | ULUS10071 | 1249 | loading | loading | loading | LOADING throughout |
| MX vs. ATV Reflex | ULUS10429 | 236 | loading | black | movie | publisher logos (a blank white screen at 3600 before) |
| MX vs. ATV Untamed | ULUS10330 | 178 | black | black | menu | title, PRESS START (black after its logo before: its movie never ended) |
| Naruto Shippuden Ultimate Ninja Impact | ULUS10582 | 1226 | black | black | black | black throughout: its data is PGD-encrypted, refused and tried again for good (part 55's trace) |
| Need for Speed - Carbon - Own the City | ULUS10114 | 10 | menu | menu | menu | notice over the title (12 fps) |
| Need for Speed - Most Wanted - 5-1-0 | ULUS10036 | 1235 | black | black | black | black throughout: its main thread waits for good in sceGeDrawSync, the GE having stopped a list at a RET with no CALL (part 55's trace) |
| Need for Speed - ProStreet | ULUS10331 | 514 | menu | hang | hang | notice, then the CPU stopped in user_main |
| Need for Speed - Shift | ULUS10462 | 45 | menu | menu | menu | title, press start |
| Need for Speed - Underground Rivals | ULUS10007 | 14 | menu | black | gameplay | autosave prompt, then a night race after Cross (black at 3600 before) |
| OutRun 2006 - Coast 2 Coast | ULUS10064 | 103 | menu | menu | menu | memory stick notice; sceUsbStart missing |
| PAC-MAN CE | NPUZ00125 | 127 | menu | menu | menu | autosave notice |
| Pac-Man World Rally | ULUS10149 | 335 | menu | menu | menu | no save file warning |
| Pangya Fantasy Golf [Black Screen Fix] | ULUS10438 | 157 | black | menu | menu | title menu |
| PaRappa the Rapper | UCUS98702 | 57 | menu | black | black | black throughout (also at the earlier report's commit) |
| Parodius Portable | ULJM05220 | 332 | menu | menu | menu | title, press start |
| Patapon | UCUS98711 | 819 | menu | menu | menu | title menu |
| Patapon 2 | UCUS98732 | 1130 | menu | menu | menu | system data prompts (every thread waiting at boot before: the stick callback and a semaphore) |
| Patapon 3 | UCUS98751 | 1023 | menu | menu | menu | autosave notice; sceImposeSetUMDPopup missing |
| Persona 2 Eternal Punishment | NPJH50581 | 83 | black | black | movie | legal notice, then a 3D scene |
| Persona 2 Innocent Sin | ULUS10584 | 73 | movie | movie | movie | intro |
| Phantom Kingdom | NPJH50451 | 575 | menu | menu | menu | title menu |
| PixelJunk Monsters - Deluxe | UCUS98739 | 473 | menu | menu | menu | title, press start |
| Platypus | ULUS10203 | 205 | menu | menu | menu | title, press start |
| Power Stone Collection | ULUS10171 | 515 | menu | menu | menu | system data prompt; sceDisplayIsForeground missing |
| Pursuit Force | UCUS98640 | 249 | menu | black | movie | memory stick notice, then an action scene |
| Pursuit Force - Extreme Justice | UCUS98703 | 615 | black | black | menu | title, press start (black after its logos before: its movie never ended) |
| Ratchet & Clank - Size Matters | UCUS98633 | 347 | menu | menu | menu | title, press start |
| Resistance - Retribution | UCUS98668 | 239 | menu | menu | menu | title, press start |
| Retro City Rampage DX (Europe) | NPEH00170 | 521 | menu | menu | menu | title, press start |
| Ridge Racer | ULUS10001 | 429 | black | black | black | the Pac-Man loading game until frame 3000, then its opening movie (two pictures, at 3100) and black frames after it, drawn every frame on both renderers; cause not found |
| Ridge Racer 2 | UCES00422 | 439 | movie | movie | movie | attract movie |
| Riviera - The Promised Land | ULUS10286 | 206 | menu | movie | movie | intro |
| Samurai Dou Portable [English] | ULJS00155 | 546 | movie | movie | movie | intro |
| Samurai Shodown Anthology | ULUS10401 | 444 | menu | menu | menu | title |
| Seen in Liberty City | ULUS11826 | 61 | menu | gameplay | gameplay | in the city |
| Sega Genesis Collection | ULUS10192 | 205 | loading | menu | movie | its intro collage, past the settings data prompt (where it stayed before) |
| Sega Rally Revo | ULUS10311 | 129 | black | black | menu | autosave notice, then the title (an assert at its 4800-packet ring before) |
| Sheperds Crossing | ULUS10499 | 86 | black | menu | menu | title, press start |
| Shin Megami Tensei - Persona 3 Portable | ULUS10512 | 755 | movie | loading | menu | logo, intro and title, PRESS ANY BUTTON (2100 to 3000), then its attract loop starts over: a loading screen at 3600 (judged again in part 55; scesupPreAcc 86debd66 missing, the game goes on) |
| Shining Blade [gugule] | NPJH50530 | 526 | black | black | black | black throughout: its CRI file system opens PSP_GAME/INSDIR/DATA.BIN, PGD-encrypted, gives its key and, refused, tries again for good (part 55's trace) |
| Shining Hearts [English MT v1.2] | NPJH50342 | 302 | menu | menu | menu | system data prompt |
| Shining Hearts [English] | NPJH50342 | 314 | menu | menu | menu | system data prompt |
| Shinobido - Tales of the Ninja [Europe] [Undub 2021-06-28] | UCES00421 | 269 | menu | menu | menu | title |
| Silent Hill - Shattered Memories | ULUS10450 | 185 | loading | black | menu | START GAME menu (title card and logos before) |
| Silent Hill Origins | ULUS10285 | 225 | menu | black | menu | main menu (black after its logos before) |
| Smash Court Tennis 3 | ULUS10269 | 411 | loading | loading | menu | "No game data" prompt |
| Snoopy vs. the Red Baron | ULUS10189 | 232 | menu | menu | menu | no save file warning |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo | UCUS98615 | 352 | menu | menu | menu | autosave notice |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 2 | UCUS98645 | 291 | menu | menu | menu | no save prompt |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 3 | UCUS98716 | 562 | black | menu | menu | autosave notice |
| SOCOM - U.S. Navy SEALs - Tactical Strike | UCUS98649 | 73 | menu | menu | menu | title |
| Sol Trigger | NPJH50619 | 101 | black | menu | menu | title menu; sceP3da 374500a5 missing |
| Sonic Rivals | ULUS10195 | 210 | menu | menu | menu | title, press start (autosave notice and logos before) |
| Sonic Rivals 2 | ULUS10323 | 226 | menu | menu | menu | title, press start |
| Soulcalibur - Broken Destiny | ULUS10457 | 419 | menu | movie | movie | intro; sceHprm c7154136 missing |
| Space Invaders Extreme | ULUS10346 | 614 | menu | menu | menu | name entry |
| Spectral Souls | ULUS10076 | 369 | menu | menu | movie | its intro, the main menu after (black before: its movies drawn as gradients) |
| Split-Second | ULUS10513 | 414 | black | movie | movie | intro |
| SSX on Tour | ULUS10042 | 363 | menu | menu | menu | title, press start |
| Star Ocean - First Departure | ULUS10374 | 117 | movie | menu | menu | title menu |
| Star Ocean - Second Evolution | ULUS10375 | 118 | movie | menu | menu | title menu |
| Star Soldier | ULJM05026 | 351 | menu | menu | menu | title, press start |
| Star Trek - Tactical Assault | ULUS10150 | 377 | menu | menu | menu | main menu |
| Star Wars - Battlefront - Elite Squadron | ULUS10390 | 111 | movie | movie | movie | intro |
| Star Wars - Battlefront - Renegade Squadron | ULUS10292 | 92 | menu | menu | menu | "Load canceled" |
| Star Wars - Battlefront II - Remastered Edition [Hack v8] | ULUS10053 | 390 | movie | gameplay | gameplay | on a map |
| Star Wars - The Force Unleashed | ULUS10345 | 80 | menu | movie | movie | intro |
| Street Fighter 3 - 3rd Strike [Port] | UCJS10041 | 390 | menu | menu | menu | character select |
| Street Fighter Alpha 3 Max | ULUS10062 | 164 | menu | menu | menu | auto-load prompt |
| Street Supremacy | ULES00239 | 389 | menu | black | loading | map loading screen (black after Cross before) |
| Super Moneky Ball Adventures | ULES00364 | 8 | menu | menu | menu | title, press start (12 fps) |
| Super Stardust Portable | NPUG80221 | 66 | black | menu | menu | main menu |
| Syphon Filter - Dark Mirror | UCUS98641 | 100 | menu | menu | menu | profile name entry |
| Syphon Filter - Logan's Shadow | UCUS98606 | 108 | menu | menu | menu | profile name entry |
| Tactics Ogre - Let Us Cling Together [One Vision v1.11a] | ULUS10565 | 82 | menu | menu | menu | title menu |
| Tales of Eternia | ULES00176 | 415 | movie | movie | movie | anime intro |
| Tales of Phantasia Full Voice Edition [English v1.2][QoL] | ULJS00079 | 488 | black | black | movie | anime intro |
| Tales of Phantasia X [English v1.2] | ULJS00293 | 468 | black | black | movie | anime intro |
| Tales Of The World - Radiant Mythology 2 [English 31-08] | ULJS00175 | 114 | movie | movie | movie | intro |
| Tekken - Dark Resurrection | ULUS10139 | 666 | black | black | black | autosave notice, then black |
| Tekken 6 | ULUS10466 | 299 | black | black | movie | intro |
| Tenchu - Shadow Assassins | ULUS10419 | 385 | movie | movie | movie | intro |
| Tenchu - Time of the Assassins [UNDUB v1.5c] | ULES00277 | 515 | movie | movie | movie | intro |
| The 3rd Birthday | ULUS10567 | 210 | menu | menu | menu | no system data prompt |
| The Legend of Heroes - Trails From Azure [English v15] | NPJH50473 | 476 | black | black | menu | title, press start |
| The Legend of Heroes I | ULUS10022 | 134 | movie | movie | movie | intro |
| The Legend of Heroes II | ULUS10125 | 232 | movie | movie | movie | intro |
| The Legend of Heroes III | ULUS10144 | 209 | black | black | movie | intro |
| The Legend of Nayuta - Boundless Trails [Addendum v1.08] | NPJH50625 | 266 | timeout | menu | menu | title, press start |
| The Sims 2 | ULUS10031 | 73 | menu | gameplay | gameplay | create-a-sim |
| The Sims 2 - Castaway | ULUS10296 | 34 | menu | menu | menu | title, press start |
| The Sims 2 - Pets | ULUS10130 | 39 | black | menu | menu | title, press start |
| Toca Race Driver 2 | ULES00042 | 300 | black | menu | menu | profile select |
| Tokobot | ULUS10061 | 488 | movie | menu | menu | title menu |
| Tony Hawk's Underground 2 Remix | ULUS10014 | 453 | menu | menu | menu | title |
| Twisted Metal Head On | UCUS98601 | 70 | menu | menu | menu | title, press X |
| Ultimate Ghosts 'n Goblins | ULUS10105 | 335 | menu | movie | movie | story text, then a stage scene |
| Umineko no Nakukoro ni Portable [English v1.0] | ULJM05968 | 205 | menu | black | menu | title menu |
| Valhalla Knights | ULUS10230 | 682 | menu | black | black | black throughout (idle) |
| Valhalla Knights 2 | ULUS10366 | 416 | menu | menu | movie | intro |
| Valkyria Chronicles II | ULUS10515 | 278 | loading | menu | menu | autosave notices |
| Valkyria Chronicles III [English v1.0.8] | ULJM05957 | 546 | black | black | black | black throughout: PSP_GAME/INSDIR/DATA.BIN is PGD-encrypted, refused and tried again for good, as Shining Blade's (part 55's trace) |
| Valkyrie Profile Lennth | ULUS10107 | 78 | loading | menu | menu | new game difficulty select |
| Virtua Tennis - World Tour | ULUS10037 | 193 | menu | menu | menu | title at 1200, white at 3600 after Cross |
| Virtua Tennis 3 | ULUS10246 | 341 | menu | menu | menu | title |
| Warriors of the Lost Empire | ULES00924 | 682 | menu | gameplay | gameplay | attract demo |
| WipEout [Portable Collection v3.0] | WPCE02025 | 499 | menu | menu | menu | main menu |
| Ys - The Ark of Napishtim | ULUS10051 | 592 | menu | menu | menu | title, press start |
| Ys - The Oath in Felghana | ULUS10558 | 203 | timeout | menu | menu | difficulty select |
| Ys I and II Chronicles | ULUS10547 | 231 | menu | menu | menu | create system data prompt |
| Ys Seven | ULUS10551 | 336 | black | hang | menu | title menu |
| ZHP - Unlosing Ranger Vs Darkdeath Evilman | ULUS10559 | 425 | menu | menu | movie | intro story |
