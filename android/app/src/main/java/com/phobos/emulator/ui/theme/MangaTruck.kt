package com.phobos.emulator.ui.theme

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Truck-kun: the truck that opens a hundred isekai stories, charging straight at the reader, seen from
 * low down: focus lines converging on its grille, its headlights blazing; ISEKAI across its front, 1-UP on
 * its plate, a VRRROOOM!! and the narration, a light novel's title: "That time ares got hit by Truck-kun
 * and was reincarnated on Android".
 */
internal fun Page.truck(out: FloatArray, lettering: Lettering?) {
    val wide = w >= h
    val frame = truckFrame(w, h)
    val grille = FloatArray(2).also { frame.toPage(0f, 0.18f, it) }
    val reach = hypot(max(grille[0], w - grille[0]), max(grille[1], h - grille[1]))
    val thickest = unit * 6.5f
    val rays = rays(340, frame.scale * 0.8f, thickest)
    val angles = FloatArray(rays.size) { rays[it].angle }
    for (y in 0 until h) for (x in 0 until w) out[y * w + x] = rayInk(x, y, grille[0], grille[1], rays, angles, reach, thickest)
    truckBody(out, frame)
    glare(out, frame)
    if (lettering != null) truckLettering(out, lettering, frame, wide)
}

/** The truck's own coordinates on a [w] by [h] page: its cab 2 units wide, seen from low down, tilted a little. */
internal fun truckFrame(w: Int, h: Int): Frame =
    if (w >= h) Frame(w * 0.5f, h * 0.6f, h * 0.31f, -0.07f, 0.22f) else Frame(w * 0.5f, h * 0.5f, w * 0.28f, -0.07f, 0.22f)

/** Where the headlights are in the truck's coordinates: either side of the grille, [HEADLIGHT_X] out. */
internal const val HEADLIGHT_X = 0.71f
internal const val HEADLIGHT_Y = 0.17f

/** The coverage that reads as dark: paper on a dark theme, ink on a light one. */
private val Page.dark: Float get() = 1f - light

/** The mirrors on each side, the right side's: x, y, half width and half height in the truck's units. */
private val MIRRORS = listOf(floatArrayOf(1.24f, -0.66f, 0.095f, 0.27f), floatArrayOf(1.17f, -0.2f, 0.075f, 0.085f))

