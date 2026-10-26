package com.phobos.emulator.ui

import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ZxKeyPressesTest {
    /** The core's keyboard matrix: the keys it holds, every call in order, and what else was held as each went down. */
    private class FakeCore : ZxKeyboardCore {
        val down = mutableSetOf<String>()
        val calls = mutableListOf<String>()
        val heldAsPressed = mutableListOf<Pair<String, Set<String>>>()

        override fun setKey(label: String, pressed: Boolean) {
            calls += if (pressed) "press $label" else "release $label"
            if (pressed) {
                heldAsPressed += label to down.toSet()
                down += label
            } else {
                down -= label
            }
        }

        override fun releaseAll() {
            calls += "release all"
            down.clear()
        }
    }

    private val core = FakeCore()
    private val presses = ZxKeyPresses(core, pause = {})

    // LOAD "" as the 48K types it: J (LOAD at the start of a line), SYMBOL SHIFT + P twice (the quotes), ENTER.
    private val load = listOf(listOf("J"), listOf("SYMBOL SHIFT", "P"), listOf("SYMBOL SHIFT", "P"), listOf("ENTER"))

    @Test
    fun aPressLetsGoOfItsKeyWhenTheFingerLifts() = runBlocking {
        presses.hold(listOf("SPACE BREAK")) { assertEquals(setOf("SPACE BREAK"), core.down) }
        assertEquals(emptySet<String>(), core.down)
    }

    @Test
    fun aPressCancelledWithTheFingerStillDownLetsGoOfItsKey() = runBlocking {
        // The keyboard goes away (hidden by a hotkey) while a finger is still on SPACE: its gesture is cancelled.
        val gesture = launch(start = CoroutineStart.UNDISPATCHED) {
            presses.hold(listOf("SPACE BREAK")) { awaitCancellation() }
        }
        assertEquals(setOf("SPACE BREAK"), core.down)
        gesture.cancelAndJoin()
        assertEquals(emptySet<String>(), core.down)
    }

    @Test
    fun aChordWhoseGestureThrowsLetsGoOfBothKeys() = runBlocking {
        val thrown = runCatching {
            presses.hold(listOf("CAPS SHIFT", "SPACE BREAK")) { throw IllegalStateException("gesture failed") }
        }.exceptionOrNull()
        assertTrue(thrown is IllegalStateException)
        assertEquals(emptySet<String>(), core.down)
    }

    @Test
    fun loadCancelledMidStepLetsGoOfThatStepsKeys() = runBlocking {
        // Cancelled while SYMBOL SHIFT and P are down for the first quote.
        val typing = ZxKeyPresses(core, pause = { if ("P" in core.down) awaitCancellation() })
        val gesture = launch(start = CoroutineStart.UNDISPATCHED) { typing.type(load) {} }
        assertEquals(setOf("SYMBOL SHIFT", "P"), core.down)
        gesture.cancelAndJoin()
        assertEquals(emptySet<String>(), core.down)
    }

    @Test
    fun goingAwayLetsGoOfHeldKeysButNotTheShiftLatches() = runBlocking {
        presses.latch("SYMBOL SHIFT", true)
        presses.latch("CAPS SHIFT", true)
        val finger = launch(start = CoroutineStart.UNDISPATCHED) { presses.hold(listOf("M")) { awaitCancellation() } }
        assertEquals(setOf("SYMBOL SHIFT", "CAPS SHIFT", "M"), core.down)
        // What the keyboard does as it leaves the composition.
        presses.releaseHeld()
        assertEquals(setOf("SYMBOL SHIFT", "CAPS SHIFT"), core.down)
        // The cancelled gesture's own ending comes after, and leaves the latches alone too.
        finger.cancelAndJoin()
        assertEquals(setOf("SYMBOL SHIFT", "CAPS SHIFT"), core.down)
    }

    @Test
    fun loadLetsGoOfEveryHeldKeyBeforeItsFirstTypedKey() = runBlocking {
        // A thumb resting on this keyboard's SPACE, which the 48K's tape loader takes as BREAK, and ENTER held from
        // outside this keyboard (the touch controls have ENTER and SPACE keys of their own).
        val thumb = launch(start = CoroutineStart.UNDISPATCHED) {
            presses.hold(listOf("SPACE BREAK")) { awaitCancellation() }
        }
        core.setKey("ENTER", true)
        presses.type(load) { core.calls += "clear shifts" }
        // When J, the first typed key, goes down, nothing else is held.
        assertEquals("J" to emptySet<String>(), core.heldAsPressed.first { it.first == "J" })
        assertTrue(core.calls.indexOf("clear shifts") < core.calls.indexOf("press J"))
        thumb.cancelAndJoin()
        assertEquals(emptySet<String>(), core.down)
    }

    @Test
    fun loadTypesEachCharacterAsAPressOfItsOwn() = runBlocking {
        presses.type(load) {}
        // Each step's keys go down together, with the step before it already let go of: J, then the quote twice
        // (P with SYMBOL SHIFT held), then ENTER.
        val expected = listOf(
            "J" to emptySet(),
            "SYMBOL SHIFT" to emptySet(),
            "P" to setOf("SYMBOL SHIFT"),
            "SYMBOL SHIFT" to emptySet(),
            "P" to setOf("SYMBOL SHIFT"),
            "ENTER" to emptySet<String>(),
        )
        assertEquals(expected, core.heldAsPressed)
        assertEquals(emptySet<String>(), core.down)
    }
}
