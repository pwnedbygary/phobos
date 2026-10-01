package com.phobos.emulator.input

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ControlLevelsTest {
    private val a = 1 shl 4
    private val b = 1 shl 5
    private val x = 1 shl 6
    private val global = mapOf(a to "k:96", b to "k:97", x to "k:99")
    private val tennis = ControlLevel.Game("Nintendo 64", "Mario Tennis (USA).zip")
    private val kart = ControlLevel.Game("Nintendo 64", "Mario Kart 64 (USA).zip")
    private val n64 = ControlLevel.Console("Nintendo 64")
    private val ps1Game = ControlLevel.Game("PlayStation", "Ape Escape (USA).chd")

    @Test
    fun aGameTakesItsOwnValueThenItsConsolesThenEveryones() {
        val overrides = ControlOverrides(
            mappings = mapOf(
                "Nintendo 64" to mapOf(a to "k:100"),
                "Nintendo 64/Mario Tennis (USA).zip" to mapOf(a to "k:102"),
            ),
        )
        assertEquals("k:102", Controls.mappings(tennis, global, overrides)[a])
        assertEquals("k:100", Controls.mappings(kart, global, overrides)[a])
        assertEquals("k:100", Controls.mappings(n64, global, overrides)[a])
        assertEquals("k:96", Controls.mappings(ps1Game, global, overrides)[a])
        assertEquals(tennis, Controls.mappingSource(tennis, a, global, overrides))
        assertEquals(n64, Controls.mappingSource(kart, a, global, overrides))
        assertEquals(ControlLevel.AllConsoles, Controls.mappingSource(kart, b, global, overrides))
    }

    @Test
    fun aGlobalChangeReachesEveryLevelThatDoesntChangeTheButton() {
        val overrides = ControlOverrides(mappings = mapOf("Nintendo 64" to mapOf(a to "k:100")))
        val changedGlobal = global + (b to "k:108")
        assertEquals("k:108", Controls.mappings(tennis, changedGlobal, overrides)[b])
        assertEquals("k:100", Controls.mappings(tennis, changedGlobal, overrides)[a])
    }

    @Test
    fun bindingAKeyAnotherButtonUsesSwapsThemWithinTheLevel() {
        // At Mario Tennis, bind A to B's key: B takes A's old key, for that game only.
        val changes = Controls.bindButton(tennis, a, "k:97", global, ControlOverrides())
        assertEquals(mapOf(a to "k:97", b to "k:96"), changes)
        val overrides = ControlOverrides(mappings = mapOf(tennis.scope!! to changes.mapValues { it.value!! }))
        assertEquals("k:96", Controls.mappings(tennis, global, overrides)[b])
        assertEquals("k:97", Controls.mappings(kart, global, overrides)[b])
    }

    @Test
    fun aValueEqualToTheInheritedOneIsStoredAsInherit() {
        val overrides = ControlOverrides(mappings = mapOf(tennis.scope!! to mapOf(a to "k:97", b to "k:96")))
        // Swapping them back restores what the console level gives, so the game keeps nothing.
        val changes = Controls.bindButton(tennis, a, "k:96", global, overrides)
        assertEquals(mapOf(a to null, b to null), changes)
    }

    @Test
    fun unbindingDiffersFromInheriting() {
        val unbind = Controls.unbindButton(tennis, x, global, ControlOverrides())
        assertEquals(mapOf(x to ""), unbind)
        val overrides = ControlOverrides(mappings = mapOf(tennis.scope!! to mapOf(x to "")))
        assertNull(Controls.mappings(tennis, global, overrides)[x])
        assertEquals(tennis, Controls.mappingSource(tennis, x, global, overrides))
        assertEquals("k:99", Controls.mappings(kart, global, overrides)[x])
        // Unbinding what nothing above binds is the same as inheriting.
        assertEquals(mapOf(1 shl 7 to null), Controls.unbindButton(tennis, 1 shl 7, global, ControlOverrides()))
    }

    @Test
    fun hotkeysResolveAndUnbindTheSameWay() {
        val globalHotkeys = mapOf("pause" to listOf(101, 96), "save" to listOf(101, 103))
        val overrides = ControlOverrides(hotkeys = mapOf(
            "Nintendo 64" to mapOf("save" to listOf(102, 103)),
            tennis.scope!! to mapOf("pause" to emptyList()),
        ))
        val resolved = Controls.hotkeys(tennis, globalHotkeys, overrides)
        assertEquals(listOf(102, 103), resolved["save"])
        assertFalse(resolved.containsKey("pause"))
        assertEquals(listOf(101, 96), Controls.hotkeys(kart, globalHotkeys, overrides)["pause"])
        assertEquals(tennis, Controls.hotkeySource(tennis, "pause", globalHotkeys, overrides))
        // Setting a game's combo to the one its console gives stores nothing.
        assertNull(Controls.hotkeyChange(tennis, "save", listOf(102, 103), globalHotkeys, overrides))
        assertEquals(emptyList<Int>(), Controls.hotkeyChange(kart, "pause", emptyList(), globalHotkeys, overrides))
    }

    @Test
    fun levelsReportWhatTheyChangeThemselves() {
        val overrides = ControlOverrides(
            mappings = mapOf(tennis.scope!! to mapOf(x to "")),
            hotkeys = mapOf("Nintendo 64" to mapOf("save" to listOf(102, 103))),
        )
        assertTrue(overrides.changesMapping(tennis, x))
        assertFalse(overrides.changesMapping(kart, x))
        assertFalse(overrides.changesMapping(ControlLevel.AllConsoles, x))
        assertTrue(overrides.hasMappings(tennis))
        assertFalse(overrides.hasMappings(n64))
        assertTrue(overrides.changesHotkey(n64, "save"))
        assertFalse(overrides.changesHotkey(tennis, "save"))
        assertTrue(overrides.hasHotkeys(n64))
        assertFalse(overrides.hasHotkeys(ControlLevel.AllConsoles))
    }

    @Test
    fun consoleButtonNamesJoinTheButtonsOneBitPresses() {
        // Neo Geo: pad R1 presses A and B together.
        val names = Controls.buttonNames(listOf("64\tA", "512\tA", "1024\tA", "128\tB", "512\tB", "512\tB", "junk", "16\t", "\tC"))
        assertEquals(mapOf(64 to "A", 512 to "A + B", 1024 to "A", 128 to "B"), names)
    }

    @Test
    fun scopedKeysParseBackAndStayClearOfTheGlobalPrefixes() {
        val key = ScopedKeys.mapping("Nintendo 64/Odd | Name.z64", a)
        assertEquals("Nintendo 64/Odd | Name.z64" to a.toString(), ScopedKeys.parse(key, ScopedKeys.MAPPING_PREFIX))
        assertEquals("Nintendo 64" to "pause", ScopedKeys.parse(ScopedKeys.hotkey("Nintendo 64", "pause"), ScopedKeys.HOTKEY_PREFIX))
        assertFalse(key.startsWith("mapping_"))
        assertFalse(ScopedKeys.hotkey("Nintendo 64", "pause").startsWith("hotkey_combo_"))
        assertNull(ScopedKeys.parse("mapping_16", ScopedKeys.MAPPING_PREFIX))
    }
}