/** The truck, back to front: its shadow, tyres and mirrors, the cab, windshield, grille, headlights, bumper and plate. */
private fun Page.truckBody(out: FloatArray, f: Frame) {
    val line = max(1.5f, unit * 2.4f)
    val thin = line * 0.5f

    // Its shadow on the road.
    box(f, -1.35f, 0.82f, 1.35f, 1.2f) { _, _, i, p ->
        val d = (hypot(p[0] / 1.2f, (p[1] - 1.0f) / 0.13f) - 1f) * 0.13f * p[2]
        paint(out, i, inside(d), dark)
    }
    for (side in listOf(-1f, 1f)) {
        // A front tyre under each end of the bumper, its tread in grooves.
        box(f, side * 0.71f - 0.24f, 0.74f, side * 0.71f + 0.24f, 1.15f) { x, y, i, p ->
            val tx = p[0] - side * 0.71f
            val ty = p[1] - 0.95f
            val d = roundBox(tx, ty, 0.2f, 0.17f, 0.07f) * p[2]
            if (d > line) return@box
            var value = shade(x, y, 0.92f)
            val groove = abs(((ty / 0.07f) % 1f + 1f) % 1f - 0.5f) * 0.07f * p[2]
            value += (light - value) * onLine(groove, thin) * inside(d + line * 1.5f)
            paint(out, i, inside(d), value)
            paint(out, i, onLine(d, line), 1f)
        }
        // A mirror on an arm from each upper corner, and a smaller one below it.
        box(f, side * 1.22f - 0.2f, -1.0f, side * 1.22f + 0.2f, -0.1f) { x, y, i, p ->
            val arm = min(
                segment(p[0], p[1], side * 0.9f, -0.98f, side * 1.22f, -0.98f),
                segment(p[0], p[1], side * 0.92f, -0.3f, side * 1.14f, -0.24f),
            ) * p[2] - line * 1.2f
            paint(out, i, inside(arm), 1f)
            for (mirror in MIRRORS) {
                val mx = side * mirror[0]
                val my = mirror[1]
                val d = roundBox(p[0] - mx, p[1] - my, mirror[2], mirror[3], 0.045f) * p[2]
                if (d > line) continue
                var value = shade(x, y, 0.42f)
                val streak = (p[0] - mx) * 0.6f - (p[1] - my) * 0.25f
                value += (light - value) * band(streak * p[2], -0.02f * p[2], 0.01f * p[2])
                paint(out, i, inside(d), value)
                paint(out, i, onLine(d, line), 1f)
            }
        }
    }

    // The cab: a tall box, narrowing a little to its roof, toned, with a light stripe across the front.
    box(f, -1.05f, -1.34f, 1.05f, 0.68f) { x, y, i, p ->
        val t = ((0.62f - p[1]) / 1.87f).coerceIn(0f, 1f)
        val taper = 1f - 0.07f * t
        val by = p[1] + 0.315f
        val d = roundBox(p[0] / taper, by, 0.98f, 0.935f, if (by < 0f) 0.2f else 0.08f) * p[2] * taper
        if (d > line) return@box
        var value = shade(x, y, 0.2f + 0.2f * smooth(-1.2f, 0.62f, p[1]))
        value += (light - value) * band(p[1] * p[2], -0.25f * p[2], -0.08f * p[2])
        paint(out, i, inside(d), value)
        paint(out, i, onLine(d, line), 1f)
        // Three marker lamps on the roof.
        for (k in -1..1) {
            val lamp = roundBox(p[0] - k * 0.2f, p[1] + 1.2f, 0.06f, 0.026f, 0.02f) * p[2]
            paint(out, i, inside(lamp), light)
            paint(out, i, onLine(lamp, thin), 1f)
        }
    }

    // The windshield: dark glass, darker toward the top, two reflections slanting across it, wipers at rest.
    box(f, -0.9f, -1.12f, 0.9f, -0.26f) { x, y, i, p ->
        val t = ((-0.3f - p[1]) / 0.78f).coerceIn(0f, 1f)
        val half = 0.84f - 0.06f * t
        val d = roundBox(p[0] * 0.84f / half, p[1] + 0.69f, 0.84f, 0.39f, 0.08f) * p[2]
        if (d > line) return@box
        var value = shade(x, y, 0.72f + 0.2f * t)
        val s = (0.87f * p[0] + 0.5f * p[1]) * p[2]
        value += (light - value) * (band(s, -0.95f * p[2], -0.83f * p[2]) + band(s, -0.77f * p[2], -0.73f * p[2])).coerceAtMost(1f)
        val wipers = min(
            segment(p[0], p[1], -0.72f, -0.34f, -0.08f, -0.39f),
            segment(p[0], p[1], 0.06f, -0.34f, 0.66f, -0.39f),
        ) * p[2]
        value += (1f - value) * onLine(wipers, line * 0.8f)
        paint(out, i, inside(d), value)
        paint(out, i, onLine(d, line), 1f)
    }

    // The grille: dark, with five slats across it.
    box(f, -0.5f, -0.02f, 0.5f, 0.42f) { x, y, i, p ->
        val gx = p[0]
        val gy = p[1] - 0.2f
        val d = roundBox(gx, gy, 0.46f, 0.19f, 0.04f) * p[2]
        if (d > line) return@box
        var value = shade(x, y, 0.86f)
        for (k in 0 until 5) {
            val slat = roundBox(gx, gy + 0.14f - k * 0.07f, 0.41f, 0.016f, 0.01f) * p[2]
            value += (shade(x, y, 0.08f) - value) * inside(slat)
            value += (1f - value) * onLine(slat, thin)
        }
        paint(out, i, inside(d), value)
        paint(out, i, onLine(d, line), 1f)
    }

    for (side in listOf(-1f, 1f)) {
        // A round headlight in a dark bezel either side of the grille, and an indicator below it.
        val hx = side * HEADLIGHT_X
        box(f, hx - 0.2f, -0.03f, hx + 0.2f, 0.48f) { x, y, i, p ->
            val rho = hypot(p[0] - hx, p[1] - HEADLIGHT_Y)
            val d = (rho - 0.135f) * p[2]
            val lens = (rho - 0.1f) * p[2]
            if (d < line) {
                var value = shade(x, y, 0.55f)
                value += (light - value) * inside(lens)
                value += (1f - value) * onLine(lens, thin)
                paint(out, i, inside(d), value)
                paint(out, i, onLine(d, line), 1f)
            }
            val indicator = roundBox(p[0] - hx, p[1] - 0.41f, 0.12f, 0.045f, 0.02f) * p[2]
            paint(out, i, inside(indicator), shade(x, y, 0.3f))
            paint(out, i, onLine(indicator, thin), 1f)
        }
    }

    // The bumper, a highlight along its top edge, and the plate in the middle.
    box(f, -1.1f, 0.58f, 1.1f, 0.86f) { x, y, i, p ->
        val d = roundBox(p[0], p[1] - 0.72f, 1.05f, 0.115f, 0.03f) * p[2]
        if (d > line) return@box
        var value = shade(x, y, 0.1f + 0.5f * smooth(0.76f, 0.83f, p[1]))
        value += (light - value) * band(p[1] * p[2], 0.625f * p[2], 0.645f * p[2])
        paint(out, i, inside(d), value)
        paint(out, i, onLine(d, line), 1f)
        val plate = roundBox(p[0], p[1] - 0.725f, 0.21f, 0.075f, 0.015f) * p[2]
        paint(out, i, inside(plate), 0f)
        paint(out, i, onLine(plate, thin), 1f)
    }
}

