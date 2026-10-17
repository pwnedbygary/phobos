package com.phobos.emulator.util

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * The screenshot effect's motion, as pure functions of the time since the shot (ms): a film-strip card holding the
 * picture pops out of the game picture, hops into a manila folder, sparkles burst and a pill says where it went. The
 * numbers are the owner's reference preview's (scratch/screenshot-fx/preview.html): its layout in hundredths of the
 * screen's width, its keyframes and CSS easings. Nothing here allocates once a timeline is built.
 */

/** CSS's cubic-bezier(x1, y1, x2, y2), solved for x as browsers do; y may overshoot. */
class CssEase(private val x1: Float, private val y1: Float, private val x2: Float, private val y2: Float) {
    fun at(p: Float): Float {
        if (p <= 0f) return 0f
        if (p >= 1f) return 1f
        return curve(solve(p), y1, y2)
    }

    private fun curve(t: Float, a: Float, b: Float): Float {
        val s = 1f - t
        return 3f * s * s * t * a + 3f * s * t * t * b + t * t * t
    }

    private fun slope(t: Float, a: Float, b: Float): Float {
        val s = 1f - t
        return 3f * s * s * a + 6f * s * t * (b - a) + 3f * t * t * (1f - b)
    }

    // Newton's method from t = x; bisection where it stalls (x1 and x2 in [0, 1] keep x monotonic).
    private fun solve(x: Float): Float {
        var t = x
        for (i in 0 until 8) {
            val error = curve(t, x1, x2) - x
            if (abs(error) < 1e-6f) return t
            val d = slope(t, x1, x2)
            if (abs(d) < 1e-6f) break
            t -= error / d
        }
        var lo = 0f
        var hi = 1f
        t = x
        for (i in 0 until 40) {
            val v = curve(t, x1, x2)
            if (abs(v - x) < 1e-6f) return t
            if (v < x) lo = t else hi = t
            t = (lo + hi) / 2f
        }
        return t
    }

    companion object {
        val EASE_OUT = CssEase(0f, 0f, 0.58f, 1f)
        val EASE_IN = CssEase(0.42f, 0f, 1f, 1f)
        val EASE_IN_OUT = CssEase(0.42f, 0f, 0.58f, 1f)
    }
}

/** A keyframe: [value] at [time] (ms); the span from it to the next is eased by [ease] (linear when null). */
class Key(val time: Float, val value: Float, val ease: CssEase? = null)

/** Keyframes as the preview's Web Animations hold them: before the first and after the last, the end values. */
class Track(vararg keys: Key) {
    private val times = FloatArray(keys.size) { keys[it].time }
    private val values = FloatArray(keys.size) { keys[it].value }
    private val eases = Array(keys.size) { keys[it].ease }

    val keyTimes: FloatArray get() = times.copyOf()

    fun at(t: Float): Float {
        if (t <= times[0]) return values[0]
        val last = times.size - 1
        if (t >= times[last]) return values[last]
        var i = 0
        while (t >= times[i + 1]) i++
        val span = times[i + 1] - times[i]
        val p = if (span > 0f) (t - times[i]) / span else 1f
        val eased = eases[i]?.at(p) ?: p
        return values[i] + (values[i + 1] - values[i]) * eased
    }
}

/** A rectangle in pixels. */
data class PxRect(val left: Float, val top: Float, val width: Float, val height: Float) {
    val right get() = left + width
    val bottom get() = top + height
    val centerX get() = left + width / 2f
    val centerY get() = top + height / 2f
}

/** Room kept clear at each edge of the screen, in pixels: the system bars and the camera's cutout. */
data class PxInsets(val left: Float = 0f, val top: Float = 0f, val right: Float = 0f, val bottom: Float = 0f)

/**
 * Where the effect's pieces sit on a [width] x [height] screen, in pixels, in the preview's units of a hundredth of
 * its width ([u]). The card rests in the bottom-left corner, or the top-left one where the touch controls show
 * ([top]: their D-pad has the bottom-left), clear of [inset], with the folder beside it and the pill beside that; it
 * starts over the game [picture] (or, unknown, a picture of [aspect] fitted to the screen), its frame shaped like it.
 */
