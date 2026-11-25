package com.phobos.emulator.ui.cartridge

import androidx.compose.ui.graphics.toArgb
import com.phobos.emulator.launch.LaunchSystems
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SystemFactsTest {

    @Test
    fun everyLibrarySystemHasFacts() {
        // The facts list itself, not factsFor: its fallback echoes the requested name, so asking factsFor
        // cannot detect a missing row.
        val known = allSystemFacts().map { it.system }.toSet()
        val missing = LaunchSystems.systemNames.filter { it !in known }
        assertTrue("systems with no facts: $missing", missing.isEmpty())
    }

    @Test
    fun factsAreUniqueAndComposed() {
        val all = allSystemFacts()
        assertEquals(all.size, all.map { it.system }.toSet().size)
        for (facts in all) {
            assertTrue(facts.system, facts.short.isNotBlank())
            assertTrue(facts.system, facts.maker.isNotBlank())
            assertTrue(facts.system, facts.year in 1971..2020)
        }
    }

    @Test
    fun fallbackIsStillUsable() {
        val facts = factsFor("A System With No Row")
        assertEquals("Phobos", facts.maker)
        assertTrue(facts.short.isNotBlank())
        assertEquals(0xFF7B6CF0.toInt(), facts.color.toArgb())
    }
}
