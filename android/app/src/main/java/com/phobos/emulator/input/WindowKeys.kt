package com.phobos.emulator.input

/**
 * The activity window's keys, which it gets before any view does (MainActivity's window callback). While a game is on
 * screen every key goes to its [GameKeys], whichever view has focus, so a combo's first key reaches the hotkeys
 * instead of being taken on the way. With the game swapped away behind the Library, keys are watched for the Library
 * hotkey that brings it back: only its keys are taken, the one completing it, the one starting a Library binding, and
 * a Library tap's key (until its release says whether it was a tap); every other key goes on to the Library's
 * screens. The repeats and release of a key whose
 * press went to a game, or was taken by the Library hotkey, go the same way, whatever changed meanwhile: a game that
 * reloads or is swapped away with keys down still gets them let go of, so its hotkeys don't count them as held when
 * it's back. Used from the main thread.
 */
class WindowKeys(private val app: App) {
    /** What the window's keys ask the app, at each key. */
    interface App {
        /** The keys of the game on screen once it's loaded; null otherwise. */
        fun gameKeys(): KeyHandler?
        /** A game is loaded and paused behind the Library or Settings. */
        fun swappedAway(): Boolean
        fun hotkeys(): Map<String, List<List<Int>>>
        /** The hat and axis directions held (GameInputState.hotkeyKeys). */
        fun heldAxes(): Set<Int>
        fun swapBackToGame()
    }

    private val library = HotkeyTracker()
    /** The keys held whose press went to a game's keys, and to which. */
    private val toGame = mutableMapOf<Int, KeyHandler>()
    /** The keys held that the Library hotkey took from behind the Library. */
    private val taken = mutableSetOf<Int>()

    /** Forgets the keys held: a controller went away, so their releases won't come. */
    fun reset() {
        library.reset()
        toGame.clear()
        taken.clear()
    }

    /** A key event ([down]: a key press, repeats included); true when it's taken. */
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean {
        if (down && repeatCount == 0) {
            // A new press decides afresh where the key goes: an older press's release may have gone to a dialog.
            toGame.remove(keyCode)
            taken.remove(keyCode)
        } else {
            toGame[keyCode]?.let { game ->
                if (!down) toGame.remove(keyCode)
                return game.onKey(keyCode, down, repeatCount)
            }
            if (keyCode in taken) return down || letGoOfTaken(keyCode)
        }
        app.gameKeys()?.let { game ->
            library.reset()
            if (down) toGame[keyCode] = game
            return game.onKey(keyCode, down, repeatCount)
        }
        if (!app.swappedAway()) {
            library.reset()
            return false
        }
        if (repeatCount > 0) return false
        if (!down) {
            library.release(keyCode)
            return false
        }
        val hotkeys = app.hotkeys()
        library.syncExternal(hotkeys, app.heldAxes())
        library.press(hotkeys, keyCode)
        val libraryTap = hotkeys[HotkeyAction.LIBRARY].orEmpty().any { it == listOf(keyCode) && isTapCombo(hotkeys, it) }
        val startsLibrary = hotkeys[HotkeyAction.LIBRARY].orEmpty().any { it.size > 1 && it.first() == keyCode }
        when {
            HotkeyAction.LIBRARY in library.completions(hotkeys) -> backToGame()
            // A Library tap's key is the hotkeys', not the Library screens', and so is a key starting a Library
            // binding: there, Back would go back as it's let go of, and Mode go Home (the fallback Android's
            // Generic.kcm gives an unhandled Mode), the way back to the game half done.
            libraryTap || startsLibrary -> {}
            else -> return false
        }
        taken += keyCode
        return true
    }

    private fun letGoOfTaken(keyCode: Int): Boolean {
        taken -= keyCode
        library.release(keyCode)
        if (HotkeyAction.LIBRARY in library.taps(app.hotkeys())) backToGame()
        return true
    }

    private fun backToGame() {
        library.reset()
        app.swapBackToGame()
    }
}
