package com.phobos.emulator.ui.diorama

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.sin

/**
 * The hero's turn, kept as pure state so it is testable off-device.
 *
 * The model spins about the glTF up axis (+Y) through the authored footprint origin - the diorama's footprint is
 * centred on X=Z=0, so rotating the root about Y is a true in-place turntable, not an orbit of the bounding-box
 * centre.
 *
 * Two motions: a slow idle turn, and a horizontal drag that sets the angle and leaves angular velocity behind, which
 * decays after release (release inertia). Angles wrap to [0, 2*PI) so a long session cannot overflow.
 */
internal class DioramaTurntable {

    /** Current turn, radians about +Y. */
    var angle: Double = 0.0
        private set

    /** Angular velocity after the last drag, radians per second; decays to zero after release. */
    var velocity: Double = 0.0
        private set

    /** True between drag start and drag end: the idle turn is suspended while the finger owns the pose. */
    var dragging: Boolean = false
        private set

    fun dragStart() {
        dragging = true
        velocity = 0.0
    }

    /**
     * Applies a horizontal drag of [dxPixels] over [dtSeconds]: the drag moves the model directly, and its speed
     * becomes the release velocity (clamped so a fast flick cannot throw the model into a blur).
     */
    fun drag(dxPixels: Float, dtSeconds: Double) {
        if (!dragging) dragStart()
        val delta = dxPixels.toDouble() * RADIANS_PER_PIXEL
        angle = wrap(angle + delta)
        velocity = if (dtSeconds > MIN_FRAME_SECONDS) {
            (delta / dtSeconds).coerceIn(-MAX_VELOCITY, MAX_VELOCITY)
        } else {
            0.0
        }
    }

    /** Ends the drag; [velocity] stays and carries the release inertia into [advance]. */
    fun dragEnd() {
        dragging = false
    }

    /** One frame of motion: idle turn plus decaying release inertia. No-op while the finger is down. */
    fun advance(dtSeconds: Double) {
        if (dragging || dtSeconds <= 0.0) return
        // The idle turn and the release inertia both apply; a flick briefly dominates and then settles into idle.
        angle = wrap(angle + (IDLE_RADIANS_PER_SECOND + velocity) * dtSeconds)
        velocity *= exp(-dtSeconds / VELOCITY_DECAY_SECONDS)
        if (abs(velocity) < MIN_VELOCITY) velocity = 0.0
    }

    fun wrap(angle: Double): Double {
        val twoPi = 2.0 * Math.PI
        val wrapped = angle % twoPi
        return if (wrapped < 0.0) wrapped + twoPi else wrapped
    }

    companion object {
        /** ~17 degrees per second: a full turn takes ~21 s, slow enough to read the model. */
        const val IDLE_RADIANS_PER_SECOND = 0.30

        /** A drag across the band (about 400 px) turns it roughly half around. */
        const val RADIANS_PER_PIXEL = 0.012

        /** A flick cannot exceed about two turns per second. */
        const val MAX_VELOCITY = 12.0

        /** Release inertia halves in ~0.35 s and is gone in about a second and a half. */
        const val VELOCITY_DECAY_SECONDS = 0.50

        /** Below this the inertia is noise; zero it so the model settles cleanly into the idle turn. */
        const val MIN_VELOCITY = 0.02

        /** Slower drags than this are treated as no velocity (a stalled frame must not fling the model). */
        const val MIN_FRAME_SECONDS = 0.001
    }
}

/**
 * The root transform that turns a diorama about +Y through the origin, as a Filament-style column-major 4x4.
 *
 * The baked models have their footprint centred on X=Z=0 with the base at Y=0, so this is the authored pivot: the
 * translation column stays zero and every point turns in place.
 */
internal object DioramaPose {

    fun rotationY(angleRadians: Double): FloatArray {
        val c = cos(angleRadians).toFloat()
        val s = sin(angleRadians).toFloat()
        return floatArrayOf(
            c, 0f, -s, 0f,
            0f, 1f, 0f, 0f,
            s, 0f, c, 0f,
            0f, 0f, 0f, 1f,
        )
    }
}
