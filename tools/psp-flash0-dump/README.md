# psp-flash0-dump

A homebrew program for a real PSP that copies every file of its flash0 (the part of its built-in flash memory
holding the firmware's modules, fonts and other system files; the settings are on flash1) into its own folder on the
memory stick. It only reads flash0. Phobos's PSP core can use those files: games' system fonts come from
`flash0:/font` (sceFont), and some of the firmware's library modules could stand in for Phobos's own (see "Firmware
as an option, per module family" in [docs/psp-core.md](../../docs/psp-core.md)).

**The copies are your own firmware, made from your own PSP.** Keep them on your own devices. They never go into
Phobos's repository: only this program's source does.

## Building

With pspdev's toolchain (`psp-config` on the `PATH`; the `phobos-linux` container has it in `/opt/pspdev`), run `make`
in this folder. It builds `EBOOT.PBP`. `make SMOKE=1` builds a version for an emulator: it starts by itself, leaves at
the end without waiting for a button, and copies a sample folder, `ms0:/flash0-sample`, instead of flash0 (an
emulator's flash0 may not list its folders). Run `make clean` when switching between the two: `make` doesn't notice
the flag changing, and would leave the other build's objects.

## Running

On a PSP with custom firmware, copy `EBOOT.PBP` to `PSP/GAME/FLASH0DUMP/` on the memory stick and start "flash0 dump"
from the XMB. X starts the copy; O leaves. It lists each folder as it copies it, then says how many files it copied
and how many it couldn't. Running it again copies everything again, over the last copy.

Beside `EBOOT.PBP` it leaves:

- `flash0/`: the files, in flash0's own folders;
- `SHA256SUMS`: each file's SHA-256 fingerprint, as `shasum -a 256 -c SHA256SUMS` checks on a computer (run it in the
  `FLASH0DUMP` folder, once it's copied over);
- `failed.txt`: anything it couldn't read or write, with the PSP's error code;
- `flash0.img`, rarely: flash0 whole, as the PSP keeps it (the device `lflash0:0,0`). Reading that needs more rights
  than a homebrew program usually gets, so its line in `failed.txt` is expected; the files are what Phobos needs.

## Checked

The `SMOKE=1` version in PPSSPP's headless build (software rendering) copied a sample of 23 files (PPSSPP's own font
files, nested folders, an empty file, one of exactly 64 KiB and one spanning four 64 KiB pieces): every copy
verified against `SHA256SUMS`. The SHA-256 code gives the same fingerprints as `shasum -a 256` on the empty input,
"abc", inputs around each padding boundary (55 to 128 bytes) and a megabyte, fed in uneven pieces. It hasn't run on
a PSP yet.
