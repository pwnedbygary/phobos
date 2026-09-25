package com.phobos.emulator.ui.touch

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** An element placed on screen: its center and size in px. */
class PlacedElement(
    val element: TouchElement,
    val cx: Float,
    val cy: Float,
    /** px per layout dp (density x global scale x element scale). */
    val unit: Float,
    /** Placed in a swap-hands (left/right mirrored) layout; shoulder shapes flip to match. */
    val mirrored: Boolean = false,
) {
    val halfW: Float get() = element.halfWidth * unit
    val halfH: Float get() = element.halfHeight * unit

    /** Radius of a D-pad or stick base, in px. */
    val radius: Float
        get() = when (element) {
            is DpadElement -> element.size / 2f * unit
            is AnalogElement -> element.size / 2f * unit
            is ButtonCluster -> max(halfW, halfH)
        }

    val buttons: List<PlacedButton> =
        (element as? ButtonCluster)?.buttons?.mapIndexed { i, b -> PlacedButton(this, i, b) } ?: emptyList()
}

class PlacedButton(val owner: PlacedElement, val index: Int, val button: TouchButton) {
    val cx: Float = owner.cx + button.dx * owner.unit
    val cy: Float = owner.cy + button.dy * owner.unit
    val halfW: Float = button.w / 2f * owner.unit
    val halfH: Float = button.h / 2f * owner.unit
    val isCircle: Boolean get() = button.shape == ButtonShape.CIRCLE

    /** Distance from the center, scaled so the visual edge is 1.0. */
    fun normalizedDistance(x: Float, y: Float): Float {
        val dx = x - cx
        val dy = y - cy
        return if (isCircle) hypot(dx, dy) / min(halfW, halfH)
        else max(abs(dx) / halfW, abs(dy) / halfH)
    }

    /** Inside the button grown by [scale], with every half-extent at least [minHalf] px. */
    fun withinReach(x: Float, y: Float, scale: Float, minHalf: Float): Boolean {
        val dx = abs(x - cx)
        val dy = abs(y - cy)
        return if (isCircle) hypot(dx, dy) <= max(min(halfW, halfH) * scale, minHalf)
        else dx <= max(halfW * scale, minHalf) && dy <= max(halfH * scale, minHalf)
    }
}

/** What the engine wants the host to do after a batch of pointer events. */
class TouchEvents(
    val haptic: Boolean,
    val actions: List<TouchAction>,
    val backgroundTap: Boolean,
    /** A button was latched by auto-hold (distinct confirmation). */
    val holdHaptic: Boolean = false,
)

/** Visual state of an active stick: base origin and thumb offset, in px. */
class StickVisual(val originX: Float, val originY: Float, val thumbDx: Float, val thumbDy: Float)

/**
 * Multi-touch state machine for the on-screen controls.
 *
 * Every pointer is tracked independently. A pointer that lands on the D-pad or a stick keeps
 * control of it until lifted; a pointer on buttons can slide onto other buttons (when enabled),
 * so rolling a thumb from B to A works as on a real pad. Sticks report a unit-circle vector with
 * screen-down positive, matching the physical-stick path in GameInputState.
 *
 * Pure Kotlin (no Android or Compose types) so the geometry and gesture rules are unit-testable.
 */
class TouchEngine {
    var prefs: TouchPrefs = TouchPrefs()
    /** Screen density (px per dp), used for gesture slop. */
    var density: Float = 3f

    private var placed: List<PlacedElement> = emptyList()
    private val pointers = LinkedHashMap<Long, Pointer>()
    private val latched = HashSet<PlacedKey>()
    private val stickValues = HashMap<Stick, FloatArray>()
    private val releasedSticks = HashSet<Stick>()

    private var pendingHaptic = false
    private var pendingHoldHaptic = false
    private val pendingActions = ArrayList<TouchAction>()
    private var pendingBackgroundTap = false

    /** Identity of a button that survives relayout (element id + index in its cluster). */
    private data class PlacedKey(val elementId: String, val index: Int)

