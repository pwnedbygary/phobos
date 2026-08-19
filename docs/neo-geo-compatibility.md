# Neo Geo (MVS/AES) Compatibility Matrix

**Scope:** This core is strictly **Neo Geo MVS/AES** (`enumerate()` in
`ares/ng/system/system.cpp` exposes only `[SNK] Neo Geo AES` and `[SNK] Neo Geo MVS`).
It is **NOT** a general arcade core. The "FBNeo + MAME hybrid" refers only to sourcing
Neo Geo ROM/driver data and decryption (CMC/CMC42/CMC50/SMA/PCM2/PVC, kof2k2-family)
from MAME and FBNeo to maximize Neo Geo compatibility — not emulating other arcade boards.

## Status legend
| Mark | Meaning |
|------|---------|
| ✓ | Verified working (on-device) |
| ✗ | Broken / known bug |
| — | Untested |

Columns: **Boot** (game loads/runs), **Gfx** (sprites/fix-layer correct),
**Audio** (sound correct), **Ctrl** (controls correct, P1-only by design on a
single handheld).

## Known core status (2026-08-19)
- **Graphics:** FIXED — sprite zoom tables (`loadZoomy`) + vflip/zoom decode corrected
  (mirrors MAME). KOF2003 verified at 59.2 FPS with correct sprites/backgrounds.
- **Controls:** P1/P2 input mirror FIXED (`controllerPlayerIndex` in `PhobosRunner.cpp`).
  P1 works; P2 requires a second controller (single handheld has one gamepad).
- **Audio:** Appears **functional** — KOF2003 has audio (user-confirmed on-device). The
  `ring buffer 0/12000` style log line is suspected to be a **string-formatting bug**,
  not a real audio fault. Verify the log formatting; otherwise audio is working.

## Priority tiers (hardest first — these strain the decrypt/board paths most)
1. **PVC / K2K2** — multi-stage decryption (P-ROM encryption + CMC50 graphics + PCM2 V-ROM)
2. **CMC42 / CMC50 / PCM2** — encrypted graphics ROMs
3. **SMA** — SMA-protection titles
4. **boot_*/rom_*** — bootlegs / alternate sets (protection often stripped; vary)
5. **standard** — unprotected early MVS/AES (baseline; most already run via upstream ares)

