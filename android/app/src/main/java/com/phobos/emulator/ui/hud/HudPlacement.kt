package com.phobos.emulator.ui.hud

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import kotlin.math.max
import kotlin.math.min

/**
 * Where the performance monitor sits: one of six places along the top and bottom edges, or
 * [CUSTOM], where the user moved it on screen. [x] and [y] are the place as a fraction of the
 * space around the box, 0 at the left or top and 1 at the right or bottom.
 */
enum class HudPosition(val label: String, val x: Float, val y: Float) {
    TOP_LEFT("Top left", 0f, 0f),
    TOP_CENTER("Top center", 0.5f, 0f),
    TOP_RIGHT("Top right", 1f, 0f),
    BOTTOM_LEFT("Bottom left", 0f, 1f),
    BOTTOM_CENTER("Bottom center", 0.5f, 1f),
    BOTTOM_RIGHT("Bottom right", 1f, 1f),
    CUSTOM("Custom", 0f, 0f);

    val isTop: Boolean get() = y == 0f && this != CUSTOM

    /** Where this place puts the box; the horizontal layout is always centered. Null for [CUSTOM]. */
    fun fractions(horizontal: Boolean): Offset? = if (this == CUSTOM) null else Offset(if (horizontal) 0.5f else x, y)

    /** Whether the box is centered between the screen's sides here, so it resizes evenly about its center. */
    fun centered(horizontal: Boolean): Boolean = this != CUSTOM && (horizontal || x == 0.5f)

    /** The horizontal layout offers only the top or the bottom. */
    fun label(horizontal: Boolean): String = when {
        this == CUSTOM || !horizontal -> label
        isTop -> "Top"
        else -> "Bottom"
    }

    /**
     * This place on the top or bottom edge, in the same column, so the vertical layout gets it back.
     * [CUSTOM] has no column and takes the middle one, where the horizontal layout showed it.
     */
    fun onEdge(top: Boolean): HudPosition {
        val column = if (this == CUSTOM) 0.5f else x
        return PRESETS.first { it.x == column && it.isTop == top }
    }

    companion object {
        val PRESETS: List<HudPosition> = entries.filter { it != CUSTOM }
        val DEFAULT = TOP_RIGHT

        /**
         * The saved setting, or before there was one, the place an older version's dragged position
         * is: an exact corner or edge center is that place, anything else [CUSTOM].
         */
        fun fromSetting(saved: String, legacyX: Float, legacyY: Float): HudPosition =
            entries.firstOrNull { it.name == saved }
                ?: PRESETS.firstOrNull { it.x == legacyX && it.y == legacyY }
                ?: CUSTOM
    }
}

/**
 * The monitor's saved placement: its position, where it was moved in each orientation since a
 * place was last picked, and the size it was given in each. [legacy] is where an older version
 * left it, for a monitor moved there and not since.
 */
data class HudPlacement(
    val position: HudPosition = HudPosition.DEFAULT,
    val customLandscape: Offset? = null,
    val customPortrait: Offset? = null,
    val sizeLandscape: HudBoxSize? = null,
    val sizePortrait: HudBoxSize? = null,
    val legacy: Offset = Offset(HudPosition.DEFAULT.x, HudPosition.DEFAULT.y),
) {
    /** Where it was moved in this orientation, else in the other one, else where an older version left it. */
    fun custom(landscape: Boolean): Offset =
        (if (landscape) customLandscape ?: customPortrait else customPortrait ?: customLandscape) ?: legacy

    fun size(landscape: Boolean): HudBoxSize? = if (landscape) sizeLandscape else sizePortrait

    /**
     * This placement once [edit] is saved, as [com.phobos.emulator.data.SettingsStore.savePerfHudEdit]
     * saves it: a place forgets where it was moved in both orientations.
     */
    fun applying(edit: HudEdit): HudPlacement = copy(
        position = edit.position,
        customLandscape = when {
            edit.custom == null -> null
            edit.landscape -> edit.custom
            else -> customLandscape
        },
        customPortrait = when {
            edit.custom == null -> null
            edit.landscape -> customPortrait
            else -> edit.custom
        },
        sizeLandscape = if (edit.landscape) edit.size else sizeLandscape,
        sizePortrait = if (edit.landscape) sizePortrait else edit.size,
    )
}