    private sealed class Grab {
        object Background : Grab()
        class Dpad(val element: PlacedElement) : Grab() { var bits = 0 }
        class Analog(val element: PlacedElement, val originX: Float, val originY: Float) : Grab() {
            var x = 0f
            var y = 0f
            var thumbDx = 0f
            var thumbDy = 0f
        }
        class Buttons(var pressed: List<PlacedButton>) : Grab()
    }

    private class Pointer(val downX: Float, val downY: Float, val downTimeMs: Long, var grab: Grab) {
        var maxTravel = 0f
        /** When the current button press began (reset when the finger slides to other buttons). */
        var pressStartMs = downTimeMs
        /** This press released an auto-hold latch, so lifting must not latch again. */
        var unlatchedHold = false
    }

    fun setLayout(elements: List<PlacedElement>) {
        val previous = placed
        placed = elements
        // Geometry changed (rotation, settings change): lift fingers and auto-hold latches cleanly.
        if (previous.isNotEmpty()) releaseAll()
        latched.retainAll { key -> elements.any { it.element.id == key.elementId } }
    }

    val elements: List<PlacedElement> get() = placed

    fun down(id: Long, x: Float, y: Float, timeMs: Long) {
        val grab = grabAt(x, y)
        val pointer = Pointer(x, y, timeMs, grab)
        pointers[id] = pointer
        when (grab) {
            is Grab.Dpad -> updateDpad(grab, x, y)
            is Grab.Analog -> updateAnalog(grab, x, y)
            is Grab.Buttons -> onButtonsPressed(pointer, emptyList(), grab.pressed)
            Grab.Background -> {}
        }
    }

    fun move(id: Long, x: Float, y: Float, timeMs: Long) {
        val pointer = pointers[id] ?: return
        pointer.maxTravel = max(pointer.maxTravel, hypot(x - pointer.downX, y - pointer.downY))
        when (val grab = pointer.grab) {
            is Grab.Dpad -> updateDpad(grab, x, y)
            is Grab.Analog -> updateAnalog(grab, x, y)
            is Grab.Buttons -> if (prefs.slideBetweenButtons) {
                // Sliding never newly presses an action button (menu, fast-forward, keyboard).
                val now = if (stillOnPress(grab.pressed, x, y)) grab.pressed
                else buttonsAt(x, y).filter { b -> b.button.action == TouchAction.NONE || grab.pressed.any { it === b } }
                if (!samePress(grab.pressed, now)) {
                    val before = grab.pressed
                    grab.pressed = now
                    pointer.pressStartMs = timeMs
                    pointer.unlatchedHold = false
                    onButtonsPressed(pointer, before, now)
                }
            }
            Grab.Background -> if (prefs.slideBetweenButtons) {
                val now = buttonsAt(x, y).filter { it.button.action == TouchAction.NONE }
                if (now.isNotEmpty()) {
                    pointer.grab = Grab.Buttons(now)
                    pointer.pressStartMs = timeMs
                    onButtonsPressed(pointer, emptyList(), now)
                }
            }
        }
    }

    fun up(id: Long, x: Float, y: Float, timeMs: Long) {
        val pointer = pointers.remove(id) ?: return
        when (val grab = pointer.grab) {
            is Grab.Dpad -> grab.bits = 0
            is Grab.Analog -> releaseStick(grab)
            is Grab.Buttons -> {
                for (b in grab.pressed) {
                    // Same reach as the press, so a small menu button opens wherever it lit up.
                    if (b.button.action == TouchAction.MENU &&
                        b.withinReach(x, y, 1f + BUTTON_SLOP, MIN_TARGET_RADIUS_DP * density)
                    ) {
                        pendingActions += TouchAction.MENU
                    }
                }
                // Auto-hold: a single plain button held long enough stays held after lifting.
                val held = grab.pressed.singleOrNull()
                if (prefs.autoHold && held != null && canAutoHold(held.button) && !pointer.unlatchedHold &&
                    timeMs - pointer.pressStartMs >= AUTO_HOLD_MS
                ) {
                    latched += PlacedKey(held.owner.element.id, held.index)
                    pendingHoldHaptic = true
                }
                grab.pressed = emptyList()
            }
            Grab.Background -> {
                // A tap that misses the buttons of a control is not a tap on the game picture.
                val travel = max(pointer.maxTravel, hypot(x - pointer.downX, y - pointer.downY))
                if (!isOnControl(pointer.downX, pointer.downY) &&
                    timeMs - pointer.downTimeMs <= TAP_TIMEOUT_MS && travel <= tapSlopPx()
                ) {
                    pendingBackgroundTap = true
                }
            }
        }
    }

