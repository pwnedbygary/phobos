package com.phobos.emulator.ui

import android.content.Context
import android.graphics.Bitmap
import android.provider.Settings
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.geometry.toRect
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.CompositingStrategy
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import com.phobos.emulator.util.PxInsets
import com.phobos.emulator.util.PxRect
import com.phobos.emulator.util.ScreenshotNote
import com.phobos.emulator.util.ShotErrorTimeline
import com.phobos.emulator.util.ShotGeometry
import com.phobos.emulator.util.ShotTimeline
import com.phobos.emulator.util.flapCorners
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

/**
 * A screenshot for the effect. With a [card] (the core gave a picture), the card shows [picture] (the capture's copy,
 * null where it couldn't be made) while the file's written, and the pill joins once [note] says how that went; without
 * one, the pill alone says why there's no screenshot ([note] is known by then).
 */
class ScreenshotEvent(val card: Boolean, val picture: ImageBitmap?, val note: StateFlow<ScreenshotNote?>)

/** Where the game picture lies on the game screen, for the effect's card to start over it. Set as they're placed. */
internal class PictureBounds {
    var screen: LayoutCoordinates? = null
    var picture: LayoutCoordinates? = null

    fun rect(): PxRect? {
        val screen = screen?.takeIf { it.isAttached } ?: return null
        val picture = picture?.takeIf { it.isAttached } ?: return null
        val r = screen.localBoundingBoxOf(picture, clipBounds = false)
        return PxRect(r.left, r.top, r.width, r.height)
    }
}

/** The effect's time since the shot (ms), read as its layers and drawings are made: a float, never boxed. */
internal fun interface ShotClock {
    fun now(): Float
}

private class Shot(
    val id: Int,
    val event: ScreenshotEvent,
    val pictureRect: PxRect?,
    val top: Boolean,
    val reduced: Boolean,
) {
    val aspect = event.picture?.let { it.width.toFloat() / it.height } ?: 0f
}

/**
 * The screenshot effect, over the game screen and its pause menu. A saved shot's picture pops out of the game picture
 * as a film-strip card, hops into a manila folder, and a pill says where it went; one that wasn't saved shows the pill
 * alone, saying why. A new shot restarts it. Nothing is in composition while none runs, and a running one recomposes
 * nothing: one clock drives it, read only as its layers and drawings are made. Each piece fades on its own
 * (ModulateAlpha), never through an offscreen buffer. It takes no touches and no focus.
 */
@Composable
internal fun ScreenshotEffect(events: SharedFlow<ScreenshotEvent>, bounds: PictureBounds, touchControlsInPlay: Boolean) {
    val context = LocalContext.current
    val view = LocalView.current
    val touch by rememberUpdatedState(touchControlsInPlay)
    var shot by remember { mutableStateOf<Shot?>(null) }
    LaunchedEffect(events) {
        var shots = 0
        events.collectLatest { event ->
            val reduced = withContext(Dispatchers.IO) { reducedMotion(context) }
            shot = Shot(++shots, event, bounds.rect(), touch, reduced)
            // TalkBack hears where it went once that's known: a saved shot's card shows before its file is written.
            val note = event.note.filterNotNull().first()
            @Suppress("DEPRECATION")
            view.announceForAccessibility(note.spoken)
        }
    }
    val current = shot ?: return
    key(current.id) {
        ShotLayer(current) { if (shot === current) shot = null }
    }
}

// The card's picture: a few hundred pixels across is all it's shown at once it has shrunk.
internal const val CARD_PICTURE_MAX_SIZE = 640

/**
 * The card's picture from the capture's copy (its width, its height, then its pixels): a hardware bitmap, so the
 * effect's first frame uploads nothing, or a software one where that can't be made; null for an empty copy.
 */
