package com.phobos.emulator.input

import android.view.KeyEvent

/**
 * The activity window's keys, which it gets before any view does (MainActivity's window callback). While a game is on
 * screen every key goes to its [GameKeys], whichever view has focus, so a combo's first key reaches the hotkeys
 * instead of being taken on the way. Away from the game screen, keys are watched for the hotkeys that run there: Show
 * screensaver's, and with the game swapped away behind the Library, the Library hotkey that brings it back. Only their
 * keys are taken, the one completing one, the one starting a binding of one, and a tap binding's key (until its release
 * says whether it was a tap); every other key goes on to the pages. A key starting a binding that's let go of with
 * nothing pressed meanwhile ([pressedBeside] included) goes to the pages after all, its press and release together as
 * it's let go of ([takeReplay]): Back still goes back, unless that would leave the app with a game behind the pages,
 * or the game screen is up by then. The repeats and release of a key whose press went to a game, or was taken by
 * those hotkeys, go the same way, whatever changed meanwhile: a game that reloads or is swapped away with keys down
 * still gets them let go of, so its hotkeys don't count them as held when it's back. So do those of a key pressed in a
 * PSP dialog over the game, a window of its own whose keys never pass this one, while the game's hotkeys count it as
 * held: a Reload from the dialog closes the game, and the dialog with it, before they come. Used from the main thread.
 */
class WindowKeys(private val app: App) {
    /** What the window's keys ask the app, at each key. */
    interface App {
        /** The keys of the game on screen once it's loaded; null otherwise. */
        fun gameKeys(): KeyHandler?
        /**
         * The game screen's keys, loaded or not (null with no game screen), if their hotkeys count [keyCode] as held:
         * its press came in a PSP dialog over the game ([GameKeys.onDialogKey]), which this window doesn't see.
         */
        fun gameKeysHolding(keyCode: Int): KeyHandler?
        /** A game is loaded and paused behind the Library or Settings. */
        fun swappedAway(): Boolean
        /** The game screen is up, its game loaded or still loading. */
        fun gameScreenShown(): Boolean
        fun hotkeys(): Map<String, List<List<Int>>>
        /** The hat and axis directions held (GameInputState.hotkeyKeys). */
        fun heldAxes(): Set<Int>
        fun swapBackToGame()
        fun showScreensaver()
        /** Whether the pages would take a Back (a page to go back to, a BackHandler); one they don't leaves the app. */
        fun pagesTakeBack(): Boolean
    }

    private val tracker = HotkeyTracker()
    /** The keys held whose press went to a game's keys, and to which. */
    private val toGame = mutableMapOf<Int, KeyHandler>()
    /** The keys held that the hotkeys watched here took. */
    private val taken = mutableSetOf<Int>()
    /** Those of them taken as a binding's first key or a tap's, nothing run as they were pressed. */
    private val waiting = mutableSetOf<Int>()
    /** The taken key the last release gave back to the pages ([takeReplay]). */
    private var replay: Int? = null

    /** Forgets the keys held: a controller went away, so their releases won't come. */
    fun reset() {
        tracker.reset()
        toGame.clear()
        taken.clear()
        waiting.clear()
        replay = null
    }

    /**
     * Whether the hotkeys watched here took [keyCode]'s press, not let go of yet: its repeats and release are theirs.
     */
    fun holds(keyCode: Int): Boolean = keyCode in taken

    /**
     * The key whose release the last [onKey] took to give back: one starting a binding, pressed and let go of with
     * nothing pressed meanwhile, which goes to the pages after all, its press and release together. Null otherwise.
     */
    fun takeReplay(): Int? = replay.also { replay = null }

    /**
     * A key went down that the hotkeys here don't see (a D-pad direction Android makes from a hat, a key's fallback,
     * the navigation bar's Back, the D-pad press Android keeps to leave touch mode): a key they took and hold is no
     * longer pressed alone, so it's no tap, nor given back.
     */
    fun pressedBeside() = tracker.pressedBeside()

