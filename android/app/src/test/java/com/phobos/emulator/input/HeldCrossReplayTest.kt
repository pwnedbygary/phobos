package com.phobos.emulator.input

import androidx.compose.ui.focus.FocusState
import com.phobos.emulator.PhobosCore.Input
import com.phobos.emulator.launch.LaunchSystems
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import kotlin.math.sin
import kotlin.random.Random

/**
 * The RP6's event sequences while Colin McRae is driven, replayed through the game screen's keys (GameKeys: its key
 * handler, hat listener and focus) and GameInputState: Cross held while the left stick sweeps, the hat D-pad, Square,
 * L1 and R1 are pressed, and the triggers pulled. What the core is sent each time is recorded. The RP6's built-in pad
 * is an "Xbox Wireless Controller": its D-pad is HAT_X/HAT_Y only, its triggers axes only, Cross the bottom face
 * button (BUTTON_A, the A bit).
 */
class HeldCrossReplayTest {
    private val pushes = mutableListOf<Int>()
    private var sticksSent = FloatArray(4)
    private val hotkeysRun = mutableListOf<String>()
    private var mappings: Map<Int, String> = DEFAULT_MAPPINGS
    private var hotkeys: Map<String, List<Int>> = DEFAULT_HOTKEYS
    private var rightStick = PspRightStick.OFF
    private lateinit var coreInput: GameInputState.CoreInput

    /** The game screen as GameKeys asks it: a PSP game running, no editor open, no ZX key being bound. */
    private val screen = object : GameKeys.Screen {
        override fun controls() = ActiveControls(mappings, hotkeys)
        override fun paused() = false
        override fun editing() = false
        override fun fastForwardToggled() = false
        override fun zxKeyToBind(): String? = null
        override fun bindZxKey(key: String, bit: Int) {}
        // As EmulatorScreen's runHotkey answers for a PSP game: every action but the keyboard's and the PS1's runs.
        override fun hotkey(action: String): Boolean {
            if (action == HotkeyAction.KEYBOARD || action == HotkeyAction.PS1_ANALOG_TOGGLE) return false
            hotkeysRun += action
            return true
        }
        override fun stopFastForward() {}
        override fun frameAdvanceHeld(held: Boolean) {}
    }
    private val gameKeys = GameKeys(screen)

    @Before
    fun start() {
        GameInputState.reset()
        coreInput = GameInputState.coreInput
        GameInputState.coreInput = GameInputState.CoreInput { lx, ly, rx, ry, buttons ->
            pushes += buttons
            sticksSent = floatArrayOf(lx, ly, rx, ry)
        }
        GameInputState.onHotkeyKeysChanged = { gameKeys.onHatChanged() }
    }

    @After
    fun stop() {
        GameInputState.onHotkeyKeysChanged = null
        GameInputState.reset()
        GameInputState.coreInput = coreInput
    }

    private fun key(code: Int, down: Boolean, repeat: Int = 0) {
        gameKeys.onKey(code, down, repeat)
    }

    private fun tap(code: Int) {
        key(code, down = true)
        key(code, down = false)
    }

    /** A joystick move: every axis the RP6 reports, at rest unless given. The sticks sent for it are checked. */
    private fun move(lx: Float = 0f, ly: Float = 0f, rx: Float = 0f, ry: Float = 0f, hatX: Float = 0f, hatY: Float = 0f,
                     lt: Float = 0f, rt: Float = 0f) {
        val axes = mapOf(
            AXIS_X to lx, AXIS_Y to ly, AXIS_Z to rx, AXIS_RZ to ry, AXIS_HAT_X to hatX, AXIS_HAT_Y to hatY,
            AXIS_LTRIGGER to lt, AXIS_RTRIGGER to rt, AXIS_BRAKE to lt, AXIS_GAS to rt,
        )
        GameInputState.onJoystickMove({ axes[it] ?: 0f }, mappings, LaunchSystems.PSP, rightStick)
        // The PSP's stick is the left one, or the right one as its Analog choice; the right stick goes as itself too.
        val pspStick = if (rightStick == PspRightStick.ANALOG) floatArrayOf(rx, ry) else floatArrayOf(lx, ly)
        assertArrayEquals("sticks sent", pspStick + floatArrayOf(rx, ry), sticksSent, 0f)
    }

