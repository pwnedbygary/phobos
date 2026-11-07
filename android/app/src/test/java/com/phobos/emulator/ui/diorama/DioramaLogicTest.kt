package com.phobos.emulator.ui.diorama

import com.phobos.emulator.data.EmulatorSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.sin
import kotlin.math.sqrt

/** The hero's pure logic: the system table, the CRT picture choice/fit, the turn and the camera fit. */
class DioramaLogicTest {

    // ---- system -> asset table ----

    @Test
    fun systemTableMapsTheThreeDioramas() {
        val snes = DioramaSystems.recordFor("Super Famicom")
        assertNotNull("Super Famicom has a baked diorama", snes)
        assertEquals("carts/consoles/snes.glb", snes!!.variants.first().asset)

        val nes = DioramaSystems.recordFor("Famicom")
        assertNotNull("Famicom has a baked diorama", nes)
        assertEquals("carts/consoles/nes.glb", nes!!.variants.first().asset)

        val md = DioramaSystems.recordFor("Sega Mega Drive")
        assertNotNull("Mega Drive has a baked diorama", md)
        assertEquals("carts/consoles/megadrive.glb", md!!.variants.first().asset)

        assertNull("a system without a model gets no hero", DioramaSystems.assetFor("PlayStation", null))
    }

    @Test
    fun systemTableIsTheOnlyMappingAndIsExtendable() {
        // The lookup must be the table, not a per-system if-chain: adding a row (or a second variant) is all a new
        // model needs.
        assertTrue(DioramaSystems.systems().containsAll(listOf("Super Famicom", "Famicom", "Sega Mega Drive")))
        assertEquals("carts/consoles/nes.glb", DioramaSystems.assetFor("Famicom", null))
        assertEquals("carts/consoles/snes.glb", DioramaSystems.assetFor("Super Famicom", null))
    }

    // ---- variant resolution ----

    @Test
    fun absentPreferenceIsTheDefaultVariant() {
        val variant = DioramaSystems.variantFor("Super Famicom", null)
        assertNotNull(variant)
        assertEquals("snes", variant!!.id)
        assertEquals("carts/consoles/snes.glb", variant.asset)
        // The first row is the default by definition.
        assertEquals(DioramaSystems.recordFor("Famicom")!!.variants.first(), DioramaSystems.variantFor("Famicom", null))
    }

    @Test
    fun storedVariantIdWins() {
        val variant = DioramaSystems.variantFor("Famicom", "nes")
        assertEquals("nes", variant!!.id)
        assertEquals("carts/consoles/nes.glb", variant.asset)
        assertEquals("carts/consoles/nes.glb", DioramaSystems.assetFor("Famicom", "nes"))
    }

    @Test
    fun unknownVariantIdFallsBackToTheDefault() {
        val variant = DioramaSystems.variantFor("Super Famicom", "hvc-001")
        assertEquals("snes", variant!!.id)
        assertEquals("carts/consoles/snes.glb", DioramaSystems.assetFor("Super Famicom", "hvc-001"))
        // An unknown system stays unknown even with a stored id for it.
        assertNull(DioramaSystems.variantFor("PlayStation", "ps1"))
    }

    // ---- CRT picture source ----

    @Test
    fun boxArtIsPreferredAndNeutralIsTheFallback() {
        assertEquals(DioramaScreenSource.BOX_ART, DioramaScreenArt.choose(hasBoxArt = true))
        assertEquals(DioramaScreenSource.NEUTRAL, DioramaScreenArt.choose(hasBoxArt = false))
    }

    // ---- fitting a cover onto the 4:3 face ----

    @Test
    fun portraitCoverIsFitNotCropped() {
        // 600x900 (2:3 box art) into 512x384: height fills, width is 384 * (600/900) = 256, centred at x = 128.
        // Fitting keeps the whole cover; cropping it to 4:3 would cut off the top and bottom thirds.
        val fit = DioramaScreenArt.fit(600, 900)
        assertEquals(DioramaFit(x = 128, y = 0, width = 256, height = 384), fit)
    }

