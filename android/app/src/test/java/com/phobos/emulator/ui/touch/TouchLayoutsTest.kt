package com.phobos.emulator.ui.touch

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.max

/**
 * Default layouts must fit common screens without controls overlapping. Sizes are in dp
 * (density 1), at the default control scale.
 */
class TouchLayoutsTest {
    private val landscapeSizes = listOf(740f to 360f, 851f to 393f, 915f to 412f, 972f to 437f, 1280f to 800f)
    private val portraitSizes = landscapeSizes.map { (w, h) -> h to w }

    private val optionSets = listOf(
        TouchLayouts.Options(showMenu = true, showFastForward = true, ps1Analog = true, wonderSwanVertical = false),
        TouchLayouts.Options(showMenu = true, showFastForward = false, ps1Analog = false, wonderSwanVertical = true),
    )

    @Test fun everyFamilyHasUniqueElementIds() {
        for (family in TouchFamily.entries) for (options in optionSets) {
            val ids = TouchLayouts.forFamily(family, options).elements.map { it.id }
            assertEquals("duplicate ids in $family: $ids", ids.size, ids.toSet().size)
        }
    }

    @Test fun librarySystemsMapToFamilies() {
        assertEquals(TouchFamily.PS1, TouchFamily.of("PlayStation"))
        assertEquals(TouchFamily.GB, TouchFamily.of("Game Boy Color"))
        assertEquals(TouchFamily.MEGA_DRIVE, TouchFamily.of("Mega CD"))
        assertEquals(TouchFamily.NEO_GEO, TouchFamily.of("Neo Geo CD"))
        assertEquals(TouchFamily.ZX, TouchFamily.of("ZX Spectrum 128"))
        assertEquals(TouchFamily.GENERIC, TouchFamily.of("Something New"))
    }

    @Test fun playStationFaceButtonsMatchNativeBits() {
        val face = TouchLayouts.forFamily(TouchFamily.PS1).elements.first { it.id == "face" } as ButtonCluster
        val bitsByGlyph = face.buttons.associate { it.glyph to it.bits }
        // PhobosRunner resolveButtonBit(): Cross = A, Circle = B, Square = X, Triangle = Y.
        assertEquals(com.phobos.emulator.PhobosCore.Input.A, bitsByGlyph[Glyph.PS_CROSS])
        assertEquals(com.phobos.emulator.PhobosCore.Input.B, bitsByGlyph[Glyph.PS_CIRCLE])
        assertEquals(com.phobos.emulator.PhobosCore.Input.X, bitsByGlyph[Glyph.PS_SQUARE])
        assertEquals(com.phobos.emulator.PhobosCore.Input.Y, bitsByGlyph[Glyph.PS_TRIANGLE])
    }

    @Test fun defaultLayoutsDoNotOverlap() {
        val problems = ArrayList<String>()
        for (family in TouchFamily.entries) for (options in optionSets) {
            val layout = TouchLayouts.forFamily(family, options)
            for ((w, h) in landscapeSizes + portraitSizes) {
                val placed = placeLayout(layout, emptyMap(), w, h, density = 1f, globalScale = 1f)
                for (i in placed.indices) for (j in i + 1 until placed.size) {
                    if (overlaps(placed[i], placed[j])) {
                        problems += "$family ${w.toInt()}x${h.toInt()}: ${placed[i].element.id} overlaps ${placed[j].element.id}"
                    }
                }
            }
        }
        if (problems.isNotEmpty()) fail(problems.distinct().joinToString("\n"))
    }

    @Test fun defaultLayoutsStayOnScreen() {
        for (family in TouchFamily.entries) {
            val layout = TouchLayouts.forFamily(family)
            for ((w, h) in landscapeSizes + portraitSizes) {
                for (el in placeLayout(layout, emptyMap(), w, h, density = 1f, globalScale = 1f)) {
                    assertTrue("$family ${el.element.id} off screen at ${w}x$h",
                        el.cx - el.halfW >= -0.5f && el.cx + el.halfW <= w + 0.5f &&
                            el.cy - el.halfH >= -0.5f && el.cy + el.halfH <= h + 0.5f)
                }
            }
        }
    }

    // ── Shape geometry ─────────────────────────────────────────────────────

    private sealed class Shape {
        data class Circle(val x: Float, val y: Float, val r: Float) : Shape()
        data class Box(val x: Float, val y: Float, val hw: Float, val hh: Float) : Shape()
    }

    private fun shapes(el: PlacedElement): List<Shape> = when (el.element) {
        is DpadElement, is AnalogElement -> listOf(Shape.Circle(el.cx, el.cy, el.radius))
        is ButtonCluster -> el.buttons.map { b ->
            if (b.isCircle) Shape.Circle(b.cx, b.cy, minOf(b.halfW, b.halfH)) else Shape.Box(b.cx, b.cy, b.halfW, b.halfH)
        }
    }

    private fun overlaps(a: PlacedElement, b: PlacedElement): Boolean =
        shapes(a).any { sa -> shapes(b).any { sb -> intersects(sa, sb) } }

    private fun intersects(a: Shape, b: Shape): Boolean = when {
        a is Shape.Circle && b is Shape.Circle -> hypot(a.x - b.x, a.y - b.y) < a.r + b.r - TOLERANCE
        a is Shape.Box && b is Shape.Box ->
            abs(a.x - b.x) < a.hw + b.hw - TOLERANCE && abs(a.y - b.y) < a.hh + b.hh - TOLERANCE
        a is Shape.Circle && b is Shape.Box -> circleBox(a, b)
        a is Shape.Box && b is Shape.Circle -> circleBox(b, a)
        else -> false
    }

    private fun circleBox(c: Shape.Circle, b: Shape.Box): Boolean {
        val dx = max(abs(c.x - b.x) - b.hw, 0f)
        val dy = max(abs(c.y - b.y) - b.hh, 0f)
        return hypot(dx, dy) < c.r - TOLERANCE
    }

    private companion object {
        const val TOLERANCE = 1f
    }
}
