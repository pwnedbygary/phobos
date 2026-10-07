# PSP Compatibility Report

**Date:** 2026-10-07
**Tested commit:** `b27f084a5` (branch `cursor/psp-ge-features-2b67`)
**Games tested:** 266
**Runner:** `tools/psp-runner/` (headless, 3600 frames, 20 s of input, 4 PNG captures, 1 WAV)

## Method

Each game was run for 3600 frames (60 s of PSP time) with the following
settings:

- `--frames 3600`
- `--press "120:Start,123:Start!,1800:Cross,1803:Cross!"`
- `--png-at 60,300,1200,3600`
- `--wav dir/sound.wav`
- `--ge-threads 2`
- `--fonts <fonts>`

The runner captures a PNG at frames 60, 300, 1200, and 3600 (each named by
the frame run, after waiting for the screen to present a new picture), and a
WAV of the full run. A game is in "menu" if its final frame shows a usable
UI (title screen, main menu, language select, save warning, or profile
dialog). A game is in "movie" if its final frame shows a cinematic,
intro, or cutscene (dark frame with visual content). A game is in
"loading" if its final frame shows a loading screen. A game is "black" if
its final frame is a true-black screen (crash, GE failure, or freeze;
max brightness ≤ 8). Four games timed out at the 1200 s batch limit and
are marked separately.

The reference frame for each game is the last frame the runner captured:
`frame-003600.png` where the screen presented by the last frame (172
games), else `frame-001200.png` (88 games), else `frame-000060.png` (2
timed-out games that were so slow only 60 frames completed in 1200 s).

## Summary

| Category | Count |
|---|---|
| Boots to menu/title (usable UI) | 141 |
| Movie (intro/cutscene playing) | 35 |
| Black (crash, GE failure, or freeze) | 73 |
| Stuck loading | 13 |
| Timed out (killed at 1200 s) | 4 |
| **Total** | **266** |

The 141 games that boot to a usable UI include title screens, main menus,
language selects, save-data warnings, and profile dialogs. These games
loaded their code and reached their first interactive screen. The 35 movie
games are playing their intro or a cutscene (the runner is built with
FFmpeg's decoders, and #153 is included, so movies do draw). The 73 black
games show a true-black screen: a crash in an unimplemented kernel function,
a feature of the PSP's own graphics engine (GE) not yet emulated (curved
surfaces, which #162 adds), or the CPU stopping in a game thread. The 13 loading
games are stuck on a loading screen.

## Since de6e6dcb7

Three PRs sit between the old run and this one: #153 (music and movie
playback through FFmpeg's decoders), #156 (41 kernel functions, which took
the missing-function count from 149 to 77), and #157 (GE line and box
drawing, DXT texture decompression; curved surfaces are not yet emulated,
and anti-aliased lines are drawn without anti-aliasing). Together they
shift the compatibility picture substantially:

| Category | Before | After | Change |
|---|---|---|---|
| Boots to menu | 104 | 141 | +37 |
| Movie | — | 35 | new |
| Black | 155 | 73 | −82 |
| Stuck loading | 4 | 13 | +9 |
| Timed out | 3 | 4 | +1 |

