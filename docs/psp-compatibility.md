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

The runner captures a PNG at frames 60, 300, 1200, and 3600 (each named by
the frame run, after waiting for the screen to present a new picture), and a
WAV of the full run. A game is considered to have "booted" if its final
frame shows a usable UI (title screen, main menu, language select, save
warning, or profile dialog). A game is in "gameplay" if its final frame
shows active game content. A game is "black" if its final frame is a black
screen (movie, loading, crash, or GE failure). Three games timed out at the
1200 s batch limit and are marked separately.

The reference frame for each game is the last frame the runner captured:
`frame-003600.png` where the screen presented by the last frame (166
games), else `frame-001200.png` (96 games), else `frame-000060.png` (2
timed-out games that were so slow only 60 frames completed in 1200 s).

## Summary

| Category | Count |
|---|---|
| Boots to menu/title (usable UI) | 104 |
| Black (movie or loading?) | 155 |
| Stuck loading | 4 |
| Timed out (killed at 1200 s) | 3 |
| **Total** | **266** |

The 104 games that boot to a usable UI include title screens, main menus,
language selects, save-data warnings, and profile dialogs. These games
loaded their code and reached their first interactive screen. The 155 black
games show a black screen; at this commit, movies don't draw yet (FFmpeg
arrives with #153), so a black frame often means a movie or a transition,
not a freeze. The rest are a crash in an unimplemented kernel function, a
GE (Direct3D) feature not yet emulated (compressed DXT textures, bounding-box
tests, line drawing), or the CPU stopping in a game thread. The 4 loading
games are stuck on a loading screen.

## Speed distribution

| Range | Count |
|---|---|
| < 30 fps (very slow) | 13 |
| 30–60 fps | 15 |
| 60–200 fps | 72 |
| > 200 fps | 166 |

These fps are the runner's own: unthrottled, built at `-O1` in `BUILD_DEBUG`
mode, with 2 GE threads. They are not the app's speed. Most games run well
above 60 fps (166 of 266 run above 200 fps). The 13 very slow games
(< 30 fps) are typically large 3D titles that stress the GE.

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
entries are memory-management functions. `sceImpose` entries are the
HOME-menu overlay functions (volume, brightness, battery, language).
`scePsmf`/`scePsmfPlayer` entries are video-playback functions.

## Per-game results

Each row: title, NPID (region-disc), speed (fps), how far it gets, and the
last note from the runner. "menu" = boots to a usable UI; "black" = black
screen (movie, loading, crash, or GE failure); "loading" = stuck on a
loading screen; "timeout" = killed at 1200 s.

Note: the NPID is read from each game's PARAM.SFO `DISC_ID` field. The
filename stem is the reliable per-game identifier; the NPID is the disc's
own ID.

