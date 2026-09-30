package com.phobos.emulator.ui.hud

import org.junit.Assert.assertEquals
import org.junit.Test

class HudOrderTest {
    @Test
    fun anEmptyOrderIsTheDefault() {
        assertEquals(HudItem.entries, HudItem.parseOrder(""))
    }

    @Test
    fun aSavedOrderRoundTrips() {
        val order = listOf(HudItem.GRAPH, HudItem.FPS, HudItem.FRAME_TIME) + (HudItem.entries - setOf(HudItem.GRAPH, HudItem.FPS, HudItem.FRAME_TIME))
        assertEquals(order, HudItem.parseOrder(HudItem.encodeOrder(order)))
    }

    @Test
    fun unknownNamesAreDroppedAndMissingItemsFollowInDefaultOrder() {
        val order = HudItem.parseOrder("CLOCK,NOT_AN_ITEM,FPS,CLOCK")
        assertEquals(listOf(HudItem.CLOCK, HudItem.FPS), order.take(2))
        assertEquals(HudItem.entries - setOf(HudItem.CLOCK, HudItem.FPS), order.drop(2))
    }

    @Test
    fun movingStopsAtEitherEnd() {
        val order = HudItem.entries
        assertEquals(order, order.moved(HudItem.FPS, -1))
        assertEquals(order, order.moved(HudItem.CLOCK, 1))
        val graphFirst = order.moved(HudItem.GRAPH, -2)
        assertEquals(listOf(HudItem.GRAPH, HudItem.FPS, HudItem.FRAME_TIME), graphFirst.take(3))
        assertEquals(order, graphFirst.moved(HudItem.GRAPH, 2))
    }

    @Test
    fun theCpuRowShowsForItsCoreAndClockAlone() {
        assertEquals(true, HudConfig(cpu = false, cpuDetail = true).shows(HudItem.CPU))
        assertEquals(false, HudConfig(cpu = false, cpuDetail = false).shows(HudItem.CPU))
    }
}