internal fun cardPicture(copy: IntArray): ImageBitmap? {
    val width = copy.getOrElse(0) { 0 }
    val height = copy.getOrElse(1) { 0 }
    if (width <= 0 || height <= 0 || copy.size < 2 + width * height) return null
    val pixels = Bitmap.createBitmap(copy, 2, width, width, height, Bitmap.Config.ARGB_8888)
    val hardware = try {
        pixels.copy(Bitmap.Config.HARDWARE, false)
    } catch (_: RuntimeException) {
        null
    }
    if (hardware == null) return pixels.asImageBitmap()
    pixels.recycle()
    return hardware.asImageBitmap()
}

/** Remove animations (an animator duration scale of 0), under which Compose would end the clock at once. */
private fun reducedMotion(context: Context) =
    Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f

@Composable
private fun ShotLayer(shot: Shot, onDone: () -> Unit) {
    val clock = remember { Animatable(0f) }
    val duration = when {
        !shot.event.card -> ShotErrorTimeline.TOTAL_MS
        shot.reduced -> ShotTimeline.REDUCED_MS
        else -> ShotTimeline.TOTAL_MS
    }
    val done by rememberUpdatedState(onDone)
    LaunchedEffect(Unit) {
        // Compose runs an animation at the animator duration scale, so under Remove animations the clock would end at
        // once; a delay isn't scaled, so the still effect stays its 1.6 s (a failure's pill its 3 s).
        if (shot.reduced) delay(duration.toLong()) else clock.animateTo(duration, tween(duration.toInt(), easing = LinearEasing))
        done()
    }
    val time = remember { ShotClock { clock.value } }
    val density = LocalDensity.current
    val direction = LocalLayoutDirection.current
    val safe = WindowInsets.safeDrawing
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val width = constraints.maxWidth.toFloat()
        val height = constraints.maxHeight.toFloat()
        // The insets as the effect starts, read again only if the screen's size changes (a rotation lays it out anew).
        val geometry = remember(width, height) {
            val inset = PxInsets(
                safe.getLeft(density, direction).toFloat(), safe.getTop(density).toFloat(),
                safe.getRight(density, direction).toFloat(), safe.getBottom(density).toFloat(),
            )
            ShotGeometry(width, height, shot.pictureRect, shot.aspect, shot.top, inset)
        }
        if (shot.event.card) {
            val timeline = remember(geometry) { ShotTimeline(geometry, shot.reduced) }
            // The pill keeps to its title on a narrow screen, as the preview does, unless it's a hint or a failure.
            SavedShot(shot.event.note, shot.event.picture, geometry, timeline, time, wide = maxWidth > 560.dp)
        } else {
            val note = shot.event.note.value ?: return@BoxWithConstraints
            val timeline = remember(geometry) { ShotErrorTimeline(geometry, shot.reduced) }
            FailedShot(note, geometry, timeline, time)
        }
    }
}

@Composable
private fun SavedShot(
    note: StateFlow<ScreenshotNote?>, picture: ImageBitmap?, g: ShotGeometry, timeline: ShotTimeline, time: ShotClock, wide: Boolean,
) {
    Layout(
        content = {
            FolderBack(g, timeline, time)
            FilmCard(g, timeline, picture, time)
            FolderFront(g, timeline, time)
            Sparkles(g, timeline, time)
            KnownPill(
                note, g, wide,
                Modifier.graphicsLayer {
                    val t = time.now()
                    transformOrigin = LEFT_CENTER
                    translationX = timeline.pillX(t) + timeline.trayX(t)
                    val scale = timeline.pillScale(t)
                    scaleX = scale
                    scaleY = scale
                    alpha = timeline.pillAlpha(t) * timeline.trayAlpha(t)
                    compositingStrategy = CompositingStrategy.ModulateAlpha
                },
            )
        },
        modifier = Modifier.fillMaxSize(),
    ) { measurables, constraints ->
        val sparkSide = (30f * g.u).roundToInt()
        val back = measurables[0].measure(fixed(g.back))
        val card = measurables[1].measure(fixed(g.film))
        val front = measurables[2].measure(fixed(g.front))
        val sparks = measurables[3].measure(Constraints.fixed(sparkSide, sparkSide))
        val pillWidth = (g.width - g.inset.right - g.pillLeft - 3f * g.u).roundToInt().coerceAtLeast(0)
        val pill = measurables.getOrNull(4)?.measure(Constraints(maxWidth = pillWidth))
        layout(constraints.maxWidth, constraints.maxHeight) {
            back.place(g.back.left.roundToInt(), g.back.top.roundToInt())
            card.place(g.film.left.roundToInt(), g.film.top.roundToInt())
            front.place(g.front.left.roundToInt(), g.front.top.roundToInt())
            sparks.place((g.mouthX - sparkSide / 2f).roundToInt(), (g.mouthY - sparkSide / 2f).roundToInt())
            if (pill != null) pill.place(g.pillLeft.roundToInt(), (g.pillBottom - pill.height).roundToInt())
        }
    }
}

