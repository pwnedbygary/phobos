package com.phobos.emulator.ui.hud

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.changedToDownIgnoreConsumed
import androidx.compose.ui.layout.IntrinsicMeasurable
import androidx.compose.ui.layout.IntrinsicMeasureScope
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.layout.Measurable
import androidx.compose.ui.layout.MeasurePolicy
import androidx.compose.ui.layout.MeasureResult
import androidx.compose.ui.layout.MeasureScope
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.node.CompositionLocalConsumerModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.PointerInputModifierNode
import androidx.compose.ui.node.currentValueOf
import androidx.compose.ui.node.requireLayoutCoordinates
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalViewConfiguration
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.round
import androidx.compose.ui.unit.sp
import androidx.compose.ui.unit.toOffset
import androidx.compose.ui.unit.toSize
import com.phobos.emulator.PerformanceStats
import com.phobos.emulator.PhobosCore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.Locale
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** MangoHud's default palette, plus status colours for FPS and thermals. */
private object HudColors {
    val gpu = Color(0xFF2E9762)
    val cpu = Color(0xFF2E97CB)
    val ram = Color(0xFFC26693)
    val engine = Color(0xFFEB5B5B)
    val battery = Color(0xFFFF9078)
    val thermal = Color(0xFFA491D3)
    val clock = Color(0xFFB0BEC5)
    val frametime = Color(0xFF00FF00)
    val text = Color.White
    val dim = Color.White.copy(alpha = 0.62f)
    val good = Color(0xFF7CE06F)
    val warn = Color(0xFFFFD54F)
    val bad = Color(0xFFFF6B6B)
}

/** One styled run of HUD text. */
private data class Seg(
    val text: String,
    val color: Color = HudColors.text,
    val bold: Boolean = false,
    val small: Boolean = false,
)

private fun value(v: String) = Seg(v, bold = true)
private fun unit(u: String) = Seg(u, color = HudColors.dim, small = true)
private fun gap() = Seg("  ")

private fun fmt1(v: Double) = String.format(Locale.US, "%.1f", v)
private fun fmt1(v: Float) = fmt1(v.toDouble())
private fun fmt2(v: Float) = String.format(Locale.US, "%.2f", v)

private fun fpsColor(fps: Double, target: Double): Color = when {
    fps <= 0.0 -> HudColors.dim
    fps >= target * 0.97 -> HudColors.good
    fps >= target * 0.75 -> HudColors.warn
    else -> HudColors.bad
}

private fun sessionText(seconds: Long): String {
    val h = seconds / 3600
    val m = (seconds / 60) % 60
    val s = seconds % 60
    return if (h > 0) String.format(Locale.US, "%d:%02d:%02d", h, m, s) else String.format(Locale.US, "%02d:%02d", m, s)
}

private val clockFormat: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm")

/** Short tag for the running core, used in the HUD's system row. */
fun hudSystemLabel(systemName: String): String = when (systemName) {
    "Nintendo 64", "Nintendo 64DD" -> "N64"
    "PlayStation" -> "PS1"
    "Super Famicom" -> "SNES"
    "Super Game Boy" -> "SGB"
    "Famicom" -> "NES"
    "Game Boy" -> "GB"
    "Game Boy Color" -> "GBC"
    "Game Boy Advance" -> "GBA"
    "Mega Drive" -> "MD"
    "Mega CD" -> "MCD"
    "Mega 32X" -> "32X"
    "Mega CD 32X" -> "CD32X"
    "Mega LD" -> "MEGALD"
    "Master System" -> "SMS"
    "Game Gear" -> "GG"
    "PC Engine", "SuperGrafx" -> "PCE"
    "PC Engine CD" -> "PCECD"
    "PC Engine LD" -> "PCELD"
    "Neo Geo" -> "NEOGEO"
    "Arcade" -> "ARCADE"
    "Neo Geo CD" -> "NGCD"
    "Neo Geo Pocket", "Neo Geo Pocket Color" -> "NGP"
    "WonderSwan", "WonderSwan Color" -> "WS"
    "Atari 2600" -> "A2600"
    "ColecoVision" -> "CV"
    "ZX Spectrum", "ZX Spectrum 128" -> "ZX"
    else -> systemName
}

// ── Row contents (null = nothing to show) ──────────────────────────────────

private fun cpuSegs(config: HudConfig, stats: PerformanceStats, host: HostSample): List<Seg>? {
    val segs = mutableListOf<Seg>()
    if (config.cpu) {
        (host.emuThreadCpuPercent ?: host.processCpuPercent)?.let { segs += listOf(value("${it.roundToInt()}"), unit("%")) }
    }
    if (config.cpuDetail) {
        if (stats.activeCore >= 0) {
            if (segs.isNotEmpty()) segs += gap()
            segs += Seg("C${stats.activeCore}", color = HudColors.dim)
        }
        host.emuCoreFreqMhz?.let {
            if (segs.isNotEmpty()) segs += Seg(" ")
            segs += listOf(value(fmt2(it / 1000f)), unit("GHz"))
        }
    }
    if (config.cpu) {
        host.cpuTempC?.let { if (segs.isNotEmpty()) segs += gap(); segs += listOf(value("${it.roundToInt()}"), unit("°C")) }
    }
    return segs.ifEmpty { null }
}

