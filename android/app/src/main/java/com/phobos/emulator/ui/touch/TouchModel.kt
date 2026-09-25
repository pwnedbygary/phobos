package com.phobos.emulator.ui.touch

import java.util.Locale
import kotlin.math.abs

/** Point an element's default position hangs from, as fractions of the overlay size. */
enum class Anchor(val fx: Float, val fy: Float) {
    TOP_LEFT(0f, 0f), TOP_CENTER(0.5f, 0f), TOP_RIGHT(1f, 0f),
    CENTER_LEFT(0f, 0.5f), CENTER(0.5f, 0.5f), CENTER_RIGHT(1f, 0.5f),
    BOTTOM_LEFT(0f, 1f), BOTTOM_CENTER(0.5f, 1f), BOTTOM_RIGHT(1f, 1f),
}

/** Default element center: the anchor point plus (dx, dy) dp, scaled with the global control size. */
data class Placement(val anchor: Anchor, val dx: Float, val dy: Float)

enum class ButtonShape {
    CIRCLE, PILL, SHOULDER_LEFT, SHOULDER_RIGHT;

    /** The same shape seen in a mirror (swap-hands layouts). */
    fun mirrored(): ButtonShape = when (this) {
        SHOULDER_LEFT -> SHOULDER_RIGHT
        SHOULDER_RIGHT -> SHOULDER_LEFT
        else -> this
    }
}

enum class Glyph {
    TEXT, PS_TRIANGLE, PS_CIRCLE, PS_CROSS, PS_SQUARE,
    ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT,
    MENU, FAST_FORWARD, KEYBOARD,
}

/** Non-game functions a touch button can trigger instead of (or besides) core input bits. */
enum class TouchAction { NONE, MENU, FAST_FORWARD, KEYBOARD }

enum class Stick { LEFT, RIGHT }

enum class StickGate { ROUND, OCTAGON }

/**
 * One touchable button inside a [ButtonCluster]. [dx]/[dy] are the button's center relative to
 * the cluster center and [w]/[h] its visual size, all in dp before scaling.
 *
 * A button sends [bits] (VirtualGamepad bits, see PhobosCore.Input) and/or holds keyboard [key]
 * (a core keyboard-matrix label for ZX Spectrum / MSX / ColecoVision keypad).
 */
data class TouchButton(
    val label: String,
    val bits: Int = 0,
    val key: String? = null,
    val dx: Float = 0f,
    val dy: Float = 0f,
    val w: Float = 58f,
    val h: Float = w,
    val shape: ButtonShape = ButtonShape.CIRCLE,
    val glyph: Glyph = Glyph.TEXT,
    val accent: Long = TouchPalette.NEUTRAL,
    val action: TouchAction = TouchAction.NONE,
    /** Tap to latch, tap again to release. */
    val toggle: Boolean = false,
    /** Label size relative to the button's smaller side. */
    val labelScale: Float = 0.42f,
)

sealed interface TouchElement {
    val id: String
    /** Name shown in the layout editor. */
    val title: String
    val land: Placement
    val port: Placement
    val hiddenByDefault: Boolean
    /** Hidden by default in portrait only (not enough room below the game picture). */
    val hiddenInPortrait: Boolean
    /** Unscaled half extents (dp) of the element's bounding box around its center. */
    val halfWidth: Float
    val halfHeight: Float
}

data class DpadElement(
    override val id: String = "dpad",
    override val title: String = "D-Pad",
    override val land: Placement,
    override val port: Placement,
    val size: Float = 150f,
    override val hiddenByDefault: Boolean = false,
    override val hiddenInPortrait: Boolean = false,
) : TouchElement {
    override val halfWidth: Float get() = size / 2f
    override val halfHeight: Float get() = size / 2f
}

data class AnalogElement(
    override val id: String,
    override val title: String,
    override val land: Placement,
    override val port: Placement,
    val stick: Stick = Stick.LEFT,
    val size: Float = 150f,
    val gate: StickGate = StickGate.ROUND,
    override val hiddenByDefault: Boolean = false,
    override val hiddenInPortrait: Boolean = false,
) : TouchElement {
    override val halfWidth: Float get() = size / 2f
    override val halfHeight: Float get() = size / 2f
}

data class ButtonCluster(
    override val id: String,
    override val title: String,
    override val land: Placement,
    override val port: Placement,
    val buttons: List<TouchButton>,
    override val hiddenByDefault: Boolean = false,
    override val hiddenInPortrait: Boolean = false,
    /** A finger landing between two buttons of this cluster presses both (e.g. A+B with one thumb). */
    val multiHit: Boolean = false,
) : TouchElement {
    override val halfWidth: Float = buttons.maxOf { abs(it.dx) + it.w / 2f }
    override val halfHeight: Float = buttons.maxOf { abs(it.dy) + it.h / 2f }
}

data class TouchLayout(val family: TouchFamily, val elements: List<TouchElement>)

/** User placement for one element in one system + orientation. Null position = keep the default. */
data class ElementOverride(
    val fx: Float? = null,
    val fy: Float? = null,
    val scale: Float = 1f,
    val hidden: Boolean? = null,
)

