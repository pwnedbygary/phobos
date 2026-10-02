package com.phobos.emulator.ui.hud

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import com.phobos.emulator.data.EmulatorSettings
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.ceil
import kotlin.math.max

class HudPlacementTest {
    private val area = Size(1000f, 500f)
    private val areaRect = Rect(Offset.Zero, area)
    private val min = Size(40f, 20f)

    @Test fun anOlderVersionsDraggedPositionReadsAsItsPlaceOrCustom() {
        assertEquals(HudPosition.TOP_RIGHT, HudPosition.fromSetting("", 1f, 0f))
        assertEquals(HudPosition.TOP_LEFT, HudPosition.fromSetting("", 0f, 0f))
        assertEquals(HudPosition.BOTTOM_CENTER, HudPosition.fromSetting("", 0.5f, 1f))
        assertEquals(HudPosition.CUSTOM, HudPosition.fromSetting("", 0.31f, 0.7f))
        assertEquals(HudPosition.BOTTOM_LEFT, HudPosition.fromSetting("BOTTOM_LEFT", 0.31f, 0.7f))
        assertEquals(HudPosition.TOP_RIGHT, HudPosition.fromSetting("SIDEWAYS", 1f, 0f))
    }

    @Test fun theHorizontalLayoutIsCenteredAndKeepsTheColumnForTheVerticalOne() {
        assertEquals(Offset(0.5f, 1f), HudPosition.BOTTOM_LEFT.fractions(horizontal = true))
        assertEquals(Offset(0f, 1f), HudPosition.BOTTOM_LEFT.fractions(horizontal = false))
        assertNull(HudPosition.CUSTOM.fractions(horizontal = true))
        assertEquals(HudPosition.BOTTOM_LEFT, HudPosition.TOP_LEFT.onEdge(top = false))
        assertEquals(HudPosition.TOP_RIGHT, HudPosition.BOTTOM_RIGHT.onEdge(top = true))
        assertEquals(HudPosition.BOTTOM_CENTER, HudPosition.CUSTOM.onEdge(top = false))
        assertEquals(HudPosition.TOP_CENTER, HudPosition.CUSTOM.onEdge(top = true))
        assertEquals("Bottom", HudPosition.BOTTOM_LEFT.label(horizontal = true))
        assertEquals("Bottom left", HudPosition.BOTTOM_LEFT.label(horizontal = false))
        assertEquals("Custom", HudPosition.CUSTOM.label(horizontal = true))
        assertTrue(HudPosition.TOP_LEFT.centered(horizontal = true))
        assertFalse(HudPosition.TOP_LEFT.centered(horizontal = false))
        assertTrue(HudPosition.TOP_CENTER.centered(horizontal = false))
        assertFalse(HudPosition.CUSTOM.centered(horizontal = true))
    }

    @Test fun placesSitOnTheEdgesOfTheSpaceAboveAKeyboard() {
        val box = Size(200f, 100f)
        assertEquals(Offset(800f, 0f), hudOffset(HudPosition.TOP_RIGHT.fractions(false)!!, box, area))
        assertEquals(Offset(400f, 400f), hudOffset(HudPosition.BOTTOM_CENTER.fractions(false)!!, box, area))
        // With a keyboard taking the lower half, the bottom places sit above it.
        assertEquals(Offset(0f, 150f), hudOffset(HudPosition.BOTTOM_LEFT.fractions(false)!!, box, Size(1000f, 250f)))
        // A box as big as the space has nowhere to go but the corner.
        assertEquals(Offset.Zero, hudOffset(Offset(1f, 1f), area, area))
    }

    @Test fun aMovedPositionRoundTripsThroughItsFractions() {
        val box = Size(200f, 100f)
        val at = Offset(321f, 123f)
        val back = hudOffset(hudFractions(at, box, area), box, area)
        assertEquals(at.x, back.x, 0.01f)
        assertEquals(at.y, back.y, 0.01f)
        assertEquals(Offset.Zero, hudFractions(Offset(5f, 5f), area, area))
    }

