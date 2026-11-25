package com.phobos.emulator.ui.carts

import android.view.InputDevice
import android.view.MotionEvent
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs

/**
 * The right stick's share of the live cart: while a live cart is on screen, the activity forwards joystick motion
 * here and the renderer orbits the shell. The app's right stick is AXIS_Z/AXIS_RZ on the RP6; AXIS_RX/AXIS_RY is
 * the fallback for pads that report it there. A manual turn stops the slow automatic orbit until another cart is
 * shown, so the user's angle stays put.
 */
object CartStick {

    private const val DEADZONE = 0.15f
    private const val YAW_PER_EVENT = 0.035
    private const val PITCH_PER_EVENT = 0.025

    private val views = AtomicInteger(0)

    /** At least one live cart is composed; only then does the activity consume the stick. */
    val attached: Boolean get() = views.get() > 0

    @Volatile
    var manual = false
        private set

    @Volatile
    var yaw = 0.0
        private set

    @Volatile
    var pitch = 0.0
        private set

    fun attach() {
        views.incrementAndGet()
    }

    fun detach() {
        views.updateAndGet { (it - 1).coerceAtLeast(0) }
    }

    /** A new shell starts at its front and turns by itself again. */
    fun reset() {
        manual = false
        yaw = 0.0
        pitch = 0.0
    }

    /** The renderer's current automatic angle, added once when the user first takes the stick. */
    fun addYaw(angle: Double) {
        yaw += angle
    }

    /**
     * Applies one joystick event. Returns false for a centred stick or a non-joystick event, so the activity can
     * leave it to the rest of the input path; a real turn is consumed and the renderer reads the new yaw/pitch.
     */
    fun onMotion(event: MotionEvent): Boolean {
        if ((event.source and InputDevice.SOURCE_JOYSTICK) == 0 || event.action != MotionEvent.ACTION_MOVE) return false
        val z = event.getAxisValue(MotionEvent.AXIS_Z)
        val rz = event.getAxisValue(MotionEvent.AXIS_RZ)
        val rx = event.getAxisValue(MotionEvent.AXIS_RX)
        val ry = event.getAxisValue(MotionEvent.AXIS_RY)
        val x = if (abs(z) >= abs(rx)) z else rx
        val y = if (abs(rz) >= abs(ry)) rz else ry
        if (abs(x) < DEADZONE && abs(y) < DEADZONE) return false
        manual = true
        yaw += x * YAW_PER_EVENT
        pitch = (pitch + y * PITCH_PER_EVENT).coerceIn(-0.45, 0.55)
        return true
    }
}