Re-judging every game's reference frame (frame 3600) with one rule—a
cinematic or text frame is a movie, a true-black frame is black, a bright
usable UI is a menu, a loading screen is loading—66 games improved
(black → menu): they now boot to a usable UI where the old code froze.
22 went from menu to black. Why isn't checked yet; anti-aliased lines
can't be the cause, since #157 draws them (without anti-aliasing). They
are: Black Wolves Saga - Last Hope, Blitz - Overtime, Castlevania - The
Dracula X Chronicles, Crisis Core - Final Fantasy VII, Disney-Pixar Cars -
Race-O-Rama, Fat Princess Fistful of Cake, Fate Extra CCC, Gitaroo Man
Lives!, Guilty Gear XX Accent Core Plus, Gungnir, Hexyz Force, Juiced 2 -
Hot Import Nights, Killzone - Liberation, The King of Fighters - Orochi
Saga, Macross - Triangle Frontier, Melodie (Prototype), Miami Vice - The
Game, Midnight Club - L.A. Remix, Pangya Fantasy Golf, Ridge Racer, Super
Stardust Portable and Toca Race Driver 2. Several last notes point at
causes worth checking first: a save or message prompt waiting for an
answer, a failed read, and scePsmfPlayer (which #160 adds). The 35 movie
games are dark intro frames the old rule lumped
into black, so the Before column's 155 "black" overstates the old run's
true-black total.

The top missing functions also changed. Before, the gaps were dominated by
`ThreadManForUser` thread-management functions (34 hits) and `sceCtrl`
(32 hits). Now the dominant gaps are video-playback functions
(`scePsmf`/`scePsmfPlayer`, 79 hits across 8 IDs) and DRM
(`scePspNpDrm_user`, 13 hits). The GE functions that were top gaps before
(`sceGe_user b77905ea`, 17 hits) no longer appear in the top 15.

## Speed distribution

| Range | Count |
|---|---|
| < 30 fps (very slow) | 8 |
| 30–60 fps | 19 |
| 60–200 fps | 63 |
| > 200 fps | 172 |

These fps are the runner's own: unthrottled, built at `-O1` in `BUILD_DEBUG`
mode, with 2 GE threads. They are not the app's speed. Most games run well
above 60 fps (172 of 266 run above 200 fps). The 8 very slow games
(< 30 fps) are typically large 3D titles that stress the GE.

## Top missing functions

The following kernel functions are hit most often across all 266 games.
77 of 266 games (29 %) hit at least one unimplemented function.

| Count | Function |
|---|---|
| 20 | `scePsmf c22c8327` |
| 20 | `scePsmfPlayer 235d8787` |
| 13 | `scePspNpDrm_user a1336091` |
| 12 | `sceRtc 011f03c1` |
| 12 | `scePsmfPlayer 9b71a274` |
| 8 | `sceDisplay 77ed8b3a` |
| 7 | `scePsmfPlayer e792cd94` |
| 6 | `ThreadManForUser d13bde95` |
| 6 | `scePsmfPlayer 95a84ee5` |
| 5 | `scePsmf 5b70fcc1` |
| 5 | `sceHprm 7e69eda4` |
| 5 | `scePsmfPlayer f8ef08a6` |
| 4 | `sceDisplay 40f1469c` |
| 4 | `scePower a85880d0` |
| 4 | `scePsmfPlayer 3d6d25a9` |

The `scePsmf`/`scePsmfPlayer` entries are video-playback functions (movies
and cutscenes). `scePspNpDrm_user` is a DRM function. `sceRtc` is a
real-time clock function. `sceDisplay` entries are display-management
functions. `ThreadManForUser` entries are thread-management functions.
`sceHprm` is a high-performance mode function. `scePower` is a
power-management function.

## Per-game results

Each row: title, NPID (region-disc), speed (fps), how far it gets, and the
last note from the runner. "menu" = boots to a usable UI; "movie" =
cinematic/intro/cutscene playing; "black" = true-black screen (crash, GE
failure, or freeze); "loading" = stuck on a loading screen; "timeout" =
killed at 1200 s.

Note: the NPID is read from each game's PARAM.SFO `DISC_ID` field. The
filename stem is the reliable per-game identifier; the NPID is the disc's
own ID.

| Game | NPID | fps | State | Last note |
|---|---|---|---|---|
| 007 - From Russia with Love | ULUS10080 | 150.91 | menu | DISC0:/PSP_GAME/USR.. |
| 50 Cent - Bulletproof - G-Unit Edition | ULUS10128 | 397.14 | menu | FIX1!!! |
| 7th Dragon 2020 [English v0.91] | NPJH50459 | 3737.93 | black |  |
| 7th Dragon 2020-II [English Patched v0.91] | NPJH50716 | 3737.82 | black | file sema = 0x109 |
| Ace Combat - Joint Assault | ULUS10511 | 5344.14 | black |  |
| Ace Combat X - Skies of Deception | ULUS10176 | 553.02 | loading | not implemented yet.. |
| Aces of War [Europe] | ULES00590 | 120.83 | movie | sound delay = 0 |
| Activision Hits Remixed | ULUS10186 | 5.15 | menu | camera active |
| After Burner - Black Falcon | ULUS10244 | 380.89 | movie | Loading SCE Modules |
| Angus Hates Aliens | NPUZ00374 | 466.66 | menu |  |
| Ape Escape Academy | UCUS98619 | 227.24 | menu | disc0:/PSP_GAME/USR.. |
| Ape Escape - On the Loose | UCUS98609 | 173.00 | menu | disc0:/PSP_GAME/USR.. |
| Archer Maclean's Mercury | ULUS10017 | 389.01 | menu | Memory available at.. |
| Armored Core 3 Portable [True Analogs Mod v1.02] | NPUH10023 | 63.59 | movie |  |
| Armored Core - Formula Front International [True Analogs v1.0] | ULJS19001 | 1697.90 | loading | the CPU stopped in .. |
| Armored Core Last Raven Portable [True Analogs Mod v1.03] | NPUH10024 | 64.78 | movie |  |
| Armored Core Silent Line Portable [True Analogs v1.02] | NPUH10025 | 2087.89 | movie | the CPU stopped in .. |
| Army of Two - The 40th Day | ULUS10472 | 29.64 | movie |  |
| ATV Offroad Fury - Blazin' Trails | UCUS98603 | 304.38 | menu |  |
| ATV Offroad Fury Pro | UCUS98648 | 5747.52 | black | the CPU stopped in .. |
| Battle vs. Chess [Europe Proto] | ULES01517 | 120.15 | menu | message dialog (ans.. |
| Black Wolves Saga - Last Hope | ULJM06220 | 1256.69 | black |  |
| BlazBlue - Calamity Trigger | ULUS10519 | 342.41 | menu | disc0:/PSP_GAME/USR.. |
| BlazBlue - Continuum Shift II | ULUS10579 | 359.00 | menu | disc0:/PSP_GAME/USR.. |
| Blitz - Overtime | ULUS10200 | 30.23 | black | Allocation not alig.. |
| Brandish - The Dark Revenant | NPUH10195 | 40.55 | black | not implemented yet.. |
| Brave Story New Traveler | ULUS10279 | 72.10 | movie | === SGX system init.. |
| Brothers in Arms - D-Day | ULUS10193 | 1304.56 | menu |  |
| Bubble Bobble Evolution | ULUS10143 | 222.59 | movie | Return to game? |
| Burnout Dominator | ULUS10236 | 146.89 | menu | disc0:/sce_lbn65e0_.. |
| Burnout Legends | ULUS10025 | 185.19 | menu | disc0:/PSP_GAME/USR.. |
| Castlevania - The Dracula X Chronicles | ULUS10277 | 3262.57 | black |  |
| Chili Con Carnage | ULUS10216 | 268.39 | menu | not implemented yet.. |
| Cladun - This Is An RPG | NPUH10072 | 354.04 | menu |  |
| Colin McRae Rally 2005 Plus [Europe] | ULES00111 | 494.51 | menu | HeapCallFail(): Err.. |
| Corpse Party - Sweet Sachikos Hysteric Birthday Bash [English 04-29-2026] | ULJM06114 | 1068.19 | menu | Utility Savedata fa.. |
| Crash Tag Team Racing | ULUS10044 | 1903.75 | black | the CPU stopped in .. |
| Crazy Taxi - Fare Wars | ULUS10273 | 474.03 | menu | PSPCI: File cache w.. |
| Crimson Gem Saga | ULUS10400 | 133.59 | menu |  |
| Crisis Core - Final Fantasy VII | ULUS10336 | 6130.21 | black |  |
| Crush | ULUS10238 | 16.72 | black | disc0:/PSP_GAME/USR.. |
| Cube | ULUS10223 | 365.09 | menu | not implemented yet.. |
| Dante's Inferno | ULUS10469 | 75.73 | black | not implemented yet.. |
| Darkstalkers Chronicle - The Chaos Tower | ULUS10005 | 232.80 | menu | disc0:/PSP_GAME/USR.. |
| Daxter | UCUS98618 | 77.88 | menu | disc0:/PSP_GAME/USR.. |
| Dead Head Fred | ULUS10288 | 769.93 | loading |  |
| Dead or Alive - Paradise | ULUS10521 | 1096.10 | black | not implemented yet.. |
| Dead to Rights - Reckoning | ULUS10023 | 208.80 | menu | Finished loading at.. |
| Death Jr. | ULUS10027 | 5786.00 | black | the CPU stopped in .. |
| Death Jr. II - Root of Evil | ULUS10157 | 30.75 | black | disc0:/PSP_GAME/USR.. |
| Def Jam - Fight for NY - The Takeover | ULUS10100 | 3437.46 | black | not implemented yet.. |
| Dirt 2 (En,Fr,Es) | ULUS10471 | 181.66 | menu |  |
| Disaster Report 3 [English] | ULJS00191 | 6140.44 | black | no threads left to .. |
| Disgaea 2 - Dark Hero Days | ULUS10461 | 745.47 | menu | not implemented yet.. |
| Disgaea - Afternoon of Darkness | ULUS10308 | 228.74 | menu |  |
| Disgaea Infinite | ULUS10522 | 155.31 | movie | message dialog (ans.. |
| Disney-Pixar Cars | ULUS10073 | 614.48 | menu |  |
| Disney-Pixar Cars 2 | UCUS98766 | 4.86 | menu | not implemented yet.. |
| Disney-Pixar Cars - Race-O-Rama | ULUS10428 | 223.18 | black | message dialog (ans.. |
| Dissidia 012 - Duodecim Final Fantasy | ULUS10566 | 6154.53 | black | no threads left to .. |
| Dissidia Final Fantasy | ULUS10437 | 301.55 | menu | Please do not remov.. |
| Dragonball Z Shin Budokai | ULUS10081 | 242.30 | menu | PSPCI: Total 5 file.. |
| Dragonball Z Shin Budokai - Another Road | ULUS10234 | 265.11 | movie | PSPCI: File cache w.. |
| Dragon Ball Z - Tenkaichi Tag Team | ULUS10537 | 6164.55 | black | no threads left to .. |
| Driver 76 | ULUS10235 | 212.29 | black | not implemented yet.. |
| Fate Extra CCC [English v1.0] | NPJH50505 | 120.39 | black |  |
| Fate Extra Perfect Patch | ULUS10576 | 2433.78 | black | the CPU stopped in .. |
| Fat Princess Fistful of Cake | UCUS98740 | 6116.17 | black |  |
| Final Fantasy - 20th Anniversary Edition | ULUS10251 | 436.00 | movie |  |
| Final Fantasy II - 20th Anniversary Edition | ULUS10263 | 1652.54 | black | the CPU stopped in .. |
| Final Fantasy III | NPUH10125 | 246.93 | movie | not implemented yet.. |
| Final Fantasy IV | ULUS10560 | 572.49 | movie | not implemented yet.. |
| Final Fantasy Tactics - War of the Lions Tweak [v2.52] | ULUS10297 | 250.39 | menu |  |
| Final Fantasy Type 0 | NPJH50443 | 216.50 | menu | not implemented yet.. |
| Full Auto 2 - Battlelines | ULUS10220 | 134.51 | menu |  |
| Fuuun Shinsengumi Bakumatsu den Portable [japan] | ULJM05561 | 85.20 | menu | disc0:/PSP_GAME/USR.. |
| Genso Suikoden Tsumugareshi Hyakunen no Toki | NPJH50535 | 1979.69 | black | the CPU stopped in .. |
| Ghostbusters - The Video Game | ULUS10486 | 6085.54 | black | no threads left to .. |
| Ghost in the Shell - Stand Alone Complex | ULUS10020 | 255.88 | menu | disc0:/PSP_GAME/USR.. |
| Gitaroo Man Lives! | ULUS10207 | 72.73 | black | not implemented yet.. |
| God Eater 2 [English v2.0 RedArtz] | NPJH50832 | 6123.46 | black | the CPU stopped in .. |
| God of War - Chains of Olympus | UCUS98653 |  | timeout |  |
| God of War - Ghost of Sparta | UCUS98737 |  | timeout |  |
| Gradius Collection | ULUS10103 | 2715.66 | loading | disc0:/PSP_GAME/USR.. |
| Grand Knights History | ULJS00394 | 327.22 | menu |  |
| Grand Theft Auto - Chinatown Wars | ULUS10490 | 100.89 | menu | not implemented yet.. |
| Grand Theft Auto - Liberty City Stories | ULUS10041 | 1221.79 | black | disc0:/sce_lbn0x640.. |
| Grand Theft Auto - Sindacco Chronicles [v2.0 English] | ULUS01826 | 54.54 | menu | disc0:/sce_lbn0xc62.. |
| Grand Theft Auto - Vice City Stories | ULUS10160 | 1163.38 | black | disc0:/sce_lbn0x667.. |
| Gran Turismo | UCUS98632 | 61.47 | menu | disc0:/PSP_GAME/USR.. |
| Growlanser IV - Wayfarer of Time [UNDUB v1.3] [HQ OPED] | ULUS10593 | 1435.25 | black | the CPU stopped in .. |
| Guilty Gear Judgment | ULUS10104 | 210.27 | menu | disc0:/PSP_GAME/USR.. |
| Guilty Gear XX Accent Core Plus | ULUS10409 | 970.47 | black | Would you like to s.. |
| Gungnir | ULUS10592 | 103.06 | black | not implemented yet.. |
| Gurumin - A Monstrous Adventure | ULUS10228 | 115.78 | menu |  |
| Hammerin Hero | ULUS10392 | 74.18 | menu | not implemented yet.. |
| Harvest Moon - Hero of Leaf Valley | ULUS10458 | 94.07 | menu | psaFileInit()>psaFi.. |
| Hexyz Force [UNDUB v1.2b] | ULUS10506 | 798.86 | black |  |
| Hot Shots Golf - Open Tee | UCUS98614 | 256.35 | menu | === SGX system init.. |
| Hot Shots Golf - Open Tee 2 | UCUS98693 | 293.11 | menu | === ... SGX system .. |
| Hot Shots Tennis - Get a Grip | UCUS98701 | 415.78 | menu | not implemented yet.. |
| Hot Wheels - Ultimate Racing | ULUS10239 | 210.81 | menu | disc0:/PSP_GAME/USR.. |
| IL-2 Sturmovik - Birds of Prey | ULUS10476 | 306.48 | menu |  |
| Innocent Life - A Futuristic Harvest Moon | ULUS10219 | 575.66 | menu | program end |
| Jak and Daxter - The Lost Frontier | UCUS98634 | 70.26 | black |  |
| Jeanne d'Arc | UCUS98700 | 91.68 | menu | GE: curved surfaces.. |
| Juiced 2 - Hot Import Nights | ULUS10312 | 4.17 | black | tgIoFillCache waiti.. |
| Juiced - Eliminator | ULUS10090 | 477.68 | loading | not implemented yet.. |
| Kenka Bancho - Badass Rumble | ULUS10442 | 1342.67 | black | not implemented yet.. |
| Key Of Heaven | UCES00178 | 249.47 | menu | === SGX system init.. |
| Kidou Senshi Gundam Gundam vs. Gundam NEXT PLUS [English] | NPJH50107 | 173.61 | menu | [PSP](SYS)===== Cap.. |
| Killzone - Liberation | UCUS98646 | 1486.84 | black |  |
| Kingdom Hearts Birth by Sleep Final Mix [English] | ULJM05775 | 329.47 | menu | not implemented yet.. |
| King of Fighters, The - Orochi Saga | ULUS10360 | 102.65 | black |  |
| Kisou Ryouhei Gunhound EX | NPJH50723 | 515.44 | menu |  |
| Kurohyou 2 [English v1.0] | NPJH50562 | 6162.54 | black | no threads left to .. |
| Kurohyou Ryu ga Gotoku Shinshou [English v1.2 TeamK4L] | NPJH50333 | 140.68 | menu | W090910s03:sceIoOpe.. |
| La Pucelle Ragnarok | ULJS00244 | 2134.07 | loading | Heap: 3454.28 |
| Last Ranker | ULJM05676 | 60.10 | menu | disc0:/PSP_GAME/USR.. |
| LittleBigPlanet | UCUS98744 | 258.21 | menu | not implemented yet.. |
| LocoRoco | UCUS98662 | 454.81 | menu | disc0:/PSP_GAME/USR.. |
| LocoRoco 2 | UCUS98731 | 286.79 | menu | disc0:/PSP_GAME/USR.. |
| Lumines II | ULUS10183 | 967.07 | menu | not implemented yet.. |
| Lumines - Puzzle Fusion | ULUS10002 | 106.80 | menu | ----Lumines Task St.. |
| Lunar - Silver Star Harmony | ULUS10482 | 242.88 | menu | disc0:/PSP_GAME/USR.. |
| MACH | ULES00565 | 237.87 | menu | -------------------.. |
| Macross - Ace Frontier [Japan] | ULJS00158 | 857.20 | menu |  |
| Macross - Triangle Frontier [Japan] | ULJS00321 | 6127.79 | black |  |
| Macross - Ultimate Frontier [Japan] | NPJH50050 | 491.54 | menu |  |
| Manhunt 2 [Uncensored] | ULUS10280 | 134.24 | movie | message dialog (ans.. |
| MediEvil Resurrection | UCES00006 | 283.33 | menu | System free: 369408 |
| Mega Man Maverick Hunter X [UNDUB v1.1] | ULUS10068 | 159.25 | menu | not implemented yet.. |
| Mega Man Powered Up [UNDUB v1.3] | ULUS10091 | 115.77 | menu | not implemented yet.. |
| Melodie (Prototype) | PETR00010 | 74.01 | black |  |
| Me & My Katamari | ULUS10094 | 84.24 | menu | disc0:/PSP_GAME/USR.. |
| Mercury Meltdown | ULUS10133 | 185.57 | menu |  |
| Metal Gear Acid | ULUS10006 | 172.34 | movie | disc0:/PSP_GAME/USR.. |
| Metal Gear Acid 2 | ULUS10077 | 363.01 | movie | disc0:/PSP_GAME/USR.. |
| Metal Gear Solid - Digital Graphic Novel | ULUS10108 | 1082.92 | black | not implemented yet.. |
| Metal Gear Solid - Peace Walker [v2.00] | ULUS10509 | 64.22 | movie | disc0:/PSP_GAME/USR.. |
| Metal Gear Solid - Portable Ops | ULUS10202 | 444.94 | movie | not implemented yet.. |
| Metal Gear Solid - Portable Ops Plus | ULUS10290 | 441.71 | movie | disc0:/PSP_GAME/USR.. |
| Metal Slug Anthology | ULUS10154 | 214.29 | menu | libc:_getmodreent: .. |
| Metal Slug XX | ULUS10495 | 163.44 | menu |  |
| Miami Vice - The Game | ULUS10109 | 82.32 | black | ** READ FAILED. RET.. |
| Micro Machines V4 | ULUS10129 | 614.60 | menu | disc0:/PSP_GAME/USR.. |
| Midnight Club 3 - DUB Edition [v2.02] | ULUS10021 | 969.98 | movie | disc0:/PSP_GAME/USR.. |
| Midnight Club - L.A. Remix | ULUS10383 | 260.11 | black | message dialog (ans.. |
| ModNation Racers | UCUS98741 | 174.95 | menu | Videoplayer: invali.. |
| Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] | ULJM05800 | 853.34 | black | not implemented yet.. |
| Monster Hunter Portable 3rd HD ver [English v6.1.0 Team Maverick One] | NPJB40001 | 6162.68 | black | no threads left to .. |
| Monster Kingdom Jewel Summoner | ULUS10211 | 53.85 | menu |  |
| Moto GP | ULUS10153 | 250.65 | menu | message dialog (ans.. |
| MotorStorm - Arctic Edge | UCUS98743 | 3202.36 | black |  |
| MX vs. ATV - On the Edge | ULUS10071 | 46.39 | loading | disc0:/PSP_GAME/USR.. |
| MX vs. ATV Reflex | ULUS10429 | 312.64 | loading | not implemented yet.. |
| MX vs. ATV Untamed | ULUS10330 | 319.04 | black | not implemented yet.. |
| Naruto Shippuden Ultimate Ninja Impact | ULUS10582 | 2830.13 | black | disc0:/sce_lbn0xE64.. |
| Need for Speed - Carbon - Own the City | ULUS10114 | 13.69 | menu | message dialog (ans.. |
| Need for Speed - Most Wanted - 5-1-0 | ULUS10036 | 2094.35 | black | GE: a display list .. |
| Need for Speed - ProStreet | ULUS10331 | 296.70 | menu | the CPU stopped in .. |
| Need for Speed - Shift | ULUS10462 | 82.28 | menu | Initing MP3 decoder |
| Need for Speed - Underground Rivals | ULUS10007 | 7.61 | menu | pathname= Global/Ga.. |
| OutRun 2006 - Coast 2 Coast | ULUS10064 | 426.31 | menu | not implemented yet.. |
| PAC-MAN CE | NPUZ00125 | 86.61 | menu |  |
| Pac-Man World Rally | ULUS10149 | 352.73 | menu | disc0:/PSP_GAME/USR.. |
| Pangya Fantasy Golf [Black Screen Fix] | ULUS10438 | 276.55 | black | ERROR: scePsmfPlaye.. |
| PaRappa the Rapper | UCUS98702 | 217.96 | menu |  |
| Parodius Portable | ULJM05220 | 705.23 | menu | memory stick error! |
| Patapon | UCUS98711 | 702.16 | menu | === ... SGX system .. |
| Patapon 2 | UCUS98732 | 808.55 | menu | not implemented yet.. |
| Patapon 3 | UCUS98751 | 1070.92 | menu | not implemented yet.. |
| Persona 2 Eternal Punishment | NPJH50581 | 6139.75 | black | the CPU stopped in .. |
| Persona 2 Innocent Sin | ULUS10584 | 47.66 | movie |  |
| Phantom Kingdom | NPJH50451 | 376.65 | menu | not implemented yet.. |
| PixelJunk Monsters - Deluxe | UCUS98739 | 503.07 | menu |  |
| Platypus | ULUS10203 | 214.50 | menu | message dialog (ans.. |
| Power Stone Collection | ULUS10171 | 226.46 | menu | disc0:/PSP_GAME/USR.. |
| Pursuit Force | UCUS98640 | 56.80 | menu | disc0:/PSP_GAME/USR.. |
| Pursuit Force - Extreme Justice | UCUS98703 | 1909.31 | black |  |
| Ratchet & Clank - Size Matters | UCUS98633 | 431.19 | menu |  |
| Resistance - Retribution | UCUS98668 | 54.93 | menu |  |
| Retro City Rampage DX (Europe) | NPEH00170 | 432.04 | menu |  |
| Ridge Racer | ULUS10001 | 210.43 | black | disc0:/PSP_GAME/USR.. |
| Ridge Racer 2 | UCES00422 | 225.66 | movie |  |
| Riviera - The Promised Land | ULUS10286 | 262.34 | menu |  |
| Samurai Dou Portable [English] | ULJS00155 | 899.39 | movie | disc0:/PSP_GAME/USR.. |
| Samurai Shodown Anthology | ULUS10401 | 200.34 | menu | message dialog (ans.. |
| Seen in Liberty City | ULUS11826 | 30.80 | menu | disc0:/sce_lbn0xe2c.. |
| Sega Genesis Collection | ULUS10192 | 3004.64 | loading |  |
| Sega Rally Revo | ULUS10311 | 2950.00 | black | the CPU stopped in .. |
| Sheperds Crossing | ULUS10499 | 757.21 | black | not implemented yet.. |
| Shining Blade [gugule] | NPJH50530 | 984.96 | black | disc0:/PSP_GAME/USR.. |
| Shining Hearts [English] | NPJH50342 | 314.19 | menu | Launch on 'disc0' |
| Shining Hearts [English MT v1.2] | NPJH50342 | 315.19 | menu | Launch on 'disc0' |
| Shin Megami Tensei - Persona 3 Portable | ULUS10512 | 387.60 | movie | GE: anti-aliased li.. |
| Shinobido - Tales of the Ninja [Europe] [Undub 2021-06-28] | UCES00421 | 346.07 | menu | disc0:/PSP_GAME/USR.. |
| Silent Hill Origins | ULUS10285 | 319.25 | menu | disc0:/PSP_GAME/USR.. |
| Silent Hill - Shattered Memories | ULUS10450 | 6142.89 | loading |  |
| Smash Court Tennis 3 | ULUS10269 | 148.23 | loading | message dialog (ans.. |
| Snoopy vs. the Red Baron | ULUS10189 | 143.25 | menu | disc0:/PSP_GAME/USR.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo | UCUS98615 | 216.22 | menu | snd_stream (non-que.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 2 | UCUS98645 | 132.91 | menu | Compiled against Sc.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 3 | UCUS98716 | 987.61 | black | the CPU stopped in .. |
| SOCOM - U.S. Navy SEALs - Tactical Strike | UCUS98649 | 330.96 | menu | not implemented yet.. |
| Sol Trigger | NPJH50619 | 1120.71 | black | not implemented yet.. |
| Sonic Rivals | ULUS10195 | 281.42 | menu |  |
| Sonic Rivals 2 | ULUS10323 | 74.78 | menu |  |
| Soulcalibur - Broken Destiny | ULUS10457 | 611.61 | menu | not implemented yet.. |
| Space Invaders Extreme | ULUS10346 | 343.32 | menu | during this time. |
| Spectral Souls | ULUS10076 | 195.26 | menu | effectnum 154 |
| Split-Second | ULUS10513 | 464.86 | black | not implemented yet.. |
| SSX on Tour | ULUS10042 | 317.99 | menu | disc0:/sce_lbn0x6BE.. |
| Star Ocean - First Departure | ULUS10374 | 63.09 | movie |  |
| Star Ocean - Second Evolution | ULUS10375 | 62.69 | movie |  |
| Star Soldier | ULJM05026 | 180.96 | menu | disc0:/PSP_GAME/USR.. |
| Star Trek - Tactical Assault | ULUS10150 | 244.67 | menu | not implemented yet.. |
| Star Wars - Battlefront - Elite Squadron | ULUS10390 | 38.30 | movie | DISC0:/PSP_GAME/USR.. |
| Star Wars - Battlefront II - Remastered Edition [Hack v8] | ULUS10053 | 52.54 | movie | disc0:/PSP_GAME/USR.. |
| Star Wars - Battlefront - Renegade Squadron | ULUS10292 | 46.73 | menu | loading took 5453 m.. |
| Star Wars - The Force Unleashed | ULUS10345 | 36.51 | menu | not implemented yet.. |
| Street Fighter 3 - 3rd Strike [Port] | UCJS10041 | 363.44 | menu |  |
| Street Fighter Alpha 3 Max | ULUS10062 | 204.35 | menu | disc0:/PSP_GAME/USR.. |
| Street Supremacy | ULES00239 | 432.04 | menu | disc0:/PSP_GAME/USR.. |
| Super Moneky Ball Adventures | ULES00364 | 6.81 | menu | Game Info: Game has.. |
| Super Stardust Portable | NPUG80221 | 68.75 | black | a display list ENDe.. |
| Syphon Filter - Dark Mirror | UCUS98641 | 120.70 | menu |  |
| Syphon Filter - Logan's Shadow | UCUS98606 | 41.09 | menu |  |
| Tactics Ogre - Let Us Cling Together [One Vision v1.11a] | ULUS10565 | 42.24 | menu | not implemented yet.. |
| Tales of Eternia | ULES00176 | 374.47 | movie | disc0:/PSP_GAME/USR.. |
| Tales of Phantasia Full Voice Edition [English v1.2][QoL] | ULJS00079 | 3558.48 | black | the CPU stopped in .. |
| Tales of Phantasia X [English v1.2] | ULJS00293 | 5855.83 | black | the CPU stopped in .. |
| Tales Of The World - Radiant Mythology 2 [English 31-08] | ULJS00175 | 147.73 | movie | disc0:/PSP_GAME/USR.. |
| Tekken 6 | ULUS10466 | 6105.90 | black | no threads left to .. |
| Tekken - Dark Resurrection | ULUS10139 | 690.27 | black | disc0:/PSP_GAME/USR.. |
| Tenchu - Shadow Assassins | ULUS10419 | 507.34 | movie |  |
| Tenchu - Time of the Assassins [UNDUB v1.5c] | ULES00277 | 526.24 | movie | disc0:/PSP_GAME/USR.. |
| The 3rd Birthday | ULUS10567 | 259.88 | menu | not implemented yet.. |
| The Legend of Heroes I | ULUS10022 | 121.22 | movie | Hetima System( Movi.. |
| The Legend of Heroes II | ULUS10125 | 194.17 | movie | Hetima System( Movi.. |
| The Legend of Heroes III | ULUS10144 | 818.42 | black |  |
| The Legend of Heroes - Trails From Azure [English v15] | NPJH50473 | 2646.11 | black | every thread is wai.. |
| The Legend of Nayuta - Boundless Trails [Addendum v1.08] | NPJH50625 |  | timeout |  |
| The Sims 2 | ULUS10031 | 110.36 | menu | disc0:/sce_lbn0x5fb.. |
| The Sims 2 - Castaway | ULUS10296 | 30.44 | menu |  |
| The Sims 2 - Pets | ULUS10130 | 32.65 | black |  |
| Toca Race Driver 2 | ULES00042 | 793.01 | black | disc0:/PSP_GAME/USR.. |
| Tokobot | ULUS10061 | 311.82 | movie | disc0:/PSP_GAME/USR.. |
| Tony Hawk's Underground 2 Remix | ULUS10014 | 961.59 | menu | disc0:/PSP_GAME/USR.. |
| Twisted Metal Head On | UCUS98601 | 34.22 | menu | disc0:/sce_lbn0xE2c.. |
| Ultimate Ghosts 'n Goblins | ULUS10105 | 1645.74 | menu | disc0:/PSP_GAME/USR.. |
| Umineko no Nakukoro ni Portable [English v1.0] | ULJM05968 | 2271.33 | menu |  |
| Valhalla Knights | ULUS10230 | 3118.56 | menu |  |
| Valhalla Knights 2 | ULUS10366 | 6018.77 | menu |  |
| Valkyria Chronicles II | ULUS10515 | 140.56 | loading | disc0:/PSP_GAME/USR.. |
| Valkyria Chronicles III [English v1.0.8] | ULJM05957 | 979.29 | black | disc0:/PSP_GAME/USR.. |
| Valkyrie Profile Lennth | ULUS10107 | 96.43 | loading | disc0:/PSP_GAME/USR.. |
| Virtua Tennis 3 | ULUS10246 | 115.81 | menu | not implemented yet.. |
| Virtua Tennis - World Tour | ULUS10037 | 100.60 | menu | GE: anti-aliased li.. |
| Warriors of the Lost Empire | ULES00924 | 380.31 | menu | disc0:/PSP_GAME/USR.. |
| WipEout [Portable Collection v3.0] | WPCE02025 | 228.95 | menu | not implemented yet.. |
| Ys I and II Chronicles | ULUS10547 | 242.42 | menu |  |
| Ys Seven | ULUS10551 | 2597.46 | black | every thread is wai.. |
| Ys - The Ark of Napishtim | ULUS10051 | 593.36 | menu | disc0:/PSP_GAME/USR.. |
| Ys - The Oath in Felghana | ULUS10558 |  | timeout |  |
| ZHP - Unlosing Ranger Vs Darkdeath Evilman | ULUS10559 | 169.51 | menu |  |

The runner's full per-game output (summary, errors, frames, WAV) is
described in `tools/psp-runner/README.md`. Contact sheets of the reference
frame for each game (ten per sheet, 26 sheets) are in
`/tmp/phobos-psp-compat-157-report/sheet-*.png`.

## Timed-out games

Four games ran for the full 1200 s batch limit without completing 3600
frames:

| Game | NPID | Frames completed |
|---|---|---|
| Ys - The Oath in Felghana | ULUS10558 | 60 |
| God of War - Ghost of Sparta | UCUS98737 | 60 |
| God of War - Chains of Olympus | UCUS98653 | 1200 |
| The Legend of Nayuta - Boundless Trails [Addendum v1.08] | NPJH50625 | 1800 |

These are large 3D titles that run well below 30 fps. The first two
completed only 60 frames in 1200 s; the third completed 1200 frames; the
fourth completed 1800 frames.

## Next missing functions

The most impactful gaps to close, in order of how many games they affect:

1. **`scePsmf`/`scePsmfPlayer` video-playback functions** — hit by 40 games
   across 8 distinct function IDs. These control movie and cutscene
   playback; closing them would un-black the movie-driven titles.
2. **`scePspNpDrm_user` DRM function** — hit by 13 games. A disc-
   protection function.
3. **`sceRtc` real-time clock function** — hit by 12 games.
4. **`sceDisplay` display-management functions** — hit by 12 games across
   2 IDs.
5. **`ThreadManForUser` thread-management functions** — hit by 12 games
   across 2 IDs (down from 34 in the previous run).
6. **`sceHprm` high-performance mode function** — hit by 5 games.
7. **`scePower` power-management function** — hit by 4 games.
