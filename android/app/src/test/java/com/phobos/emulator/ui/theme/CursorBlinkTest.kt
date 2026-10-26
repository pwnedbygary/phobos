package com.phobos.emulator.ui.theme

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CursorBlinkTest {
    // Moments from just after boot, and a day of uptime later.
    private val moments = (0L..5_000L) + (0L until 2_000L).map { 86_400_000L + it * 37 }

    /** What a cursor whose blink began at [start] shows at [time]: its loop's passes run, each waiting as asked. */
    private fun shown(start: Long, time: Long): Boolean {
        val blinks = CursorBlinks()
        blinks.start(start)
        var woke = start
        var wait = blinks.pass(woke)
        while (woke + wait <= time) {
            woke += wait
            wait = blinks.pass(woke)
        }
        return blinks.on.value
    }

    @Test fun waitingAsLongAsAskedLandsOnTheNextBlinkAndMissesNone() {
        for (t in moments) {
            val wait = untilCursorBlink(t)
            assertTrue("$t", wait > 0)
            assertNotEquals("$t", cursorOnAt(t), cursorOnAt(t + wait))
            assertTrue("$t", (t until t + wait).all { cursorOnAt(it) == cursorOnAt(t) })
        }
    }

    @Test fun itTurnsOnAndOffAboutTwiceASecond() {
        var t = untilCursorBlink(0)
        repeat(20) {
            val lasts = untilCursorBlink(t)
            assertTrue("$t: $lasts ms", lasts in 400..700)
            t += lasts
        }
    }

    @Test fun cursorsThatAppearAtDifferentMomentsBlinkTogether() {
        // Each cursor's blink starts when it appears (a panel taking the focus, say); once two have, they agree.
        for ((first, second) in listOf(1_000L to 4_321L, 0L to 265L, 86_400_000L to 86_400_529L)) {
            for (time in second..second + 6_000) {
                assertEquals("$first, $second at $time", shown(first, time), shown(second, time))
                assertEquals("$first at $time: the clock's state", cursorOnAt(time), shown(first, time))
            }
        }
    }

    @Test fun aCursorThatAppearsWhileOthersBlinkShowsWhatTheyShowEvenJustAfterATurn() {
        for (first in listOf(100L, 600L, 86_400_123L)) {
            val blinks = CursorBlinks()
            blinks.start(first)  // LIBRARY's, say
            val turn = first + blinks.pass(first)
            val before = blinks.on.value
            // A panel taking the focus a millisecond after the next turn, before the loop has woken for it
            blinks.start(turn + 1)
            assertEquals("$first: still what the first shows", before, blinks.on.value)
            blinks.pass(turn + 4)
            assertNotEquals("$first: both turned as the loop wakes", before, blinks.on.value)
        }
    }

    @Test fun whenOneOfTwoCursorsStopsTheNextToAppearStillShowsWhatTheOtherShows() {
        for (first in listOf(100L, 600L, 86_400_123L)) {
            val blinks = CursorBlinks()
            blinks.start(first)  // LIBRARY's
            val turn = first + blinks.pass(first)
            blinks.start(first + 50)  // a panel's, which then loses the focus
            blinks.pass(first + 50)
            blinks.stop()
            val before = blinks.on.value
            // Another panel taking the focus a millisecond after the next turn, before LIBRARY's loop has woken for it
            blinks.start(turn + 1)
            assertEquals("$first: what LIBRARY's shows", before, blinks.on.value)
        }
    }

    @Test fun theFirstCursorAfterNoneShowsTheClocksStateWhateverWasLeft() {
        for (t in moments) {
            val blinks = CursorBlinks()
            blinks.start(t)
            blinks.stop()
            // No cursor blinks from t to the next turn, where one appears: its state, not the one left at t
            val next = t + untilCursorBlink(t)
            blinks.start(next)
            assertEquals("$t", cursorOnAt(next), blinks.on.value)
            assertNotEquals("$t", cursorOnAt(t), blinks.on.value)
        }
    }
}