| Game | NPID | fps | State | Last note |
|---|---|---|---|---|
| 007 - From Russia with Love | ULUS10080 | 354.52 | menu | DISC0:/PSP_GAME/.. |
| 50 Cent - Bulletproof - G-Unit Edition | ULUS10128 | 134.14 | black | GE: compressed (.. |
| 7th Dragon 2020 [English v0.91] | NPJH50459 | 1237.55 | black | not implemented .. |
| 7th Dragon 2020-II [English Patched v0.91] | NPJH50716 | 1254.68 | black | not implemented .. |
| Ace Combat - Joint Assault | ULUS10511 | 1339.65 | black | not implemented .. |
| Ace Combat X - Skies of Deception | ULUS10176 | 384.71 | black | not implemented .. |
| Aces of War [Europe] | ULES00590 | 872.19 | black | fatal error : re.. |
| Activision Hits Remixed | ULUS10186 | 4.16 | menu | Calling FreeMusi.. |
| After Burner - Black Falcon | ULUS10244 | 24.01 | loading |  Loading SCE Mod.. |
| Angus Hates Aliens | NPUZ00374 | 656.43 | black | not implemented .. |
| Ape Escape Academy | UCUS98619 | 204.98 | menu | disc0:/PSP_GAME/.. |
| Ape Escape - On the Loose | UCUS98609 | 154.71 | menu | disc0:/PSP_GAME/.. |
| Archer Maclean's Mercury | ULUS10017 | 533.32 | menu | Memory available.. |
| Armored Core 3 Portable [True Analogs Mod v1.02] | NPUH10023 | 427.68 | black | sceMpegQueryStre.. |
| Armored Core - Formula Front International [True Analogs v1.0] | ULJS19001 | 1764.75 | black | the CPU stopped .. |
| Armored Core Last Raven Portable [True Analogs Mod v1.03] | NPUH10024 | 892.48 | black | sceMpegQueryStre.. |
| Armored Core Silent Line Portable [True Analogs v1.02] | NPUH10025 | 991.44 | black | the CPU stopped .. |
| Army of Two - The 40th Day | ULUS10472 | 1619.89 | black | not implemented .. |
| ATV Offroad Fury - Blazin' Trails | UCUS98603 | 141.51 | menu |  |
| ATV Offroad Fury Pro | UCUS98648 | 1374.44 | black | the CPU stopped .. |
| Battle vs. Chess [Europe Proto] | ULES01517 | 35.73 | black | GE: compressed (.. |
| Black Wolves Saga - Last Hope | ULJM06220 | 1346.23 | menu |  |
| BlazBlue - Calamity Trigger | ULUS10519 | 362.29 | menu | disc0:/PSP_GAME/.. |
| BlazBlue - Continuum Shift II | ULUS10579 | 373.92 | menu | disc0:/PSP_GAME/.. |
| Blitz - Overtime | ULUS10200 | 62.16 | menu | Allocation not a.. |
| Brandish - The Dark Revenant | NPUH10195 | 3.72 | black | not implemented .. |
| Brave Story New Traveler | ULUS10279 | 74.25 | menu | *** sgxpsp.c(187.. |
| Brothers in Arms - D-Day | ULUS10193 | 740.52 | menu |  |
| Bubble Bobble Evolution | ULUS10143 | 141.54 | menu | Return to game? |
| Burnout Dominator | ULUS10236 | 148.41 | black | GE: lines aren't.. |
| Burnout Legends | ULUS10025 | 186.36 | menu | disc0:/PSP_GAME/.. |
| Castlevania - The Dracula X Chronicles | ULUS10277 | 1142.42 | menu |  |
| Chili Con Carnage | ULUS10216 | 234.76 | black | not implemented .. |
| Cladun - This Is An RPG | NPUH10072 | 84.07 | black | not implemented .. |
| Colin McRae Rally 2005 Plus [Europe] | ULES00111 | 511.96 | menu | HeapCallFail(): .. |
| Corpse Party - Sweet Sachikos Hysteric Birthday | ULJM06114 | 1088.98 | menu | Utility Savedata.. |
| Crash Tag Team Racing | ULUS10044 | 919.38 | black | the CPU stopped .. |
| Crazy Taxi - Fare Wars | ULUS10273 | 642.38 | menu | PSPCI: File cach.. |
| Crimson Gem Saga | ULUS10400 | 32.30 | black | not implemented .. |
| Crisis Core - Final Fantasy VII | ULUS10336 | 1392.32 | menu |  |
| Crush | ULUS10238 | 233.53 | black | the CPU stopped .. |
| Cube | ULUS10223 | 861.51 | black | not implemented .. |
| Dante's Inferno | ULUS10469 | 79.28 | black | not implemented .. |
| Darkstalkers Chronicle - The Chaos Tower | ULUS10005 | 215.60 | menu | disc0:/PSP_GAME/.. |
| Daxter | UCUS98618 | 83.02 | menu | disc0:/PSP_GAME/.. |
| Dead Head Fred | ULUS10288 | 58.57 | black | GE: bounding box.. |
| Dead or Alive - Paradise | ULUS10521 | 19.11 | black | not implemented .. |
| Dead to Rights - Reckoning | ULUS10023 | 216.20 | loading | Finished loading.. |
| Death Jr. | ULUS10027 | 1326.99 | black | the CPU stopped .. |
| Death Jr. II - Root of Evil | ULUS10027 | 9.37 | black | GE: bounding box.. |
| Def Jam - Fight for NY - The Takeover | ULUS10100 | 3201.61 | black | not implemented .. |
| Dirt 2 (En,Fr,Es) | ULUS10471 | 181.94 | black | not implemented .. |
| Disaster Report 3 [English] | ULJS00191 | 1392.00 | black | no threads left .. |
| Disgaea 2 - Dark Hero Days | ULUS10461 | 683.86 | black | not implemented .. |
| Disgaea - Afternoon of Darkness | ULUS10308 | 129.66 | black | GE: compressed (.. |
| Disgaea Infinite | ULUS10522 | 156.03 | black | not implemented .. |
| Disney-Pixar Cars | UCUS98766 | 342.85 | menu |  |
| Disney-Pixar Cars 2 | UCUS98766 | 5.07 | black | not implemented .. |
| Disney-Pixar Cars - Race-O-Rama | ULUS10073 | 121.89 | menu | message dialog (.. |
| Dissidia 012 - Duodecim Final Fantasy | ULUS10566 | 1395.62 | black | no threads left .. |
| Dissidia Final Fantasy | ULUS10437 | 321.02 | menu | Please do not re.. |
| Dragonball Z Shin Budokai | ULUS10234 | 883.97 | menu | PSPCI: Total 5 f.. |
| Dragonball Z Shin Budokai - Another Road | ULUS10234 | 775.56 | menu | PSPCI: File cach.. |
| Dragon Ball Z - Tenkaichi Tag Team | ULUS10537 | 1393.21 | black | no threads left .. |
| Driver 76 | ULUS10235 | 254.27 | black | not implemented .. |
| Fate Extra CCC [English v1.0] | NPJH50505 | 128.33 | menu |  |
| Fate Extra Perfect Patch | ULUS10576 | 2400.18 | black | the CPU stopped .. |
| Fat Princess Fistful of Cake | UCUS98740 | 1390.50 | menu |  |
| Final Fantasy - 20th Anniversary Edition | ULUS10251 | 912.60 | black | not implemented .. |
| Final Fantasy II - 20th Anniversary Edition | ULUS10263 | 1723.87 | black | the CPU stopped .. |
| Final Fantasy III | NPUH10125 | 244.52 | black | not implemented .. |
| Final Fantasy IV | ULUS10560 | 654.39 | black | not implemented .. |
| Final Fantasy Tactics - War of the Lions Tweak [v2.52] | ULUS10297 | 772.93 | black | not implemented .. |
| Final Fantasy Type 0 | NPJH50443 | 198.22 | black | not implemented .. |
| Full Auto 2 - Battlelines | ULUS10220 | 140.40 | menu |  |
| Fuuun Shinsengumi Bakumatsu den Portable [japan] | ULJM05561 | 82.93 | black | not implemented .. |
| Genso Suikoden Tsumugareshi Hyakunen no Toki | NPJH50535 | 2038.63 | black | the CPU stopped .. |
| Ghostbusters - The Video Game | ULUS10486 | 1387.46 | black | no threads left .. |
| Ghost in the Shell - Stand Alone Complex | ULUS10020 | 261.96 | menu | disc0:/PSP_GAME/.. |
| Gitaroo Man Lives! | ULUS10207 | 77.49 | menu | disc0:/PSP_GAME/.. |
| God Eater 2 [English v2.0 RedArtz] | NPJH50832 | 1390.07 | black | the CPU stopped .. |
| God of War - Chains of Olympus | UCUS98653 |  | timeout |  |
| God of War - Ghost of Sparta | UCUS98737 |  | timeout |  |
| Gradius Collection | ULUS10103 | 1038.46 | menu | disc0:/PSP_GAME/.. |
| Grand Knights History | ULJS00394 | 276.33 | black | not implemented .. |
| Grand Theft Auto - Chinatown Wars | ULUS10490 | 96.19 | black | not implemented .. |
| Grand Theft Auto - Liberty City Stories | ULUS10041 | 43.89 | black | GE: lines aren't.. |
| Grand Theft Auto - Sindacco Chronicles [v2.0 English] | ULUS01826 | 52.69 | black | GE: lines aren't.. |
| Grand Theft Auto - Vice City Stories | ULUS10160 | 61.30 | black | GE: lines aren't.. |
| Gran Turismo | UCUS98632 | 1388.24 | black | not implemented .. |
| Growlanser IV - Wayfarer of Time [UNDUB v1.3] [HQ OPED] | ULUS10593 | 1377.47 | black | not implemented .. |
| Guilty Gear Judgment | ULUS10104 | 215.57 | menu | disc0:/PSP_GAME/.. |
| Guilty Gear XX Accent Core Plus | ULUS10409 | 152.72 | menu | Would you like t.. |
| Gungnir | ULUS10592 | 218.35 | menu | POLL SEMA |
| Gurumin - A Monstrous Adventure | ULUS10228 | 111.91 | black | GE: bounding box.. |
| Hammerin Hero | ULUS10392 | 77.09 | black | not implemented .. |
| Harvest Moon - Hero of Leaf Valley | ULUS10458 | 95.60 | menu | psaFileInit()>ps.. |
| Hexyz Force [UNDUB v1.2b] | ULUS10506 | 912.74 | menu |  |
| Hot Shots Golf - Open Tee | UCUS98693 | 160.05 | menu | *** sgxpsp.c(158.. |
| Hot Shots Golf - Open Tee 2 | UCUS98693 | 209.78 | menu | *** sgxpsp.c(209.. |
| Hot Shots Tennis - Get a Grip | UCUS98701 | 438.13 | black | not implemented .. |
| Hot Wheels - Ultimate Racing | ULUS10239 | 482.30 | menu | disc0:/PSP_GAME/.. |
| IL-2 Sturmovik - Birds of Prey | ULUS10476 | 324.26 | black | not implemented .. |
| Innocent Life - A Futuristic Harvest Moon | ULUS10219 | 445.80 | menu | program end |
| Jak and Daxter - The Lost Frontier | UCUS98634 | 73.43 | black | not implemented .. |
| Jeanne d'Arc | UCUS98700 | 1564.24 | black | fatal error : ma.. |
| Juiced 2 - Hot Import Nights | ULUS10312 | 4.18 | menu | tgIoFillCache wa.. |
| Juiced - Eliminator | ULUS10090 | 385.25 | menu | disc0:/PSP_GAME/.. |
| Kenka Bancho - Badass Rumble | ULUS10442 | 1407.60 | black | not implemented .. |
| Key Of Heaven | UCES00178 | 238.37 | menu | === SGX system i.. |
| Kidou Senshi Gundam Gundam vs. Gundam NEXT PLUS [English] | NPJH50107 | 159.71 | black | not implemented .. |
| Killzone - Liberation | UCUS98646 | 831.48 | menu |  |
| Kingdom Hearts Birth by Sleep Final Mix [English] | ULJM05775 | 342.97 | black | not implemented .. |
| King of Fighters, The - Orochi Saga | ULUS10360 | 121.17 | menu |  |
| Kisou Ryouhei Gunhound EX | NPJH50723 | 519.41 | menu |  |
| Kurohyou 2 [English v1.0] | NPJH50562 | 1391.81 | black | no threads left .. |
| Kurohyou Ryu ga Gotoku Shinshou [English v1.2 TeamK4L] | NPJH50333 | 55.38 | black | not implemented .. |
| La Pucelle Ragnarok | ULJS00244 | 2063.38 | menu | Heap: 3454.28 |
| Last Ranker | ULJM05676 | 1720.61 | menu | disc0:/PSP_GAME/.. |
| LittleBigPlanet | UCUS98744 | 266.69 | menu | Error: ../../Cod.. |
| LocoRoco | UCUS98731 | 437.56 | menu | disc0:/PSP_GAME/.. |
| LocoRoco 2 | UCUS98731 | 310.27 | menu | disc0:/PSP_GAME/.. |
| Lumines II | ULUS10183 | 1064.33 | black | not implemented .. |
| Lumines - Puzzle Fusion | ULUS10002 | 101.40 | menu | ----Lumines Task.. |
| Lunar - Silver Star Harmony | ULUS10482 | 219.17 | menu | disc0:/PSP_GAME/.. |
| MACH | ULES00565 | 116.93 | menu | Error in sceAtra.. |
| Macross - Ace Frontier [Japan] | ULJS00158 | 202.34 | black | GE: curved surfa.. |
| Macross - Triangle Frontier [Japan] | ULJS00321 | 1391.24 | menu |  |
| Macross - Ultimate Frontier [Japan] | NPJH50050 | 505.68 | black | not implemented .. |
| Manhunt 2 [Uncensored] | ULUS10280 | 1863.89 | menu | message dialog (.. |
| MediEvil Resurrection | UCES00006 | 303.99 | menu | System free: 369.. |
| Mega Man Maverick Hunter X [UNDUB v1.1] | ULUS10068 | 161.01 | black | not implemented .. |
| Mega Man Powered Up [UNDUB v1.3] | ULUS10091 | 115.38 | black | not implemented .. |
| Melodie (Prototype) | PETR00010 | 77.05 | menu |  |
| Me & My Katamari | ULUS10094 | 109.41 | black | GE: lines aren't.. |
| Mercury Meltdown | ULUS10133 | 74.48 | menu |  |
| Metal Gear Acid | ULUS10077 | 160.33 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Acid 2 | ULUS10077 | 394.79 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Solid - Digital Graphic Novel | ULUS10108 | 1056.63 | black | not implemented .. |
| Metal Gear Solid - Peace Walker [v2.00] | ULUS10509 | 67.60 | menu | disc0:/PSP_GAME/.. |
| Metal Gear Solid - Portable Ops | ULUS10202 | 479.82 | black | not implemented .. |
| Metal Gear Solid - Portable Ops Plus | ULUS10202 | 375.68 | menu | disc0:/PSP_GAME/.. |
| Metal Slug Anthology | ULUS10154 | 225.18 | black | not implemented .. |
| Metal Slug XX | ULUS10495 | 166.14 | menu |  |
| Miami Vice - The Game | ULUS10109 | 84.20 | menu | ** READ FAILED. .. |
| Micro Machines V4 | ULUS10129 | 481.48 | menu | disc0:/PSP_GAME/.. |
| Midnight Club 3 - DUB Edition [v2.02] | ULUS10021 | 16.87 | menu | disc0:/PSP_GAME/.. |
| Midnight Club - L.A. Remix | ULUS10383 | 32.62 | menu | message dialog (.. |
| ModNation Racers | UCUS98741 | 175.48 | menu | Videoplayer: inv.. |
| Monster Hunter Portable 3rd [English v6.1.0 Team Maverick One] | ULJM05800 | 644.11 | black | not implemented .. |
| Monster Hunter Portable 3rd HD | NPJB40001 | 1386.98 | black | no threads left .. |
| Monster Kingdom Jewel Summoner | ULUS10211 | 72.09 | menu |  |
| Moto GP | ULUS10153 | 295.27 | menu | message dialog (.. |
| MotorStorm - Arctic Edge | UCUS98743 | 768.60 | black | the CPU stopped .. |
| MX vs. ATV - On the Edge | ULUS10071 | 43.74 | menu | disc0:/PSP_GAME/.. |
| MX vs. ATV Reflex | ULUS10429 | 147.03 | black | not implemented .. |
| MX vs. ATV Untamed | ULUS10330 | 220.25 | black | not implemented .. |
| Naruto Shippuden Ultimate Ninja Impact | ULUS10582 | 2832.82 | black | not implemented .. |
| Need for Speed - Carbon - Own the City | ULUS10114 | 109.91 | black | not implemented .. |
| Need for Speed - Most Wanted - 5-1-0 | ULUS10036 | 1005.76 | black | GE: a display li.. |
| Need for Speed - ProStreet | ULUS10331 | 98.22 | black | GE: compressed (.. |
| Need for Speed - Shift | ULUS10462 | 75.58 | black | GE: lines aren't.. |
| Need for Speed - Underground Rivals | ULUS10007 | 100.59 | menu | pathname= Global.. |
| OutRun 2006 - Coast 2 Coast | ULUS10064 | 231.85 | black | not implemented .. |
| PAC-MAN CE | NPUZ00125 | 14.67 | menu |  |
| Pac-Man World Rally | ULUS10149 | 364.31 | black | GE: bounding box.. |
| Pangya Fantasy Golf [Black Screen Fix] | ULUS10438 | 240.70 | menu | ERROR: scePsmfPl.. |
| PaRappa the Rapper | UCUS98702 | 218.70 | menu |  |
| Parodius Portable | ULJM05220 | 812.93 | menu | memory stick err.. |
| Patapon | UCUS98732 | 529.42 | black | GE: lines aren't.. |
| Patapon 2 | UCUS98732 | 599.39 | black | GE: lines aren't.. |
| Patapon 3 | UCUS98751 | 1079.22 | black | GE: lines aren't.. |
| Persona 2 Eternal Punishment | NPJH50581 | 1393.40 | black | the CPU stopped .. |
| Persona 2 Innocent Sin | ULUS10584 | 98.23 | black | GE: lines aren't.. |
| Phantom Kingdom | NPJH50451 | 79.79 | black | not implemented .. |
| PixelJunk Monsters - Deluxe | UCUS98739 | 537.37 | menu |  |
| Platypus | ULUS10203 | 191.67 | menu | message dialog (.. |
| Power Stone Collection | ULUS10171 | 237.02 | black | not implemented .. |
| Pursuit Force | UCUS98640 | 55.60 | menu | disc0:/PSP_GAME/.. |
| Pursuit Force - Extreme Justice | UCUS98640 | 333.07 | black | GE: lines aren't.. |
| Ratchet & Clank - Size Matters | UCUS98633 | 1052.65 | black | not implemented .. |
| Resistance - Retribution | UCUS98668 | 55.84 | black | not implemented .. |
| Retro City Rampage DX (Europe) | NPEH00170 | 50.26 | menu |  |
| Ridge Racer | UCES00422 | 484.18 | menu | error in sceAtra.. |
| Ridge Racer 2 | UCES00422 | 236.69 | menu |  |
| Riviera - The Promised Land | ULUS10286 | 261.05 | black | not implemented .. |
| Samurai Dou Portable [English] | ULJS00155 | 1093.40 | black | not implemented .. |
| Samurai Shodown Anthology | ULUS10401 | 200.77 | menu | message dialog (.. |
| Seen in Liberty City | ULUS11826 | 30.39 | black | GE: lines aren't.. |
| Sega Genesis Collection | ULUS10192 | 2986.37 | black | not implemented .. |
| Sega Rally Revo | ULUS10311 | 2924.79 | black | the CPU stopped .. |
| Sheperds Crossing | ULUS10499 | 744.96 | black | not implemented .. |
| Shining Blade [gugule] | NPJH50530 | 1170.66 | black | every thread is .. |
| Shining Hearts [English] | NPJH50342 | 330.29 | black | not implemented .. |
| Shining Hearts [English MT v1.2] | NPJH50342 | 324.68 | black | not implemented .. |
| Shin Megami Tensei - Persona 3 Portable | ULUS10512 | 1914.88 | black | not implemented .. |
| Shinobido - Tales of the Ninja [Europe] [Undub 2021-06-28] | UCES00421 | 1084.99 | black | not implemented .. |
| Silent Hill Origins | ULUS10285 | 271.72 | black | not implemented .. |
| Silent Hill - Shattered Memories | ULUS10450 | 1388.48 | menu |  |
| Smash Court Tennis 3 | ULUS10269 | 140.15 | menu | message dialog (.. |
| Snoopy vs. the Red Baron | ULUS10189 | 122.22 | black | GE: bounding box.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo | UCUS98645 | 231.45 | black | snd_stream: coul.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 2 | UCUS98645 | 137.92 | menu | Compiled against.. |
| SOCOM - U.S. Navy SEALs - Fireteam Bravo 3 | UCUS98716 | 1098.08 | black | the CPU stopped .. |
| SOCOM - U.S. Navy SEALs - Tactical Strike | UCUS98649 | 307.46 | black | not implemented .. |
| Sol Trigger | NPJH50619 | 921.89 | black | not implemented .. |
| Sonic Rivals | ULUS10323 | 248.65 | black | GE: bounding box.. |
| Sonic Rivals 2 | ULUS10323 | 72.69 | black | GE: bounding box.. |
| Soulcalibur - Broken Destiny | ULUS10457 | 1384.33 | black | the CPU stopped .. |
| Space Invaders Extreme | ULUS10346 | 363.77 | black | GE: lines aren't.. |
| Spectral Souls | ULUS10076 | 201.05 | menu | effectnum 154 |
| Split-Second | ULUS10513 | 56.12 | black | not implemented .. |
| SSX on Tour | ULUS10042 | 2796.67 | black | every thread is .. |
| Star Ocean - First Departure | ULUS10374 | 80.27 | black | not implemented .. |
| Star Ocean - Second Evolution | ULUS10375 | 64.17 | black | not implemented .. |
| Star Soldier | ULJM05026 | 68.69 | black | GE: compressed (.. |
| Star Trek - Tactical Assault | ULUS10150 | 262.50 | black | the CPU stopped .. |
| Star Wars - Battlefront - Elite Squadron | ULUS10390 | 61.94 | menu | DISC0:/PSP_GAME/.. |
| Star Wars - Battlefront II - Remastered Edition [Hack v8] | ULUS10053 | 507.15 | menu | disc0:/PSP_GAME/.. |
| Star Wars - Battlefront - Renegade Squadron | ULUS10292 | 83.66 | loading | loading took 531.. |
| Star Wars - The Force Unleashed | ULUS10345 | 45.95 | black | not implemented .. |
| Street Fighter 3 - 3rd Strike [Port] | UCJS10041 | 307.49 | menu |  |
| Street Fighter Alpha 3 Max | ULUS10062 | 211.63 | black | not implemented .. |
| Street Supremacy | ULES00239 | 342.42 | menu | disc0:/PSP_GAME/.. |
| Super Moneky Ball Adventures | ULES00364 | 4.79 | loading | Game Info: Game .. |
| Super Stardust Portable | NPUG80221 | 70.63 | menu | a display list E.. |
| Syphon Filter - Dark Mirror | UCUS98641 | 126.39 | black | not implemented .. |
| Syphon Filter - Logan's Shadow | UCUS98606 | 41.54 | black | not implemented .. |
| Tactics Ogre - Let Us Cling Together [One Vision v1.11a] | ULUS10565 | 703.17 | black | not implemented .. |
| Tales of Eternia | ULES00176 | 166.64 | menu | disc0:/PSP_GAME/.. |
| Tales of Phantasia Full Voice Edition [English v1.2][QoL] | ULJS00079 | 1169.49 | black | the CPU stopped .. |
| Tales of Phantasia X [English v1.2] | ULJS00293 | 1393.12 | black | the CPU stopped .. |
| Tales Of The World - Radiant Mythology 2 [English 31-08] | ULJS00175 | 2678.67 | black | not implemented .. |
| Tekken 6 | ULUS10466 | 1356.23 | black | no threads left .. |
| Tekken - Dark Resurrection | ULUS10139 | 735.44 | black | not implemented .. |
| Tenchu - Shadow Assassins | ULUS10419 | 2438.09 | black | not implemented .. |
| Tenchu - Time of the Assassins [UNDUB v1.5c] | ULES00277 | 978.43 | black | not implemented .. |
| The 3rd Birthday | ULUS10567 | 211.37 | black | not implemented .. |
| The Legend of Heroes I | ULUS10022 | 396.30 | black | the CPU stopped .. |
| The Legend of Heroes II | ULUS10022 | 1521.70 | black | the CPU stopped .. |
| The Legend of Heroes III | ULUS10022 | 1719.82 | black | the CPU stopped .. |
| The Legend of Heroes - Trails From Azure [English v15] | NPJH50473 | 1064.53 | black | every thread is .. |
| The Legend of Nayuta - Boundless Trails [Addendum v1.08] | NPJH50625 | 350.31 | menu | !Err! mem damage |
| The Sims 2 | ULUS10296 | 1199.95 | menu | disc0:/sce_lbn0x.. |
| The Sims 2 - Castaway | ULUS10296 | 100.32 | black | not implemented .. |
| The Sims 2 - Pets | ULUS10031 | 74.50 | black | not implemented .. |
| Toca Race Driver 2 | ULES00042 | 78.75 | menu | disc0:/PSP_GAME/.. |
| Tokobot | ULUS10061 | 644.12 | black | the CPU stopped .. |
| Tony Hawk's Underground 2 Remix | ULUS10014 | 640.57 | menu | disc0:/PSP_GAME/.. |
| Twisted Metal Head On | UCUS98601 | 116.89 | black | snd_stream: coul.. |
| Ultimate Ghosts 'n Goblins | ULUS10105 | 1711.65 | menu | disc0:/PSP_GAME/.. |
| Umineko no Nakukoro ni Portable [English v1.0] | ULJM05968 | 994.36 | menu |  |
| Valhalla Knights | ULUS10366 | 3026.96 | menu |  |
| Valhalla Knights 2 | ULUS10366 | 1391.77 | menu |  |
| Valkyria Chronicles II | ULUS10515 | 1158.46 | black | every thread is .. |
| Valkyria Chronicles III [English v1.0.8] | ULUS10515 | 1176.21 | black | every thread is .. |
| Valkyrie Profile Lennth | ULUS10107 | 70.29 | black | GE: lines aren't.. |
| Virtua Tennis 3 | ULUS10246 | 115.62 | black | GE: bounding box.. |
| Virtua Tennis - World Tour | ULUS10037 | 103.42 | black | GE: lines aren't.. |
| Warriors of the Lost Empire | ULES00924 | 79.82 | menu | disc0:/PSP_GAME/.. |
| WipEout [Portable Collection v3.0] | WPCE02025 | 241.88 | black | not implemented .. |
| Ys I and II Chronicles | ULUS10547 | 251.68 | black | not implemented .. |
| Ys Seven | ULUS10551 | 1079.40 | black | every thread is .. |
| Ys - The Ark of Napishtim | ULUS10051 | 361.12 | black | GE: compressed (.. |
| Ys - The Oath in Felghana | ULUS10558 |  | timeout |  |
| ZHP - Unlosing Ranger Vs Darkdeath Evilman | ULUS10559 | 178.60 | black | not implemented .. |

The runner's full per-game output (summary, errors, frames, WAV) is
described in `tools/psp-runner/README.md`. Contact sheets of the reference
frame for each game (ten per sheet, 27 sheets) are in
`/tmp/phobos-psp-compat-report/sheet-*.png`.

## Timed-out games

Three games ran for the full 1200 s batch limit without completing 3600
frames:

| Game | NPID | Frames completed |
|---|---|---|
| Ys - The Oath in Felghana | ULUS10558 | 60 |
| God of War - Ghost of Sparta | UCUS98737 | 60 |
| God of War - Chains of Olympus | UCUS98653 | 1200 |

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
6. **`sceImpose` HOME-menu overlay functions** — hit by 16 games across 2 IDs.
7. **`scePsmf`/`scePsmfPlayer` video-playback functions** — hit by 19 games
   across 3 IDs.
