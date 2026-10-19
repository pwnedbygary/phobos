package com.phobos.emulator.input

import android.view.KeyEvent
import android.view.MotionEvent

/** Hotkey action identifiers, as persisted in settings (`hotkey_combo_<action>`). */
object HotkeyAction {
    const val PAUSE = "pause"
    const val FAST_FORWARD_HOLD = "ff_hold"
    const val FAST_FORWARD_TOGGLE = "ff_toggle"
    const val SAVE_STATE = "save"
    const val LOAD_STATE = "load"
    const val NEXT_SLOT = "inc_slot"
    const val PREVIOUS_SLOT = "dec_slot"
    const val RESET = "reset"
    const val RELOAD = "reload"
    const val QUIT = "quit"
    const val SCREENSHOT = "screenshot"
    const val MUTE = "mute"
    const val FRAME_ADVANCE = "frame_advance"
    const val N64_PAK = "n64_pak"
    const val PS1_ANALOG_TOGGLE = "analog_toggle"
    const val KEYBOARD = "keyboard"
    const val LIBRARY = "library"

    const val SAVE_SLOT_PREFIX = "save_slot_"
    const val LOAD_SLOT_PREFIX = "load_slot_"

    /** Direct save/load slot actions, 1 through 9. */
    val DIRECT_SLOTS: List<String> = (1..9).flatMap { listOf("$SAVE_SLOT_PREFIX$it", "$LOAD_SLOT_PREFIX$it") }

    /** Actions that last while their combo is held, rather than running once. */
    val HOLDS: Set<String> = setOf(FAST_FORWARD_HOLD, FRAME_ADVANCE)
}

/**
 * The hotkeys a player who never bound one has. Mode (the Home or Guide button) leads every one, so Select and every
 * other button stay the game's: tapped alone, Mode pauses; held with one other button, it runs that button's action.
 * The actions left out (Reset, Quit, fast-forward hold, the direct slots) are unbound: the pause menu has the
 * dangerous ones, and a player can bind them on purpose.
 */
val DEFAULT_HOTKEYS: Map<String, List<Int>> = KeyEvent.KEYCODE_BUTTON_MODE.let { mode ->
    mapOf(
        HotkeyAction.PAUSE to listOf(mode),
        HotkeyAction.SAVE_STATE to listOf(mode, KeyEvent.KEYCODE_BUTTON_R1),
        HotkeyAction.LOAD_STATE to listOf(mode, KeyEvent.KEYCODE_BUTTON_L1),
        HotkeyAction.NEXT_SLOT to listOf(mode, KeyEvent.KEYCODE_DPAD_RIGHT),
        HotkeyAction.PREVIOUS_SLOT to listOf(mode, KeyEvent.KEYCODE_DPAD_LEFT),
        HotkeyAction.RELOAD to listOf(mode, KeyEvent.KEYCODE_BUTTON_X),
        HotkeyAction.SCREENSHOT to listOf(mode, KeyEvent.KEYCODE_BUTTON_THUMBR),
        HotkeyAction.MUTE to listOf(mode, KeyEvent.KEYCODE_BUTTON_A),
        HotkeyAction.FAST_FORWARD_TOGGLE to listOf(mode, KeyEvent.KEYCODE_BUTTON_R2),
        HotkeyAction.FRAME_ADVANCE to listOf(mode, KeyEvent.KEYCODE_BUTTON_THUMBL),
        HotkeyAction.N64_PAK to listOf(mode, KeyEvent.KEYCODE_DPAD_DOWN),
        HotkeyAction.PS1_ANALOG_TOGGLE to listOf(mode, KeyEvent.KEYCODE_DPAD_UP),
        HotkeyAction.KEYBOARD to listOf(mode, KeyEvent.KEYCODE_BUTTON_L2),
        HotkeyAction.LIBRARY to listOf(mode, KeyEvent.KEYCODE_BUTTON_Y),
    )
}

