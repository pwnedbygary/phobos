package com.phobos.emulator.ui.carts

import com.google.android.filament.Engine
import kotlin.math.atan
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.math.tan

/**
 * The camera framing the live view and the offscreen card renderer share.
 *
 * The baked shells differ a lot in size (a Game Boy cart is 48 units tall, an AES cart 190), so a fixed distance - the
 * 420 units both renderers started with - framed the small carts as a speck and cropped the big ones. The distance
 * comes from the shell's world bounding box instead, and the camera looks slightly down at the box's centre and orbits
 * it about the glTF up axis (Y).
 *
 * World means world: [com.google.android.filament.RenderableManager.getAxisAlignedBoundingBox] is an *object-space*
 * box (gltfio passes the primitive's box through unchanged), so a node transform - `tape.glb`'s `dark.001` carries a
 * (0, 0, 8.2) translation - only shows up when the box is transformed by the entity's
 * [com.google.android.filament.TransformManager] world matrix. [bounds] does exactly that, and the two-node case in
 * CartFramingTest pins it (the union of the raw boxes is the bug being avoided).
 */
internal object CartFraming {

    /** Vertical field of view of both cameras; the live one derives its horizontal FOV from the surface's aspect. */
    const val FOV_DEGREES = 45.0

    /** A little air around the shell, so it never touches the frame's edge. */
    private const val MARGIN = 1.12

    /** How far above the shell's centre the camera sits, as a fraction of [distance]: a slight three-quarter view. */
    const val ELEVATION = 0.30

    /** One renderable: its object-space AABB and the world matrix of the entity that carries it (column-major). */
    internal class Part(val center: FloatArray, val half: FloatArray, val world: FloatArray)

    /**
     * World AABB of an object-space box under [world] (column-major 4x4), as `[centre x, y, z, half x, y, z]`.
     * All eight corners are transformed and re-boxed, so a pure translation moves the box exactly and a rotation
     * grows it to the smallest axis-aligned box that contains it.
     */
    internal fun transformAabb(center: FloatArray, half: FloatArray, world: FloatArray): FloatArray {
        var minX = Float.MAX_VALUE
        var minY = Float.MAX_VALUE
        var minZ = Float.MAX_VALUE
        var maxX = -Float.MAX_VALUE
        var maxY = -Float.MAX_VALUE
        var maxZ = -Float.MAX_VALUE
        for (corner in 0 until 8) {
            val sx = if (corner and 1 == 0) -half[0] else half[0]
            val sy = if (corner and 2 == 0) -half[1] else half[1]
            val sz = if (corner and 4 == 0) -half[2] else half[2]
            val x = center[0] + sx
            val y = center[1] + sy
            val z = center[2] + sz
            val wx = world[0] * x + world[4] * y + world[8] * z + world[12]
            val wy = world[1] * x + world[5] * y + world[9] * z + world[13]
            val wz = world[2] * x + world[6] * y + world[10] * z + world[14]
            minX = minOf(minX, wx)
            maxX = maxOf(maxX, wx)
            minY = minOf(minY, wy)
            maxY = maxOf(maxY, wy)
            minZ = minOf(minZ, wz)
            maxZ = maxOf(maxZ, wz)
        }
        return floatArrayOf(
            (minX + maxX) / 2f, (minY + maxY) / 2f, (minZ + maxZ) / 2f,
            (maxX - minX) / 2f, (maxY - minY) / 2f, (maxZ - minZ) / 2f,
        )
    }

    /** Union of the parts' world AABBs as `[centre x, y, z, half x, y, z]`, or null when there are no parts. */
    internal fun bounds(parts: List<Part>): FloatArray? {
        if (parts.isEmpty()) return null
        var minX = Float.MAX_VALUE
        var minY = Float.MAX_VALUE
        var minZ = Float.MAX_VALUE
        var maxX = -Float.MAX_VALUE
        var maxY = -Float.MAX_VALUE
        var maxZ = -Float.MAX_VALUE
        for (part in parts) {
            val w = transformAabb(part.center, part.half, part.world)
            minX = minOf(minX, w[0] - w[3])
            maxX = maxOf(maxX, w[0] + w[3])
            minY = minOf(minY, w[1] - w[4])
            maxY = maxOf(maxY, w[1] + w[4])
            minZ = minOf(minZ, w[2] - w[5])
            maxZ = maxOf(maxZ, w[2] + w[5])
        }
        return floatArrayOf(
            (minX + maxX) / 2f, (minY + maxY) / 2f, (minZ + maxZ) / 2f,
            (maxX - minX) / 2f, (maxY - minY) / 2f, (maxZ - minZ) / 2f,
        )
    }

    /** The world bounds of the renderables in [entities], each box carried by its entity's world transform. */
    fun bounds(engine: Engine, entities: IntArray): FloatArray? {
        val rm = engine.renderableManager
        val tm = engine.transformManager
        val parts = mutableListOf<Part>()
        for (e in entities) {
            if (!rm.hasComponent(e)) continue
            val box = runCatching { rm.getAxisAlignedBoundingBox(rm.getInstance(e)) }.getOrNull() ?: continue
            val world = runCatching {
                FloatArray(16).also { tm.getWorldTransform(tm.getInstance(e), it) }
            }.getOrNull() ?: continue
            parts += Part(box.center, box.halfExtent, world)
        }
        return bounds(parts)
    }

    /**
     * Distance that fits a bounding sphere of [halfExtents] into the view at [aspect] (width / height). Both the
     * vertical and the horizontal field of view are fitted: a portrait surface (aspect < 1) is tighter across than up,
     * so it needs the camera further away than the vertical-only fit would put it.
     */
    fun distance(halfExtents: FloatArray, aspect: Double = 1.0): Double {
        val radius = sqrt(
            halfExtents[0].toDouble() * halfExtents[0] + halfExtents[1].toDouble() * halfExtents[1] +
                halfExtents[2].toDouble() * halfExtents[2],
        )
        val verticalHalf = Math.toRadians(FOV_DEGREES / 2.0)
        val horizontalHalf = atan(tan(verticalHalf) * aspect.coerceAtLeast(1e-3))
        val tightest = minOf(sin(verticalHalf), sin(horizontalHalf))
        // A degenerate (empty) box must not put the camera inside the shell.
        return (radius / tightest * MARGIN).coerceAtLeast(1.0)
    }

    /** Near plane for a camera [distance] away: small enough for the nearest face, far out of the way. */
    fun near(distance: Double): Double = (distance * 0.05).coerceAtLeast(0.01)

    /** Far plane for a camera [distance] away: past the whole shell and its skybox. */
    fun far(distance: Double): Double = distance * 8.0 + 1000.0

    /**
     * The camera position for an orbit about the shell's centre at [distance]: the live view's eye for an orbit
     * [angle] (radians) about the glTF up axis (Y), raised by [elevation] * distance.
     *
     * Caveat kept explicit: this orbits the bounding box's centre, not the glTF root/pivot. For a shell whose pivot is
     * at the origin but whose content is not centred (a "diorama"), the shell reads centred on screen but it is not
     * rotating about its authored pivot; integration work for those assets is tracked separately.
     */
    fun orbitEye(center: FloatArray, distance: Double, angle: Double, elevation: Double = ELEVATION): DoubleArray =
        doubleArrayOf(
            center[0] + sin(angle) * distance,
            center[1] + distance * elevation,
            center[2] + cos(angle) * distance,
        )
}
