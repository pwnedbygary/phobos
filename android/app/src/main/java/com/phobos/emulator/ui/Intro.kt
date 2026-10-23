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
 * press, and so does touch mode ending ([onUnseenPress]), the next event of a key it doesn't hold telling which it
 * was: a repeat or a release is that press's, a press means the window had it after all.
 */
class IntroKeys {
    private val held = mutableSetOf<Int>()
    private var unseenPress = false

    /**
     * Touch mode ended while the intro is [playing] or not: a press the window didn't have. [skip] runs while it plays.
     */
    fun onUnseenPress(playing: Boolean, skip: () -> Unit) {
        if (!playing) return
        unseenPress = true
        skip()
    }

    /**
     * A key event while the intro is [playing] or not; true when it's taken. [skip] runs on each press while it plays.
     */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int, playing: Boolean, skip: () -> Unit): Boolean {
        if (keyCode in VOLUME_KEYS) return false
        // A skip ends the intro before Android's first repeat comes, so this holds after it too.
        if (unseenPress && keyCode !in held) {
            unseenPress = false
            if (!down || repeatCount > 0) {
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
}
