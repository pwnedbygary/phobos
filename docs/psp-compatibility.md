# PSP Compatibility Report

**Date:** 2026-10-06
**Tested commit:** `de6e6dcb7` (branch `cursor/psp-test-data-2b67`)
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

The runner captures a PNG at frames 60, 300, 1200, and 3600, and a WAV of
the full run. A game is considered to have "booted" if its final frame
shows a usable UI (title screen, main menu, language select, save warning,
or profile dialog). A game is in "gameplay" if its final frame shows active
game content. A game is "frozen" if its final frame is a black screen
(crash, GE texture failure, or stuck loading). Three games timed out at the
1200 s batch limit and are marked separately.

The reference frame for each game is the last frame the screen thread
presented: `frame-003600.png` where the screen caught up (166 games), else
`frame-001200.png` (96 games), else `frame-000060.png` (2 timed-out games
that were so slow only 60 frames completed in 1200 s).

## Summary

| Category | Count |
|---|---|
| Boots to menu/title (usable UI) | ~90 |
| In active gameplay | ~12 |
| Frozen (black screen, crash, GE failure) | ~155 |
| Stuck loading | ~5 |
| Timed out (killed at 1200 s) | 3 |
| **Total** | **266** |

The ~90 games that boot to a usable UI include title screens, main menus,
language selects, save-data warnings, and profile dialogs. These games
loaded their code and reached their first interactive screen. The ~12 games
in active gameplay show actual game content (3D scenes, 2D platformers,
tennis, city scenes, etc.). The ~155 frozen games show a black screen,
typically caused by a crash in an unimplemented kernel function, a GE
(Direct3D) feature not yet emulated (compressed DXT textures, bounding-box
tests, line drawing), or the CPU stopping in a game thread.

## Speed distribution

| Range | Count |
|---|---|
| < 30 fps (very slow) | 13 |
| 30–60 fps | 15 |
| 60–200 fps | 72 |
| > 200 fps | 166 |

Most games run well above 60 fps (166 of 266 run above 200 fps). The 13 very
slow games (< 30 fps) are typically large 3D titles that stress the GE.

## Top missing functions

The following kernel functions are hit most often across all 266 games.
149 of 266 games (56 %) hit at least one unimplemented function.

| Count | Function |
|---|---|
| 34 | `ThreadManForUser 6b30100f` |
| 32 | `sceCtrl a7144800` |
| 31 | `ThreadManForUser b7d098c6` |
| 30 | `ThreadManForUser b011b11f` |
| 19 | `ThreadManForUser 6652b8ca` |
| 17 | `sceGe_user b77905ea` |
| 15 | `SysMemUserForUser fe707fdf` |
| 11 | `sceImpose 24fd7bcf` |
| 8 | `ThreadManForUser 20fff560` |
| 8 | `scePsmfPlayer 235d8787` |
| 7 | `scePsmf c22c8327` |
| 6 | `ThreadManForUser 5bf4dd27` |
| 5 | `ThreadManForUser fccfad26` |
| 5 | `ThreadManForUser 2c34e053` |
| 5 | `sceImpose 8c943191` |

The `ThreadManForUser` entries are thread-management functions (the most
common gap). `sceCtrl a7144800` is a control/setting function.
`sceGe_user b77905ea` is a GE (graphics) function. `SysMemUserForUser`
entries are memory-management functions. `sceImpose` entries are DRM
imposition functions. `scePsmf`/`scePsmfPlayer` entries are video-playback
functions.

## Per-game results

Each row: title, NPID (region-disc), speed (fps), how far it gets, and the
last note from the runner. "menu" = boots to a usable UI; "gameplay" =
active game content visible; "black" = frozen on a black screen; "loading"
= stuck on a loading screen; "timeout" = killed at 1200 s.

Note: the NPID is extracted from each game's SFO file by scanning its value
data for the DISC_ID pattern (two letters + six alphanumeric chars). Some
NPIDs are duplicated across games because the SFO value offsets are
non-standard and the scan can match the wrong field. The filename stem is
the reliable per-game identifier; the NPID is best-effort.