## Matrix (288 titles)
| Title | Set (ROM) | Protection | Boot | Gfx | Audio | Ctrl | Notes |
|-------|-----------|------------|------|-----|-------|------|-------|
| Matrimelee / Shin Gouketsuji Ichizoku Toukon (NGM-2660 ~ NGH-2660) | matrim | K2K2 | — | — | — | — |  |
| Metal Slug 5 (NGH-2680) | mslug5h | PVC | — | — | — | — |  |
| Metal Slug 5 (NGM-2680) | mslug5 | PVC | — | — | — | — |  |
| Samurai Shodown V / Samurai Spirits Zero (NGH-2700) | samsho5h | K2K2 | — | — | — | — |  |
| Samurai Shodown V / Samurai Spirits Zero (NGM-2700, set 1) | samsho5 | K2K2 | — | — | — | — |  |
| Samurai Shodown V / Samurai Spirits Zero (NGM-2700, set 2) | samsho5a | K2K2 | — | — | — | — |  |
| Samurai Shodown V Special / Samurai Spirits Zero Special (NGH-2720, 1st release, censored) | samsh5spho | K2K2 | — | — | — | — |  |
| Samurai Shodown V Special / Samurai Spirits Zero Special (NGH-2720, 2nd release, less censored) | samsh5sph | K2K2 | — | — | — | — |  |
| Samurai Shodown V Special / Samurai Spirits Zero Special (NGM-2720) | samsh5sp | K2K2 | — | — | — | — |  |
| SNK vs. Capcom - SVC Chaos (NGM-2690 ~ NGH-2690) | svc | PVC | — | — | — | — |  |
| The King of Fighters 2002 (NGM-2650 ~ NGH-2650) | kof2002 | K2K2 | — | — | — | — |  |
| The King of Fighters 2002 Plus (bootleg set 1) | kf2k2pls | K2K2 | — | — | — | — |  |
| The King of Fighters 2002 Plus (bootleg set 2) | kf2k2pla | K2K2 | — | — | — | — |  |
| The King of Fighters 2003 (NGH-2710) | kof2003h | PVC | ✓ | ✓ | ✓ (log artifact) | ✓ (P1) | Export set; same as kof2003 (audio also works) |
| The King of Fighters 2003 (NGM-2710, Export) | kof2003 | PVC | ✓ | ✓ | ✓ (log artifact) | ✓ (P1) | Graphics + controls verified on-device @59.2 FPS; AUDIO WORKS (user-confirmed) - ring-buffer 0/12000 log is a suspected string-format bug, not a real fault |
| Bang Bead | bangbead | CMC42 | — | — | — | — |  |
| Ganryu / Musashi Ganryuki | ganryu | CMC42 | — | — | — | — |  |
| Jockey Grand Prix (set 1) | jockeygp | CMC50 | — | — | — | — |  |
| Jockey Grand Prix (set 2) | jockeygpa | CMC50 | — | — | — | — |  |
| Metal Slug 3 (NGH-2560) | mslug3h | CMC42 | — | — | — | — |  |
| Metal Slug 4 (NGH-2630) | mslug4h | PCM2 | — | — | — | — |  |
| Metal Slug 4 (NGM-2630) | mslug4 | PCM2 | — | — | — | — |  |
| Metal Slug 4 Plus (bootleg) | ms4plus | PCM2 | — | — | — | — |  |
| Nightmare in the Dark | nitd | CMC42 | — | — | — | — |  |
| Pochi and Nyaa (Ver 2.00) | pnyaaa | PCM2 | — | — | — | — |  |
| Pochi and Nyaa (Ver 2.02) | pnyaa | PCM2 | — | — | — | — |  |
| Prehistoric Isle 2 | preisle2 | CMC42 | — | — | — | — |  |
| Rage of the Dragons (NGH-2640?) | rotdh | PCM2 | — | — | — | — |  |
| Rage of the Dragons (NGM-2640?) | rotd | PCM2 | — | — | — | — |  |
| Sengoku 3 / Sengoku Densho 2001 (set 1) | sengoku3 | CMC42 | — | — | — | — |  |
| Sengoku 3 / Sengoku Densho 2001 (set 2) | sengoku3a | CMC42 | — | — | — | — |  |
| Strikers 1945 Plus | s1945p | CMC42 | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (Korean release, non-encrypted program) | kof99ka | CMC42 | — | — | — | — |  |
| The King of Fighters 2000 (not encrypted) | kof2000n | CMC50 | — | — | — | — |  |
| The King of Fighters 2001 (NGH-2621) | kof2001h | CMC50 | — | — | — | — |  |
| The King of Fighters 2001 (NGM-262?) | kof2001 | CMC50 | — | — | — | — |  |
| Zupapa! | zupapa | CMC42 | — | — | — | — |  |
| Garou - Mark of the Wolves (NGH-2530) | garouha | SMA | — | — | — | — |  |
| Garou - Mark of the Wolves (NGM-2530 ~ NGH-2530) | garouh | SMA | — | — | — | — |  |
| Garou - Mark of the Wolves (NGM-2530) | garou | SMA | — | — | — | — |  |
| Metal Slug 3 (NGM-2560) | mslug3 | SMA | — | — | — | — |  |
| Metal Slug 3 (NGM-2560, earlier) | mslug3a | SMA | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (earlier) | kof99e | SMA | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (Korean release) | kof99k | SMA | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (NGH-2510) | kof99h | SMA | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (NGM-2510) | kof99 | SMA | — | — | — | — |  |
| The King of Fighters 2000 (NGM-2570 ~ NGH-2570) | kof2000 | SMA | — | — | — | — |  |
| Crouching Tiger Hidden Dragon 2003 (hack of The King of Fighters 2001) | cthd2003 | boot_cthd2k3 | — | — | — | — |  |
| Crouching Tiger Hidden Dragon 2003 Super Plus (hack of The King of Fighters 2001) | ct2k3sp | boot_ct2k3sp | — | — | — | — |  |
| Crouching Tiger Hidden Dragon 2003 Super Plus (hack of The King of Fighters 2001, alternate) | ct2k3sa | boot_ct2k3sa | — | — | — | — |  |
| Fatal Fury 2 / Garou Densetsu 2 - Arata-naru Tatakai (NGM-047 ~ NGH-047) | fatfury2 | rom_fatfur2 | — | — | — | — |  |
| Garou - Mark of the Wolves (bootleg) | garoubl | boot_garoubl | — | — | — | — |  |
| King of Gladiator (bootleg of The King of Fighters '97) | kog | boot_kog | — | — | — | — |  |
| Lansquenet 2004 (bootleg of Shock Troopers - 2nd Squad) | lans2004 | boot_lans2004 | — | — | — | — |  |
| Matrimelee / Shin Gouketsuji Ichizoku Toukon (bootleg) | matrimbl | boot_matrimbl | — | — | — | — |  |
| Metal Slug 5 (bootleg) | mslug5b | boot_mslug5b | — | — | — | — |  |
| Metal Slug 5 Plus (bootleg) | ms5plus | boot_ms5plus | — | — | — | — |  |
| Metal Slug 6 (bootleg of Metal Slug 3) | mslug3b6 | boot_mslug3b6 | — | — | — | — |  |
| Metal Slug X - Super Vehicle-001 (NGM-2500 ~ NGH-2500) | mslugx | rom_mslugx | — | — | — | — |  |
| Samurai Shodown V / Samurai Spirits Zero (bootleg) | samsho5b | boot_samsho5b | — | — | — | — |  |
| SNK vs. Capcom - SVC Chaos (bootleg) | svcboot | boot_svcboot | — | — | — | — |  |
| SNK vs. Capcom - SVC Chaos Plus (bootleg, set 1) | svcplus | boot_svcplus | — | — | — | — |  |
| SNK vs. Capcom - SVC Chaos Plus (bootleg, set 2) | svcplusa | boot_svcplusa | — | — | — | — |  |
| SNK vs. Capcom - SVC Chaos Super Plus (bootleg) | svcsplus | boot_svcsplus | — | — | — | — |  |
| Super Bubble Pop (MVS) | sbp | boot_sbp | — | — | — | — |  |
| Super Sidekicks / Tokuten Ou | ssideki | rom_fatfur2 | — | — | — | — |  |
| The King of Fighters '97 Chongchu Jianghu Plus 2003 (bootleg) | kof97oro | boot_kof97oro | — | — | — | — |  |
| The King of Fighters '98 - The Slugfest / King of Fighters '98 - Dream Match Never Ends (Korean board, set 1) | kof98k | rom_kof98 | — | — | — | — |  |
| The King of Fighters '98 - The Slugfest / King of Fighters '98 - Dream Match Never Ends (Korean board, set 2) | kof98ka | rom_kof98 | — | — | — | — |  |
| The King of Fighters '98 - The Slugfest / King of Fighters '98 - Dream Match Never Ends (NGM-2420) | kof98 | rom_kof98 | — | — | — | — |  |
| The King of Fighters '98 - The Slugfest / King of Fighters '98 - Dream Match Never Ends (NGM-2420, alt board) | kof98a | rom_kof98 | — | — | — | — |  |
| The King of Fighters 10th Anniversary (bootleg of The King of Fighters 2002) | kof10th | boot_kf10th | — | — | — | — |  |
| The King of Fighters 10th Anniversary 2005 Unique (bootleg of The King of Fighters 2002) | kf2k5uni | boot_kf2k5uni | — | — | — | — |  |
| The King of Fighters 10th Anniversary Extra Plus (bootleg of The King of Fighters 2002) | kf10thep | boot_kf10thep | — | — | — | — |  |
| The King of Fighters 2002 (bootleg) | kof2002b | boot_kf2k2b | — | — | — | — |  |
| The King of Fighters 2002 Magic Plus (bootleg) | kf2k2mp | boot_kf2k2mp | — | — | — | — |  |
| The King of Fighters 2002 Magic Plus II (bootleg) | kf2k2mp2 | boot_kf2k2mp2 | — | — | — | — |  |
| The King of Fighters 2003 (bootleg, set 1) | kf2k3bl | boot_kf2k3bl | — | — | — | — |  |
| The King of Fighters 2003 (bootleg, set 2) | kf2k3bla | boot_kf2k3pl | — | — | — | — |  |
| The King of Fighters 2004 Plus / Hero (bootleg of The King of Fighters 2003) | kf2k3pl | boot_kf2k3pl | — | — | — | — |  |
| The King of Fighters 2004 Ultra Plus (bootleg of The King of Fighters 2003) | kf2k3upl | boot_kf2k3upl | — | — | — | — |  |
| The King of Fighters Special Edition 2004 (bootleg of The King of Fighters 2002) | kof2k4se | boot_kf2k4se | — | — | — | — |  |
| 2020 Super Baseball (set 1) | 2020bb | standard | — | — | — | — |  |
| 2020 Super Baseball (set 2) | 2020bba | standard | — | — | — | — |  |
| 2020 Super Baseball (set 3) | 2020bbh | standard | — | — | — | — |  |
| 3 Count Bout / Fire Suplex (NGM-043 ~ NGH-043) | 3countb | standard | — | — | — | — |  |
| Aero Fighters 2 / Sonic Wings 2 | sonicwi2 | standard | — | — | — | — |  |
| Aero Fighters 3 / Sonic Wings 3 | sonicwi3 | standard | — | — | — | — |  |
| Aggressors of Dark Kombat / Tsuukai GANGAN Koushinkyoku (ADM-008 ~ ADH-008) | aodk | standard | — | — | — | — |  |
| Alpha Mission II / ASO II - Last Guardian (NGM-007 ~ NGH-007) | alpham2 | standard | — | — | — | — |  |
| Alpha Mission II / ASO II - Last Guardian (prototype) | alpham2p | standard | — | — | — | — |  |
| Andro Dunos (NGM-049 ~ NGH-049) | androdun | standard | — | — | — | — |  |
| Art of Fighting / Ryuuko no Ken (NGM-044 ~ NGH-044) | aof | standard | — | — | — | — |  |
| Art of Fighting 2 / Ryuuko no Ken 2 (NGH-056) | aof2a | standard | — | — | — | — |  |
| Art of Fighting 2 / Ryuuko no Ken 2 (NGM-056) | aof2 | standard | — | — | — | — |  |
| Art of Fighting 3 - The Path of the Warrior (Korean release) | aof3k | standard | — | — | — | — |  |
| Art of Fighting 3 - The Path of the Warrior / Art of Fighting - Ryuuko no Ken Gaiden | aof3 | standard | — | — | — | — |  |
| Bakatonosama Mahjong Manyuuki (MOM-002 ~ MOH-002) | bakatono | standard | — | — | — | — |  |
| Bang Bang Busters | b2b | standard | — | — | — | — |  |
| Baseball Stars 2 | bstars2 | standard | — | — | — | — |  |
| Baseball Stars Professional (NGH-002) | bstarsh | standard | — | — | — | — |  |
| Baseball Stars Professional (NGM-002) | bstars | standard | — | — | — | — |  |
| Battle Flip Shot | flipshot | standard | — | — | — | — |  |
| Blazing Star | blazstar | standard | — | — | — | — |  |
| Blue's Journey / Raguy (ALH-001) | bjourneyh | standard | — | — | — | — |  |
| Blue's Journey / Raguy (ALM-001 ~ ALH-001) | bjourney | standard | — | — | — | — |  |
| Breakers | breakers | standard | — | — | — | — |  |
| Breakers Revenge | breakrev | standard | — | — | — | — |  |
| Burning Fight (NGH-018, US) | burningfh | standard | — | — | — | — |  |
| Burning Fight (NGM-018 ~ NGH-018) | burningf | standard | — | — | — | — |  |
| Burning Fight (prototype, near final, ver 23.3, 910326) | burningfpa | standard | — | — | — | — |  |
| Burning Fight (prototype, newer, V07) | burningfpb | standard | — | — | — | — |  |
| Burning Fight (prototype, older) | burningfp | standard | — | — | — | — |  |
| Captain Tomaday | ctomaday | standard | — | — | — | — |  |
| Chibi Maruko-chan: Maruko Deluxe Quiz | marukodq | standard | — | — | — | — |  |
| Choutetsu Brikin'ger / Iron Clad (prototype) | ironclad | standard | — | — | — | — |  |
| Choutetsu Brikin'ger / Iron Clad (prototype, bootleg) | ironclado | standard | — | — | — | — |  |
| Crossed Swords (ALM-002 ~ ALH-002) | crsword | standard | — | — | — | — |  |
| Crossed Swords 2 (bootleg of CD version) | crswd2bl | standard | — | — | — | — |  |
| Cyber-Lip (NGM-010) | cyberlip | standard | — | — | — | — |  |
| Digger Man (prototype) | diggerma | standard | — | — | — | — |  |
| Double Dragon (Neo-Geo) | doubledr | standard | — | — | — | — |  |
| Dragon's Heaven (development board) | dragonsh | standard | — | — | — | — |  |
| Eight Man (NGM-025 ~ NGH-025) | eightman | standard | — | — | — | — |  |
| Far East of Eden - Kabuki Klash / Tengai Makyou - Shin Den | kabukikl | standard | — | — | — | — |  |
| Fatal Fury - King of Fighters / Garou Densetsu - Shukumei no Tatakai (NGM-033 ~ NGH-033) | fatfury1 | standard | — | — | — | — |  |
| Fatal Fury 3 - Road to the Final Victory / Garou Densetsu 3 - Haruka-naru Tatakai (NGM-069 ~ NGH-069) | fatfury3 | standard | — | — | — | — |  |
| Fatal Fury Special / Garou Densetsu Special (NGM-058 ~ NGH-058, set 1) | fatfursp | standard | — | — | — | — |  |
| Fatal Fury Special / Garou Densetsu Special (NGM-058 ~ NGH-058, set 2) | fatfurspa | standard | — | — | — | — |  |
| Fight Fever / Wang Jung Wang (set 1) | fightfev | standard | — | — | — | — |  |
| Fight Fever / Wang Jung Wang (set 2) | fightfeva | standard | — | — | — | — |  |
| Fighters Swords (Korean release of Samurai Shodown III) | fswords | standard | — | — | — | — |  |
| Football Frenzy (NGM-034 ~ NGH-034) | fbfrenzy | standard | — | — | — | — |  |
| Galaxy Fight - Universal Warriors | galaxyfg | standard | — | — | — | — |  |
| Garou - Mark of the Wolves (prototype) | garoup | standard | — | — | — | — |  |
| Ghost Pilots (NGH-020, US) | gpilotsh | standard | — | — | — | — |  |
| Ghost Pilots (NGM-020 ~ NGH-020) | gpilots | standard | — | — | — | — |  |
| Ghost Pilots (prototype) | gpilotsp | standard | — | — | — | — |  |
| Ghostlop (prototype) | ghostlop | standard | — | — | — | — |  |
| GladMort (demo) | gladmort_d1 | standard | — | — | — | — |  |
| GladMort (demo²) | gladmort | standard | — | — | — | — |  |
| Goal! Goal! Goal! | goalx3 | standard | — | — | — | — |  |
| Gururin | gururin | standard | — | — | — | — |  |
| Idol Mahjong Final Romance 2 (Neo-Geo, bootleg of CD version) | froman2b | standard | — | — | — | — |  |
| Janshin Densetsu - Quest of Jongmaster | janshin | standard | — | — | — | — |  |
| Karnov's Revenge / Fighter's History Dynamite | karnovr | standard | — | — | — | — |  |
| King of the Monsters (set 1) | kotm | standard | — | — | — | — |  |
| King of the Monsters (set 2) | kotmh | standard | — | — | — | — |  |
| King of the Monsters 2 - The Next Thing (NGM-039 ~ NGH-039) | kotm2 | standard | — | — | — | — |  |
| King of the Monsters 2 - The Next Thing (older) | kotm2a | standard | — | — | — | — |  |
| King of the Monsters 2 - The Next Thing (prototype) | kotm2p | standard | — | — | — | — |  |
| Kizuna Encounter - Super Tag Battle / Fu'un Super Tag Battle | kizuna | standard | — | — | — | — |  |
| Last Hope | lasthope | standard | — | — | — | — |  |
| Last Resort | lresort | standard | — | — | — | — |  |
| Last Resort (prototype) | lresortp | standard | — | — | — | — |  |
| League Bowling (NGM-019 ~ NGH-019) | lbowling | standard | — | — | — | — |  |
| Legend of Success Joe / Ashita no Joe Densetsu | legendos | standard | — | — | — | — |  |
| Looptris | looptris | standard | — | — | — | — |  |
| Looptris Plus | looptrsp | standard | — | — | — | — |  |
| Magical Drop II | magdrop2 | standard | — | — | — | — |  |
| Magical Drop III | magdrop3 | standard | — | — | — | — |  |
| Magician Lord (NGH-005) | maglordh | standard | — | — | — | — |  |
| Magician Lord (NGM-005) | maglord | standard | — | — | — | — |  |
| Mahjong Kyo Retsuden (NGM-004 ~ NGH-004) | mahretsu | standard | — | — | — | — |  |
| Metal Slug - Super Vehicle-001 | mslug | standard | — | — | — | — |  |
| Metal Slug 2 - Super Vehicle-001/II (NGM-2410 ~ NGH-2410) | mslug2 | standard | — | — | — | — |  |
| Minasan no Okagesamadesu! Dai Sugoroku Taikai (MOM-001 ~ MOH-001) | minasan | standard | — | — | — | — |  |
| Money Puzzle Exchanger / Money Idol Exchanger | miexchng | standard | — | — | — | — |  |
| Mutation Nation (NGM-014 ~ NGH-014) | mutnat | standard | — | — | — | — |  |
| NAM-1975 (NGM-001 ~ NGH-001) | nam1975 | standard | — | — | — | — |  |
| Neo Bomberman | neobombe | standard | — | — | — | — |  |
| Neo Drift Out - New Technology | neodrift | standard | — | — | — | — |  |
| Neo Mr. Do! | neomrdo | standard | — | — | — | — |  |
| Neo Turf Masters / Big Tournament Golf | turfmast | standard | — | — | — | — |  |
| Neo-Geo Cup '98 - The Road to the Victory | neocup98 | standard | — | — | — | — |  |
| NeoBlack Tiger (demo) | nblktiger | standard | — | — | — | — |  |
| NeoTRIS | neotris | standard | — | — | — | — |  |
| Nightmare in the Dark (bootleg) | nitdbl | standard | — | — | — | — |  |
| Ninja Combat (NGH-009) | ncombath | standard | — | — | — | — |  |
| Ninja Combat (NGM-009) | ncombat | standard | — | — | — | — |  |
| Ninja Commando | ncommand | standard | — | — | — | — |  |
| Ninja Master's - Haoh-ninpo-cho | ninjamas | standard | — | — | — | — |  |
| Over Top | overtop | standard | — | — | — | — |  |
| Paewang Jeonseol / Legend of a Warrior (Korean censored Samurai Shodown IV) | samsho4k | standard | — | — | — | — |  |
| Panic Bomber | panicbom | standard | — | — | — | — |  |
| Pleasure Goal / Futsal - 5 on 5 Mini Soccer (NGM-219) | pgoal | standard | — | — | — | — |  |
| Pop 'n Bounce / Gapporin | popbounc | standard | — | — | — | — |  |
| Power Spikes II (NGM-068) | pspikes2 | standard | — | — | — | — |  |
| Pulstar | pulstar | standard | — | — | — | — |  |
| Puzzle Bobble / Bust-A-Move (Neo-Geo, bootleg) | pbobblenb | standard | — | — | — | — |  |
| Puzzle Bobble / Bust-A-Move (Neo-Geo, NGM-083) | pbobblen | standard | — | — | — | — |  |
| Puzzle Bobble 2 / Bust-A-Move Again (Neo-Geo) | pbobbl2n | standard | — | — | — | — |  |
| Puzzle De Pon! | puzzledp | standard | — | — | — | — |  |
| Puzzle De Pon! R! | puzzldpr | standard | — | — | — | — |  |
| Puzzled / Joy Joy Kid (NGM-021 ~ NGH-021) | joyjoy | standard | — | — | — | — |  |
| Quiz Daisousa Sen - The Last Count Down (NGM-023 ~ NGH-023) | quizdais | standard | — | — | — | — |  |
| Quiz King of Fighters (Korea) | quizkofk | standard | — | — | — | — |  |
| Quiz King of Fighters (SAM-080 ~ SAH-080) | quizkof | standard | — | — | — | — |  |
| Quiz Meitantei Neo & Geo - Quiz Daisousa Sen Part 2 (NGM-042 ~ NGH-042) | quizdai2 | standard | — | — | — | — |  |
| Quiz Salibtamjeong - The Last Count Down (Korean localized Quiz Daisousa Sen) | quizdaisk | standard | — | — | — | — |  |
| Ragnagard / Shin-Oh-Ken | ragnagrd | standard | — | — | — | — |  |
| Real Bout Fatal Fury / Real Bout Garou Densetsu (bug fix revision) | rbff1a | standard | — | — | — | — |  |
| Real Bout Fatal Fury / Real Bout Garou Densetsu (Korean release) | rbff1k | standard | — | — | — | — |  |
| Real Bout Fatal Fury / Real Bout Garou Densetsu (Korean release, bug fix revision) | rbff1ka | standard | — | — | — | — |  |
| Real Bout Fatal Fury / Real Bout Garou Densetsu (NGM-095 ~ NGH-095) | rbff1 | standard | — | — | — | — |  |
| Real Bout Fatal Fury 2 - The Newcomers (Korean release) | rbff2k | standard | — | — | — | — |  |
| Real Bout Fatal Fury 2 - The Newcomers / Real Bout Garou Densetsu 2 - The Newcomers (NGH-2400) | rbff2h | standard | — | — | — | — |  |
| Real Bout Fatal Fury 2 - The Newcomers / Real Bout Garou Densetsu 2 - The Newcomers (NGM-2400) | rbff2 | standard | — | — | — | — |  |
| Real Bout Fatal Fury Special / Real Bout Garou Densetsu Special | rbffspec | standard | — | — | — | — |  |
| Real Bout Fatal Fury Special / Real Bout Garou Densetsu Special (Korean release) | rbffspeck | standard | — | — | — | — |  |
| Riding Hero (NGM-006 ~ NGH-006) | ridhero | standard | — | — | — | — |  |
| Riding Hero (set 2) | ridheroh | standard | — | — | — | — |  |
| Robo Army | roboarmy | standard | — | — | — | — |  |
| Robo Army (NGM-032 ~ NGH-032) | roboarmya | standard | — | — | — | — |  |
| Samurai Shodown / Samurai Spirits (NGH-045) | samshoh | standard | — | — | — | — |  |
| Samurai Shodown / Samurai Spirits (NGM-045) | samsho | standard | — | — | — | — |  |
| Samurai Shodown II / Shin Samurai Spirits - Haohmaru Jigokuhen (NGM-063 ~ NGH-063) | samsho2 | standard | — | — | — | — |  |
| Samurai Shodown III / Samurai Spirits - Zankurou Musouken (NGH-087) | samsho3h | standard | — | — | — | — |  |
| Samurai Shodown III / Samurai Spirits - Zankurou Musouken (NGM-087) | samsho3 | standard | — | — | — | — |  |
| Samurai Shodown IV - Amakusa's Revenge / Samurai Spirits - Amakusa Kourin (NGM-222 ~ NGH-222) | samsho4 | standard | — | — | — | — |  |
| Saulabi Spirits / Jin Saulabi Tu Hon (Korean release of Samurai Shodown II, set 1) | samsho2k | standard | — | — | — | — |  |
| Saulabi Spirits / Jin Saulabi Tu Hon (Korean release of Samurai Shodown II, set 2) | samsho2ka | standard | — | — | — | — |  |
| Savage Reign / Fu'un Mokushiroku - Kakutou Sousei | savagere | standard | — | — | — | — |  |
| Sengoku / Sengoku Denshou (NGH-017, US) | sengokuh | standard | — | — | — | — |  |
| Sengoku / Sengoku Denshou (NGM-017 ~ NGH-017) | sengoku | standard | — | — | — | — |  |
| Sengoku 2 / Sengoku Denshou 2 | sengoku2 | standard | — | — | — | — |  |
| Shock Troopers (set 1) | shocktro | standard | — | — | — | — |  |
| Shock Troopers (set 2) | shocktroa | standard | — | — | — | — |  |
| Shock Troopers - 2nd Squad | shocktr2 | standard | — | — | — | — |  |
| Shougi no Tatsujin - Master of Shougi | moshougi | standard | — | — | — | — |  |
| Soccer Brawl (NGH-031) | socbrawlh | standard | — | — | — | — |  |
| Soccer Brawl (NGM-031) | socbrawl | standard | — | — | — | — |  |
| Spin Master / Miracle Adventure | spinmast | standard | — | — | — | — |  |
| Stakes Winner / Stakes Winner - GI Kinzen Seiha e no Michi | stakwin | standard | — | — | — | — |  |
| Stakes Winner / Stakes Winner - GI Kinzen Seiha e no Michi (early development board) | stakwindev | standard | — | — | — | — |  |
| Stakes Winner 2 | stakwin2 | standard | — | — | — | — |  |
| Street Hoop / Street Slam / Dunk Dream (DEM-004 ~ DEH-004) | strhoop | standard | — | — | — | — |  |
| Super Dodge Ball / Kunio no Nekketsu Toukyuu Densetsu | sdodgeb | standard | — | — | — | — |  |
| Super Sidekicks 2 - The World Championship / Tokuten Ou 2 - Real Fight Football (NGM-061 ~ NGH-061) | ssideki2 | standard | — | — | — | — |  |
| Super Sidekicks 3 - The Next Glory / Tokuten Ou 3 - Eikou e no Chousen | ssideki3 | standard | — | — | — | — |  |
| Tecmo World Soccer '96 | twsoc96 | standard | — | — | — | — |  |
| The Eye of Typhoon (alpha) | etyphoon_a | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 1) | etyphoon_b1 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 2) | etyphoon_b2 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 3) | etyphoon_b3 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 4) | etyphoon_b4 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 5) | etyphoon_b5 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 6) | etyphoon_b6 | standard | — | — | — | — |  |
| The Eye of Typhoon (Tsunami Edition, beta 7) | etyphoon | standard | — | — | — | — |  |
| The Irritating Maze / Ultra Denryu Iraira Bou | irrmaze | standard | — | — | — | — |  |
| The King of Fighters '94 (NGM-055 ~ NGH-055) | kof94 | standard | — | — | — | — |  |
| The King of Fighters '95 (NGH-084) | kof95h | standard | — | — | — | — |  |
| The King of Fighters '95 (NGM-084) | kof95 | standard | — | — | — | — |  |
| The King of Fighters '95 (NGM-084, alt board) | kof95a | standard | — | — | — | — |  |
| The King of Fighters '96 (NGH-214) | kof96h | standard | — | — | — | — |  |
| The King of Fighters '96 (NGM-214) | kof96 | standard | — | — | — | — |  |
| The King of Fighters '97 (Korean release) | kof97k | standard | — | — | — | — |  |
| The King of Fighters '97 (NGH-2320) | kof97h | standard | — | — | — | — |  |
| The King of Fighters '97 (NGM-2320) | kof97 | standard | — | — | — | — |  |
| The King of Fighters '97 Plus (bootleg) | kof97pls | standard | — | — | — | — |  |
| The King of Fighters '98 - The Slugfest / King of Fighters '98 - Dream Match Never Ends (NGH-2420) | kof98h | standard | — | — | — | — |  |
| The King of Fighters '99 - Millennium Battle (prototype) | kof99p | standard | — | — | — | — |  |
| The Last Blade / Bakumatsu Roman - Gekka no Kenshi (NGH-2340) | lastbladh | standard | — | — | — | — |  |
| The Last Blade / Bakumatsu Roman - Gekka no Kenshi (NGM-2340) | lastblad | standard | — | — | — | — |  |
| The Last Blade 2 / Bakumatsu Roman - Dai Ni Maku Gekka no Kenshi (NGM-2430 ~ NGH-2430) | lastbld2 | standard | — | — | — | — |  |
| The Last Soldier (Korean release of The Last Blade) | lastsold | standard | — | — | — | — |  |
| The Super Spy (NGM-011 ~ NGH-011) | superspy | standard | — | — | — | — |  |
| The Ultimate 11 - The SNK Football Championship / Tokuten Ou - Honoo no Libero | ssideki4 | standard | — | — | — | — |  |
| Thrash Rally (ALM-003 ~ ALH-003) | trally | standard | — | — | — | — |  |
| Top Hunter - Roddy & Cathy (NGH-046) | tophuntrh | standard | — | — | — | — |  |
| Top Hunter - Roddy & Cathy (NGM-046) | tophuntr | standard | — | — | — | — |  |
| Top Player's Golf (NGM-003 ~ NGH-003) | tpgolf | standard | — | — | — | — |  |
| Treasures of The Caribbean | totc | standard | — | — | — | — |  |
| Twinkle Star Sprites | twinspri | standard | — | — | — | — |  |
| Viewpoint | viewpoin | standard | — | — | — | — |  |
| Viewpoint (prototype) | viewpoinp | standard | — | — | — | — |  |
| Voltage Fighter - Gowcaizer / Choujin Gakuen Gowcaizer | gowcaizr | standard | — | — | — | — |  |
| Waku Waku 7 | wakuwak7 | standard | — | — | — | — |  |
| Windjammers / Flying Power Disc | wjammers | standard | — | — | — | — |  |
| World Heroes (ALH-005) | wh1h | standard | — | — | — | — |  |
| World Heroes (ALM-005) | wh1 | standard | — | — | — | — |  |
| World Heroes (set 3) | wh1ha | standard | — | — | — | — |  |
| World Heroes 2 (ALH-006) | wh2h | standard | — | — | — | — |  |
| World Heroes 2 (ALM-006 ~ ALH-006) | wh2 | standard | — | — | — | — |  |
| World Heroes 2 Jet (ADM-007 ~ ADH-007) | wh2j | standard | — | — | — | — |  |
| World Heroes Perfect | whp | standard | — | — | — | — |  |
| Zed Blade / Operation Ragnarok | zedblade | standard | — | — | — | — |  |
| Zintrick / Oshidashi Zentrix (bootleg of CD version) | zintrckb | standard | — | — | — | — |  |
