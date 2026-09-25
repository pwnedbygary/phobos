package com.phobos.emulator.ui.touch

import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.HapticFeedbackConstants
import android.view.View
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.changedToDownIgnoreConsumed
import androidx.compose.ui.input.pointer.changedToUpIgnoreConsumed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.rememberTextMeasurer
import com.phobos.emulator.PhobosCore
import com.phobos.emulator.input.GameInputState
import kotlinx.coroutines.delay

/**
 * On-screen controls for one system. A single pointer handler covers the whole overlay so any
 * finger can reach any control (and slide between them); drawing happens in one Canvas that is
 * invalidated per gesture event without recomposition.
 *
 * The landscape or portrait customization and opacity are chosen from the overlay's own size.
 * Touches that land on no control and lift quickly are reported through [onBackgroundTap].
 */
@Composable
fun TouchControlsOverlay(
    layout: TouchLayout,
    landscapeOverrides: Map<String, ElementOverride>,
    portraitOverrides: Map<String, ElementOverride>,
    prefs: TouchPrefs,
    onAction: (TouchAction) -> Unit,
    onBackgroundTap: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val density = LocalDensity.current
    val layoutDirection = LocalLayoutDirection.current
    val engine = remember { TouchEngine() }
    val sink = remember { TouchInputSink() }
    val haptics = remember(view) { TouchHaptics(view) }
    val textMeasurer = rememberTextMeasurer()
    val painter = remember(textMeasurer) { TouchPainter(textMeasurer) }
    var frame by remember { mutableIntStateOf(0) }
    var touching by remember { mutableStateOf(false) }
    var idle by remember { mutableStateOf(false) }
    val currentOnAction by rememberUpdatedState(onAction)
    val currentOnBackgroundTap by rememberUpdatedState(onBackgroundTap)
    val currentPrefs by rememberUpdatedState(prefs)

    SideEffect {
        engine.prefs = prefs
        engine.density = density.density
    }
    DisposableEffect(engine) {
        onDispose {
            engine.releaseAll()
            engine.resetLatches()
            sink.publish(engine)
            sink.flushPendingReleases()
        }
    }

    // Optional idle fade: dim a few seconds after the last finger lifts; any touch restores.
    LaunchedEffect(prefs.idleFade, touching) {
        idle = false
        if (prefs.idleFade && !touching) {
            delay(IDLE_FADE_DELAY_MS)
            idle = true
        }
    }
    val fade by animateFloatAsState(
        targetValue = if (idle) IDLE_FADE_OPACITY else 1f,
        animationSpec = tween(if (idle) 600 else 120),
        label = "touchIdleFade",
    )

    val cutout = WindowInsets.displayCutout
    BoxWithConstraints(modifier.fillMaxSize()) {
        val widthPx = constraints.maxWidth.toFloat()
        val heightPx = constraints.maxHeight.toFloat()
        val landscape = widthPx >= heightPx
        val insets = ScreenInsets(
            left = cutout.getLeft(density, layoutDirection).toFloat(),
            top = cutout.getTop(density).toFloat(),
            right = cutout.getRight(density, layoutDirection).toFloat(),
            bottom = cutout.getBottom(density).toFloat(),
        )
        val overrides = if (landscape) landscapeOverrides else portraitOverrides
        val opacity = if (landscape) prefs.opacity else prefs.opacityPortrait
        val placed = remember(layout, overrides, prefs.scale, prefs.swapHands, widthPx, heightPx, insets, density.density) {
            placeLayout(layout, overrides, widthPx, heightPx, density.density, prefs.scale, insets, mirror = prefs.swapHands)
        }
        SideEffect {
            if (engine.elements !== placed) {
                engine.setLayout(placed)
                sink.publish(engine)
                frame++
            }
        }

        Canvas(
            Modifier
                .fillMaxSize()
                .pointerInput(engine) {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            for (change in event.changes) {
                                val id = change.id.value
                                val p = change.position
                                when {
                                    change.changedToDownIgnoreConsumed() -> engine.down(id, p.x, p.y, change.uptimeMillis)
                                    // Compose delivers a cancelled gesture as an already-consumed lift.
                                    change.changedToUpIgnoreConsumed() ->
                                        if (change.isConsumed) engine.cancel(id) else engine.up(id, p.x, p.y, change.uptimeMillis)
                                    change.pressed && change.positionChanged() -> engine.move(id, p.x, p.y, change.uptimeMillis)
                                }
                                change.consume()
                            }
                            val anyPressed = event.changes.any { it.pressed }
                            if (anyPressed != touching) touching = anyPressed
                            sink.publish(engine)
                            engine.consumeEvents()?.let { events ->
                                if (events.haptic) haptics.press(currentPrefs.haptics)
                                if (events.holdHaptic) haptics.hold(currentPrefs.haptics)
                                events.actions.forEach { currentOnAction(it) }
                                if (events.backgroundTap) currentOnBackgroundTap()
                            }
                            frame++
                        }
                    }
                }
        ) {
            if (frame < 0) return@Canvas // Reading `frame` subscribes this draw to gesture updates.
            val alpha = opacity * fade
            for (element in placed) with(painter) { drawElement(element, engine, alpha) }
        }
    }
}

