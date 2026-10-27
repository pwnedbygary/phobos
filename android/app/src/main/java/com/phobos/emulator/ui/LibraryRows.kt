package com.phobos.emulator.ui

import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord

/** How many games Continue Playing shows, as many as Cartridge's does. */
const val CONTINUE_PLAYING_SIZE = 24

/** The Library's two rows of games. */
data class LibraryRows(val continuePlaying: List<GameRecord>, val favorites: List<GameRecord>)

/**
 * The rows for [records], leaving out games of systems not in [visibleSystems] (the hidden ones). Continue Playing has
 * the games played, newest first, but not those hidden from it, as Cartridge's has. Favorites go by ROM name, as a
 * system's list sorts its games. A game whose file wasn't found keeps its place in both.
 */
fun libraryRowsOf(records: List<GameRecord>, visibleSystems: Collection<String>): LibraryRows {
    val visible = visibleSystems.toSet()
    val shown = records.filter { it.key.system in visible }
    return LibraryRows(
        continuePlaying = shown
            .filter { it.lastPlayed > 0 && !it.hiddenFromHistory }
            .sortedWith(
                compareByDescending<GameRecord> { it.lastPlayed }.thenBy { it.key.system }.thenBy { it.key.name },
            )
            .take(CONTINUE_PLAYING_SIZE),
        favorites = shown.filter { it.favourite }.sortedWith(compareBy({ it.key.name }, { it.key.system })),
    )
}

/**
 * Whether [record] has to be started from its system's page: a multi-disc game whose discs it doesn't know (recorded
 * before records kept them). That page's scan records them, so this happens once. Its name gives it away: a playlist's
 * ends in .m3u, and a disc set's is a title with none of the system's [extensions] (as a system's scan lists files:
 * those extensions and .zip).
 */
fun needsSystemPage(record: GameRecord, extensions: Collection<String>): Boolean {
    if (record.discs.isNotEmpty()) return false
    val extension = record.key.name.substringAfterLast('.', "").lowercase()
    return extension == "m3u" || (extension != "zip" && extension !in extensions)
}

/** What a game's menu offers. */
enum class GameMenuItem { ADD_TO_FAVORITES, REMOVE_FROM_FAVORITES, HIDE_FROM_CONTINUE_PLAYING }

/**
 * The menu for the game [record] is about (null when it has none yet): Favorite or not, and Hide from Continue Playing
 * while the game is in it (played, and not hidden), as Cartridge offers its own.
 */
fun gameMenuItems(record: GameRecord?): List<GameMenuItem> = buildList {
    add(if (record?.favourite == true) GameMenuItem.REMOVE_FROM_FAVORITES else GameMenuItem.ADD_TO_FAVORITES)
    if (record != null && record.lastPlayed > 0 && !record.hiddenFromHistory) {
        add(GameMenuItem.HIDE_FROM_CONTINUE_PLAYING)
    }
}

/**
 * Tells a press of a game's A from a hold. Held for the long-press timeout it opens the game's menu, and let go of
 * sooner it starts the game. Only the down and up events count, the timeout timed from the down, so a hold works
 * whether or not the device repeats a held key; the repeats do nothing.
 */
class KeyHold {
    private var down = false
    private var held = false

    /**
     * The key went down: true when that starts a press to time, as any down but a repeat does. One while a press is
     * still counted means that press's up went elsewhere (to the menu's window, say), so that press is forgotten.
     */
    fun press(repeatCount: Int): Boolean {
        if (repeatCount > 0) return false
        down = true
        held = false
        return true
    }

    /** The timeout ran out since [press]: true when the key is still down, which makes the press a hold. */
    fun timeout(): Boolean {
        if (!down || held) return false
        held = true
        return true
    }

    /** The key came up: true when that ends a press shorter than a hold, a click. */
    fun release(): Boolean {
        val click = down && !held
        down = false
        held = false
        return click
    }

    /** Forgets the press: the focus moved away, or Android cancelled the key. */
    fun cancel() {
        down = false
        held = false
    }
}

/**
 * Keeps the A that opened a game's menu by being held from also picking an item in it. While that A is held, its
 * repeats and its release reach the menu, as presses of what has the focus there. [armed] when a held A opened the
 * menu: then each of that key's events is dropped until it goes down afresh.
 */
class HeldKeyGuard(private var armed: Boolean) {
    /** Whether the menu drops this event of the guarding key: [down] or up, with [repeatCount] for a down. */
    fun drops(down: Boolean, repeatCount: Int): Boolean {
        if (!armed) return false
        if (down && repeatCount == 0) {
            armed = false
            return false
        }
        if (!down) armed = false
        return true
    }
}

/**
 * Where the focus goes in one of the Library's rows when the focused card leaves it (no longer a favorite, or hidden
 * from Continue Playing): to the card that takes its place, or the one before it at the row's end.
 */
class LeavingCard {
    private var leaving: GameKey? = null
    private var shown: List<GameKey> = emptyList()

    /** [key]'s card has the focus and is about to leave the row. */
    fun leaving(key: GameKey) {
        leaving = key
    }

    /**
     * The row now shows [now]: the card to take the focus if the leaving card has gone (none when the row is empty).
     * The leaving card is forgotten either way, so one still there (its change didn't take) keeps the focus, and its
     * leaving later, for another reason, moves nothing.
     */
    fun shown(now: List<GameKey>): GameKey? {
        val key = leaving
        val before = shown
        leaving = null
        shown = now
        if (key == null || key in now) return null
        val index = before.indexOf(key)
        return if (index < 0 || now.isEmpty()) null else now[minOf(index, now.lastIndex)]
    }
}
