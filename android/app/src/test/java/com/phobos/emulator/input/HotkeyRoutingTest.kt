package com.phobos.emulator.input

import com.phobos.emulator.PhobosCore.Input
import com.phobos.emulator.launch.LaunchSystems
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * Hotkeys as a game gets them: key events through the activity's window (WindowKeys) to the game screen's keys
 * (GameKeys), joystick moves through GameInputState, which records what the core is sent. The defaults, Back leading
 * them, and a keyboard's second bindings; and on a set led by Mode, as the defaults once were, taps, the hold actions
 * driven from the hat, from a trigger reported only as an axis and from a key that also leads a longer binding of the
 * same hold, a key bound alone beside a longer binding it leads, a button reported both ways, keys let go of while the
 * game is closed or away, and the Library's way back; and a PSP system dialog over the game: its keys, those a Reload
 * from it leaves to the window included, and its hat D-pad, a direction held as it closes too.
 */
class HotkeyRoutingTest {
    private val run = mutableListOf<String>()
    private val events = mutableListOf<String>()
    private var hotkeys: Map<String, List<List<Int>>> = MODE_LED
    private var onScreen = true
    private var loaded = true
    private lateinit var coreInput: GameInputState.CoreInput
    /** A test's own list of the taps waiting their moment (GameKeys.Screen.later), run by hand; null: at once. */
    private var waiting: MutableList<() -> Unit>? = null

    /** The game screen as GameKeys asks it: the game running, every action applying. */
    private val gameKeys = GameKeys(object : GameKeys.Screen {
        override fun controls() = ActiveControls(HeldCrossReplayTest.DEFAULT_MAPPINGS, hotkeys)
        override fun paused() = false
        override fun editing() = false
        override fun fastForwardToggled() = false
        override fun zxKeyToBind(): String? = null
        override fun bindZxKey(key: String, bit: Int) {}
        override fun hotkey(action: String): Boolean {
            run += action
            // Reloading closes the game at once (MainViewModel.closeSessionLocked): it isn't loaded until it's back.
            if (action == HotkeyAction.RELOAD) loaded = false
            return true
        }
        override fun stopFastForward() {
            events += "fast forward stopped"
        }
        override fun frameAdvanceHeld(held: Boolean) {
            events += if (held) "frame advance held" else "frame advance let go"
        }
        override fun slowMotionHeld(held: Boolean) {
            events += if (held) "slow motion held" else "slow motion let go"
        }
        override fun later(delayMs: Long, run: () -> Unit) {
            waiting?.add(run) ?: run()
        }
    })

    /** The window as MainActivity runs it: the game on screen once loaded, or swapped away behind the Library. */
    private val window = WindowKeys(object : WindowKeys.App {
        override fun gameKeys(): KeyHandler? = gameKeys.takeIf { onScreen && loaded }
        override fun gameKeysHolding(keyCode: Int): KeyHandler? = gameKeys.takeIf { onScreen && it.holdsKey(keyCode) }
        override fun swappedAway() = loaded && !onScreen
        override fun hotkeys() = hotkeys
        override fun heldAxes(): Set<Int> = GameInputState.hotkeyKeys
        override fun swapBackToGame() {
            events += "back to the game"
            onScreen = true
        }
    })

    @Before
    fun start() {
        GameInputState.reset()
        coreInput = GameInputState.coreInput
        GameInputState.coreInput = GameInputState.CoreInput { _, _, _, _, _ -> }
        GameInputState.onHotkeyKeysChanged = { gameKeys.onHatChanged() }
    }

    @After
    fun stop() {
        GameInputState.onHotkeyKeysChanged = null
        GameInputState.reset()
        GameInputState.coreInput = coreInput
    }

    /** A key event through the window; true when it's taken. */
    private fun key(code: Int, down: Boolean, repeat: Int = 0) = window.onKey(code, down, repeat)

    private fun tap(code: Int) {
        key(code, down = true)
        key(code, down = false)
    }

    /** A joystick move: the hat and the right trigger (as its axis only), everything else at rest. */
    private fun move(hatX: Float = 0f, hatY: Float = 0f, rt: Float = 0f) {
        val axes = mapOf(AXIS_HAT_X to hatX, AXIS_HAT_Y to hatY, AXIS_RTRIGGER to rt, AXIS_GAS to rt)
        GameInputState.onJoystickMove({ axes[it] ?: 0f }, HeldCrossReplayTest.DEFAULT_MAPPINGS, LaunchSystems.PSP, PspRightStick.OFF)
    }

