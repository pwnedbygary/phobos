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
            "Vulkan-Headers", "puff", "Systematic console icons", "ZX Spectrum", "Android and Kotlin libraries", "C-BIOS",
            "FFmpeg",
        )
        for (name in expected) assertTrue("no notice for $name", titles.any { it.startsWith(name) })
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

    @Test fun copyingIsTheGplVersion3() {
        val gpl = File("../../COPYING").readText()
        assertTrue(gpl.trimStart().startsWith("GNU GENERAL PUBLIC LICENSE"))
        assertTrue(gpl.contains("Version 3, 29 June 2007"))
        // The APK appends it to LICENSE as one notice.
        assertTrue(gpl.lines().none { it == rule })
    }
}
