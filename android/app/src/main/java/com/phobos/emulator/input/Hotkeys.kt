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
    const val SLOW_MOTION = "slow_motion"
    const val N64_PAK = "n64_pak"
    const val PS1_ANALOG_TOGGLE = "analog_toggle"
    const val KEYBOARD = "keyboard"
    const val LIBRARY = "library"
    /** The screensaver at once, on the Library's and the menus' pages alone, never over a game ([WindowKeys]). */
    const val SHOW_SCREENSAVER = "show_screensaver"

    const val SAVE_SLOT_PREFIX = "save_slot_"
    const val LOAD_SLOT_PREFIX = "load_slot_"

    /** Direct save/load slot actions, 1 through 9. */
    val DIRECT_SLOTS: List<String> = (1..9).flatMap { listOf("$SAVE_SLOT_PREFIX$it", "$LOAD_SLOT_PREFIX$it") }

    /** Actions that last while their combo is held, rather than running once. */
    val HOLDS: Set<String> = setOf(FAST_FORWARD_HOLD, SLOW_MOTION, FRAME_ADVANCE)
}

/**
 * An action's bindings: up to [LIMIT] combos, as ares allows (its BindingLimit), any of which runs it. Each keeps its
 * slot, so clearing one leaves the others where they are; the empty slots after the last bound one are dropped.
 */
object HotkeyBindings {
    const val LIMIT = 3

    /** [bindings] with the combo in [slot] replaced by [combo]; an empty one clears the slot. */
    fun with(bindings: List<List<Int>>, slot: Int, combo: List<Int>): List<List<Int>> =
        List(maxOf(bindings.size, slot + 1)) { if (it == slot) combo else bindings.getOrElse(it) { emptyList() } }
            .dropLastWhile { it.isEmpty() }

    /** As stored: each combo's keys joined by ",", the combos by ";", an empty slot kept so the later ones keep theirs. */
    fun encode(bindings: List<List<Int>>): String =
        bindings.dropLastWhile { it.isEmpty() }.joinToString(";") { it.joinToString(",") }

    /** A stored value: one from before there were several bindings, a single combo, is the first. */
    fun parse(value: String): List<List<Int>> =
        value.split(';').take(LIMIT).map { combo -> combo.split(',').mapNotNull { it.trim().toIntOrNull() } }
            .dropLastWhile { it.isEmpty() }
}

/**
 * The hotkeys a player who never bound one has. Back (the button a handheld such as the Retroid Pocket has beside Home)
 * leads every controller one, held with one other button, so each button stays the game's when pressed alone: Back
 * with A pauses, with R1 saves, with L1 loads (Quit's asks first), and with Start, outside a game, shows the
 * screensaver. A controller with no Back button, most of them, sending theirs as Select, gets the Hotkeys page's offer
 * of another button in Back's place, at every level at once ([Controls.replaceHotkeyLeadEverywhere]). A keyboard's keys
 * are second bindings: F5 saves, F9 loads, F8 takes a screenshot, F12 pauses and Tab holds fast forward. The actions
 * left out (Reset, Frame Advance, the fast-forward toggle, the direct slots) are unbound: the pause menu has Reset, and
 * a player can bind them on purpose.
 */
val DEFAULT_HOTKEYS: Map<String, List<List<Int>>> = KeyEvent.KEYCODE_BACK.let { back ->
    mapOf(
        HotkeyAction.PAUSE to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_A), listOf(KeyEvent.KEYCODE_F12)),
        HotkeyAction.SAVE_STATE to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_R1), listOf(KeyEvent.KEYCODE_F5)),
        HotkeyAction.LOAD_STATE to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_L1), listOf(KeyEvent.KEYCODE_F9)),
        HotkeyAction.NEXT_SLOT to listOf(listOf(back, KeyEvent.KEYCODE_DPAD_RIGHT)),
        HotkeyAction.PREVIOUS_SLOT to listOf(listOf(back, KeyEvent.KEYCODE_DPAD_LEFT)),
        HotkeyAction.RELOAD to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_X)),
        HotkeyAction.QUIT to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_B)),
        HotkeyAction.SCREENSHOT to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_THUMBR), listOf(KeyEvent.KEYCODE_F8)),
        HotkeyAction.MUTE to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_SELECT)),
        HotkeyAction.FAST_FORWARD_HOLD to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_R2), listOf(KeyEvent.KEYCODE_TAB)),
        HotkeyAction.SLOW_MOTION to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_L2)),
        HotkeyAction.N64_PAK to listOf(listOf(back, KeyEvent.KEYCODE_DPAD_DOWN)),
        HotkeyAction.PS1_ANALOG_TOGGLE to listOf(listOf(back, KeyEvent.KEYCODE_DPAD_UP)),
        HotkeyAction.KEYBOARD to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_THUMBL)),
        HotkeyAction.LIBRARY to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_Y)),
        HotkeyAction.SHOW_SCREENSAVER to listOf(listOf(back, KeyEvent.KEYCODE_BUTTON_START)),
    )
}

