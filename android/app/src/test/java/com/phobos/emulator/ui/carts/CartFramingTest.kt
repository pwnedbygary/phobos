package com.phobos.emulator.ui.carts

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** The camera framing both renderers share: it must scale with the shell, not be a fixed distance. */
class CartFramingTest {

    @Test
    fun distanceScalesWithTheShell() {
        val small = CartFraming.distance(floatArrayOf(24f, 29f, 5f))
        val large = CartFraming.distance(floatArrayOf(75f, 95f, 15f))
        assertTrue("a 190-unit cart needs a longer distance than a 58-unit one", large > small)
        // Bigger shells stay closer to the fitted size than a fixed distance would allow: the ratio follows the boxes.
        assertTrue("distances differ by the shells' size ratio", large / small > 2.5)
    }

    @Test
    fun distanceLeavesRoomForTheWholeShell() {
        val half = floatArrayOf(50f, 50f, 10f)
        val d = CartFraming.distance(half)
        val radius = kotlin.math.sqrt(
            half[0].toDouble() * half[0] + half[1].toDouble() * half[1] + half[2].toDouble() * half[2],
        )
        // The vertical half-angle of the frame at that distance must cover the shell's bounding sphere.
        val halfAngle = kotlin.math.atan((radius / d) * 1.0)
        assertTrue("the shell fits in the vertical field of view", halfAngle < Math.toRadians(CartFraming.FOV_DEGREES / 2.0))
    }

    @Test
    fun nearAndFarBracketTheShell() {
        val d = CartFraming.distance(floatArrayOf(40f, 40f, 10f))
        assertTrue("near plane is in front of the shell", CartFraming.near(d) < d / 4.0)
        assertTrue("far plane is behind the shell", CartFraming.far(d) > d + 500.0)
    }

    @Test
    fun portraitSurfacesBackOffFurther() {
        // The same shell on a square card and on a tall (portrait) surface: the portrait frame is tighter across, so
        // the camera must be further away than the vertical-only fit would put it.
        val half = floatArrayOf(60f, 30f, 10f)
        val square = CartFraming.distance(half, aspect = 1.0)
        val portrait = CartFraming.distance(half, aspect = 0.5)
        val wide = CartFraming.distance(half, aspect = 4.0)
        assertTrue("portrait needs more distance than square", portrait > square)
        assertTrue("a wide live surface is not looser than square", wide <= square)
    }

    @Test
    fun theOrbitPivotsOnTheBoundsCentre() {
        val centre = floatArrayOf(0f, 22f, 14f)
        val d = 200.0
        // At angle 0 the eye sits straight in front of the centre (plus the elevation), i.e. behind the +Z label face.
        val front = CartFraming.orbitEye(centre, d, 0.0)
        assertEquals("front eye x follows the centre", centre[0].toDouble(), front[0], 1e-6)
        assertEquals("front eye y is the centre plus the elevation", centre[1] + d * CartFraming.ELEVATION, front[1], 1e-6)
        assertEquals("front eye z is the centre plus the distance", centre[2] + d, front[2], 1e-6)
        // A half turn looks at the back face from the other side, still centred on the same point.
        val back = CartFraming.orbitEye(centre, d, Math.PI)
        assertEquals(centre[0].toDouble(), back[0], 1e-6)
        assertEquals(centre[2] - d, back[2], 1e-6)
        // The orbit's radius about the centre is the fitted distance (in the XZ plane).
        val side = CartFraming.orbitEye(centre, d, Math.PI / 2)
        assertEquals(centre[0] + d, side[0], 1e-6)
    }

