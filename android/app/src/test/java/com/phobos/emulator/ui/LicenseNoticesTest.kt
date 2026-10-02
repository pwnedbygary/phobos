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
        )
        for (name in expected) assertTrue("no notice for $name", titles.any { it.startsWith(name) })
    }
}