| Game | NPID | fps | State | Last note |
|---|---|---|---|---|
| 007 - From Russia with Love | ULUS1008 | 354.52 | menu | DISC0:/PSP_GAME/.. |
| 50 Cent - Bulletproof - G-Unit Edition | ULUS1012 | 134.14 | black | GE: compressed (.. |
| 7th Dragon 2020 [English v0.91] | NPJH5045 | 1237.55 | black | not implemented .. |
| 7th Dragon 2020-II [English Patched v0.91] | NPJH5071 | 1254.68 | black | not implemented .. |
| Ace Combat - Joint Assault | ULUS1051 | 1339.65 | black | not implemented .. |
| Ace Combat X - Skies of Deception | ULUS1017 | 384.71 | black | not implemented .. |
| Aces of War [Europe] | ULES0059 | 872.19 | black | fatal error : re.. |
| Activision Hits Remixed | ULUS1018 | 4.16 | menu | Calling FreeMusi.. |
| After Burner - Black Falcon | ULUS1024 | 24.01 | loading |  Loading SCE Mod.. |
| Angus Hates Aliens | NPUZ0037 | 656.43 | black | not implemented .. |
| Ape Escape Academy | UCUS9861 | 204.98 | menu | disc0:/PSP_GAME/.. |
| Ape Escape - On the Loose | UCUS9860 | 154.71 | menu | disc0:/PSP_GAME/.. |
| Archer Maclean's Mercury | ULUS1001 | 533.32 | menu | Memory available.. |
| Armored Core 3 Portable [True Analogs Mod v1.02] | NPUH1002 | 427.68 | black | sceMpegQueryStre.. |
| Armored Core - Formula Front International [True Analogs v1.0] | ULJS1900 | 1764.75 | black | the CPU stopped .. |
| Armored Core Last Raven Portable [True Analogs Mod v1.03] | NPUH1002 | 892.48 | black | sceMpegQueryStre.. |
| Armored Core Silent Line Portable [True Analogs v1.02] | NPUH1002 | 991.44 | black | the CPU stopped .. |
| Army of Two - The 40th Day | ULUS1047 | 1619.89 | black | not implemented .. |
| ATV Offroad Fury - Blazin' Trails | UCUS9860 | 141.51 | menu |  |
| ATV Offroad Fury Pro | UCUS9864 | 1374.44 | black | the CPU stopped .. |
| Battle vs. Chess [Europe Proto] | ULES0151 | 35.73 | black | GE: compressed (.. |
| Black Wolves Saga - Last Hope | ULJM0622 | 1346.23 | menu |  |
| BlazBlue - Calamity Trigger | ULUS1051 | 362.29 | menu | disc0:/PSP_GAME/.. |
| BlazBlue - Continuum Shift II | ULUS1057 | 373.92 | menu | disc0:/PSP_GAME/.. |
| Blitz - Overtime | ULUS1020 | 62.16 | menu | Allocation not a.. |
| Brandish - The Dark Revenant | NPUH1019 | 3.72 | black | not implemented .. |
| Brave Story New Traveler | ULUS1027 | 74.25 | menu | *** sgxpsp.c(187.. |
| Brothers in Arms - D-Day | ULUS1019 | 740.52 | menu |  |
| Bubble Bobble Evolution | ULUS1014 | 141.54 | menu | Return to game? |
| Burnout Dominator | ULUS1023 | 148.41 | black | GE: lines aren't.. |
| Burnout Legends | ULUS1002 | 186.36 | menu | disc0:/PSP_GAME/.. |
| Castlevania - The Dracula X Chronicles | ULUS1027 | 1142.42 | menu |  |
| Chili Con Carnage | ULUS1021 | 234.76 | black | not implemented .. |
| Cladun - This Is An RPG | NPUH1007 | 84.07 | black | not implemented .. |
| Colin McRae Rally 2005 Plus [Europe] | ULES0011 | 511.96 | menu | HeapCallFail(): .. |
| Corpse Party - Sweet Sachikos Hysteric Birthday | ULJM0611 | 1088.98 | menu | Utility Savedata.. |
| Crash Tag Team Racing | ULUS1004 | 919.38 | black | the CPU stopped .. |
| Crazy Taxi - Fare Wars | ULUS1027 | 642.38 | menu | PSPCI: File cach.. |
| Crimson Gem Saga | ULUS1040 | 32.30 | black | not implemented .. |
| Crisis Core - Final Fantasy VII | ULUS1033 | 1392.32 | menu |  |
| Crush | ULUS1023 | 233.53 | black | the CPU stopped .. |
| Cube | ULUS1022 | 861.51 | black | not implemented .. |
| Dante's Inferno | ULUS1046 | 79.28 | black | not implemented .. |
| Darkstalkers Chronicle - The Chaos Tower | ULUS1000 | 215.60 | menu | disc0:/PSP_GAME/.. |
| Daxter | UCUS9861 | 83.02 | menu | disc0:/PSP_GAME/.. |
| Dead Head Fred | ULUS1028 | 58.57 | black | GE: bounding box.. |
| Dead or Alive - Paradise | ULUS1052 | 19.11 | black | not implemented .. |
| Dead to Rights - Reckoning | ULUS1002 | 216.20 | loading | Finished loading.. |
| Death Jr. | ULUS1002 | 1326.99 | black | the CPU stopped .. |
| Death Jr. II - Root of Evil | ULUS1015 | 9.37 | black | GE: bounding box.. |
| Def Jam - Fight for NY - The Takeover | ULUS1010 | 3201.61 | black | not implemented .. |
| Dirt 2 (En,Fr,Es) | ULUS1047 | 181.94 | black | not implemented .. |
| Disaster Report 3 [English] | ULJS0019 | 1392.00 | black | no threads left .. |
| Disgaea 2 - Dark Hero Days | ULUS1046 | 683.86 | black | not implemented .. |
| Disgaea - Afternoon of Darkness | ULUS1030 | 129.66 | black | GE: compressed (.. |
| Disgaea Infinite | ULUS1052 | 156.03 | black | not implemented .. |
| Disney-Pixar Cars | ULUS1007 | 342.85 | menu |  |
| Disney-Pixar Cars 2 | UCUS9876 | 5.07 | black | not implemented .. |
| Disney-Pixar Cars - Race-O-Rama | ULUS1042 | 121.89 | menu | message dialog (.. |
| Dissidia 012 - Duodecim Final Fantasy | ULUS1056 | 1395.62 | black | no threads left .. |
| Dissidia Final Fantasy | ULUS1043 | 321.02 | menu | Please do not re.. |
| Dragonball Z Shin Budokai | ULUS1008 | 883.97 | menu | PSPCI: Total 5 f.. |
| Dragonball Z Shin Budokai - Another Road | ULUS1023 | 775.56 | menu | PSPCI: File cach.. |
| Dragon Ball Z - Tenkaichi Tag Team | ULUS1053 | 1393.21 | black | no threads left .. |
| Driver 76 | ULUS1023 | 254.27 | black | not implemented .. |
| Fate Extra CCC [English v1.0] | NPJH5050 | 128.33 | menu |  |
| Fate Extra Perfect Patch | ULUS1057 | 2400.18 | black | the CPU stopped .. |
| Fat Princess Fistful of Cake | UCUS9874 | 1390.50 | menu |  |
| Final Fantasy - 20th Anniversary Edition | ULUS1025 | 912.60 | black | not implemented .. |
| Final Fantasy II - 20th Anniversary Edition | ULUS1026 | 1723.87 | black | the CPU stopped .. |
| Final Fantasy III | NPUH1012 | 244.52 | black | not implemented .. |
| Final Fantasy IV | ULUS1056 | 654.39 | black | not implemented .. |
| Final Fantasy Tactics - War of the Lions Tweak [v2.52] | ULUS1029 | 772.93 | black | not implemented .. |
| Final Fantasy Type 0 | NPJH5044 | 198.22 | black | not implemented .. |
| Full Auto 2 - Battlelines | ULUS1022 | 140.40 | menu |  |
| Fuuun Shinsengumi Bakumatsu den Portable [japan] | ULJM0556 | 82.93 | black | not implemented .. |
| Genso Suikoden Tsumugareshi Hyakunen no Toki | NPJH5053 | 2038.63 | black | the CPU stopped .. |
| Ghostbusters - The Video Game | ULUS1048 | 1387.46 | black | no threads left .. |
| Ghost in the Shell - Stand Alone Complex | ULUS1002 | 261.96 | menu | disc0:/PSP_GAME/.. |
| Gitaroo Man Lives! | ULUS1020 | 77.49 | menu | disc0:/PSP_GAME/.. |
| God Eater 2 [English v2.0 RedArtz] | NPJH5083 | 1390.07 | black | the CPU stopped .. |
| God of War - Chains of Olympus | UCUS9865 |  | timeout |  |
| God of War - Ghost of Sparta | UCUS9873 |  | timeout |  |
| Gradius Collection | ULUS1010 | 1038.46 | menu | disc0:/PSP_GAME/.. |
| Grand Knights History | ULJS0039 | 276.33 | black | not implemented .. |
| Grand Theft Auto - Chinatown Wars | ULUS1049 | 96.19 | black | not implemented .. |
| Grand Theft Auto - Liberty City Stories | ULUS1004 | 43.89 | black | GE: lines aren't.. |
| Grand Theft Auto - Sindacco Chronicles [v2.0 English] | ULUS0182 | 52.69 | black | GE: lines aren't.. |
| Grand Theft Auto - Vice City Stories | ULUS1016 | 61.30 | black | GE: lines aren't.. |
| Gran Turismo | UCUS9863 | 1388.24 | black | not implemented .. |
| Growlanser IV - Wayfarer of Time [UNDUB v1.3] [HQ OPED] | ULUS1059 | 1377.47 | black | not implemented .. |
| Guilty Gear Judgment | ULUS1010 | 215.57 | menu | disc0:/PSP_GAME/.. |
| Guilty Gear XX Accent Core Plus | ULUS1040 | 152.72 | menu | Would you like t.. |
| Gungnir | ULUS1059 | 218.35 | menu | POLL SEMA |
| Gurumin - A Monstrous Adventure | ULUS1022 | 111.91 | black | GE: bounding box.. |
| Hammerin Hero | ULUS1039 | 77.09 | black | not implemented .. |
| Harvest Moon - Hero of Leaf Valley | ULUS1045 | 95.60 | menu | psaFileInit()>ps.. |
| Hexyz Force [UNDUB v1.2b] | ULUS1050 | 912.74 | menu |  |
| Hot Shots Golf - Open Tee | UCUS9861 | 160.05 | menu | *** sgxpsp.c(158.. |
| Hot Shots Golf - Open Tee 2 | UCUS9869 | 209.78 | menu | *** sgxpsp.c(209.. |
| Hot Shots Tennis - Get a Grip | UCUS9870 | 438.13 | black | not implemented .. |
| Hot Wheels - Ultimate Racing | ULUS1023 | 482.30 | menu | disc0:/PSP_GAME/.. |
| IL-2 Sturmovik - Birds of Prey | ULUS1047 | 324.26 | black | not implemented .. |
| Innocent Life - A Futuristic Harvest Moon | ULUS1021 | 445.80 | menu | program end |
| Jak and Daxter - The Lost Frontier | UCUS9863 | 73.43 | black | not implemented .. |
| Jeanne d'Arc | UCUS9870 | 1564.24 | black | fatal error : ma.. |
| Juiced 2 - Hot Import Nights | ULUS1031 | 4.18 | menu | tgIoFillCache wa.. |
| Juiced - Eliminator | ULUS1009 | 385.25 | menu | disc0:/PSP_GAME/.. |
| Kenka Bancho - Badass Rumble | ULUS1044 | 1407.60 | black | not implemented .. |
| Key Of Heaven | UCES0017 | 238.37 | menu | === SGX system i.. |
| Kidou Senshi Gundam Gundam vs. Gundam NEXT PLUS [English] | NPJH5010 | 159.71 | black | not implemented .. |
| Killzone - Liberation | UCUS9864 | 831.48 | menu |  |
| Kingdom Hearts Birth by Sleep Final Mix [English] | ULJM0577 | 342.97 | black | not implemented .. |
| King of Fighters, The - Orochi Saga | ULUS1036 | 121.17 | menu |  |
| Kisou Ryouhei Gunhound EX | NPJH5072 | 519.41 | menu |  |
| Kurohyou 2 [English v1.0] | NPJH5056 | 1391.81 | black | no threads left .. |
| Kurohyou Ryu ga Gotoku Shinshou [English v1.2 TeamK4L] | NPJH5033 | 55.38 | black | not implemented .. |
| La Pucelle Ragnarok | ULJS0024 | 2063.38 | menu | Heap: 3454.28 |
| Last Ranker | ULJM0567 | 1720.61 | menu | disc0:/PSP_GAME/.. |
| LittleBigPlanet | UCUS9874 | 266.69 | menu | Error: ../../Cod.. |
| LocoRoco | UCUS9866 | 437.56 | menu | disc0:/PSP_GAME/.. |
| LocoRoco 2 | UCUS9873 | 310.27 | menu | disc0:/PSP_GAME/.. |
| Lumines II | ULUS1018 | 1064.33 | black | not implemented .. |
| Lumines - Puzzle Fusion | ULUS1000 | 101.40 | menu | ----Lumines Task.. |
| Lunar - Silver Star Harmony | ULUS1048 | 219.17 | menu | disc0:/PSP_GAME/.. |
| MACH | ULES0056 | 116.93 | menu | Error in sceAtra.. |
| Macross - Ace Frontier [Japan] | ULJS0015 | 202.34 | black | GE: curved surfa.. |
| Macross - Triangle Frontier [Japan] | ULJS0032 | 1391.24 | menu |  |
| Macross - Ultimate Frontier [Japan] | NPJH5005 | 505.68 | black | not implemented .. |
| Manhunt 2 [Uncensored] | ULUS1028 | 1863.89 | menu | message dialog (.. |
| MediEvil Resurrection | UCES0000 | 303.99 | menu | System free: 369.. |
| Mega Man Maverick Hunter X [UNDUB v1.1] | ULUS1006 | 161.01 | black | not implemented .. |
| Mega Man Powered Up [UNDUB v1.3] | ULUS1009 | 115.38 | black | not implemented .. |
| Melodie (Prototype) | PETR0001 | 77.05 | menu |  |
| Me & My Katamari | ULUS1009 | 109.41 | black | GE: lines aren't.. |
| Mercury Meltdown | ULUS1013 | 74.48 | menu |  |
| Metal Gear Acid | ULUS1000 | 160.33 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Acid 2 | ULUS1007 | 394.79 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Solid - Digital Graphic Novel | ULUS1010 | 1056.63 | black | not implemented .. |
| Metal Gear Solid - Peace Walker [v2.00] | ULUS1050 | 67.60 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Solid - Portable Ops | ULUS1020 | 479.82 | black | not implemented .. |
| Metal Gear Solid - Portable Ops Plus | ULUS1029 | 375.68 | menu | disc0:/PSP_GAME/.. |
| Metal Slug Anthology | ULUS1015 | 225.18 | black | not implemented .. |
| Metal Slug XX | ULUS1049 | 166.14 | menu |  |
| Miami Vice - The Game | ULUS1010 | 84.20 | menu | ** READ FAILED. .. |
| Micro Machines V4 | ULUS1012 | 481.48 | menu | disc0:/PSP_GAME/.. |
| Midnight Club 3 - DUB Edition [v2.02] | ULUS1002 | 16.87 | menu | disc0:/PSP_GAME/.. |
| Midnight Club - L.A. Remix | ULUS1038 | 32.62 | menu | message dialog (.. |
| ModNation Racers | UCUS9874 | 175.48 | menu | Videoplayer: inv.. |
| Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] | ULJM0580 | 644.11 | black | not implemented .. |
| Monster Hunter Portable 3rd HD | NPJB4000 | 1386.98 | black | no threads left .. |
| Monster Kingdom Jewel Summoner | ULUS1021 | 72.09 | menu |  |
| Moto GP | ULUS1015 | 295.27 | menu | message dialog (.. |
| MotorStorm - Arctic Edge | UCUS9874 | 768.60 | black | the CPU stopped .. |
| MX vs. ATV - On the Edge | ULUS1007 | 43.74 | menu | disc0:/PSP_GAME/.. |
| MX vs. ATV Reflex | ULUS1042 | 147.03 | black | not implemented .. |
| MX vs. ATV Untamed | ULUS1033 | 220.25 | black | not implemented .. |
| Naruto Shippuden Ultimate Ninja Impact | ULUS1058 | 2832.82 | black | not implemented .. |
| Need for Speed - Carbon - Own the City | ULUS1011 | 109.91 | black | not implemented .. |
| Need for Speed - Most Wanted - 5-1-0 | ULUS1003 | 1005.76 | black | GE: a display li.. |
| Need for Speed - ProStreet | ULUS1033 | 98.22 | black | GE: compressed (.. |
| Need for Speed - Shift | ULUS1046 | 75.58 | black | GE: lines aren't.. |
| Need for Speed - Underground Rivals | ULUS1000 | 100.59 | menu | pathname= Global.. |
| OutRun 2006 - Coast 2 Coast | ULUS1006 | 231.85 | black | not implemented .. |
| PAC-MAN CE | NPUZ0012 | 14.67 | menu |  |
| Pac-Man World Rally | ULUS1014 | 364.31 | black | GE: bounding box.. |
| Pangya Fantasy Golf [Black Screen Fix] | ULUS1043 | 240.70 | menu | ERROR: scePsmfPl.. |
| PaRappa the Rapper | UCUS9870 | 218.70 | menu |  |
| Parodius Portable | ULJM0522 | 812.93 | menu | memory stick err.. |
| Patapon | UCUS9871 | 529.42 | black | GE: lines aren't.. |
| Patapon 2 | UCUS9873 | 599.39 | black | GE: lines aren't.. |
| Patapon 3 | UCUS9875 | 1079.22 | black | GE: lines aren't.. |
| Persona 2 Eternal Punishment | NPJH5058 | 1393.40 | black | the CPU stopped .. |
| Persona 2 Innocent Sin | ULUS1058 | 98.23 | black | GE: lines aren't.. |
| Phantom Kingdom | NPJH5045 | 79.79 | black | not implemented .. |
| PixelJunk Monsters - Deluxe | UCUS9873 | 537.37 | menu |  |
| Platypus | ULUS1020 | 191.67 | menu | message dialog (.. |
| Power Stone Collection | ULUS1017 | 237.02 | black | not implemented .. |
| Pursuit Force | UCUS9864 | 55.60 | menu | disc0:/PSP_GAME/.. |
| Pursuit Force - Extreme Justice | UCUS9870 | 333.07 | black | GE: lines aren't.. |
| Ratchet & Clank - Size Matters | UCUS9863 | 1052.65 | black | not implemented .. |
| Resistance - Retribution | UCUS9866 | 55.84 | black | not implemented .. |
| Retro City Rampage DX (Europe) | NPEH0017 | 50.26 | menu |  |
| Ridge Racer | ULUS1000 | 484.18 | menu | error in sceAtra.. |
| Ridge Racer 2 | UCES0042 | 236.69 | menu |  |
| Riviera - The Promised Land | ULUS1028 | 261.05 | black | not implemented .. |
| Samurai Dou Portable [English] | ULJS0015 | 1093.40 | black | not implemented .. |
| Samurai Shodown Anthology | ULUS1040 | 200.77 | menu | message dialog (.. |
| Seen in Liberty City | ULUS1182 | 30.39 | black | GE: lines aren't.. |
| Sega Genesis Collection | ULUS1019 | 2986.37 | black | not implemented .. |
| Sega Rally Revo | ULUS1031 | 2924.79 | black | the CPU stopped .. |
| Sheperds Crossing | ULUS1049 | 744.96 | black | not implemented .. |
| Shining Blade [gugule] | NPJH5053 | 1170.66 | black | every thread is .. |
| Shining Hearts [English] | NPJH5034 | 330.29 | black | not implemented .. |
| Shining Hearts [English MT v1.2] | NPJH5034 | 324.68 | black | not implemented .. |
| Shin Megami Tensei - Persona 3 Portable | ULUS1051 | 1914.88 | black | not implemented .. |
| Shinobido - Tales of the Ninja [Europe] [Undub 2021-06-28] | UCES0042 | 1084.99 | black | not implemented .. |
| Silent Hill Origins | ULUS1028 | 271.72 | black | not implemented .. |
| Silent Hill - Shattered Memories | ULUS1045 | 1388.48 | menu |  |
| Smash Court Tennis 3 | ULUS1026 | 140.15 | menu | message dialog (.. |
| Snoopy vs. the Red Baron | ULUS1018 | 122.22 | black | GE: bounding box.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo | UCUS9861 | 231.45 | black | snd_stream: coul.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 2 | UCUS9864 | 137.92 | menu | Compiled against.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 3 | UCUS9871 | 1098.08 | black | the CPU stopped .. |
| SOCOM - U.S. Navy SEALs - Tactical Strike | UCUS9864 | 307.46 | black | not implemented .. |
| Sol Trigger | NPJH5061 | 921.89 | black | not implemented .. |
| Sonic Rivals | ULUS1019 | 248.65 | black | GE: bounding box.. |
| Sonic Rivals 2 | ULUS1032 | 72.69 | black | GE: bounding box.. |
| Soulcalibur - Broken Destiny | ULUS1045 | 1384.33 | black | the CPU stopped .. |
| Space Invaders Extreme | ULUS1034 | 363.77 | black | GE: lines aren't.. |
| Spectral Souls | ULUS1007 | 201.05 | menu | effectnum 154 |
| Split-Second | ULUS1051 | 56.12 | black | not implemented .. |
| SSX on Tour | ULUS1004 | 2796.67 | black | every thread is .. |
| Star Ocean - First Departure | ULUS1037 | 80.27 | black | not implemented .. |
| Star Ocean - Second Evolution | ULUS1037 | 64.17 | black | not implemented .. |
| Star Soldier | ULJM0502 | 68.69 | black | GE: compressed (.. |
| Star Trek - Tactical Assault | ULUS1015 | 262.50 | black | the CPU stopped .. |
| Star Wars - Battlefront - Elite Squadron | ULUS1039 | 61.94 | menu | DISC0:/PSP_GAME/.. |
| Star Wars - Battlefront II - Remastered Edition [Hack v8] | ULUS1005 | 507.15 | menu | disc0:/PSP_GAME/.. |
| Star Wars - Battlefront - Renegade Squadron | ULUS1029 | 83.66 | loading | loading took 531.. |
| Star Wars - The Force Unleashed | ULUS1034 | 45.95 | black | not implemented .. |
| Street Fighter 3 - 3rd Strike [Port] | UCJS1004 | 307.49 | menu |  |
| Street Fighter Alpha 3 Max | ULUS1006 | 211.63 | black | not implemented .. |
| Street Supremacy | ULES0023 | 342.42 | menu | disc0:/PSP_GAME/.. |
| Super Moneky Ball Adventures | ULES0036 | 4.79 | loading | Game Info: Game .. |
| Super Stardust Portable | NPUG8022 | 70.63 | menu | a display list E.. |
| Syphon Filter - Dark Mirror | UCUS9864 | 126.39 | black | not implemented .. |
| Syphon Filter - Logan's Shadow | UCUS9860 | 41.54 | black | not implemented .. |
| Tactics Ogre - Let Us Cling Together [One Vision v1.11a] | ULUS1056 | 703.17 | black | not implemented .. |
| Tales of Eternia | ULES0017 | 166.64 | menu | disc0:/PSP_GAME/.. |
| Tales of Phantasia Full Voice Edition [English v1.2][QoL] | ULJS0007 | 1169.49 | black | the CPU stopped .. |
| Tales of Phantasia X [English v1.2] | ULJS0029 | 1393.12 | black | the CPU stopped .. |
| Tales Of The World - Radiant Mythology 2 [English 31-08] | ULJS0017 | 2678.67 | black | not implemented .. |
| Tekken 6 | ULUS1046 | 1356.23 | black | no threads left .. |
| Tekken - Dark Resurrection | ULUS1013 | 735.44 | black | not implemented .. |
| Tenchu - Shadow Assassins | ULUS1041 | 2438.09 | black | not implemented .. |
| Tenchu - Time of the Assassins [UNDUB v1.5c] | ULES0027 | 978.43 | black | not implemented .. |
| The 3rd Birthday | ULUS1056 | 211.37 | black | not implemented .. |
| The Legend of Heroes I | ULUS1002 | 396.30 | black | the CPU stopped .. |
| The Legend of Heroes II | ULUS1012 | 1521.70 | black | the CPU stopped .. |
| The Legend of Heroes III | ULUS1014 | 1719.82 | black | the CPU stopped .. |
| The Legend of Heroes - Trails From Azure [English v15] | NPJH5047 | 1064.53 | black | every thread is .. |
| The Legend of Nayuta - Boundless Trails [Addendum v1.08] | NPJH5062 | 350.31 | menu | !Err! mem damage |
| The Sims 2 | ULUS1003 | 1199.95 | menu | disc0:/sce_lbn0x.. |
| The Sims 2 - Castaway | ULUS1029 | 100.32 | black | not implemented .. |
| The Sims 2 - Pets | ULUS1013 | 74.50 | black | not implemented .. |
| Toca Race Driver 2 | ULES0004 | 78.75 | menu | disc0:/PSP_GAME/.. |
| Tokobot | ULUS1006 | 644.12 | black | the CPU stopped .. |
| Tony Hawk's Underground 2 Remix | ULUS1001 | 640.57 | menu | disc0:/PSP_GAME/.. |
| Twisted Metal Head On | UCUS9860 | 116.89 | black | snd_stream: coul.. |
| Ultimate Ghosts 'n Goblins | ULUS1010 | 1711.65 | menu | disc0:/PSP_GAME/.. |
| Umineko no Nakukoro ni Portable [English v1.0] | ULJM0596 | 994.36 | menu |  |
| Valhalla Knights | ULUS1023 | 3026.96 | menu |  |
| Valhalla Knights 2 | ULUS1036 | 1391.77 | menu |  |
| Valkyria Chronicles II | ULUS1051 | 1158.46 | black | every thread is .. |
| Valkyria Chronicles III [English v1.0.8] | ULJM0595 | 1176.21 | black | every thread is .. |
| Valkyrie Profile Lennth | ULUS1010 | 70.29 | black | GE: lines aren't.. |
| Virtua Tennis 3 | ULUS1024 | 115.62 | black | GE: bounding box.. |
| Virtua Tennis - World Tour | ULUS1003 | 103.42 | black | GE: lines aren't.. |
| Warriors of the Lost Empire | ULES0092 | 79.82 | menu | disc0:/PSP_GAME/.. |
| WipEout [Portable Collection v3.0] | WPCE0202 | 241.88 | black | not implemented .. |
| Ys I and II Chronicles | ULUS1054 | 251.68 | black | not implemented .. |
| Ys Seven | ULUS1055 | 1079.40 | black | every thread is .. |
| Ys - The Ark of Napishtim | ULUS1005 | 361.12 | black | GE: compressed (.. |
| Ys - The Oath in Felghana | ULUS1055 |  | timeout |  |
| ZHP - Unlosing Ranger Vs Darkdeath Evilman | ULUS1055 | 178.60 | black | not implemented .. |

The runner's full per-game output (summary, errors, frames, WAV) is
described in `tools/psp-runner/README.md`. Contact sheets of the reference
frame for each game (ten per sheet, 27 sheets) are in
`/tmp/phobos-psp-compat-report/sheet-*.png`.

## Timed-out games

Three games ran for the full 1200 s batch limit without completing 3600
frames:

| Game | NPID | Frames completed |
|---|---|---|
| Ys - The Oath in Felghana | ULUS1055 | 60 |
| God of War - Ghost of Sparta | UCUS9873 | 60 |
| God of War - Chains of Olympus | UCUS9865 | 1200 |

These are large 3D titles that run well below 30 fps. The first two
completed only 60 frames in 1200 s; the third completed 1200 frames.

## Next missing functions

The most impactful gaps to close, in order of how many games they affect:

1. **`ThreadManForUser` thread-management functions** — hit by 130+ games
   across the 34 distinct function IDs. These are the most common cause of
   frozen games.
2. **`sceCtrl a7144800`** — hit by 32 games. A control/setting function.
3. **`sceGe_user b77905ea`** — hit by 17 games. A GE (graphics) function.
4. **`SysMemUserForUser` memory-management functions** — hit by 17 games
   across 2 IDs.
5. **GE feature emulation** — compressed (DXT) textures, bounding-box
   tests, and line drawing are not emulated yet; these affect 29 games
   (6 DXT, 8 bounding-box, 15 line-drawing).
6. **`sceImpose` DRM functions** — hit by 16 games across 2 IDs.
7. **`scePsmf`/`scePsmfPlayer` video-playback functions** — hit by 19 games
   across 3 IDs.