    /**
     * Cross held through [steps] joystick moves of a stage: the stick swept, the hat pressed in each of its eight
     * directions, [buttons] tapped and held (alone, together, on the same move as a hat change), the triggers pulled,
     * and the right stick moved. Android repeats Cross until another key goes down.
     */
    private fun driveWithCrossHeld(steps: Int, seed: Int, buttons: List<Int> = listOf(BUTTON_X, BUTTON_L1, BUTTON_R1)) {
        val random = Random(seed)
        key(BUTTON_A, down = true)
        var crossRepeats = 0
        var hatX = 0f
        var hatY = 0f
        val held = mutableSetOf<Int>()
        val hats = listOf(0f to -1f, 1f to -1f, 1f to 0f, 1f to 1f, 0f to 1f, -1f to 1f, -1f to 0f, -1f to -1f)
        for (step in 0 until steps) {
            // Android repeats the newest key down, so Cross only until another goes down.
            if (crossRepeats >= 0 && step % 3 == 0) key(BUTTON_A, down = true, repeat = ++crossRepeats)
            when (random.nextInt(12)) {
                0 -> { val (x, y) = hats[random.nextInt(hats.size)]; hatX = x; hatY = y }
                1 -> { hatX = 0f; hatY = 0f }
                2, 3 -> {
                    val button = buttons.random(random)
                    if (button in held) { key(button, down = false); held -= button } else { key(button, down = true); held += button }
                    crossRepeats = -1
                }
                4 -> { tap(buttons.random(random)); crossRepeats = -1 }
                else -> {}
            }
            val t = step / 40.0
            move(
                lx = (sin(t) * 0.95 + random.nextDouble(-0.03, 0.03)).toFloat(),
                ly = random.nextDouble(-0.08, 0.08).toFloat(),
                rx = if (step % 200 < 50) random.nextDouble(-1.0, 1.0).toFloat() else 0f,
                ry = if (step % 200 < 50) random.nextDouble(-1.0, 1.0).toFloat() else 0f,
                hatX = hatX, hatY = hatY,
                lt = if (step % 300 < 30) 1f else 0f,
                rt = if (step % 500 < 20) random.nextFloat() else 0f,
            )
        }
        for (button in held) key(button, down = false)
        move()
    }

    private fun assertCrossHeldInEveryPushAfter(from: Int, what: String) {
        assertTrue("$what: nothing pushed", pushes.size > from)
        for (n in from until pushes.size) {
            assertTrue("$what: push $n of ${pushes.size} lost Cross (mask ${Integer.toHexString(pushes[n])})", pushes[n] and Input.A != 0)
        }
    }

    /** The screen's focus: on the screen itself ([onScreen]), or on something in it ([inScreen] without [onScreen]). */
    private fun focus(onScreen: Boolean, inScreen: Boolean) = object : FocusState {
        override val isFocused = onScreen
        override val hasFocus = inScreen
        override val isCaptured = false
        override fun toString() = "isFocused $isFocused, hasFocus $hasFocus"
    }

    @Test
    fun crossStaysHeldThroughTheStickTheHatSquareLAndRWithTheDefaultBindings() {
        for (seed in 1..20) {
            val from = pushes.size
            driveWithCrossHeld(steps = 3_000, seed = seed)
            assertCrossHeldInEveryPushAfter(from, "seed $seed")
            key(BUTTON_A, down = false)
            assertEquals("seed $seed: Cross let go", 0, pushes.last() and Input.A)
        }
    }

    @Test
    fun crossStaysHeldWithTheBindingsTheRp6CapturesForTheTriggersAndTheHat() {
        // Captured on the RP6 (ControlCapture): a trigger binds its axis, a hat D-pad its hat axis.
        mappings = DEFAULT_MAPPINGS + mapOf(
            Input.L2 to "a:$AXIS_LTRIGGER:1", Input.R2 to "a:$AXIS_RTRIGGER:1",
            Input.UP to "a:$AXIS_HAT_Y:0", Input.DOWN to "a:$AXIS_HAT_Y:1",
            Input.LEFT to "a:$AXIS_HAT_X:0", Input.RIGHT to "a:$AXIS_HAT_X:1",
        )
        driveWithCrossHeld(steps = 5_000, seed = 7)
        assertCrossHeldInEveryPushAfter(0, "captured bindings")
        key(BUTTON_A, down = false)
        // The PSP's L and R moved to the triggers, the shoulder buttons to L2 and R2 instead.
        mappings = DEFAULT_MAPPINGS + mapOf(
            Input.L1 to "a:$AXIS_BRAKE:1", Input.R1 to "a:$AXIS_GAS:1", Input.L2 to "k:$BUTTON_L1", Input.R2 to "k:$BUTTON_R1",
        )
        val from = pushes.size
        driveWithCrossHeld(steps = 5_000, seed = 8)
        assertCrossHeldInEveryPushAfter(from, "L and R on the triggers")
    }