private fun gpuSegs(host: HostSample): List<Seg>? {
    val segs = mutableListOf<Seg>()
    host.gpuBusyPercent?.let { segs += listOf(value("$it"), unit("%")) }
    host.gpuFreqMhz?.let { if (segs.isNotEmpty()) segs += gap(); segs += listOf(value("$it"), unit("MHz")) }
    host.gpuTempC?.let { if (segs.isNotEmpty()) segs += gap(); segs += listOf(value("${it.roundToInt()}"), unit("°C")) }
    return segs.ifEmpty { null }
}

private fun ramSegs(host: HostSample): List<Seg>? {
    val segs = mutableListOf<Seg>()
    host.processRssMb?.let { segs += listOf(value("$it"), unit("MB")) }
    val used = host.systemUsedMb
    val total = host.systemTotalMb
    if (used != null && total != null) {
        if (segs.isNotEmpty()) segs += gap()
        segs += listOf(Seg("${fmt1(used / 1024f)}/${fmt1(total / 1024f)}", color = HudColors.dim), unit("GB"))
    }
    return segs.ifEmpty { null }
}

private fun batterySegs(host: HostSample): List<Seg>? {
    val segs = mutableListOf<Seg>()
    host.batteryPercent?.let { segs += listOf(value("$it"), unit("%")) }
    if (host.batteryCharging) segs += Seg(" \u26A1", color = HudColors.warn)
    host.batteryPowerW?.let { if (segs.isNotEmpty()) segs += gap(); segs += listOf(value(fmt1(it)), unit("W")) }
    host.batteryTempC?.let { if (segs.isNotEmpty()) segs += gap(); segs += listOf(value("${it.roundToInt()}"), unit("°C")) }
    return segs.ifEmpty { null }
}

private fun thermalSegs(host: HostSample): List<Seg>? {
    val status = host.thermalStatus ?: return null
    val (name, color) = when (status) {
        0 -> "Normal" to HudColors.good
        1 -> "Light" to HudColors.good
        2 -> "Moderate" to HudColors.warn
        3 -> "Severe" to HudColors.bad
        4 -> "Critical" to HudColors.bad
        5 -> "Emergency" to HudColors.bad
        6 -> "Shutdown" to HudColors.bad
        else -> "Unknown" to HudColors.dim
    }
    val segs = mutableListOf(Seg(name, color = color, bold = true))
    host.thermalHeadroom?.let { segs += listOf(gap(), Seg(fmt2(it), color = HudColors.dim)) }
    return segs
}

private fun systemSegs(systemLabel: String?, resolution: String?): List<Seg>? {
    val segs = mutableListOf<Seg>()
    systemLabel?.takeIf { it.isNotEmpty() }?.let { segs += value(it) }
    resolution?.let { if (segs.isNotEmpty()) segs += gap(); segs += Seg(it, color = HudColors.dim) }
    return segs.ifEmpty { null }
}

private fun clockSegs(sessionSeconds: Long): List<Seg> =
    listOf(value(LocalTime.now().format(clockFormat)), gap(), Seg(sessionText(sessionSeconds), color = HudColors.dim))

// ── Presentation ───────────────────────────────────────────────────────────

private fun hudStyle(size: TextUnit): TextStyle = TextStyle(
    fontSize = size,
    fontFeatureSettings = "tnum",
    shadow = Shadow(Color.Black.copy(alpha = 0.85f), Offset(0f, 1.5f), 3f),
)

private fun segsText(segs: List<Seg>, base: TextUnit): AnnotatedString = buildAnnotatedString {
    for (s in segs) {
        withStyle(
            SpanStyle(
                color = s.color,
                fontWeight = if (s.bold) FontWeight.Bold else FontWeight.Medium,
                fontSize = if (s.small) base * 0.74f else base,
            )
        ) { append(s.text) }
    }
}

/** Average of the most recent frame intervals, or null without enough samples. */
private fun recentIntervalMs(times: FloatArray): Float? {
    if (times.size < 4) return null
    val n = min(30, times.size)
    var sum = 0f
    for (i in times.size - n until times.size) sum += times[i]
    return sum / n
}

