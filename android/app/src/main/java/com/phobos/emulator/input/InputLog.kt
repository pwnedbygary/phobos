package com.phobos.emulator.input

import android.os.SystemClock
import android.util.Log
import android.view.KeyEvent

/**
 * An opt-in log of the controller input a game gets, to see on a device where a held button is let go of or a press
 * lost: `adb shell setprop log.tag.PhobosInput DEBUG`, then start the game (`adb logcat -s PhobosInput`). It logs each
 * key event the window is given ("window"), which hands a running game's keys straight to its key handler (WindowKeys);
 * each change of the buttons sent to the core; a let-go of every button, with its caller,
 * and a reset; and the window's, the activity's and the game screen's focus. Times are uptime milliseconds, the clock
 * `adb shell getevent -lt` stamps the kernel's events with (in seconds).
 */
object InputLog {
    private const val TAG = "PhobosInput"

    /** Read again as a game starts; false in the JVM tests, which have no Log. */
    @Volatile
    var enabled = false
        private set

    fun refresh() {
        enabled = try {
            Log.isLoggable(TAG, Log.DEBUG)
        } catch (_: RuntimeException) {
            false
        }
    }

    fun key(where: String, event: KeyEvent) {
        if (!enabled) return
        val action = when (event.action) {
            KeyEvent.ACTION_DOWN -> "down"
            KeyEvent.ACTION_UP -> "up"
            else -> "action ${event.action}"
        }
        Log.d(TAG, "$where key ${event.keyCode} $action repeat ${event.repeatCount} flags 0x${hex(event.flags)} " +
            "scan ${event.scanCode} device ${event.deviceId} source 0x${hex(event.source)} down at ${event.downTime} " +
            "at ${event.eventTime}")
    }

    fun buttons(from: Int, to: Int) {
        if (enabled) Log.d(TAG, "buttons 0x${hex(from)} -> 0x${hex(to)} at ${SystemClock.uptimeMillis()}")
    }

    fun letGoOfAll(buttons: Int) {
        if (enabled) Log.d(TAG, "let go of all 0x${hex(buttons)} at ${SystemClock.uptimeMillis()}", Throwable("from"))
    }

    /** [what] is only built while the log is on. */
    inline fun note(what: () -> String) {
        if (enabled) write("${what()} at ${SystemClock.uptimeMillis()}")
    }

    @PublishedApi
    internal fun write(text: String) {
        Log.d(TAG, text)
    }

    private fun hex(value: Int) = Integer.toHexString(value)
}
