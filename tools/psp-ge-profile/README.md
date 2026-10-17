# psp-ge-profile

A plugin for a real PSP, and a decoder for the Mac, that record how a commercial game drives the GE (the PSP's
graphics chip) and the display, frame by frame: every display list the game queues, when the GE finishes it, how
long the CPU waits for it, and when the game flips its frame buffers. It's the ground truth for Phobos's GE timing
model, which was calibrated from synthetic lists (`tools/psp-measure`, round 6; docs/psp-core.md, parts 60 and 64;
`ares/psp/ge/draw.cpp`) and has never been checked against real games.

The question that started it: in Phobos, God of War: Chains of Olympus draws its 3D title scene two or three times
per vblank (`draw.cpp`'s fast mode charges mesh pixels too). If a real PSP draws it once, Phobos's GE time is too
short, and fixing it would be both more accurate and two or three times less GPU work at every resolution. The same
scenes are captured here as Phobos measures from save states, so the two compare directly.

- `plugin/`: the plugin, a kernel module (PRX) for ARK-4's plugin loader. It records the game's calls to the GE's and
  the display's functions, with microsecond times, into kernel memory, and writes them to the memory stick after the
  capture. It shows short notices on screen (that it's on, that a capture is coming, that it was saved or why not;
  only a small blinking square while it counts) and keeps a trace of every step in `PHOBOS/trace.txt`.
- `decoder/geprof.py`: reads a capture and prints, per vblank and per second, what the GE and the CPU did.

## The owner's steps

These are for a PSP running ARK-4 on firmware 6.61. The first run is a dry run: the plugin installs its hooks and
only counts calls, then writes a short text report, so it shows the plugin loads and hooks without recording
anything. Captures come after that report has been looked at. From version 0.2.0 the plugin says on screen what it's
doing (see "What the notices mean", below), so a run that writes nothing still shows how far it got, and
`PHOBOS/trace.txt` says the rest.

### What goes where

| File | Where it goes on the memory stick |
|---|---|
| `phobosprof.prx` | `SEPLUGINS/phobosprof.prx` |
| `prof.txt` | `PHOBOS/prof.txt` (make the `PHOBOS` folder at the stick's top level) |
| `prof-capture.txt`, `prof-boot.txt` | `PHOBOS/` too, for later: the plugin reads only `prof.txt` |

On a PSP Go without a memory stick card, the same folders go on the internal storage (`ef0:`), and the paths in
`PLUGINS.TXT` start with `ef0:` instead of `ms0:`.

The plugin writes only inside `PHOBOS` (its reports and captures go in `PHOBOS/PROF`, which it makes, and its trace
in `PHOBOS/trace.txt`, which it adds to and never shortens), never renames or deletes anything, and opens no file
while a capture runs. If there's no `PHOBOS` folder, it writes nothing at all (and says so on screen).

### Before the first run

1. In ARK's settings (the XMB's CFW settings, or the recovery menu), set the CPU clock to the default: not
   Overclock, not Power Save, not Balanced. The games set their own clocks, and those are part of what's measured.
   ARK-4's own settings file, `SEPLUGINS/SETTINGS.TXT`, comes with the line `always, overclock, on`; if yours has
   it, make it `always, overclock, off`. With it on, the PSP's PLL runs at 333 MHz, and a game that asks for more
   than its 222/111 is given it, where a PSP without it would refuse: the first Activision Hits Remixed captures
   ran at 333/166 that way (docs/psp-core.md, part 104).
2. Turn off any other plugin for these games (cheats, frame counters, overclockers) while profiling: a plugin that
   hooks the same functions would hide them from this one.
3. Don't save the game while the plugin is on. (The plugin doesn't touch saves, but it writes to the same stick,
   and a crash during a save is the one way a save could suffer.) Load a save if you need to; just don't save.

### Turning it on for the games

Add these lines to ARK's `PLUGINS.TXT`. ARK reads it from `SEPLUGINS/PLUGINS.TXT` or from
`PSP/SAVEDATA/ARK_01234/PLUGINS.TXT`; if one of those already exists, add the lines to it, otherwise make
`SEPLUGINS/PLUGINS.TXT`. Each line turns the plugin on for one game only, by the game's ID, one ID to a line: the
copies Phobos's scenes were made from, and for Chains of Olympus the other two copies you have saves of. Yours is
the Asian disc, UCAS40198 (as an ISO); the plugin loaded for it only from a line of its own, where a line with all
three IDs didn't load it (ARK-4's source reads several IDs on a line, but one to a line is what worked).

```
UCAS40198, ms0:/SEPLUGINS/phobosprof.prx, on
UCUS98653, ms0:/SEPLUGINS/phobosprof.prx, on
NPUG80325, ms0:/SEPLUGINS/phobosprof.prx, on
UCUS98737, ms0:/SEPLUGINS/phobosprof.prx, on
ULUS10186, ms0:/SEPLUGINS/phobosprof.prx, on
UCUS98646, ms0:/SEPLUGINS/phobosprof.prx, on
ULES00364, ms0:/SEPLUGINS/phobosprof.prx, on
ULUS10472, ms0:/SEPLUGINS/phobosprof.prx, on
NPUG80221, ms0:/SEPLUGINS/phobosprof.prx, on
```

