package com.phobos.emulator.ui.touch

import com.phobos.emulator.PhobosCore.Input
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TouchEngineTest {
    private val anywhere = Placement(Anchor.CENTER, 0f, 0f)

    private val dpad = DpadElement(land = anywhere, port = anywhere, size = 200f)
    private val stick = AnalogElement("stick", "Stick", anywhere, anywhere, Stick.LEFT, size = 200f)
    private val face = ButtonCluster(
        "face", "Face", anywhere, anywhere,
        listOf(
            TouchButton("B", Input.B, dx = -40f, dy = 0f, w = 60f),
            TouchButton("A", Input.A, dx = 40f, dy = 0f, w = 60f),
        ),
        multiHit = true,
    )
    private val turbo = ButtonCluster(
        "turbo", "Turbo", anywhere, anywhere, listOf(TouchButton("T", Input.X, w = 40f, toggle = true)),
    )
    private val menu = ButtonCluster(
        "menu", "Menu", anywhere, anywhere, listOf(TouchButton("Menu", w = 40f, action = TouchAction.MENU)),
    )

    /** D-pad at (200,200), stick at (200,600), face at (800,400), toggle at (800,100), menu at (500,50). */
    private fun engine(prefs: TouchPrefs = TouchPrefs()) = TouchEngine().apply {
        this.prefs = prefs
        density = 1f
        setLayout(
            listOf(
                PlacedElement(dpad, 200f, 200f, 1f),
                PlacedElement(stick, 200f, 600f, 1f),
                PlacedElement(face, 800f, 400f, 1f),
                PlacedElement(turbo, 800f, 100f, 1f),
                PlacedElement(menu, 500f, 50f, 1f),
            )
        )
    }

    // ── D-pad geometry ─────────────────────────────────────────────────────

    @Test fun dpadCardinalsFollowScreenAxes() {
        val r = 100f
        assertEquals(TouchEngine.RIGHT, TouchEngine.dpadDirection(80f, 0f, r, DpadMode.EIGHT_WAY, 0.5f, 0))
        assertEquals(TouchEngine.UP, TouchEngine.dpadDirection(0f, -80f, r, DpadMode.EIGHT_WAY, 0.5f, 0))
        assertEquals(TouchEngine.LEFT, TouchEngine.dpadDirection(-80f, 0f, r, DpadMode.EIGHT_WAY, 0.5f, 0))
        assertEquals(TouchEngine.DOWN, TouchEngine.dpadDirection(0f, 80f, r, DpadMode.EIGHT_WAY, 0.5f, 0))
    }

    @Test fun dpadCenterIsDeadZone() {
        assertEquals(0, TouchEngine.dpadDirection(10f, 5f, 100f, DpadMode.EIGHT_WAY, 0.5f, 0))
    }

    @Test fun dpadDiagonalsInEightWayMode() {
        assertEquals(TouchEngine.UP or TouchEngine.RIGHT, TouchEngine.dpadDirection(60f, -60f, 100f, DpadMode.EIGHT_WAY, 0.5f, 0))
        assertEquals(TouchEngine.DOWN or TouchEngine.LEFT, TouchEngine.dpadDirection(-60f, 60f, 100f, DpadMode.EIGHT_WAY, 0.5f, 0))
    }

    @Test fun dpadHysteresisKeepsHeldDirection() {
        // 25 degrees above the right axis: outside the (shrunk) diagonal zone when coming from
        // a cardinal, but inside the (grown) zone while the diagonal is already held.
        val dx = 90.6f
        val dy = -42.3f
        assertEquals(TouchEngine.RIGHT, TouchEngine.dpadDirection(dx, dy, 100f, DpadMode.EIGHT_WAY, 0.5f, TouchEngine.RIGHT))
        val held = TouchEngine.UP or TouchEngine.RIGHT
        assertEquals(held, TouchEngine.dpadDirection(dx, dy, 100f, DpadMode.EIGHT_WAY, 0.5f, held))
    }

    @Test fun fourWayModeNeverReportsDiagonals() {
        val bits = TouchEngine.dpadDirection(80f, -60f, 100f, DpadMode.FOUR_WAY, 0.5f, 0)
        assertEquals(TouchEngine.RIGHT, bits)
    }

    // ── Stick math ─────────────────────────────────────────────────────────

    @Test fun stickReachesFullDeflectionAtTravelRadius() {
        assertArrayEquals(floatArrayOf(1f, 0f), TouchEngine.stickVector(80f, 0f, 100f, 1f, 0f), 1e-4f)
        assertArrayEquals(floatArrayOf(0.5f, 0f), TouchEngine.stickVector(40f, 0f, 100f, 1f, 0f), 1e-4f)
        assertArrayEquals(floatArrayOf(0f, 1f), TouchEngine.stickVector(0f, 500f, 100f, 1f, 0f), 1e-4f)
    }

    @Test fun stickSensitivityShortensTravel() {
        assertArrayEquals(floatArrayOf(1f, 0f), TouchEngine.stickVector(40f, 0f, 100f, 2f, 0f), 1e-4f)
    }

    @Test fun stickDeadZoneRescalesOutput() {
        assertArrayEquals(floatArrayOf(0f, 0f), TouchEngine.stickVector(8f, 0f, 100f, 1f, 0.2f), 1e-4f)
        assertArrayEquals(floatArrayOf(0.375f, 0f), TouchEngine.stickVector(40f, 0f, 100f, 1f, 0.2f), 1e-4f)
    }

    // ── Gestures ───────────────────────────────────────────────────────────

    @Test fun buttonPressAndRelease() {
        val e = engine()
        e.down(1, 840f, 400f, 0)
        assertEquals(Input.A, e.buttonBits)
        assertTrue(e.consumeEvents()!!.haptic)
        e.up(1, 840f, 400f, 50)
        assertEquals(0, e.buttonBits)
    }

    @Test fun fingerSlidesFromBToA() {
        val e = engine()
        e.down(1, 760f, 400f, 0)
        assertEquals(Input.B, e.buttonBits)
        e.move(1, 840f, 400f, 20)
        assertEquals(Input.A, e.buttonBits)
    }

    @Test fun slidingCanBeDisabled() {
        val e = engine(TouchPrefs(slideBetweenButtons = false))
        e.down(1, 760f, 400f, 0)
        e.move(1, 840f, 400f, 20)
        assertEquals(Input.B, e.buttonBits)
    }

    @Test fun gapBetweenNeighboursPressesBoth() {
        val e = engine()
        e.down(1, 800f, 400f, 0)
        assertEquals(Input.A or Input.B, e.buttonBits)
    }

    @Test fun dpadKeepsFingerOutsideItsArea() {
        val e = engine()
        e.down(1, 270f, 200f, 0)
        assertEquals(Input.RIGHT, e.buttonBits)
        e.move(1, 20f, 200f, 20)
        assertEquals(Input.LEFT, e.buttonBits)
    }

    @Test fun twoFingersCombine() {
        val e = engine()
        e.down(1, 270f, 200f, 0)
        e.down(2, 840f, 400f, 0)
        assertEquals(Input.RIGHT or Input.A, e.buttonBits)
        e.up(1, 270f, 200f, 10)
        assertEquals(Input.A, e.buttonBits)
    }

    @Test fun quickTapOnEmptySpaceIsBackgroundTap() {
        val e = engine()
        e.down(1, 500f, 400f, 0)
        e.up(1, 503f, 401f, 100)
        assertTrue(e.consumeEvents()!!.backgroundTap)
    }

    @Test fun longPressOnEmptySpaceIsNotATap() {
        val e = engine()
        e.down(1, 500f, 400f, 0)
        e.up(1, 500f, 400f, 2_000)
        assertNull(e.consumeEvents())
    }

    @Test fun toggleButtonLatches() {
        val e = engine()
        e.down(1, 800f, 100f, 0)
        e.up(1, 800f, 100f, 50)
        assertEquals(Input.X, e.buttonBits)
        e.down(1, 800f, 100f, 100)
        e.up(1, 800f, 100f, 150)
        assertEquals(0, e.buttonBits)
    }

    @Test fun menuFiresOnRelease() {
        val e = engine()
        e.down(1, 500f, 50f, 0)
        assertTrue(e.consumeEvents()!!.actions.isEmpty())
        e.up(1, 500f, 50f, 50)
        assertEquals(listOf(TouchAction.MENU), e.consumeEvents()!!.actions)
        assertEquals(0, e.buttonBits)
    }

    @Test fun stickReportsVectorAndRelease() {
        val e = engine()
        e.down(1, 200f, 600f, 0)
        e.move(1, 240f, 600f, 10)
        val v = e.stick(Stick.LEFT)
        assertNotNull(v)
        assertArrayEquals(floatArrayOf(0.5f, 0f), v!!, 1e-4f)
        e.up(1, 240f, 600f, 10)
        assertNull(e.stick(Stick.LEFT))
        assertEquals(setOf(Stick.LEFT), e.consumeReleasedSticks())
        assertTrue(e.consumeReleasedSticks().isEmpty())
    }

    @Test fun floatingStickCentersOnFirstTouch() {
        val e = engine(TouchPrefs(analogMode = AnalogMode.FLOATING))
        // Inside the floating zone but off the base: the stick starts centered under the thumb.
        e.down(1, 200f, 470f, 0)
        assertArrayEquals(floatArrayOf(0f, 0f), e.stick(Stick.LEFT)!!, 1e-4f)
        e.move(1, 200f, 550f, 10)
        assertArrayEquals(floatArrayOf(0f, 1f), e.stick(Stick.LEFT)!!, 1e-4f)
    }

    @Test fun longPressLatchesWithAutoHold() {
        val e = engine(TouchPrefs(autoHold = true))
        e.down(1, 840f, 400f, 0)
        e.up(1, 840f, 400f, TouchEngine.AUTO_HOLD_MS + 50)
        assertEquals(Input.A, e.buttonBits)
        assertTrue(e.consumeEvents()!!.holdHaptic)
        // Tapping the held button releases it, and that tap doesn't latch again.
        e.down(2, 840f, 400f, 5_000)
        e.up(2, 840f, 400f, 5_000 + TouchEngine.AUTO_HOLD_MS + 50)
        assertEquals(0, e.buttonBits)
    }

    @Test fun shortPressDoesNotLatch() {
        val e = engine(TouchPrefs(autoHold = true))
        e.down(1, 840f, 400f, 0)
        e.up(1, 840f, 400f, 100)
        assertEquals(0, e.buttonBits)
    }

    @Test fun autoHoldIsOffByDefault() {
        val e = engine()
        e.down(1, 840f, 400f, 0)
        e.up(1, 840f, 400f, 5_000)
        assertEquals(0, e.buttonBits)
    }

    @Test fun smallButtonsHaveMinimumTouchTarget() {
        val tiny = ButtonCluster("tiny", "Tiny", anywhere, anywhere, listOf(TouchButton("K", Input.Y, w = 20f)))
        val e = TouchEngine().apply {
            density = 1f
            setLayout(listOf(PlacedElement(tiny, 100f, 100f, 1f)))
        }
        // 20px button (radius 10) still answers 22px from its center (48dp target at density 1).
        e.down(1, 122f, 100f, 0)
        assertEquals(Input.Y, e.buttonBits)
    }

    @Test fun smallMenuButtonOpensWhereverItPresses() {
        val tinyMenu = ButtonCluster(
            "menu", "Menu", anywhere, anywhere, listOf(TouchButton("Menu", w = 20f, action = TouchAction.MENU)),
        )
        val e = TouchEngine().apply {
            density = 1f
            setLayout(listOf(PlacedElement(tinyMenu, 100f, 100f, 1f)))
        }
        // 22px from a 10px-radius button: inside its 48dp target, so lifting there opens the menu.
        e.down(1, 122f, 100f, 0)
        e.up(1, 122f, 100f, 50)
        assertEquals(listOf(TouchAction.MENU), e.consumeEvents()!!.actions)
    }

    @Test fun releaseAllClearsAutoHoldButKeepsToggles() {
        val e = engine(TouchPrefs(autoHold = true))
        e.down(1, 800f, 100f, 0)
        e.up(1, 800f, 100f, 50)
        e.down(2, 840f, 400f, 100)
        e.up(2, 840f, 400f, 100 + TouchEngine.AUTO_HOLD_MS + 50)
        assertEquals(Input.X or Input.A, e.buttonBits)
        e.releaseAll()
        assertEquals(Input.X, e.buttonBits)
    }

    @Test fun relayoutClearsAutoHold() {
        val e = engine(TouchPrefs(autoHold = true))
        e.down(1, 840f, 400f, 0)
        e.up(1, 840f, 400f, TouchEngine.AUTO_HOLD_MS + 50)
        assertEquals(Input.A, e.buttonBits)
        e.setLayout(e.elements.map { PlacedElement(it.element, it.cx, it.cy + 10f, it.unit) })
        assertEquals(0, e.buttonBits)
    }

    @Test fun cancelledGestureHasNoEffects() {
        val e = engine(TouchPrefs(autoHold = true))
        e.down(1, 500f, 50f, 0)
        e.down(2, 840f, 400f, 0)
        e.cancel(1)
        e.cancel(2)
        val events = e.consumeEvents()!!
        assertTrue(events.actions.isEmpty())
        assertEquals(0, e.buttonBits)
    }

    @Test fun slidingDoesNotPressActionButtons() {
        val e = engine()
        e.down(1, 600f, 50f, 0)
        e.move(1, 500f, 50f, 20)
        e.up(1, 500f, 50f, 40)
        val events = e.consumeEvents()
        assertTrue(events == null || events.actions.isEmpty())
    }

    @Test fun releaseAllLiftsEveryFinger() {
        val e = engine()
        e.down(1, 270f, 200f, 0)
        e.down(2, 200f, 600f, 0)
        e.releaseAll()
        assertEquals(0, e.buttonBits)
        assertEquals(setOf(Stick.LEFT), e.consumeReleasedSticks())
    }
}