    @Test fun savedSizesAndPositionsDecodeOrAreIgnored() {
        assertEquals(HudBoxSize(0.25f, 0.5f), HudBoxSize.decode(HudBoxSize(0.25f, 0.5f).encode()))
        assertNull(HudBoxSize.decode(""))
        assertNull(HudBoxSize.decode("0.5"))
        assertNull(HudBoxSize.decode("0,0.5"))
        assertNull(HudBoxSize.decode("1.5,0.5"))
        assertNull(HudBoxSize.decode("a,b"))
        assertEquals(Offset(0.2f, 0.9f), decodeHudFractions(encodeHudFractions(Offset(0.2f, 0.9f))))
        assertNull(decodeHudFractions("-0.1,0.5"))
        assertNull(decodeHudFractions("NaN,0.5"))
    }

    @Test fun settingsGiveThePlacementWithAnOldDragStandingIn() {
        val fresh = EmulatorSettings().hudPlacement()
        assertEquals(HudPosition.TOP_RIGHT, fresh.position)
        assertNull(fresh.size(landscape = true))
        val old = EmulatorSettings(perfOverlayPosX = 0.3f, perfOverlayPosY = 0.6f).hudPlacement()
        assertEquals(HudPosition.CUSTOM, old.position)
        assertEquals(Offset(0.3f, 0.6f), old.custom(landscape = true))
        assertEquals(Offset(0.3f, 0.6f), old.custom(landscape = false))
        val moved = EmulatorSettings(
            perfOverlayPosX = 0.3f,
            perfOverlayPosY = 0.6f,
            perfHudPosPortrait = "0.1,0.2",
            perfHudSizeLandscape = "0.4,0.3",
        ).hudPlacement()
        assertEquals(HudPosition.CUSTOM, moved.position)
        assertEquals(Offset(0.1f, 0.2f), moved.custom(landscape = false))
        assertEquals(Offset(0.1f, 0.2f), moved.custom(landscape = true))
        assertEquals(HudBoxSize(0.4f, 0.3f), moved.size(landscape = true))
        assertNull(moved.size(landscape = false))
    }

    @Test fun anEditKeepsTheOtherOrientationAndAPlaceForgetsTheMoves() {
        val before = HudPlacement(
            position = HudPosition.CUSTOM,
            customLandscape = Offset(0.3f, 0.6f),
            customPortrait = Offset(0.1f, 0.2f),
            sizeLandscape = HudBoxSize(0.4f, 0.3f),
            sizePortrait = HudBoxSize(0.5f, 0.2f),
            legacy = Offset(0.7f, 0.7f),
        )
        val moved = before.applying(HudEdit(HudPosition.CUSTOM, landscape = false, custom = Offset(0.9f, 0.4f), size = HudBoxSize(0.6f, 0.1f), scale = 1f))
        assertEquals(Offset(0.9f, 0.4f), moved.custom(landscape = false))
        assertEquals(HudBoxSize(0.6f, 0.1f), moved.size(landscape = false))
        assertEquals(before.custom(landscape = true), moved.custom(landscape = true))
        assertEquals(before.size(landscape = true), moved.size(landscape = true))

        val placed = before.applying(HudEdit(HudPosition.BOTTOM_LEFT, landscape = true, custom = null, size = null, scale = 1.5f))
        assertEquals(HudPosition.BOTTOM_LEFT, placed.position)
        assertNull(placed.size(landscape = true))
        assertEquals(before.size(landscape = false), placed.size(landscape = false))
        assertNull(placed.customLandscape)
        assertNull(placed.customPortrait)
        val movedAgain = placed.applying(HudEdit(HudPosition.CUSTOM, landscape = true, custom = Offset(0.2f, 0.8f), size = null, scale = 1f))
        assertEquals(Offset(0.2f, 0.8f), movedAgain.custom(landscape = true))
        assertEquals(Offset(0.2f, 0.8f), movedAgain.custom(landscape = false))
    }

