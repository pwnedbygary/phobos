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
    private fun key(code: Int, down: Boolean, repeat: Int = 0, playing: Boolean, at: Long = 0L) =
        keys.onKey(code, down, repeat, at, playing) { skips++ }
    // repeatDelay: the system's key-repeat delay, 400 ms by default
    private fun touchModeEnds(playing: Boolean, at: Long, repeatDelay: Long = 400L) =
        keys.onUnseenPress(playing, at, repeatDelay) { skips++ }

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
        touchModeEnds(playing = true, at = 10_000)
        assertEquals(1, skips)
        // The skip has ended the intro by the first repeat (a skip takes 300 ms, Android's first repeat 400).
        assertTrue("its repeats",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_400))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 2, playing = false, at = 10_450))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 11_000))
        assertFalse("pressed again: the Library's",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false, at = 12_000))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 12_400))
        assertEquals(1, skips)
    }

    @Test
    fun aBriefUnseenPressHasItsReleaseTakenAndNothingAfter() {
        touchModeEnds(playing = true, at = 10_000)
        assertTrue(key(KeyEvent.KEYCODE_DPAD_UP, down = false, playing = false, at = 10_080))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = true, playing = false, at = 10_500))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = true, repeat = 1, playing = false, at = 10_900))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_LEFT, down = false, playing = false, at = 11_000))
    }

    @Test
    fun aPressThatLeavesTouchModeAndComesHereAnywayLeavesTheLibraryItsNextKeyWhole() {
        // A typing key leaves touch mode and still reaches the window, its press from before touch mode ended: there
        // was no unseen press.
        touchModeEnds(playing = true, at = 10_000)
        assertTrue(key(KeyEvent.KEYCODE_X, down = true, playing = true, at = 9_998))
        assertTrue(key(KeyEvent.KEYCODE_X, down = false, playing = true, at = 10_100))
        // the intro over: a release whose press came before it, a press and its release, then a held key's repeats
        // and release, all the Library's
        assertFalse(key(KeyEvent.KEYCODE_BUTTON_START, down = false, playing = false, at = 10_400))
        assertFalse(key(KeyEvent.KEYCODE_ENTER, down = true, playing = false, at = 10_600))
        assertFalse("the release of the Library's press",
            key(KeyEvent.KEYCODE_ENTER, down = false, playing = false, at = 10_700))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false, at = 11_000))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 11_400))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 11_500))
        assertEquals(2, skips)
    }

    @Test
    fun aSecondKeyPressedAtOnceLeavesTheKeptPressTheIntros() {
        // A pad's D-pad down kept by Android to leave touch mode, its A pressed 60 ms later, as on the RP6.
        touchModeEnds(playing = true, at = 10_000)
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A, down = true, playing = true, at = 10_060))
        assertTrue(key(KeyEvent.KEYCODE_BUTTON_A, down = false, playing = true, at = 10_120))
        // the intro over: the kept press's repeats and its release are still the intro's
        assertTrue("its first repeat",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_400))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 2, playing = false, at = 10_450))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 11_600))
        assertFalse("pressed again: the Library's",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false, at = 12_000))
        assertEquals(2, skips)
    }

    @Test
    fun aKeyPressedAfterItWhileTheKeptPressWaitsIsTheLibrarysWhole() {
        touchModeEnds(playing = true, at = 10_000)
        // the intro over (a skip takes 300 ms), another key pressed and let go of before the kept press's first repeat
        assertFalse(key(KeyEvent.KEYCODE_BUTTON_B, down = true, playing = false, at = 10_320))
        assertFalse("its release, not the kept press's",
            key(KeyEvent.KEYCODE_BUTTON_B, down = false, playing = false, at = 10_380))
        assertTrue("the kept press's repeat",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_400))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 10_500))
    }

    @Test
    fun withNeitherARepeatNorAReleaseInTimeThereWasNoKeptPress() {
        touchModeEnds(playing = true, at = 10_000, repeatDelay = 400)
        // past the key-repeat delay and the margin, a release whose press came before the intro, then a key pressed
        // and held: the Library's
        assertFalse(key(KeyEvent.KEYCODE_BUTTON_START, down = false, playing = false, at = 11_100))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false, at = 11_500))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 11_900))
    }

    @Test
    fun theKeptPressWaitsAsLongAsTheSystemsKeyRepeatDelay() {
        // The longest Touch & hold delay (the key-repeat delay through Android 14): the first repeat comes 1.5 s after
        // the press.
        touchModeEnds(playing = true, at = 10_000, repeatDelay = 1_500)
        assertTrue("its first repeat",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 11_500))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 2, playing = false, at = 11_550))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 12_000))
        assertFalse("pressed again: the Library's",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = false, at = 13_000))
    }

    @Test
    fun theKeptPressWaitsAMarginMoreForAFirstRepeatComingLate() {
        touchModeEnds(playing = true, at = 10_000, repeatDelay = 400)
        // 550 ms past the delay, within the margin: still the kept press's
        assertTrue("its late first repeat",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_950))
        assertTrue("its release", key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 11_200))
    }

    @Test
    fun aPressTimedAtTheMomentTouchModeEndedIsTheOneThatEndedIt() {
        touchModeEnds(playing = true, at = 10_000)
        assertTrue(key(KeyEvent.KEYCODE_X, down = true, playing = true, at = 10_000))
        assertTrue(key(KeyEvent.KEYCODE_X, down = false, playing = true, at = 10_100))
        // the intro over: there's no kept press, so a release whose press came before it is the Library's
        assertFalse(key(KeyEvent.KEYCODE_BUTTON_START, down = false, playing = false, at = 10_300))
    }

    @Test
    fun touchModeEndingAgainForgetsTheKeysPressedSinceItEndedBefore() {
        touchModeEnds(playing = true, at = 10_000)
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, playing = true, at = 10_050))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = true, at = 10_100))
        assertTrue("the kept press's release",
            key(KeyEvent.KEYCODE_DPAD_UP, down = false, playing = true, at = 10_150))
        // a touch, then the D-pad's down pressed and kept by Android to leave touch mode again
        touchModeEnds(playing = true, at = 10_200)
        assertTrue("the second kept press's repeat",
            key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_600))
        assertTrue(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 10_700))
        assertEquals(3, skips)
    }

    @Test
    fun touchModeEndingAfterItIsTheLibrarysBusiness() {
        touchModeEnds(playing = false, at = 10_000)
        assertEquals(0, skips)
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = true, repeat = 1, playing = false, at = 10_400))
        assertFalse(key(KeyEvent.KEYCODE_DPAD_DOWN, down = false, playing = false, at = 10_500))
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
