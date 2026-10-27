package com.phobos.emulator.ui

import com.phobos.emulator.data.GameDisc
import com.phobos.emulator.data.GameFiles
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.data.GameStore
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.util.concurrent.Executor

class LibraryRowsTest {
    @get:Rule
    val folder = TemporaryFolder()

    private val visible = listOf("Game Boy", "Super Famicom", "PlayStation")

    private fun played(
        system: String, name: String, lastPlayed: Long, hidden: Boolean = false, missing: Boolean = false,
    ) = GameRecord(
        GameKey(system, name), uri = "content://$name", plays = 1, lastPlayed = lastPlayed,
        hiddenFromHistory = hidden, missing = missing,
    )

    private fun names(games: List<GameRecord>) = games.map { it.key.name }

    @Test
    fun continuePlayingHasTheLatest24GamesPlayedNewestFirst() {
        val records = (1..30).map { played("Game Boy", "Game $it.gb", lastPlayed = it * 1_000L) } + listOf(
            played("Super Famicom", "Hidden.sfc", lastPlayed = 99_000, hidden = true),
            played("Nintendo 64", "On a hidden system.z64", lastPlayed = 98_000),
            GameRecord(GameKey("Game Boy", "Favourite never played.gb"), favourite = true),
        )
        val rows = libraryRowsOf(records, visible)
        assertEquals((30 downTo 7).map { "Game $it.gb" }, names(rows.continuePlaying))
    }

    @Test
    fun favoritesGoByRomNameAsASystemsListSortsItsGames() {
        val records = listOf(
            GameRecord(GameKey("Super Famicom", "b.sfc"), favourite = true),
            GameRecord(GameKey("Game Boy", "a.gb"), favourite = true, lastPlayed = 5),
            GameRecord(GameKey("PlayStation", "A.cue"), favourite = true),
            GameRecord(GameKey("Super Famicom", "a.gb"), favourite = true),
            GameRecord(GameKey("Game Boy", "Not a favourite.gb"), lastPlayed = 9),
            GameRecord(GameKey("Nintendo 64", "On a hidden system.z64"), favourite = true),
        )
        val favorites = libraryRowsOf(records, visible).favorites
        // A system's list sorts by name with String's own order, capitals first; the same name goes by system.
        assertEquals(
            listOf(GameKey("PlayStation", "A.cue"), GameKey("Game Boy", "a.gb"), GameKey("Super Famicom", "a.gb"),
                GameKey("Super Famicom", "b.sfc")),
            favorites.map { it.key },
        )
    }

    @Test
    fun aGameWhoseFileWasntFoundKeepsItsPlaceInBothRows() {
        val records = listOf(
            played("Game Boy", "c.gb", lastPlayed = 3_000).copy(favourite = true),
            played("Game Boy", "b.gb", lastPlayed = 2_000, missing = true).copy(favourite = true),
            played("Game Boy", "a.gb", lastPlayed = 1_000).copy(favourite = true),
        )
        val rows = libraryRowsOf(records, visible)
        assertEquals(listOf("c.gb", "b.gb", "a.gb"), names(rows.continuePlaying))
        assertEquals(listOf("a.gb", "b.gb", "c.gb"), names(rows.favorites))
    }

    @Test
    fun onlyAMultiDiscGameWhoseDiscsItsRecordLacksNeedsItsSystemsPage() {
        val psx = listOf("cue", "chd", "pbp")
        fun record(name: String, discs: List<GameDisc> = emptyList()) =
            GameRecord(GameKey("PlayStation", name), uri = "content://disc1", discs = discs)
        assertTrue(needsSystemPage(record("Final Fantasy VII (USA).m3u"), psx))
        assertTrue(needsSystemPage(record("Final Fantasy VII (USA)"), psx))
        assertFalse(needsSystemPage(record("Final Fantasy VII (USA)", listOf(GameDisc("d1.cue", "content://d1"))), psx))
        assertFalse(needsSystemPage(record("Crash Bandicoot (USA).cue"), psx))
        assertFalse(needsSystemPage(record("CRASH BANDICOOT (USA).CHD"), psx))
        assertFalse(needsSystemPage(record("Crash Bandicoot (USA).zip"), psx))
    }

    @Test
    fun aMultiDiscGameFromTheFirstReleaseTakesItsSystemsPageOnce() {
        val file = folder.root.resolve("games.json")
        // As the first release (#317) recorded a disc set: the set's title, disc 1's URI, no discs.
        file.writeText(
            """{"version":1,"games":[{"system":"PlayStation","name":"Final Fantasy VII (USA)",""" +
                """"uri":"content://tree/psx/document/disc1","plays":1,"lastPlayed":5}]}""",
        )
        val store = GameStore(file, Executor { it.run() })
        val psx = listOf("cue", "chd", "pbp")
        store.load()
        val before = store.games.value!!.single()
        assertTrue(needsSystemPage(before, psx))
        // Its system's page scans the folders and finds the set's discs.
        val setFolder = "content://tree/psx/document/ff7"
        val discs = listOf(
            GameDisc("Final Fantasy VII (USA) (Disc 1).cue", "content://tree/psx/document/disc1", setFolder),
            GameDisc("Final Fantasy VII (USA) (Disc 2).cue", "content://tree/psx/document/disc2", setFolder),
        )
        store.refreshFiles(
            "PlayStation",
            listOf("Final Fantasy VII (USA)" to GameFiles(discs[0].uri, discs[0].parentUri, discs)),
        )
        val after = store.games.value!!.single()
        assertEquals(discs, after.discs)
        assertFalse(needsSystemPage(after, psx))
    }