/** What an on-screen edit leaves, for the orientation it was made in; [custom] is null for a preset position. */
data class HudEdit(
    val position: HudPosition,
    val landscape: Boolean,
    val custom: Offset?,
    val size: HudBoxSize?,
    val scale: Float,
)

/** A size set on screen, as fractions of the screen's width and height. */
data class HudBoxSize(val width: Float, val height: Float) {
    fun toPx(screen: Size): Size = Size(width * screen.width, height * screen.height)

    fun encode(): String = "$width,$height"

    companion object {
        fun of(size: Size, screen: Size): HudBoxSize =
            HudBoxSize((size.width / screen.width).coerceIn(0f, 1f), (size.height / screen.height).coerceIn(0f, 1f))

        /** A saved size, or null for none or anything unreadable. */
        fun decode(saved: String): HudBoxSize? =
            decodePair(saved)?.takeIf { it.x > 0f && it.y > 0f && it.x <= 1f && it.y <= 1f }?.let { HudBoxSize(it.x, it.y) }
    }
}

/** A saved custom position ("x,y", fractions of the space around the box), or null. */
fun decodeHudFractions(saved: String): Offset? = decodePair(saved)?.takeIf { it.x in 0f..1f && it.y in 0f..1f }

fun encodeHudFractions(fractions: Offset): String = "${fractions.x},${fractions.y}"

private fun decodePair(saved: String): Offset? {
    val parts = saved.split(',')
    if (parts.size != 2) return null
    val a = parts[0].trim().toFloatOrNull() ?: return null
    val b = parts[1].trim().toFloatOrNull() ?: return null
    if (a.isNaN() || b.isNaN()) return null
    return Offset(a, b)
}

/** The top-left corner of a [box]-sized monitor placed at [fractions] of the free space in [area]. */
fun hudOffset(fractions: Offset, box: Size, area: Size): Offset = Offset(
    fractions.x.coerceIn(0f, 1f) * max(0f, area.width - box.width),
    fractions.y.coerceIn(0f, 1f) * max(0f, area.height - box.height),
)

/** The fractions of the free space that put a [box]-sized monitor's corner at [offset]; the inverse of [hudOffset]. */
fun hudFractions(offset: Offset, box: Size, area: Size): Offset {
    val freeX = area.width - box.width
    val freeY = area.height - box.height
    return Offset(
        if (freeX > 0f) (offset.x / freeX).coerceIn(0f, 1f) else 0f,
        if (freeY > 0f) (offset.y / freeY).coerceIn(0f, 1f) else 0f,
    )
}

/** [size] no smaller than [min] and no larger than [max]; [max] wins where they conflict. */
fun clampHudSize(size: Size, min: Size, max: Size): Size = Size(
    size.width.coerceAtLeast(min.width).coerceAtMost(max.width),
    size.height.coerceAtLeast(min.height).coerceAtMost(max.height),
)

/** A resize handle on the box's corners and sides, by the sides it moves. */
enum class HudHandle(val left: Boolean = false, val top: Boolean = false, val right: Boolean = false, val bottom: Boolean = false) {
    TOP_LEFT(left = true, top = true),
    TOP(top = true),
    TOP_RIGHT(top = true, right = true),
    RIGHT(right = true),
    BOTTOM_RIGHT(right = true, bottom = true),
    BOTTOM(bottom = true),
    BOTTOM_LEFT(left = true, bottom = true),
    LEFT(left = true);

    /** Where the handle sits on [box]. */
    fun at(box: Rect): Offset = Offset(
        when {
            left -> box.left
            right -> box.right
            else -> box.center.x
        },
        when {
            top -> box.top
            bottom -> box.bottom
            else -> box.center.y
        },
    )
}

/**
 * [box] after its [handle] is dragged by [delta]: the sides the handle holds follow the finger
 * and the others stay, or with [symmetric] the left and right sides move together about the
 * center. The box keeps at least [min] and stays within [area].
 */
