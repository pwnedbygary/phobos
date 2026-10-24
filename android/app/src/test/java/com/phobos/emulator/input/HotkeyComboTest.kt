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
        val hotkeys = hotkeysOf(save to combo)
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
        val hotkeys = hotkeysOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        assertFalse("R1 alone is the game's", tracker.press(hotkeys, R1))
        assertTrue("Select is the modifier and is always consumed", tracker.press(hotkeys, SELECT))
        assertTrue("the modifier pressed after the action key does not complete the combo", tracker.matches(hotkeys).isEmpty())
    }

    @Test
    fun aReleasedComboCanBePressedAgain() {
        val hotkeys = hotkeysOf(save to listOf(SELECT, R1))
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
        val hotkeys = hotkeysOf(save to listOf(SELECT, R1))
        val tracker = HotkeyTracker()
        assertFalse(tracker.syncExternal(hotkeys, setOf(stickUp)))
        tracker.press(hotkeys, SELECT)
        tracker.press(hotkeys, R1)
        assertEquals(listOf(save), tracker.matches(hotkeys))
        // A combo that uses it: the direction joins the keys held and completes the combo.
        val withStick = hotkeysOf(save to listOf(SELECT, stickUp))
        val other = HotkeyTracker()
        other.press(withStick, SELECT)
        assertTrue(other.syncExternal(withStick, setOf(stickUp)))
        assertEquals(listOf(save), other.matches(withStick))
        assertFalse("a let-go presses nothing", other.syncExternal(withStick, emptySet()))
        assertTrue(other.matches(withStick).isEmpty())
    }

    @Test
    fun conflictsCoverEqualPrefixAndCrossRoleBindings() {
        val hotkeys = hotkeysOf(
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
                HotkeyConflict(HotkeyAction.SAVE_STATE, 0, HotkeyConflict.Kind.EXACT),
                HotkeyConflict(HotkeyAction.PAUSE, 0, HotkeyConflict.Kind.OVERLAP),
                HotkeyConflict(HotkeyAction.LIBRARY, 0, HotkeyConflict.Kind.CROSS_ROLE),
                HotkeyConflict(HotkeyAction.MUTE, 0, HotkeyConflict.Kind.CROSS_ROLE),
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
        val hotkeys = hotkeysOf(save to listOf(MODE, R1), load to listOf(MODE, L1))
        assertTrue(hotkeyConflicts(hotkeys, pause, listOf(MODE)).isEmpty())
        // The other way round: a combo bound with the tap that leads it already there.
        assertTrue(hotkeyConflicts(hotkeysOf(pause to listOf(MODE)), save, listOf(MODE, R1)).isEmpty())
        // A hold can't wait for its key's release, so it collides with the combos its key starts.
        assertEquals(
            listOf(HotkeyConflict(save, 0, HotkeyConflict.Kind.OVERLAP), HotkeyConflict(load, 0, HotkeyConflict.Kind.OVERLAP)),
            hotkeyConflicts(hotkeys, HotkeyAction.FAST_FORWARD_HOLD, listOf(MODE)),
        )
    }

    @Test
    fun aKeyHeldAsAKeyEventAndAsAHatStaysHeldUntilBothLetGo() {
        val hotkeys = hotkeysOf(save to listOf(SELECT, R1))
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
        val hotkeys = hotkeysOf(HotkeyAction.FAST_FORWARD_TOGGLE to listOf(MODE, R2))
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
        val hotkeys = hotkeysOf(save to listOf(MODE, R1), frameAdvance to listOf(MODE, L3))
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
        val exactSave = HotkeyConflict(save, 0, HotkeyConflict.Kind.EXACT)
        assertEquals("The selected hotkey combo is already bound to Save State, would you like to overwrite?", message(exactSave))
        assertEquals(
            "The selected hotkey combo is already bound to Save State, would you like to overwrite? " +
                "It also overlaps Load State and Mute Audio, which would lose those bindings too.",
            message(
                HotkeyConflict(HotkeyAction.LOAD_STATE, 0, HotkeyConflict.Kind.OVERLAP),
                exactSave,
                HotkeyConflict(HotkeyAction.MUTE, 1, HotkeyConflict.Kind.CROSS_ROLE),
            ),
        )
        assertEquals(
            "The selected hotkey combo is already bound to Save State, would you like to overwrite? " +
                "It also overlaps Load State, which would lose that binding too.",
            message(exactSave, HotkeyConflict(HotkeyAction.LOAD_STATE, 2, HotkeyConflict.Kind.OVERLAP)),
        )
        assertEquals(
            "The selected hotkey combo is already bound to Save State and Load State, would you like to overwrite?",
            message(exactSave, HotkeyConflict(HotkeyAction.LOAD_STATE, 0, HotkeyConflict.Kind.EXACT)),
        )
        assertEquals(
            "The selected hotkey combo overlaps Load State, would you like to overwrite?",
            message(HotkeyConflict(HotkeyAction.LOAD_STATE, 0, HotkeyConflict.Kind.OVERLAP)),
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
        val hotkeys = hotkeysOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        assertTrue("Mode starts combos: never the game's", tracker.press(hotkeys, MODE))
        assertTrue("nothing fires on the press", tracker.matches(hotkeys).isEmpty())
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
        assertTrue("a tap is reported once", tracker.taps(hotkeys).isEmpty())
    }

    @Test
    fun aKeyLetGoOfWithNothingElsePressedMeanwhileWasLetGoOfAloneTapBindingOrNot() {
        val hotkeys = hotkeysOf(save to listOf(BACK, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, BACK)
        tracker.release(BACK)
        assertTrue("Back, which only leads a combo, let go of alone", tracker.letGoAlone(BACK))
        assertFalse("another key wasn't", tracker.letGoAlone(R1))
        tracker.press(hotkeys, BACK)
        tracker.press(hotkeys, A)
        tracker.release(A)
        tracker.release(BACK)
        assertFalse("Back with A pressed meanwhile wasn't", tracker.letGoAlone(BACK))
        assertFalse("nor A, pressed with Back held", tracker.letGoAlone(A))
        tracker.press(hotkeys, BACK)
        tracker.press(hotkeys, BACK) // a repeat
        tracker.release(BACK)
        assertTrue("a repeat is no other key", tracker.letGoAlone(BACK))
    }

    @Test
    fun aKeyNoKeyEventReportsPressedMeanwhileMakesAKeyLetGoOfNoTapAndNotAlone() {
        val hotkeys = hotkeysOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        tracker.pressedBeside() // a D-pad direction Android makes from a hat, say
        tracker.release(MODE)
        assertFalse("Mode wasn't let go of alone", tracker.letGoAlone(MODE))
        assertTrue("so it's no tap", tracker.taps(hotkeys).isEmpty())
        tracker.press(hotkeys, MODE)
        tracker.release(MODE)
        assertEquals("pressed alone again, it is", listOf(pause), tracker.taps(hotkeys))
    }

    @Test
    fun aTapIsOffWhenAnotherKeyIsPressedMeanwhileOrWasAlreadyHeld() {
        val hotkeys = hotkeysOf(pause to listOf(MODE), save to listOf(MODE, R1))
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
        val hotkeys = hotkeysOf(pause to listOf(MODE), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        assertTrue("a repeat keeps what its press decided", tracker.press(hotkeys, MODE))
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
    }

    @Test
    fun aOneKeyComboThatStartsNothingElseFiresOnItsPress() {
        val hotkeys = hotkeysOf(HotkeyAction.SCREENSHOT to listOf(F8), save to listOf(MODE, R1))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, F8)
        assertEquals(listOf(HotkeyAction.SCREENSHOT), tracker.matches(hotkeys))
        tracker.release(F8)
        assertTrue(tracker.taps(hotkeys).isEmpty())
    }

    @Test
    fun anyOfAnActionsBindingsRunsItAndTheActionIsReportedOnce() {
        val hotkeys = mapOf(
            pause to listOf(listOf(MODE), listOf(F12)),
            save to listOf(listOf(MODE, R1), listOf(F5), listOf(MODE, R1)),
        )
        val tracker = HotkeyTracker()
        assertTrue("F5 starts a binding: never the game's", tracker.press(hotkeys, F5))
        assertEquals(listOf(save), tracker.completions(hotkeys))
        tracker.release(F5)
        tracker.press(hotkeys, MODE)
        tracker.press(hotkeys, R1)
        assertEquals("two bindings match, the action is run once", listOf(save), tracker.completions(hotkeys))
        tracker.release(R1)
        tracker.release(MODE)
        assertTrue("Mode leads a combo, so it waits for its tap", tracker.taps(hotkeys).isEmpty())
        tracker.press(hotkeys, MODE)
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
        tracker.press(hotkeys, F12)
        assertEquals("F12 leads nothing, so it fires on its press", listOf(pause), tracker.completions(hotkeys))
    }

    @Test
    fun aHoldsOwnLongerBindingLeavesItsKeyAloneNoTapButAnotherActionsOrANonHoldsOwnMakesItOne() {
        val ffHold = HotkeyAction.FAST_FORWARD_HOLD
        val own = mapOf(ffHold to listOf(listOf(TAB), listOf(TAB, X)))
        assertFalse("Tab and Tab + X hold the same, so Tab needn't wait for its release", isTapCombo(own, listOf(TAB)))
        assertTrue("another action's longer combo", isTapCombo(own + hotkeysOf(save to listOf(TAB, R1)), listOf(TAB)))
        assertTrue(
            "a hold's longer binding still makes another action's key alone a tap",
            isTapCombo(mapOf(ffHold to listOf(listOf(TAB, X)), save to listOf(listOf(TAB))), listOf(TAB)),
        )
        assertTrue(
            "a binding that isn't a hold waits, so its longer one doesn't run it twice",
            isTapCombo(mapOf(save to listOf(listOf(F5), listOf(F5, X))), listOf(F5)),
        )
    }

    @Test
    fun aTapBoundTwiceToOneActionRunsItOnce() {
        // An action's own bindings aren't checked against each other, so one key can be two of them: Pause run twice
        // would pause and resume.
        val hotkeys = mapOf(pause to listOf(listOf(MODE), listOf(MODE)), save to listOf(listOf(MODE, R1)))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        tracker.release(MODE)
        assertEquals(listOf(pause), tracker.taps(hotkeys))
    }

    @Test
    fun aStickDirectionInOnlyASecondBindingIsTrackedAndCompletesIt() {
        val left = HotkeyTokens.axis(AXIS_X, positive = false)
        val hotkeys = mapOf(HotkeyAction.MUTE to listOf(listOf(MODE, A), listOf(MODE, left)))
        val tracker = HotkeyTracker()
        tracker.press(hotkeys, MODE)
        assertTrue(tracker.completions(hotkeys).isEmpty())
        assertTrue("a stick direction a binding uses is pressed", tracker.syncExternal(hotkeys, setOf(left)))
        assertEquals(listOf(HotkeyAction.MUTE), tracker.completions(hotkeys))
    }

    @Test
    fun aConflictNamesTheCollidingBindingsSlotAndLeavesTheActionsOwnOut() {
        val screenshot = HotkeyAction.SCREENSHOT
        val hotkeys = mapOf(
            save to listOf(listOf(MODE, R1), listOf(F5)),
            screenshot to listOf(emptyList(), listOf(F8)),
        )
        assertEquals(listOf(HotkeyConflict(screenshot, 1, HotkeyConflict.Kind.EXACT)), hotkeyConflicts(hotkeys, HotkeyAction.LOAD_STATE, listOf(F8)))
        assertEquals(listOf(HotkeyConflict(save, 1, HotkeyConflict.Kind.EXACT)), hotkeyConflicts(hotkeys, screenshot, listOf(F5)))
        assertTrue("Save's own other binding", hotkeyConflicts(hotkeys, save, listOf(F5)).isEmpty())
        assertTrue(hotkeyConflicts(hotkeys, save, listOf(MODE, R1, L1)).isEmpty())
    }

    @Test
    fun existingConflictsNameEachPairOnceHoweverManyOfTheirBindingsCollide() {
        val hotkeys = hotkeysOf(save to listOf(MODE, R1), HotkeyAction.LOAD_STATE to listOf(MODE, L1), HotkeyAction.SCREENSHOT to listOf(MODE, R1))
        assertEquals(listOf(save to HotkeyAction.SCREENSHOT), existingHotkeyConflicts(hotkeys))
        val twice = mapOf(save to listOf(listOf(MODE, R1), listOf(F5)), HotkeyAction.SCREENSHOT to listOf(listOf(F5), listOf(MODE, R1)))
        assertEquals(listOf(save to HotkeyAction.SCREENSHOT), existingHotkeyConflicts(twice))
    }

    @Test
    fun bindingsAreStoredJoinedBySemicolonsAndAValueFromBeforeIsTheFirst() {
        assertEquals("a single combo, as stored before", listOf(listOf(MODE, R1)), HotkeyBindings.parse("110,103"))
        assertEquals("unbound", emptyList<List<Int>>(), HotkeyBindings.parse(""))
        assertEquals(listOf(listOf(MODE, R1), listOf(F5)), HotkeyBindings.parse("110,103;135"))
        assertEquals("an empty slot keeps the next in its place", listOf(emptyList(), listOf(F5)), HotkeyBindings.parse(";135"))
        assertEquals(listOf(listOf(MODE, R1)), HotkeyBindings.parse("110, 103;;"))
        assertEquals("three at most", listOf(listOf(1), listOf(2), listOf(3)), HotkeyBindings.parse("1;2;3;4"))

        assertEquals("110,103;135", HotkeyBindings.encode(listOf(listOf(MODE, R1), listOf(F5))))
        assertEquals(";135", HotkeyBindings.encode(listOf(emptyList(), listOf(F5))))
        assertEquals("110", HotkeyBindings.encode(listOf(listOf(MODE), emptyList())))
        assertEquals("", HotkeyBindings.encode(emptyList()))
        for (bindings in listOf(listOf(listOf(MODE, R1), listOf(F5), listOf(F8)), listOf(emptyList(), emptyList(), listOf(F12)))) {
            assertEquals(bindings, HotkeyBindings.parse(HotkeyBindings.encode(bindings)))
        }
    }

    @Test
    fun replacingABindingKeepsTheOthersInTheirSlots() {
        val two = listOf(listOf(MODE, R1), listOf(F5))
        assertEquals(listOf(emptyList(), listOf(F5)), HotkeyBindings.with(two, 0, emptyList()))
        assertEquals(listOf(listOf(MODE, R1)), HotkeyBindings.with(two, 1, emptyList()))
        assertEquals(listOf(listOf(MODE, R1), listOf(F5), listOf(F8)), HotkeyBindings.with(two, 2, listOf(F8)))
        assertEquals(listOf(listOf(MODE, R1), emptyList(), listOf(F8)), HotkeyBindings.with(listOf(listOf(MODE, R1)), 2, listOf(F8)))
        assertEquals(emptyList<List<Int>>(), HotkeyBindings.with(listOf(emptyList(), listOf(F5)), 1, emptyList()))
    }

    @Test
    fun theDefaultsAreTheOwnersSetWithR3AndSelectInPlaceOfZAndCBesideAKeyboardsSecondBindings() {
        // The owner's set as the RP6's test copy stored it (hotkey_combo_pause "4,96" and so on), but for its Back + Z
        // (a screenshot) and Back + C (mute), with a keyboard's keys as second bindings, and with Back + Start showing
        // the screensaver: the owner's choices. Every other action (Reset, Frame Advance, the fast-forward toggle, the
        // direct slots) is unbound, as in that set.
        val expected = mapOf(
            "pause" to listOf(listOf(BACK, A), listOf(F12)),
            "save" to listOf(listOf(BACK, R1), listOf(F5)),
            "load" to listOf(listOf(BACK, L1), listOf(F9)),
            "inc_slot" to listOf(listOf(BACK, DPAD_RIGHT)),
            "dec_slot" to listOf(listOf(BACK, DPAD_LEFT)),
            "reload" to listOf(listOf(BACK, X)),
            "quit" to listOf(listOf(BACK, B)),
            "screenshot" to listOf(listOf(BACK, R3), listOf(F8)),
            "mute" to listOf(listOf(BACK, SELECT)),
            "ff_hold" to listOf(listOf(BACK, R2), listOf(TAB)),
            "slow_motion" to listOf(listOf(BACK, L2)),
            "n64_pak" to listOf(listOf(BACK, DPAD_DOWN)),
            "analog_toggle" to listOf(listOf(BACK, DPAD_UP)),
            "keyboard" to listOf(listOf(BACK, L3)),
            "library" to listOf(listOf(BACK, Y)),
            "show_screensaver" to listOf(listOf(BACK, START)),
        )
        assertEquals(expected, DEFAULT_HOTKEYS)
        assertTrue("none needs C or Z, which most controllers lack", DEFAULT_HOTKEYS.values.flatten().none { C in it || Z in it })
        assertTrue("slow motion is a hold", "slow_motion" in HotkeyAction.HOLDS)
        assertTrue(existingHotkeyConflicts(DEFAULT_HOTKEYS).isEmpty())
    }

    @Test
    fun showScreensaversDefaultTakesTheButtonChosenForBacksPlaceBeforeItCame() {
        val show = HotkeyAction.SHOW_SCREENSAVER
        val backStart = listOf(listOf(BACK, START))
        assertEquals("nothing chosen: Back + Start", backStart, showScreensaverDefault(DEFAULT_HOTKEYS))
        // Mode chosen for Back's place, as the builds before Show Screensaver stored it: every action Back led.
        val modeLed = DEFAULT_HOTKEYS + leadReplacement(DEFAULT_HOTKEYS - show, BACK, MODE).changes
        assertEquals(listOf(listOf(MODE, START)), showScreensaverDefault(modeLed))
        assertTrue(existingHotkeyConflicts(modeLed + (show to listOf(listOf(MODE, START)))).isEmpty())
        // A binding Back led by default changed in more than its first key, or unbound, says nothing of the lead.
        val otherTail = modeLed + (pause to listOf(listOf(C, B), listOf(F12)))
        assertEquals(listOf(listOf(MODE, START)), showScreensaverDefault(otherTail))
        assertEquals(listOf(listOf(MODE, START)), showScreensaverDefault(modeLed - pause))
        // Start chosen (a game button, after the page's question): Show Screensaver would lose a key, so it's unbound.
        val startLed = DEFAULT_HOTKEYS + leadReplacement(DEFAULT_HOTKEYS - show, BACK, START).changes
        assertTrue(showScreensaverDefault(startLed).isEmpty())
        // Back still leading a binding: Back + Start as ever.
        val oneMoved = DEFAULT_HOTKEYS + (pause to listOf(listOf(MODE, A), listOf(F12)))
        assertEquals(backStart, showScreensaverDefault(oneMoved))
        // The bindings Back led started by two buttons now: no one choice to follow, so it's unbound.
        val twoLeads = modeLed + (pause to listOf(listOf(C, A), listOf(F12)))
        assertTrue(showScreensaverDefault(twoLeads).isEmpty())
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

    @Test
    fun aKeyInBacksPlaceStartsEveryBindingBackStartsAndLeavesTheRest() {
        val hotkeys = mapOf(
            HotkeyAction.PAUSE to listOf(listOf(BACK, A), listOf(F12)),
            HotkeyAction.SAVE_STATE to listOf(listOf(BACK, R1), listOf(F5)),
            HotkeyAction.LOAD_STATE to listOf(listOf(F9), listOf(BACK, L1, R1)),
            HotkeyAction.SCREENSHOT to listOf(listOf(F8)),
        )
        val replacement = leadReplacement(hotkeys, BACK, MODE)
        assertEquals(
            mapOf(
                HotkeyAction.PAUSE to listOf(listOf(MODE, A), listOf(F12)),
                HotkeyAction.SAVE_STATE to listOf(listOf(MODE, R1), listOf(F5)),
                HotkeyAction.LOAD_STATE to listOf(listOf(F9), listOf(MODE, L1, R1)),
            ),
            replacement.changes,
        )
        assertTrue("nothing is in the way", replacement.blockers.isEmpty())
        assertEquals("Back for Back changes nothing", LeadReplacement(emptyMap(), emptyList()), leadReplacement(hotkeys, BACK, BACK))
    }

    @Test
    fun whatsInTheWayIsABindingTheKeyWouldLoseAKeyOfOrNewlyCollideWithNotOneCollidingAlready() {
        // Back + Select would become Select + Select: a key lost.
        assertEquals(listOf(HotkeyAction.MUTE), leadReplacement(hotkeysOf(HotkeyAction.MUTE to listOf(BACK, SELECT)), BACK, SELECT).blockers)
        // Back + X becomes Mode + X, Quit's already; Back + Y becomes Mode + Y, part of Screenshot's Mode + Y + R1. Back
        // alone (Pause's) becomes a tap beside the longer bindings it starts, no collision; and Load's X + A collides
        // with Save's Back + X already (X starts one and is in the other), as it will with Mode + X.
        val hotkeys = mapOf(
            HotkeyAction.PAUSE to listOf(listOf(BACK)),
            HotkeyAction.SAVE_STATE to listOf(listOf(BACK, X)),
            HotkeyAction.QUIT to listOf(listOf(MODE, X)),
            HotkeyAction.SCREENSHOT to listOf(listOf(F8), listOf(MODE, Y, R1)),
            HotkeyAction.LIBRARY to listOf(listOf(BACK, Y)),
            HotkeyAction.LOAD_STATE to listOf(listOf(X, A)),
        )
        assertEquals(setOf(HotkeyAction.QUIT, HotkeyAction.SCREENSHOT), leadReplacement(hotkeys, BACK, MODE).blockers.toSet())
    }

    @Test
    fun backIsSeenFromAnyConnectedDeviceThatSentItAndAskedAboutOnlyWithAGamepadConnected() {
        // One controller as two devices: its gamepad, and a part of it that isn't one, its Back coming from that part.
        val pad = "pad" to true
        val padsKeys = "pad's keys" to false
        assertTrue("no gamepad connected: nothing to ask about yet", backSeen(listOf(padsKeys), emptySet()))
        assertFalse("a gamepad connected, no Back seen", backSeen(listOf(pad, padsKeys), emptySet()))
        assertTrue("the gamepad sent it", backSeen(listOf(pad, padsKeys), setOf("pad")))
        assertTrue("its part that isn't a gamepad sent it", backSeen(listOf(pad, padsKeys), setOf("pad's keys")))
        assertFalse("a device that sent it once, not connected now", backSeen(listOf(pad), setOf("pad's keys")))
    }

    companion object {
        // KeyEvent and MotionEvent constants (android.view).
        const val BACK = 4
        const val A = 96
        const val B = 97
        const val C = 98
        const val X = 99
        const val Y = 100
        const val Z = 101
        const val L1 = 102
        const val R1 = 103
        const val L2 = 104
        const val R2 = 105
        const val L3 = 106
        const val R3 = 107
        const val START = 108
        const val SELECT = 109
        const val MODE = 110
        const val TAB = 61
        const val F5 = 135
        const val F8 = 138
        const val F9 = 139
        const val F12 = 142
        const val DPAD_UP = 19
        const val DPAD_DOWN = 20
        const val DPAD_LEFT = 21
        const val DPAD_RIGHT = 22
        const val AXIS_X = 0
        const val AXIS_HAT_X = 15
        const val AXIS_LTRIGGER = 17
        const val AXIS_GAS = 22
    }
}

/** Hotkeys with one binding each, as most tests need. */
internal fun hotkeysOf(vararg bindings: Pair<String, List<Int>>): Map<String, List<List<Int>>> =
    bindings.associate { (action, combo) -> action to listOf(combo) }