In order: God of War: Chains of Olympus (three copies), God of War: Ghost of Sparta, Activision Hits Remixed,
Killzone: Liberation, Super Monkey Ball Adventure, Army of Two: The 40th Day, Super Stardust Portable (the
PlayStation Store copy Phobos has; the last line is new since the first captures). If no notice at all appears when
one of them starts, the plugin wasn't loaded for it: its ID isn't on a line. The ID is on the game's info screen in
ARK's launcher ("View info"); add a line for it. The other regions' IDs:

| Game | Other IDs |
|---|---|
| Chains of Olympus | UCES00842 (Europe), ULJM05348 (Japan), UCKS45084 (Korea) |
| Ghost of Sparta | UCES01401, UCES01473 (Europe), UCJS10114 (Japan), UCAS40323 (Asia), UCKS45161 (Korea) |
| Activision Hits Remixed | ULES00640 (Europe) |
| Killzone: Liberation | UCES00279 (Europe), UCKS45041 (Korea), UCUS80170 and NPEG00007 (PlayStation Store) |
| Super Monkey Ball Adventure | ULUS10132 (North America) |
| Army of Two: The 40th Day | ULES01381 (Europe), ULJM05643, ULJM05849 (Japan) |

### The dry run (first)

1. Start Chains of Olympus (any of them would do) and let it run: on its menus is fine. As soon as the game shows
   its first picture, a black box at the top left says, for about 4 seconds, `PHOBOS PROFILER ON` and, under it,
   your copy's ID and `DRY RUN`. A third line, if there is one, is the first thing in the way (see below). (Version
   0.2.0 showed the box flickering during the boot, before the game's first picture, and `UNKNOWN` for the ID: from
   0.2.2 it waits for the game's first frame, and asks for the ID again then.)
2. From 20 seconds after the game starts, a small square blinks at the top right for 10 seconds while the plugin
   counts the calls; then the box says `SAVED` and `UCUS98653-1.TXT` (or your copy's ID), or `NOT SAVED` and why.
3. Then try the buttons once: on a menu, hold L, R and SELECT together for a second or two. The box says
   `DRY RUN IN 1 S`; let go, and a second later the square blinks for 10 seconds, then `SAVED` and `...-2.TXT`.
4. Quit the game and send back `PHOBOS/trace.txt` and the `.txt` files in `PHOBOS/PROF`, and say what appeared on
   screen and when (a photo of a notice helps). The reports say whether every hook went in, how often the game called
   each function, how much memory a capture can have, and what the hooks cost on your PSP; the trace says each step
   the plugin took, from its start.

If nothing appears on screen at all, the plugin wasn't loaded for that game, or stopped before it could show
anything: check its ID (above), then run the load test (below). If the box appears but nothing is saved, the box and
`trace.txt` say why.

### What the notices mean

| On screen | What it means |
|---|---|
| `PHOBOS PROFILER ON`, then `<ID> DRY RUN` or `<ID> CAPTURE` | The plugin is loaded for this game, in this mode. |
| `NO PHOBOS FOLDER YET` (third line) | There was no `PHOBOS` folder on the stick when the game started. The plugin looks again before each run and each file, and shows the box again if it turns up. |
| `n HOOKS MISSING: SEE TRACE` | Some of the firmware's functions weren't found; `trace.txt` says which. |
| `NO HOOKS: SEE TRACE.TXT` | ARK's function for hooking wasn't found, so nothing can be counted or recorded. |
| `DRY RUN IN 1 S` or `CAPTURE IN 1 S` | The buttons were recognised; the run starts a second after you let go. |
| A small square blinking at the top right | Counting (dry run) or recording (capture). Nothing else is drawn meanwhile. |
| `SAVED`, the file's name (a capture: its records, or `BUFFER FILLED`) | The file was written. |
| `NOT SAVED`, then a reason | `NO PHOBOS FOLDER`, `CANNOT MAKE PHOBOS/PROF`, `NO FREE FILE NUMBER`, `CANNOT CREATE THE FILE`, `WRITE FAILED`, or `NOT ENOUGH MEMORY` (then `SEE <ID>-<n>.TXT`, which says how much memory there was). |

`notices = off` in `prof.txt` turns them all off.

### If nothing appears on screen: the load test

`hello/` builds two modules whose `module_start` only appends a line to `PHOBOS/hello.txt`: `phoboshello.prx`, a
kernel module built the way `phobosprof.prx` is (the same flags, module attributes and exports; pspsdk's libraries
only, nothing from ARK), and `phoboshellou.prx`, the same as a user module. For one session, `PLUGINS.TXT` holds just
these three lines (keep the old file as `PLUGINS.OLD` and put it back afterwards):

```
psp, ms0:/SEPLUGINS/phoboshello.prx, on
psp, ms0:/SEPLUGINS/phoboshellou.prx, on
psp, ms0:/SEPLUGINS/phobosprof.prx, on
```

ARK matches `psp` for every PSP game, from a UMD, an ISO or the PlayStation Store, so only the game being tested is
started that session. ARK loads the three in this order. Afterwards:

| On the stick | What it means |
|---|---|
| No `hello.txt` line from that boot | ARK started none of the three: it isn't loading plugins for that boot (START held, plugins off, another `PLUGINS.TXT` read), or the game isn't in its `psp` runlevel (homebrew is `game` or `homebrew`). |
| A `phoboshellou` line only | ARK starts user modules from `SEPLUGINS` there, but not kernel ones. |
| A `phoboshello` line, but no line of 0.2.1's (or a later version's) in `trace.txt` | ARK loads and starts kernel modules there, but `phobosprof.prx` doesn't start: the difference is the plugin itself. The line's kernel memory figures say whether its 53 KB could fit. |
| A `phoboshello` line and 0.2.1's (or a later version's) lines in `trace.txt` | The plugin starts, and `trace.txt` says how far it got. The `phoboshello` line's title ID is the ID ARK compares `PLUGINS.TXT`'s game IDs with: if it isn't on the plugin's game line, that's why that line didn't load it. |