/** The pill, once the file's written and it's known what to say: as a rule well before it slides out. */
@Composable
private fun KnownPill(note: StateFlow<ScreenshotNote?>, g: ShotGeometry, wide: Boolean, motion: Modifier) {
    val known by note.collectAsState()
    val said = known ?: return
    Pill(said, g, showDetail = wide || said.hint || said.failed, motion)
}

@Composable
private fun FailedShot(note: ScreenshotNote, g: ShotGeometry, timeline: ShotErrorTimeline, time: ShotClock) {
    Layout(
        content = {
            Pill(
                note, g, showDetail = true,
                Modifier.graphicsLayer {
                    val t = time.now()
                    transformOrigin = LEFT_CENTER
                    translationX = timeline.pillX(t)
                    val scale = timeline.pillScale(t)
                    scaleX = scale
                    scaleY = scale
                    alpha = timeline.pillAlpha(t)
                    compositingStrategy = CompositingStrategy.ModulateAlpha
                },
            )
        },
        modifier = Modifier.fillMaxSize(),
    ) { measurables, constraints ->
        val margin = 3.2f * g.u
        val inset = g.inset
        val pill = measurables[0].measure(Constraints(maxWidth = (g.width - inset.left - inset.right - 6f * g.u).roundToInt().coerceAtLeast(0)))
        layout(constraints.maxWidth, constraints.maxHeight) {
            val y = if (g.top) inset.top + margin else g.height - inset.bottom - margin - pill.height
            pill.place((inset.left + 3f * g.u).roundToInt(), y.roundToInt())
        }
    }
}

private fun fixed(box: PxRect) = Constraints.fixed(box.width.roundToInt(), box.height.roundToInt())

/** The folder's back panel and its tab, behind the card. */
@Composable
private fun FolderBack(g: ShotGeometry, timeline: ShotTimeline, time: ShotClock) {
    val u = g.u
    // The shadow's outline takes one radius (the panel's corners differ a little): with mixed ones Compose would build a
    // Path for it each frame.
    val shadow = remember(g) { RoundedCornerShape(CornerSize(0.7f * u)) }
    Spacer(
        Modifier
            .graphicsLayer {
                val t = time.now()
                transformOrigin = BOTTOM_CENTER
                scaleX = timeline.folderScaleX(t)
                scaleY = timeline.folderScaleY(t)
                alpha = timeline.folderAlpha(t) * timeline.trayAlpha(t)
                // An offscreen buffer would be the node's size and cut off the tab, drawn above it.
                compositingStrategy = CompositingStrategy.ModulateAlpha
                translationX = timeline.trayX(t)
                shadowElevation = 0.8f * u
                shape = shadow
            }
            .drawWithCache {
                val tab = Path().apply {
                    addRoundRect(RoundRect(0f, -g.tabRise, 5f * u, 0.4f * u, CornerRadius(0.6f * u), CornerRadius(1.6f * u), CornerRadius.Zero, CornerRadius.Zero))
                }
                val panel = Path().apply {
                    addRoundRect(RoundRect(size.toRect(), CornerRadius(0.5f * u), CornerRadius(0.9f * u), CornerRadius(0.7f * u), CornerRadius(0.7f * u)))
                }
                val brush = Brush.verticalGradient(0f to FOLDER_TAB, 0.45f to FOLDER_BACK, startY = 0f, endY = size.height)
                onDrawBehind {
                    drawPath(tab, FOLDER_TAB)
                    drawPath(panel, brush)
                }
            },
    )
}

