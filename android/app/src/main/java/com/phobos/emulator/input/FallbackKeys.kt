package com.phobos.emulator.input

import android.view.KeyCharacterMap
import android.view.KeyEvent

/** The navigation bar's or gesture's Back, a key with no predictive back (the manifest), and no controller's. */
fun isSystemBack(keyCode: Int, deviceId: Int): Boolean =
    keyCode == KeyEvent.KEYCODE_BACK && deviceId == KeyCharacterMap.VIRTUAL_KEYBOARD

/**
 * Tells a key Android makes up for one the app left unhandled (a fallback: the Back an unhandled B gives, say) from a
 * press of its own. Android marks a fallback with FLAG_FALLBACK, but the RP6 sends it unmarked, with the flags of a
 * press; it still comes with the device, scan code and down time of the key it stands for, under another key code.
 */
class FallbackKeys {
    // Per device and scan code, the down time and key code of the latest key that wasn't a fallback.
    private val keys = HashMap<Long, Pair<Long, Int>>()

    /** Whether the key is a fallback; every key the window gets goes through here, so its originals are known. */
    fun isFallback(keyCode: Int, deviceId: Int, scanCode: Int, downTime: Long, flags: Int): Boolean {
        if ((flags and KeyEvent.FLAG_FALLBACK) != 0) return true
        // A key with no scan code (one the system or adb makes up) can't be told this way.
        if (scanCode == 0) return false
        val key = (deviceId.toLong() shl 32) or scanCode.toLong()
        val original = keys[key]
        if (original != null && original.first == downTime && original.second != keyCode) return true
        keys[key] = downTime to keyCode
        return false
    }
}