The kernel module's line reads `phoboshello loaded: kernel module, time <microseconds since power-on> us, api type
<0x120 a UMD, 0x123 to 0x126 an ISO or a PlayStation Store game, 0x141 or 0x152 homebrew>, boot file <the game's
file>, title id <the game's ID>, kernel memory free <bytes> in one block, <bytes> in all`; the user module's, just its
name and the time.

### Captures (when the dry run's report has been looked at)

1. Copy `prof-capture.txt` over `PHOBOS/prof.txt` (keep the name `prof.txt`).
2. For each scene below, get to it, wait about five seconds for it to settle, then hold L + R + SELECT until the box
   says `CAPTURE IN 1 S`, and let go. The capture starts a second later and runs 10 seconds (the square blinks at
   the top right); keep doing what the scene says during it. When it ends, the memory stick light flashes while the
   file is written, then the box says `SAVED` and the file's name: wait for that before the next capture.
3. Each capture is `PHOBOS/PROF/<ID>-<n>.bin`, numbered from 1 per game. If a capture couldn't run (not enough
   memory, say), a `.txt` with that number says why instead. (On the owner's PSP-1000 a game leaves some 560 KB of
   kernel memory free: with `keep = 512`, 0.2.2's and before, the first captures were refused that way; from 0.2.3
   `keep` is 256, which leaves some 300 KB for the records, where ten seconds of these games took 50 to 130 KB.)
4. For Activision Hits Remixed's copyright screen, which comes as the game boots, copy `prof-boot.txt` over
   `prof.txt` instead, then start the game and leave it: the capture runs for the game's first 12 seconds. Copy
   `prof-capture.txt` back afterwards.
5. Two or three captures of each scene are better than one, in separate visits if you can.
6. Two scenes added after the first captures, a minute each, with `prof-capture.txt` as in step 2 (the table's last
   two rows): Killzone's language menu and, if you have the game, Super Stardust Portable's intro. Phobos's earlier
   model of the GE's time (part 76) ran the first at about 15 frames a second and the second at 30; these say what a
   PSP does.

### Copying the files back

