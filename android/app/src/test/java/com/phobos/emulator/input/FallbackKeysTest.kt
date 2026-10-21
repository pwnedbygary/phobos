package com.phobos.emulator.input

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FallbackKeysTest {
    private val keys = FallbackKeys()

    private fun fallback(keyCode: Int, scanCode: Int, downTime: Long, device: Int = CONTROLS, flags: Int = FROM_SYSTEM) =
        keys.isFallback(keyCode, device, scanCode, downTime, flags)

    @Test
    fun theBackTheRp6GivesForAnUnhandledBIsAFallbackThoughUnmarkedAndItsOwnBackIsnt() {
        // As the RP6 sent them on 2026-10-20: B, unhandled on the Hotkeys page, then Back with B's scan code and down
        // time and the same flags (FLAG_FROM_SYSTEM alone).
        assertFalse("B is a press", fallback(BUTTON_B, BTN_EAST, 409816264))
        assertTrue("the Back for it is a fallback", fallback(BACK, BTN_EAST, 409816264))
        assertFalse("B's release is B's", fallback(BUTTON_B, BTN_EAST, 409816264))
        assertTrue("and so is the Back for it", fallback(BACK, BTN_EAST, 409816264))
        assertFalse("the controls' Back button is a press", fallback(BACK, KEY_BACK, 409800596))
        assertFalse("its release too", fallback(BACK, KEY_BACK, 409800596))
    }

    @Test
    fun aMarkedFallbackIsOneWhateverCameBefore() {
        assertTrue(fallback(BACK, BTN_EAST, 1_000, flags = FROM_SYSTEM or FALLBACK))
    }

    @Test
    fun aKeyBetweenAPressAndItsFallbackDoesntHideIt() {
        assertFalse(fallback(BUTTON_B, BTN_EAST, 1_000))
        assertFalse(fallback(BUTTON_A, BTN_SOUTH, 1_005))
        assertTrue("B's Back", fallback(BACK, BTN_EAST, 1_000))
        assertTrue("A's D-pad centre", fallback(DPAD_CENTER, BTN_SOUTH, 1_005))
    }

    @Test
    fun anotherPressOrAnotherDeviceIsntTheOriginal() {
        assertFalse(fallback(BUTTON_B, BTN_EAST, 1_000))
        assertFalse("another device's key with that scan code and down time", fallback(BACK, BTN_EAST, 1_000, device = 9))
        assertFalse("B pressed again", fallback(BUTTON_B, BTN_EAST, 2_000))
        assertFalse("a Back with the earlier press's down time", fallback(BACK, BTN_EAST, 1_000))
    }

    @Test
    fun theNavigationBarsBackIsTheSystemsAndAControllersIsnt() {
        assertTrue("the navigation bar's or gesture's Back", isSystemBack(BACK, VIRTUAL_KEYBOARD))
        assertFalse("the controls' Back", isSystemBack(BACK, CONTROLS))
        assertFalse("another key the system types", isSystemBack(BUTTON_B, VIRTUAL_KEYBOARD))
    }

    @Test
    fun keysWithNoScanCodeArentToldByTheKeyBefore() {
        // What adb or the system types has no scan code: the navigation bar's Back, say.
        assertFalse(fallback(BUTTON_B, 0, 1_000, device = VIRTUAL_KEYBOARD))
        assertFalse(fallback(BACK, 0, 1_000, device = VIRTUAL_KEYBOARD))
    }

    companion object {
        // KeyEvent's key codes and flags (android.view), Linux's scan codes (input-event-codes.h), and the device ID
        // the RP6 gave its controls.
        const val BACK = 4
        const val DPAD_CENTER = 23
        const val BUTTON_A = 96
        const val BUTTON_B = 97
        const val FROM_SYSTEM = 0x8
        const val FALLBACK = 0x400
        const val KEY_BACK = 158
        const val BTN_SOUTH = 304
        const val BTN_EAST = 305
        const val CONTROLS = 8
        const val VIRTUAL_KEYBOARD = -1
    }
}
