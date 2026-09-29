package com.phobos.emulator.ui.theme

import android.animation.ValueAnimator
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.CornerBasedShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextStyle
import com.phobos.emulator.data.UiEffects
import kotlinx.coroutines.delay

/**
 * What a style effect (Settings → Appearance → Style effects) changes over the theme's colors.
 * [GlassUi] is the glass look, for None and for Retrowave, whose neon the screens draw themselves;
 * [SolidUi] styles draw their own panels, headers and controls in place of glass.
 */
@Stable
abstract class UiStyle {
    open val typography: Typography get() = PhobosTypography
    open val shapes: Shapes get() = PhobosShapes

    /** Material's fully rounded pill, which [Shapes] doesn't cover: buttons, value pills, segmented buttons. */
    open val pill: CornerBasedShape get() = CircleShape

    /** Whether [Backdrop] replaces the glass glows (and Retrowave's sunset) behind the pages. */
    open val ownBackdrop: Boolean get() = false

    @Composable
    open fun Backdrop(modifier: Modifier) {}
}

/** The glass look: glass panels over soft glows, or Retrowave's neon over its sunset. */
object GlassUi : UiStyle()

/**
 * A style with its own solid panels, which Glass effects don't apply to, and its own backdrop, headers
 * and controls. Screens draw the glass versions when the style isn't one of these. Its parts take
 * [MaterialTheme.colorScheme], not [PhobosThemeInfo.scheme], so they cross-fade with the pages after a
 * theme change.
 */
@Stable
abstract class SolidUi : UiStyle() {
    override val ownBackdrop: Boolean get() = true

    /** Page titles, the top bar's title and section headers in capitals. */
    open val capitalHeaders: Boolean get() = false

    /** A blinking cursor after page titles and the top bar's title. */
    open val titleCursor: Boolean get() = false

    /** [base], the type scale's style, for a page title or the top bar's title. */
    @Composable
    open fun titleStyle(base: TextStyle): TextStyle = base

    /** A page title's color: [onPanel] for the top bar's, which also sits on panels once content scrolls under it. */
    @Composable
    open fun titleColor(onPanel: Boolean): Color =
        if (onPanel) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onBackground

    /** [base] for a section header. */
    @Composable
    open fun sectionStyle(base: TextStyle): TextStyle = base

    /** Draws a rule from the end of a section header to the end of its row; null for none. */
    @Composable
    open fun sectionRule(): Modifier? = null

    /** Behind text drawn straight on the backdrop: page titles, section headers, notes. */
    @Composable
    abstract fun plate(): Modifier

    /** A panel of [fill] in [shape]: cards and the dock. */
    @Composable
    abstract fun panel(shape: Shape, fill: Color): Modifier

    /** The edge of a container that draws its own fill in [shape]: dialogs and the pause menu. */
    @Composable
    abstract fun edge(shape: Shape): Modifier

    /** A dropdown menu's border, drawn in place of its shadow. */
    @Composable
    abstract fun menuBorder(): BorderStroke

    abstract val dockShape: Shape
    abstract val dockTabShape: Shape

    /** The selected dock tab's indicator in [colors], [size] (it springs open), at [alpha]; centered by the caller. */
    abstract fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme)

    /** The selected dock tab's icon, over [drawDockIndicator]. */
    @Composable
    abstract fun dockIconColor(): Color

    /** A switch, display only: the settings row it sits in does the toggling. */
    @Composable
    abstract fun Switch(checked: Boolean)

    @Composable
    abstract fun SliderThumb()

    /** A slider's track, filled to [fraction] of its width. */
    @Composable
    abstract fun SliderTrack(fraction: Float)

    /** Linear progress, indeterminate while [progress] is null. */
    @Composable
    abstract fun ProgressBar(progress: (() -> Float)?, modifier: Modifier)
}

/** The pill of buttons, value pills and segmented buttons in the current style ([UiStyle.pill]). */
@Composable
@ReadOnlyComposable
fun pillShape(): CornerBasedShape = LocalPhobosTheme.current.style.pill

/** The look of these effects. */
val UiEffects.style: UiStyle
    get() = when (this) {
        UiEffects.NONE, UiEffects.RETROWAVE -> GlassUi
        UiEffects.PIXEL_ART -> PixelUi
        UiEffects.CRT -> CrtUi
        UiEffects.RPG -> RpgUi
    }

/**
 * A terminal cursor's blink while [enabled]: on and off about twice a second, and steadily on when
 * Android's animations are off.
 */
@Composable
fun rememberCursorBlink(enabled: Boolean): State<Boolean> {
    val on = remember { mutableStateOf(true) }
    LaunchedEffect(enabled) {
        on.value = true
        if (!enabled || !ValueAnimator.areAnimatorsEnabled()) return@LaunchedEffect
        while (true) {
            delay(CURSOR_BLINK_MS)
            on.value = !on.value
        }
    }
    return on
}

private const val CURSOR_BLINK_MS = 530L
