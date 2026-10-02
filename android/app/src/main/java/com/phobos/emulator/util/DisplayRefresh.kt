package com.phobos.emulator.util

import android.app.Activity
import android.os.Build
import android.util.Log
import android.view.Surface
import kotlin.math.abs

/**
 * Picks a display mode whose refresh rate is closest to [contentHz] and asks the window to
 * use it. On panels where [Surface.setFrameRate] only registers an override (RP6 reports
 * supportsFrameRateOverrideByContent=false), preferredDisplayModeId is what actually switches
 * 120 Hz → 60 Hz for a 60 Hz game. Cleared with [clearPreferredDisplayMode].
 */
object DisplayRefresh {
    private const val TAG = "PhobosDisplay"

    fun preferContentRate(activity: Activity, contentHz: Double) {
        if (contentHz < 20.0 || contentHz > 240.0) return
        @Suppress("DEPRECATION")
        val display = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            activity.display
        } else {
            activity.windowManager.defaultDisplay
        } ?: return
        val modes = display.supportedModes
        val best = modes.minByOrNull { abs(it.refreshRate - contentHz) } ?: return
        // Don't vote for a mode far from the game (e.g. 75 Hz content on only 60/120).
        if (abs(best.refreshRate - contentHz) > 30.0) return
        val attrs = activity.window.attributes
        if (attrs.preferredDisplayModeId == best.modeId) return
        attrs.preferredDisplayModeId = best.modeId
        activity.window.attributes = attrs
        Log.i(TAG, "preferredDisplayModeId=${best.modeId} (${best.refreshRate} Hz) for content $contentHz Hz")
    }

    fun clearPreferredDisplayMode(activity: Activity) {
        val attrs = activity.window.attributes
        if (attrs.preferredDisplayModeId == 0) return
        attrs.preferredDisplayModeId = 0
        activity.window.attributes = attrs
        Log.i(TAG, "preferredDisplayModeId cleared")
    }

    /** Matches Mupen's SurfaceView vote: DEFAULT compatibility, seamless-or-always. */
    fun setSurfaceFrameRate(surface: Surface, contentHz: Double) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R || contentHz <= 0.0) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            surface.setFrameRate(
                contentHz.toFloat(),
                Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                Surface.CHANGE_FRAME_RATE_ALWAYS,
            )
        } else {
            surface.setFrameRate(contentHz.toFloat(), Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
        }
    }

    fun clearSurfaceFrameRate(surface: Surface) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            surface.setFrameRate(0f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT, Surface.CHANGE_FRAME_RATE_ALWAYS)
        } else {
            surface.setFrameRate(0f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT)
        }
    }
}