/**
 * MangoHud-style performance HUD (presentation only). Rows the device can't provide are
 * hidden. [frameTimes] are frame-to-frame intervals in ms, oldest first. With [fill], for a box
 * sized on screen, it takes the width it's given: rows wrap to it and the graph stretches across.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PerformanceHud(
    stats: PerformanceStats,
    host: HostSample,
    frameTimes: FloatArray,
    config: HudConfig,
    systemLabel: String?,
    resolution: String?,
    sessionSeconds: Long,
    modifier: Modifier = Modifier,
    fill: Boolean = false,
) {
    val lines = if (fill) Int.MAX_VALUE else 1
    val s = config.scale.coerceIn(0.5f, 2.5f)
    val base = (11f * s).sp
    val target = if (stats.targetFps > 1.0) stats.targetFps else 60.0
    val targetMs = (1000.0 / target).toFloat()
    val intervalMs = recentIntervalMs(frameTimes) ?: if (stats.fps > 0) (1000.0 / stats.fps).toFloat() else null
    val background = Color(0xFF020202).copy(alpha = config.opacity.coerceIn(0f, 1f))
    val shape = RoundedCornerShape((8 * s).dp)

    // A labelled stat row (CPU, GPU, ...), or null when the device gives nothing to show
    fun row(item: HudItem): Triple<String, Color, List<Seg>>? = when (item) {
        HudItem.CPU -> cpuSegs(config, stats, host)?.let { Triple("CPU", HudColors.cpu, it) }
        HudItem.GPU -> gpuSegs(host)?.let { Triple("GPU", HudColors.gpu, it) }
        HudItem.RAM -> ramSegs(host)?.let { Triple("RAM", HudColors.ram, it) }
        HudItem.BATTERY -> batterySegs(host)?.let { Triple("BAT", HudColors.battery, it) }
        HudItem.THERMAL -> thermalSegs(host)?.let { Triple("THM", HudColors.thermal, it) }
        HudItem.SYSTEM -> systemSegs(systemLabel, resolution)?.let { Triple("SYS", HudColors.engine, it) }
        HudItem.CLOCK -> Triple("TIME", HudColors.clock, clockSegs(sessionSeconds))
        HudItem.FPS, HudItem.FRAME_TIME, HudItem.GRAPH -> null
    }
    val shown = config.order.filter { config.shows(it) }
    val shaderWarning = config.shaderFails && stats.pipelineFailures > 0

    if (config.horizontal) {
        // Items flow onto another line instead of squeezing when they don't fit.
        val container = modifier
            .background(background, shape)
            .padding(horizontal = (8 * s).dp, vertical = (4 * s).dp)
        val itemSpacing = Arrangement.spacedBy((10 * s).dp)
        val lineSpacing = (2 * s).dp

        @Composable
        fun Items(item: Modifier) {
            for (entry in shown) {
                when (entry) {
                    HudItem.FPS -> Text(
                        segsText(
                            listOf(Seg(if (stats.fps > 0) fmt1(stats.fps) else "--", color = fpsColor(stats.fps, target), bold = true), unit(" FPS")),
                            base * 1.15f,
                        ),
                        style = hudStyle(base),
                        maxLines = lines,
                        softWrap = fill,
                        modifier = item,
                    )
                    HudItem.FRAME_TIME -> if (intervalMs != null) {
                        Text(segsText(listOf(value(fmt1(intervalMs)), unit("ms")), base), style = hudStyle(base), maxLines = lines, softWrap = fill, modifier = item)
                    }
                    HudItem.GRAPH -> if (frameTimes.size >= 2) {
                        Box(item) { FrameTimeGraph(frameTimes, targetMs, Modifier.size((64 * s).dp, (16 * s).dp)) }
                    }
                    else -> row(entry)?.let { (label, color, segs) ->
                        Text(
                            segsText(listOf(Seg("$label ", color = color, bold = true)) + segs, base),
                            style = hudStyle(base),
                            maxLines = lines,
                            softWrap = fill,
                            modifier = item,
                        )
                    }
                }
            }
            if (shaderWarning) {
                Text(
                    segsText(listOf(Seg("\u26A0 ${stats.pipelineFailures}", color = HudColors.bad, bold = true)), base),
                    style = hudStyle(base),
                    maxLines = lines,
                    softWrap = fill,
                    modifier = item,
                )
            }
        }

        if (fill) {
            HudFlow(itemSpacing, lineSpacing, container) { Items(Modifier) }
        } else {
            FlowRow(container, horizontalArrangement = itemSpacing, verticalArrangement = Arrangement.spacedBy(lineSpacing)) {
                Items(Modifier.align(Alignment.CenterVertically))
            }
        }
        return
    }

    val labelWidth = (36 * s).dp
    val graphWidth = (168 * s).dp
    Column(
        modifier = modifier
            .background(background, shape)
            .padding(horizontal = (9 * s).dp, vertical = (6 * s).dp),
        verticalArrangement = Arrangement.spacedBy((2 * s).dp),
    ) {
        var i = 0
        while (i < shown.size) {
            val entry = shown[i]
            when (entry) {
                HudItem.FPS, HudItem.FRAME_TIME -> {
                    // FPS and frame time share a row when they're next to each other
                    val paired = shown.getOrNull(i + 1).let { it == HudItem.FPS || it == HudItem.FRAME_TIME }
                    val withFps = entry == HudItem.FPS || paired
                    val withMs = (entry == HudItem.FRAME_TIME || paired) && intervalMs != null
                    val segs = buildList {
                        if (withFps) {
                            add(Seg(if (stats.fps > 0) fmt1(stats.fps) else "--", color = fpsColor(stats.fps, target), bold = true))
                            add(unit(" FPS"))
                        }
                        if (withMs && intervalMs != null) {
                            if (isNotEmpty()) add(gap())
                            add(value(fmt1(intervalMs)))
                            add(unit("ms"))
                        }
                        if (withFps && stats.fps > 0) {
                            add(gap())
                            add(Seg("${(stats.fps / target * 100).roundToInt()}%", color = HudColors.dim, small = true))
                        }
                    }
                    if (segs.isNotEmpty()) {
                        Row(verticalAlignment = Alignment.Bottom) {
                            Text(segsText(segs, base * 1.25f), style = hudStyle(base * 1.25f), maxLines = lines, softWrap = fill)
                        }
                    }
                    i += if (paired) 2 else 1
                }
                HudItem.GRAPH -> {
                    if (frameTimes.size >= 2) {
                        val title = segsText(listOf(Seg("frametime", color = HudColors.frametime.copy(alpha = 0.85f), small = true)), base)
                        val peak = frameTimes.maxOrNull()?.let { segsText(listOf(Seg("max ${fmt1(it)} ms", color = HudColors.dim, small = true)), base) }
                        if (fill) {
                            // Too narrow for both, the peak takes a line of its own rather than breaking.
                            HudFlow(Arrangement.SpaceBetween, 0.dp, Modifier.fillMaxWidth()) {
                                Text(title, style = hudStyle(base))
                                if (peak != null) Text(peak, style = hudStyle(base), modifier = Modifier.padding(start = (6 * s).dp))
                            }
                        } else {
                            Row(Modifier.width(graphWidth), verticalAlignment = Alignment.CenterVertically) {
                                Text(title, style = hudStyle(base))
                                Spacer(Modifier.weight(1f))
                                if (peak != null) Text(peak, style = hudStyle(base))
                            }
                        }
                        FrameTimeGraph(frameTimes, targetMs, (if (fill) Modifier.fillMaxWidth() else Modifier.width(graphWidth)).height((34 * s).dp))
                        Spacer(Modifier.height((2 * s).dp))
                    }
                    i++
                }
                else -> {
                    row(entry)?.let { (label, color, segs) ->
                        // A wrapped row keeps its label on its first line.
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                segsText(listOf(Seg(label, color = color, bold = true)), base * 0.9f),
                                style = hudStyle(base * 0.9f),
                                maxLines = 1,
                                softWrap = false,
                                modifier = if (fill) Modifier.width(labelWidth).alignByBaseline() else Modifier.width(labelWidth),
                            )
                            Text(
                                segsText(segs, base),
                                style = hudStyle(base),
                                maxLines = lines,
                                softWrap = fill,
                                modifier = if (fill) Modifier.weight(1f).alignByBaseline() else Modifier,
                            )
                        }
                    }
                    i++
                }
            }
        }
        if (shaderWarning) {
            Text(
                segsText(listOf(Seg("\u26A0 ${stats.pipelineFailures} shaders failed", color = HudColors.bad, bold = true)), base * 0.9f),
                style = hudStyle(base * 0.9f),
            )
        }
    }
}

/** Frame-interval line graph with a dashed target line; spikes over 1.5× target are marked. [modifier] sizes it. */
@Composable
private fun FrameTimeGraph(times: FloatArray, targetMs: Float, modifier: Modifier) {
    Canvas(modifier) {
        val top = max(targetMs * 2f, min(times.maxOrNull() ?: targetMs, targetMs * 4f))
        fun yOf(t: Float) = size.height * (1f - (t.coerceIn(0f, top) / top))
        drawLine(
            color = Color.White.copy(alpha = 0.28f),
            start = Offset(0f, yOf(targetMs)),
            end = Offset(size.width, yOf(targetMs)),
            strokeWidth = 1.dp.toPx(),
            pathEffect = PathEffect.dashPathEffect(floatArrayOf(4.dp.toPx(), 3.dp.toPx())),
        )
        val n = times.size
        if (n < 2) return@Canvas
        val step = size.width / (n - 1)
        val line = Path()
        for (i in 0 until n) {
            val x = i * step
            val y = yOf(times[i])
            if (i == 0) line.moveTo(x, y) else line.lineTo(x, y)
        }
        val fill = Path().apply {
            addPath(line)
            lineTo(size.width, size.height)
            lineTo(0f, size.height)
            close()
        }
        drawPath(fill, HudColors.frametime.copy(alpha = 0.12f))
        drawPath(line, HudColors.frametime, style = Stroke(width = 1.4.dp.toPx(), cap = StrokeCap.Round))
        for (i in 0 until n) {
            if (times[i] > targetMs * 1.5f) drawCircle(HudColors.bad, radius = 1.7.dp.toPx(), center = Offset(i * step, yOf(times[i])))
        }
    }
}

