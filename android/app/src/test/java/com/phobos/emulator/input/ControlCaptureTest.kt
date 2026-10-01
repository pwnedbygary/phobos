package com.phobos.emulator.input

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ControlCaptureTest {
    private val scope = CoroutineScope(Dispatchers.Unconfined)
    private val capture = ControlCapture(scope)
    private val up = 1 shl 0
    private val a = 1 shl 4

    @After
    fun tearDown() = scope.cancel()

    // Key codes: 4 BACK, 24 VOLUME_UP, 96 BUTTON_A, 97 BUTTON_B, 101 BUTTON_Z, 22 DPAD_RIGHT.
    private fun key(code: Int, down: Boolean, now: Long, repeat: Int = 0, systemBack: Boolean = false) =
        capture.onKey(code, down, repeat, systemBack, now)

    @Test
    fun aCapturedPressBindsAndItsRepeatsAndReleaseGoNowhereElse() {
        var bound: String? = null
        capture.captureButton(a, "A") { bound = it }
        assertTrue(key(97, down = true, now = 1_000))
        assertEquals("k:97", bound)
        assertNull(capture.target.value)
        // Another press straight after is the menu's, Back included.
        assertFalse(key(4, down = true, now = 1_050))
        // Still held after the capture: the repeats and the release stay with it.
        assertTrue(key(97, down = true, now = 2_000, repeat = 1))
        assertTrue(key(97, down = false, now = 2_100))
        // The next press is the game's or the menu's again.
        assertFalse(key(97, down = true, now = 3_000))
        assertFalse(key(97, down = false, now = 3_100))
    }

    @Test
    fun aTriggersKeyFollowingItsAxisIsSwallowedAndNothingElse() {
        var bound: String? = null
        capture.captureButton(1 shl 10, "L2") { bound = it }
        // Axis 17 is LTRIGGER; key 104 is BUTTON_L2.
        assertTrue(capture.onJoystickMove({ if (it == 17) 1f else 0f }, now = 1_000))
        assertEquals("a:17:1", bound)
        assertTrue(key(104, down = true, now = 1_020))
        assertTrue(key(104, down = false, now = 1_300))
        assertFalse(key(96, down = true, now = 1_030))
        assertFalse(key(104, down = true, now = 2_000))
    }

    @Test
    fun anAxisStillHeldAfterItBindsIsTakenUntilLetGo() {
        capture.captureButton(up, "Up") {}
        assertTrue(capture.onJoystickMove({ if (it == 16) -1f else 0f }, now = 1_000))
        assertTrue(capture.onJoystickMove({ if (it == 16) -1f else 0.1f }, now = 1_400))
        assertTrue(capture.onJoystickMove({ 0f }, now = 1_600))
        assertFalse(capture.onJoystickMove({ if (it == 0) 0.9f else 0f }, now = 1_700))
    }

    @Test
    fun endingWithoutABindingStillHoldsBackADeflectedStick() {
        capture.captureButton(up, "Up") {}
        // A stick, which the D-pad ignores, then the navigation bar's Back.
        assertTrue(capture.onJoystickMove({ if (it == 0) 0.9f else 0f }, now = 1_000))
        assertTrue(key(4, down = true, now = 1_100, systemBack = true))
        assertNull(capture.target.value)
        assertTrue(capture.onJoystickMove({ if (it == 0) 0.9f else 0f }, now = 1_200))
        assertTrue(capture.onJoystickMove({ 0f }, now = 1_300))
        assertFalse(capture.onJoystickMove({ if (it == 1) 0.9f else 0f }, now = 1_400))
    }

    @Test
    fun closingThePageHandsAHeldAxisBack() {
        capture.captureButton(up, "Up") {}
        capture.onJoystickMove({ if (it == 16) -1f else 0f }, now = 1_000)
        capture.cancel()
        assertFalse(capture.onJoystickMove({ if (it == 16) -1f else 0f }, now = 1_200))
    }

    @Test
    fun aKeyHeldFromBeforeTheCaptureIsReleasedAsUsual() {
        assertFalse(key(101, down = true, now = 500))
        capture.captureButton(a, "A") {}
        assertFalse(key(101, down = false, now = 1_000))
        assertNotNull(capture.target.value)
    }

    @Test
    fun aKeyWhoseReleaseWasLostReachesTheGameAgain() {
        capture.captureButton(a, "A") {}
        key(97, down = true, now = 1_000)
        assertFalse(key(97, down = true, now = 5_000))
        assertFalse(key(97, down = false, now = 5_100))
    }

    @Test
    fun theSystemBackCancelsWhileAControllerBackBinds() {
        var bound: String? = null
        capture.captureButton(a, "A") { bound = it }
        assertTrue(key(4, down = true, now = 1_000, systemBack = true))
        assertNull(bound)
        assertNull(capture.target.value)
        capture.captureButton(a, "A") { bound = it }
        assertTrue(key(4, down = true, now = 2_000))
        assertEquals("k:4", bound)
    }

    @Test
    fun keysPassWhenNothingIsCapturedAndVolumeKeysAlways() {
        assertFalse(key(96, down = true, now = 1_000))
        capture.captureButton(a, "A") {}
        assertFalse(key(24, down = true, now = 1_100))
        assertNotNull(capture.target.value)
    }

    @Test
    fun aComboTakesEachKeyOnceAndTheHatAsDpadKeys() {
        capture.captureHotkey("pause", "Pause Emulation") {}
        assertTrue(key(101, down = true, now = 1_000))
        assertTrue(key(101, down = true, now = 1_050))
        assertTrue(capture.onJoystickMove({ if (it == 15) 1f else 0f }, now = 1_100))
        assertEquals(listOf(101, 22), capture.combo.value)
        // Once it ends, the held hat's D-pad key repeats and releases go nowhere else; a new press does.
        capture.cancel()
        assertTrue(key(22, down = true, now = 2_500, repeat = 3))
        assertTrue(key(22, down = false, now = 2_600))
        assertFalse(key(22, down = true, now = 3_000))
    }

    @Test
    fun stickMovesPassWhenIdleAndAreAllTakenWhileCapturing() {
        assertFalse(capture.onJoystickMove({ 1f }, now = 1_000))
        var bound: String? = null
        capture.captureButton(up, "Up") { bound = it }
        assertTrue(capture.onJoystickMove({ if (it == 0) 0.9f else 0f }, now = 1_100))
        assertNull(bound)
        assertTrue(capture.onJoystickMove({ if (it == 16) -1f else 0f }, now = 1_200))
        assertEquals("a:16:0", bound)
    }
}