class ShotGeometry(
    val width: Float,
    val height: Float,
    picture: PxRect?,
    aspect: Float,
    val top: Boolean,
    val inset: PxInsets = PxInsets(),
) {
    val u = width / 100f
    val picture: PxRect = picture?.takeIf { it.width > 0f && it.height > 0f } ?: fitted(if (aspect > 0f) aspect else 480f / 272f)
    private val pictureAspect = this.picture.width / this.picture.height

    /** The picture's place on the film, from its top-left corner: 25u wide (at most 18.75u high), the picture's shape. */
    val frame: PxRect
    val film: PxRect
    /** The bands of sprocket holes above and below the frame. */
    val band = 2.4f * u
    /** The folder's back panel (its tab rises [tabRise] above it), its front panel, and the middle of its mouth. */
    val back: PxRect
    val front: PxRect
    val tabRise = 1.5f * u
    val mouthX: Float
    val mouthY: Float
    /** The pill's left edge and the line its bottom sits on. */
    val pillLeft: Float
    val pillBottom: Float
    /** The CSS perspective the front panel tips in. */
    val perspective = 0.6f * width

    /** The card's offset from where it rests (px) and its scale: at 0 ms, over the game picture. */
    val startX: Float
    val startY: Float
    val startScale: Float
    /**
     * In the folder, sunk between its panels with its top showing: 80% of its width, or narrower for a tall picture
     * (the WonderSwan held upright), whose card would otherwise reach out below the front panel.
     */
    val inX: Float
    val inY: Float
    val inScale: Float
    /** The top of its hop, above the folder. */
    val peakX: Float
    val peakY: Float
    /** One step of the sprocket holes, and how far everything slides off at the end. */
    val pitch = 2.2f * u
    val trayOut: Float

    init {
        var frameWidth = 25f * u
        var frameHeight = frameWidth / pictureAspect
        if (frameHeight > 18.75f * u) {
            frameHeight = 18.75f * u
            frameWidth = frameHeight * pictureAspect
        }
        frame = PxRect(1.3f * u, band, frameWidth, frameHeight)
        val filmWidth = frameWidth + 2.6f * u
        val filmHeight = frameHeight + 2f * band
        // The line the film, the folder and the pill stand on.
        val floor = if (top) inset.top + 3.2f * u + filmHeight else height - inset.bottom - 3.2f * u
        film = PxRect(inset.left + 3f * u, floor - filmHeight, filmWidth, filmHeight)
        val folderLeft = film.right + 3.6f * u
        back = PxRect(folderLeft, floor - 9.4f * u, 12.6f * u, 9.4f * u)
        front = PxRect(folderLeft, floor - 7.8f * u, 12.6f * u, 7.8f * u)
        mouthX = back.centerX
        mouthY = front.top
        pillLeft = back.right + 2.2f * u
        pillBottom = floor - 1.4f * u
        startX = this.picture.centerX - film.centerX
        startY = this.picture.centerY - film.centerY
        startScale = this.picture.width / frameWidth
        // The card's top 45% shows above the front panel, the rest behind it, clear of the bottom.
        inScale = minOf(back.width * 0.8f / filmWidth, (front.height - 0.6f * u) / (filmHeight * 0.55f))
        inX = back.centerX - film.centerX
        inY = front.top + filmHeight * inScale * 0.05f - film.centerY
        peakX = (film.centerX + back.centerX) / 2f - film.centerX
        peakY = back.top - 5f * u - film.centerY
        trayOut = pillLeft + 32f * u
    }

    private fun fitted(aspect: Float): PxRect {
        var w = width
        var h = width / aspect
        if (h > height) {
            h = height
            w = height * aspect
        }
        return PxRect((width - w) / 2f, (height - h) / 2f, w, h)
    }
}

/**
 * The saved shot's effect on [g], 2.7 s; [reduced] (Remove animations) holds the card in the folder with the pill for
 * 1.6 s instead, nothing moving. Each value is the preview's: offsets in pixels, angles in degrees (clockwise for the
 * card and the sparkles), the front panel's tip as CSS's rotateX (negative: its top toward the viewer).
 */
class ShotTimeline(private val g: ShotGeometry, private val reduced: Boolean) {
    val duration = if (reduced) REDUCED_MS else TOTAL_MS