/**
 * Children in lines, like FlowRow, for a box sized on screen: each child takes its full width,
 * or the line's if that's less (its text wraps there), and starts a new line when the current
 * one has no room for it; [horizontalArrangement] spaces each line, and each child is centered
 * in its line's height. Its intrinsic height follows the same rules, so a box can fit its text to
 * it; FlowRow's assumes every child shrinks to its narrowest, which a child here never does.
 */
@Composable
private fun HudFlow(horizontalArrangement: Arrangement.Horizontal, lineSpacing: Dp, modifier: Modifier, content: @Composable () -> Unit) {
    val policy = remember(horizontalArrangement, lineSpacing) { HudFlowPolicy(horizontalArrangement, lineSpacing) }
    Layout(content = content, modifier = modifier, measurePolicy = policy)
}

private class HudFlowPolicy(private val arrangement: Arrangement.Horizontal, private val lineSpacing: Dp) : MeasurePolicy {
    /** The children in each line, as index ranges, for children [widths] wide in lines [maxWidth] wide. */
    private fun Density.lines(widths: List<Int>, maxWidth: Int): List<IntRange> {
        val gap = arrangement.spacing.roundToPx()
        val lines = ArrayList<IntRange>()
        var start = 0
        var x = 0
        for (i in widths.indices) {
            if (i > start && x + gap + widths[i] > maxWidth) {
                lines += start until i
                start = i
                x = widths[i]
            } else {
                x = if (i == start) widths[i] else x + gap + widths[i]
            }
        }
        if (widths.isNotEmpty()) lines += start until widths.size
        return lines
    }