    @Test fun aHandleMovesItsSidesWithinLimits() {
        val box = Rect(400f, 100f, 600f, 200f)
        assertEquals(Rect(350f, 100f, 600f, 200f), resizeHudBox(box, HudHandle.LEFT, Offset(-50f, 30f), min, areaRect, symmetric = false))
        assertEquals(Rect(400f, 100f, 650f, 240f), resizeHudBox(box, HudHandle.BOTTOM_RIGHT, Offset(50f, 40f), min, areaRect, symmetric = false))
        // Not smaller than the minimum, nor off the screen.
        assertEquals(Rect(560f, 100f, 600f, 200f), resizeHudBox(box, HudHandle.LEFT, Offset(500f, 0f), min, areaRect, symmetric = false))
        assertEquals(Rect(400f, 0f, 600f, 200f), resizeHudBox(box, HudHandle.TOP, Offset(0f, -500f), min, areaRect, symmetric = false))
        // A centered box grows on both sides, as far as the nearer edge allows.
        assertEquals(Rect(350f, 100f, 650f, 200f), resizeHudBox(box, HudHandle.RIGHT, Offset(50f, 0f), min, areaRect, symmetric = true))
        assertEquals(Rect(350f, 100f, 650f, 200f), resizeHudBox(box, HudHandle.LEFT, Offset(-50f, 0f), min, areaRect, symmetric = true))
        assertEquals(Rect(0f, 100f, 1000f, 200f), resizeHudBox(box, HudHandle.RIGHT, Offset(900f, 0f), min, areaRect, symmetric = true))
    }

    @Test fun movingAndPinchingKeepTheBoxOnScreen() {
        val box = Rect(400f, 100f, 600f, 200f)
        assertEquals(Rect(800f, 0f, 1000f, 100f), moveHudBox(box, Offset(900f, -900f), areaRect))
        assertSize(Size(400f, 200f), scaleHudBox(box, 2f, min, areaRect).size)
        assertEquals(box.center, scaleHudBox(box, 2f, min, areaRect).center)
        assertSize(Size(1000f, 500f), scaleHudBox(box, 50f, min, areaRect).size)
        assertSize(Size(40f, 20f), scaleHudBox(box, 0.01f, min, areaRect).size)
    }

    private fun assertSize(expected: Size, actual: Size) {
        assertEquals(expected.width, actual.width, 0.01f)
        assertEquals(expected.height, actual.height, 0.01f)
    }

    @Test fun aTouchGrabsTheNearestHandleOrTheBox() {
        val box = Rect(400f, 100f, 600f, 200f)
        assertEquals(HudEditTarget.Handle(HudHandle.TOP_LEFT), hudEditTarget(box, Offset(390f, 95f), 24f))
        assertEquals(HudEditTarget.Handle(HudHandle.RIGHT), hudEditTarget(box, Offset(610f, 150f), 24f))
        assertEquals(HudEditTarget.Box, hudEditTarget(box, Offset(500f, 150f), 24f))
        assertEquals(HudEditTarget.Outside, hudEditTarget(box, Offset(100f, 400f), 24f))
        // A box smaller than the handles' reach still moves from its middle.
        val small = Rect(400f, 100f, 440f, 120f)
        assertEquals(HudEditTarget.Box, hudEditTarget(small, small.center, 24f))
        assertEquals(HudEditTarget.Handle(HudHandle.BOTTOM_RIGHT), hudEditTarget(small, Offset(441f, 121f), 24f))
    }

    @Test fun theTextTakesTheLargestScaleWhoseWrappedLayoutFits() {
        // Stand-in contents: 12 words 50 wide, in lines 20 high, at scale 1.
        val unitHeight = { width: Float -> ceil(12f / max(1, (width / 50f).toInt())) * 20f }
        val scale = fitHudScale(300f, 200f, 0.6f, 2.5f, unitHeight)
        assertTrue(unitHeight(300f / scale) * scale <= 200f)
        assertFalse(unitHeight(300f / (scale + 0.01f)) * (scale + 0.01f) <= 200f)
        assertEquals(2f, scale, 0.01f)
        // Room to spare stops at the largest scale; too little room gives the smallest.
        assertEquals(2.5f, fitHudScale(5000f, 5000f, 0.6f, 2.5f, unitHeight), 0f)
        assertEquals(0.6f, fitHudScale(30f, 10f, 0.6f, 2.5f, unitHeight), 0f)
    }
}