/**
 * Hotkey tokens are the positive Android keycodes plus negative tokens for axes that are not
 * delivered as keys (an analog stick direction). Keeping them in the same [Int] stream means the
 * persisted format stays comma-separated and every existing binding keeps working.
 */
object HotkeyTokens {
    /** A stick axis direction: [axis] is a MotionEvent.AXIS_* constant. */
    fun axis(axis: Int, positive: Boolean): Int = -(axis * 2 + if (positive) 1 else 2)

    fun isAxis(token: Int): Boolean = token < 0

    /** The axis and direction a token names, or null for a keycode. */
    fun axisOf(token: Int): Pair<Int, Boolean>? {
        if (!isAxis(token)) return null
        val value = -token
        return ((value - 1) / 2) to ((value - 1) % 2 == 0)
    }
}

/** Something that takes key events, as the window hands them on: true when it takes one. */
fun interface KeyHandler {
    fun onKey(keyCode: Int, down: Boolean, repeatCount: Int): Boolean
}

data class HotkeyConflict(val action: String, val kind: Kind) {
    enum class Kind { EXACT, OVERLAP, CROSS_ROLE }
}

/**
 * A one-key combo whose key leads longer combos: a tap binding. It fires when its key is let go with nothing else
 * pressed meanwhile, so it doesn't fire as the longer combos are pressed.
 */
fun isTapCombo(hotkeys: Map<String, List<Int>>, combo: List<Int>): Boolean =
    combo.size == 1 && hotkeys.values.any { it.size > 1 && it.first() == combo[0] }

/**
 * Tracks keys and axes in press order for exact-set hotkey combos.
 *
 * The first key of a combo is its modifier: it is held back from the game, and the other keys of
 * the combo are held back while it is down. Action keys still reach the game when pressed alone,
 * and a combo only fires when its modifier was pressed before its other keys. This removes the
 * old prefix leak (Z reaching the game while Z + R1 was being pressed) without delaying ordinary
 * game buttons. A tap binding ([isTapCombo]) fires on its key's release instead, reported by [taps].
 */
class HotkeyTracker {
    private val order = mutableListOf<Int>()
    private val consumed = mutableSetOf<Int>()
    /** Tokens held by a key event. */
    private val keyTokens = mutableSetOf<Int>()
    /** Tokens held outside key events (a hat D-pad or an axis). */
    private val externals = mutableSetOf<Int>()
    /** The key pressed with nothing else held, while nothing else has been pressed since. */
    private var tapCandidate: Int? = null
    /** The key the last release let go of as a tap, until [taps] reports it. */
    private var tapped: Int? = null
    /** A token joined the keys held since [completions] last looked. */
    private var pressedSinceLook = false

    /** Tokens whose key event must not reach the game. */
    val consumedTokens: Set<Int> get() = consumed

    fun reset() {
        order.clear()
        consumed.clear()
        keyTokens.clear()
        externals.clear()
        tapCandidate = null
        tapped = null
        pressedSinceLook = false
    }

    /**
     * A key event went down. Returns true when the game must not see it. A repeat changes nothing, and neither does a
     * key the hat or an axis already holds: a button a pad reports both ways is pressed once.
     */
    fun press(hotkeys: Map<String, List<Int>>, token: Int): Boolean {
        if (!keyTokens.add(token) || token in order) return token in consumed
        tapped = null
        tapCandidate = if (order.isEmpty()) token else null
        order += token
        val take = consumedOnDown(hotkeys, token)
        if (take) consumed += token else consumed -= token
        pressedSinceLook = true
        return take
    }

    /** A key event went up. Returns true when its down was taken from the game. */
    fun release(token: Int): Boolean {
        keyTokens -= token
        val wasConsumed = consumed.contains(token)
        // A hat or an axis may still hold the same key: the token stays until both let go.
        if (token !in externals) letGo(token)
        return wasConsumed
    }

    private fun letGo(token: Int) {
        order.remove(token)
        consumed.remove(token)
        if (token == tapCandidate) {
            tapped = token
            tapCandidate = null
        }
    }