    private fun lineWidth(child: IntrinsicMeasurable, maxWidth: Int): Int = min(child.maxIntrinsicWidth(Constraints.Infinity), maxWidth)

    override fun MeasureScope.measure(measurables: List<Measurable>, constraints: Constraints): MeasureResult {
        val maxWidth = constraints.maxWidth
        val placeables = measurables.map { it.measure(Constraints(maxWidth = maxWidth)) }
        val lines = lines(measurables.map { lineWidth(it, maxWidth) }, maxWidth)
        val heights = lines.map { line -> line.maxOf { placeables[it].height } }
        val gap = arrangement.spacing.roundToPx()
        val spacing = lineSpacing.roundToPx()
        val width = if (constraints.hasBoundedWidth) maxWidth else {
            lines.maxOfOrNull { line -> line.sumOf { placeables[it].width } + gap * (line.last - line.first) } ?: 0
        }
        val height = heights.sum() + spacing * (lines.size - 1).coerceAtLeast(0)
        return layout(width.coerceIn(constraints.minWidth, constraints.maxWidth), height.coerceIn(constraints.minHeight, constraints.maxHeight)) {
            var y = 0
            lines.forEachIndexed { n, line ->
                val sizes = IntArray(line.last - line.first + 1) { placeables[line.first + it].width }
                val xs = IntArray(sizes.size)
                with(arrangement) { arrange(width, sizes, layoutDirection, xs) }
                for (k in sizes.indices) {
                    val child = placeables[line.first + k]
                    child.place(xs[k], y + (heights[n] - child.height) / 2)
                }
                y += heights[n] + spacing
            }
        }
    }

    override fun IntrinsicMeasureScope.minIntrinsicHeight(measurables: List<IntrinsicMeasurable>, width: Int): Int {
        val widths = measurables.map { lineWidth(it, width) }
        val lines = lines(widths, width)
        return lines.sumOf { line -> line.maxOf { measurables[it].minIntrinsicHeight(widths[it]) } } +
            lineSpacing.roundToPx() * (lines.size - 1).coerceAtLeast(0)
    }

    override fun IntrinsicMeasureScope.maxIntrinsicHeight(measurables: List<IntrinsicMeasurable>, width: Int): Int =
        minIntrinsicHeight(measurables, width)

    override fun IntrinsicMeasureScope.minIntrinsicWidth(measurables: List<IntrinsicMeasurable>, height: Int): Int =
        measurables.maxOfOrNull { it.minIntrinsicWidth(height) } ?: 0

    override fun IntrinsicMeasureScope.maxIntrinsicWidth(measurables: List<IntrinsicMeasurable>, height: Int): Int =
        measurables.sumOf { it.maxIntrinsicWidth(height) } + arrangement.spacing.roundToPx() * (measurables.size - 1).coerceAtLeast(0)
}

/** How long a finger rests on the monitor before it opens for moving and resizing. */
private const val HUD_HOLD_MS = 2000

/** The text scales a box sized on screen fits its contents between; the low end is the Size slider's. */
private const val HUD_MIN_SCALE = 0.6f
private const val HUD_MAX_SCALE = 2.5f

/** The Size slider's range, which a pinch on a box of automatic size moves along too. */
private val HUD_AUTO_SCALES = 0.6f..2.0f