/** The headlights blazing: a glow washing out round each, lines flaring out of it, and a glint over it, long sideways. */
private fun Page.glare(out: FloatArray, f: Frame) {
    val center = FloatArray(2)
    val thin = max(1f, unit * 1.1f)
    for (side in listOf(-1f, 1f)) {
        f.toPage(side * HEADLIGHT_X, HEADLIGHT_Y, center)
        val r = 0.135f * f.pixels(HEADLIGHT_Y)
        val cx = center[0]
        val cy = center[1]
        val flares = List(36) {
            val a = (2 * PI * (it + 0.6f * random.nextFloat()) / 36).toFloat()
            Triple(a, r * (1.5f + 0.3f * random.nextFloat()), r * (2.6f + 1.8f * random.nextFloat()))
        }
        val far = r * 4.6f
        box(cx - far, cy - far, cx + far, cy + far) { x, y, i ->
            val dx = x + 0.5f - cx
            val dy = y + 0.5f - cy
            val rho = hypot(dx, dy)
            paint(out, i, 1f - smooth(r * 1.05f, r * 2.3f, rho), light)
            var ink = 0f
            for ((a, from, to) in flares) {
                if (rho < from - thin || rho > to + thin) continue
                val ca = cos(a)
                val sa = sin(a)
                ink = max(ink, onLine(segment(dx, dy, ca * from, sa * from, ca * to, sa * to), thin))
            }
            paint(out, i, ink, 1f)
        }
        glint(out, cx, cy, r, f.angle, thin)
    }
}

