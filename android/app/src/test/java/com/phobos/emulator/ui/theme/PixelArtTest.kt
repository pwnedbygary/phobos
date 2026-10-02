package com.phobos.emulator.ui.theme

import com.phobos.emulator.data.PixelBackdropScene
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PixelArtTest {
    private val palette = PixelPalette(
        background = 0xFF101020.toInt(),
        onBackground = 0xFFF0F0F0.toInt(),
        primary = 0xFFC060F0.toInt(),
        secondary = 0xFF60C0A0.toInt(),
        tertiary = 0xFFF0B040.toInt(),
        isDark = true,
    )
    private val spaceColors = PixelSceneColors(
        skyTop = 0xFF0A0A1A.toInt(),
        skyBottom = 0xFF2A1A3A.toInt(),
        star = 0xFFFFFFFF.toInt(),
        planetLit = 0xFFB04020.toInt(),
        planetShade = 0xFF702010.toInt(),
        crater = 0xFF401008.toInt(),
        rim = 0xFFFF9060.toInt(),
        glow = 0xFFD06030.toInt(),
    )

    private fun art(scene: PixelBackdropScene, cols: Int, rows: Int): PixelArt = when (scene) {
        PixelBackdropScene.SPACE -> SpaceArt(cols, rows, spaceColors)
        PixelBackdropScene.NIGHT_DRIVE -> NightDriveArt(cols, rows, palette)
        PixelBackdropScene.PLAINS -> PlainsArt(cols, rows, palette)
        PixelBackdropScene.SAKURA_CYCLE -> SakuraArt(cols, rows, palette, SakuraMode.CYCLE)
        PixelBackdropScene.SAKURA_NIGHT -> SakuraArt(cols, rows, palette, SakuraMode.NIGHT)
        PixelBackdropScene.SAKURA_DAY -> SakuraArt(cols, rows, palette, SakuraMode.DAY)
        PixelBackdropScene.UNDERWATER -> UnderwaterArt(cols, rows, palette)
        PixelBackdropScene.CASTLE -> CastleArt(cols, rows, palette)
    }

    private fun frame(art: PixelArt, time: Long, live: Boolean = true) = IntArray(art.cols * art.rows).also { art.compose(time, it, live) }

    @Test fun everySceneDrawsEveryPixelAtAnySize() {
        val sizes = listOf(1 to 1, 2 to 3, 7 to 5, 40 to 20, 120 to 68, 68 to 120, 480 to 270)
        for (scene in PixelBackdropScene.entries) {
            for ((cols, rows) in sizes) {
                val art = art(scene, cols, rows)
                for (time in listOf(0L, 1_234L, 987_654L, 4_000_000_000L)) {
                    val pixels = frame(art, time)
                    assertTrue("$scene ${cols}x$rows at $time left a pixel undrawn", pixels.all { it ushr 24 == 0xFF })
                    assertTrue("$scene ${cols}x$rows holds ${art.hold(time)} ms at $time", art.hold(time) in 1L..1_000L)
                }
            }
        }
    }

    @Test fun theSameMomentGivesTheSamePictureAndTheStillPictureNeverChanges() {
        for (scene in PixelBackdropScene.entries) {
            assertArrayEquals(scene.name, frame(art(scene, 240, 135), 5_000), frame(art(scene, 240, 135), 5_000))
            val art = art(scene, 240, 135)
            assertArrayEquals(scene.name, frame(art, 0, live = false), frame(art, 77_777, live = false))
        }
    }

    @Test fun everySceneMovesWithinTwoSeconds() {
        for (scene in PixelBackdropScene.entries) {
            val art = art(scene, 240, 135)
            assertFalse("$scene didn't change", frame(art, 10_000).contentEquals(frame(art, 12_000)))
        }
    }

    @Test fun thePlanetTurnsUnderAStillSilhouette() {
        val art = SpaceArt(240, 135, spaceColors)
        val before = frame(art, 0)
        val after = frame(art, 60_000)
        val horizon = (135 * PixelScene.HORIZON).toInt()
        val face = horizon * 240 until before.size
        assertFalse("the craters didn't move", face.all { before[it] == after[it] })
        val rim = { pixels: IntArray -> pixels.indices.filter { pixels[it] == spaceColors.rim }.toSet() }
        assertEquals(rim(before), rim(after))
    }

    @Test fun aShootingStarDrawsEveryFrameAndOnlyOneFliesAtATime() {
        val meteors = Meteors(240, 135, 80, seed = 1977)
        var flights = 0
        var wasFlying = false
        var time = 0L
        while (time < Meteors.WINDOW * 12) {
            val flying = meteors.flying(time)
            if (flying && !wasFlying) flights++
            assertEquals(flying, meteors.untilNext(time) == 0L)
            wasFlying = flying
            time += 10
        }
        assertTrue("$flights shooting stars in 12 windows", flights in 4..12)
        val art = SpaceArt(240, 135, spaceColors)
        val inFlight = (0L until Meteors.WINDOW * 12 step 10).first { meteors.flying(it) }
        assertEquals(PixelArt.FRAME_MS, art.hold(inFlight))
    }

    @Test fun theRoadMovesUnderAStillSky() {
        val art = NightDriveArt(240, 135, palette)
        val a = frame(art, 1_000)
        val b = frame(art, 1_083)
        val horizon = (135 * 0.55f).toInt()
        assertTrue("the sky changed", (0 until horizon * 240).all { a[it] == b[it] })
        assertFalse("the road didn't move", (horizon * 240 until a.size).all { a[it] == b[it] })
        assertTrue(NightDriveArt.CAR.all { it.length == NightDriveArt.CAR[0].length })
    }

    @Test fun theCloudsDriftOverStillGround() {
        val art = PlainsArt(480, 270, palette)
        val a = frame(art, 3_000)
        val b = frame(art, 6_000)
        val ground = (270 - 270 * 0.13f).toInt()
        assertTrue("the ground changed", (ground * 480 until a.size).all { a[it] == b[it] })
        assertFalse("the clouds didn't move", (0 until 270 * 2 / 5 * 480).all { a[it] == b[it] })
    }

    @Test fun petalsFallOnlyWhileAnimating() {
        val art = SakuraArt(240, 135, palette)
        val still = frame(art, 0, live = false)
        val falling = frame(art, 30_000)
        assertFalse(still.contentEquals(falling))
        assertFalse(frame(art, 30_000).contentEquals(frame(art, 30_500)))
    }
}
