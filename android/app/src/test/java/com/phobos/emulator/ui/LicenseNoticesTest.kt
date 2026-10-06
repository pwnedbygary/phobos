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
        assertEquals("ares", titles.first())
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
    }
}