/** The film-strip card: the picture between two bands of sprocket holes, washed white at first (the flash). */
@Composable
private fun FilmCard(g: ShotGeometry, timeline: ShotTimeline, picture: ImageBitmap?, time: ShotClock) {
    val u = g.u
    val outline = remember(g) { RoundedCornerShape(CornerSize(0.7f * u)) }
    Spacer(
        Modifier
            .graphicsLayer {
                val t = time.now()
                val scale = timeline.cardScale(t)
                translationX = timeline.cardX(t) + timeline.trayX(t)
                translationY = timeline.cardY(t)
                scaleX = scale
                scaleY = scale
                rotationZ = timeline.cardTurn(t)
                alpha = timeline.trayAlpha(t)
                compositingStrategy = CompositingStrategy.ModulateAlpha
                // No shadow while it covers the game picture; it settles in as the card shrinks to its corner.
                shadowElevation = 1.2f * u * (2f - scale).coerceIn(0f, 1f)
                shape = outline
            }
            .drawWithCache {
                val frame = g.frame
                val body = Path().apply { addRoundRect(RoundRect(size.toRect(), CornerRadius(0.7f * u))) }
                val frameClip = Path().apply { addRoundRect(RoundRect(frame.left, frame.top, frame.right, frame.bottom, CornerRadius(0.35f * u))) }
                val holeTop = g.band / 2f - 0.55f * u
                val holes = Path().apply {
                    for (k in 0 until 16) {
                        val left = 0.55f * u + k * g.pitch
                        addRoundRect(RoundRect(left, holeTop, left + 1.15f * u, holeTop + 1.1f * u, CornerRadius(0.28f * u)))
                    }
                }
                val frameOffset = Offset(frame.left, frame.top)
                val frameSize = Size(frame.width, frame.height)
                val pictureOffset = IntOffset(frame.left.roundToInt(), frame.top.roundToInt())
                val pictureSize = IntSize(frame.width.roundToInt(), frame.height.roundToInt())
                val edgeSize = Size(size.width - 1f, size.height - 1f)
                val edgeRadius = CornerRadius(0.7f * u - 0.5f)
                onDrawBehind {
                    val t = time.now()
                    drawPath(body, FILM)
                    drawRoundRect(FILM_EDGE, Offset(0.5f, 0.5f), edgeSize, edgeRadius, EDGE_STROKE)
                    clipPath(frameClip) {
                        if (picture != null) drawImage(picture, dstOffset = pictureOffset, dstSize = pictureSize)
                        else drawRect(Color.Black, frameOffset, frameSize)
                        val flash = timeline.develop(t)
                        if (flash > 0f) drawRect(Color.White, frameOffset, frameSize, alpha = flash)
                    }
                    val wound = timeline.holes(t)
                    clipRect(0f, 0f, size.width, g.band) { translate(wound, 0f) { drawPath(holes, SPROCKET) } }
                    clipRect(0f, size.height - g.band, size.width, size.height) {
                        translate(wound, size.height - g.band) { drawPath(holes, SPROCKET) }
                    }
                }
            },
    )
}

/**
 * The folder's front panel, over the card: it tips open toward the viewer and snaps shut, drawn through the preview's
 * CSS perspective (flapCorners) rather than a layer's camera, so it's that perspective exactly.
 */