/** Every bound combo, with its action: each action's bindings in slot order, the empty slots left out. */
private fun Map<String, List<List<Int>>>.bound(): List<Pair<String, List<Int>>> =
    flatMap { (action, bindings) -> bindings.filter { it.isNotEmpty() }.map { action to it } }

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

/** [action]'s binding in [slot] collides with another, as [kind]. */
data class HotkeyConflict(val action: String, val slot: Int, val kind: Kind) {
    enum class Kind { EXACT, OVERLAP, CROSS_ROLE }
}

/**
 * A one-key combo whose key leads longer combos: a tap binding. It fires when its key is let go with nothing else
 * pressed meanwhile, so it doesn't fire as the longer combos are pressed. A hold's own longer bindings don't make that
 * hold's one-key binding a tap: whichever of them is held, the hold is the same, so its key starts it at once (a tap,
 * coming as its key is let go, can't start a hold).
 */
fun isTapCombo(hotkeys: Map<String, List<List<Int>>>, combo: List<Int>): Boolean =
    combo.size == 1 && hotkeys.bound().any { (action, other) ->
        other.size > 1 && other.first() == combo[0] &&
            !(action in HotkeyAction.HOLDS && combo in hotkeys.getValue(action))
    }

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

    /** A key event pressed [token], and none has let it go yet. */
    fun holdsKey(token: Int): Boolean = token in keyTokens

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
    fun press(hotkeys: Map<String, List<List<Int>>>, token: Int): Boolean {
        if (!keyTokens.add(token) || token in order) return token in consumed
        tapped = null
        tapCandidate = if (order.isEmpty()) token else null
        order += token
        val take = consumedOnDown(hotkeys, token)
        if (take) consumed += token else consumed -= token
        pressedSinceLook = true
        return take
    }

    /**
     * A key went down that no key event here reports (a D-pad direction Android makes from a hat, say): the key held is
     * no longer pressed alone, so its release is no tap ([taps], [letGoAlone]).
     */
    fun pressedBeside() {
        tapCandidate = null
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
    fun syncExternal(hotkeys: Map<String, List<List<Int>>>, held: Set<Int>): Boolean {
        val combos = hotkeys.bound().map { it.second }
        val relevant = held.filterTo(mutableSetOf()) { token ->
            !HotkeyTokens.isAxis(token) || combos.any { token in it }
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
        val modifiers = combos.map { it.first() }.toSet()
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
    fun completions(hotkeys: Map<String, List<List<Int>>>): List<String> {
        if (!pressedSinceLook) return emptyList()
        pressedSinceLook = false
        return matches(hotkeys)
    }

    /**
     * The actions with a binding whose combo is exactly the set held, its modifier pressed first; never a tap binding.
     * Each action once, however many of its bindings match.
     */
    fun matches(hotkeys: Map<String, List<List<Int>>>): List<String> {
        val pressed = order.toSet()
        return hotkeys.bound().mapNotNull { (action, combo) ->
            if (combo.size != pressed.size || !pressed.containsAll(combo)) return@mapNotNull null
            if (isTapCombo(hotkeys, combo)) return@mapNotNull null
            val positions = combo.map(order::indexOf)
            if (positions.first() == positions.min()) action else null
        }.distinct()
    }

    /**
     * Whether the last release let go of [token] pressed alone, nothing else pressed while it was held, as a tap's key
     * is: ask before [taps], which forgets it.
     */
    fun letGoAlone(token: Int): Boolean = tapped == token

    /** The actions a tap binding of the last release completed; each tap is reported once. */
    fun taps(hotkeys: Map<String, List<List<Int>>>): List<String> {
        val key = tapped ?: return emptyList()
        tapped = null
        return hotkeys.bound()
            .mapNotNull { (action, combo) -> action.takeIf { combo == listOf(key) && isTapCombo(hotkeys, combo) } }
            .distinct()
    }

    /** Whether every key of [combo] is held (others may be too): how long a hold action lasts. */
    fun holding(combo: List<Int>): Boolean = combo.isNotEmpty() && order.containsAll(combo)

    private fun consumedOnDown(hotkeys: Map<String, List<List<Int>>>, token: Int): Boolean {
        val combos = hotkeys.bound().map { it.second }
        // The first key of a combo is always its modifier, never the game's.
        if (combos.any { it.first() == token }) return true
        val at = order.indexOf(token)
        if (at < 0) return false
        return combos.any { combo ->
            combo.size > 1 && token in combo.drop(1) &&
                combo.first() in order && order.indexOf(combo.first()) < at
        }
    }
}

/**
 * The other actions' bindings that binding [combo] to one of [action]'s would overwrite at a level: the same set, a
 * combo that is part of it or contains it, or a key that is one combo's modifier and another's action. Two combos that
 * share only their modifier are fine, and so are a tap binding and the longer combos its key leads ([isTapCombo]),
 * unless the tap is a hold action, which can't wait for its key's release. [action]'s own other bindings aren't
 * checked, since whichever of them fires runs the same action: a hold's one-key binding starts it at once even where
 * its key leads a longer binding of that hold ([isTapCombo]). But where a combo of two keys or more is part of another
 * of the same action, not a hold, pressing the longer can run the action twice, the shorter completing on the way.
 */
fun hotkeyConflicts(hotkeys: Map<String, List<List<Int>>>, action: String, combo: List<Int>): List<HotkeyConflict> {
    if (combo.isEmpty()) return emptyList()
    val candidate = combo.toSet()
    return hotkeys.filterKeys { it != action }.flatMap { (otherAction, bindings) ->
        bindings.mapIndexedNotNull { slot, otherCombo ->
            val other = otherCombo.toSet()
            val kind = when {
                otherCombo.isEmpty() -> null
                candidate == other -> HotkeyConflict.Kind.EXACT
                tapAndCombo(action, combo, otherCombo) || tapAndCombo(otherAction, otherCombo, combo) -> null
                candidate.containsAll(other) || other.containsAll(candidate) -> HotkeyConflict.Kind.OVERLAP
                combo.first() in otherCombo.drop(1) || otherCombo.first() in combo.drop(1) -> HotkeyConflict.Kind.CROSS_ROLE
                else -> null
            }
            kind?.let { HotkeyConflict(otherAction, slot, it) }
        }
    }
}

private fun tapAndCombo(tapAction: String, tap: List<Int>, longer: List<Int>): Boolean =
    tap.size == 1 && tapAction !in HotkeyAction.HOLDS && longer.size > 1 && longer.first() == tap[0]

/**
 * The question a conflicting binding asks, the actions named by [name]. An exact conflict asks whether to overwrite the
 * action(s) bound to the same combo, and names any that only overlap it after; with no exact one, the overlaps are asked
 * about. Overwriting clears only the colliding bindings, so the overlapped actions keep their others.
 */
fun hotkeyConflictMessage(conflicts: List<HotkeyConflict>, name: (String) -> String): String {
    val exact = conflicts.filter { it.kind == HotkeyConflict.Kind.EXACT }.map { name(it.action) }.distinct()
    val overlapping = conflicts.map { name(it.action) }.distinct() - exact.toSet()
    if (exact.isEmpty()) return "The selected hotkey combo overlaps ${andList(overlapping)}, would you like to overwrite?"
    val question = "The selected hotkey combo is already bound to ${andList(exact)}, would you like to overwrite?"
    if (overlapping.isEmpty()) return question
    val lost = if (conflicts.count { name(it.action) in overlapping } > 1) "those bindings" else "that binding"
    return "$question It also overlaps ${andList(overlapping)}, which would lose $lost too."
}

/** Names joined as a sentence lists them: "A", "A and B", "A, B and C". */
fun andList(names: List<String>): String =
    if (names.size < 2) names.joinToString() else names.dropLast(1).joinToString(", ") + " and " + names.last()

/** The pairs of actions with bindings that already collide at a level, by [hotkeyConflicts]'s rules, each pair once. */
fun existingHotkeyConflicts(hotkeys: Map<String, List<List<Int>>>): List<Pair<String, String>> {
    val actions = hotkeys.bound().map { it.first }.distinct()
    return actions.flatMapIndexed { index, action ->
        hotkeys.getValue(action).flatMap { hotkeyConflicts(hotkeys, action, it) }
            .map { it.action }
            .distinct()
            .filter { actions.indexOf(it) > index }
            .map { action to it }
    }
}

/**
 * What starting with another key every binding one key starts would do at a level ([leadReplacement]): [changes], each
 * changed action's bindings as they'd be, and [blockers], the actions in the way.
 */
data class LeadReplacement(val changes: Map<String, List<List<Int>>>, val blockers: List<String>)

/**
 * [hotkeys]' bindings that [from] starts started by [to] instead, the rest of each combo kept, and the actions in the
 * way: one with a binding [from] starts that has [to] already (it would lose a key), or with a binding that a moved one
 * would collide with by [hotkeyConflicts]'s rules and doesn't already. Each of those has [to] in it, a moved binding
 * differing from before only in [to]; the moved ones stand to one another as before.
 */
fun leadReplacement(hotkeys: Map<String, List<List<Int>>>, from: Int, to: Int): LeadReplacement {
    if (to == from) return LeadReplacement(emptyMap(), emptyList())
    fun replaced(combo: List<Int>) = if (combo.firstOrNull() == from) listOf(to) + combo.drop(1) else combo
    val led = hotkeys.filterValues { bindings -> bindings.any { it.firstOrNull() == from } }
    val losing = led.filterValues { bindings -> bindings.any { it.firstOrNull() == from && to in it } }.keys
    val colliding = led.flatMap { (action, bindings) ->
        bindings.filter { it.firstOrNull() == from && to !in it }.flatMap { combo ->
            val before = hotkeyConflicts(hotkeys, action, combo).map { it.action to it.slot }.toSet()
            hotkeyConflicts(hotkeys, action, replaced(combo)).filter { (it.action to it.slot) !in before }
        }
    }.map { it.action }
    return LeadReplacement(led.mapValues { (_, bindings) -> bindings.map(::replaced) }, (losing + colliding).distinct())
}

/**
 * Show Screensaver's bindings where none are stored for it, [hotkeys] being all consoles' hotkeys as the defaults and
 * what's stored make them: Back + Start, unless no other binding starts with Back. A button chosen for Back's place
 * before Show Screensaver came ([Controls.replaceHotkeyLeadEverywhere]) took it in every binding Back started, and Show
 * Screensaver moves to it too, as the choice would have moved it then; with the bindings Back led by default not
 * started by one same button now, or something in the way ([leadReplacement]), it's unbound.
 */
fun showScreensaverDefault(hotkeys: Map<String, List<List<Int>>>): List<List<Int>> {
    val back = KeyEvent.KEYCODE_BACK
    val default = DEFAULT_HOTKEYS.getValue(HotkeyAction.SHOW_SCREENSAVER)
    val others = hotkeys - HotkeyAction.SHOW_SCREENSAVER
    if (others.values.any { bindings -> bindings.any { it.firstOrNull() == back } }) return default
    // The keys in Back's place now in the bindings it led by default, the rest of each combo as it was.
    val leads = (DEFAULT_HOTKEYS - HotkeyAction.SHOW_SCREENSAVER).flatMap { (action, defaults) ->
        defaults.mapIndexedNotNull { slot, combo ->
            val now = others[action]?.getOrNull(slot).orEmpty()
            now.firstOrNull()?.takeIf { combo.firstOrNull() == back && now.drop(1) == combo.drop(1) }
        }
    }.toSet()
    val lead = leads.singleOrNull() ?: return emptyList()
    val moved = leadReplacement(others + (HotkeyAction.SHOW_SCREENSAVER to default), back, lead)
    return if (moved.blockers.isEmpty()) moved.changes.getValue(HotkeyAction.SHOW_SCREENSAVER) else emptyList()
}

/**
 * Whether a connected controller has been seen sending Back, [seen] holding the descriptors of the devices that have;
 * true with no gamepad among [devices] (each one's descriptor, and whether it's a gamepad), as nothing says otherwise
 * yet. A device seen counts whether it's a gamepad or not: a controller can come as more than one device, its Back on
 * one that isn't a gamepad. A controller declaring the key doesn't say: one may declare keys it has no button for.
 */
fun backSeen(devices: List<Pair<String, Boolean>>, seen: Set<String>): Boolean =
    devices.none { it.second } || devices.any { it.first in seen }

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
