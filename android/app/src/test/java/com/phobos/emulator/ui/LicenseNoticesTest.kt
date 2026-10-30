package com.phobos.emulator.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class LicenseNoticesTest {
    private val rule = "-".repeat(70)

    @Test fun blocksBetweenRulesBecomeNoticesTitledByTheirFirstLine() {
        val text = listOf(rule, "First", "", "Body one", rule, "", rule, "  Second  ", "Body", "", "two", "", rule, "", rule, "", rule)
            .joinToString("\n")
        assertEquals(
            listOf(LicenseNotice("First", "Body one"), LicenseNotice("Second", "Body\n\ntwo")),
            parseLicenseNotices(text),
        )
    }

    @Test fun theRepositoryLicenseCoversWhatPhobosShips() {
        val notices = parseLicenseNotices(File("../../LICENSE").readText())
        val titles = notices.map { it.title }
        assertEquals(listOf("Phobos", "ares"), titles.take(2))
        val phobos = notices.first().text.replace(Regex("\\s+"), " ")
        assertTrue(phobos.contains("either version 3 of the License, or (at your option) any later version"))
        assertEquals(titles.size, titles.toSet().size)
        assertTrue(notices.all { it.text.isNotBlank() })
        val expected = listOf(
            "MAME (portions)", "paraLLEl-RDP", "volk", "sse2neon", "miniz", "Zstandard", "libadrenotools",
            "Vulkan-Headers", "font-rs", "puff", "Systematic console icons", "ZX Spectrum", "Android and Kotlin libraries", "C-BIOS",
            "FFmpeg", "MinGW-w64 runtime", "SDL 3.2.30 (", "MoltenVK", "AppImage runtime", "Cartridge (portions",
            "Artwork downloaded while Phobos runs", "Filament (Apache License 2.0", "three.js (MIT; build tools only",
            "studio.hdr (origin unknown",
        )
        for (name in expected) assertTrue("no notice for $name", titles.any { it.startsWith(name) })
    }

    @Test fun theSystematicNoticeCoversTheConsolesDerivedFromIt() {
        val notices = parseLicenseNotices(File("../../LICENSE").readText())
        val systematic = notices.single { it.title.startsWith("Systematic console icons") }.text.replace(Regex("\\s+"), " ")
        assertTrue("the MIT license", systematic.contains("Permission is hereby granted, free of charge"))
        for (derived in listOf("ConsolePixels.kt", "tools/console-art/pixelate.py", "ConsoleTreatment.kt")) {
            assertTrue("the notice names $derived", systematic.replace(" ", "").contains(derived))
            val path = if (derived.startsWith("tools/")) "../../$derived" else "src/main/java/com/phobos/emulator/ui/theme/$derived"
            assertTrue("$derived is where the notice says", File(path).exists())
        }
        assertTrue("no notice for the removed glyph packs", notices.none { it.title.contains("glyph", ignoreCase = true) })
    }

    @Test fun cartridgesNoticeCoversTheArtworkCodePortedFromIt() {
        val notices = parseLicenseNotices(File("../../LICENSE").readText())
        val cartridge = notices.single { it.title.startsWith("Cartridge (portions") }.text.replace(Regex("\\s+"), " ")
        assertTrue("the MIT license", cartridge.contains("Permission is hereby granted, free of charge"))
        assertTrue("its reconstructed line", cartridge.contains("Copyright (c) Isaiah (Cartridge)"))
        assertTrue("says the line is reconstructed", cartridge.contains("reconstructed"))
        for (derived in listOf("ArtMatch.kt", "ArtDownloader.kt", "ArtStore.kt", "ArtSystems.kt")) {
            assertTrue("the notice names $derived", cartridge.contains(derived))
            val source = File("src/main/java/com/phobos/emulator/art/$derived")
            assertTrue("$derived is where the notice says", source.exists())
            assertTrue("$derived's header names Cartridge", source.readLines().take(5).any { "Cartridge" in it })
        }
        assertTrue("the provenance file it names", File("../../docs/third-party/cartridge.md").exists())
        val art = notices.single { it.title.startsWith("Artwork downloaded while Phobos runs") }.text
        assertTrue("names the server", art.contains("https://thumbnails.libretro.com"))
        assertTrue("says no license is published", art.contains("publishes no license"))
    }

    @Test fun ffmpegsNoticeCarriesItsLicenseAndWhereItsSourceIs() {
        val ffmpeg = parseLicenseNotices(File("../../LICENSE").readText()).single { it.title.startsWith("FFmpeg") }
        assertTrue(ffmpeg.text.contains("This software uses libraries from the FFmpeg project under the LGPLv2.1."))
        assertTrue(ffmpeg.text.contains("GNU LESSER GENERAL PUBLIC LICENSE"))
        val build = File("../../thirdparty/ffmpeg/build.sh").readText()
        val version = Regex("VERSION=(\\S+)").find(build)!!.groupValues[1]
        val sha256 = Regex("SHA256=(\\S+)").find(build)!!.groupValues[1]
        assertTrue("the notice names the release built", ffmpeg.text.contains("ffmpeg-$version.tar.xz"))
        assertTrue("the notice gives its hash", ffmpeg.text.replace("\n", " ").contains(sha256))
        assertTrue("no GPL parts", build.contains("--disable-gpl") && !build.contains("--enable-gpl"))
        val flat = ffmpeg.text.replace(Regex("\\s+"), " ")
        assertTrue(
            "the IJG's credit (FFmpeg's LICENSE.md asks for it)",
            flat.contains("This software is based in part on the work of the Independent JPEG Group."),
        )
        val decoders = Regex("--enable-decoder=(\\S+)").find(build)!!.groupValues[1].split(",")
        assertEquals(listOf("atrac3", "atrac3p", "mp3float", "h264"), decoders)
        assertTrue("the notice lists the decoders built", flat.contains("the decoders atrac3, atrac3p, mp3float and h264."))
    }

    @Test fun mingwsNoticeCarriesTheRuntimesAndWinpthreadsTexts() {
        val mingw = parseLicenseNotices(File("../../LICENSE").readText()).single { it.title.startsWith("MinGW-w64 runtime") }
        val flat = mingw.text.replace(Regex("\\s+"), " ")
        assertTrue("the runtime's notices", flat.contains("statically linked against the MinGW-w64 runtime"))
        assertTrue("its overall (ZPL) notice", flat.contains("Copyright (c) 2009, 2010, 2011, 2012, 2013 by the mingw-w64 project"))
        assertTrue("winpthreads' (MIT) notice", flat.contains("Copyright (c) 2011 mingw-w64 project"))
        assertTrue("winpthreads' Lockless part", flat.contains("(C) 2010 Lockless Inc."))
    }

    @Test fun everyLabelCapableFontIsCoveredWithItsCopyrightAndTheOfl() {
        val notices = parseLicenseNotices(File("../../LICENSE").readText())
        for (face in listOf(
            "vt323", "comic_neue_regular", "comic_neue_bold", "galmuri11", "galmuri9", "mplus1",
            "dotgothic16", "press_start_2p", "bangers",
        )) {
            assertTrue("$face is a bundled face", File("src/main/res/font/$face.ttf").exists())
        }
        for (family in listOf("VT323", "Comic Neue", "M PLUS 1", "Galmuri", "DotGothic16", "Press Start 2P", "Bangers")) {
            val notice = notices.first { it.title.contains(family) }
            assertTrue("$family's notice has its copyright line", notice.text.contains("Copyright"))
            assertTrue("$family's notice carries the OFL", notice.text.contains("SIL OPEN FONT LICENSE"))
        }
    }

    @Test fun copyingIsTheGplVersion3() {
        val gpl = File("../../COPYING").readText()
        assertTrue(gpl.trimStart().startsWith("GNU GENERAL PUBLIC LICENSE"))
        assertTrue(gpl.contains("Version 3, 29 June 2007"))
        // The APK appends it to LICENSE as one notice.
        assertTrue(gpl.lines().none { it == rule })
    }
}
