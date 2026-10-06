package com.phobos.emulator.launch

import com.phobos.emulator.launch.LaunchSystems.Match
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LaunchSystemsTest {
    // PhobosJNI.cpp's systemExtensions on 2026-10-04.
    private val extensions = mapOf(
        "Atari 2600" to listOf("a26", "bin"),
        "ColecoVision" to listOf("col", "cv"),
        "Famicom" to listOf("fc", "nes", "unf", "unif", "unh", "fds"),
        "Super Famicom" to listOf("sfc", "smc", "swc", "fig", "bs", "st"),
        "Super Game Boy" to listOf("gb"),
        "Arcade" to listOf("zip"),
        "Mega LD" to listOf("mmi"),
        "PC Engine LD" to listOf("mmi"),
        "Nintendo 64" to listOf("n64", "v64", "z64", "n64dd", "ndd", "d64"),
        "Game Boy" to listOf("gb"),
        "Game Boy Color" to listOf("gb", "gbc", "nbc"),
        "Game Boy Advance" to listOf("gba"),
        "SG-1000" to listOf("sg1000", "sg"),
        "Master System" to listOf("ms", "sms"),
        "Mega Drive" to listOf("md", "gen", "bin"),
        "Mega 32X" to listOf("32x", "bin"),
        "Game Gear" to listOf("gg"),
        "Mega CD" to listOf("cue", "chd", "iso"),
        "Mega CD 32X" to listOf("cue", "chd", "iso"),
        "PlayStation" to listOf("cue", "chd", "exe", "ps-exe", "pbp", "iso", "mdf", "img"),
        "PlayStation Portable" to listOf("iso", "cso", "pbp", "elf"),
        "Neo Geo" to listOf("ng", "neo"),
        "Neo Geo CD" to listOf("ngc", "cue", "chd", "iso", "bin", "zip"),
        "Neo Geo Pocket" to listOf("ngp", "nap"),
        "Neo Geo Pocket Color" to listOf("ngpc", "ngc", "nbc"),
        "ZX Spectrum" to listOf("wav", "tzx", "tap"),
        "ZX Spectrum 128" to listOf("wav", "tzx", "tap"),
        "PC Engine" to listOf("pce", "tg16"),
        "PC Engine CD" to listOf("cue", "chd"),
        "SuperGrafx" to listOf("sgx"),
        "WonderSwan" to listOf("ws"),
        "WonderSwan Color" to listOf("wsc"),
        "Pocket Challenge V2" to listOf("pc2", "pcv2"),
        "MSX" to listOf("msx", "rom", "wav", "tzx", "tsx", "cas"),
        "MSX2" to listOf("msx2", "rom", "wav", "tzx", "tsx", "cas"),
    )
    private val systems = extensions.keys.sorted()
    private val root = "/storage/emulated/0"

    private fun resolve(
        name: String,
        path: String? = null,
        hint: String? = null,
        folders: Map<String, List<String>> = emptyMap(),
        entries: List<String> = emptyList(),
    ) = LaunchSystems.resolve(name, path, hint, systems, extensions, folders) { entries }

    private fun found(system: String) = Match.Found(system)

    @Test fun frontendNamesMapToPhobosSystems() {
        // Argosy's platform slugs.
        mapOf(
            "n64" to "Nintendo 64", "psx" to "PlayStation", "genesis" to "Mega Drive", "scd" to "Mega CD",
            "tg16" to "PC Engine", "tgcd" to "PC Engine CD", "coleco" to "ColecoVision", "wsc" to "WonderSwan Color",
            "zx" to "ZX Spectrum", "sg1000" to "SG-1000", "sms" to "Master System", "gg" to "Game Gear",
            "nes" to "Famicom", "fds" to "Famicom", "snes" to "Super Famicom", "n64dd" to "Nintendo 64",
            "ngpc" to "Neo Geo Pocket Color", "neogeocd" to "Neo Geo CD", "supergrafx" to "SuperGrafx",
            "32x" to "Mega 32X", "sega32" to "Mega 32X",
        ).forEach { (slug, system) -> assertEquals(slug, system, LaunchSystems.systemForName(slug)) }
        // ES-DE's system names.
        mapOf(
            "megadrive" to "Mega Drive", "mastersystem" to "Master System", "sg-1000" to "SG-1000",
            "tg-cd" to "PC Engine CD", "pcenginecd" to "PC Engine CD", "zxspectrum" to "ZX Spectrum",
            "wonderswancolor" to "WonderSwan Color", "colecovision" to "ColecoVision", "sfc" to "Super Famicom",
            "megacdjp" to "Mega CD", "msx1" to "MSX", "sega32x" to "Mega 32X", "sega32xjp" to "Mega 32X",
            "sega32xna" to "Mega 32X",
        ).forEach { (name, system) -> assertEquals(name, system, LaunchSystems.systemForName(name)) }
        // Daijisho's short names, and Phobos's own names in any case and spacing.
        assertEquals("Master System", LaunchSystems.systemForName("master"))
        assertEquals("WonderSwan", LaunchSystems.systemForName("ws"))
        assertEquals("Nintendo 64", LaunchSystems.systemForName("Nintendo 64"))
        assertEquals("Mega CD", LaunchSystems.systemForName("MEGA CD"))
        assertEquals("Mega 32X", LaunchSystems.systemForName("Mega 32X"))
        assertEquals("Mega CD 32X", LaunchSystems.systemForName("Mega CD 32X"))
        assertEquals("ZX Spectrum", LaunchSystems.systemForName("ZX Spectrum 128"))
        listOf("ps2", "saturn", "", null).forEach { assertNull(it, LaunchSystems.systemForName(it)) }
        assertEquals("Arcade", LaunchSystems.systemForName("arcade"))
        assertEquals("Arcade", LaunchSystems.systemForName("aleck64"))
        assertEquals("Arcade", LaunchSystems.systemForName("mame"))
    }

    @Test fun pspGamesGoToThePsp() {
        assertEquals("PlayStation Portable", LaunchSystems.systemForName("psp"))
        assertEquals(found("PlayStation Portable"), resolve("Game.iso", hint = "psp"))
        // Only the PSP takes a CSO or an ELF.
        assertEquals(found("PlayStation Portable"), resolve("Game.cso"))
        assertEquals(found("PlayStation Portable"), resolve("Cube.elf"))
        // An ISO or an EBOOT.PBP goes by its folder: the PlayStation takes both too.
        assertEquals(found("PlayStation Portable"), resolve("Game.iso", "$root/ROMs/psp/Game.iso"))
        assertEquals(found("PlayStation Portable"), resolve("EBOOT.PBP", "$root/ROMs/psp/Cube/EBOOT.PBP"))
        assertEquals(found("PlayStation"), resolve("Game.pbp", "$root/ROMs/psx/Game.pbp"))
        assertEquals(Match.Ask(listOf("PlayStation", "PlayStation Portable")), resolve("Game.pbp"))
    }

    @Test fun theFrontendsHintComesFirst() {
        assertEquals(found("Game Boy Color"), resolve("Tetris.gb", "$root/ROMs/gb/Tetris.gb", hint = "gbc"))
        assertEquals(found("PlayStation"), resolve("Game.cue", hint = "psx"))
        // A hint that isn't a Phobos system is ignored.
        assertEquals(found("Nintendo 64"), resolve("Game.z64", hint = "saturn"))
    }

    @Test fun theLibraryFolderGivesTheLibrarysSystem() {
        val folders = mapOf("Game Boy Color" to listOf("$root/Games/Handhelds"), "Game Boy" to listOf("$root/Other"))
        assertEquals(found("Game Boy Color"), resolve("Tetris.gb", "$root/Games/Handhelds/Tetris.gb", folders = folders))
        // The deepest Library folder wins, among systems that take the file.
        val nested = mapOf("PlayStation" to listOf("$root/ROMs"), "Mega CD" to listOf("$root/ROMs/sega/"))
        assertEquals(found("Mega CD"), resolve("Sonic CD.cue", "$root/ROMs/sega/Sonic CD.cue", folders = nested))
        // A folder whose system doesn't take the extension doesn't count; the scan wouldn't list the file there.
        val wrong = mapOf("Game Boy" to listOf("$root/ROMs"))
        assertEquals(found("Nintendo 64"), resolve("Mario.z64", "$root/ROMs/Mario.z64", folders = wrong))
        // "Inside" means under the folder, not a name that starts the same.
        val prefix = mapOf("PlayStation" to listOf("$root/ROMs/ps"))
        assertEquals(Match.Ask(listOf("Mega CD", "Mega CD 32X", "Neo Geo CD", "PC Engine CD", "PlayStation")),
            resolve("Game.cue", "$root/ROMs/ps2/Game.cue", folders = prefix))
    }

    @Test fun anExtensionOnlyOneSystemUsesDecides() {
        assertEquals(found("Nintendo 64"), resolve("Mario Tennis (USA).z64"))
        assertEquals(found("Famicom"), resolve("Zelda.FDS"))
        assertEquals(found("ZX Spectrum"), resolve("Manic Miner.tap"))
        assertEquals(found("WonderSwan Color"), resolve("Game.wsc", "$root/ROMs/ws/Game.wsc"))
    }

    @Test fun folderNamesDecideSharedExtensions() {
        assertEquals(found("PlayStation"), resolve("Game.cue", "$root/ROMs/psx/Game/Game.cue"))
        assertEquals(found("Mega CD"), resolve("Game.chd", "/storage/EBFF-F6C0/ROMs/segacd/Game.chd"))
        assertEquals(found("Game Boy Color"), resolve("Tetris.gb", "$root/ROMs/GBC/Tetris.gb"))
        assertEquals(found("Mega Drive"), resolve("Sonic.bin", "$root/ROMs/genesis/Sonic.bin"))
        assertEquals(found("MSX2"), resolve("Game.rom", "$root/ROMs/msx2/Game.rom"))
        // Folder names as the RP6's ROM card has them.
        assertEquals(found("Neo Geo CD"), resolve("Game.chd", "/storage/EBFF-F6C0/ROMs/neo-geo-cd/Game.chd"))
        assertEquals(found("Neo Geo"), resolve("kof98.zip", "/storage/EBFF-F6C0/ROMs/neogeoaes/kof98.zip"))
        assertEquals(found("PC Engine CD"), resolve("Game.cue", "/storage/EBFF-F6C0/ROMs/turbografx-cd/Game.cue"))
        assertEquals(found("Nintendo 64"), resolve("Game.zip", "/storage/EBFF-F6C0/ROMs/64dd/Game.zip"))
        assertEquals(Match.Ask(LaunchSystems.librarySystems(systems).sorted()), resolve("Game.zip", "/storage/EBFF-F6C0/ROMs/ngc/Game.zip"))
        assertEquals(found("Neo Geo"), resolve("kof98.zip", "$root/ROMs/neogeo/kof98.zip"))
    }

    @Test fun sharedExtensionsFallBackToTheirUsualSystem() {
        assertEquals(found("Game Boy"), resolve("Tetris.gb"))
        assertEquals(found("Neo Geo Pocket Color"), resolve("Sonic.ngc"))
    }

    @Test fun superGameBoyHintSelectsTheSgbSystem() {
        assertEquals(found("Super Game Boy"), resolve("Tetris.gb", hint = "sgb"))
        assertEquals(found("Super Game Boy"), resolve("Tetris.gb", hint = "supergameboy"))
        // Without a hint, .gb stays Game Boy.
        assertEquals(found("Game Boy"), resolve("Tetris.gb"))
    }

    @Test fun arcadeHintAndAleck64ZipSelectArcade() {
        assertEquals(found("Arcade"), resolve("11beat.zip", hint = "arcade"))
        assertEquals(found("Arcade"), resolve("11beat.zip", hint = "aleck64"))
        assertEquals(
            found("Arcade"),
            resolve("11beat.zip", entries = listOf("nus-zhaj.u3", "pifdata.bin")),
        )
    }

    @Test fun laserActiveHintsSelectMegaLdOrPceLd() {
        assertEquals(found("Mega LD"), resolve("Game.mmi", hint = "megald"))
        assertEquals(found("Mega LD"), resolve("Game.mmi", hint = "laseractive"))
        assertEquals(found("PC Engine LD"), resolve("Game.mmi", hint = "necpac"))
        assertEquals(found("PC Engine LD"), resolve("Game.mmi", hint = "ldrom2"))
    }

    @Test fun pocketChallengeV2GamesAndNames() {
        assertEquals(found("Pocket Challenge V2"), resolve("Game.pc2"))
        assertEquals(found("Pocket Challenge V2"), resolve("Game.pcv2"))
        assertEquals(found("Pocket Challenge V2"), resolve("Game.zip", "$root/ROMs/pcv2/Game.zip"))
        assertEquals("Pocket Challenge V2", LaunchSystems.systemForName("pcv2"))
        assertEquals("Pocket Challenge V2", LaunchSystems.systemForName("Pocket Challenge V2"))
    }

    @Test fun zipsAreIdentifiedByWhatTheyHold() {
        assertEquals(found("Famicom"), resolve("Mario.zip", entries = listOf("Mario.nes")))
        assertEquals(found("Game Boy"), resolve("Tetris.zip", entries = listOf("readme.txt", "Tetris.gb")))
        assertEquals(found("Neo Geo"), resolve("kof98.zip", entries = listOf("242-c1.c1", "242-m1.m1", "242-p1.p1")))
        assertEquals(Match.Ask(LaunchSystems.librarySystems(systems).sorted()), resolve("Unknown.zip", entries = listOf("a.txt")))
    }

    @Test fun discsFiledWithThe32xAreCd32xGames() {
        // ES-DE keeps CD 32X discs in its 32X folders and sends them as sega32x.
        assertEquals(found("Mega CD 32X"), resolve("Night Trap.chd", hint = "sega32x"))
        assertEquals(found("Mega CD 32X"), resolve("Night Trap.chd", "$root/ROMs/sega32x/Night Trap.chd"))
        // Cartridges stay 32X games, however they're named or packed.
        assertEquals(found("Mega 32X"), resolve("Chaotix.32x"))
        assertEquals(found("Mega 32X"), resolve("Chaotix.zip", hint = "sega32x"))
        assertEquals(found("Mega 32X"), resolve("Doom.bin", "$root/ROMs/sega32x/Doom.bin"))
        // Other systems' discs are left alone.
        assertEquals(found("Mega CD"), resolve("Sonic CD.chd", hint = "segacd"))
    }

    @Test fun anUnclearGameAsksBetweenTheSystemsThatTakeIt() {
        assertEquals(Match.Ask(listOf("Mega CD", "Mega CD 32X", "Neo Geo CD", "PC Engine CD", "PlayStation")), resolve("Game.cue"))
        assertEquals(Match.Ask(listOf("MSX", "MSX2", "ZX Spectrum")), resolve("Game.tzx"))
        val all = resolve("Game.xyz") as Match.Ask
        assertTrue("ZX Spectrum" in all.candidates)
        assertFalse("ZX Spectrum 128" in all.candidates)
    }

    @Test fun zipEntriesAreOnlyReadWhenNeeded() {
        var reads = 0
        val match = LaunchSystems.resolve("Mario.zip", "$root/ROMs/nes/Mario.zip", null, systems, extensions) {
            reads++; listOf("Mario.nes")
        }
        assertEquals(found("Famicom"), match)
        assertEquals(0, reads)
    }

    @Test fun documentIdsAndPathsConvertBothWays() {
        assertEquals("$root/ROMs/n64/Mario.z64", LaunchSystems.documentIdToPath("primary:ROMs/n64/Mario.z64", root))
        assertEquals("/storage/EBFF-F6C0/ROMs", LaunchSystems.documentIdToPath("EBFF-F6C0:ROMs", root))
        assertEquals("/storage/EBFF-F6C0", LaunchSystems.documentIdToPath("EBFF-F6C0:", root))
        assertEquals("$root/Documents/x.gb", LaunchSystems.documentIdToPath("home:x.gb", root))
        assertNull(LaunchSystems.documentIdToPath("12345", root))

        assertEquals("primary:ROMs/n64/Mario.z64", LaunchSystems.pathToDocumentId("$root/ROMs/n64/Mario.z64", root))
        assertEquals("primary:ROMs/gb/x.gb", LaunchSystems.pathToDocumentId("/sdcard/ROMs/gb/x.gb", root))
        assertEquals("EBFF-F6C0:ROMs/n64/Mario.z64", LaunchSystems.pathToDocumentId("/storage/EBFF-F6C0/ROMs/n64/Mario.z64", root))
        assertEquals("EBFF-F6C0:", LaunchSystems.pathToDocumentId("/storage/EBFF-F6C0", root))
        assertNull(LaunchSystems.pathToDocumentId("/data/data/x/files/rom.z64", root))
        assertNull(LaunchSystems.pathToDocumentId("/storage/emulated/10/x.gb", root))
    }

    @Test fun treesContainTheirDocumentsOnly() {
        assertTrue(LaunchSystems.treeContains("primary:ROMs", "primary:ROMs/n64/Mario.z64"))
        assertTrue(LaunchSystems.treeContains("primary:ROMs", "primary:ROMs"))
        assertTrue(LaunchSystems.treeContains("EBFF-F6C0:", "EBFF-F6C0:ROMs/Mario.z64"))
        assertFalse(LaunchSystems.treeContains("primary:ROMs", "primary:ROMs2/x.gb"))
        assertFalse(LaunchSystems.treeContains("primary:ROMs", "EBFF-F6C0:ROMs/x.gb"))
    }

    @Test fun launchUrisMapToTheirFiles() {
        fun path(scheme: String?, authority: String?, segments: List<String>, path: String?) =
            LaunchSystems.filesystemPath(scheme, authority, segments, path, root)
        val saf = "com.android.externalstorage.documents"
        // A SAF document inside a tree, and a plain one (Uri decodes each segment, so the ID keeps its slashes).
        assertEquals("$root/ROMs/n64/Mario.z64",
            path("content", saf, listOf("tree", "primary:ROMs", "document", "primary:ROMs/n64/Mario.z64"), null))
        assertEquals("/storage/EBFF-F6C0/ROMs/psx/Game.chd",
            path("content", saf, listOf("document", "EBFF-F6C0:ROMs/psx/Game.chd"), null))
        // Downloads documents are paths only when their ID is a raw path, not a MediaStore ID.
        val downloads = "com.android.providers.downloads.documents"
        assertEquals("$root/Download/x.gb", path("content", downloads, listOf("document", "raw:$root/Download/x.gb"), null))
        assertNull(path("content", downloads, listOf("document", "msf:42"), null))
        // Argosy's FileProvider serves the whole file system under "root".
        assertEquals("$root/ROMs/n64/Mario Tennis.z64",
            path("content", "com.nendo.argosy.fileprovider", listOf("root", "storage", "emulated", "0", "ROMs", "n64", "Mario Tennis.z64"),
                "/root/storage/emulated/0/ROMs/n64/Mario Tennis.z64"))
        assertEquals("$root/ROMs/gb/x.gb", path("content", "some.fileprovider", listOf("sd", "sdcard", "ROMs", "gb", "x.gb"), "/sd/sdcard/ROMs/gb/x.gb"))
        assertNull(path("content", "some.fileprovider", listOf("files", "x.gb"), "/files/x.gb"))
        assertEquals("/storage/EBFF-F6C0/ROMs/x.gb", path("file", null, listOf("storage", "EBFF-F6C0", "ROMs", "x.gb"), "/storage/EBFF-F6C0/ROMs/x.gb"))
        assertEquals("$root/ROMs/x.gb", path(null, null, emptyList(), "$root/ROMs/x.gb"))
        assertNull(path("https", "example.com", listOf("x.gb"), "/x.gb"))
    }

    @Test fun folderNamesAreTheNearestParents() {
        assertEquals(listOf("Game", "psx", "ROMs"), LaunchSystems.folderNames("$root/ROMs/psx/Game/Game.cue"))
        assertEquals(listOf("n64"), LaunchSystems.folderNames("/n64/Mario.z64"))
    }
}