fun resizeHudBox(box: Rect, handle: HudHandle, delta: Offset, min: Size, area: Rect, symmetric: Boolean): Rect {
    var left = box.left
    var right = box.right
    if (symmetric && (handle.left || handle.right)) {
        val grow = if (handle.left) -delta.x else delta.x
        val room = min(box.center.x - area.left, area.right - box.center.x)
        val half = (box.width / 2f + grow).coerceAtLeast(min.width / 2f).coerceAtMost(room)
        left = box.center.x - half
        right = box.center.x + half
    } else {
        if (handle.left) left = (box.left + delta.x).coerceAtMost(box.right - min.width).coerceAtLeast(area.left)
        if (handle.right) right = (box.right + delta.x).coerceAtLeast(box.left + min.width).coerceAtMost(area.right)
    }
    var top = box.top
    var bottom = box.bottom
    if (handle.top) top = (box.top + delta.y).coerceAtMost(box.bottom - min.height).coerceAtLeast(area.top)
    if (handle.bottom) bottom = (box.bottom + delta.y).coerceAtLeast(box.top + min.height).coerceAtMost(area.bottom)
    return Rect(left, top, right, bottom)
}

/** What a touch grabs in the edit mode. */
sealed interface HudEditTarget {
    data class Handle(val handle: HudHandle) : HudEditTarget
    data object Box : HudEditTarget
    data object Outside : HudEditTarget
}

/**
 * What a touch at [point] grabs: the nearest handle within [reach], else the box when it's on
 * it. On a box small enough that every point is near a handle, a touch nearer its middle than any
 * handle still grabs the box.
 */
fun hudEditTarget(box: Rect, point: Offset, reach: Float): HudEditTarget {
    val inside = point.x in box.left..box.right && point.y in box.top..box.bottom
    val toMiddle = if (inside) (point - box.center).getDistance() else Float.POSITIVE_INFINITY
    val (handle, distance) = HudHandle.entries.map { it to (it.at(box) - point).getDistance() }.minBy { it.second }
    return when {
        distance <= reach && distance < toMiddle -> HudEditTarget.Handle(handle)
        inside -> HudEditTarget.Box
        else -> HudEditTarget.Outside
    }
}

/** [box] moved by [delta], kept within [area]. */
fun moveHudBox(box: Rect, delta: Offset, area: Rect): Rect {
    val left = (box.left + delta.x).coerceAtMost(area.right - box.width).coerceAtLeast(area.left)
    val top = (box.top + delta.y).coerceAtMost(area.bottom - box.height).coerceAtLeast(area.top)
    return Rect(Offset(left, top), box.size)
}

/** [box] scaled evenly by [factor] about its center, keeping at least [min] and within [area]. */
fun scaleHudBox(box: Rect, factor: Float, min: Size, area: Rect): Rect {
    if (box.width <= 0f || box.height <= 0f) return box
    val most = min(area.width / box.width, area.height / box.height)
    val least = min(max(min.width / box.width, min.height / box.height), most)
    val f = factor.coerceIn(least, most)
    val size = Size(box.width * f, box.height * f)
    val left = (box.center.x - size.width / 2f).coerceAtMost(area.right - size.width).coerceAtLeast(area.left)
    val top = (box.center.y - size.height / 2f).coerceAtMost(area.bottom - size.height).coerceAtLeast(area.top)
    return Rect(Offset(left, top), size)
}

/**
 * The largest text scale in [minScale]..[maxScale] at which the monitor's contents fit a box
 * [width] by [height]. [unitHeight] measures the contents' height at scale 1 laid out to a given
 * width; at scale s they're that layout at width / s, drawn s times larger, so they fit when
 * s × unitHeight(width / s) ≤ height, which grows with s. [minScale] when nothing fits.
 */
fun fitHudScale(width: Float, height: Float, minScale: Float, maxScale: Float, unitHeight: (Float) -> Float): Float {
    fun fits(scale: Float) = unitHeight(width / scale) * scale <= height
    if (!fits(minScale)) return minScale
    if (fits(maxScale)) return maxScale
    var low = minScale
    var high = maxScale
    repeat(10) {
        val mid = (low + high) / 2f
        if (fits(mid)) low = mid else high = mid
    }
    return low
}