    /** The system took over the gesture: drop the finger without tap, menu or auto-hold effects. */
    fun cancel(id: Long) {
        val grab = pointers.remove(id)?.grab ?: return
        if (grab is Grab.Analog) releaseStick(grab)
    }

    /** Lift every finger and drop auto-hold latches (relayout, overlay hidden). Toggle latches survive. */
    fun releaseAll() {
        for (pointer in pointers.values) {
            val grab = pointer.grab
            if (grab is Grab.Analog) releaseStick(grab)
        }
        pointers.clear()
        latched.removeAll { buttonFor(it)?.button?.toggle != true }
    }

    /** Clear toggle latches too (new game, layout reset). */
    fun resetLatches() {
        latched.clear()
    }

    // ── Outputs ────────────────────────────────────────────────────────────

    /** Core input bits currently held by touch (fingers plus latched toggles). */
    val buttonBits: Int
        get() {
            var bits = 0
            for (pointer in pointers.values) {
                when (val grab = pointer.grab) {
                    is Grab.Dpad -> bits = bits or grab.bits
                    is Grab.Buttons -> for (b in grab.pressed) if (!b.button.toggle) bits = bits or b.button.bits
                    else -> {}
                }
            }
            for (key in latched) bits = bits or (buttonFor(key)?.button?.bits ?: 0)
            return bits
        }

    /** Keyboard-matrix keys currently held by touch. */
    val keys: Set<String>
        get() {
            val held = HashSet<String>()
            for (pointer in pointers.values) {
                val grab = pointer.grab as? Grab.Buttons ?: continue
                for (b in grab.pressed) if (!b.button.toggle) b.button.key?.let(held::add)
            }
            for (key in latched) buttonFor(key)?.button?.key?.let(held::add)
            return held
        }

    /** Stick vector controlled by touch, or null when no finger is on that stick. */
    fun stick(stick: Stick): FloatArray? = stickValues[stick]

    /** Sticks released since the last call; the host sends one zero update for each. */
    fun consumeReleasedSticks(): Set<Stick> {
        if (releasedSticks.isEmpty()) return emptySet()
        val released = releasedSticks.toSet()
        releasedSticks.clear()
        return released
    }

    fun consumeEvents(): TouchEvents? {
        if (!pendingHaptic && !pendingHoldHaptic && pendingActions.isEmpty() && !pendingBackgroundTap) return null
        val events = TouchEvents(pendingHaptic, pendingActions.toList(), pendingBackgroundTap, pendingHoldHaptic)
        pendingHaptic = false
        pendingHoldHaptic = false
        pendingActions.clear()
        pendingBackgroundTap = false
        return events
    }

    // ── Visual queries ─────────────────────────────────────────────────────

    fun isPressed(button: PlacedButton): Boolean {
        if (PlacedKey(button.owner.element.id, button.index) in latched) return true
        for (pointer in pointers.values) {
            val grab = pointer.grab as? Grab.Buttons ?: continue
            if (grab.pressed.any { it === button }) return true
        }
        return false
    }

    fun dpadBits(element: PlacedElement): Int {
        for (pointer in pointers.values) {
            val grab = pointer.grab as? Grab.Dpad ?: continue
            if (grab.element === element) return grab.bits
        }
        return 0
    }

    fun stickVisual(element: PlacedElement): StickVisual? {
        for (pointer in pointers.values) {
            val grab = pointer.grab as? Grab.Analog ?: continue
            if (grab.element === element) return StickVisual(grab.originX, grab.originY, grab.thumbDx, grab.thumbDy)
        }
        return null
    }

    // ── Hit testing ────────────────────────────────────────────────────────

