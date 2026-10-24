package com.phobos.emulator.ui

import com.phobos.emulator.input.VOLUME_KEYS

/**
 * Whether the intro (IntroSplash) plays as the activity starts: on a fresh start to the Library ([freshStart]: not the
 * activity made again after a rotation or the process's death), not when a game is asked for (a frontend's launch,
 * adb's debug load), with the setting on, and neither with the system's animations removed nor with TalkBack exploring
 * by touch, the intro telling nothing the Library doesn't.
 */
fun introPlays(freshStart: Boolean, gameRequested: Boolean, settingOn: Boolean, animationsOn: Boolean,
               touchExploration: Boolean): Boolean =
    freshStart && !gameRequested && settingOn && animationsOn && !touchExploration

/**
 * The keys while the intro plays (MainActivity's window has them first): each is the intro's, a press skipping it,
 * but the volume keys, left to the system. A key pressed during the intro stays the intro's until it's let go of, so
 * the Library gets neither the repeats nor the release of a press it never had. After a touch, Android may keep the
 * press that leaves touch mode (a D-pad's, not a typing key's): a repeat whose press didn't come here counts as the
 * press, and so does touch mode ending ([onUnseenPress]). That press is settled by its own first repeat or its release,
 * which are the intro's (the first repeat or release of a key not pressed since), or by a press from before touch mode
 * ended, the press the window had after all; another key's press from after that settles nothing. With neither a
 * repeat nor a release within the system's key-repeat delay and [KEPT_PRESS_MARGIN_MS] more, there was no kept press.
 * Times are the events' own, in SystemClock.uptimeMillis()'s terms.
 */
class IntroKeys {
    private val held = mutableSetOf<Int>()
    private var unseenPress = false
    private var unseenAt = 0L
    private var keptPressMs = 0L
    // the keys pressed since touch mode ended: their repeats and releases are their own, not the kept press's
    private val pressedSince = mutableSetOf<Int>()

    /**
     * Touch mode ended at [now] while the intro is [playing] or not: a press the window didn't have, its first repeat
     * [keyRepeatTimeout] after it (ViewConfiguration.getKeyRepeatTimeout(): 400 ms by default; through Android 14 the
     * Touch & hold delay, from Android 15 a key-repeat setting of its own). [skip] runs while it plays.
     */
    fun onUnseenPress(playing: Boolean, now: Long, keyRepeatTimeout: Long, skip: () -> Unit) {
        if (!playing) return
        unseenPress = true
        unseenAt = now
        keptPressMs = keyRepeatTimeout + KEPT_PRESS_MARGIN_MS
        pressedSince.clear()
        skip()
    }

    /**
     * A key event at [eventTime] while the intro is [playing] or not; true when it's taken. [skip] runs on each press
     * while it plays.
     */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int, eventTime: Long, playing: Boolean,
              skip: () -> Unit): Boolean {
        if (keyCode in VOLUME_KEYS) return false
        if (unseenPress && eventTime - unseenAt > keptPressMs) unseenPress = false
        // A skip ends the intro before Android's first repeat comes, so this holds after it too.
        if (unseenPress && keyCode !in held) {
            if (down && repeatCount == 0) {
                if (eventTime <= unseenAt) unseenPress = false else pressedSince += keyCode
            } else if (keyCode !in pressedSince) {
                unseenPress = false
                if (down) held += keyCode
                return true
            }
        }
        if (playing) {
            if (!down) {
                held -= keyCode
            } else if (held.add(keyCode) || repeatCount == 0) {
                skip()
            }
            return true
        }
        if (keyCode !in held) return false
        // a press after the intro is the Library's, its release lost or not
        if (down && repeatCount == 0) {
            held -= keyCode
            return false
        }
        if (!down) held -= keyCode
        return true
    }

    companion object {
        /**
         * How long past the key-repeat delay a kept press has for its first repeat (a brief press's release comes
         * sooner), for the repeat coming late.
         */
        const val KEPT_PRESS_MARGIN_MS = 600L
    }
}