private const val IDLE_FADE_DELAY_MS = 5_000L
private const val IDLE_FADE_OPACITY = 0.25f

/** Forwards touch state to the core, sending only what changed since the last event. */
private class TouchInputSink {
    private var keys: Set<String> = emptySet()
    private val sticks = HashMap<Stick, Pair<Float, Float>>()
    private val keyPressedAt = HashMap<String, Long>()
    private val pendingReleases = HashMap<String, Runnable>()
    private val handler = Handler(Looper.getMainLooper())

    fun publish(engine: TouchEngine) {
        // No-op when unchanged; re-asserts held buttons if a focus change cleared them.
        GameInputState.updateVirtualButtons(engine.buttonBits)

        val newKeys = engine.keys
        if (newKeys != keys) {
            for (key in keys) if (key !in newKeys) releaseKey(key)
            for (key in newKeys) if (key !in keys) pressKey(key)
            keys = newKeys
        }

        for (stick in Stick.entries) {
            val v = engine.stick(stick) ?: continue
            val value = v[0] to v[1]
            if (sticks[stick] != value) {
                sticks[stick] = value
                GameInputState.setTouchStick(stick == Stick.LEFT, value.first, value.second)
            }
        }
        for (stick in engine.consumeReleasedSticks()) {
            sticks.remove(stick)
            GameInputState.setTouchStick(stick == Stick.LEFT, 0f, 0f)
        }
    }

    /** Releases keys whose minimum hold is still running (the overlay is going away). */
    fun flushPendingReleases() {
        for ((key, release) in pendingReleases.toList()) {
            handler.removeCallbacks(release)
            PhobosCore.setKeyboardKey(key, false)
        }
        pendingReleases.clear()
    }

    private fun pressKey(key: String) {
        val pending = pendingReleases.remove(key)
        if (pending != null) {
            handler.removeCallbacks(pending) // Still held natively; just keep it.
        } else {
            PhobosCore.setKeyboardKey(key, true)
        }
        keyPressedAt[key] = SystemClock.uptimeMillis()
    }

    // Keyboard and keypad keys are read by the core's matrix scan, typically once per frame, so a
    // very quick tap is held for at least MIN_KEY_HOLD_MS to be seen. (Gamepad bits are latched
    // natively until read instead; see PhobosRunner.cpp input().)
    private fun releaseKey(key: String) {
        val heldFor = SystemClock.uptimeMillis() - (keyPressedAt[key] ?: 0L)
        if (heldFor >= MIN_KEY_HOLD_MS) {
            PhobosCore.setKeyboardKey(key, false)
            return
        }
        val release = Runnable {
            pendingReleases.remove(key)
            PhobosCore.setKeyboardKey(key, false)
        }
        pendingReleases[key] = release
        handler.postDelayed(release, MIN_KEY_HOLD_MS - heldFor)
    }

    private companion object {
        const val MIN_KEY_HOLD_MS = 50L
    }
}

/** Short press confirmations, rate-limited so sliding across buttons doesn't buzz continuously. */
private class TouchHaptics(private val view: View) {
    private val vibrator: Vibrator? = view.context.getSystemService(Vibrator::class.java)
    private var lastNanos = 0L

    fun press(level: HapticLevel) {
        if (level == HapticLevel.OFF) return
        val now = System.nanoTime()
        if (now - lastNanos < MIN_INTERVAL_NANOS) return
        lastNanos = now

        val v = vibrator
        if (v == null || !v.hasVibrator()) {
            view.performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            return
        }
        val effect = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            VibrationEffect.createPredefined(
                when (level) {
                    HapticLevel.LIGHT -> VibrationEffect.EFFECT_TICK
                    HapticLevel.MEDIUM -> VibrationEffect.EFFECT_CLICK
                    else -> VibrationEffect.EFFECT_HEAVY_CLICK
                }
            )
        } else {
            when (level) {
                HapticLevel.LIGHT -> VibrationEffect.createOneShot(8L, 90)
                HapticLevel.MEDIUM -> VibrationEffect.createOneShot(14L, 170)
                else -> VibrationEffect.createOneShot(22L, 255)
            }
        }
        v.vibrate(effect)
    }

    /** Double pulse confirming that auto-hold latched a button. */
    fun hold(level: HapticLevel) {
        if (level == HapticLevel.OFF) return
        val v = vibrator ?: return
        if (!v.hasVibrator()) return
        lastNanos = System.nanoTime()
        val effect = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            VibrationEffect.createPredefined(VibrationEffect.EFFECT_DOUBLE_CLICK)
        } else {
            VibrationEffect.createWaveform(longArrayOf(0L, 12L, 60L, 12L), -1)
        }
        v.vibrate(effect)
    }

    private companion object {
        const val MIN_INTERVAL_NANOS = 25_000_000L
    }
}