    @Test
    fun crossStaysHeldWhateverThePspRightStickDoes() {
        for (choice in PspRightStick.entries) {
            rightStick = choice
            val from = pushes.size
            driveWithCrossHeld(steps = 2_000, seed = choice.ordinal + 30)
            assertCrossHeldInEveryPushAfter(from, "$choice")
            key(BUTTON_A, down = false)
        }
    }

    @Test
    fun aHotkeyRunsOnExactlyItsKeysSoNoneRunsWhileCrossIsHeld() {
        hotkeys = RP6_HOTKEYS
        // Without Cross: each runs, and the key completing it isn't the game's.
        key(BUTTON_SELECT, down = true)
        key(BUTTON_R1, down = true)
        assertEquals(listOf(HotkeyAction.SAVE_STATE), hotkeysRun)
        assertEquals(0, GameInputState.buttons and Input.R1)
        key(BUTTON_R1, down = false)
        key(BUTTON_SELECT, down = false)
        key(BUTTON_MODE, down = true)
        key(BUTTON_X, down = true)
        assertEquals(listOf(HotkeyAction.SAVE_STATE, HotkeyAction.LIBRARY), hotkeysRun)
        key(BUTTON_X, down = false)
        key(BUTTON_MODE, down = false)
        key(BUTTON_SELECT, down = true)
        move(hatX = 1f)
        assertEquals(listOf(HotkeyAction.SAVE_STATE, HotkeyAction.LIBRARY, HotkeyAction.NEXT_SLOT), hotkeysRun)
        move()
        key(BUTTON_SELECT, down = false)
        hotkeysRun.clear()

        // With Cross held, the same presses run nothing: their extra key means no exact match, and
        // the action keys are held back while the modifier is down. Cross stays.
        val from = pushes.size
        key(BUTTON_A, down = true)
        key(BUTTON_SELECT, down = true)
        key(BUTTON_R1, down = true)
        assertEquals(0, GameInputState.buttons and Input.R1)
        key(BUTTON_R1, down = false)
        key(BUTTON_SELECT, down = false)
        key(BUTTON_MODE, down = true)
        key(BUTTON_X, down = true)
        assertEquals(0, GameInputState.buttons and Input.X)
        key(BUTTON_X, down = false)
        key(BUTTON_MODE, down = false)
        key(BUTTON_SELECT, down = true)
        move(hatX = 1f)
        assertEquals(0, GameInputState.buttons and Input.RIGHT)
        move()
        key(BUTTON_SELECT, down = false)
        assertTrue("ran $hotkeysRun", hotkeysRun.isEmpty())
        assertCrossHeldInEveryPushAfter(from, "Cross held through the combos")

        // And through a stage with those keys in it.
        driveWithCrossHeld(steps = 5_000, seed = 11, buttons = listOf(BUTTON_X, BUTTON_L1, BUTTON_R1, BUTTON_SELECT, BUTTON_MODE))
        assertTrue("ran $hotkeysRun", hotkeysRun.isEmpty())
        assertCrossHeldInEveryPushAfter(from, "a stage with the hotkeys' keys")
    }

    @Test
    fun focusMovingIntoTheScreensChildrenKeepsCrossAndOnlyLeavingItLetsGo() {
        key(BUTTON_A, down = true)
        val from = pushes.size
        gameKeys.onFocusChanged(focus(onScreen = true, inScreen = true))
        move(lx = 0.5f)
        // Focus moving to something in the screen (the game picture's view) is still the screen's focus.
        gameKeys.onFocusChanged(focus(onScreen = false, inScreen = true))
        move(lx = -0.5f)
        gameKeys.onFocusChanged(focus(onScreen = true, inScreen = true))
        move(lx = 0.2f)
        assertCrossHeldInEveryPushAfter(from, "focus in the screen")
        // Leaving the screen (a dialog, the pause menu) lets go of everything.
        gameKeys.onFocusChanged(focus(onScreen = false, inScreen = false))
        assertEquals(0, pushes.last())
    }