    private val u = g.u
    private val cardX: Track
    private val cardY: Track
    private val cardScale: Track
    private val cardTurn: Track

    init {
        // Each pose: offset x, offset y, scale, turn; the span from it eased by its easing.
        val poses = listOf(
            Pose(0f, g.startX, g.startY, g.startScale, 0f, CssEase(0.25f, 0.8f, 0.3f, 1f)),
            Pose(330f, 0f, 0f, 0.955f, -6f, CssEase.EASE_OUT),
            Pose(470f, 0f, 0f, 1.015f, -2.5f, CssEase.EASE_IN_OUT),
            Pose(600f, 0f, 0f, 1f, -3.5f, null),
            Pose(1060f, 0f, 0f, 1f, -3.5f, CssEase.EASE_OUT),
            Pose(1170f, -0.7f * u, 0.6f * u, 1.035f, -7f, CssEase(0.2f, 0.6f, 0.35f, 1f)),
            Pose(1390f, g.peakX, g.peakY, 0.62f, 9f, CssEase(0.55f, 0f, 0.8f, 0.45f)),
            Pose(1600f, g.inX, g.inY, g.inScale, -2f, CssEase.EASE_OUT),
            Pose(1700f, g.inX, g.inY + 0.7f * u, g.inScale, -2f, CssEase.EASE_IN_OUT),
            Pose(1830f, g.inX, g.inY, g.inScale, -2f, null),
            Pose(TOTAL_MS, g.inX, g.inY, g.inScale, -2f, null),
        )
        cardX = Track(*poses.map { Key(it.time, it.x, it.ease) }.toTypedArray())
        cardY = Track(*poses.map { Key(it.time, it.y, it.ease) }.toTypedArray())
        cardScale = Track(*poses.map { Key(it.time, it.scale, it.ease) }.toTypedArray())
        cardTurn = Track(*poses.map { Key(it.time, it.turn, it.ease) }.toTypedArray())
    }

    private class Pose(val time: Float, val x: Float, val y: Float, val scale: Float, val turn: Float, val ease: CssEase?)

    // The flash on the picture fades as it develops.
    private val develop = Track(Key(0f, 0.7f, CssEase.EASE_OUT), Key(650f, 0f), Key(TOTAL_MS, 0f))

    // The film winds on two sprocket holes, a hole at a time.
    private val wind = CssEase(0.6f, 0f, 0.3f, 1f)
    private val holes = Track(
        Key(0f, 0f), Key(380f, 0f, wind), Key(620f, -g.pitch), Key(740f, -g.pitch, wind), Key(980f, -2f * g.pitch),
        Key(TOTAL_MS, -2f * g.pitch),
    )

    // The folder springs in, then squashes as it shuts.
    private val folderScaleX: Track
    private val folderScaleY: Track
    private val folderAlpha: Track

    init {
        val spring = CssEase(0.3f, 0.9f, 0.4f, 1f)
        fun folder(pick: (Float, Float, Float) -> Float) = Track(
            Key(0f, pick(0f, 0f, 0f)), Key(560f, pick(0f, 0f, 0f), spring), Key(740f, pick(1.12f, 1.12f, 1f)),
            Key(860f, pick(0.96f, 0.96f, 1f), CssEase.EASE_IN_OUT), Key(960f, pick(1f, 1f, 1f)),
            Key(1610f, pick(1f, 1f, 1f), CssEase.EASE_OUT), Key(1690f, pick(1.12f, 0.86f, 1f), CssEase.EASE_IN_OUT),
            Key(1800f, pick(0.95f, 1.05f, 1f), CssEase.EASE_IN_OUT), Key(1910f, pick(1f, 1f, 1f)), Key(TOTAL_MS, pick(1f, 1f, 1f)),
        )
        folderScaleX = folder { x, _, _ -> x }
        folderScaleY = folder { _, y, _ -> y }
        folderAlpha = folder { _, _, a -> a }
    }

    // The front panel tips open toward the viewer, and snaps shut a little past closed.
    private val flap = Track(
        Key(0f, 0f), Key(960f, 0f, CssEase(0.2f, 0.7f, 0.3f, 1f)), Key(1100f, -44f), Key(1570f, -44f, CssEase(0.6f, 0f, 0.9f, 0.5f)),
        Key(1650f, 5f, CssEase.EASE_OUT), Key(1760f, 0f), Key(TOTAL_MS, 0f),
    )

