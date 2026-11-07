package com.phobos.emulator.ui.theme

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Cartridge's design language, ported from the recovered frontend: a near-black room, a blurred picture
 * behind the page, translucent white panels with a hairline, full pills, sentence-case titles and small
 * letter-spaced section labels. It draws its own surfaces, so the glass effects don't apply; the active
 * theme still supplies the background, text and accent roles, so Phobos's own palette and every other
 * colorway keep working.
 */
object CartridgeUi : SolidUi() {
    override val typography: Typography get() = CartridgeTypography
    override val shapes: Shapes get() = CartridgeShapes
    override val pill: androidx.compose.foundation.shape.CornerBasedShape get() = RoundedCornerShape(percent = 50)
    override val animatedBackdrop: Boolean get() = false

    @Composable
    override fun titleStyle(base: TextStyle): TextStyle = base.copy(
        fontWeight = FontWeight.SemiBold,
        letterSpacing = (-0.6).sp,
    )

    @Composable
    override fun sectionStyle(base: TextStyle): TextStyle = base.copy(
        fontWeight = FontWeight.SemiBold,
        letterSpacing = 1.4.sp,
        fontSize = 11.sp,
    )

    @Composable
    override fun titleColor(onPanel: Boolean): Color =
        if (onPanel) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onBackground

    @Composable
    override fun sectionRule(): Modifier? = null

    /** Text that sits straight on the room: no plate, just the background's own contrast. */
    @Composable
    override fun plate(): Modifier = Modifier

    @Composable
    override fun panel(shape: Shape, fill: Color): Modifier {
        val line = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.28f)
        return Modifier
            .clip(shape)
            .background(fill)
            .border(1.dp, line, shape)
    }

    @Composable
    override fun edge(shape: Shape): Modifier =
        Modifier.border(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.34f), shape)

    @Composable
    override fun menuBorder() = androidx.compose.foundation.BorderStroke(
        1.dp,
        MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.34f),
    )

    override val dockShape: Shape = RoundedCornerShape(22.dp)
    override val dockTabShape: Shape = RoundedCornerShape(percent = 50)

    override fun DrawScope.drawDockIndicator(size: Size, alpha: Float, colors: ColorScheme) {
        drawRoundRect(
            color = colors.primary.copy(alpha = 0.20f * alpha),
            size = size,
            cornerRadius = androidx.compose.ui.geometry.CornerRadius(size.height / 2f),
        )
    }

    @Composable
    override fun dockIconColor(): Color = MaterialTheme.colorScheme.onSurface

    @Composable
    override fun Switch(checked: Boolean) {
        val progress by animateFloatAsState(if (checked) 1f else 0f, spring(), label = "cartridgeSwitch")
        val scheme = MaterialTheme.colorScheme
        Box(
            Modifier
                .size(width = 44.dp, height = 26.dp)
                .clip(CircleShape)
                .background(if (checked) scheme.primary else scheme.surfaceContainerHighest)
                .border(1.dp, scheme.outlineVariant.copy(alpha = 0.35f), CircleShape),
            contentAlignment = if (checked) Alignment.CenterEnd else Alignment.CenterStart,
        ) {
            Box(
                Modifier
                    .padding(horizontal = 3.dp)
                    .size(20.dp)
                    .clip(CircleShape)
                    .background(if (checked) scheme.onPrimary else scheme.onSurfaceVariant),
            )
        }
    }

    @Composable
    override fun SliderThumb() {
        Box(Modifier.size(20.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primary))
    }

    @Composable
    override fun SliderTrack(fraction: Float) {
        val scheme = MaterialTheme.colorScheme
        Box(
            Modifier
                .fillMaxWidth()
                .height(4.dp)
                .clip(CircleShape)
                .background(scheme.surfaceContainerHighest),
        ) {
            Box(
                Modifier
                    .fillMaxWidth(fraction.coerceIn(0f, 1f))
                    .height(4.dp)
                    .clip(CircleShape)
                    .background(scheme.primary),
            )
        }
    }

    @Composable
    override fun ProgressBar(progress: (() -> Float)?, modifier: Modifier) {
        val scheme = MaterialTheme.colorScheme
        val value = progress?.invoke()
        Canvas(modifier.fillMaxWidth().height(4.dp)) {
            val round = androidx.compose.ui.geometry.CornerRadius(size.height / 2f)
            drawRoundRect(scheme.surfaceContainerHighest, size = size, cornerRadius = round)
            if (value != null) {
                drawRoundRect(
                    scheme.primary,
                    size = Size(size.width * value.coerceIn(0f, 1f), size.height),
                    cornerRadius = round,
                )
            }
        }
    }

    /**
     * The room behind every page: the theme's background, a soft accent bloom near the top, and a
     * darker foot. The Library replaces this with the selected game's blurred art.
     */
    @Composable
    override fun Backdrop(modifier: Modifier) {
        val scheme = MaterialTheme.colorScheme
        val dark = LocalPhobosTheme.current.isDark
        Canvas(modifier.fillMaxSize()) {
            drawRect(scheme.background)
            drawRect(
                Brush.verticalGradient(
                    0f to scheme.background,
                    0.42f to scheme.background.mix(scheme.primary, if (dark) 0.055f else 0.04f),
                    1f to scheme.background,
                ),
            )
            drawCircle(
                brush = Brush.radialGradient(
                    colors = listOf(scheme.primary.copy(alpha = if (dark) 0.16f else 0.10f), Color.Transparent),
                    center = Offset(size.width * 0.72f, size.height * 0.08f),
                    radius = size.minDimension * 0.72f,
                ),
                radius = size.minDimension * 0.72f,
                center = Offset(size.width * 0.72f, size.height * 0.08f),
            )
            drawRect(
                Brush.verticalGradient(
                    0.55f to Color.Transparent,
                    1f to scheme.background.copy(alpha = 0.92f),
                ),
            )
        }
    }
}