/**
 * In-game host for [PerformanceHud]: samples telemetry off the main thread, polls the
 * frame-time history and places the HUD as [placement] says, in the space it's given above
 * [bottomLimit], where an on-screen keyboard takes the screen. Touches go through it to whatever
 * is under it.
 * A finger held still on it for [HUD_HOLD_MS], where [isControlAt] (a point in this overlay's
 * coordinates) finds no control, calls [onEditRequest], and can go on to drag it without lifting;
 * while [editing], the HUD can be moved, resized and reshaped, and [onEditDone] gets the result,
 * or null when cancelled.
 */
@Composable
fun PerformanceHudOverlay(
    stats: PerformanceStats,
    config: HudConfig,
    placement: HudPlacement,
    systemName: String,
    resolution: String?,
    editing: Boolean,
    onEditRequest: () -> Unit,
    onEditDone: (HudEdit?) -> Unit,
    modifier: Modifier = Modifier,
    isControlAt: (Offset) -> Boolean = { false },
    bottomLimit: Float = Float.POSITIVE_INFINITY,
) {
    val context = LocalContext.current
    val sampler = remember { HudTelemetrySampler(context) }
    val latestStats by rememberUpdatedState(stats)
    val latestConfig by rememberUpdatedState(config)
    val latestEditing by rememberUpdatedState(editing)
    var host by remember { mutableStateOf(HostSample()) }
    var frameTimes by remember { mutableStateOf(FloatArray(0)) }

    // While editing, the readings hold still so the box doesn't change size under the finger.
    val wantsHost = config.cpu || config.cpuDetail || config.gpu || config.ram || config.battery || config.thermal
    LaunchedEffect(wantsHost) {
        if (!wantsHost) return@LaunchedEffect
        while (isActive) {
            if (!latestEditing) {
                val sample = withContext(Dispatchers.IO) {
                    runCatching { sampler.sample(latestConfig, latestStats.emuTid, latestStats.activeCore) }.getOrNull()
                }
                if (sample != null) host = sample
            }
            delay(1000)
        }
    }
    LaunchedEffect(config.graph || config.frameTime) {
        if (!config.graph && !config.frameTime) return@LaunchedEffect
        while (isActive) {
            if (!latestEditing) {
                val times = withContext(Dispatchers.IO) { runCatching { PhobosCore.getFrameTimes() }.getOrNull() }
                if (times != null) frameTimes = times
            }
            delay(250)
        }
    }
    val heldStats = remember(editing) { stats }
    val shownStats = if (editing) heldStats else stats

    // What Done saved stands in until the settings bring it back, so the box doesn't jump back meanwhile.
    var saved by remember { mutableStateOf<HudEdit?>(null) }
    LaunchedEffect(placement, config.scale) { saved = null }
    val current = saved?.let(placement::applying) ?: placement
    val currentScale = saved?.scale ?: config.scale

    // Placed in the space this overlay is given, which can differ from the display's.
    BoxWithConstraints(modifier) {
        val landscape = constraints.maxWidth >= constraints.maxHeight
        val screen = Size(constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat())
        val area = Size(screen.width, min(screen.height, bottomLimit).coerceAtLeast(0f))
        val areaRect = Rect(Offset.Zero, area)
        val minSize = rememberHudMinSize(config.horizontal)
        val savedSize = current.size(landscape)?.toPx(screen)

        // The edit mode's working copy, taken from what's saved by the hold that opens it, and again
        // on a rotation, since Done saves it for the orientation it's in then.
        var editPosition by remember(landscape) { mutableStateOf(current.position) }
        var editCustom by remember(landscape) { mutableStateOf(current.custom(landscape)) }
        var editSize by remember(landscape) { mutableStateOf(savedSize) }
        var editScale by remember(landscape) { mutableFloatStateOf(currentScale) }
        val gestureStart = remember { HudGestureStart() }

        fun beginEdit() {
            editPosition = current.position
            editCustom = current.custom(landscape)
            editSize = savedSize
            editScale = currentScale
        }

        val position = if (editing) editPosition else current.position
        val fractions = position.fractions(config.horizontal) ?: if (editing) editCustom else current.custom(landscape)
        val boxSize = (if (editing) editSize else savedSize)?.let { clampHudSize(it, minSize, area) }
        val scale = if (editing) editScale else currentScale

        var measured by remember { mutableStateOf(Size.Zero) }
        val placedSize = boxSize ?: measured
        val offset = hudOffset(fractions, placedSize, area)
        val box = Rect(offset, placedSize)
        val latestBox by rememberUpdatedState(box)
        val latestIsControlAt by rememberUpdatedState(isControlAt)
        var holding by remember { mutableStateOf(false) }
        // The finger whose hold opened the edit mode is still down.
        var holdDown by remember { mutableStateOf(false) }

        fun startGesture() {
            gestureStart.box = latestBox
            gestureStart.sized = editSize != null
            gestureStart.scale = editScale
        }

        fun move(travel: Offset) {
            val moved = moveHudBox(gestureStart.box, travel, areaRect)
            editPosition = HudPosition.CUSTOM
            editCustom = hudFractions(moved.topLeft, moved.size, area)
        }

        val holdProgress by animateFloatAsState(
            targetValue = if (holding) 1f else 0f,
            animationSpec = tween(if (holding) HUD_HOLD_MS else 150, easing = LinearEasing),
            label = "hudHold",
        )

        if (editing) Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.45f)))
        Box(
            Modifier
                .offset { offset.round() }
                .onSizeChanged { measured = it.toSize() }
                .then(
                    HudHoldElement(
                        enabled = !editing,
                        // From where the HUD is placed, so the point is the one the controls get.
                        isControlAt = { latestIsControlAt(latestBox.topLeft.round().toOffset() + it) },
                        onHolding = { holding = it },
                        onHold = {
                            beginEdit()
                            holdDown = true
                            onEditRequest()
                        },
                        onHeldDragStart = ::startGesture,
                        onHeldDrag = ::move,
                        onHeldEnd = { holdDown = false },
                    )
                )
                .drawWithContent {
                    drawContent()
                    if (holdProgress > 0f) {
                        drawRoundRect(
                            color = Color.White.copy(alpha = holdProgress),
                            cornerRadius = CornerRadius(8.dp.toPx()),
                            style = Stroke(width = 2.dp.toPx()),
                        )
                    }
                }
        ) {
            val systemLabel = hudSystemLabel(systemName)
            val sessionSeconds = shownStats.playTimeMs / 1000
            if (boxSize != null) {
                FitHud(IntSize(boxSize.width.roundToInt(), boxSize.height.roundToInt())) {
                    PerformanceHud(shownStats, host, frameTimes, config.copy(scale = 1f), systemLabel, resolution, sessionSeconds, fill = true)
                }
            } else {
                PerformanceHud(shownStats, host, frameTimes, config.copy(scale = scale), systemLabel, resolution, sessionSeconds)
            }
        }
        if (editing) {
            HudEditLayer(
                box = box,
                area = area,
                canResetSize = editSize != null,
                holdDown = holdDown,
                onGestureStart = ::startGesture,
                onMove = ::move,
                onResize = { handle, travel ->
                    val resized = resizeHudBox(gestureStart.box, handle, travel, minSize, areaRect, editPosition.centered(config.horizontal))
                    editSize = resized.size
                    // A preset keeps its place while the sides it's pinned by stay put.
                    val placed = editPosition.fractions(config.horizontal)?.let { hudOffset(it, resized.size, area) }
                    if (placed == null || (placed - resized.topLeft).getDistance() > 1f) {
                        editPosition = HudPosition.CUSTOM
                        editCustom = hudFractions(resized.topLeft, resized.size, area)
                    }
                },
                onPinch = { factor ->
                    if (gestureStart.sized) {
                        val scaled = scaleHudBox(gestureStart.box, factor, minSize, areaRect)
                        editSize = scaled.size
                        if (editPosition == HudPosition.CUSTOM) editCustom = hudFractions(scaled.topLeft, scaled.size, area)
                    } else {
                        editScale = (gestureStart.scale * factor).coerceIn(HUD_AUTO_SCALES)
                    }
                },
                onResetSize = { editSize = null },
                onCancel = { onEditDone(null) },
                onDone = {
                    val edit = HudEdit(
                        position = editPosition,
                        landscape = landscape,
                        custom = editCustom.takeIf { editPosition == HudPosition.CUSTOM },
                        size = editSize?.let { HudBoxSize.of(clampHudSize(it, minSize, area), screen) },
                        scale = editScale,
                    )
                    saved = edit
                    onEditDone(edit)
                },
            )
        }
    }
}