    @Test
    fun onlyALetGoOfEverythingOrCrosssOwnReleaseDropsItAndNothingButAPressBringsItBack() {
        // What the report describes: Cross stays down on the pad, but the game sees it up until it's pressed again.
        // releaseAllButtons() (the screen losing focus, a pause, a PSP dialog) or a KeyUp for Cross (as Android sends,
        // FLAG_CANCELED, when it cancels a held key) is all that does it; afterwards no move or other key brings it back.
        for (drop in listOf<() -> Unit>({ GameInputState.releaseAllButtons() }, { key(BUTTON_A, down = false) })) {
            GameInputState.reset()
            key(BUTTON_A, down = true)
            move(lx = 0.5f)
            drop()
            val after = pushes.size
            move(lx = -0.7f, hatX = 1f)
            tap(BUTTON_X)
            tap(BUTTON_L1)
            move(lx = 0.2f)
            key(BUTTON_A, down = true, repeat = 1)
            for (n in after until pushes.size) assertEquals("push $n", 0, pushes[n] and Input.A)
            key(BUTTON_A, down = true)
            assertEquals(Input.A, pushes.last() and Input.A)
            key(BUTTON_A, down = false)
        }
    }

    companion object {
        // KeyEvent and MotionEvent constants (android.view), as the RP6 reports them.
        const val BUTTON_A = 96
        const val BUTTON_X = 99
        const val BUTTON_L1 = 102
        const val BUTTON_R1 = 103
        const val BUTTON_START = 108
        const val BUTTON_SELECT = 109
        const val BUTTON_MODE = 110
        const val DPAD_LEFT = 21
        const val DPAD_RIGHT = 22
        const val AXIS_X = 0
        const val AXIS_Y = 1
        const val AXIS_Z = 11
        const val AXIS_RZ = 14
        const val AXIS_HAT_X = 15
        const val AXIS_HAT_Y = 16
        const val AXIS_LTRIGGER = 17
        const val AXIS_RTRIGGER = 18
        const val AXIS_GAS = 22
        const val AXIS_BRAKE = 23

        /** SettingsStore.initializeDefaults()'s bindings. */
        val DEFAULT_MAPPINGS = mapOf(
            1 shl 0 to "k:19", 1 shl 1 to "k:20", 1 shl 2 to "k:21", 1 shl 3 to "k:22",
            1 shl 4 to "k:96", 1 shl 5 to "k:97", 1 shl 6 to "k:99", 1 shl 7 to "k:100",
            1 shl 8 to "k:102", 1 shl 9 to "k:103", 1 shl 10 to "k:104", 1 shl 11 to "k:105",
            1 shl 12 to "k:106", 1 shl 13 to "k:107", 1 shl 14 to "k:109", 1 shl 15 to "k:108",
            1 shl 17 to "a:1:0", 1 shl 18 to "a:1:1", 1 shl 19 to "a:0:0", 1 shl 20 to "a:0:1",
            1 shl 21 to "a:14:0", 1 shl 22 to "a:14:1", 1 shl 23 to "a:11:0", 1 shl 24 to "a:11:1",
        )

        /** Combos an RP6 owner might set without Z or C, on Select and the Xbox button. */
        val RP6_HOTKEYS = mapOf(
            HotkeyAction.PAUSE to listOf(BUTTON_SELECT, BUTTON_START),
            HotkeyAction.SAVE_STATE to listOf(BUTTON_SELECT, BUTTON_R1),
            HotkeyAction.LOAD_STATE to listOf(BUTTON_SELECT, BUTTON_L1),
            HotkeyAction.NEXT_SLOT to listOf(BUTTON_SELECT, DPAD_RIGHT),
            HotkeyAction.PREVIOUS_SLOT to listOf(BUTTON_SELECT, DPAD_LEFT),
            HotkeyAction.LIBRARY to listOf(BUTTON_MODE, BUTTON_X),
        )
    }
}
