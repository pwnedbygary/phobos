package com.phobos.emulator.ui.hud

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.PerformanceStats
import com.phobos.emulator.PhobosCore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
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
    "Famicom" -> "NES"
    "Game Boy" -> "GB"
    "Game Boy Color" -> "GBC"
    "Game Boy Advance" -> "GBA"
    "Mega Drive" -> "MD"
    "Mega CD" -> "MCD"
    "Master System" -> "SMS"
    "Game Gear" -> "GG"
    "PC Engine", "SuperGrafx" -> "PCE"
    "PC Engine CD" -> "PCECD"
    "Neo Geo" -> "NEOGEO"
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
 * hidden. [frameTimes] are frame-to-frame intervals in ms, oldest first.
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
) {
    val s = config.scale.coerceIn(0.5f, 2.5f)
    val base = (11f * s).sp
    val target = if (stats.targetFps > 1.0) stats.targetFps else 60.0
    val targetMs = (1000.0 / target).toFloat()
    val intervalMs = recentIntervalMs(frameTimes) ?: if (stats.fps > 0) (1000.0 / stats.fps).toFloat() else null
    val background = Color(0xFF020202).copy(alpha = config.opacity.coerceIn(0f, 1f))
    val shape = RoundedCornerShape((8 * s).dp)

    val rows = buildList {
        if (config.cpu || config.cpuDetail) cpuSegs(config, stats, host)?.let { add(Triple("CPU", HudColors.cpu, it)) }
        if (config.gpu) gpuSegs(host)?.let { add(Triple("GPU", HudColors.gpu, it)) }
        if (config.ram) ramSegs(host)?.let { add(Triple("RAM", HudColors.ram, it)) }
        if (config.battery) batterySegs(host)?.let { add(Triple("BAT", HudColors.battery, it)) }
        if (config.thermal) thermalSegs(host)?.let { add(Triple("THM", HudColors.thermal, it)) }
        if (config.system) systemSegs(systemLabel, resolution)?.let { add(Triple("SYS", HudColors.engine, it)) }
        if (config.clock) add(Triple("TIME", HudColors.clock, clockSegs(sessionSeconds)))
    }
    val shaderWarning = config.shaderFails && stats.pipelineFailures > 0

    if (config.horizontal) {
        // Items flow onto another line instead of squeezing when they don't fit.
        FlowRow(
            modifier = modifier
                .background(background, shape)
                .padding(horizontal = (8 * s).dp, vertical = (4 * s).dp),
            horizontalArrangement = Arrangement.spacedBy((10 * s).dp),
            verticalArrangement = Arrangement.spacedBy((2 * s).dp),
        ) {
            val item = Modifier.align(Alignment.CenterVertically)
            if (config.fps) {
                Text(
                    segsText(
                        listOf(Seg(if (stats.fps > 0) fmt1(stats.fps) else "--", color = fpsColor(stats.fps, target), bold = true), unit(" FPS")),
                        base * 1.15f,
                    ),
                    style = hudStyle(base),
                    maxLines = 1,
                    softWrap = false,
                    modifier = item,
                )
            }
            if (config.frameTime && intervalMs != null) {
                Text(segsText(listOf(value(fmt1(intervalMs)), unit("ms")), base), style = hudStyle(base), maxLines = 1, softWrap = false, modifier = item)
            }
            for ((label, color, segs) in rows) {
                Text(
                    segsText(listOf(Seg("$label ", color = color, bold = true)) + segs, base),
                    style = hudStyle(base),
                    maxLines = 1,
                    softWrap = false,
                    modifier = item,
                )
            }
            if (config.graph && frameTimes.size >= 2) {
                Box(item) { FrameTimeGraph(frameTimes, targetMs, (64 * s).dp, (16 * s).dp) }
            }
            if (shaderWarning) {
                Text(
                    segsText(listOf(Seg("\u26A0 ${stats.pipelineFailures}", color = HudColors.bad, bold = true)), base),
                    style = hudStyle(base),
                    maxLines = 1,
                    softWrap = false,
                    modifier = item,
                )
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
        if (config.fps || (config.frameTime && intervalMs != null)) {
            Row(verticalAlignment = Alignment.Bottom) {
                val segs = buildList {
                    if (config.fps) {
                        add(Seg(if (stats.fps > 0) fmt1(stats.fps) else "--", color = fpsColor(stats.fps, target), bold = true))
                        add(unit(" FPS"))
                    }
                    if (config.frameTime && intervalMs != null) {
                        if (isNotEmpty()) add(gap())
                        add(value(fmt1(intervalMs)))
                        add(unit("ms"))
                    }
                    if (config.fps && stats.fps > 0) {
                        add(gap())
                        add(Seg("${(stats.fps / target * 100).roundToInt()}%", color = HudColors.dim, small = true))
                    }
                }
                Text(segsText(segs, base * 1.25f), style = hudStyle(base * 1.25f), maxLines = 1, softWrap = false)
            }
        }
        if (config.graph && frameTimes.size >= 2) {
            Row(Modifier.width(graphWidth), verticalAlignment = Alignment.CenterVertically) {
                Text(segsText(listOf(Seg("frametime", color = HudColors.frametime.copy(alpha = 0.85f), small = true)), base), style = hudStyle(base))
                Spacer(Modifier.weight(1f))
                frameTimes.maxOrNull()?.let { maxMs ->
                    Text(segsText(listOf(Seg("max ${fmt1(maxMs)} ms", color = HudColors.dim, small = true)), base), style = hudStyle(base))
                }
            }
            FrameTimeGraph(frameTimes, targetMs, graphWidth, (34 * s).dp)
            Spacer(Modifier.height((2 * s).dp))
        }
        for ((label, color, segs) in rows) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    segsText(listOf(Seg(label, color = color, bold = true)), base * 0.9f),
                    style = hudStyle(base * 0.9f),
                    maxLines = 1,
                    softWrap = false,
                    modifier = Modifier.width(labelWidth),
                )
                Text(segsText(segs, base), style = hudStyle(base), maxLines = 1, softWrap = false)
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

/** Frame-interval line graph with a dashed target line; spikes over 1.5× target are marked. */
@Composable
private fun FrameTimeGraph(times: FloatArray, targetMs: Float, width: Dp, height: Dp) {
    Canvas(Modifier.size(width, height)) {
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
 * In-game host for [PerformanceHud]: samples telemetry off the main thread, polls the
 * frame-time history, and lets the user drag the HUD (position persisted as a 0..1
 * fraction of the free space on each axis).
 */
@Composable
fun PerformanceHudOverlay(
    stats: PerformanceStats,
    config: HudConfig,
    systemName: String,
    resolution: String?,
    savedPosX: Float,
    savedPosY: Float,
    screenWidth: Int,
    screenHeight: Int,
    onPositionChanged: (Float, Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val sampler = remember { HudTelemetrySampler(context) }
    val latestStats by rememberUpdatedState(stats)
    val latestConfig by rememberUpdatedState(config)
    var host by remember { mutableStateOf(HostSample()) }
    var frameTimes by remember { mutableStateOf(FloatArray(0)) }

    val wantsHost = config.cpu || config.cpuDetail || config.gpu || config.ram || config.battery || config.thermal
    LaunchedEffect(wantsHost) {
        if (!wantsHost) return@LaunchedEffect
        while (isActive) {
            val sample = withContext(Dispatchers.IO) {
                runCatching { sampler.sample(latestConfig, latestStats.emuTid, latestStats.activeCore) }.getOrNull()
            }
            if (sample != null) host = sample
            delay(1000)
        }
    }
    LaunchedEffect(config.graph || config.frameTime) {
        if (!config.graph && !config.frameTime) return@LaunchedEffect
        while (isActive) {
            val times = withContext(Dispatchers.IO) { runCatching { PhobosCore.getFrameTimes() }.getOrNull() }
            if (times != null) frameTimes = times
            delay(250)
        }
    }

    var hudSize by remember { mutableStateOf(IntSize.Zero) }
    var dragPos by remember { mutableStateOf<Offset?>(null) }
    val maxX = max(0, screenWidth - hudSize.width).toFloat()
    val maxY = max(0, screenHeight - hudSize.height).toFloat()
    val latestMaxX by rememberUpdatedState(maxX)
    val latestMaxY by rememberUpdatedState(maxY)
    val latestSavedX by rememberUpdatedState(savedPosX)
    val latestSavedY by rememberUpdatedState(savedPosY)
    val pos = dragPos ?: Offset(savedPosX.coerceIn(0f, 1f) * maxX, savedPosY.coerceIn(0f, 1f) * maxY)

    Box(modifier) {
        Box(
            Modifier
                .offset { IntOffset(pos.x.coerceIn(0f, maxX).roundToInt(), pos.y.coerceIn(0f, maxY).roundToInt()) }
                .onSizeChanged { hudSize = it }
                .pointerInput(Unit) {
                    detectDragGestures(
                        onDragEnd = {
                            dragPos?.let {
                                onPositionChanged(
                                    if (latestMaxX > 0f) it.x / latestMaxX else 0f,
                                    if (latestMaxY > 0f) it.y / latestMaxY else 0f,
                                )
                            }
                        },
                    ) { change, drag ->
                        change.consume()
                        val start = dragPos ?: Offset(latestSavedX.coerceIn(0f, 1f) * latestMaxX, latestSavedY.coerceIn(0f, 1f) * latestMaxY)
                        dragPos = Offset(
                            (start.x + drag.x).coerceIn(0f, latestMaxX),
                            (start.y + drag.y).coerceIn(0f, latestMaxY),
                        )
                    }
                }
        ) {
            PerformanceHud(
                stats = stats,
                host = host,
                frameTimes = frameTimes,
                config = config,
                systemLabel = hudSystemLabel(systemName),
                resolution = resolution,
                sessionSeconds = stats.playTimeMs / 1000,
            )
        }
    }
}
