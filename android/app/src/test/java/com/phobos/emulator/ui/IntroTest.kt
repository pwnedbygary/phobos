package com.phobos.emulator.ui

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class IntroTest {
    private fun plays(fresh: Boolean = true, game: Boolean = false, on: Boolean = true, animations: Boolean = true,
                      talkBack: Boolean = false) =
        introPlays(freshStart = fresh, gameRequested = game, settingOn = on, animationsOn = animations,
            touchExploration = talkBack)

    @Test
    fun theIntroPlaysOnAFreshStartToTheLibraryAlone() {
        assertTrue(plays())
        assertFalse("made again (a rotation, the process back)", plays(fresh = false))
        assertFalse("a frontend's game", plays(game = true))
        assertFalse("turned off", plays(on = false))
        assertFalse("the system's animations removed", plays(animations = false))
        assertFalse("TalkBack", plays(talkBack = true))
    }

    private val keys = IntroKeys()
    private var skips = 0
    private fun key(code: Int, down: Boolean, repeat: Int = 0, playing: Boolean) =
        keys.onKey(code, down, repeat, playing) { skips++ }

    @Test
    fun whileItPlaysEveryKeyIsItsOwnAndEachPressSkipsIt() {
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A, down = true, playing = true))
        assertEquals(1, skips)
        assertTrue("a repeat is taken", key(KeyEvent.KEYCODE_BUTTON_A, down = true, repeat = 1, playing = true))
        assertEquals("and skips nothing more", 1, skips)
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A, down = false, playing = true))
        assertTrue("Back too, which would leave the app", key(KeyEvent.KEYCODE_BACK, down = true, playing = true))
        assertTrue(key(KeyEvent.KEYCODE_BACK, down = false, playing = true))
        assertEquals(2, skips)
    }

    @Test
    fun aKeyPressedDuringItIsTheIntrosUntilLetGoOfAndTheRestTheLibrarys() {
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = true))
        assertTrue(key(KeyEvent.KEYCODE_ENTER, down = true, playing = true))
        // the intro over, both still held
        assertTrue("its repeats", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 3, playing = false))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
        assertFalse("pressed again: the Library's", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
        // a press whose release was lost is the Library's all the same
        assertFalse(key(KeyEvent.KEYCODE_ENTER, down = true, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_BUTTON_B, down = true, playing = false))
        assertEquals(2, skips)
    }

    @Test
    fun aRepeatWhosePressItNeverHadSkipsItAndKeepsTheKeyUntilLetGoOf() {
        // After a touch, Android keeps the first D-pad press to leave touch mode: the repeats come here, the press
        // doesn't.
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = true))
        assertEquals(1, skips)
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 2, playing = true))
        assertEquals("once", 1, skips)
        // the intro over, the key still held
        assertTrue("its repeats", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 9, playing = false))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
        assertFalse("pressed again: the Library's", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false))
        assertEquals(1, skips)
    }

    @Test
    fun touchModeEndingWhileItPlaysSkipsItAndThatPressesRepeatsAndReleaseAreItsToo() {
        keys.onUnseenPress(playing = true) { skips++ }
        assertEquals(1, skips)
        // The skip has ended the intro by the first repeat (a skip takes 300 ms, Android's first repeat 400).
        assertTrue("its repeats", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 2, playing = false))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
        assertFalse("pressed again: the Library's", key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false))
        assertEquals(1, skips)
    }

    @Test
    fun aBriefUnseenPressHasItsReleaseTakenAndNothingAfter() {
        keys.onUnseenPress(playing = true) { skips++ }
        assertTrue(key(KeyEvent.KEYCODE_DPAD_UP, down = false, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = true, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = true, repeat = 1, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = false, playing = false))
    }

    @Test
    fun aPressThatLeavesTouchModeAndComesHereAnywayLeavesTheLibraryItsNextKeyWhole() {
        // A typing key leaves touch mode and still reaches the window: there was no unseen press.
        keys.onUnseenPress(playing = true) { skips++ }
        assertTrue(key(KeyEvent.KEYCODE_X, down = true, playing = true))
        assertTrue(key(KeyEvent.KEYCODE_X, down = false, playing = true))
        // the intro over: a press and its release, then a held key's repeats and release, all the Library's
        assertFalse(key(KeyEvent.KEYCODE_ENTER, down = true, playing = false))
        assertFalse("the release of the Library's press", key(KeyEvent.KEYCODE_ENTER, down = false, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
        assertEquals(2, skips)
    }

    @Test
    fun touchModeEndingAfterItIsTheLibrarysBusiness() {
        keys.onUnseenPress(playing = false) { skips++ }
        assertEquals(0, skips)
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false))
    }

    @Test
    fun theVolumeKeysStayTheSystems() {
        for (code in listOf(KeyEvent.KEYCODE_VOLUME_UP, KeyEvent.KEYCODE_VOLUME_DOWN, KeyEvent.KEYCODE_VOLUME_MUTE)) {
            assertFalse(key(code, down = true, playing = true))
            assertFalse(key(code, down = false, playing = true))
        }
        assertEquals(0, skips)
    }
}