    // Nine sparkles burst from the folder's mouth: a direction (degrees, 0 to the right, negative up) and a reach (u).
    private val sparkDx = FloatArray(SPARKS)
    private val sparkDy = FloatArray(SPARKS)
    private val sparkReach: Array<Track>
    private val sparkScale: Array<Track>
    private val sparkTurn: Array<Track>
    private val sparkAlpha: Array<Track>

    init {
        val fan = floatArrayOf(-150f, 8f, -122f, 10f, -98f, 11f, -75f, 9.6f, -52f, 8.8f, -28f, 7.4f, -172f, 6.4f, -138f, 12f, -62f, 12.5f)
        for (i in 0 until SPARKS) {
            val angle = Math.toRadians(fan[2 * i].toDouble())
            sparkDx[i] = (cos(angle) * fan[2 * i + 1] * u).toFloat()
            sparkDy[i] = (sin(angle) * fan[2 * i + 1] * u).toFloat()
        }
        val burst = CssEase(0.2f, 0.8f, 0.3f, 1f)
        fun spark(i: Int, a: Float, b: Float, c: Float, d: Float, e: Float): Track {
            val start = 1640f + i * 12f
            return Track(Key(0f, a), Key(start, b), Key(start + 20f, c, burst), Key(start + 230f, d, CssEase.EASE_IN), Key(start + 460f, e))
        }
        sparkReach = Array(SPARKS) { spark(it, 0f, 0f, 0f, 0.75f, 1f) }
        sparkScale = Array(SPARKS) { spark(it, 0f, 0.3f, 0.4f, 1f, 0.2f) }
        sparkTurn = Array(SPARKS) { spark(it, 0f, 0f, 0f, 70f, 140f) }
        sparkAlpha = Array(SPARKS) { spark(it, 0f, 0f, 1f, 1f, 0f) }
    }

    // The pill slides out beside the folder with a little overshoot.
    private val pillX: Track
    private val pillScale: Track
    private val pillAlpha: Track

    init {
        val pop = CssEase(0.2f, 0.9f, 0.3f, 1.2f)
        pillX = Track(Key(0f, -2f * u), Key(1720f, -2f * u, pop), Key(1900f, 0.3f * u, CssEase.EASE_OUT), Key(2000f, 0f), Key(TOTAL_MS, 0f))
        pillScale = Track(Key(0f, 0.85f), Key(1720f, 0.85f, pop), Key(1900f, 1.03f, CssEase.EASE_OUT), Key(2000f, 1f), Key(TOTAL_MS, 1f))
        pillAlpha = Track(Key(0f, 0f), Key(1720f, 0f, pop), Key(1900f, 1f, CssEase.EASE_OUT), Key(2000f, 1f), Key(TOTAL_MS, 1f))
    }

    // Then everything slides off to the left and fades.
    private val leave = CssEase(0.55f, 0f, 0.85f, 0.35f)
    private val trayX = Track(Key(0f, 0f), Key(2380f, 0f, leave), Key(TOTAL_MS, -g.trayOut))
    private val trayAlpha = Track(Key(0f, 1f), Key(2380f, 1f, leave), Key(TOTAL_MS, 0f))

    internal val tracks: List<Track>
        get() = listOf(cardX, cardY, cardScale, cardTurn, develop, holes, folderScaleX, folderScaleY, folderAlpha, flap, pillX,
            pillScale, pillAlpha, trayX, trayAlpha) + sparkReach + sparkScale + sparkTurn + sparkAlpha