/** Where an edit gesture started from: the box, whether it had a size of its own, and its automatic scale. */
private class HudGestureStart {
    var box = Rect.Zero
    var sized = false
    var scale = 1f
}

/**
 * A box sized on screen: [content], the HUD at scale 1 filling the width it's given, is laid
 * out at the largest scale whose wrapped layout fits [size] (measured through its intrinsic
 * height) and drawn that much larger. What still doesn't fit at the smallest scale is clipped.
 */
@Composable
private fun FitHud(size: IntSize, content: @Composable () -> Unit) {
    Layout(content = content, modifier = Modifier.clipToBounds()) { measurables, _ ->
        val hud = measurables.first()
        val width = size.width.toFloat()
        val height = size.height.toFloat()
        val scale = fitHudScale(width, height, HUD_MIN_SCALE, HUD_MAX_SCALE) { unitWidth ->
            hud.minIntrinsicHeight(unitWidth.roundToInt().coerceAtLeast(1)).toFloat()
        }
        val placeable = hud.measure(
            Constraints.fixed((width / scale).roundToInt().coerceAtLeast(1), (height / scale).roundToInt().coerceAtLeast(1))
        )
        layout(size.width, size.height) {
            placeable.placeWithLayer(0, 0) {
                scaleX = scale
                scaleY = scale
                transformOrigin = TransformOrigin(0f, 0f)
            }
        }
    }
}