    /** A joystick move in a PSP system dialog's window (PspDialogHat): the hat and the left stick across. */
    private fun dialogMove(hatX: Float = 0f, hatY: Float = 0f, x: Float = 0f) {
        val axes = mapOf(AXIS_HAT_X to hatX, AXIS_HAT_Y to hatY, AXIS_X to x)
        GameInputState.onDialogJoystickMove { axes[it] ?: 0f }
    }

    @Test
    fun aCombosFirstKeyGoesThroughTheWindowToTheGamesHotkeysSoModeAndR1Save() {
        // The window used to take a combo's first key for the Library hotkey, so no other combo could complete.
        assertTrue("Mode is taken, by the game's hotkeys", key(MODE, down = true))
        assertTrue(key(R1, down = true))
        assertEquals(listOf(HotkeyAction.SAVE_STATE), run)
        assertEquals("R1 stays out of the game", 0, GameInputState.buttons and Input.R1)
        key(R1, down = false)
        key(MODE, down = false)
        assertEquals("no tap after a combo", listOf(HotkeyAction.SAVE_STATE), run)
    }

    @Test
    fun withTheDefaultsBackAndAPausesBackAloneRunsNothingAndBackAndR2HoldFastForward() {
        hotkeys = DEFAULT_HOTKEYS
        assertTrue("Back is taken, by the game's hotkeys", key(BACK, down = true))
        assertTrue("A completes Back + A and is taken", key(BUTTON_A, down = true))
        assertEquals(listOf(HotkeyAction.PAUSE), run)
        key(BUTTON_A, down = false)
        key(BACK, down = false)
        assertTrue("Back alone is taken too", key(BACK, down = true))
        assertTrue(key(BACK, down = false))
        assertEquals("and runs nothing, no binding being Back alone", listOf(HotkeyAction.PAUSE), run)
        key(BACK, down = true)
        assertTrue("R2 completes Back + R2", key(R2, down = true))
        assertEquals(listOf(HotkeyAction.PAUSE, HotkeyAction.FAST_FORWARD_HOLD), run)
        key(R2, down = true, repeat = 1)
        assertTrue(events.isEmpty())
        key(R2, down = false)
        assertEquals(listOf("fast forward stopped"), events)
        key(BACK, down = false)
    }

    @Test
    fun selectReachesTheGameWithTheDefaultHotkeys() {
        // Select is Mute's second key, after Back: alone, it's the game's.
        hotkeys = DEFAULT_HOTKEYS
        key(SELECT, down = true)
        assertEquals(Input.SELECT, GameInputState.buttons and Input.SELECT)
        key(R1, down = true)
        assertEquals(Input.R1, GameInputState.buttons and Input.R1)
        key(R1, down = false)
        key(SELECT, down = false)
        assertEquals(0, GameInputState.buttons and (Input.SELECT or Input.R1))
        assertTrue("ran $run", run.isEmpty())
    }

    @Test
    fun aModeTapPausesAndAModeComboRunsOnlyItsAction() {
        tap(MODE)
        assertEquals(listOf(HotkeyAction.PAUSE), run)
        key(MODE, down = true)
        key(DPAD_RIGHT, down = true)
        key(DPAD_RIGHT, down = false)
        key(MODE, down = false)
        assertEquals(listOf(HotkeyAction.PAUSE, HotkeyAction.NEXT_SLOT), run)
    }

    @Test
    fun aTapWaitsAMomentAndItsControllerGoingAwayMeanwhileDropsIt() {
        val waiting = mutableListOf<() -> Unit>()
        this.waiting = waiting
        tap(MODE)
        assertTrue("Pause waits its moment", run.isEmpty())
        waiting.removeAt(0)()
        assertEquals(listOf(HotkeyAction.PAUSE), run)
        // Mode held as its controller goes: Android lets go of it, here before it reports the controller gone, and the
        // activity's onInputDeviceRemoved then forgets the keys.
        key(MODE, down = true)
        assertTrue(key(MODE, down = false))
        window.reset()
        gameKeys.forgetPressedKeys()
        GameInputState.releaseAllButtons()
        waiting.forEach { it() }
        assertEquals("no Pause for the controller going away", listOf(HotkeyAction.PAUSE), run)
    }