    private fun grabAt(x: Float, y: Float): Grab {
        directButtonAt(x, y)?.let { return Grab.Buttons(listOf(it)) }
        gapPairAt(x, y)?.let { return Grab.Buttons(it) }
        for (el in placed.asReversed()) {
            if (el.element is DpadElement && hypot(x - el.cx, y - el.cy) <= el.radius * (1f + PAD_SLOP)) {
                return Grab.Dpad(el)
            }
        }
        for (el in placed.asReversed()) {
            if (el.element is AnalogElement && !stickHeld(el) &&
                hypot(x - el.cx, y - el.cy) <= el.radius * (1f + PAD_SLOP)
            ) {
                return analogGrab(el, x, y)
            }
        }
        slopButtonAt(x, y)?.let { return Grab.Buttons(listOf(it)) }
        if (prefs.analogMode == AnalogMode.FLOATING) {
            for (el in placed.asReversed()) {
                if (el.element is AnalogElement && !stickHeld(el) &&
                    hypot(x - el.cx, y - el.cy) <= el.radius * FLOATING_ZONE
                ) {
                    return analogGrab(el, x, y)
                }
            }
        }
        return Grab.Background
    }

    /** One finger per stick: a second finger would zero it when either lifts. */
    private fun stickHeld(el: PlacedElement): Boolean =
        pointers.values.any { (it.grab as? Grab.Analog)?.element === el }

    private fun isOnControl(x: Float, y: Float): Boolean =
        placed.any { abs(x - it.cx) <= it.halfW && abs(y - it.cy) <= it.halfH }

    /** A single pressed button stays pressed slightly past its edge, so a resting thumb does not chatter. */
    private fun stillOnPress(pressed: List<PlacedButton>, x: Float, y: Float): Boolean {
        val single = pressed.singleOrNull() ?: return false
        return single.normalizedDistance(x, y) <= 1f + PRESS_STICKINESS
    }

    private fun analogGrab(el: PlacedElement, x: Float, y: Float): Grab.Analog =
        if (prefs.analogMode == AnalogMode.FLOATING) Grab.Analog(el, x, y) else Grab.Analog(el, el.cx, el.cy)

    /** Buttons under a sliding finger: a direct hit, a between-buttons pair, or a near miss. */
    private fun buttonsAt(x: Float, y: Float): List<PlacedButton> {
        directButtonAt(x, y)?.let { return listOf(it) }
        gapPairAt(x, y)?.let { return it }
        slopButtonAt(x, y)?.let { return listOf(it) }
        return emptyList()
    }

    private fun directButtonAt(x: Float, y: Float): PlacedButton? = nearestButton(x, y, 1f, minTarget = false)

    /** Near misses: 18% beyond the visual edge, and never less than a 48dp-wide target. */
    private fun slopButtonAt(x: Float, y: Float): PlacedButton? = nearestButton(x, y, 1f + BUTTON_SLOP, minTarget = true)

    private fun nearestButton(x: Float, y: Float, limit: Float, minTarget: Boolean): PlacedButton? {
        val minHalf = if (minTarget) MIN_TARGET_RADIUS_DP * density else 0f
        var best: PlacedButton? = null
        var bestDistance = Float.MAX_VALUE
        for (el in placed.asReversed()) {
            for (b in el.buttons) {
                val d = b.normalizedDistance(x, y)
                if (d < bestDistance && b.withinReach(x, y, limit, minHalf)) {
                    best = b
                    bestDistance = d
                }
            }
        }
        return best
    }

