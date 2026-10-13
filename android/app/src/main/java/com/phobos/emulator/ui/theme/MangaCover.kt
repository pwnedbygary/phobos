package com.phobos.emulator.ui.theme

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Magazine cover: the cover of Weekly Shonen Phobos, an original weekly manga magazine, announcing the
 * app. The masthead, outlined over a toned drop shadow; the issue number and price; a NEW SERIES! banner
 * and the cover lines ("ares, now on Android!", "...and back to Windows and Linux!"); a balloon and a
 * barcode; and in the middle the app icon's moon, cratered and set with a d-pad, bursting out over the
 * curve of Mars (ares, the planet's Greek name) through focus lines. Wide pages get a wide cover, tall
 * pages a tall one.
 */
internal fun Page.cover(out: FloatArray, lettering: Lettering?) {
    val layout = CoverLayout(w, h)
    coverBackground(out, layout)
    debris(out, layout.moonX, layout.moonY, layout.moonR)
    moon(out, layout.moonX, layout.moonY, layout.moonR, layout.moonAngle)
    if (lettering != null) coverLettering(out, lettering, layout)
}

/** Rocks flung out of the burst: angle from the moon, distance and size in its radii. */
private val DEBRIS = floatArrayOf(
    -2.55f, 1.3f, 0.11f,
    -1.95f, 1.36f, 0.065f,
    -0.62f, 1.3f, 0.09f,
    0.32f, 1.26f, 0.055f,
    1.25f, 1.3f, 0.085f,
    2.2f, 1.3f, 0.07f,
    2.95f, 1.4f, 0.05f,
)

/** Rocks flying out from the moon around ([cx], [cy]) ([r] its radius), each lumpy, inked and toned, trailing a few speed lines. */
private fun Page.debris(out: FloatArray, cx: Float, cy: Float, r: Float) {
    val line = max(1f, unit * 1.6f)
    for (k in DEBRIS.indices step 3) {
        val angle = DEBRIS[k]
        val ux = cos(angle)
        val uy = sin(angle)
        val rx = cx + ux * DEBRIS[k + 1] * r
        val ry = cy + uy * DEBRIS[k + 1] * r
        val size = DEBRIS[k + 2] * r
        val trail = size * 2.6f
        box(rx - size * 1.3f - trail, ry - size * 1.3f - trail, rx + size * 1.3f + trail, ry + size * 1.3f + trail) { x, y, i ->
            val dx = x + 0.5f - rx
            val dy = y + 0.5f - ry
            // Speed lines behind it, toward the moon, the middle one longest.
            var streak = 0f
            for (s in -1..1) {
                val ox = -uy * s * size * 0.55f
                val oy = ux * s * size * 0.55f
                val from = size * (1.1f + 0.3f * abs(s))
                val to = from + trail * (1f - 0.3f * abs(s))
                streak = max(streak, onLine(segment(dx, dy, ox - ux * from, oy - uy * from, ox - ux * to, oy - uy * to), line * 0.6f))
            }
            paint(out, i, streak, 1f)
            val rho = hypot(dx, dy)
            val d = rho - size * moonRadius(atan2(dy, dx) + angle * 3f)
            if (d > line) return@box
            val lit = (-dx * 0.6f - dy * 0.8f) / max(1f, rho)
            paint(out, i, inside(d), shade(x, y, 0.15f + 0.55f * smooth(0.2f, -0.4f, lit * rho / size)))
            paint(out, i, onLine(d, line), 1f)
        }
    }
}

/** Where the cover's parts go, in pixels: a wide cover's layout on a landscape page, a tall one's on a portrait page. */
private class CoverLayout(w: Int, h: Int) {
    val wide = w >= h
    private val m = min(w, h) * if (wide) 0.035f else 0.04f
    val moonX = w * 0.5f
    val moonY = h * if (wide) 0.6f else 0.43f
    val moonR = if (wide) h * 0.245f else w * 0.33f
    val moonAngle = -0.32f
    val burstR = moonR * 1.5f

    /** Mars: a circle far bigger than the page, its top just above the page's bottom. */
    val marsR = max(w, h) * 1.6f
    val marsX = w * 0.5f
    val marsY = h * (if (wide) 0.87f else 0.86f) + marsR

    /** On a wide page, the masthead and the issue box stay in the top fifth, which the Library's grid of cards leaves showing. */
    val masthead = if (wide) Box(w * 0.25f, h * 0.018f, w * 0.75f, h * 0.205f) else Box(m, h * 0.018f, w - m, h * 0.148f)
    val issue = if (wide) Box(w - m - h * 0.17f, h * 0.025f, w - m, h * 0.2f) else Box(w - m - w * 0.25f, h * 0.155f, w - m, h * 0.155f + w * 0.29f)
    val banner = if (wide) Box(m, h * 0.33f, w * 0.29f, h * 0.425f) else Box(m, h * 0.165f, w * 0.66f, h * 0.208f)
    val bannerAngle = if (wide) -0.1f else -0.08f
    val headline = if (wide) Box(m, h * 0.45f, w * 0.3f, h * 0.67f) else Box(m, h * 0.635f, w - m, h * 0.72f)
    val caption = if (wide) Box(m * 1.4f, h * 0.7f, w * 0.27f, h * 0.84f) else Box(w * 0.14f, h * 0.735f, w * 0.86f, h * 0.79f)
    val balloonX = if (wide) w * 0.84f else w * 0.79f
    val balloonY = if (wide) h * 0.5f else h * 0.54f
    val balloonR = if (wide) h * 0.135f else w * 0.15f
    val barcode = if (wide) Box(w - m - h * 0.3f, h - m - h * 0.16f, w - m, h - m) else Box(m, h - m - w * 0.17f, m + w * 0.34f, h - m)
}

/** The focus lines converging on the moon, the burst flashing behind it, and Mars' curve across the bottom. */
private fun Page.coverBackground(out: FloatArray, c: CoverLayout) {
    val reach = hypot(max(c.moonX, w - c.moonX), max(c.moonY, h - c.moonY))
    val thickest = unit * 5f
    val rays = rays(300, c.burstR * 0.9f, thickest)
    val angles = FloatArray(rays.size) { rays[it].angle }
    for (y in 0 until h) for (x in 0 until w) out[y * w + x] = rayInk(x, y, c.moonX, c.moonY, rays, angles, reach, thickest) * 0.85f

    val line = max(1.5f, unit * 2f)
    val (edgeAngles, edgeRadii) = burstEdge(26 + random.nextInt(6), c.burstR, 1.32f, 0.86f)
    val far = c.burstR * 1.4f
    box(c.moonX - far, c.moonY - far, c.moonX + far, c.moonY + far) { x, y, i ->
        val d = burstDistance(x, y, c.moonX, c.moonY, edgeAngles, edgeRadii)
        paint(out, i, inside(d), 0f)
        paint(out, i, onLine(d, line), 1f)
    }

    // Mars' surface: tone deepening below its rim, a thin line of sky above it, and craters seen edge on.
    val air = min(w, h) * 0.018f
    val deep = min(w, h) * 0.25f
    val top = c.marsY - c.marsR - air - line
    box(0f, top, w - 1f, h - 1f) { x, y, i ->
        val d = hypot(x + 0.5f - c.marsX, y + 0.5f - c.marsY) - c.marsR
        if (d > air + line) return@box
        paint(out, i, inside(d - air), 0f)
        paint(out, i, onLine(d - air, line * 0.5f), 1f)
        if (d < line) paint(out, i, inside(d), shade(x, y, 0.2f + 0.38f * smooth(0f, deep, -d)))
        paint(out, i, onLine(d, line), 1f)
    }
    val craters = if (c.wide) listOf(-0.22f to 0.03f, -0.1f to 0.075f, 0.06f to 0.035f, 0.17f to 0.09f, 0.27f to 0.05f) else listOf(-0.3f to 0.03f, -0.08f to 0.06f, 0.16f to 0.035f, 0.32f to 0.07f)
    for ((across, down) in craters) {
        val size = min(w, h) * (0.03f + 0.25f * down)
        val angle = across * w / c.marsR
        val nx = sin(angle)
        val ny = -cos(angle)
        val depth = down * min(w, h) * 1.2f
        marsCrater(out, c.marsX + nx * (c.marsR - depth), c.marsY + ny * (c.marsR - depth), size, angle, line * 0.7f)
    }
}

/** A crater on Mars seen nearly edge on: a flat ellipse along the surface at [angle], rimmed in ink, shadowed inside its far wall. */
private fun Page.marsCrater(out: FloatArray, cx: Float, cy: Float, size: Float, angle: Float, line: Float) {
    val cos = cos(angle)
    val sin = sin(angle)
    val flat = 0.32f
    box(cx - size - line, cy - size - line, cx + size + line, cy + size + line) { x, y, i ->
        val dx = x + 0.5f - cx
        val dy = y + 0.5f - cy
        val along = dx * cos + dy * sin
        val across = -dx * sin + dy * cos
        val d = (hypot(along, across / flat) - size) * sqrt(flat)
        val shadowed = (hypot(along, (across - size * flat * 0.45f) / flat) - size) * sqrt(flat)
        if (d < 0f) paint(out, i, inside(d) * (1f - inside(-shadowed)), shade(x, y, 0.7f))
        if (d < 0f) paint(out, i, inside(d) * inside(-shadowed), shade(x, y, 0.12f))
        paint(out, i, onLine(d, line), 1f)
    }
}

/** The moon's craters: x, y and radius in the moon's own units (its long radius 1). */
private val MOON_CRATERS = floatArrayOf(
    -0.62f, -0.36f, 0.18f,
    -0.3f, -0.62f, 0.11f,
    0.4f, -0.48f, 0.17f,
    0.74f, -0.04f, 0.12f,
    0.55f, 0.4f, 0.15f,
    -0.56f, 0.38f, 0.14f,
    -0.88f, 0.08f, 0.07f,
    0.12f, 0.64f, 0.09f,
    0.86f, 0.28f, 0.06f,
    -0.25f, 0.66f, 0.06f,
    0.2f, -0.72f, 0.05f,
)

/** The moon's edge at angle [theta]: an ellipse, its long radius 1, with gentle lumps. */
private fun moonRadius(theta: Float): Float {
    val c = cos(theta)
    val s = sin(theta)
    val ellipse = 1f / sqrt(c * c + s * s / (0.8f * 0.8f))
    return ellipse * (1f + 0.05f * sin(3 * theta + 0.4f) + 0.035f * sin(5 * theta + 1.9f) + 0.02f * sin(9 * theta + 0.7f))
}

/**
 * The app icon's moon around ([cx], [cy]), [r] pixels to its long radius, turned by [angle]: a lumpy rock
 * lit from the upper left, toned darker away from the light with a rim of light on its shadowed edge,
 * cratered, and with a d-pad set into a cross-shaped hollow in its middle.
 */
private fun Page.moon(out: FloatArray, cx: Float, cy: Float, r: Float, angle: Float) {
    val frame = Frame(cx, cy, r, angle)
    val line = max(1.5f, unit * 2.4f)
    val thin = line * 0.55f
    // Toward the light, in the moon's units: from the upper left and in front.
    val lx = -0.5f
    val ly = -0.68f
    val lz = 0.54f
    box(frame, -1.2f, -1.1f, 1.2f, 1.1f) { x, y, i, p ->
        val u = p[0]
        val v = p[1]
        val px = p[2]
        val rho = hypot(u, v)
        val theta = atan2(v, u)
        val edge = moonRadius(theta)
        val d = (rho - edge) * px
        if (d > line) return@box
        val q = (rho / edge).coerceAtMost(1f)
        val nz = sqrt(max(0f, 1f - q * q))
        val lambert = (u / edge * lx + v / edge * ly + nz * lz)
        var value = shade(x, y, 0.1f + 0.5f * smooth(0.5f, 0.05f, lambert))
        // Light caught on the shadowed rim.
        val rim = smooth(-0.5f * line, -1.6f * line, d) * smooth(-3.6f * line, -2.4f * line, d) * smooth(0.25f, -0.05f, lambert)
        value += (light - value) * rim
        for (k in MOON_CRATERS.indices step 3) value = crater(x, y, u, v, px, MOON_CRATERS[k], MOON_CRATERS[k + 1], MOON_CRATERS[k + 2], value, thin)
        value = dpad(x, y, u, v, px, value, thin, line)
        paint(out, i, inside(d), value)
        paint(out, i, onLine(d, line), 1f)
    }
}

/**
 * A crater at ([cx], [cy]), radius [cr], over [under] at moon point ([u], [v]) ([px] pixels to the unit):
 * foreshortened toward the moon's edge, its wall toward the light in shadow, the far wall catching it.
 */
private fun Page.crater(x: Int, y: Int, u: Float, v: Float, px: Float, cx: Float, cy: Float, cr: Float, under: Float, line: Float): Float {
    val ox = u - cx
    val oy = v - cy
    if (abs(ox) > cr * 1.3f || abs(oy) > cr * 1.3f) return under
    val toward = hypot(cx, cy).coerceAtLeast(1e-3f)
    val rx = cx / toward
    val ry = cy / toward
    val q = (toward / moonRadius(atan2(cy, cx))).coerceAtMost(0.95f)
    val squash = sqrt(1f - q * q).coerceAtLeast(0.35f)
    fun distance(dx: Float, dy: Float): Float {
        val radial = (dx * rx + dy * ry) / squash
        val tangent = -dx * ry + dy * rx
        return (hypot(radial, tangent) - cr) * px * sqrt(squash)
    }
    val d = distance(ox, oy)
    var value = under
    if (d < line) {
        // The same crater moved away from the light and toward it: what each leaves uncovered is the wall in shadow, and the lit one.
        val awayFromLight = distance(ox - 0.42f * cr * 0.6f, oy - 0.42f * cr * 0.8f)
        val towardLight = distance(ox + 0.25f * cr * 0.6f, oy + 0.25f * cr * 0.8f)
        val a = inside(d)
        value += (shade(x, y, 0.3f) - value) * a
        value += (shade(x, y, 0.88f) - value) * a * inside(-awayFromLight)
        value += (light - value) * a * inside(-towardLight)
        value += (1f - value) * onLine(d, line)
    }
    return value
}

/** The d-pad over [under] at moon point ([u], [v]): a pad on a cross-shaped hollow, an arrow on each arm, a dimple in the middle. */
private fun Page.dpad(x: Int, y: Int, u: Float, v: Float, px: Float, under: Float, thin: Float, line: Float): Float {
    val cu = u - 0.02f
    val cv = v - 0.02f
    if (abs(cu) > 0.62f || abs(cv) > 0.62f) return under
    fun cross(a: Float, b: Float, length: Float, width: Float, round: Float) = min(roundBox(a, b, length, width, round), roundBox(a, b, width, length, round)) * px
    val hollow = cross(cu, cv, 0.5f, 0.19f, 0.05f)
    var value = under
    value += (shade(x, y, 0.66f) - value) * inside(hollow)
    val pu = cu - 0.018f
    val pv = cv - 0.024f
    val pad = cross(pu, pv, 0.42f, 0.13f, 0.035f)
    // The pad's lower right sides turned from the light.
    val lifted = cross(pu + 0.05f, pv + 0.06f, 0.42f, 0.13f, 0.035f)
    value += (shade(x, y, 0.1f) - value) * inside(pad)
    value += (shade(x, y, 0.5f) - value) * inside(pad) * (1f - inside(lifted))
    value += (1f - value) * onLine(pad, thin)
    value += (1f - value) * onLine(hollow, line * 0.8f)
    // An arrow on each arm, pointing out; the dimple.
    for (k in 0 until 4) {
        val ax = if (k == 0) 1f else if (k == 1) -1f else 0f
        val ay = if (k == 2) 1f else if (k == 3) -1f else 0f
        val along = pu * ax + pv * ay - 0.29f
        val side = abs(pu * ay - pv * ax)
        val arrow = max(max(-along, along - 0.075f), side - (0.075f - along) * 0.75f) * px
        value += (1f - value) * inside(arrow)
    }
    val dimple = (hypot(pu, pv) - 0.065f) * px
    value += (shade(x, y, 0.5f) - value) * inside(dimple)
    value += (1f - value) * onLine(dimple, thin)
    return value
}

/** The cover's words: the masthead, the issue box, the banner, the cover lines, the balloon and the barcode. */
private fun Page.coverLettering(out: FloatArray, l: Lettering, c: CoverLayout) {
    val line = max(1.5f, unit * 1.8f)
    masthead(out, l, c.masthead, c.wide)
    issueBox(out, l, c.issue, line)
    banner(out, l, c.banner, c.bannerAngle)
    headline(out, l, c.headline, c.wide)
    caption(out, l, c.caption, line, listOf("...AND BACK TO", "WINDOWS AND LINUX!"))
    balloon(out, l, c.balloonX, c.balloonY, c.balloonR, line)
    barcode(out, l, c.barcode, line)
}

/**
 * WEEKLY SHONEN in paper on an ink tab, and PHOBOS: inked letters cut by two speed stripes, ringed,
 * outlined, over a toned shadow. The tab stands beside the title when [beside], else above it.
 */
private fun Page.masthead(out: FloatArray, l: Lettering, box: Box, beside: Boolean) {
    val ring = max(1f, unit * 1.3f)
    val ink = max(1.5f, unit * 2.4f)
    val shadow = (unit * 4f).roundToInt().coerceAtLeast(1)
    val slant = 0.1f
    val angle = -0.03f
    val outer = (ring + ink) * 2 + shadow
    if (beside) {
        val cap = min(box.height - outer, (box.width - outer) / (l.width("PHOBOS", 1f / l.capHeight, 0.02f) + 1.1f))
        val size = cap / l.capHeight
        val tabWidth = cap * 0.95f
        val width = l.width("PHOBOS", size, 0.02f)
        val left = box.cx - (tabWidth + cap * 0.15f + width) / 2f
        val baseY = box.top + (box.height + cap - shadow) / 2f
        title(out, l, left + tabWidth + cap * 0.15f, baseY, size, ring, ink, shadow, slant, angle)
        tab(out, l, left, baseY - cap * 0.9f, tabWidth, cap * 0.8f, listOf("WEEKLY", "SHONEN"))
        return
    }
    val tabHeight = box.height * 0.2f
    val titleTop = box.top + tabHeight * 1.25f
    val size = min(l.fit("PHOBOS", box.width - outer, 0.02f), (box.bottom - titleTop - outer) / l.capHeight)
    val width = l.width("PHOBOS", size, 0.02f)
    val baseX = box.cx - width / 2f
    val baseY = titleTop + (box.bottom - titleTop + size * l.capHeight - shadow) / 2f
    title(out, l, baseX, baseY, size, ring, ink, shadow, slant, angle)
    val tabText = "WEEKLY SHONEN"
    val tabSize = tabHeight * 0.62f / l.capHeight
    tab(out, l, baseX + size * l.capHeight * 0.05f, box.top, l.width(tabText, tabSize, 0.06f) + tabHeight * 1.2f, tabHeight, listOf(tabText))
}

/** PHOBOS from ([baseX], [baseY]) on its baseline, at [size], as the masthead's title. */
private fun Page.title(out: FloatArray, l: Lettering, baseX: Float, baseY: Float, size: Float, ring: Float, ink: Float, shadow: Int, slant: Float, angle: Float) {
    val cap = size * l.capHeight
    l.mask(l.line("PHOBOS", size, baseX, baseY, angle, slant, 0.02f), (ring + ink).toInt() + shadow + 3)?.let { mask ->
        val sin = sin(angle)
        val cos = cos(angle)
        logo(out, mask, ring, ink, shadow, shadow, 0.55f) { x, y ->
            val up = (x + 0.5f - baseX) * sin - (y + 0.5f - baseY) * cos
            1f - band(up, 0.3f * cap, 0.37f * cap) - band(up, 0.44f * cap, 0.47f * cap)
        }
    }
}

/** A slanted ink tab from ([left], [top]), [width] by [height], [lines] lettered in paper on it. */
private fun Page.tab(out: FloatArray, l: Lettering, left: Float, top: Float, width: Float, height: Float, lines: List<String>) {
    val lean = height * 0.18f
    val xs = floatArrayOf(left + lean, left + width + lean, left + width - lean, left - lean)
    val ys = floatArrayOf(top, top, top + height, top + height)
    box(left - lean - 2, top - 2, left + width + lean + 2, top + height + 2) { x, y, i ->
        paint(out, i, inside(polygon(x + 0.5f, y + 0.5f, xs, ys)), 1f)
    }
    val pad = height * 0.16f / lines.size
    val size = min(lines.minOf { l.fit(it, width - lean * 2 - pad * 2, 0.06f) }, (height - pad * 2) / (l.capHeight * (lines.size + 0.35f * (lines.size - 1))))
    l.mask(l.block(lines, lines.map { size }, left + width / 2f, top + height / 2f, 0f, 0.3f, gap = 0.35f, tracking = 0.06f))?.let { letters(out, it, 0f) }
}

/** The issue box: the year, the issue's number, a rule, and the price. */
private fun Page.issueBox(out: FloatArray, l: Lettering, box: Box, line: Float) {
    panel(out, box, line)
    val pad = box.width * 0.1f
    val inner = box.width - pad * 2
    val year = min(l.fit("2026", inner * 0.6f), box.height * 0.13f / l.capHeight)
    val number = min(l.fit("No.42", inner), box.height * 0.36f / l.capHeight)
    val price = min(l.fit("PRICE ¥0", inner), box.height * 0.14f / l.capHeight)
    val rule = box.top + box.height * 0.7f
    l.mask(l.block(listOf("2026", "No.42"), listOf(year, number), box.cx, box.top + box.height * 0.37f, gap = 0.22f))?.let { letters(out, it, 1f) }
    box(box.left + pad, rule - line / 2f, box.right - pad, rule + line / 2f) { _, y, i -> paint(out, i, onLine(y + 0.5f - rule, line * 0.6f), 1f) }
    l.mask(l.line("PRICE ¥0", price, box.cx - l.width("PRICE ¥0", price) / 2f, box.bottom - box.height * 0.1f))?.let { letters(out, it, 1f) }
}

/** NEW SERIES! in paper on an ink ribbon with swallowtail ends, a sparkle at each end. */
private fun Page.banner(out: FloatArray, l: Lettering, box: Box, angle: Float) {
    val text = "NEW SERIES!"
    val height = box.height * 0.78f
    val notch = height * 0.35f
    val size = min(l.fit(text, box.width - notch * 4f, 0.03f), height * 0.6f / l.capHeight)
    val cos = cos(angle)
    val sin = sin(angle)
    val halfW = box.width / 2f
    val halfH = height / 2f
    // The ribbon in its own axes, then turned onto the page.
    val ux = floatArrayOf(-halfW, halfW, halfW - notch, halfW, -halfW, -halfW + notch)
    val uy = floatArrayOf(-halfH, -halfH, 0f, halfH, halfH, 0f)
    val xs = FloatArray(6) { box.cx + ux[it] * cos - uy[it] * sin }
    val ys = FloatArray(6) { box.cy + ux[it] * sin + uy[it] * cos }
    val reach = halfW + halfH
    box(box.cx - reach, box.cy - reach, box.cx + reach, box.cy + reach) { x, y, i ->
        paint(out, i, inside(polygon(x + 0.5f, y + 0.5f, xs, ys)), 1f)
    }
    l.mask(l.block(listOf(text), listOf(size), box.cx, box.cy, angle, 0.12f, tracking = 0.03f))?.let { letters(out, it, 0f) }
    for (side in listOf(-1f, 1f)) {
        val along = side * (halfW - notch * 0.2f)
        val sx = (box.cx + along * cos - (-halfH * 1.55f) * sin).coerceIn(height * 0.5f, w - height * 0.5f)
        val sy = box.cy + along * sin + (-halfH * 1.55f) * cos
        sparkle(out, sx, sy, height * 0.42f, max(1f, unit * 1.2f))
    }
}

/** A four-pointed sparkle around ([cx], [cy]), [r] to its tips: paper inside an ink line. */
private fun Page.sparkle(out: FloatArray, cx: Float, cy: Float, r: Float, line: Float) {
    val xs = FloatArray(8)
    val ys = FloatArray(8)
    for (k in 0 until 8) {
        val a = (PI / 4 * k - PI / 2).toFloat()
        val reach = if (k % 2 == 0) r else r * 0.22f
        xs[k] = cx + cos(a) * reach
        ys[k] = cy + sin(a) * reach
    }
    box(cx - r - line, cy - r - line, cx + r + line, cy + r + line) { x, y, i ->
        val d = polygon(x + 0.5f, y + 0.5f, xs, ys)
        paint(out, i, inside(d), 0f)
        paint(out, i, onLine(d, line), 1f)
    }
}

/** The lead cover line, "ares, now on Android!", in outlined letters. */
private fun Page.headline(out: FloatArray, l: Lettering, box: Box, wide: Boolean) {
    val lines = if (wide) listOf("ares,", "NOW ON ANDROID!") else listOf("ares, NOW ON", "ANDROID!")
    val ring = max(1f, unit * 1.7f)
    val ink = max(1f, unit * 1.5f)
    val shadow = (unit * 2f).roundToInt().coerceAtLeast(1)
    val widest = lines.maxOf { l.width(it, 1f, 0.04f) }
    val size = min((box.width - shadow - (ring + ink) * 2) / widest, box.height / (l.capHeight * (lines.size + 0.34f * (lines.size - 1))))
    val sizes = lines.map { size }
    l.mask(l.block(lines, sizes, box.cx, box.cy, -0.04f, 0.08f, gap = 0.34f, tracking = 0.04f, left = if (wide) widest * size else null), (ring + ink).toInt() + shadow + 3)?.let { mask ->
        logo(out, mask, ring, ink, shadow, shadow, 0.3f) { _, _ -> 1f }
    }
}

/** A spiky balloon: 30+ SYSTEMS! */
private fun Page.balloon(out: FloatArray, l: Lettering, cx: Float, cy: Float, r: Float, line: Float) {
    val (angles, radii) = burstEdge(13, r, 1.28f, 0.84f)
    box(cx - r * 1.5f, cy - r * 1.5f, cx + r * 1.5f, cy + r * 1.5f) { x, y, i ->
        val d = burstDistance(x, y, cx, cy, angles, radii)
        paint(out, i, inside(d), 0f)
        paint(out, i, onLine(d, line), 1f)
    }
    val big = min(l.fit("30+", r * 1.05f), r * 0.62f / l.capHeight)
    val small = l.fit("SYSTEMS!", r * 1.35f, 0.02f)
    l.mask(l.block(listOf("30+", "SYSTEMS!"), listOf(big, small), cx, cy, 0.08f, 0.08f, gap = 0.25f, tracking = 0.02f))?.let { letters(out, it, 1f) }
}

/** A barcode in a paper box: the magazine's number as EAN-13 bars, its digits beneath. */
private fun Page.barcode(out: FloatArray, l: Lettering, box: Box, line: Float) {
    panel(out, box, line * 0.8f)
    val digits = "491020261042"
    val code = digits + ean13Check(digits)
    val modules = ean13(code)
    val quiet = box.width * 0.1f
    val module = (box.width - quiet * 2) / modules.size
    val left = box.left + quiet
    val top = box.top + box.height * 0.12f
    val bottom = box.bottom - box.height * 0.3f
    val guardBottom = box.bottom - box.height * 0.2f
    box(left, top, left + module * modules.size, guardBottom) { x, y, i ->
        // The share of the pixel's width each bar covers.
        val from = ((x - left) / module).toInt().coerceAtLeast(0)
        val to = ((x + 1 - left) / module).toInt().coerceAtMost(modules.size - 1)
        var ink = 0f
        for (k in from..to) if (modules[k]) {
            val start = left + k * module
            ink += (min(x + 1f, start + module) - max(x.toFloat(), start)).coerceAtLeast(0f)
        }
        val guard = from < 3 || from in 45..49 || from >= 92
        val low = if (guard) guardBottom else bottom
        paint(out, i, ink.coerceAtMost(1f) * (low - y).coerceIn(0f, 1f), 1f)
    }
    val size = min((guardBottom - bottom) * 2.2f / l.capHeight, module * 6.2f / l.width("0", 1f))
    val baseline = box.bottom - box.height * 0.07f
    l.mask(l.line(code.substring(0, 1), size, left - module * 8f, baseline))?.let { letters(out, it, 1f) }
    l.mask(l.line(code.substring(1, 7), size, left + module * 4f, baseline, tracking = 0.12f))?.let { letters(out, it, 1f) }
    l.mask(l.line(code.substring(7), size, left + module * 50f, baseline, tracking = 0.12f))?.let { letters(out, it, 1f) }
}

/** EAN-13's check digit for its first twelve [digits]. */
internal fun ean13Check(digits: String): Int {
    val sum = digits.withIndex().sumOf { (i, c) -> (c - '0') * if (i % 2 == 0) 1 else 3 }
    return (10 - sum % 10) % 10
}

/** The 95 modules (true for a bar) of the EAN-13 barcode for thirteen [digits]. */
internal fun ean13(digits: String): BooleanArray {
    val odd = intArrayOf(0x0D, 0x19, 0x13, 0x3D, 0x23, 0x31, 0x2F, 0x3B, 0x37, 0x0B)
    val parity = intArrayOf(0x00, 0x0B, 0x0D, 0x0E, 0x13, 0x19, 0x1C, 0x15, 0x16, 0x1A)
    val bits = StringBuilder("101")
    fun seven(pattern: Int) = (6 downTo 0).joinToString("") { if (pattern shr it and 1 == 1) "1" else "0" }
    val first = digits[0] - '0'
    for (k in 1..6) {
        val d = digits[k] - '0'
        val even = parity[first] shr (6 - k) and 1 == 1
        // An even-parity digit is its right-hand pattern reversed, which is its odd one inverted and reversed.
        bits.append(if (even) seven(odd[d] xor 0x7F).reversed() else seven(odd[d]))
    }
    bits.append("01010")
    for (k in 7..12) bits.append(seven(odd[digits[k] - '0'] xor 0x7F))
    bits.append("101")
    return BooleanArray(bits.length) { bits[it] == '1' }
}
