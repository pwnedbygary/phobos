package com.phobos.emulator.input

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class HotkeyComboTest {
    private val save = HotkeyAction.SAVE_STATE
    private val pause = HotkeyAction.PAUSE
    private val combo = listOf(SELECT, R1, X, Y)

    @Test
    fun arbitraryLengthComboMatchesExactlyAndOnlyWithItsModifierFirst() {
        val hotkeys = mapOf(save to combo)
        val tracker = HotkeyTracker()
        combo.forEachIndexed { index, key ->
            assertFalse("key $index is not consumed before the combo is complete", tracker.matches(hotkeys).isNotEmpty())
            assertTrue("the first key and every action key are consumed", tracker.press(hotkeys, key))
        }
        assertEquals(listOf(save), tracker.matches(hotkeys))
        assertEquals(combo.toSet(), tracker.consumedTokens)
    }

    @Test
    fun actionKeysStillWorkAloneAndBeforeTheModifier() {
        val hotkeys = mapOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        assertFalse("R1 alone is the game's", tracker.press(hotkeys, R1))
        assertTrue("Select is the modifier and is always consumed", tracker.press(hotkeys, SELECT))
        assertTrue("the modifier pressed after the action key does not complete the combo", tracker.matches(hotkeys).isEmpty())
    }

    @Test
    fun aReleasedComboCanBePressedAgain() {
        val hotkeys = mapOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, SELECT)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.matches(hotkeys))
        assertTrue(tracker.release(R1))
        assertTrue(tracker.release(SELECT))
        tracker.press(hotkeys, SELECT)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.matches(hotkeys))
    }

    @Test
    fun aStickDirectionCountsOnlyWhenACombosUsesIt() {
        val stickUp = HotkeyTokens.axis(1, positive = false)
        // No combo uses the stick: moving it doesn't keep Select + R1 from matching.
        val hotkeys = mapOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        assertFalse(tracker.syncExternal(hotkeys, setOf(stickUp)))
        tracker.press(hotkeys, SELECT)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.matches(hotkeys))
        // A combo that uses it: the direction joins the keys held and completes the combo.
        val withStick = mapOf(save to listOf(SELECT, stickUp))
        val other = HotkeyTracker()
        other.press(withStick, SELECT)
        assertTrue(other.syncExternal(withStick, setOf(stickUp)))
        assertEquals(listOf(save), other.matches(withStick))
        assertFalse("a let-go presses nothing", other.syncExternal(withStick, emptySet()))
        assertTrue(other.matches(withStick).isEmpty())
    }

    @Test
    fun conflictsCoverEqualPrefixAndCrossRoleBindings() {
        val hotkeys = mapOf(
            HotkeyAction.SAVE_STATE to listOf(SELECT, R1),
            HotkeyAction.PAUSE to listOf(SELECT, START, R1),
            HotkeyAction.LIBRARY to listOf(R1, X),
            HotkeyAction.MUTE to listOf(R1, Y),
        )
        // Select + R1 is Save's exact combo, a prefix of Pause's Select + Start + R1, and R1 is
        // the modifier of the Library and Mute combos: all four are conflicts.
        val sameAsSave = hotkeyConflicts(hotkeys, HotkeyAction.RESET, listOf(SELECT, R1))
        assertEquals(
            listOf(
                HotkeyConflict(HotkeyAction.SAVE_STATE, HotkeyConflict.Kind.EXACT),
                HotkeyConflict(HotkeyAction.PAUSE, HotkeyConflict.Kind.OVERLAP),
                HotkeyConflict(HotkeyAction.LIBRARY, HotkeyConflict.Kind.CROSS_ROLE),
                HotkeyConflict(HotkeyAction.MUTE, HotkeyConflict.Kind.CROSS_ROLE),
            ),
            sameAsSave,
        )
        // A longer combo contains Save's and the Library combo's: those are overlaps, and R1 is
        // still the Mute combo's modifier.
        assertEquals(
            setOf(HotkeyAction.SAVE_STATE, HotkeyAction.LIBRARY, HotkeyAction.MUTE),
            hotkeyConflicts(hotkeys, HotkeyAction.RESET, listOf(SELECT, R1, X)).map { it.action }.toSet(),
        )
        // The same keys in another order are the same combo.
        assertEquals(
            HotkeyConflict.Kind.EXACT,
            hotkeyConflicts(hotkeys, HotkeyAction.RESET, listOf(X, R1)).first { it.kind == HotkeyConflict.Kind.EXACT }.kind,
        )
        // A combo that shares only the modifier with the others is fine.
        assertTrue(hotkeyConflicts(hotkeys, HotkeyAction.RESET, listOf(SELECT, X)).isEmpty())
    }

    @Test
    fun aTapAndTheCombosItsKeyLeadsDontConflictUnlessTheTapIsAHoldAction() {
        val load = HotkeyAction.LOAD_STATE
        val hotkeys = mapOf(save to listOf(MODE, R1), load to listOf(MODE, L1))
        assertTrue(hotkeyConflicts(hotkeys, pause, listOf(MODE)).isEmpty())
        // The other way round: a combo bound with the tap that leads it already there.
        assertTrue(hotkeyConflicts(mapOf(pause to listOf(MODE)), save, listOf(MODE, R1)).isEmpty())
        // A hold can't wait for its key's release, so it collides with the combos its key starts.
        assertEquals(
            listOf(HotkeyConflict(save, HotkeyConflict.Kind.OVERLAP), HotkeyConflict(load, HotkeyConflict.Kind.OVERLAP)),
            hotkeyConflicts(hotkeys, HotkeyAction.FAST_FORWARD_HOLD, listOf(MODE)),
        )
    }

    @Test
    fun aKeyHeldAsAKeyEventAndAsAHatStaysHeldUntilBothLetGo() {
        val hotkeys = mapOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, SELECT)
        tracker.press(hotkeys, R1)
        tracker.syncExternal(hotkeys, setOf(R1))
        tracker.release(R1)
        assertEquals("the hat still holds the key", listOf(save), tracker.matches(hotkeys))
        tracker.syncExternal(hotkeys, emptySet())
        assertTrue("nothing is held any more", tracker.matches(hotkeys).isEmpty())
    }

    @Test
    fun aButtonReportedAsAKeyAndAsAnAxisCompletesItsComboOnceEitherWayRound() {
        val hotkeys = mapOf(HotkeyAction.FAST_FORWARD_TOGGLE to listOf(MODE, R2))
        val keyFirst = HotkeyTracker()
        keyFirst.press(hotkeys, MODE)
        keyFirst.press(hotkeys, R2)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_TOGGLE), keyFirst.completions(hotkeys))
        assertFalse("the axis presses nothing new", keyFirst.syncExternal(hotkeys, setOf(R2)))
        assertTrue(keyFirst.completions(hotkeys).isEmpty())

        val axisFirst = HotkeyTracker()
        axisFirst.press(hotkeys, MODE)
        assertTrue(axisFirst.syncExternal(hotkeys, setOf(R2)))
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_TOGGLE), axisFirst.completions(hotkeys))
        assertTrue("taken, as the axis's press was", axisFirst.press(hotkeys, R2))
        assertTrue("the key presses nothing new", axisFirst.completions(hotkeys).isEmpty())
    }

    @Test
    fun aReleaseCompletesNoComboEvenWhenWhatsLeftIsOne() {
        val frameAdvance = HotkeyAction.FRAME_ADVANCE
        val hotkeys = mapOf(save to listOf(MODE, R1), frameAdvance to listOf(MODE, L3))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.completions(hotkeys))
        tracker.press(hotkeys, L3)
        assertTrue("Mode + R1 + L3 is no combo", tracker.completions(hotkeys).isEmpty())
        tracker.release(R1)
        assertEquals("Mode + L3 is held", listOf(frameAdvance), tracker.matches(hotkeys))
        assertTrue("but no press completed it", tracker.completions(hotkeys).isEmpty())
    }

    @Test
    fun theConflictQuestionUsesTheAlreadyBoundSentenceWheneverACombosTheSame() {
        val names = mapOf(save to "Save State", HotkeyAction.LOAD_STATE to "Load State", HotkeyAction.MUTE to "Mute Audio")
        fun message(vararg conflicts: HotkeyConflict) = hotkeyConflictMessage(conflicts.toList()) { names.getValue(it) }
        val exactSave = HotkeyConflict(save, HotkeyConflict.Kind.EXACT)
        assertEquals("The selected hotkey combo is already bound to Save State, would you like to overwrite?", message(exactSave))
        assertEquals(
            "The selected hotkey combo is already bound to Save State, would you like to overwrite? " +
                "It also overlaps Load State and Mute Audio, which would be unbound too.",
            message(
                HotkeyConflict(HotkeyAction.LOAD_STATE, HotkeyConflict.Kind.OVERLAP),
                exactSave,
                HotkeyConflict(HotkeyAction.MUTE, HotkeyConflict.Kind.CROSS_ROLE),
            ),
        )
        assertEquals(
            "The selected hotkey combo is already bound to Save State and Load State, would you like to overwrite?",
            message(exactSave, HotkeyConflict(HotkeyAction.LOAD_STATE, HotkeyConflict.Kind.EXACT)),
        )
        assertEquals(
            "The selected hotkey combo overlaps Load State, would you like to overwrite?",
            message(HotkeyConflict(HotkeyAction.LOAD_STATE, HotkeyConflict.Kind.OVERLAP)),
        )
    }

    @Test
    fun anAxisTokenIsPressedPastHalfwayAndLetGoUnderFourTenths() {
        fun tokens(trigger: Float, stickX: Float, held: Set<Int> = emptySet()) =
            CapturePolicy.hotkeyAxisTokens({ axis -> mapOf(AXIS_LTRIGGER to trigger, AXIS_X to stickX)[axis] ?: 0f }, held).toSet()
        val stickRight = HotkeyTokens.axis(AXIS_X, positive = true)
        assertEquals(setOf(L2, stickRight), tokens(0.55f, 0.55f))
        assertTrue("not yet pressed", tokens(0.45f, 0.45f).isEmpty())
        assertEquals("still held", setOf(L2, stickRight), tokens(0.45f, 0.45f, held = setOf(L2, stickRight)))
        assertTrue("let go", tokens(0.35f, 0.35f, held = setOf(L2, stickRight)).isEmpty())
        assertEquals("the other way is another token", setOf(HotkeyTokens.axis(AXIS_X, positive = false)), tokens(0f, -0.6f))
    }

    @Test
    fun aKeyAloneThatLeadsCombosFiresWhenTappedNotWhenPressed() {
        val hotkeys = mapOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        assertTrue("Mode starts combos: never the game's", tracker.press(hotkeys, MODE))
        assertTrue("nothing fires on the press", tracker.matches(hotkeys).isEmpty())
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
        assertTrue("a tap is reported once", tracker.taps(hotkeys).isEmpty())
    }

    @Test
    fun aTapIsOffWhenAnotherKeyIsPressedMeanwhileOrWasAlreadyHeld() {
        val hotkeys = mapOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.matches(hotkeys))
        tracker.release(R1)
        tracker.release(MODE)
        assertTrue("Mode + R1 is no tap", tracker.taps(hotkeys).isEmpty())

        tracker.press(hotkeys, A)
        tracker.press(hotkeys, MODE)
        tracker.release(MODE)
        assertTrue("Mode pressed with A already held is no tap", tracker.taps(hotkeys).isEmpty())
        tracker.release(A)

        // A hat direction pressed meanwhile is another key too.
        tracker.press(hotkeys, MODE)
        tracker.syncExternal(hotkeys, setOf(DPAD_RIGHT))
        tracker.syncExternal(hotkeys, emptySet())
        tracker.release(MODE)
        assertTrue(tracker.taps(hotkeys).isEmpty())
    }

    @Test
    fun repeatsOfAHeldKeyChangeNothing() {
        val hotkeys = mapOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        assertTrue("a repeat keeps what its press decided", tracker.press(hotkeys, MODE))
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
    }

    @Test
    fun aOneKeyComboThatStartsNothingElseFiresOnItsPress() {
        val hotkeys = mapOf(HotkeyAction.SCREENSHOT to listOf(F8), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, F8)
        assertEquals(listOf(HotkeyAction.SCREENSHOT), tracker.matches(hotkeys))
        tracker.release(F8)
        assertTrue(tracker.taps(hotkeys).isEmpty())
    }

    @Test
    fun theDefaultsLeaveSelectToTheGameAndDontCollide() {
        assertTrue("no default uses Select", DEFAULT_HOTKEYS.values.none { SELECT in it })
        assertTrue("every default starts with Mode", DEFAULT_HOTKEYS.values.all { it.first() == MODE })
        assertEquals("Pause is a Mode tap", listOf(MODE), DEFAULT_HOTKEYS[pause])
        assertTrue(existingHotkeyConflicts(DEFAULT_HOTKEYS).isEmpty())
        val unbound = listOf(HotkeyAction.RESET, HotkeyAction.QUIT, HotkeyAction.FAST_FORWARD_HOLD) + HotkeyAction.DIRECT_SLOTS
        for (action in unbound) assertNull(action, DEFAULT_HOTKEYS[action])
    }

    @Test
    fun existingConflictsNameEachPairOnce() {
        val hotkeys = mapOf(save to listOf(MODE, R1), HotkeyAction.LOAD_STATE to listOf(MODE, L1), HotkeyAction.SCREENSHOT to listOf(MODE, R1))
        assertEquals(listOf(save to HotkeyAction.SCREENSHOT), existingHotkeyConflicts(hotkeys))
    }

    @Test
    fun aButtonBindingsTokensAreTheKeysAHotkeyWouldTakeFromIt() {
        assertEquals(listOf(SELECT), bindingTokens("k:$SELECT"))
        assertEquals(listOf(L2), bindingTokens("a:$AXIS_LTRIGGER:1"))
        assertEquals(listOf(R2), bindingTokens("a:$AXIS_GAS:1"))
        assertEquals(listOf(DPAD_RIGHT), bindingTokens("a:$AXIS_HAT_X:1"))
        assertEquals(listOf(HotkeyTokens.axis(1, positive = false)), bindingTokens("a:1:0"))
        assertTrue(bindingTokens("junk").isEmpty())
    }

    @Test
    fun axisTokensRoundTripAndDoNotCollideWithKeycodes() {
        val tokens = (0 until 64).flatMap { axis ->
            listOf(HotkeyTokens.axis(axis, positive = true), HotkeyTokens.axis(axis, positive = false))
        }
        assertEquals(tokens.size, tokens.toSet().size)
        assertTrue(tokens.all { it < 0 })
        for (axis in 0 until 64) {
            assertEquals(axis to true, HotkeyTokens.axisOf(HotkeyTokens.axis(axis, true)))
            assertEquals(axis to false, HotkeyTokens.axisOf(HotkeyTokens.axis(axis, false)))
        }
        assertNull(HotkeyTokens.axisOf(SELECT))
    }

    companion object {
        // KeyEvent and MotionEvent constants (android.view).
        const val A = 96
        const val X = 99
        const val Y = 100
        const val L1 = 102
        const val R1 = 103
        const val L2 = 104
        const val R2 = 105
        const val L3 = 106
        const val START = 108
        const val SELECT = 109
        const val MODE = 110
        const val F8 = 138
        const val DPAD_RIGHT = 22
        const val AXIS_X = 0
        const val AXIS_HAT_X = 15
        const val AXIS_LTRIGGER = 17
        const val AXIS_GAS = 22
    }
}