    @Test
    fun aHatTapWaitsItsMomentTooAndItsControllerGoingAwayMeanwhileDropsIt() {
        // The RP6's D-pad is a hat: down alone is a tap here, leading down + R1.
        hotkeys = hotkeysOf(HotkeyAction.PAUSE to listOf(DPAD_DOWN), HotkeyAction.SAVE_STATE to listOf(DPAD_DOWN, R1))
        val waiting = mutableListOf<() -> Unit>()
        this.waiting = waiting
        move(hatY = 1f)
        move()
        assertTrue("Pause waits its moment", run.isEmpty())
        waiting.removeAt(0)()
        assertEquals(listOf(HotkeyAction.PAUSE), run)
        // Down held as the controller goes, if its last report lets go of the hat: the activity then forgets the keys.
        move(hatY = 1f)
        move()
        window.reset()
        gameKeys.forgetPressedKeys()
        GameInputState.releaseAllButtons()
        waiting.forEach { it() }
        assertEquals("no Pause for the controller going away", listOf(HotkeyAction.PAUSE), run)
    }

    @Test
    fun theLibraryHotkeyFromBehindTheLibraryBringsTheGameBackTakingOnlyItsKeys() {
        onScreen = false
        // On the Library's screens Mode would go Home (Android's fallback for an unhandled Mode), and Back in its place
        // would go back.
        assertTrue("Mode, which starts the Library hotkey, is held back from the Library's screens", key(MODE, down = true))
        assertFalse("a key completing something else goes on to them", key(BUTTON_A, down = true))
        assertFalse(key(BUTTON_A, down = false))
        assertTrue("Y completes the Library hotkey", key(BUTTON_Y, down = true))
        assertEquals(listOf("back to the game"), events)
        assertTrue("its repeats are taken", key(BUTTON_Y, down = true, repeat = 1))
        assertTrue("and its release", key(BUTTON_Y, down = false))
        assertTrue("and Mode's", key(MODE, down = false))
        assertTrue("nothing ran as a game hotkey: ran $run", run.isEmpty())
    }

    @Test
    fun aLibraryHotkeyStartedByBackBringsTheGameBackWithoutBackReachingTheLibrarysScreens() {
        hotkeys = mapOf(HotkeyAction.LIBRARY to listOf(listOf(BACK, BUTTON_Y)), HotkeyAction.SAVE_STATE to listOf(listOf(BACK, R1)))
        onScreen = false
        assertTrue("Back is held back from the Library's screens, where it would go back", key(BACK, down = true))
        assertTrue("Y completes the Library hotkey", key(BUTTON_Y, down = true))
        assertEquals(listOf("back to the game"), events)
        assertTrue(key(BUTTON_Y, down = false))
        assertTrue("Back's release is taken too", key(BACK, down = false))
        // A key starting only another hotkey still goes on to the Library's screens.
        hotkeys = hotkeysOf(HotkeyAction.LIBRARY to listOf(MODE, BUTTON_Y), HotkeyAction.SAVE_STATE to listOf(BACK, R1))
        onScreen = false
        assertFalse(key(BACK, down = true))
        assertFalse(key(BACK, down = false))
    }

