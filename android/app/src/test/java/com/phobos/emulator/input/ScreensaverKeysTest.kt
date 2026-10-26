package com.phobos.emulator.input

import android.view.KeyEvent
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ScreensaverKeysTest {
    private val keys = ScreensaverKeys()

    private fun key(code: Int, down: Boolean, showing: Boolean, repeat: Int = 0) =
        keys.onKey(code, down, repeat, showing)

    @Test
    fun whileItShowsEveryKeyIsItsOwnItsRepeatsAndReleaseWithItAfterItsGone() {
        assertTrue(key(A, down = true, showing = true))
        assertTrue("a repeat, the screensaver gone meanwhile", key(A, down = true, showing = false, repeat = 1))
        assertTrue("its release", key(A, down = false, showing = false))
        assertFalse("the next press is the pages'", key(A, down = true, showing = false))
        assertFalse(key(A, down = false, showing = false))
    }

    @Test
    fun aKeyLetGoOfWhileItStillShowsLeavesNothingHeld() {
        assertTrue(key(A, down = true, showing = true))
        assertTrue(key(A, down = false, showing = true))
        assertFalse("a press once it's gone is the pages'", key(START, down = true, showing = false))
    }

    @Test
    fun theRestOfAComboPressedToPutItAwayIsItsOwnUntilLetGoOf() {
        // Back + Start, which shows it, pressed again: Back's press puts it away, and Start comes after.
        assertTrue(key(BACK, down = true, showing = true))
        assertTrue("Start, pressed with Back held, the screensaver gone", key(START, down = true, showing = false))
        assertTrue(key(START, down = false, showing = false))
        assertTrue("pressed again while Back is still held", key(START, down = true, showing = false))
        assertTrue(key(START, down = false, showing = false))
        assertTrue("Back's release", key(BACK, down = false, showing = false))
        assertFalse("with none of them held, a press is the pages'", key(START, down = true, showing = false))
        assertFalse(key(START, down = false, showing = false))
    }

    @Test
    fun aKeyHeldFromBeforeItShowedIsTakenOnlyWhileItShows() {
        // The D-pad held as the screensaver came: its press was the pages'.
        assertTrue("a repeat while it shows", key(DPAD_DOWN, down = true, showing = true, repeat = 5))
        assertFalse("its release once it's gone is the pages'", key(DPAD_DOWN, down = false, showing = false))
    }

    @Test
    fun aNewPressOnceItsGoneDecidesAfreshThoughTheLastReleaseWasLost() {
        assertTrue(key(A, down = true, showing = true)) // its release never comes
        assertFalse("pressed again with nothing else held: the pages'", key(A, down = true, showing = false))
        assertFalse(key(A, down = false, showing = false))
        assertFalse("and the next key's too", key(START, down = true, showing = false))
    }

    @Test
    fun theVolumeKeysStayTheSystems() {
        assertFalse(key(VOLUME_UP, down = true, showing = true))
        assertFalse(key(VOLUME_UP, down = false, showing = true))
        assertTrue(key(A, down = true, showing = true))
        assertFalse("not even while a key that put it away is held", key(VOLUME_UP, down = true, showing = false))
        assertFalse(key(VOLUME_UP, down = false, showing = false))
    }

    @Test
    fun aControllerGoneForgetsTheKeysItHeld() {
        assertTrue(key(BACK, down = true, showing = true))
        keys.reset()
        assertFalse("a press after is the pages'", key(START, down = true, showing = false))
    }

    private companion object {
        const val A = KeyEvent.KEYCODE_BUTTON_A
        const val BACK = KeyEvent.KEYCODE_BACK
        const val START = KeyEvent.KEYCODE_BUTTON_START
        const val DPAD_DOWN = KeyEvent.KEYCODE_DPAD_DOWN
        const val VOLUME_UP = KeyEvent.KEYCODE_VOLUME_UP
    }
}