    /**
     * Adds/removes tokens pressed outside key events (a hat D-pad or an axis). A stick direction no
     * combo uses is left out, so moving a stick doesn't keep combos from matching. Returns true when
     * a token was pressed: one already down as a key event isn't pressed again.
     */
    fun syncExternal(hotkeys: Map<String, List<Int>>, held: Set<Int>): Boolean {
        val relevant = held.filterTo(mutableSetOf()) { token ->
            !HotkeyTokens.isAxis(token) || hotkeys.values.any { token in it }
        }
        // The same key may still be down as a key event: it stays until that one is let go.
        (externals - relevant).forEach { if (it !in keyTokens) letGo(it) }
        val pressed = (relevant - externals).filter { it !in order }
        externals.clear()
        externals.addAll(relevant)
        if (pressed.isEmpty()) return false
        tapCandidate = if (order.isEmpty() && pressed.size == 1) pressed.first() else null
        // A modifier must lead: when a hat reports two directions together, add the ones that are
        // some combo's first key before the rest.
        val modifiers = hotkeys.values.mapNotNull { it.firstOrNull() }.toSet()
        pressed.sortedBy { if (it in modifiers) 0 else 1 }.forEach { token ->
            order += token
            if (consumedOnDown(hotkeys, token)) consumed += token else consumed -= token
        }
        pressedSinceLook = true
        return true
    }

    /**
     * The combos the presses since the last look completed ([matches]), for running them: a release completes none
     * (only a tap, [taps]), and a combo still held isn't completed again as other keys come and go.
     */
    fun completions(hotkeys: Map<String, List<Int>>): List<String> {
        if (!pressedSinceLook) return emptyList()
        pressedSinceLook = false
        return matches(hotkeys)
    }

    /** The actions whose combo is exactly the set held, with their modifier pressed first; never a tap binding. */
    fun matches(hotkeys: Map<String, List<Int>>): List<String> {
        val pressed = order.toSet()
        return hotkeys.mapNotNull { (action, combo) ->
            if (combo.isEmpty() || combo.size != pressed.size || !pressed.containsAll(combo)) return@mapNotNull null
            if (isTapCombo(hotkeys, combo)) return@mapNotNull null
            val positions = combo.map(order::indexOf)
            if (positions.first() == positions.min()) action else null
        }
    }

    /** The tap bindings the last release completed; each tap is reported once. */
    fun taps(hotkeys: Map<String, List<Int>>): List<String> {
        val key = tapped ?: return emptyList()
        tapped = null
        return hotkeys.mapNotNull { (action, combo) -> action.takeIf { combo == listOf(key) && isTapCombo(hotkeys, combo) } }
    }

    /** Whether every key of [combo] is held (others may be too): how long a hold action lasts. */
    fun holding(combo: List<Int>): Boolean = combo.isNotEmpty() && order.containsAll(combo)

    private fun consumedOnDown(hotkeys: Map<String, List<Int>>, token: Int): Boolean {
        // The first key of a combo is always its modifier, never the game's.
        if (hotkeys.values.any { it.firstOrNull() == token }) return true
        val at = order.indexOf(token)
        if (at < 0) return false
        return hotkeys.values.any { combo ->
            combo.size > 1 && token in combo.drop(1) &&
                combo.first() in order && order.indexOf(combo.first()) < at
        }
    }
}

/**
 * What binding [action] to [combo] would overwrite at a level: the same set, a combo that is part
 * of it or contains it, or a key that is one combo's modifier and another's action. Two combos
 * that share only their modifier are fine, and so are a tap binding and the longer combos its key
 * leads ([isTapCombo]), unless the tap is a hold action, which can't wait for its key's release.
 */