    @Test
    fun fastForwardHoldStartsAndStopsOnATriggerReportedOnlyAsAnAxis() {
        hotkeys = hotkeysOf(HotkeyAction.FAST_FORWARD_HOLD to listOf(MODE, R2))
        key(MODE, down = true)
        move(rt = 1f)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        move(rt = 0f)
        assertEquals(listOf("fast forward stopped"), events)
        key(MODE, down = false)
        assertEquals("no second start, and no tap", listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
    }

    @Test
    fun frameAdvanceFromTheHatRunsAFrameOnItsPressThenHoldsAndRepeatsChangeNothing() {
        hotkeys = hotkeysOf(HotkeyAction.FRAME_ADVANCE to listOf(MODE, DPAD_DOWN))
        key(MODE, down = true)
        move(hatY = 1f)
        assertEquals("the press runs a frame", listOf(HotkeyAction.FRAME_ADVANCE), run)
        assertEquals(listOf("frame advance held"), events)
        assertEquals("the hat's Down stays out of the game", 0, GameInputState.buttons and Input.DOWN)
        key(MODE, down = true, repeat = 1)
        assertEquals(listOf("frame advance held"), events)
        move()
        assertEquals(listOf("frame advance held", "frame advance let go"), events)
        assertEquals("one frame for one press", listOf(HotkeyAction.FRAME_ADVANCE), run)
    }

    @Test
    fun eachFrameAdvancePressRunsAFrameThoughItIsLetGoOfAtOnce() {
        // The screen used to run a press's frame when it next drew with the hold on, so presses and releases arriving
        // together, as they can while it is running a frame, ran none.
        key(MODE, down = true)
        repeat(3) { tap(START) }
        key(MODE, down = false)
        assertEquals(List(3) { HotkeyAction.FRAME_ADVANCE }, run)
        assertEquals(List(3) { listOf("frame advance held", "frame advance let go") }.flatten(), events)
    }

    @Test
    fun aComboStillHeldDoesntRunAgainWhenAnotherKeyIsLetGo() {
        key(MODE, down = true)
        key(R1, down = true)
        move(hatX = 1f)
        move()
        key(BUTTON_A, down = true)
        key(BUTTON_A, down = false)
        assertEquals(listOf(HotkeyAction.SAVE_STATE), run)
    }

    @Test
    fun forgettingTheKeysHeldLetsTheGameHaveTheActionKeysAgain() {
        // A controller gone with Mode down never sends its release; MainActivity then forgets the keys held.
        key(MODE, down = true)
        window.reset()
        gameKeys.forgetPressedKeys()
        key(R1, down = true)
        assertEquals("R1 is the game's again", Input.R1, GameInputState.buttons and Input.R1)
        assertTrue("ran $run", run.isEmpty())
    }

    @Test
    fun keysLetGoOfAfterAReloadClosedTheGameStillReachItsHotkeys() {
        // Mode + X reloads, which closes the game before its keys are let go: none may stay held for the game back.
        key(MODE, down = true)
        key(BUTTON_X, down = true)
        assertEquals(listOf(HotkeyAction.RELOAD), run)
        key(BUTTON_X, down = false)
        key(MODE, down = false)
        loaded = true
        key(BUTTON_X, down = true)
        assertEquals("X alone is the game's", Input.X, GameInputState.buttons and Input.X)
        key(BUTTON_X, down = false)
        key(BUTTON_A, down = true)
        assertEquals("so is A", Input.A, GameInputState.buttons and Input.A)
        key(BUTTON_A, down = false)
        tap(MODE)
        assertEquals("and a Mode tap pauses", listOf(HotkeyAction.RELOAD, HotkeyAction.PAUSE), run)
    }

    @Test
    fun aKeyPressedAgainBehindTheLibraryGoesThereWithItsReleaseThoughTheGameNeverSawItLetGo() {
        key(BUTTON_A, down = true) // the game's; its release goes to a dialog's window
        onScreen = false
        assertFalse(key(BUTTON_A, down = true))
        assertFalse("its release goes on to the Library's screens too", key(BUTTON_A, down = false))
    }

    @Test
    fun aTriggerReportedAsAKeyAndAsAnAxisTogglesFastForwardOncePerPress() {
        key(MODE, down = true)
        key(R2, down = true)
        move(rt = 1f)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_TOGGLE), run)
        move()
        key(R2, down = false)
        // The other way round: the axis first, then the key.
        move(rt = 1f)
        key(R2, down = true)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_TOGGLE, HotkeyAction.FAST_FORWARD_TOGGLE), run)
        key(R2, down = false)
        move()
        key(MODE, down = false)
        assertEquals("and no tap", 2, run.size)
    }

    @Test
    fun aDpadReportedAsAKeyAndAsAHatChangesTheSlotOnce() {
        key(MODE, down = true)
        key(DPAD_RIGHT, down = true)
        move(hatX = 1f)
        assertEquals(listOf(HotkeyAction.NEXT_SLOT), run)
        assertEquals("the D-pad stays out of the game", 0, GameInputState.buttons and Input.RIGHT)
        move()
        key(DPAD_RIGHT, down = false)
        key(MODE, down = false)
        assertEquals(listOf(HotkeyAction.NEXT_SLOT), run)
    }

    @Test
    fun aHoldDoesntStartWhenLettingGoOfAKeyLeavesItsCombo() {
        key(MODE, down = true)
        key(R1, down = true)
        key(START, down = true)
        key(R1, down = false)
        assertEquals("Mode + R1 saved; Mode + Start was never pressed as such", listOf(HotkeyAction.SAVE_STATE), run)
        assertTrue("frame advance didn't start: $events", events.isEmpty())
    }

    @Test
    fun aTriggerRestingNearHalfwayTogglesFastForwardOnce() {
        key(MODE, down = true)
        for (rt in listOf(0.6f, 0.45f, 0.55f, 0.42f, 0.58f)) move(rt = rt)
        move(rt = 0.3f)
        key(MODE, down = false)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_TOGGLE), run)
    }

    @Test
    fun aKeyboardsSecondBindingsSaveLoadTakeAScreenshotAndPauseFromTheirKeyAlone() {
        hotkeys = DEFAULT_HOTKEYS
        for (code in listOf(F5, F9, F8, F12)) {
            assertTrue("$code is taken", key(code, down = true))
            key(code, down = false)
        }
        assertEquals(listOf(HotkeyAction.SAVE_STATE, HotkeyAction.LOAD_STATE, HotkeyAction.SCREENSHOT, HotkeyAction.PAUSE), run)
    }

    @Test
    fun tabHoldsFastForwardUntilItIsLetGo() {
        hotkeys = DEFAULT_HOTKEYS
        assertTrue(key(TAB, down = true))
        key(TAB, down = true, repeat = 1)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        assertTrue(events.isEmpty())
        key(TAB, down = false)
        assertEquals(listOf("fast forward stopped"), events)
    }

    @Test
    fun modeAndL2HoldSlowMotionUntilOneIsLetGoAndAPauseEndsIt() {
        key(MODE, down = true)
        assertTrue("L2 completes Mode + L2 and is taken", key(L2, down = true))
        key(L2, down = true, repeat = 1)
        assertEquals(listOf("slow motion held"), events)
        key(L2, down = false)
        assertEquals(listOf("slow motion held", "slow motion let go"), events)
        key(MODE, down = false)
        assertTrue("a hold runs no action, and Mode's release after L2 is no tap", run.isEmpty())

        events.clear()
        key(MODE, down = true)
        key(L2, down = true)
        gameKeys.forgetPressedKeys()
        assertEquals(listOf("slow motion held", "slow motion let go"), events)
    }

    @Test
    fun aHoldStartedFromItsSecondBindingLastsWhileThatOneIsHeld() {
        hotkeys = mapOf(HotkeyAction.FAST_FORWARD_HOLD to listOf(listOf(MODE, R2), listOf(TAB)))
        key(TAB, down = true)
        key(BUTTON_A, down = true)
        key(BUTTON_A, down = false)
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        assertTrue("Tab is still held: $events", events.isEmpty())
        key(TAB, down = false)
        assertEquals(listOf("fast forward stopped"), events)
    }

    @Test
    fun aHoldsKeyAloneStartsItAtOnceThoughItAlsoLeadsALongerBindingOfThatHold() {
        hotkeys = mapOf(HotkeyAction.FAST_FORWARD_HOLD to listOf(listOf(TAB), listOf(TAB, BUTTON_X)))
        assertTrue(key(TAB, down = true))
        assertEquals("no waiting for Tab's release", listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        assertTrue("X with Tab held is the longer binding's, not the game's", key(BUTTON_X, down = true))
        key(BUTTON_X, down = false)
        assertEquals("the same hold, started once", listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        assertTrue("Tab is still held: $events", events.isEmpty())
        key(TAB, down = false)
        assertEquals(listOf("fast forward stopped"), events)
    }

    @Test
    fun anotherActionsKeyAloneBesideAHoldsLongerBindingWaitsForItsReleaseAndTheHoldStillStarts() {
        hotkeys = mapOf(
            HotkeyAction.SAVE_STATE to listOf(listOf(TAB)),
            HotkeyAction.FAST_FORWARD_HOLD to listOf(listOf(TAB, BUTTON_X)),
        )
        assertTrue(key(TAB, down = true))
        assertTrue("Save waits for Tab's release, Tab leading the hold's binding: $run", run.isEmpty())
        assertTrue(key(BUTTON_X, down = true))
        assertEquals(listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        key(BUTTON_X, down = false)
        assertEquals(listOf("fast forward stopped"), events)
        key(TAB, down = false)
        assertEquals("Tab pressed with X is no tap", listOf(HotkeyAction.FAST_FORWARD_HOLD), run)
        tap(TAB)
        assertEquals(
            "Tab alone saves as it's let go of",
            listOf(HotkeyAction.FAST_FORWARD_HOLD, HotkeyAction.SAVE_STATE),
            run,
        )
    }

    @Test
    fun anActionsKeyAloneBesideItsOwnLongerBindingRunsItOnceAsATap() {
        hotkeys = mapOf(HotkeyAction.SAVE_STATE to listOf(listOf(F5), listOf(F5, BUTTON_X)))
        key(F5, down = true)
        assertTrue("F5 waits for its release: $run", run.isEmpty())
        key(BUTTON_X, down = true)
        assertEquals(listOf(HotkeyAction.SAVE_STATE), run)
        key(BUTTON_X, down = false)
        key(F5, down = false)
        assertEquals("once, F5 pressed with X being no tap", listOf(HotkeyAction.SAVE_STATE), run)
        tap(F5)
        assertEquals(listOf(HotkeyAction.SAVE_STATE, HotkeyAction.SAVE_STATE), run)
    }

    @Test
    fun inAPspDialogTheHotkeysRunAndKeepTheirKeysTheRestBeingTheDialogsAndNoneTheGames() {
        hotkeys = DEFAULT_HOTKEYS
        assertTrue("Back is held back from the dialog", gameKeys.onDialogKey(BACK, down = true, repeatCount = 0))
        assertTrue("R1 completes Back + R1", gameKeys.onDialogKey(R1, down = true, repeatCount = 0))
        assertEquals(listOf(HotkeyAction.SAVE_STATE), run)
        assertTrue("its repeats stay the hotkeys'", gameKeys.onDialogKey(R1, down = true, repeatCount = 1))
        assertTrue("and its release", gameKeys.onDialogKey(R1, down = false, repeatCount = 0))
        assertTrue("R2 with Back held", gameKeys.onDialogKey(R2, down = true, repeatCount = 0))
        assertEquals("starts fast forward", listOf(HotkeyAction.SAVE_STATE, HotkeyAction.FAST_FORWARD_HOLD), run)
        gameKeys.onDialogKey(R2, down = false, repeatCount = 0)
        assertEquals("and letting go of it stops it", listOf("fast forward stopped"), events)
        assertTrue("Back's release is taken too", gameKeys.onDialogKey(BACK, down = false, repeatCount = 0))
        assertFalse("A alone is the dialog's", gameKeys.onDialogKey(BUTTON_A, down = true, repeatCount = 0))
        assertFalse("its repeats too", gameKeys.onDialogKey(BUTTON_A, down = true, repeatCount = 1))
        assertFalse(gameKeys.onDialogKey(BUTTON_A, down = false, repeatCount = 0))
        assertEquals("the game gets no button", 0, GameInputState.buttons)
        assertEquals(listOf(HotkeyAction.SAVE_STATE, HotkeyAction.FAST_FORWARD_HOLD), run)
    }

    @Test
    fun keysPressedInAPspDialogAndLetGoOfAfterAReloadClosedTheGameStillReachItsHotkeys() {
        hotkeys = DEFAULT_HOTKEYS
        // Back + X in the dialog reloads, which closes the game, and the dialog with it, before its keys are let go:
        // their releases come through the window while the game isn't loaded.
        assertTrue(gameKeys.onDialogKey(BACK, down = true, repeatCount = 0))
        assertTrue(gameKeys.onDialogKey(BUTTON_X, down = true, repeatCount = 0))
        assertEquals(listOf(HotkeyAction.RELOAD), run)
        assertTrue("X's release is the hotkeys'", key(BUTTON_X, down = false))
        assertTrue("and Back's", key(BACK, down = false))
        loaded = true
        key(BUTTON_A, down = true)
        assertEquals("A alone is the game's", Input.A, GameInputState.buttons and Input.A)
        key(BUTTON_A, down = false)
        tap(BUTTON_X)
        key(R1, down = true)
        assertEquals("so is R1, after an X", Input.R1, GameInputState.buttons and Input.R1)
        key(R1, down = false)
        assertEquals("nothing else ran", listOf(HotkeyAction.RELOAD), run)
    }

    @Test
    fun inAPspDialogBackAndTheHatsDirectionsChangeTheSlotAndTheDialogGetsNoneOfThoseDirections() {
        hotkeys = DEFAULT_HOTKEYS
        // The dialog opening (EmulatorScreen): the game's buttons let go of, the keys forgotten.
        GameInputState.releaseAllButtons()
        gameKeys.forgetPressedKeys()
        assertTrue(gameKeys.onDialogKey(BACK, down = true, repeatCount = 0))
        dialogMove(hatX = 1f)
        assertEquals("Back + the hat's right: the next slot", listOf(HotkeyAction.NEXT_SLOT), run)
        assertTrue("its Android D-pad key moves nothing", GameInputState.hatDirectionTaken(DPAD_RIGHT))
        // Back, held and taken, is no direction: the navigation bar's Back isn't dropped in the dialog meanwhile.
        assertFalse("nor is Back taken from the dialog", GameInputState.hatDirectionTaken(BACK))
        dialogMove(hatX = 1f, x = 0.3f)
        assertEquals("a move that leaves the hat as it was runs nothing more", listOf(HotkeyAction.NEXT_SLOT), run)
        dialogMove()
        assertFalse(GameInputState.hatDirectionTaken(DPAD_RIGHT))
        dialogMove(hatX = -1f)
        dialogMove()
        assertEquals("Back + left: the previous slot", listOf(HotkeyAction.NEXT_SLOT, HotkeyAction.PREVIOUS_SLOT), run)
        assertTrue("Back's release is taken", gameKeys.onDialogKey(BACK, down = false, repeatCount = 0))
        assertEquals("the game gets no button", 0, GameInputState.buttons)
        assertEquals(listOf(HotkeyAction.NEXT_SLOT, HotkeyAction.PREVIOUS_SLOT), run)
    }

    @Test
    fun inAPspDialogTheHatsDirectionNoHotkeyTakesAndTheLeftStickStayTheDialogs() {
        hotkeys = DEFAULT_HOTKEYS
        dialogMove(hatY = 1f)
        assertFalse("down alone moves the dialog's selection", GameInputState.hatDirectionTaken(DPAD_DOWN))
        dialogMove()
        dialogMove(x = 1f)
        assertTrue("the left stick is no D-pad to the hotkeys", GameInputState.hotkeyKeys.isEmpty())
        assertTrue(run.isEmpty())
        assertEquals("the game gets nothing", 0, GameInputState.buttons)
    }

    @Test
    fun aHatDirectionHeldAsAPspDialogClosesIsTheGamesOnlyOncePressedAgain() {
        hotkeys = DEFAULT_HOTKEYS
        dialogMove(hatX = 1f)
        GameInputState.endDialogMotion() // the dialog answered, right still held
        move(hatX = 1f)
        assertEquals("pressed in the dialog, it isn't the game's", 0, GameInputState.buttons and Input.RIGHT)
        move()
        move(hatX = 1f)
        assertEquals("pressed again, it is", Input.RIGHT, GameInputState.buttons and Input.RIGHT)
    }

    @Test
    fun aHatDirectionPressedInAPspDialogAndLetGoOfOverTheGameStillReachesTheHotkeys() {
        // Down alone is a tap here, leading down + R1.
        hotkeys = hotkeysOf(HotkeyAction.PAUSE to listOf(DPAD_DOWN), HotkeyAction.SAVE_STATE to listOf(DPAD_DOWN, R1))
        dialogMove(hatY = 1f)
        assertTrue("down leads a combo: the hotkeys take it", GameInputState.hatDirectionTaken(DPAD_DOWN))
        GameInputState.endDialogMotion()
        move()
        assertEquals("its release, the game's first move, is the tap", listOf(HotkeyAction.PAUSE), run)
    }

    @Test
    fun frameAdvanceOnModeAloneBesideModeAndL3RunsAFrameAsModeGoesDown() {
        hotkeys = mapOf(HotkeyAction.FRAME_ADVANCE to listOf(listOf(MODE, L3), listOf(MODE)))
        key(MODE, down = true)
        assertEquals(listOf(HotkeyAction.FRAME_ADVANCE), run)
        assertEquals(listOf("frame advance held"), events)
        key(MODE, down = false)
        assertEquals(listOf("frame advance held", "frame advance let go"), events)
        assertEquals("and no tap as Mode is let go", listOf(HotkeyAction.FRAME_ADVANCE), run)
    }

    @Test
    fun aLibraryTapFromBehindTheLibraryTakesItsKeysPressAndRelease() {
        hotkeys = hotkeysOf(HotkeyAction.LIBRARY to listOf(MODE), HotkeyAction.SAVE_STATE to listOf(MODE, R1))
        onScreen = false
        assertTrue("Mode is held back from the Library's screens", key(MODE, down = true))
        assertTrue(events.isEmpty())
        assertTrue("its release is the tap", key(MODE, down = false))
        assertEquals(listOf("back to the game"), events)
    }

    @Test
    fun aLibraryTapsKeyPressedWithAnotherIsNoTapAndItsReleaseIsStillTaken() {
        hotkeys = hotkeysOf(HotkeyAction.LIBRARY to listOf(MODE), HotkeyAction.SAVE_STATE to listOf(MODE, R1))
        onScreen = false
        assertTrue(key(MODE, down = true))
        assertFalse("A goes on to the Library's screens", key(BUTTON_A, down = true))
        assertFalse(key(BUTTON_A, down = false))
        assertTrue("Mode's release is taken, as its press was", key(MODE, down = false))
        assertTrue(events.isEmpty())
    }

    @Test
    fun aLibraryTapInASecondBindingBringsTheGameBackFromBehindTheLibrary() {
        hotkeys = mapOf(
            HotkeyAction.LIBRARY to listOf(listOf(MODE, BUTTON_Y), listOf(SELECT)),
            HotkeyAction.SAVE_STATE to listOf(listOf(SELECT, R1)),
        )
        onScreen = false
        assertTrue("Select is held back from the Library's screens", key(SELECT, down = true))
        assertTrue(events.isEmpty())
        assertTrue("its release is the tap", key(SELECT, down = false))
        assertEquals(listOf("back to the game"), events)
    }

    companion object {
        // KeyEvent and MotionEvent constants (android.view).
        const val BACK = 4
        const val BUTTON_A = 96
        const val BUTTON_B = 97
        const val BUTTON_X = 99
        const val BUTTON_Y = 100
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
        const val AXIS_HAT_Y = 16
        const val AXIS_RTRIGGER = 18
        const val AXIS_GAS = 22

        /** The defaults before Back led them: Mode tapped pauses, Mode with another button runs that button's action. */
        val MODE_LED: Map<String, List<List<Int>>> = mapOf(
            HotkeyAction.PAUSE to listOf(listOf(MODE), listOf(F12)),
            HotkeyAction.SAVE_STATE to listOf(listOf(MODE, R1), listOf(F5)),
            HotkeyAction.LOAD_STATE to listOf(listOf(MODE, L1), listOf(F9)),
            HotkeyAction.NEXT_SLOT to listOf(listOf(MODE, DPAD_RIGHT)),
            HotkeyAction.PREVIOUS_SLOT to listOf(listOf(MODE, DPAD_LEFT)),
            HotkeyAction.RELOAD to listOf(listOf(MODE, BUTTON_X)),
            HotkeyAction.QUIT to listOf(listOf(MODE, BUTTON_B)),
            HotkeyAction.SCREENSHOT to listOf(listOf(MODE, R3), listOf(F8)),
            HotkeyAction.MUTE to listOf(listOf(MODE, BUTTON_A)),
            HotkeyAction.FAST_FORWARD_HOLD to listOf(listOf(TAB)),
            HotkeyAction.FAST_FORWARD_TOGGLE to listOf(listOf(MODE, R2)),
            HotkeyAction.SLOW_MOTION to listOf(listOf(MODE, L2)),
            HotkeyAction.FRAME_ADVANCE to listOf(listOf(MODE, START)),
            HotkeyAction.N64_PAK to listOf(listOf(MODE, DPAD_DOWN)),
            HotkeyAction.PS1_ANALOG_TOGGLE to listOf(listOf(MODE, DPAD_UP)),
            HotkeyAction.KEYBOARD to listOf(listOf(MODE, L3)),
            HotkeyAction.LIBRARY to listOf(listOf(MODE, BUTTON_Y)),
        )
    }
}
