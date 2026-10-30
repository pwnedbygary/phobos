package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The pure label layout against fixed metrics (measure = length * size / 2), so every number is exact and
 * machine-independent; the draw sequence is checked structurally through a recording [LabelPainter.LabelSurface].
 * Rendered pixels are the RP6's job by decision (no android.graphics in this suite).
 */
class LabelPainterTest {

    private val metric = { text: String, size: Float -> text.length * size * 0.5f }

    private fun color(r: Int, g: Int, b: Int) = (0xFF shl 24) or (r shl 16) or (g shl 8) or b

    @Test
    fun squareLayoutHasCartridgesGeometryAndColours() {
        val blue = color(0x3B, 0x6D, 0xB5)
        val layout = LabelPainter.layout(LabelPainter.Spec(blue, "GB", "POKEMON RED"), 520, 520, metric)

        // mix(color, .35, white) and mix(color, .5, black); blue is dark, so text is white.
        assertEquals(color(0x7F, 0xA0, 0xCE), layout.gradientTop)
        assertEquals(blue, layout.gradientMiddle)
        assertEquals(color(0x1D, 0x36, 0x5A), layout.gradientBottom)
        assertEquals(0xFFFFFFFF.toInt(), layout.title.first().color)

        // stripes: step 7% of the width from -h while below w+h
        assertEquals(520 * 0.07f, layout.stripeStep, 0.0001f)
        assertEquals(520 * 0.02f, layout.stripeWidth, 0.0001f)
        val stripes = layout.stripeXs
        assertEquals(43, stripes.size)
        assertEquals(-520f, stripes.first(), 0.0001f)
        assertEquals(-520f + 42 * 520 * 0.07f, stripes.last(), 0.01f)

        // band: x = 8% of width, baseline x + 6% of the short side, size 7.5%
        val band = layout.band!!
        assertEquals("GB", band.text)
        assertEquals(41.6f, band.x, 0.001f)
        assertEquals(41.6f + 31.2f, band.baseline, 0.001f)
        assertEquals(39f, band.size, 0.001f)
        assertEquals(700, band.weight)

        // title: size 13% of width (below 20% of height), one line, baseline h - l with no sub
        val title = layout.title.single()
        assertEquals("POKEMON RED", title.text)
        assertEquals(68f, title.size, 0.001f)
        assertEquals(41.6f, title.x, 0.001f)
        assertEquals(520f - 41.6f, title.baseline, 0.001f)
        assertEquals(800, title.weight)
        assertNull(layout.sub)

        // generated labels carry no Game Boy side strip (that belongs to art crops)
        assertEquals(0f, layout.gbStripWidth, 0f)
        assertNull(layout.gbStripText)
    }

    @Test
    fun subLiftsTheFirstBaselineAndScalesToTheTitle() {
        val layout = LabelPainter.layout(LabelPainter.Spec(color(0x20, 0x20, 0x20), null, "ONE LINE", "SUB"), 400, 300, metric)
        val title = layout.title.single()
        assertEquals(52f, title.size, 0.001f) // round(min(400*.13=52, 300*.2=60))
        assertEquals(300f - 32f - 52f * 0.55f, title.baseline, 0.01f)
        val sub = layout.sub!!
        assertEquals("SUB", sub.text)
        assertEquals(22f, sub.size, 0.01f) // round(52 * .42)
        assertEquals(300f - 32f * 0.8f, sub.baseline, 0.01f)
        assertEquals(600, sub.weight)
    }

    @Test
    fun wrapStopsAtThreeLines() {
        val words = "AAAA BBBB CCCC DDDD"
        val lines = LabelPainter.wrap(words, 1f, 10f, { t, s -> t.length * s }, maxLines = 3)
        assertEquals(listOf("AAAA", "BBBB", "CCCC"), lines)
    }

    @Test
    fun roundLayoutCentresTheTitleAndSub() {
        val layout = LabelPainter.layout(LabelPainter.Spec(color(0x40, 0x40, 0x40), null, "DISC", "SUB", round = true), 300, 300, metric)
        assertNull(layout.band)
        val title = layout.title.single()
        assertEquals(150f, title.x, 0.001f)
        assertEquals(90f, title.baseline, 0.001f) // 30% of the height
        assertEquals(30f, title.size, 0.001f) // 10% of the width
        assertTrue(title.centered)
        assertEquals(150f, layout.sub!!.x, 0.001f)
        assertEquals(234f, layout.sub!!.baseline, 0.001f) // 78% of the height
    }

    @Test
    fun lightColourTakesDarkText() {
        val layout = LabelPainter.layout(LabelPainter.Spec(color(0xE8, 0xEC, 0xF2), null, "X"), 200, 200, metric)
        assertEquals(0xFF000000.toInt(), layout.title.first().color)
    }

    @Test
    fun paintDrawsBackgroundStripesThenText() {
        val layout = LabelPainter.layout(LabelPainter.Spec(color(0x30, 0x30, 0x30), null, "X"), 200, 200, metric)
        val calls = mutableListOf<String>()
        val surface = object : LabelPainter.LabelSurface {
            override val width = 200
            override val height = 200
            override fun background(top: Int, middle: Int, bottom: Int, endX: Float, endY: Float) { calls += "background" }
            override fun line(x0: Float, y0: Float, x1: Float, y1: Float, width: Float, color: Int) { calls += "line" }
            override fun fillRect(x: Float, y: Float, w: Float, h: Float, color: Int) { calls += "rect" }
            override fun text(t: LabelPainter.Text) { calls += "text:${t.text}" }
            override fun rotate(degrees: Float, cx: Float, cy: Float, block: () -> Unit) { block() }
        }
        LabelPainter.paint(surface, layout)
        assertEquals("background", calls.first())
        assertEquals(layout.stripeXs.size, calls.count { it == "line" })
        assertEquals("text:X", calls.last())
        assertEquals(layout.stripeXs.size + 2, calls.size) // background + stripes + one title
    }
}