Connect the PSP to the computer by USB (USB connection in the XMB's settings) and copy the `PHOBOS/PROF` folder and
`PHOBOS/trace.txt`. Delete them from the stick afterwards if you like (the plugin only adds new numbers and new trace
lines, it never overwrites).

### Switching it off

- For good: change `on` to `off` at the end of the plugin's lines in `PLUGINS.TXT` (or delete them). ARK's plugin
  manager (in the XMB, or the recovery menu) can turn them off too. The `PHOBOS` folder can then be deleted.
- Only the notices: `notices = off` in `prof.txt`.
- For one game boot: hold START while the game starts (ARK's vanilla mode starts it without plugins, that once).
- If a game won't start with the plugin on: hold START when starting it, as above; if the PSP itself won't start,
  hold R while switching it on (or while running ARK Loader, for a live install) to get ARK's recovery menu, and
  turn the plugin's lines off there.

## The scenes to capture

The six scenes Phobos measures from save states (`~/phobos-work/scratch/cpu-round3/scenes` and
`~/phobos-work/scratch/psp-hires/scenes`), and two more where part 76's model of the GE's time changed what Phobos
draws (step 6 above). Each is captured with nothing pressed unless it says otherwise, as Phobos runs them.

| Scene | Game (the copy Phobos used) | How to get there | During the capture |
|---|---|---|---|
| coo-diff | Chains of Olympus (UCUS98653) | Start the game, let the logos pass, press START at the title, then X on New Game: the "Select your difficulty" menu (Mortal, Hero, Spartan, God) over Kratos' face in 3D. | Nothing: leave Hero (Normal) selected. This is the one Phobos draws two or three times a vblank. |
| gos-menu | Ghost of Sparta (UCUS98737) | Start the game, let the logos pass: the main menu (New Game, Load Game, Options, Treasures) over Kratos and the stormy sea. | Nothing. |
| smba-story | Super Monkey Ball Adventure (ULES00364) | From the title, choose Story Mode: the Story Mode screen (the "NEW GAME" panel over the jungle island). | Nothing. |
| kz-gp | Killzone: Liberation (UCUS98646) | Gameplay in the first level, at the spot where you saved Phobos's Killzone state on the RP6 (an open yard: concrete, a metal staircase, barrels, your squad mate beside you), with no fight going on. | Stand still. A second capture while walking around is a bonus. |
| a2-gp | Army of Two: The 40th Day (ULUS10472) | Campaign, then the first chapter (Shanghai Streets), once you have control. | Walk forward, then sideways, and fire now and then, as Phobos's scene does (its script moves the stick and fires for about 25 seconds). A second capture standing still is a bonus. |
| ahr-legal | Activision Hits Remixed (ULUS10186) | The copyright screen (black text on white) that opens the game, before the Activision logo: frames 120 to 480 after boot in Phobos, so seconds 2 to 8. | Use `prof-boot.txt` (above): start the game and press nothing until the logo. |
| kz-lang | Killzone: Liberation (UCUS98646) | The language menu that comes as the game starts, about 4 seconds in (Phobos's `kz-lang`): a list of languages over a full-screen picture. If your copy goes past it (a save holding the language, say), it can't be captured: say so. | Nothing: leave the first language selected. |
| ssp-intro | Super Stardust Portable (NPUG80221, or your copy's ID) | The intro that plays as the game starts, before its title screen (its glowing effects drawn in several passes). | Nothing: start the capture as soon as the intro begins, and let it play. |

Phobos's pictures of five of these scenes are in the coordinator's scratch folders, to send along:
`speed9/sheets/scene/{coo-diff,gos-menu,smba-story}/frame-000030.png`, `scenes-owner/killzone-gameplay-owner.png`
and `psp-hires/tmp/ahr-boot/frame-000300.png` (Army of Two's chapter is in `speed9/sheets/a2.png`, frames 3000 to
3480).

## How it works

### Loading

ARK loads `phobosprof.prx` into kernel memory as one of the listed games boots. Its `module_start` finds the
functions it needs, patches the hooks in (they pass straight through until a capture), starts its control thread
and returns. The module can't be stopped (`PSP_MODULE_NO_STOP`), since a game thread may
be asleep inside one of its hooks at any time.

The only function imported from ARK at load time is `sctrlHENFindFunction` (SystemControl's `SystemCtrlForKernel`,
NID 0x159AF5CC, the SHA-1 of its name, which is how ARK exports), through the plugin's own stub
(`plugin/systemctrl.S`), so the plugin doesn't link ARK's or PRO's GPL code. Everything else is found at run time
through it: ARK's `sctrlHENPatchSyscall` (three NIDs tried), `sctrlKernelQuerySystemCall`, `sctrlGetInitPARAM`,
`sctrlFlushCache`; the firmware's kernel memory functions (`SysMemForKernel`), its sub-interrupt monitor
(`InterruptManagerForKernel`), the controller, the clocks, the vblank count and the frame buffer. A lookup's answer is
taken only if it's plausibly a function: firmware functions live in kernel memory, from 0x88000000, while 0 and a PSP
error code (0x8002xxxx) don't, so a lookup that fails either way leaves its feature off instead of being called. A
function that isn't taken leaves that one feature off, and `trace.txt` and the dry run's report name it; the plugin
still loads. The statically linked firmware functions (threads, files, interrupts) come from pspsdk's
`libpspkernel`, with NIDs that uOFW's 6.60 export lists and pspsdk's own 6.60 libraries agree on.

### The hooks

| What | How it's installed | What's recorded |
|---|---|---|
| `sceGeListEnQueue`, `sceGeListEnQueueHead` | ARK's `sctrlHENPatchSyscall` on the function `sctrlHENFindFunction("sceGE_Manager", "sceGe_user", NID)` returns | list, stall address, callback id; the list's id |
| `sceGeListUpdateStallAddr`, `sceGeListDeQueue` | the same | list id, stall address; the result |
| `sceGeListSync`, `sceGeDrawSync` | the same | list id, mode; the list's state |
| `sceGeBreak`, `sceGeContinue`, `sceGeSetCallback`, `sceGeUnsetCallback` | the same | mode, the callback data's handlers; the result (the callback id) |
| `sceDisplaySetFrameBuf` | the same, in `sceDisplay_Service`'s `sceDisplay` | frame buffer address, width, pixel format, sync mode |
| `sceDisplayWaitVblankStart`, `...WaitVblank`, their `CB` forms, `...WaitVblankStartMulti` and its `CB` form | the same | the vblank count (Multi) |
| The GE's SIGNAL and FINISH callbacks | the interrupt manager's sub-interrupt monitor (`RegisterSubIntrruptMonitor`), registered only while a capture runs | callback id, the command's low 16 bits, the list address after it (and a SIGNAL's whole command), when the game's handler started and returned |
| Vertical blanks | the same monitor (the controller driver's handler runs at every vblank while the game reads the pad at vblanks, the default) | each vblank's time and count |

Every call record has the call's entry and return times (`sceKernelGetSystemTimeLow`, microseconds), the thread
that called (an index into a table of names written with the capture), the vblank count, and the GE's state at
entry: the firmware's own `sceGeDrawSync(1)` answer (idle, drawing, or stalled at the stall address), and when a list
is drawing or stalled, the address of the GE's next command. The GE's registers are read only then, when the
firmware's call has just read the same register: the GE's bus clock is off while its queue is empty, and the
firmware never reads it blind either.

Patching the syscalls catches the game's calls (and any other user-mode caller, such as a system dialog's) without
changing the game; the firmware's functions run as before, with the game's arguments and privileges (k1). The GE's
callbacks aren't wrapped or moved: the firmware's own monitor slot, which `sceKernelCallSubIntrHandler` calls just
before and just after each sub-interrupt handler, sees them (uOFW, `interruptman.c`); the GE driver calls the FINISH
callback before it starts the next list, so the callback's return is when the next list could start.

What a hook costs, by instruction count (the dry run measures it on the PSP itself and reports it):

- Idle (no capture): six instructions, a load, a branch and a jump straight into the firmware's function, with no
  stack frame: tens of nanoseconds.
- Capturing: about 170 instructions of the plugin's own and about as many in the firmware's helpers (the time twice,
  the GE's state, the vblank count, the thread id, interrupts off and on four times), roughly 2 microseconds at
  222 MHz. A game making 50 of these calls a frame spends about 0.1 ms of its 16.7 ms on them.
- The monitor, while capturing: about 1 to 2 microseconds per GE callback and per vblank, with interrupts off (as
  the handlers themselves run). While not capturing, it isn't registered at all.

### Capture control

- The control thread waits at priority 0x18, waking every 50 ms to read the buttons (`sceCtrlPeekBufferPositive`
  from kernel mode, which reads the controller driver's kernel-side copy, apart from the game's, and consumes
  nothing). Holding the combination (default L + R + SELECT) for a second triggers;
  the capture starts `delay` seconds (default 1) after the buttons are let go, so the press isn't in it.
- `start = N` in `prof.txt` also starts one N seconds after the plugin started (the game's boot), once per boot; it's
  skipped if another capture was running then.
- The memory stick's `PHOBOS` folder is looked for at the start and again before every run, every file and every
  write of the trace (a stick not ready at the game's boot is found later); `prof.txt` is read the first time the
  folder is there.
- A capture's events go to a buffer in kernel memory (partition 1), taken when the capture starts and given back
  after its file is written: as much as `buffer` KB (default 2048), leaving `keep` KB (default 256) of the
  partition's largest free block for the game and the firmware. If that leaves under 64 KB, there's no capture, and a
  `.txt` says how much memory there was. Records are 32 bytes. Phobos counts at most about 17 GE submissions a frame
  in these six scenes; with their callbacks, syncs, waits, flips and vblanks that's perhaps 3,000 records a second,
  so 1 MB holds about 11 seconds (the dry run counts the real number for each game). When the buffer fills,
  recording stops cleanly: the capture ends there, the records that didn't fit are counted, and the file's header
  says so.
- While capturing, the thread wakes every 250 ms only to see whether the time is up or the buffer full. Then it
  stops the hooks (with interrupts off), lowers itself to priority 0x7E, next to the lowest there is, and writes the
  file in 32 KB pieces with pauses between them, so the game's own reads of the stick get turns. A call that returns
  after its capture ended (a thread asleep in a vblank wait) writes nothing: each record carries its capture's
  generation.
- Dry mode (`mode = dry`, the default) counts calls instead of recording them, for the same window, and writes a
  report: each hook's state and count, the records a second a capture would need, the hooks' cost measured on the
  PSP (half a millisecond with interrupts off, after the window), the memory partitions' sizes and free space at the
  game's start and after the window, the clocks, the model, the firmware and ARK versions, which firmware
  functions were found, and when the first flip came through the `sceDisplaySetFrameBuf` hook.

### On-screen notices

The notices (`plugin/notice.c`) are a few lines in a black box at the screen's top left (yellow first line, white
after), drawn into the frame buffer the game hands `sceDisplaySetFrameBuf`, once the firmware has accepted it and
after the call's record is closed, in the game's pixel format (5650, 5551, 4444 or 8888), through the uncached
address so the display sees the writes at once. The font is the plugin's own: 5x7 glyphs drawn twice their size. A
notice's time starts when it's first drawn, so one set before the game's first picture is still seen. Nothing is drawn
unless the buffer is plausible: 64 to 1024 pixels wide, a known format, aligned, with all of the screen's 272 rows
inside VRAM or inside the user RAM every model has.

- While a capture or a dry run counts, only an 8x8 square blinks at the top right: 64 pixel writes a flip, under
  about 6 microseconds at 222 MHz (0.04% of a frame), after the flip's timestamp, so no recorded time includes them.
  The countdown is cleared as the window starts.
- The box itself (some 300x50 pixels) costs perhaps half a millisecond to a millisecond a flip, for the few seconds it
  shows, never during a window.
- If the hook sees no flips for 200 ms (it isn't in place, or the game doesn't flip), the control thread draws the
  text into the buffer `sceDisplayGetFrameBuf` reports instead, every 10 ms while there's text to show: it flickers,
  but it shows. It never draws during a window.
- The start notice waits for the game's first flip through the hook (on the owner's PSP, Chains of Olympus's came
  4.35 s after the plugin started): the plugin starts as the game boots, before there's anything on the screen, and
  0.2.0's box, shown at once, was drawn the other way through the boot, flickering, its 4 seconds over before the
  game's first frame. With the hook not in place, or no flip through it 6 seconds after the start, it's shown then,
  and drawn the other way. At the first flip the disc ID is asked for again if the start didn't get it: ARK reads it
  from the game's `PARAM.SFO` on its disc, which isn't readable yet as the game boots (0.2.0's trace had `UNKNOWN`
  there; its files were named right, from `PARAM.SFO` read after the window).

### The trace

`plugin/trace.c` keeps a line per step, each starting with the seconds since the module started: the module's start;
each firmware function found (with its address), refused (with what the lookup gave) or not found; each hook
(found, patched, its syscall number); the monitor; the control thread made; the device found or not; the settings;
the disc ID; the first flip through the hook; each trigger; each file written or not and why. The lines are kept in
memory (6 KB) and appended to `PHOBOS/trace.txt`, never truncated. `module_start` writes them itself (in the module
manager's thread, at the game's boot, before the game runs): first thing, so the file shows the module started even
if nothing after that works, then once the functions and hooks are found, and again if the control thread can't
run. From then on the control thread writes them at the writing priority: when it starts (before it asks ARK for the
game's `PARAM.SFO`, which may mean reading a disc not yet ready, and again after), 6 seconds after the start, and
after every run, never while a capture or a dry run's window is on. Lines from before the `PHOBOS` folder is found
wait until it is.

### Settings (`PHOBOS/prof.txt`)

`name = value` lines; `#` starts a comment. Values outside a setting's range are held to it.

| Setting | Default | |
|---|---|---|
| `mode` | `dry` | `dry` or `capture` |
| `buttons` | `L+R+SELECT` | names joined by `+`: SELECT START UP RIGHT DOWN LEFT L R TRIANGLE CIRCLE CROSS SQUARE NOTE SCREEN VOLUP VOLDOWN |
| `delay` | 1 | seconds from letting go to the start (0-30) |
| `start` | 20 | seconds after the game's start for a timed run, or `off` (0-3600) |
| `length` | 10 | seconds (1-60) |
| `buffer` | 2048 | KB of kernel memory at most (64-16384) |
| `keep` | 256 | KB left free beside it (0-8192) |
| `partition` | 1 | the memory partition (1-12); leave it at 1 unless the dry run's report suggests otherwise |
| `notices` | `on` | `on` or `off`: the on-screen notices |

## The capture file

`PHOBOS/PROF/<disc id>-<n>.bin`, little-endian, laid out in `plugin/profile.h` (the decoder checks every size):

- A 256-byte header: `PHGEPROF`, the format (1), the header's and a record's sizes, the record count, flags (1: the
  buffer filled; 2: the monitor was on; 4: started by time, not buttons; 8: GE states sampled), records dropped, the
  plugin's version, the disc ID and title (from ARK's `sctrlGetInitPARAM`, else the disc's `PARAM.SFO`), the
  capture's start and end times and vblank counts, the CPU and bus clocks at its start and end, the firmware version,
  ARK's version, the model, which hooks were in place (bit n for record kind n, bit 31 the monitor), the capture's
  number, the plugin's start time, the buffer's capacity, the thread table's offset and size, and the memory
  partition and bytes used.
- The records, 32 bytes each: kind, thread index, vblank count (low 16 bits), entry time, return time (0 if the
  call was still running at the end), the GE's state word, three arguments and the result. The kinds and their
  arguments are listed in `profile.h` (and as names in `geprof.py`).
- The thread table: 36 bytes per thread, its UID and name (index 0 is interrupt context).

## The decoder

```
tools/psp-ge-profile/decoder/geprof.py PHOBOS/PROF/UCUS98653-1.bin [--lists] [--events] [--no-vblanks] [--from S] [--to S] [--json FILE]
```

Python 3, standard library only. It prints the capture's header, then:

- Per second: vblanks; displayed frames (vblanks after a flip, so showing a new frame); renders (flips: frame
  buffer changes); renders per displayed frame; lists queued; the GE's milliseconds per displayed frame, in lists and
  in their tails; the GE's busy share; the CPU's milliseconds blocked in GE syncs and in vblank waits.
- Per vblank: lists queued, FINISH and SIGNAL callbacks, stall updates (and how many found the GE waiting at its
  stall address), the GE's time in lists and its share of the vblank, the tail time, the CPU's sync and vblank-wait
  time, flips, and the frame buffer shown.
- With `--lists`, each list: queued, start, end, GE time, tail, stall updates, callback id, thread, and what ended it.
  With `--events`, every record. `--json` writes the tables for a program (phase 2's comparison).

How it reads the records:

- Vblanks come from the monitor's vblank records (gaps filled from the vblank counts); without those, from the
  returns of vblank waits, fitted to the LCD's period (1001/60000 s); failing both, from the period alone.
- A list's GE time runs from when the GE could begin it (when it was queued, or when the previous list's FINISH
  callback returned, whichever is later) to its FINISH callback. A FINISH is matched to the queued list whose
  commands it ends (its list address lies between the list's start and its last stall address), else to the head of
  the queue. A list without a callback ends at the sync that waited for it. FINISHes that match no list seen, before
  the GE has been seen idle, are counted as lists queued before the capture.
- A list the game builds while the GE runs it (queued with a stall address, then extended with
  `sceGeListUpdateStallAddr`) spends part of its span waiting for the CPU. Its tail is the GE time after its last
  stall update, when the whole list was there: pure GE work, a lower bound for the list's.
- A flip is a `sceDisplaySetFrameBuf` to another buffer than the call before; a vblank shows a new frame if the
  vblank before it had a flip. (A game that renders twice a vblank into two buffers ends each vblank on the same
  buffer, but shows a new image every vblank; a game that draws into one buffer and never flips shows no frames
  here.)

`decoder/test_geprof.py` checks all this on synthetic captures written with `geprof.write_capture`
(`python3 -m unittest test_geprof` in `decoder/`), among them a game rendering once a vblank, three times a vblank,
and at 30 frames a second, stall-driven lists, lists without callbacks, captures without vblank records, the timer
wrapping, files cut short and lists left running from before the capture; and, once `make hosttest` has written it,
the capture the plugin's own record path makes.

## Building

`make` in `plugin/`, with pspdev's toolchain on the PATH (`psp-config`), gives `phobosprof.prx`. On the
coordinator's Mac, pspdev's macOS release is in `~/phobos-work/tools/pspdev`; it links Homebrew's gmp, mpfr, mpc and
zstd, which were built into `~/phobos-work/tools/deps` instead (`~/phobos-work/tools/deps-src/build-deps.sh`, then
`relink.sh` to point the toolchain's binaries at them), so nothing is installed globally:

```
export PSPDEV=~/phobos-work/tools/pspdev PATH=~/phobos-work/tools/pspdev/bin:$PATH
make -C tools/psp-ge-profile/plugin
```

The build is warning-free; `psp-fixup-imports` and `psp-prxgen` pass. Checks worth repeating after a change: the
code uses no `$gp` (`psp-objdump -d phobosprof.elf | grep -w gp` finds nothing: the hooks run with the game's or
the firmware's `$gp`), and the imports are only `SystemCtrlForKernel` (one), `IoFileMgrForKernel`,
`InterruptManagerForKernel` and `ThreadManForKernel`.

`make` in `hello/` gives the load test's `phoboshello.prx` and `phoboshellou.prx` the same way, and `make hosttest`
there runs `hello.c` on the Mac as both (the line in each case; `ms0:` or else `ef0:`; nothing written without a
`PHOBOS` folder; a missing function's error code or a bad pointer never read as text).

`make hosttest` (in `plugin/`) builds the plugin's C (`main.c`, `notice.c`, `trace.c`, `hooks.c`, `text.c`) for the
Mac with the firmware faked (`hosttest.c`), the memory stick and the disc being folders in a temporary one, VRAM and a
stretch of user RAM being arrays, and plays a scripted game whenever the control thread sleeps: lists built while the
GE runs them, syncs during which the FINISH interrupt arrives, a SIGNAL, flips, vblank waits during which the vblank
interrupt arrives, across the 32-bit timer's wrap. It checks the module's start, the functions it takes and one it
refuses (a lookup giving an error code), the hooks it installs; that idle hooks record nothing; every record of a
capture; that a thread asleep in a wait when its capture ends writes nothing; that a full buffer drops and counts; the
notices, drawn in each pixel format with the exact colors, never into an unsafe buffer (none, outside VRAM and user
RAM, running past VRAM's end, an unknown format or width, misaligned), timed from their first drawing, the 64-pixel
marker alone during a window, drawn through `sceDisplayGetFrameBuf` when the hook sees no flips, and not at all when
off; the `PHOBOS` folder turning up after the start (the start notice says so; the next run finds it, reads the
settings and writes the trace kept since the module's start); the settings file's parsing; the dry run's report line
by line; the capture file `main.c` writes; the note and notice when memory is short; the disc ID from `PARAM.SFO` when
ARK can't give it; every expected line of `trace.txt`, in order; and all along that no file is touched while a
capture or a dry run's window is on, that files are written only at the writing priority and never truncated, and
that nothing is written outside `ms0:/PHOBOS/`. The decoder's tests then read the capture back.

## Files

| File | What it is |
|---|---|
| `plugin/main.c` | The module's start, the settings, the control thread (trigger, capture, dry run, notices) and the files. |
| `plugin/hooks.c` | The hooks, the sub-interrupt monitor, the record buffer and the cost measure. |
| `plugin/notice.c` | The on-screen notices: the font, the frame buffer's formats and checks. |
| `plugin/trace.c` | The trace, kept in memory and appended to `PHOBOS/trace.txt`. |
| `plugin/plugin.h` | What those share: the firmware's and ARK's functions found at run time, with their NIDs. |
| `plugin/profile.h` | The capture file's layout. |
| `plugin/text.c` | A small formatter, and the memory functions the compiler may call (no kernel libc is linked). |
| `plugin/systemctrl.S` | The one import stub for ARK (`sctrlHENFindFunction`). |
| `plugin/exports.exp`, `plugin/Makefile` | The module's exports and its build (and `make hosttest`). |
| `plugin/hosttest.c` | The plugin on the Mac: its C with the firmware faked, driven by a scripted game. |
| `plugin/prof.txt`, `prof-capture.txt`, `prof-boot.txt` | Settings: the dry run, captures by buttons, the capture from boot. |
| `hello/hello.c` | The load test: a line in `PHOBOS/hello.txt` from `module_start`, as a kernel module and as a user one. |
| `hello/Makefile`, `kernel.mak`, `user.mak`, `exports.exp` | Its two builds (and `make hosttest`). |
| `hello/hosttest.c` | The load test on the Mac. |
| `decoder/geprof.py` | The decoder (and `write_capture`, for synthetic captures). |
| `decoder/test_geprof.py` | Its tests. |

## What's been checked, and what hasn't

The plugin can't run in Phobos (there's no custom firmware there), so it's checked on the owner's PSP: a PSP-1000
(model 0) on firmware 6.61 with ARK 4.20.69 r205 (cIPL). Version 0.1.0's first dry run there (Chains of Olympus)
wrote nothing and showed nothing, which can't tell "not loaded" from "loaded but stopped"; 0.2.0's notices and trace
were to tell them apart. 0.2.0's run, with all three of his copies' IDs on one line, showed nothing either.
What could stop the plugin before its first line runs was then checked against ARK-4 r205's plugin loader and uOFW's
module loader, and nothing was found: ARK reads `PLUGINS.TXT` as these lines are written and matches a game's ID for
UMD, ISO and PlayStation Store games alike; the imports are marked weak (attribute 0x0009, as in ARK's and pspsdk's
own stubs), so a missing library or function can't fail the load; the module's layout, exports and relocations are
the usual ones; `module_start` uses well under the 4 KB stack a kernel module's start gets. 0.2.1 writes `trace.txt`
from `module_start` itself, so a module that starts but gets no further, or whose control thread can't run, still
leaves a trace, and the load test (above) tells "ARK starts no such module there" from "this plugin fails". Then
0.2.0 loaded from a line with his copy's ID alone (UCAS40198, the Asian disc, an ISO on the memory stick), and its
dry runs worked: all 17 hooks in place and the sub-interrupt monitor there, the first flip through the hook 4.35 s
after the start, 24 ns a call idle and about 1.85 microseconds capturing, 52 KB of records for a 10-second capture
and some 580 KB of kernel memory free. In each 10-second window on the game's menus it counted 357 sceGeListEnQueue,
357 sceGeListSync and 357 sceDisplaySetFrameBuf, 600 vertical blanks, no vblank waits and no GE callbacks. On the
Mac: it builds as a kernel PRX (attribute 0x1001, ELF type 0xFFA0, module info in a kernel segment) with exactly the
imports above; every static NID is confirmed by two sources (uOFW's 6.60 export lists or pspsdk's 6.60 libraries,
and pspsdk's stub libraries; ARK's by its name's SHA-1 and pspsdk's `libpspsystemctrl_kernel`); the hooks' idle path
was read in the disassembly; GCC checks every report line's format against its arguments; the plugin's C runs on
the Mac with the firmware faked (`make hosttest`), and the decoder reads its capture back and passes its tests on
synthetic ones. Not checked until the PSP runs it: that ARK's
`sctrlHENFindFunction` finds the user libraries by these module names (`sceGE_Manager` and `sceController_Service`
are uOFW's; `sceDisplay_Service` and `scePower_Service` are the firmware's usual names, with ARK's library-wide
search as a fallback where it has one), that `sctrlHENPatchSyscall` and the monitor behave as uOFW and ARK's header
describe on this firmware, what the hooks really cost, and how much kernel memory a game leaves (the dry run's
report answers each).

Clean room: the plugin, the load test and the decoder are this project's own code. The references were pspsdk's
headers and libraries, uOFW (for what the GE, display, interrupt and memory drivers do, their NIDs, and how the
module loader links and starts a module), and ARK-4's README, wiki and `systemctrl.h` (for its plugin API), its
plugin loader's and game launcher's sources and its own plugins' Makefiles (for how it loads plugins and what they
look like); no emulator's code and no other plugin's.