    fun cardX(t: Float) = if (reduced) g.inX else cardX.at(t)
    fun cardY(t: Float) = if (reduced) g.inY else cardY.at(t)
    fun cardScale(t: Float) = if (reduced) g.inScale else cardScale.at(t)
    fun cardTurn(t: Float) = if (reduced) -2f else cardTurn.at(t)
    fun develop(t: Float) = if (reduced) 0f else develop.at(t)
    fun holes(t: Float) = if (reduced) 0f else holes.at(t)
    fun folderScaleX(t: Float) = if (reduced) 1f else folderScaleX.at(t)
    fun folderScaleY(t: Float) = if (reduced) 1f else folderScaleY.at(t)
    fun folderAlpha(t: Float) = if (reduced) 1f else opacity(folderAlpha.at(t))
    fun flap(t: Float) = if (reduced) 0f else flap.at(t)
    fun sparkX(i: Int, t: Float) = if (reduced) 0f else sparkDx[i] * sparkReach[i].at(t)
    fun sparkY(i: Int, t: Float) = if (reduced) 0f else sparkDy[i] * sparkReach[i].at(t)
    fun sparkScale(i: Int, t: Float) = if (reduced) 0f else sparkScale[i].at(t)
    fun sparkTurn(i: Int, t: Float) = if (reduced) 0f else sparkTurn[i].at(t)
    fun sparkAlpha(i: Int, t: Float) = if (reduced) 0f else opacity(sparkAlpha[i].at(t))
    fun pillX(t: Float) = if (reduced) 0f else pillX.at(t)
    fun pillScale(t: Float) = if (reduced) 1f else pillScale.at(t)
    fun pillAlpha(t: Float) = if (reduced) 1f else opacity(pillAlpha.at(t))
    fun trayX(t: Float) = if (reduced) 0f else trayX.at(t)
    fun trayAlpha(t: Float) = if (reduced) 1f else opacity(trayAlpha.at(t))

    companion object {
        const val TOTAL_MS = 2700f
        const val REDUCED_MS = 1600f
        const val SPARKS = 9
    }
}

/**
 * A shot that wasn't saved: no card or folder, only the pill, error-tinted, in the corner, 3 s; [reduced] holds it
 * still. Offsets in pixels.
 */
class ShotErrorTimeline(g: ShotGeometry, private val reduced: Boolean) {
    val duration = TOTAL_MS

    private val pop = CssEase(0.2f, 0.9f, 0.3f, 1.2f)
    private val leave = CssEase(0.55f, 0f, 0.85f, 0.35f)
    private val pillX = Track(
        Key(0f, -2f * g.u, pop), Key(180f, 0.3f * g.u, CssEase.EASE_OUT), Key(280f, 0f), Key(2700f, 0f, leave), Key(TOTAL_MS, -g.trayOut),
    )
    private val pillScale = Track(Key(0f, 0.85f, pop), Key(180f, 1.03f, CssEase.EASE_OUT), Key(280f, 1f), Key(TOTAL_MS, 1f))
    private val pillAlpha = Track(Key(0f, 0f, pop), Key(180f, 1f), Key(2700f, 1f, leave), Key(TOTAL_MS, 0f))

    internal val tracks: List<Track> get() = listOf(pillX, pillScale, pillAlpha)

    fun pillX(t: Float) = if (reduced) 0f else pillX.at(t)
    fun pillScale(t: Float) = if (reduced) 1f else pillScale.at(t)
    fun pillAlpha(t: Float) = if (reduced) 1f else opacity(pillAlpha.at(t))

    companion object {
        const val TOTAL_MS = 3000f
    }
}

// An overshooting easing can carry an opacity past its ends, which CSS clamps.
private fun opacity(value: Float) = value.coerceIn(0f, 1f)

/**
 * The front panel's corners after CSS's `perspective(p) rotateX(degrees)` about the middle of its bottom edge, as the
 * preview tips it: [out] gets the top-left, top-right, bottom-right and bottom-left corners (x, y pairs) of a [w] x [h]
 * panel. Negative degrees tip its top toward the viewer: nearer, so wider, and lower.
 */
fun flapCorners(degrees: Float, w: Float, h: Float, p: Float, out: FloatArray) {
    val a = Math.toRadians(degrees.toDouble())
    val y = (-h * cos(a)).toFloat()
    // CSS's z, toward the viewer.
    val z = (-h * sin(a)).toFloat()
    val k = p / (p - z)
    val half = w / 2f
    out[0] = half - half * k
    out[1] = h + y * k
    out[2] = half + half * k
    out[3] = h + y * k
    out[4] = w
    out[5] = h
    out[6] = 0f
    out[7] = h
}