/** The smallest size the box may be given: room for the FPS reading at the smallest text scale. */
@Composable
private fun rememberHudMinSize(horizontal: Boolean): Size {
    val measurer = rememberTextMeasurer()
    val density = LocalDensity.current
    return remember(horizontal, density, measurer) {
        val base = (11f * HUD_MIN_SCALE).sp
        val size = if (horizontal) base * 1.15f else base * 1.25f
        val fps = measurer.measure(segsText(listOf(Seg("88.8", bold = true), unit(" FPS")), size), hudStyle(size), softWrap = false, maxLines = 1)
        val padding = if (horizontal) Size(16f, 8f) else Size(18f, 12f)
        with(density) {
            Size(
                fps.size.width + (padding.width * HUD_MIN_SCALE).dp.toPx(),
                fps.size.height + (padding.height * HUD_MIN_SCALE).dp.toPx(),
            )
        }
    }
}

private data class HudHoldElement(
    val enabled: Boolean,
    val isControlAt: (Offset) -> Boolean,
    val onHolding: (Boolean) -> Unit,
    val onHold: () -> Unit,
    val onHeldDragStart: () -> Unit,
    val onHeldDrag: (Offset) -> Unit,
    val onHeldEnd: () -> Unit,
) : ModifierNodeElement<HudHoldNode>() {
    override fun create() = HudHoldNode(enabled, isControlAt, onHolding, onHold, onHeldDragStart, onHeldDrag, onHeldEnd)

    override fun update(node: HudHoldNode) {
        node.enabled = enabled
        node.isControlAt = isControlAt
        node.onHolding = onHolding
        node.onHold = onHold
        node.onHeldDragStart = onHeldDragStart
        node.onHeldDrag = onHeldDrag
        node.onHeldEnd = onHeldEnd
    }
}

/**
 * Calls [onHold] when one finger rests on the HUD for [HUD_HOLD_MS] without moving, where
 * [isControlAt] (given the HUD's own coordinates) finds no control, and [onHolding] as such a hold
 * starts and stops. The finger can go on to drag the HUD without lifting: past the touch slop,
 * [onHeldDragStart], then [onHeldDrag] with its travel since the hold, in pixels on the screen,
 * and [onHeldEnd] as it lifts. The HUD shares every touch with the layers under it, so the game
 * and the controls get each touch as if it weren't there; only the rest of a hold that reached
 * [onHold] is consumed, so its lift doesn't count as a tap on the game. [enabled] is off while
 * the edit mode is open; a held finger still down as it closes goes back to the game.
 */
private class HudHoldNode(
    enabled: Boolean,
    var isControlAt: (Offset) -> Boolean,
    var onHolding: (Boolean) -> Unit,
    var onHold: () -> Unit,
    var onHeldDragStart: () -> Unit,
    var onHeldDrag: (Offset) -> Unit,
    var onHeldEnd: () -> Unit,
) : Modifier.Node(), PointerInputModifierNode, CompositionLocalConsumerModifierNode {
    var enabled = enabled
        set(value) {
            if (held && !field && value) letGo = true
            field = value
        }
    private var finger: PointerId? = null
    private var timer: Job? = null
    private var held = false
    private var dragging = false
    private var letGo = false

    // In the root's coordinates, since the HUD moves under the finger it follows.
    private var at = Offset.Zero
    private var downAt = Offset.Zero
    private var heldAt = Offset.Zero

    override fun onPointerEvent(pointerEvent: PointerEvent, pass: PointerEventPass, bounds: IntSize) {
        if (pass != PointerEventPass.Initial) return
        for (change in pointerEvent.changes) {
            if (finger == null && enabled && change.changedToDownIgnoreConsumed() && !isControlAt(change.position)) {
                finger = change.id
                held = false
                dragging = false
                downAt = requireLayoutCoordinates().localToRoot(change.position)
                onHolding(true)
                timer = coroutineScope.launch {
                    delay(HUD_HOLD_MS.toLong())
                    held = true
                    heldAt = at
                    onHolding(false)
                    onHold()
                }
            }
            if (change.id != finger) continue
            if (letGo || !change.pressed) {
                if (held && !letGo) change.consume()
                end()
                continue
            }
            if (held) change.consume()
            at = requireLayoutCoordinates().localToRoot(change.position)
            val slop = currentValueOf(LocalViewConfiguration).touchSlop
            if (!held) {
                if ((at - downAt).getDistance() > slop) end()
            } else {
                val travel = at - heldAt
                if (!dragging && travel.getDistance() > slop) {
                    dragging = true
                    onHeldDragStart()
                }
                if (dragging) onHeldDrag(travel)
            }
        }
    }

    override fun onCancelPointerInput() = end()

    override fun sharePointerInputWithSiblings() = true

    override fun onDetach() {
        timer?.cancel()
        timer = null
        finger = null
        held = false
        dragging = false
        letGo = false
    }

    private fun end() {
        timer?.cancel()
        timer = null
        if (finger != null && !held) onHolding(false)
        if (held) onHeldEnd()
        finger = null
        held = false
        dragging = false
        letGo = false
    }
}
