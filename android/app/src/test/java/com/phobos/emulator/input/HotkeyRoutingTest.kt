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
 * (GameKeys), joystick moves through GameInputState, which records what the core is sent. The default Mode hotkeys,
 * taps, the hold actions driven from the hat and a trigger reported only as an axis, a button reported both ways, keys
 * let go of while the game is closed or away, and the Library's way back.
 */
class HotkeyRoutingTest {
    private val run = mutableListOf<String>()
    private val events = mutableListOf<String>()
    private var hotkeys: Map<String, List<Int>> = DEFAULT_HOTKEYS
    private var onScreen = true
    private var loaded = true
    private lateinit var coreInput: GameInputState.CoreInput

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
    })

    /** The window as MainActivity runs it: the game on screen once loaded, or swapped away behind the Library. */
    private val window = WindowKeys(object : WindowKeys.App {
        override fun gameKeys(): KeyHandler? = gameKeys.takeIf { onScreen && loaded }
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
    fun selectReachesTheGameWithTheDefaultHotkeys() {
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
    fun theLibraryHotkeyFromBehindTheLibraryBringsTheGameBackTakingOnlyTheKeyThatCompletesIt() {
        onScreen = false
        assertFalse("Mode goes on to the Library's screens", key(MODE, down = true))
        assertFalse("so does a key completing something else", key(BUTTON_A, down = true))
        assertFalse(key(BUTTON_A, down = false))
        assertTrue("Y completes the Library hotkey", key(BUTTON_Y, down = true))
        assertEquals(listOf("back to the game"), events)
        assertTrue("its repeats are taken", key(BUTTON_Y, down = true, repeat = 1))
        assertTrue("and its release", key(BUTTON_Y, down = false))
        key(MODE, down = false)
        assertTrue("nothing ran as a game hotkey: ran $run", run.isEmpty())
    }

    @Test
    fun fastForwardHoldStartsAndStopsOnATriggerReportedOnlyAsAnAxis() {
        hotkeys = mapOf(HotkeyAction.FAST_FORWARD_HOLD to listOf(MODE, R2))
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
        hotkeys = mapOf(HotkeyAction.FRAME_ADVANCE to listOf(MODE, DPAD_DOWN))
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
        repeat(3) { tap(L3) }
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
        key(L3, down = true)
        key(R1, down = false)
        assertEquals("Mode + R1 saved; Mode + L3 was never pressed as such", listOf(HotkeyAction.SAVE_STATE), run)
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
    fun aLibraryTapFromBehindTheLibraryTakesItsKeysPressAndRelease() {
        hotkeys = mapOf(HotkeyAction.LIBRARY to listOf(MODE), HotkeyAction.SAVE_STATE to listOf(MODE, R1))
        onScreen = false
        assertTrue("Mode is held back from the Library's screens", key(MODE, down = true))
        assertTrue(events.isEmpty())
        assertTrue("its release is the tap", key(MODE, down = false))
        assertEquals(listOf("back to the game"), events)
    }

    @Test
    fun aLibraryTapsKeyPressedWithAnotherIsNoTapAndItsReleaseIsStillTaken() {
        hotkeys = mapOf(HotkeyAction.LIBRARY to listOf(MODE), HotkeyAction.SAVE_STATE to listOf(MODE, R1))
        onScreen = false
        assertTrue(key(MODE, down = true))
        assertFalse("A goes on to the Library's screens", key(BUTTON_A, down = true))
        assertFalse(key(BUTTON_A, down = false))
        assertTrue("Mode's release is taken, as its press was", key(MODE, down = false))
        assertTrue(events.isEmpty())
    }

    companion object {
        // KeyEvent and MotionEvent constants (android.view).
        const val BUTTON_A = 96
        const val BUTTON_X = 99
        const val BUTTON_Y = 100
        const val R1 = 103
        const val R2 = 105
        const val L3 = 106
        const val SELECT = 109
        const val MODE = 110
        const val DPAD_DOWN = 20
        const val DPAD_RIGHT = 22
        const val AXIS_HAT_X = 15
        const val AXIS_HAT_Y = 16
        const val AXIS_RTRIGGER = 18
        const val AXIS_GAS = 22
    }
}