    /**
     * For clusters that allow it, a finger resting in the gap between two neighbouring round
     * buttons presses both. The zone is the corridor joining the two centers, narrower than the
     * smaller button.
     */
    private fun gapPairAt(x: Float, y: Float): List<PlacedButton>? {
        var best: List<PlacedButton>? = null
        var bestOffset = Float.MAX_VALUE
        for (el in placed) {
            val cluster = el.element as? ButtonCluster ?: continue
            if (!cluster.multiHit) continue
            val candidates = el.buttons.filter { it.isCircle && it.button.action == TouchAction.NONE }
            for (i in candidates.indices) for (j in i + 1 until candidates.size) {
                val a = candidates[i]
                val b = candidates[j]
                val ra = min(a.halfW, a.halfH)
                val rb = min(b.halfW, b.halfH)
                val abx = b.cx - a.cx
                val aby = b.cy - a.cy
                val length = hypot(abx, aby)
                if (length <= 0f || length > (ra + rb) * GAP_NEIGHBOUR_FACTOR) continue
                val t = ((x - a.cx) * abx + (y - a.cy) * aby) / (length * length)
                if (t <= 0f || t >= 1f) continue
                val offset = abs((x - a.cx) * aby - (y - a.cy) * abx) / length
                if (offset <= min(ra, rb) * GAP_CORRIDOR && offset < bestOffset) {
                    best = listOf(a, b)
                    bestOffset = offset
                }
            }
        }
        return best
    }

    private fun samePress(a: List<PlacedButton>, b: List<PlacedButton>): Boolean =
        a.size == b.size && a.all { x -> b.any { it === x } }

    // ── Buttons ────────────────────────────────────────────────────────────

    private fun onButtonsPressed(pointer: Pointer, before: List<PlacedButton>, now: List<PlacedButton>) {
        for (b in now) {
            if (before.any { it === b }) continue
            pendingHaptic = true
            val key = PlacedKey(b.owner.element.id, b.index)
            if (b.button.toggle) {
                if (!latched.remove(key)) latched.add(key)
            } else if (latched.remove(key)) {
                // Tapping an auto-held button releases it; lifting this finger must not re-latch.
                pointer.unlatchedHold = true
            }
            when (b.button.action) {
                TouchAction.FAST_FORWARD, TouchAction.KEYBOARD -> pendingActions += b.button.action
                TouchAction.MENU, TouchAction.NONE -> {}
            }
        }
    }

    private fun canAutoHold(button: TouchButton): Boolean =
        button.action == TouchAction.NONE && !button.toggle && (button.bits != 0 || button.key != null)

    private fun buttonFor(key: PlacedKey): PlacedButton? =
        placed.firstOrNull { it.element.id == key.elementId }?.buttons?.getOrNull(key.index)

    // ── D-pad ──────────────────────────────────────────────────────────────

    private fun updateDpad(grab: Grab.Dpad, x: Float, y: Float) {
        val el = grab.element
        val bits = dpadDirection(x - el.cx, y - el.cy, el.radius, prefs.dpadMode, prefs.dpadDiagonal, grab.bits)
        if (bits != grab.bits) {
            if (bits != 0 && (bits and grab.bits.inv()) != 0) pendingHaptic = true
            grab.bits = bits
        }
    }

    // ── Sticks ─────────────────────────────────────────────────────────────

    private fun updateAnalog(grab: Grab.Analog, x: Float, y: Float) {
        val el = grab.element
        val stick = (el.element as AnalogElement).stick
        val dx = x - grab.originX
        val dy = y - grab.originY
        val value = stickVector(dx, dy, el.radius, prefs.analogSensitivity, prefs.analogDeadzone)
        grab.x = value[0]
        grab.y = value[1]
        // The thumb shows the output: it reaches its travel limit exactly at full deflection.
        val maxThumb = el.radius * THUMB_TRAVEL
        grab.thumbDx = value[0] * maxThumb
        grab.thumbDy = value[1] * maxThumb
        stickValues[stick] = value
        releasedSticks.remove(stick)
    }

    private fun releaseStick(grab: Grab.Analog) {
        val stick = (grab.element.element as AnalogElement).stick
        if (stickValues.remove(stick) != null) releasedSticks.add(stick)
    }

    private fun tapSlopPx(): Float = density * TAP_SLOP_DP