    @Test
    fun theMenuOffersHideOnlyWhileTheGameIsInContinuePlaying() {
        val key = GameKey("Game Boy", "Tetris (World).gb")
        assertEquals(listOf(GameMenuItem.ADD_TO_FAVORITES), gameMenuItems(null))
        assertEquals(listOf(GameMenuItem.REMOVE_FROM_FAVORITES), gameMenuItems(GameRecord(key, favourite = true)))
        assertEquals(
            listOf(GameMenuItem.ADD_TO_FAVORITES, GameMenuItem.HIDE_FROM_CONTINUE_PLAYING),
            gameMenuItems(GameRecord(key, plays = 1, lastPlayed = 5)),
        )
        assertEquals(
            listOf(GameMenuItem.ADD_TO_FAVORITES),
            gameMenuItems(GameRecord(key, plays = 1, lastPlayed = 5, hiddenFromHistory = true)),
        )
    }

    @Test
    fun aQuickPressIsAClick() {
        val hold = KeyHold()
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.release())
    }

    @Test
    fun aHoldOpensTheMenuOnceAndItsReleaseIsNoClick() {
        val hold = KeyHold()
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.timeout())
        assertFalse(hold.timeout())
        assertFalse(hold.release())
    }

    @Test
    fun repeatsWhileHeldChangeNothing() {
        val hold = KeyHold()
        assertTrue(hold.press(repeatCount = 0))
        assertFalse(hold.press(repeatCount = 1))
        assertFalse(hold.press(repeatCount = 2))
        assertTrue(hold.timeout())
        assertFalse(hold.press(repeatCount = 3))
        assertFalse(hold.release())
        // The next press is a press of its own.
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.release())
    }

    @Test
    fun aTimerThatRunsOutAfterTheReleaseDoesNothing() {
        val hold = KeyHold()
        hold.press(repeatCount = 0)
        assertTrue(hold.release())
        assertFalse(hold.timeout())
    }

    @Test
    fun aReleaseWithoutItsPressOrAfterACancelIsNoClick() {
        val hold = KeyHold()
        // A's down went to something else (another card, before the focus moved here).
        assertFalse(hold.press(repeatCount = 4))
        assertFalse(hold.release())
        hold.press(repeatCount = 0)
        hold.cancel()
        assertFalse(hold.timeout())
        assertFalse(hold.release())
    }

    @Test
    fun aFreshPressAfterAnUpThatWentElsewhereIsAPressOfItsOwn() {
        val hold = KeyHold()
        // A hold opened the menu, whose window got A's up.
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.timeout())
        // Back on the card, A's next press is timed afresh: let go of soon, it's a click.
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.release())
        // So after a quick press whose up went elsewhere: the next press, held, is a hold.
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.press(repeatCount = 0))
        assertTrue(hold.timeout())
        assertFalse(hold.release())
    }

    @Test
    fun theMenuDropsTheHeldAUntilItsPressedAgain() {
        val guard = HeldKeyGuard(armed = true)
        assertTrue(guard.drops(down = true, repeatCount = 7))
        assertTrue(guard.drops(down = true, repeatCount = 8))
        assertTrue(guard.drops(down = false, repeatCount = 0))
        assertFalse(guard.drops(down = true, repeatCount = 0))
        assertFalse(guard.drops(down = false, repeatCount = 0))
    }

    @Test
    fun aFreshPressInTheMenuIsntDropped() {
        // The held A's release went to the Library (Android cancels a window's held keys when another takes the focus).
        val guard = HeldKeyGuard(armed = true)
        assertFalse(guard.drops(down = true, repeatCount = 0))
        assertFalse(guard.drops(down = true, repeatCount = 1))
        assertFalse(guard.drops(down = false, repeatCount = 0))
    }

    @Test
    fun aMenuOpenedByTouchDropsNothing() {
        val guard = HeldKeyGuard(armed = false)
        assertFalse(guard.drops(down = true, repeatCount = 3))
        assertFalse(guard.drops(down = false, repeatCount = 0))
    }

    @Test
    fun aLeavingCardHandsTheFocusToTheCardInItsPlace() {
        val (a, b, c) = listOf("a.gb", "b.gb", "c.gb").map { GameKey("Game Boy", it) }
        val card = LeavingCard()
        assertNull(card.shown(listOf(a, b, c)))
        card.leaving(b)
        assertEquals(c, card.shown(listOf(a, c)))
        // At the row's end, the card before it; the row's last card leaves the focus nowhere.
        card.leaving(c)
        assertEquals(a, card.shown(listOf(a)))
        card.leaving(a)
        assertNull(card.shown(emptyList()))
    }

    @Test
    fun aCardThatDidntLeaveIsForgotten() {
        val (a, b, c, d) = listOf("a.gb", "b.gb", "c.gb", "d.gb").map { GameKey("Game Boy", it) }
        val card = LeavingCard()
        card.shown(listOf(a, b, c))
        card.leaving(b)
        // Its change didn't take: the row, changed otherwise, still shows it, and it keeps the focus.
        assertNull(card.shown(listOf(a, b, c, d)))
        // Leaving later, for another reason, it moves the focus nowhere.
        assertNull(card.shown(listOf(a, c, d)))
    }
}