@Composable
private fun FolderFront(g: ShotGeometry, timeline: ShotTimeline, time: ShotClock) {
    val u = g.u
    Spacer(
        Modifier
            .graphicsLayer {
                val t = time.now()
                transformOrigin = BOTTOM_CENTER
                scaleX = timeline.folderScaleX(t)
                scaleY = timeline.folderScaleY(t)
                alpha = timeline.folderAlpha(t) * timeline.trayAlpha(t)
                compositingStrategy = CompositingStrategy.ModulateAlpha
                translationX = timeline.trayX(t)
            }
            .drawWithCache {
                val w = size.width
                val h = size.height
                val panel = Path().apply {
                    addRoundRect(RoundRect(0f, 0f, w, h, CornerRadius(0.55f * u), CornerRadius(0.55f * u), CornerRadius(0.7f * u), CornerRadius(0.7f * u)))
                }
                val brush = Brush.verticalGradient(listOf(FOLDER_LIGHT, FOLDER_FRONT), startY = 0f, endY = h)
                val highlight = Size(w, 0.22f * u)
                val shadeTop = Offset(0f, h - 0.3f * u)
                val shade = Size(w, 0.3f * u)
                val flat = floatArrayOf(0f, 0f, w, 0f, w, h, 0f, h)
                val tipped = FloatArray(8)
                val matrix = android.graphics.Matrix()
                onDrawBehind {
                    flapCorners(timeline.flap(time.now()), w, h, g.perspective, tipped)
                    matrix.setPolyToPoly(flat, 0, tipped, 0, 4)
                    val canvas = drawContext.canvas.nativeCanvas
                    canvas.save()
                    canvas.concat(matrix)
                    drawPath(panel, brush)
                    clipPath(panel) {
                        drawRect(FOLDER_HIGHLIGHT, size = highlight)
                        drawRect(FOLDER_SHADE, shadeTop, shade)
                    }
                    canvas.restore()
                }
            },
    )
}

/** Nine four-point sparkles bursting from the folder's mouth (the middle of this node). */
@Composable
private fun Sparkles(g: ShotGeometry, timeline: ShotTimeline, time: ShotClock) {
    Spacer(
        Modifier
            .graphicsLayer()
            .drawWithCache {
                val r = 1.3f * g.u
                val i = 0.24f * r
                val star = Path().apply {
                    moveTo(0f, -r); lineTo(i, -i); lineTo(r, 0f); lineTo(i, i)
                    lineTo(0f, r); lineTo(-i, i); lineTo(-r, 0f); lineTo(-i, -i); close()
                }
                val cx = size.width / 2f
                val cy = size.height / 2f
                onDrawBehind {
                    val t = time.now()
                    for (s in 0 until ShotTimeline.SPARKS) {
                        val alpha = timeline.sparkAlpha(s, t)
                        if (alpha <= 0f) continue
                        val grow = timeline.sparkScale(s, t)
                        withTransform({
                            translate(cx + timeline.sparkX(s, t), cy + timeline.sparkY(s, t))
                            rotate(timeline.sparkTurn(s, t), Offset.Zero)
                            scale(grow, grow, Offset.Zero)
                        }) {
                            drawPath(star, if (s % 2 == 0) SPARK_GOLD else Color.White, alpha = alpha)
                        }
                    }
                }
            },
    )
}