    companion object {
        /** Extra reach around round/pill buttons, as a fraction of their size. */
        const val BUTTON_SLOP = 0.18f
        /** How far past its edge a pressed button stays pressed under a sliding finger. */
        const val PRESS_STICKINESS = 0.08f
        const val PAD_SLOP = 0.10f
        const val FLOATING_ZONE = 1.8f
        /** Buttons count as neighbours (gap presses both) when centers are this close, in radii sums. */
        const val GAP_NEIGHBOUR_FACTOR = 1.45f
        const val GAP_CORRIDOR = 0.6f
        /** Fraction of the stick radius the thumb must travel for full deflection (at sensitivity 1). */
        const val STICK_TRAVEL = 0.8f
        const val THUMB_TRAVEL = 0.62f
        const val DPAD_DEADZONE = 0.18f
        const val DIAGONAL_MIN_DEG = 20f
        const val DIAGONAL_MAX_DEG = 70f
        const val DPAD_HYSTERESIS_DEG = 4f
        const val TAP_TIMEOUT_MS = 350L
        const val TAP_SLOP_DP = 12f
        /** Minimum touch target radius (48dp diameter, the Android accessibility minimum). */
        const val MIN_TARGET_RADIUS_DP = 24f
        /** Hold time after which auto-hold keeps a button pressed. */
        const val AUTO_HOLD_MS = 800L

        const val UP = 1 shl 0
        const val DOWN = 1 shl 1
        const val LEFT = 1 shl 2
        const val RIGHT = 1 shl 3

        /**
         * D-pad direction bits for a finger at (dx, dy) px from the pad center. The 8-way diagonal
         * zones are [diagonal]-scaled around 45 degree lines, with hysteresis so a finger resting
         * on a boundary does not chatter between two directions.
         */
        fun dpadDirection(dx: Float, dy: Float, radius: Float, mode: DpadMode, diagonal: Float, previous: Int): Int {
            if (hypot(dx, dy) < radius * DPAD_DEADZONE) return 0
            var deg = Math.toDegrees(atan2(-dy.toDouble(), dx.toDouble())).toFloat()
            if (deg < 0f) deg += 360f

            if (mode == DpadMode.EIGHT_WAY) {
                val halfWidth = (DIAGONAL_MIN_DEG + (DIAGONAL_MAX_DEG - DIAGONAL_MIN_DEG) * diagonal.coerceIn(0f, 1f)) / 2f
                for ((center, bits) in DIAGONALS) {
                    // Wider while this diagonal is held, narrower while a cardinal is; equal on first touch.
                    val limit = halfWidth + when (previous) {
                        bits -> DPAD_HYSTERESIS_DEG
                        0 -> 0f
                        else -> -DPAD_HYSTERESIS_DEG
                    }
                    if (angularDistance(deg, center) <= limit) return bits
                }
            }
            // Cardinal: nearest axis, with hysteresis toward the one already held.
            var best = 0
            var bestDistance = Float.MAX_VALUE
            for ((center, bits) in CARDINALS) {
                val distance = angularDistance(deg, center) - if (previous == bits) DPAD_HYSTERESIS_DEG else 0f
                if (distance < bestDistance) {
                    best = bits
                    bestDistance = distance
                }
            }
            return best
        }

        /** Unit-circle stick vector for a thumb offset of (dx, dy) px from the stick origin. */
        fun stickVector(dx: Float, dy: Float, radius: Float, sensitivity: Float, deadzone: Float): FloatArray {
            val travel = radius * STICK_TRAVEL / sensitivity.coerceIn(0.25f, 4f)
            var x = dx / travel
            var y = dy / travel
            var magnitude = hypot(x, y)
            if (magnitude > 1f) {
                x /= magnitude
                y /= magnitude
                magnitude = 1f
            }
            val dz = deadzone.coerceIn(0f, 0.9f)
            if (dz > 0f) {
                if (magnitude <= dz) return floatArrayOf(0f, 0f)
                val rescale = (magnitude - dz) / (1f - dz) / magnitude
                x *= rescale
                y *= rescale
            }
            return floatArrayOf(x, y)
        }

        private fun angularDistance(a: Float, b: Float): Float {
            val d = abs(a - b) % 360f
            return if (d > 180f) 360f - d else d
        }

        private val CARDINALS = listOf(0f to RIGHT, 90f to UP, 180f to LEFT, 270f to DOWN)
        private val DIAGONALS = listOf(
            45f to (UP or RIGHT), 135f to (UP or LEFT), 225f to (DOWN or LEFT), 315f to (DOWN or RIGHT),
        )
    }
}