/** Cartridge's type: a compact grotesque, semibold display, wide-tracked small caps. */
val CartridgeTypography: Typography = run {
    val base = PhobosTypography
    base.copy(
        displayLarge = base.displayLarge.copy(fontWeight = FontWeight.Bold, letterSpacing = (-1.2).sp),
        displayMedium = base.displayMedium.copy(fontWeight = FontWeight.Bold, letterSpacing = (-0.9).sp),
        displaySmall = base.displaySmall.copy(fontWeight = FontWeight.Bold, letterSpacing = (-0.6).sp),
        headlineLarge = base.headlineLarge.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.7).sp),
        headlineMedium = base.headlineMedium.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.5).sp),
        headlineSmall = base.headlineSmall.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.3).sp),
        titleLarge = base.titleLarge.copy(fontWeight = FontWeight.SemiBold, letterSpacing = (-0.3).sp),
        titleMedium = base.titleMedium.copy(fontWeight = FontWeight.SemiBold),
        titleSmall = base.titleSmall.copy(fontWeight = FontWeight.SemiBold, letterSpacing = 0.1.sp),
        labelLarge = base.labelLarge.copy(fontWeight = FontWeight.SemiBold),
        labelMedium = base.labelMedium.copy(fontWeight = FontWeight.SemiBold, letterSpacing = 0.2.sp),
        labelSmall = base.labelSmall.copy(fontWeight = FontWeight.Medium, letterSpacing = 0.6.sp),
    )
}

val CartridgeShapes: Shapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp),
    small = RoundedCornerShape(10.dp),
    medium = RoundedCornerShape(14.dp),
    large = RoundedCornerShape(18.dp),
    extraLarge = RoundedCornerShape(24.dp),
)

/** [this] moved [amount] of the way toward [other] in sRGB, for the faint tints the room uses. */
internal fun Color.mix(other: Color, amount: Float): Color {
    val t = amount.coerceIn(0f, 1f)
    return Color(
        red = red + (other.red - red) * t,
        green = green + (other.green - green) * t,
        blue = blue + (other.blue - blue) * t,
        alpha = alpha + (other.alpha - alpha) * t,
    )
}