    @Test
    fun landscapePictureIsLetterboxedVertically() {
        // 512x224 into 512x384: full width, height 224, centred at y = (384-224)/2 = 80.
        val fit = DioramaScreenArt.fit(512, 224)
        assertEquals(DioramaFit(x = 0, y = 80, width = 512, height = 224), fit)
    }

    @Test
    fun exactFourByThreePictureFillsTheFace() {
        assertEquals(DioramaFit(0, 0, 512, 384), DioramaScreenArt.fit(640, 480))
    }

    @Test
    fun squarePictureIsPillarboxed() {
        // 400x400 into 512x384: height fills, width 384, centred at x = 64.
        assertEquals(DioramaFit(x = 64, y = 0, width = 384, height = 384), DioramaScreenArt.fit(400, 400))
    }

    @Test
    fun degenerateSourceYieldsAnEmptyFit() {
        assertEquals(DioramaFit(0, 0, 0, 0), DioramaScreenArt.fit(0, 480))
        assertEquals(DioramaFit(0, 0, 0, 0), DioramaScreenArt.fit(640, 0))
    }

    @Test
    fun fitAlwaysStaysInsideTheFaceAndKeepsTheAspect() {
        for (width in listOf(320, 600, 640, 1920)) {
            for (height in listOf(240, 400, 480, 1080)) {
                val fit = DioramaScreenArt.fit(width, height)
                assertTrue("x in range for ${width}x$height", fit.x >= 0 && fit.x + fit.width <= 512)
                assertTrue("y in range for ${width}x$height", fit.y >= 0 && fit.y + fit.height <= 384)
                // The whole picture is preserved: its aspect is unchanged up to whole-pixel rounding.
                val sourceRatio = width.toDouble() / height
                val fitRatio = fit.width.toDouble() / fit.height
                assertTrue(
                    "aspect kept for ${width}x$height (got $fitRatio vs $sourceRatio)",
                    abs(fitRatio - sourceRatio) / sourceRatio < 0.01,
                )
                // Fit never enlarges past the face (no cropping, no upscale past the target).
                assertTrue("fits the width", fit.width <= 512)
                assertTrue("fits the height", fit.height <= 384)
            }
        }
    }

    // ---- stage gating and still cache ----

    @Test
    fun stageRendersOnlyWhenActiveAndShown() {
        assertTrue(DioramaStagePolicy.shouldRender(active = true, shown = true))
        assertTrue("an active but hidden stage must not draw", !DioramaStagePolicy.shouldRender(active = true, shown = false))
        assertTrue("an inactive stage must not draw", !DioramaStagePolicy.shouldRender(active = false, shown = true))
        assertTrue(!DioramaStagePolicy.shouldRender(active = false, shown = false))
    }

    @Test
    fun stillCacheKeyCoversTheGlbDigestAndThePixelSize() {
        val digest = "0123456789abcdef"
        val a = DioramaStillCache.key(digest, 512, 384)
        assertEquals("same inputs, same key", a, DioramaStillCache.key(digest, 512, 384))
        assertTrue("a different GLB invalidates the picture", a != DioramaStillCache.key("fedcba9876543210", 512, 384))
        assertTrue("a different size invalidates the picture", a != DioramaStillCache.key(digest, 640, 480))
        assertEquals("a cache key is a stable id", 64, a.length)
    }

    // ---- turn: idle, drag, release inertia, wrapping ----

    @Test
    fun idleTurnIsTheDocumentedRate() {
        val turntable = DioramaTurntable()
        turntable.advance(1.0)
        assertEquals(DioramaTurntable.IDLE_RADIANS_PER_SECOND, turntable.angle, 1e-12)
        assertTrue("a second of idle is a fraction of a turn", turntable.angle < 2 * Math.PI)
    }

