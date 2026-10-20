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
        val globalHotkeys = hotkeysOf("pause" to listOf(101, 96), "save" to listOf(101, 103))
        val overrides = ControlOverrides(hotkeys = mapOf(
            "Nintendo 64" to hotkeysOf("save" to listOf(102, 103)),
            tennis.scope!! to mapOf("pause" to emptyList()),
        ))
        val resolved = Controls.hotkeys(tennis, globalHotkeys, overrides)
        assertEquals(listOf(listOf(102, 103)), resolved["save"])
        assertFalse(resolved.containsKey("pause"))
        assertEquals(listOf(listOf(101, 96)), Controls.hotkeys(kart, globalHotkeys, overrides)["pause"])
        assertEquals(tennis, Controls.hotkeySource(tennis, "pause", globalHotkeys, overrides))
        // Setting a game's combo to the one its console gives stores nothing.
        assertNull(Controls.hotkeyChange(tennis, "save", listOf(listOf(102, 103)), globalHotkeys, overrides))
        assertEquals(emptyList<List<Int>>(), Controls.hotkeyChange(kart, "pause", emptyList(), globalHotkeys, overrides))
    }

    @Test
    fun overwritingClearsOnlyTheCollidingBindingAtEachLevel() {
        val save = HotkeyAction.SAVE_STATE
        val screenshot = HotkeyAction.SCREENSHOT
        val global = mapOf(save to listOf(listOf(MODE, R1), listOf(F5)), screenshot to listOf(listOf(MODE, R3), listOf(F8)))
        // All consoles: F8 as Save's third binding takes it from Screenshot, whose first binding stays.
        assertEquals(
            mapOf(save to listOf(listOf(MODE, R1), listOf(F5), listOf(F8)), screenshot to listOf(listOf(MODE, R3))),
            Controls.bindHotkey(ControlLevel.AllConsoles, save, 2, listOf(F8), listOf(screenshot to 1), global, ControlOverrides()),
        )
        // A console overwriting a binding it inherits keeps the action's others, in their slots, as its own.
        val console = Controls.bindHotkey(n64, save, 0, listOf(MODE, R3), listOf(screenshot to 0), global, ControlOverrides())
        assertEquals(mapOf(save to listOf(listOf(MODE, R3), listOf(F5)), screenshot to listOf(emptyList(), listOf(F8))), console)
        val stored = console.mapValues { (action, bindings) -> Controls.hotkeyChange(n64, action, bindings, global, ControlOverrides()) }
        assertEquals("both differ from all consoles'", console, stored)
        val overrides = ControlOverrides(hotkeys = mapOf(n64.scope!! to console))
        assertEquals("the console's games follow it", listOf(emptyList(), listOf(F8)), Controls.hotkeys(tennis, global, overrides)[screenshot])
        // A game taking F5 from Save leaves it the console's Mode + R3; binding back what the console gives stores nothing.
        val game = Controls.bindHotkey(tennis, HotkeyAction.LOAD_STATE, 0, listOf(F5), listOf(save to 1), global, overrides)
        assertEquals(mapOf(HotkeyAction.LOAD_STATE to listOf(listOf(F5)), save to listOf(listOf(MODE, R3))), game)
        assertEquals(listOf(listOf(MODE, R3)), Controls.hotkeyChange(tennis, save, game.getValue(save), global, overrides))
        val same = Controls.bindHotkey(tennis, save, 1, listOf(F5), emptyList(), global, overrides)
        assertNull(Controls.hotkeyChange(tennis, save, same.getValue(save), global, overrides))
    }

    @Test
    fun aKeyInBacksPlaceIsStoredAtEveryLevelAtOnceUnlessAnythingAnywhereIsInTheWay() {
        val save = HotkeyAction.SAVE_STATE
        val screenshot = HotkeyAction.SCREENSHOT
        val quit = HotkeyAction.QUIT
        // All consoles' as the store gives them: Save as a default leaves it (Back + R1, F5), Screenshot stored as F8.
        val global = mapOf(save to listOf(listOf(BACK, R1), listOf(F5)), screenshot to listOf(listOf(F8)))
        // The Nintendo 64 stores Quit as Back + R3; Mario Tennis unbinds Save and stores Screenshot with Back + Y.
        val overrides = ControlOverrides(
            hotkeys = mapOf(
                n64.scope!! to mapOf(quit to listOf(listOf(BACK, R3))),
                tennis.scope!! to mapOf(save to emptyList(), screenshot to listOf(listOf(F8), listOf(BACK, Y))),
            ),
        )
        val swap = Controls.replaceHotkeyLeadEverywhere(BACK, MODE, global, overrides)
        assertEquals("all consoles store Save whole; Screenshot, Back not in it, stays", mapOf(save to listOf(listOf(MODE, R1), listOf(F5))), swap.global)
        assertEquals(
            "each level what it stores itself, whole; the game's unbinding stays",
            mapOf(n64.scope!! to mapOf(quit to listOf(listOf(MODE, R3))), tennis.scope!! to mapOf(screenshot to listOf(listOf(F8), listOf(MODE, Y)))),
            swap.scoped,
        )
        assertTrue(swap.blockers.isEmpty())
        // R3 is in the console's Quit, which would lose it (in Mario Tennis too, which inherits it): nothing is stored
        // anywhere, and the console's Quit is named, where it's set.
        val lost = Controls.replaceHotkeyLeadEverywhere(BACK, R3, global, overrides)
        assertEquals(listOf(n64 to quit), lost.blockers)
        assertTrue(lost.global.isEmpty() && lost.scoped.isEmpty())
        // A game's own Mute on Mode + R1, where all consoles' Save would land: the game's Mute is named.
        val clashing = ControlOverrides(hotkeys = mapOf(tennis.scope!! to mapOf(HotkeyAction.MUTE to listOf(listOf(MODE, R1)))))
        assertEquals(listOf(tennis to HotkeyAction.MUTE), Controls.replaceHotkeyLeadEverywhere(BACK, MODE, global, clashing).blockers)
        // A stored scope reads back as its level, a game's ROM name holding "|" included.
        assertEquals(tennis, ControlLevel.of(tennis.scope!!))
        assertEquals(n64, ControlLevel.of(n64.scope!!))
        assertEquals(ControlLevel.Game("Nintendo 64", "Odd | Name.z64"), ControlLevel.of("Nintendo 64/Odd | Name.z64"))
    }

    @Test
    fun levelsReportWhatTheyChangeThemselves() {
        val overrides = ControlOverrides(
            mappings = mapOf(tennis.scope!! to mapOf(x to "")),
            hotkeys = mapOf("Nintendo 64" to hotkeysOf("save" to listOf(102, 103))),
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

    companion object {
        // KeyEvent constants (android.view).
        const val BACK = 4
        const val Y = 100
        const val R1 = 103
        const val R3 = 107
        const val MODE = 110
        const val F5 = 135
        const val F8 = 138
    }
}