/** The dark pill: "Screenshot saved" over where, or, error-tinted, "Screenshot not saved" over why. */
@Composable
private fun Pill(note: ScreenshotNote, g: ShotGeometry, showDetail: Boolean, motion: Modifier) {
    val density = LocalDensity.current
    val u = with(density) { g.u.toDp().value }
    val edge = with(density) { 1f.toDp() }
    val titleSize = (1.75f * u).coerceIn(11f, 15f).sp
    val detailSize = (1.45f * u).coerceIn(11f, 13f).sp
    // Round at the ends as a pill of two lines is; one that wraps further keeps those corners.
    val shape = remember(density, u) {
        val twoLines = with(density) { (2.5f * u).dp.toPx() + (titleSize.toPx() + detailSize.toPx()) * 1.25f }
        RoundedCornerShape(CornerSize(twoLines / 2f))
    }
    Column(
        motion
            .background(if (note.failed) PILL_ERROR else PILL, shape)
            .border(edge, if (note.failed) PILL_ERROR_EDGE else PILL_EDGE, shape)
            .padding(start = (2.1f * u).dp, end = (2.1f * u).dp, top = (1.1f * u).dp, bottom = (1.2f * u).dp),
        verticalArrangement = Arrangement.spacedBy((0.2f * u).dp),
    ) {
        Text(
            note.title,
            style = TextStyle(
                color = if (note.failed) PILL_ERROR_TITLE else Color.White,
                fontSize = titleSize, fontWeight = FontWeight.Medium, lineHeight = 1.25.em,
            ),
        )
        if (showDetail) {
            Text(
                note.detail,
                style = TextStyle(
                    color = if (note.failed) PILL_ERROR_DETAIL else PILL_DETAIL,
                    fontSize = detailSize, lineHeight = 1.25.em,
                ),
            )
        }
    }
}

private val FILM = Color(0xFF18171C)
private val FILM_EDGE = Color(0x12FFFFFF)
private val EDGE_STROKE = Stroke(width = 1f)
private val SPROCKET = Color(0xFFEFE6D2)
private val FOLDER_TAB = Color(0xFFCF8F2A)
private val FOLDER_BACK = Color(0xFFE2A237)
private val FOLDER_LIGHT = Color(0xFFFFD56E)
private val FOLDER_FRONT = Color(0xFFF6BB41)
private val FOLDER_HIGHLIGHT = Color(0x8CFFFFFF)
private val FOLDER_SHADE = Color(0x14000000)
private val SPARK_GOLD = Color(0xFFFFE7A1)
private val PILL = Color(0xEB121218)
private val PILL_EDGE = Color(0x1AFFFFFF)
private val PILL_DETAIL = Color(0xFFC9C8D4)
private val PILL_ERROR = Color(0xF02E1216)
private val PILL_ERROR_EDGE = Color(0x59FF8A8A)
private val PILL_ERROR_TITLE = Color(0xFFFFE1E1)
private val PILL_ERROR_DETAIL = Color(0xFFF2BDBD)
private val BOTTOM_CENTER = TransformOrigin(0.5f, 1f)
private val LEFT_CENTER = TransformOrigin(0f, 0.5f)

@Composable
private fun ShotPreview(at: Float, failed: Boolean = false) {
    Box(Modifier.fillMaxSize().background(Color(0xFF55604E))) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val g = ShotGeometry(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), null, 480f / 272f, top = false)
            val time = ShotClock { at }
            if (failed) {
                FailedShot(
                    ScreenshotNote("Screenshot not saved", "The game has no picture to save yet", failed = true, spoken = ""),
                    g, ShotErrorTimeline(g, reduced = false), time,
                )
            } else {
                val note = ScreenshotNote("Screenshot saved", "Screenshots › PlayStation Portable", failed = false, spoken = "")
                SavedShot(MutableStateFlow(note), null, g, ShotTimeline(g, reduced = false), time, wide = true)
            }
        }
    }
}

@Preview(widthDp = 720, heightDp = 405)
@Composable
private fun ShotAt420() = ShotPreview(420f)

@Preview(widthDp = 720, heightDp = 405)
@Composable
private fun ShotAt1400() = ShotPreview(1400f)

@Preview(widthDp = 720, heightDp = 405)
@Composable
private fun ShotAt2000() = ShotPreview(2000f)

@Preview(widthDp = 720, heightDp = 405)
@Composable
private fun ShotNotSaved() = ShotPreview(1500f, failed = true)