    @Test
    fun dragTurnsTheModelAndClampsAFlingsVelocity() {
        val turntable = DioramaTurntable()
        turntable.dragStart()
        turntable.drag(dxPixels = 100f, dtSeconds = 1.0 / 60.0)
        assertEquals(100f * DioramaTurntable.RADIANS_PER_PIXEL, turntable.angle, 1e-9)
        assertEquals("a fast flick does not exceed the velocity cap", DioramaTurntable.MAX_VELOCITY, turntable.velocity, 1e-9)
    }

    @Test
    fun releaseInertiaCarriesOnThenSettlesIntoIdle() {
        val turntable = DioramaTurntable()
        turntable.dragStart()
        turntable.drag(dxPixels = 10f, dtSeconds = 0.016)
        val releaseVelocity = turntable.velocity
        assertTrue("the drag left velocity behind", releaseVelocity > 0.0)
        turntable.dragEnd()

        // The first half second after release travels further than idle alone would.
        val before = turntable.angle
        turntable.advance(0.5)
        val coasted = turntable.angle - before
        assertTrue("release inertia adds travel", coasted > DioramaTurntable.IDLE_RADIANS_PER_SECOND * 0.5)
        assertTrue("the inertia decays", turntable.velocity < releaseVelocity)

        // Long after the flick, the model is back to the slow idle turn (0.3 rad per second).
        repeat(50) { turntable.advance(0.1) }
        assertEquals("inertia is gone", 0.0, turntable.velocity, 1e-12)
        val idleStart = turntable.angle
        turntable.advance(1.0)
        assertEquals(DioramaTurntable.IDLE_RADIANS_PER_SECOND, turntable.angle - idleStart, 1e-9)
    }

    @Test
    fun advanceDoesNothingWhileTheFingerIsDown() {
        val turntable = DioramaTurntable()
        turntable.dragStart()
        turntable.drag(10f, 0.016)
        val angle = turntable.angle
        turntable.advance(1.0)
        assertEquals("the idle turn is suspended during a drag", angle, turntable.angle, 1e-12)
    }

    @Test
    fun anglesWrapIntoOneTurn() {
        val turntable = DioramaTurntable()
        turntable.dragStart()
        turntable.drag(dxPixels = 10_000f, dtSeconds = 0.016)
        assertTrue("angle stays in [0, 2*PI)", turntable.angle >= 0.0)
        assertTrue("angle stays in [0, 2*PI)", turntable.angle < 2 * Math.PI)
        assertEquals(2 * Math.PI - 0.5, turntable.wrap(-0.5), 1e-12)
    }

    // ---- pivot/pose ----

    @Test
    fun rotationTurnsAboutTheFootprintOrigin() {
        val identity = DioramaPose.rotationY(0.0)
        assertArrayEquals(floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f), identity)
        // The translation column stays zero: the authored footprint origin (0,0,0) is the pivot.
        assertEquals(0f, identity[12], 0f)
        assertEquals(0f, identity[13], 0f)
        assertEquals(0f, identity[14], 0f)

