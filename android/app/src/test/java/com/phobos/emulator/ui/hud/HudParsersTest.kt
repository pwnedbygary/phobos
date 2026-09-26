package com.phobos.emulator.ui.hud

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HudParsersTest {
    @Test fun statTicksSumUtimeAndStimeEvenWhenCommHasSpaces() {
        // pid (comm) state ppid pgrp session tty tpgid flags minflt cminflt majflt cmajflt utime stime ...
        val stat = "4242 (DefaultDispatch 1) R 1 1 0 0 -1 4194624 100 0 5 0 1234 567 0 0 20 0 30 0 99"
        assertEquals(1801L, HudParsers.statCpuTicks(stat))
    }

    @Test fun statTicksRejectMalformedInput() {
        assertNull(HudParsers.statCpuTicks("no parens here"))
        assertNull(HudParsers.statCpuTicks("1 (x) R 1 2"))
    }

    @Test fun vmRssIsParsedInKilobytes() {
        val status = "Name:\tphobos\nVmPeak:\t 9000 kB\nVmRSS:\t  421888 kB\nThreads:\t40\n"
        assertEquals(421888L, HudParsers.vmRssKb(status))
        assertNull(HudParsers.vmRssKb("Name:\tphobos\n"))
    }

    @Test fun gpuBusyAcceptsBothKgslFormats() {
        assertEquals(37, HudParsers.gpuBusyPercent("37 %\n"))
        assertEquals(25, HudParsers.gpuBusyPercent("1000 4000"))
        assertEquals(0, HudParsers.gpuBusyPercent("0 1000"))
        assertNull(HudParsers.gpuBusyPercent("5 0"))
        assertNull(HudParsers.gpuBusyPercent(""))
    }

    @Test fun thermalReadingsAcceptMilliAndWholeDegrees() {
        assertEquals(41.5f, HudParsers.celsius("41500")!!, 0.001f)
        assertEquals(41f, HudParsers.celsius("41")!!, 0.001f)
        assertNull(HudParsers.celsius("250000"))
        assertNull(HudParsers.celsius("n/a"))
    }

    @Test fun batteryCurrentNormalizesMilliampReportsAndSign() {
        assertEquals(850_000L, HudParsers.batteryCurrentMicroAmps(-850_000))
        assertEquals(1_500_000L, HudParsers.batteryCurrentMicroAmps(-1500))
        assertNull(HudParsers.batteryCurrentMicroAmps(0))
        assertNull(HudParsers.batteryCurrentMicroAmps(Int.MIN_VALUE))
    }

    @Test fun wattsComeFromCurrentAndVoltage() {
        assertEquals(4.0f, HudParsers.watts(1_000_000, 4000)!!, 0.001f)
        assertNull(HudParsers.watts(1_000_000, 0))
        assertNull(HudParsers.watts(100_000_000, 4000))
    }

    @Test fun everyPresetIsRecognisedAfterApplying() {
        val base = HudConfig(horizontal = true, opacity = 0.3f, scale = 1.4f)
        for (preset in HudPreset.entries) {
            val applied = preset.applyTo(base)
            assertEquals(preset, HudPreset.matching(applied))
            assertEquals(base.horizontal, applied.horizontal)
            assertEquals(base.opacity, applied.opacity, 0f)
            assertEquals(base.scale, applied.scale, 0f)
        }
    }

    @Test fun customSelectionMatchesNoPreset() {
        val custom = HudPreset.ESSENTIAL.applyTo(HudConfig()).copy(battery = true)
        assertNull(HudPreset.matching(custom))
        assertTrue(HudPreset.matching(HudConfig()) == HudPreset.ESSENTIAL)
    }
}
