package com.phobos.emulator.input

import com.phobos.emulator.PhobosCore.Input
import com.phobos.emulator.input.PspRightStick.ANALOG
import com.phobos.emulator.input.PspRightStick.DPAD
import com.phobos.emulator.input.PspRightStick.FACE_BUTTONS
import com.phobos.emulator.input.PspRightStick.OFF
import com.phobos.emulator.launch.LaunchSystems
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class PspRightStickTest {
    private val psp = LaunchSystems.PSP
    private val diagonal = 0.7071f

    @Before
    fun startReleased() = GameInputState.reset()

    /** A stick move as the motion event's bindings give it: the right stick at [rx], [ry], screen-down positive. */
    private fun move(rx: Float, ry: Float, choice: PspRightStick, system: String = psp, lx: Float = 0f, ly: Float = 0f) =
        GameInputState.updateSticks(lx, ly, rx, ry, system, choice)

    private val buttons get() = GameInputState.buttons

    @Test
    fun faceButtonsAreThePspsDiamond() {
        move(0f, -1f, FACE_BUTTONS); assertEquals(Input.Y, buttons)  // up: Triangle
        move(1f, 0f, FACE_BUTTONS); assertEquals(Input.B, buttons)   // right: Circle
        move(0f, 1f, FACE_BUTTONS); assertEquals(Input.A, buttons)   // down: Cross
        move(-1f, 0f, FACE_BUTTONS); assertEquals(Input.X, buttons)  // left: Square
        move(0f, 0f, FACE_BUTTONS); assertEquals(0, buttons)
    }

    @Test
    fun theDpadChoicePressesTheSticksDirections() {
        move(0f, -1f, DPAD); assertEquals(Input.UP, buttons)
        move(1f, 0f, DPAD); assertEquals(Input.RIGHT, buttons)
        move(0f, 1f, DPAD); assertEquals(Input.DOWN, buttons)
        move(-1f, 0f, DPAD); assertEquals(Input.LEFT, buttons)
        move(0f, 0f, DPAD); assertEquals(0, buttons)
    }

    @Test
    fun offAndAnalogPressNothing() {
        for (choice in listOf(OFF, ANALOG)) {
            move(diagonal, -diagonal, choice); assertEquals(0, buttons)
            move(0f, 1f, choice); assertEquals(0, buttons)
            for (directions in 0..15) assertEquals(0, choice.buttons(directions))
        }
    }

    @Test
    fun aDiagonalPressesTwo() {
        move(diagonal, -diagonal, FACE_BUTTONS); assertEquals(Input.Y or Input.B, buttons)  // up-right: Triangle and Circle
        move(-diagonal, diagonal, FACE_BUTTONS); assertEquals(Input.A or Input.X, buttons)  // down-left: Cross and Square
        move(-diagonal, -diagonal, DPAD); assertEquals(Input.UP or Input.LEFT, buttons)
        // From the middle, nearer right than up-right: up isn't past half, so Circle alone.
        move(0f, 0f, FACE_BUTTONS)
        move(0.89f, -0.45f, FACE_BUTTONS); assertEquals(Input.B, buttons)
    }

    @Test
    fun aDirectionPressesPastHalfAndLetsGoBelowFourTenths() {
        // A stick hovering around either threshold doesn't chatter.
        val steps = listOf(
            -0.5f to 0, -0.55f to Input.Y, -0.45f to Input.Y, -0.52f to Input.Y, -0.41f to Input.Y,
            -0.4f to 0, -0.45f to 0, -0.5f to 0, -0.51f to Input.Y,
        )
        for ((ry, held) in steps) {
            move(0f, ry, FACE_BUTTONS)
            assertEquals("at $ry", held, buttons)
        }
    }

    @Test
    fun theStickLatchesAsTheN64sCButtonsDo() {
        // The N64's stick-as-button bits go through the same test (GameInputState.handleMotionEvent).
        assertFalse(GameInputState.axisPressed(0.5f, wasPressed = false))
        assertTrue(GameInputState.axisPressed(0.51f, wasPressed = false))
        assertTrue(GameInputState.axisPressed(0.41f, wasPressed = true))
        assertFalse(GameInputState.axisPressed(0.4f, wasPressed = true))
        assertEquals(Input.UP or Input.RIGHT, GameInputState.stickDirections(0.45f, -0.45f, held = Input.UP or Input.RIGHT))
        assertEquals(0, GameInputState.stickDirections(0.45f, -0.45f, held = 0))
    }

    @Test
    fun aButtonHeldOnThePadAndFromTheStickLetsGoOnlyWhenBothDo() {
        GameInputState.hwButtons = Input.Y  // Triangle held on the pad,
        move(0f, -1f, FACE_BUTTONS)         // and pushed up on the right stick.
        assertEquals(Input.Y, buttons)
        move(0f, 0f, FACE_BUTTONS)          // The stick lets go: the pad still holds it.
        assertEquals(Input.Y, buttons)
        move(0f, -1f, FACE_BUTTONS)
        GameInputState.hwButtons = 0        // The pad lets go: the stick still holds it.
        assertEquals(Input.Y, buttons)
        move(0f, 0f, FACE_BUTTONS)
        assertEquals(0, buttons)
    }

    @Test
    fun theSticksPressesAddToThePadsAndTheTouchControls() {
        GameInputState.hwButtons = Input.UP or Input.L1  // the pad's D-pad up and L
        GameInputState.virtualButtons = Input.A          // Cross on the touch controls
        move(0f, -1f, DPAD)
        assertEquals(Input.UP or Input.L1 or Input.A, buttons)
        move(0f, 0f, DPAD)
        assertEquals(Input.UP or Input.L1 or Input.A, buttons)
        move(1f, 0f, FACE_BUTTONS)
        assertEquals(Input.UP or Input.L1 or Input.A or Input.B, buttons)
        assertEquals(Input.B, GameInputState.rightStickButtons)
    }

    @Test
    fun aNewChoiceTakesEffectAtTheSticksNextMove() {
        move(0f, -1f, FACE_BUTTONS); assertEquals(Input.Y, buttons)
        move(0f, -1f, DPAD); assertEquals(Input.UP, buttons)
        move(0f, -1f, OFF); assertEquals(0, buttons)
        move(0f, -0.45f, OFF)
        // Still past the release threshold since it was pushed: held, though Off pressed nothing.
        move(0f, -0.45f, FACE_BUTTONS); assertEquals(Input.Y, buttons)
    }

    @Test
    fun otherSystemsKeepTheRightStickAnalog() {
        move(0.9f, -0.9f, FACE_BUTTONS, system = "PlayStation")
        assertEquals(0, buttons)
        assertEquals(0.9f, GameInputState.rx, 0f)
        assertEquals(-0.9f, GameInputState.ry, 0f)
        // The N64's C-buttons come from its stick bits' own bindings, not this setting.
        move(0.9f, -0.9f, DPAD, system = "Nintendo 64")
        assertEquals(0, buttons)
    }

    @Test
    fun analogMakesTheRightStickThePspsStick() {
        move(-0.8f, 0.6f, ANALOG, lx = 0.3f, ly = 0.1f)
        assertEquals(-0.8f, GameInputState.lx, 0f)
        assertEquals(0.6f, GameInputState.ly, 0f)
        // The left stick takes the right's place, which the PSP doesn't read.
        assertEquals(0.3f, GameInputState.rx, 0f)
        assertEquals(0.1f, GameInputState.ry, 0f)

        move(-0.8f, 0.6f, OFF, lx = 0.3f, ly = 0.1f)
        assertEquals(0.3f, GameInputState.lx, 0f)
        assertEquals(-0.8f, GameInputState.rx, 0f)

        move(-0.8f, 0.6f, ANALOG, system = "PlayStation", lx = 0.3f, ly = 0.1f)
        assertEquals(0.3f, GameInputState.lx, 0f)
        assertEquals(0.6f, GameInputState.ry, 0f)
    }

    @Test
    fun resetLetsGoOfTheStick() {
        move(0f, -1f, FACE_BUTTONS)
        GameInputState.reset()
        assertEquals(0, buttons)
        // Released, a direction presses only past half again.
        move(0f, -0.45f, FACE_BUTTONS)
        assertEquals(0, buttons)
    }
}