    @Test
    fun nodeTransformsMoveTheWorldBox() {
        // tape.glb's "dark.001" node carries a (0, 0, 8.2) translation while its primitive box is centred on the
        // origin. Unioning the raw object-space boxes (the bug) would report half z 1.1; carrying each box by its
        // entity's world matrix reports the label at z 9.1 and the translated part at 9.2 - half z 5.15.
        val identity = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f)
        val translated = identity.copyOf().also { it[14] = 8.2f }
        val shell = CartFraming.Part(floatArrayOf(0f, 0f, 0f), floatArrayOf(54.8f, 35.3f, 1.1f), identity)
        val dark = CartFraming.Part(floatArrayOf(0f, 0f, 0f), floatArrayOf(54.8f, 35.3f, 1.1f), translated)
        val label = CartFraming.Part(floatArrayOf(0f, 0f, 9.1f), floatArrayOf(52f, 32.5f, 0.05f), identity)
        // What the old code did: ignore the world matrices.
        val raw = CartFraming.Part(floatArrayOf(0f, 0f, 0f), floatArrayOf(54.8f, 35.3f, 1.1f), identity)
        val rawBox = CartFraming.bounds(listOf(raw, raw, raw))!!
        assertEquals("raw object-space union is thin (the bug)", 1.1f, rawBox[5], 0.01f)
        val world = CartFraming.bounds(listOf(shell, dark, label))!!
        assertEquals("the translated part moves the world box centre", 4.1f, world[2], 0.01f)
        assertEquals("the world box spans the shell and the translated node", 5.2f, world[5], 0.01f)
    }

    @Test
    fun theEngineFacingPathAppliesEachEntitysWorldTransform() {
        // This pins the seam that regressed before: the old code unioned the renderables' raw object-space boxes.
        // Entity 1 is the same box translated by (0, 0, 8.2) exactly like tape.glb's dark.001 node. A fake source is
        // enough because the world transform is applied inside bounds(source, entities), not by the caller.
        val identity = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f)
        val translated = identity.copyOf().also { it[14] = 8.2f }
        val box = floatArrayOf(0f, 0f, 0f, 54.8f, 35.3f, 1.1f)
        val source = CartFraming.WorldBoxSource { e ->
            when (e) {
                7 -> CartFraming.Part(box.copyOf(), floatArrayOf(54.8f, 35.3f, 1.1f), identity)
                9 -> CartFraming.Part(box.copyOf(), floatArrayOf(54.8f, 35.3f, 1.1f), translated)
                else -> null
            }
        }
        val b = CartFraming.bounds(source, intArrayOf(7, 9, 12))!!
        assertEquals("raw object-space union would stop at 1.1", 4.1f, b[2], 0.01f)
        assertEquals("translated node widens the span to 8.2 + 2.2", 5.2f, b[5], 0.01f)
    }

    @Test
    fun aRotationGrowsTheAxisAlignedBox() {
        // A 90-degree turn about Y maps the x half-extent onto z (and z onto x): [1,2,3] -> [3,2,1].
        val c = Math.cos(Math.PI / 2).toFloat()
        val s = Math.sin(Math.PI / 2).toFloat()
        val rotY = floatArrayOf(c, 0f, -s, 0f, 0f, 1f, 0f, 0f, s, 0f, c, 0f, 0f, 0f, 0f, 1f)
        val box = CartFraming.transformAabb(floatArrayOf(0f, 0f, 0f), floatArrayOf(1f, 2f, 3f), rotY)
        assertEquals(3f, box[3], 1e-3f)
        assertEquals(2f, box[4], 1e-3f)
        assertEquals(1f, box[5], 1e-3f)
    }

    @Test
    fun aPureTranslationCarriesTheBoxExactly() {
        val m = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 10f, -20f, 30f, 1f)
        val box = CartFraming.transformAabb(floatArrayOf(1f, 2f, 3f), floatArrayOf(4f, 5f, 6f), m)
        assertEquals(11f, box[0], 1e-4f)
        assertEquals(-18f, box[1], 1e-4f)
        assertEquals(33f, box[2], 1e-4f)
        assertEquals(4f, box[3], 1e-4f)
        assertEquals(5f, box[4], 1e-4f)
        assertEquals(6f, box[5], 1e-4f)
    }

    @Test
    fun degenerateBoundsStillFrame() {
        // A zero-size box (an asset whose renderables report no extent) must not produce a zero or NaN distance.
        val d = CartFraming.distance(floatArrayOf(0f, 0f, 0f))
        assertTrue("finite distance", d.isFinite())
        assertEquals("a floor keeps the camera off the shell", 1.0, d, 0.0)
    }
}
