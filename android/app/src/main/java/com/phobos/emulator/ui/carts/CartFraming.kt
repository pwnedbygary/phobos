package com.phobos.emulator.ui.carts

import com.google.android.filament.Engine
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * The camera framing the live view and the offscreen card renderer share.
 *
 * The baked shells differ a lot in size (a Game Boy cart is 48 mm tall, an AES cart 190), so a fixed distance - the
 * 420 units both renderers started with - framed the small carts as a speck and cropped the big ones. The distance
 * comes from the shell's own world bounding box instead (the renderables' bounds include the glTF node transforms),
 * and the camera looks slightly down at the box's centre and orbits it about the glTF up axis (Y).
 */
internal object CartFraming {

    /** Vertical field of view of both cameras; the live one derives its horizontal FOV from the surface's aspect. */
    const val FOV_DEGREES = 45.0

    /** A little air around the shell, so it never touches the frame's edge. */
    private const val MARGIN = 1.12

    /** How far above the shell's centre the camera sits, as a fraction of [distance]: a slight three-quarter view. */
    const val ELEVATION = 0.30

    /**
     * The world bounding box over [entities]' renderables as `[centre x, y, z, half x, y, z]`, or null when nothing in
     * [entities] is renderable (an asset that failed to load).
     */
    fun bounds(engine: Engine, entities: IntArray): FloatArray? {
        val rm = engine.renderableManager
        var minX = Float.MAX_VALUE
        var minY = Float.MAX_VALUE
        var minZ = Float.MAX_VALUE
        var maxX = -Float.MAX_VALUE
        var maxY = -Float.MAX_VALUE
        var maxZ = -Float.MAX_VALUE
        var any = false
        for (e in entities) {
            if (!rm.hasComponent(e)) continue
            val box = runCatching { rm.getAxisAlignedBoundingBox(rm.getInstance(e)) }.getOrNull() ?: continue
            val c = box.center
            val h = box.halfExtent
            minX = minOf(minX, c[0] - h[0])
            maxX = maxOf(maxX, c[0] + h[0])
            minY = minOf(minY, c[1] - h[1])
            maxY = maxOf(maxY, c[1] + h[1])
            minZ = minOf(minZ, c[2] - h[2])
            maxZ = maxOf(maxZ, c[2] + h[2])
            any = true
        }
        if (!any) return null
        return floatArrayOf(
            (minX + maxX) / 2f, (minY + maxY) / 2f, (minZ + maxZ) / 2f,
            (maxX - minX) / 2f, (maxY - minY) / 2f, (maxZ - minZ) / 2f,
        )
    }

    /** Distance that fits a bounding sphere of [halfExtents] into a vertical [FOV_DEGREES] view. */
    fun distance(halfExtents: FloatArray): Double {
        val radius = sqrt(
            halfExtents[0].toDouble() * halfExtents[0] + halfExtents[1].toDouble() * halfExtents[1] +
                halfExtents[2].toDouble() * halfExtents[2],
        )
        // A degenerate (empty) box must not put the camera inside the shell.
        return (radius / sin(Math.toRadians(FOV_DEGREES / 2.0)) * MARGIN).coerceAtLeast(1.0)
    }

    /** Near plane for a camera [distance] away: small enough for the nearest face, far out of the way. */
    fun near(distance: Double): Double = (distance * 0.05).coerceAtLeast(0.01)

    /** Far plane for a camera [distance] away: past the whole shell and its skybox. */
    fun far(distance: Double): Double = distance * 8.0 + 1000.0
}