    /** A key event ([down]: a key press, repeats included); true when it's taken. */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean {
        replay = null
        if (down && repeatCount == 0) {
            // A new press decides afresh where the key goes: an older press's release may have gone to a dialog.
            toGame.remove(keyCode)
            taken.remove(keyCode)
            waiting.remove(keyCode)
        } else {
            toGame[keyCode]?.let { game ->
                if (!down) toGame.remove(keyCode)
                return game.onKey(keyCode, down, repeatCount)
            }
            if (keyCode in taken) return down || letGoOfTaken(keyCode)
            app.gameKeysHolding(keyCode)?.let { game -> return game.onKey(keyCode, down, repeatCount) }
        }
        app.gameKeys()?.let { game ->
            tracker.reset()
            if (down) toGame[keyCode] = game
            return game.onKey(keyCode, down, repeatCount)
        }
        val hotkeys = app.hotkeys()
        val watched = watched(hotkeys)
        if (watched.isEmpty()) {
            tracker.reset()
            return false
        }
        if (repeatCount > 0) return false
        if (!down) {
            tracker.release(keyCode)
            return false
        }
        tracker.syncExternal(hotkeys, app.heldAxes())
        tracker.press(hotkeys, keyCode)
        val bindings = watched.flatMap { hotkeys[it].orEmpty() }
        val tapKey = bindings.any { it == listOf(keyCode) && isTapCombo(hotkeys, it) }
        val startsOne = bindings.any { it.size > 1 && it.first() == keyCode }
        val completed = tracker.completions(hotkeys).filter { it in watched }
        when {
            completed.isNotEmpty() -> completed.forEach(::run)
            // A tap's key is the hotkeys', not the pages', and so is a key starting a binding: there, Back would go
            // back as it's let go of, and Mode go Home (the fallback Android's Generic.kcm gives an unhandled Mode),
            // the hotkey half done. Let go of alone, it goes to them after all (letGoOfTaken).
            tapKey || startsOne -> waiting += keyCode
            else -> return false
        }
        taken += keyCode
        return true
    }

    private fun letGoOfTaken(keyCode: Int): Boolean {
        taken -= keyCode
        val first = waiting.remove(keyCode)
        tracker.release(keyCode)
        val alone = tracker.letGoAlone(keyCode)
        val hotkeys = app.hotkeys()
        val taps = tracker.taps(hotkeys).filter { it in watched(hotkeys) }
        if (taps.isNotEmpty()) taps.forEach(::run) else if (first && alone && givesBack(keyCode)) replay = keyCode
        return true
    }

    /**
     * Whether a binding's first key let go of alone goes to the pages after all: not once the game screen is up, where
     * Back would ask to quit the game still loading, nor a Back no page would take with a game behind them, which would
     * leave the app and close that game.
     */
    private fun givesBack(keyCode: Int) =
        !app.gameScreenShown() && !(keyCode == KeyEvent.KEYCODE_BACK && app.swappedAway() && !app.pagesTakeBack())

    /**
     * The hotkeys run here, those bound: Show screensaver with no game screen up, and the Library's with the game
     * behind the pages.
     */
    private fun watched(hotkeys: Map<String, List<List<Int>>>): Set<String> = buildSet {
        if (!app.gameScreenShown()) add(HotkeyAction.SHOW_SCREENSAVER)
        if (app.swappedAway()) add(HotkeyAction.LIBRARY)
    }.filterTo(mutableSetOf()) { action -> hotkeys[action].orEmpty().any { it.isNotEmpty() } }

    private fun run(action: String) {
        when (action) {
            HotkeyAction.LIBRARY -> {
                tracker.reset()
                app.swapBackToGame()
            }
            HotkeyAction.SHOW_SCREENSAVER -> app.showScreensaver()
        }
    }
}