        // Column-major +90 degrees about Y sends +X to -Z, which is the glTF up axis.
        val quarter = DioramaPose.rotationY(Math.PI / 2.0)
        val x = quarter[0]
        val z = quarter[2]
        assertEquals("cos(90) = 0", 0f, x, 1e-6f)
        assertEquals("-sin(90) = -1", -1f, z, 1e-6f)
        assertEquals("sin(90) = 1", 1f, quarter[8], 1e-6f)
        assertEquals("cos(90) = 0", 0f, quarter[10], 1e-6f)
    }

    @Test
    fun rotationKeepsTheModelRigid() {
        val angle = 0.7
        val m = DioramaPose.rotationY(angle)
        // The three basis columns are unit length and orthogonal: a rotation, not a scale or shear.
        assertEquals(1.0, sqrt((m[0] * m[0] + m[1] * m[1] + m[2] * m[2]).toDouble()), 1e-6)
        assertEquals(1.0, sqrt((m[4] * m[4] + m[5] * m[5] + m[6] * m[6]).toDouble()), 1e-6)
        assertEquals(1.0, sqrt((m[8] * m[8] + m[9] * m[9] + m[10] * m[10]).toDouble()), 1e-6)
        assertEquals(0.0, (m[0] * m[4] + m[1] * m[5] + m[2] * m[6]).toDouble(), 1e-6)
        assertEquals("cos/sin agree with the angle", cos(angle).toFloat(), m[0], 1e-6f)
        assertEquals("cos/sin agree with the angle", sin(angle).toFloat(), m[8], 1e-6f)
    }

    // ---- camera framing ----

    @Test
    fun cameraFitsTheDioramasOwnBoxAtEveryTurnAngle() {
        // Real SNES/NES diorama scale: about 580 x 352 x 688 mm -> half extents in the hundreds, pivot on the floor.
        val center = floatArrayOf(0f, 176f, 0f)
        val half = floatArrayOf(290f, 176f, 344f)
        val aspect = 1.64
        val distance = DioramaFraming.distance(center, half, aspect)
        val verticalHalf = Math.toRadians(DioramaFraming.FOV_DEGREES / 2.0)
        val horizontalHalf = Math.atan(Math.tan(verticalHalf) * aspect)
        // Both fields contain the box at every angle: the near side (distance - swept radius) must still fit the
        // half height, and the spinning footprint must fit the horizontal field.
        val swept = DioramaFraming.sweptRadius(center, half)
        assertTrue("the near-side height fits", Math.atan(half[1] / (distance - swept)) <= verticalHalf)
        assertTrue("the spinning footprint fits", Math.atan(swept / distance) <= horizontalHalf)
        assertTrue("the camera is outside the model", distance > swept)
        // Box fitting is tighter than a bounding-sphere fit would be, so the CRT actually fills the band.
        val sphere = Math.sqrt(
            half[0].toDouble() * half[0] + half[1].toDouble() * half[1] + half[2].toDouble() * half[2],
        )
        assertTrue("box fit is tighter than the sphere fit", distance < sphere / Math.sin(verticalHalf) * DioramaFraming.MARGIN)
    }

    @Test
    fun portraitBandBacksOffFurther() {
        val center = floatArrayOf(0f, 176f, 0f)
        val half = floatArrayOf(290f, 176f, 344f)
        val wide = DioramaFraming.distance(center, half, aspect = 1.8)
        val tall = DioramaFraming.distance(center, half, aspect = 0.7)
        assertTrue("a tighter horizontal field needs more distance", tall > wide)
    }

    @Test
    fun cameraLooksSlightlyDownFromTheFront() {
        val center = floatArrayOf(0f, 176f, 0f)
        val distance = 1200.0
        val eye = DioramaFraming.eye(center, distance)
        assertEquals(center[0].toDouble(), eye[0], 1e-9)
        assertEquals(center[2].toDouble() + distance, eye[2], 1e-9)
        assertTrue("the eye is above the centre", eye[1] > center[1])
        assertTrue("...but slightly, not from overhead", eye[1] - center[1] < distance * 0.5)
        assertTrue("near plane in front", DioramaFraming.near(distance) > 0.0)
        assertTrue("far plane behind the model", DioramaFraming.far(distance) > distance + 1000.0)
    }

    // ---- settings ----

    @Test
    fun dioramaHeroDefaultsToOn() {
        assertTrue("the new dioramas toggle defaults on", EmulatorSettings().dioramas)
        assertTrue("no variant preference means every system's default", EmulatorSettings().dioramaVariant.isEmpty())
        // Keep the neighbouring carts default pinned so the toggle is independent, not accidentally coupled.
        assertTrue(EmulatorSettings().carts)
    }

    private fun assertArrayEquals(expected: FloatArray, actual: FloatArray) {
        assertEquals(expected.size, actual.size)
        for (i in expected.indices) assertEquals("element $i", expected[i], actual[i], 0f)
    }
}
