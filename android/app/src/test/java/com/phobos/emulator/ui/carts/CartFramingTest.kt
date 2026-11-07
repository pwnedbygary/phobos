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
    fun degenerateBoundsStillFrame() {
        // A zero-size box (an asset whose renderables report no extent) must not produce a zero or NaN distance.
        val d = CartFraming.distance(floatArrayOf(0f, 0f, 0f))
        assertTrue("finite distance", d.isFinite())
        assertEquals("a floor keeps the camera off the shell", 1.0, d, 0.0)
    }
}