/** A glint around ([cx], [cy]), the light [r] across: an eight-pointed star turned by [angle], its sideways points longest. */
private fun Page.glint(out: FloatArray, cx: Float, cy: Float, r: Float, angle: Float, line: Float) {
    val xs = FloatArray(16)
    val ys = FloatArray(16)
    for (k in 0 until 16) {
        val a = (PI / 8 * k).toFloat() + angle
        val reach = when {
            k % 2 == 1 -> r * 0.55f
            k % 8 == 0 -> r * 7f
            k % 4 == 0 -> r * 2.6f
            else -> r * 1.6f
        }
        xs[k] = cx + cos(a) * reach
        ys[k] = cy + sin(a) * reach
    }
    val cos = cos(angle)
    val sin = sin(angle)
    val far = r * 7.2f
    box(cx - far, cy - far, cx + far, cy + far) { x, y, i ->
        val dx = x + 0.5f - cx
        val dy = y + 0.5f - cy
        // Past the short points only the two long ones reach, and they're narrow.
        if (dx * dx + dy * dy > r * r * 7.3f && abs(dy * cos - dx * sin) > r * 0.5f + line) return@box
        val d = polygon(x + 0.5f, y + 0.5f, xs, ys)
        paint(out, i, inside(d), light)
        paint(out, i, onLine(d, line), 1f)
    }
}

/** ISEKAI on the stripe, 1-UP on the plate, the engine's roar, and the narration. */
private fun Page.truckLettering(out: FloatArray, l: Lettering, f: Frame, wide: Boolean) {
    val at = FloatArray(2)
    fun onTruck(text: String, y: Float, capHeight: Float, value: Float) {
        val px = f.pixels(y)
        val size = capHeight * px / l.capHeight
        f.toPage(0f, y + capHeight / 2f, at)
        val width = l.width(text, size, 0.08f)
        val cos = cos(f.angle)
        val sin = sin(f.angle)
        l.mask(l.line(text, size, at[0] - cos * width / 2f, at[1] - sin * width / 2f, f.angle, 0f, 0.08f))?.let { letters(out, it, value) }
    }
    onTruck("ISEKAI", -0.165f, 0.105f, dark)
    onTruck("1-UP", 0.725f, 0.085f, 1f)

    val line = max(1.5f, unit * 1.8f)
    val ring = max(1f, unit * 1.3f)
    val ink = max(1.5f, unit * 2.2f)
    val shadow = (unit * 3.5f).roundToInt().coerceAtLeast(1)
    val roar = "VRRROOOM!!"
    // On a wide page the roar and the narration stay in the top fifth, which the Library's grid of cards leaves showing.
    val roarBox = if (wide) PageBox(w * 0.17f, h * 0.02f, w * 0.6f, h * 0.215f) else PageBox(w * 0.05f, h * 0.79f, w * 0.95f, h * 0.95f)
    val grow = if (wide) 1.6f else 1.9f
    val angle = if (wide) -0.05f else -0.16f
    // The letters grow along the line, so it sets about (1 + grow) / 2 as wide as at the first letter's size.
    val first = min(l.fit(roar, roarBox.width / ((1f + grow) / 2f) * 0.92f, 0.02f), roarBox.height * 0.5f / (l.capHeight * grow))
    val startY = roarBox.bottom - roarBox.height * 0.2f
    l.mask(l.line(roar, first, roarBox.left, startY, angle, 0.12f, 0.02f, grow), (ring + ink).toInt() + shadow + 3)?.let { mask ->
        logo(out, mask, ring, ink, shadow, shadow, 0.5f) { _, _ -> 1f }
    }

    // The narration, a light novel's title.
    val narration = if (wide) PageBox(w * 0.655f, h * 0.03f, w * 0.97f, h * 0.2f) else PageBox(w * 0.06f, h * 0.045f, w * 0.94f, h * 0.17f)
    caption(out, l, narration, line, listOf("THAT TIME ARES GOT HIT", "BY TRUCK-KUN AND WAS", "REINCARNATED ON ANDROID"))
}
