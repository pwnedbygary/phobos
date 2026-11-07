package com.phobos.emulator.ui.diorama

import kotlin.math.atan
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.tan

/**
 * The hero camera, fitted from the asset's own world bounds so the console + CRT fill the band at every turn angle.
 *
 * The model spins about the footprint origin, so the fit uses the largest horizontal reach of the box (a rotating
 * box sweeps its X/Z diagonal) plus its height: a bounding sphere around the footprint-spanned box can never clip
 * at any angle. The camera looks slightly down at the box centre from +Z, the side the CRT face looks toward.
 */
internal object DioramaFraming {

    /** Vertical field of view; the horizontal one follows the band's aspect. */
    const val FOV_DEGREES = 40.0

    /** A little air around the model, so it never touches the band's edge. */
    const val MARGIN = 1.10

    /** How far above the model's centre the camera sits, as a fraction of the fitted distance. */
    const val ELEVATION = 0.24

    /**
     * The largest distance from the turn axis (+Y through the origin) reached by the box's footprint corners.
     * The model spins, so this - not the static half width - is what the horizontal field must cover at every angle.
     */
    fun sweptRadius(center: FloatArray, halfExtents: FloatArray): Double {
        var radius = 0.0
        for (sx in intArrayOf(-1, 1)) {
            for (sz in intArrayOf(-1, 1)) {
                val x = center[0].toDouble() + sx * halfExtents[0].toDouble()
                val z = center[2].toDouble() + sz * halfExtents[2].toDouble()
                radius = max(radius, hypot(x, z))
            }
        }
        return radius.coerceAtLeast(0.001)
    }

    /**
     * Camera distance that fits the box at [aspect] (width / height), at every turn angle.
     *
     * The model sweeps a cylinder of radius [sweptRadius] around the turn axis, so its nearest surface is only
     * `distance - radius` from the camera: fitting the half height at the *centre* plane (the first attempt) put the
     * near side of the CRT outside the frame and it clipped while turning. The vertical fit therefore adds the swept
     * radius, and the horizontal fit uses the cylinder's tangent. Fitting the box this way - not its bounding
     * sphere - still makes the console + CRT fill the band instead of floating as a small speck.
     */
    fun distance(center: FloatArray, halfExtents: FloatArray, aspect: Double = 1.0): Double {
        val verticalHalf = Math.toRadians(FOV_DEGREES / 2.0)
        val horizontalHalf = atan(tan(verticalHalf) * aspect.coerceAtLeast(1e-3))
        val radius = sweptRadius(center, halfExtents)
        val vertical = halfExtents[1].toDouble().coerceAtLeast(0.001) / tan(verticalHalf) + radius
        val horizontal = radius / tan(horizontalHalf.coerceAtLeast(1e-3))
        return (max(vertical, horizontal) * MARGIN).coerceAtLeast(1.0)
    }

    /**
     * The camera position for a model whose world box centre is [center] and which is [distance] away: straight in
     * front (+Z) and raised by [ELEVATION] * distance, looking down at the centre.
     */
    fun eye(center: FloatArray, distance: Double): DoubleArray = doubleArrayOf(
        center[0].toDouble(),
        center[1].toDouble() + distance * ELEVATION,
        center[2].toDouble() + distance,
    )

    /** Near plane: in front of the model, not clipping the nearest face. */
    fun near(distance: Double): Double = (distance * 0.05).coerceAtLeast(0.01)

    /** Far plane: past the whole model. */
    fun far(distance: Double): Double = distance * 8.0 + 1000.0
}
