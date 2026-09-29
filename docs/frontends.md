# Starting games from a frontend

Phobos starts a game when another app asks it to, so frontends such as ES-DE, Daijisho and
Argosy can use it as their emulator. This page describes what Phobos accepts and how to set
up each frontend. The Retroid launcher can't be set up; see [below](#retroid-launcher).

## What Phobos accepts

- **Activity:** `com.phobos.emulator/.MainActivity`.
- **The game:** the intent's data URI, or else one of the extras `rom`, `ROM`, `romPath`,
  `path`, `file`, `filePath` or `uri` (a URI or a plain path).
  - A `content://` URI with read permission (ES-DE's `%ROMPROVIDER%`, Argosy's FileProvider
    URIs, a document the frontend grants) opens directly.
  - A plain path, a `file://` URI, or a storage document that comes without a grant (ES-DE's
    `%ROMSAF%`) opens through a folder Phobos already holds: add the game's folder to its
    system in Phobos (the system page's **Add ROM Folder**) first. Android doesn't let an app
    read other folders by path.
  - Phobos loads a single file, so use single-file formats such as `.chd`, `.pbp` or `.zip`
    rather than a `.cue` with separate `.bin` tracks.
- **The system (optional):** the extra `platform` or `system`. It takes Phobos's own names
  (`Nintendo 64`), Argosy's platform slugs (`n64`, `psx`, `genesis`, `tg16`), ES-DE's system
  names (`megadrive`, `pcenginecd`, `zxspectrum`) and Daijisho's short names (`master`, `ws`).
  Without it, Phobos goes by the Library folder the game is in, then an extension only one
  system uses, then the names of the folders above the game (`n64`, `psx`, `neo-geo-cd`),
  then what a `.zip` holds. If that still isn't clear, it asks.
- **Flags:** none needed. Phobos runs as one instance. A launch while a game runs switches
  to the new game, saving the running one first when Auto-Save State is on; a launch of the
  running game resumes it. Clear-task launches work the same way.
- **Leaving:** Quit Game in a game a frontend started returns to the frontend.
- **Saves:** the game keeps its file name, so its saves and states are the same ones it has
  when started from Phobos's Library.

To try a launch from a computer:

```sh
adb shell am start -n com.phobos.emulator/.MainActivity -a android.intent.action.VIEW \
  -d 'file:///storage/emulated/0/ROMs/n64/Game.z64' --es platform n64
```

## ES-DE

ES-DE reads extra emulators from two files in its `ES-DE/custom_systems` folder (restart
ES-DE after changing them).

`es_find_rules.xml` tells ES-DE where Phobos is:

```xml
<?xml version="1.0"?>
<ruleList>
    <emulator name="PHOBOS">
        <!-- Phobos, multi-system emulator -->
        <rule type="androidpackage">
            <entry>com.phobos.emulator/com.phobos.emulator.MainActivity</entry>
        </rule>
    </emulator>
</ruleList>
```

`es_systems.xml` adds Phobos to each system you want it for. A system in this file replaces
ES-DE's own entry for it, so copy the system from ES-DE's bundled
[es_systems.xml for Android](https://gitlab.com/es-de/emulationstation-de/-/blob/master/resources/systems/android/es_systems.xml)
and add a Phobos command. The first command is the system's default; the others can be
picked per system or per game in ES-DE. For the Nintendo 64, with the other commands as ES-DE
ships them (September 2026):

```xml
<?xml version="1.0"?>
<systemList>
    <system>
        <name>n64</name>
        <fullname>Nintendo 64</fullname>
        <path>%ROMPATH%/n64</path>
        <extension>.app .APP .bin .BIN .d64 .D64 .n64 .N64 .ndd .NDD .u1 .U1 .v64 .V64 .z64 .Z64 .7z .7Z .zip .ZIP</extension>
        <command label="Phobos (Standalone)">%EMULATOR_PHOBOS% %ACTION%=android.intent.action.VIEW %DATA%=%ROMPROVIDER% %EXTRA_platform%=n64</command>
        <command label="Mupen64Plus-Next">%EMULATOR_RETROARCH% %EXTRA_CONFIGFILE%=%EXTERNALDATA%/Android/data/%ANDROIDPACKAGE%/files/retroarch.cfg %EXTRA_LIBRETRO%=%INTERNALDATA%/%ANDROIDPACKAGE%/cores/mupen64plus_next_gles3_libretro_android.so %EXTRA_ROM%=%ROM%</command>
        <command label="M64Plus FZ (Standalone)">%EMULATOR_M64PLUS-FZ% %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%</command>
        <command label="Mupen64Plus AE (Standalone)">%EMULATOR_MUPEN64PLUS-AE% %ACTION%=android.intent.action.VIEW %DATA%=%ROMSAF%</command>
        <command label="ParaLLEl N64">%EMULATOR_RETROARCH% %EXTRA_CONFIGFILE%=%EXTERNALDATA%/Android/data/%ANDROIDPACKAGE%/files/retroarch.cfg %EXTRA_LIBRETRO%=%INTERNALDATA%/%ANDROIDPACKAGE%/cores/parallel_n64_libretro_android.so %EXTRA_ROM%=%ROM%</command>
        <command label="Native port">%ANDROIDAPP%=%FILEINJECT%</command>
        <platform>n64</platform>
        <theme>n64</theme>
    </system>
</systemList>
```

For another system, change `%EXTRA_platform%` to its ES-DE name (the `<name>` of the system,
for example `psx` or `megadrive`). `%ROMPROVIDER%` hands Phobos the file with permission
attached, so nothing needs setting up in Phobos; `%DATA%=%ROMSAF%` also works once the
system's folder is added in Phobos.

## Daijisho

Daijisho starts emulators through a player, a set of `am start` arguments per platform. Add a
player to each platform you want Phobos for, with:

```
-n com.phobos.emulator/.MainActivity
-a android.intent.action.VIEW
-d {file.uri}
-e platform n64
```

Change `n64` to the platform's short name (Daijisho's own: `n64`, `psx`, `genesis`, `master`,
`snes`, `gba`, …). If Phobos says it can't read a game, add the platform's folder to the
system in Phobos.

## Argosy

Argosy only offers the emulators in its built-in registry, and a user can't add one, so
Phobos needs an entry there: a change to
[rommapp/argosy-launcher](https://github.com/rommapp/argosy-launcher). With it, Argosy starts
Phobos with a FileProvider URI and its platform slug, and nothing needs setting up in Phobos.
The entry is prepared but not yet submitted.

## Retroid launcher

The Retroid Pocket's own launcher (`com.retroidpocket.gamelauncher`, part of the system) has
a fixed list of emulators and no way to add one, so it can't start games in Phobos. Use ES-DE,
Daijisho or Argosy on Retroid devices.
