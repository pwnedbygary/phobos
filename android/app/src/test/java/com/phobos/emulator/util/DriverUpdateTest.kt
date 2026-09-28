package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DriverUpdateTest {
    private fun release(tag: String, publishedAt: String) = DriverAsset(tag, "$tag.zip", "", 0, publishedAt)

    // StevenMXZ/Adreno-Tools-Drivers on 2026-09-28: three lines, newest first by creation.
    private val stevenMxz = listOf(
        release("v863.1_a7xx", "2026-05-23T10:43:36Z"),
        release("v36", "2026-09-08T11:07:59Z"),
        release("v35", "2026-08-24T12:42:55Z"),
        release("v26.3.0-R5", "2026-09-08T11:04:12Z"),
        release("v26.3.0-R4", "2026-08-24T12:44:02Z"),
    )

    @Test fun aNewerReleaseInAnotherLineIsNotAnUpdate() {
        // Gen8 V36 went up four minutes after Turnip R5; it is for another GPU generation.
        assertNull(newerDriverRelease("v26.3.0-R5", stevenMxz))
        assertNull(newerDriverRelease("v36", stevenMxz))
        assertNull(newerDriverRelease("v863.1_a7xx", stevenMxz))
    }

    @Test fun aNewerReleaseInTheSameLineIsAnUpdate() {
        assertEquals("v26.3.0-R5", newerDriverRelease("v26.3.0-R4", stevenMxz)?.tag)
        assertEquals("v36", newerDriverRelease("v35", stevenMxz)?.tag)
        val withR10 = stevenMxz + release("v26.3.0-R10", "2026-09-20T09:00:00Z")
        assertEquals("v26.3.0-R10", newerDriverRelease("v26.3.0-R5", withR10)?.tag)
    }

    @Test fun anOlderReleaseInTheSameLineIsNotAnUpdate() {
        // Listed first because it was created first, but published before the installed one.
        val reordered = listOf(release("v26.3.0-R3", "2026-08-04T18:37:14Z")) + stevenMxz
        assertNull(newerDriverRelease("v26.3.0-R5", reordered))
    }

    @Test fun anInstalledReleaseOlderThanTheListStillFindsTheNewest() {
        assertEquals("v26.3.0-R5", newerDriverRelease("v26.2.0-R9", stevenMxz)?.tag)
    }

    @Test fun unknownTagsAreIgnored() {
        assertNull(newerDriverRelease("unknown", listOf(release("unknown", "2026-09-28T00:00:00Z"))))
    }
}