fun hotkeyConflicts(hotkeys: Map<String, List<Int>>, action: String, combo: List<Int>): List<HotkeyConflict> {
    if (combo.isEmpty()) return emptyList()
    val candidate = combo.toSet()
    return hotkeys.mapNotNull { (otherAction, otherCombo) ->
        if (otherAction == action || otherCombo.isEmpty()) return@mapNotNull null
        val other = otherCombo.toSet()
        when {
            candidate == other -> HotkeyConflict(otherAction, HotkeyConflict.Kind.EXACT)
            tapAndCombo(action, combo, otherCombo) || tapAndCombo(otherAction, otherCombo, combo) -> null
            candidate.containsAll(other) || other.containsAll(candidate) ->
                HotkeyConflict(otherAction, HotkeyConflict.Kind.OVERLAP)
            combo.first() in otherCombo.drop(1) || otherCombo.first() in combo.drop(1) ->
                HotkeyConflict(otherAction, HotkeyConflict.Kind.CROSS_ROLE)
            else -> null
        }
    }
}

private fun tapAndCombo(tapAction: String, tap: List<Int>, longer: List<Int>): Boolean =
    tap.size == 1 && tapAction !in HotkeyAction.HOLDS && longer.size > 1 && longer.first() == tap[0]

/**
 * The question a conflicting binding asks, the actions named by [name]. An exact conflict asks whether to overwrite the
 * action(s) bound to the same combo, and names any that only overlap it after; with no exact one, the overlaps are asked
 * about.
 */
fun hotkeyConflictMessage(conflicts: List<HotkeyConflict>, name: (String) -> String): String {
    val exact = conflicts.filter { it.kind == HotkeyConflict.Kind.EXACT }.map { name(it.action) }.distinct()
    val overlapping = conflicts.map { name(it.action) }.distinct() - exact.toSet()
    if (exact.isEmpty()) return "The selected hotkey combo overlaps ${andList(overlapping)}, would you like to overwrite?"
    val question = "The selected hotkey combo is already bound to ${andList(exact)}, would you like to overwrite?"
    return if (overlapping.isEmpty()) question else "$question It also overlaps ${andList(overlapping)}, which would be unbound too."
}

private fun andList(names: List<String>): String =
    if (names.size < 2) names.joinToString() else names.dropLast(1).joinToString(", ") + " and " + names.last()

/** The pairs of actions whose combos already collide at a level, by [hotkeyConflicts]'s rules, each pair once. */
fun existingHotkeyConflicts(hotkeys: Map<String, List<Int>>): List<Pair<String, String>> {
    val actions = hotkeys.filterValues { it.isNotEmpty() }.keys.toList()
    return actions.flatMapIndexed { index, action ->
        hotkeyConflicts(hotkeys, action, hotkeys.getValue(action))
            .map { it.action }
            .filter { actions.indexOf(it) > index }
            .map { action to it }
    }
}

/**
 * The hotkey tokens that hold a button binding ("k:<keycode>" or "a:<axis>:<dir>", as InputBindings reads them): a
 * trigger's L2/R2 key, a hat's D-pad key, a stick direction's axis token. A combo whose first key is among them
 * takes that button from the game.
 */
fun bindingTokens(binding: String): List<Int> {
    val parts = binding.split(':')
    val code = parts.getOrNull(1)?.toIntOrNull() ?: return emptyList()
    return when (parts[0]) {
        "k" -> listOf(code)
        "a" -> {
            val positive = parts.getOrNull(2) == "1"
            when (code) {
                MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE -> listOf(KeyEvent.KEYCODE_BUTTON_L2)
                MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS -> listOf(KeyEvent.KEYCODE_BUTTON_R2)
                MotionEvent.AXIS_HAT_X -> listOf(if (positive) KeyEvent.KEYCODE_DPAD_RIGHT else KeyEvent.KEYCODE_DPAD_LEFT)
                MotionEvent.AXIS_HAT_Y -> listOf(if (positive) KeyEvent.KEYCODE_DPAD_DOWN else KeyEvent.KEYCODE_DPAD_UP)
                else -> listOf(HotkeyTokens.axis(code, positive))
            }
        }
        else -> emptyList()
    }
}