/**
 * Compact persistence format for per-system layout overrides:
 * `id|fx|fy|scale|hidden;id|...` where an empty fx/fy keeps the default position and
 * hidden is `1`, `0` or empty (element default).
 */
object TouchLayoutCodec {
    fun encode(overrides: Map<String, ElementOverride>): String =
        overrides.entries
            .filter { (id, _) -> id.isNotEmpty() && '|' !in id && ';' !in id }
            .joinToString(";") { (id, o) ->
                val fx = o.fx?.let { String.format(Locale.ROOT, "%.4f", it) } ?: ""
                val fy = o.fy?.let { String.format(Locale.ROOT, "%.4f", it) } ?: ""
                val scale = String.format(Locale.ROOT, "%.3f", o.scale)
                val hidden = when (o.hidden) { true -> "1"; false -> "0"; null -> "" }
                "$id|$fx|$fy|$scale|$hidden"
            }

    fun decode(encoded: String?): Map<String, ElementOverride> {
        if (encoded.isNullOrBlank()) return emptyMap()
        val result = LinkedHashMap<String, ElementOverride>()
        for (entry in encoded.split(';')) {
            val parts = entry.split('|')
            if (parts.size < 5 || parts[0].isEmpty()) continue
            val fx = parts[1].toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f)
            val fy = parts[2].toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f)
            val scale = parts[3].toFloatOrNull()?.takeIf { it.isFinite() }?.coerceIn(MIN_ELEMENT_SCALE, MAX_ELEMENT_SCALE) ?: 1f
            val hidden = when (parts[4]) { "1" -> true; "0" -> false; else -> null }
            // A position needs both coordinates; a half-specified point falls back to the default.
            val (px, py) = if (fx != null && fy != null) fx to fy else null to null
            result[parts[0]] = ElementOverride(px, py, scale, hidden)
        }
        return result
    }

    const val MIN_ELEMENT_SCALE = 0.5f
    const val MAX_ELEMENT_SCALE = 2.0f
}

enum class HapticLevel(val label: String) { OFF("Off"), LIGHT("Light"), MEDIUM("Medium"), STRONG("Strong") }

enum class DpadMode(val label: String) { EIGHT_WAY("8-way"), FOUR_WAY("4-way") }

enum class AnalogMode(val label: String) { FIXED("Fixed"), FLOATING("Floating") }

/** Global touch-control preferences (per-system placement lives in the layout overrides). */
data class TouchPrefs(
    /** Landscape opacity (controls overlap the picture). */
    val opacity: Float = 0.8f,
    /** Portrait opacity (controls usually sit below the picture). */
    val opacityPortrait: Float = 1.0f,
    val scale: Float = 1.0f,
    val haptics: HapticLevel = HapticLevel.LIGHT,
    val slideBetweenButtons: Boolean = true,
    /** Holding a button for [TouchEngine.AUTO_HOLD_MS] keeps it held until it is tapped again. */
    val autoHold: Boolean = false,
    /** Mirror control positions left/right (D-pad on the right). */
    val swapHands: Boolean = false,
    /** Dim the controls after a few seconds without touches; any touch restores them. */
    val idleFade: Boolean = false,
    val dpadMode: DpadMode = DpadMode.EIGHT_WAY,
    /** 0..1: width of the diagonal zones of the 8-way D-pad (0.5 = eight equal sectors). */
    val dpadDiagonal: Float = 0.5f,
    val analogMode: AnalogMode = AnalogMode.FIXED,
    /** Radial dead zone applied to the touch stick before the core's own dead zone (0..0.5). */
    val analogDeadzone: Float = 0.0f,
    /** Travel multiplier: higher = full deflection with less thumb movement (0.5..2). */
    val analogSensitivity: Float = 1.0f,
    val hideOnController: Boolean = true,
    val showMenuButton: Boolean = true,
    val showFastForwardButton: Boolean = false,
)

/** Button accent colors, as ARGB longs so layouts stay free of Compose types. */
object TouchPalette {
    const val NEUTRAL = 0xFFC9D1DE
    const val GOLD = 0xFFE6C229

    const val RED = 0xFFF0525F
    const val YELLOW = 0xFFF7C948
    const val GREEN = 0xFF34C77B
    const val BLUE = 0xFF4C8DFF
    const val ORANGE = 0xFFFF8A3D

    // Nintendo 64
    const val N64_A = 0xFF4C7DFF
    const val N64_B = 0xFF2FBF71
    const val N64_C = 0xFFFFCC33
    const val N64_START = 0xFFFF5252

    // PlayStation face symbols
    const val PS_TRIANGLE = 0xFF3DDBB0
    const val PS_CIRCLE = 0xFFFF6A7A
    const val PS_CROSS = 0xFF84ADFF
    const val PS_SQUARE = 0xFFFF94DC

    // Game Boy / Game Boy Color A-B, Game Boy Advance A-B
    const val GB_AB = 0xFFD9427A
    const val GBA_AB = 0xFF9A8CFF
}
