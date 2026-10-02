package com.phobos.emulator.ui.hud

import androidx.compose.ui.geometry.Offset
import com.phobos.emulator.data.EmulatorSettings

/** What the performance HUD shows and how it looks; built from [EmulatorSettings]. */
data class HudConfig(
    val fps: Boolean = true,
    val frameTime: Boolean = true,
    val graph: Boolean = true,
    val cpu: Boolean = true,
    /** Core index and clock next to CPU load. */
    val cpuDetail: Boolean = true,
    val gpu: Boolean = true,
    val ram: Boolean = true,
    val battery: Boolean = false,
    val thermal: Boolean = false,
    val system: Boolean = false,
    val clock: Boolean = false,
    val shaderFails: Boolean = false,
    val horizontal: Boolean = false,
    val opacity: Float = 0.55f,
    val scale: Float = 1f,
    /** The order the items are drawn in, left to right or top to bottom. */
    val order: List<HudItem> = HudItem.entries,
) {
    /** Whether [item] is switched on (the CPU row also shows for its core and clock alone). */
    fun shows(item: HudItem): Boolean = when (item) {
        HudItem.FPS -> fps
        HudItem.FRAME_TIME -> frameTime
        HudItem.GRAPH -> graph
        HudItem.CPU -> cpu || cpuDetail
        HudItem.GPU -> gpu
        HudItem.RAM -> ram
        HudItem.BATTERY -> battery
        HudItem.THERMAL -> thermal
        HudItem.SYSTEM -> system
        HudItem.CLOCK -> clock
    }

    /** True when this config shows the same metrics as [other] (layout and appearance ignored). */
    fun sameMetrics(other: HudConfig): Boolean =
        fps == other.fps && frameTime == other.frameTime && graph == other.graph &&
            cpu == other.cpu && cpuDetail == other.cpuDetail && gpu == other.gpu && ram == other.ram &&
            battery == other.battery && thermal == other.thermal && system == other.system &&
            clock == other.clock
}

/** The HUD's items, declared in their default order. */
enum class HudItem(val label: String) {
    FPS("FPS"),
    FRAME_TIME("Frame time"),
    GRAPH("Frame-time graph"),
    CPU("CPU"),
    GPU("GPU"),
    RAM("Memory"),
    BATTERY("Battery"),
    THERMAL("Thermal status"),
    SYSTEM("System & resolution"),
    CLOCK("Clock & session");

    companion object {
        /** A saved order ("FPS,GRAPH,…"): names it doesn't know are dropped, and items it lacks follow in their default order. */
        fun parseOrder(saved: String): List<HudItem> {
            val named = saved.split(',').mapNotNull { name -> entries.firstOrNull { it.name == name.trim() } }.distinct()
            return named + entries.filter { it !in named }
        }

        fun encodeOrder(order: List<HudItem>): String = order.joinToString(",") { it.name }
    }
}

/** This order with [item] moved [by] places, negative towards the start, stopping at either end. */
fun List<HudItem>.moved(item: HudItem, by: Int): List<HudItem> {
    val from = indexOf(item)
    if (from < 0) return this
    val to = (from + by).coerceIn(0, size - 1)
    if (to == from) return this
    return toMutableList().apply { removeAt(from); add(to, item) }
}

/** One-tap metric sets, like MangoHud/GameNative presets. Layout and appearance are kept. */
enum class HudPreset(val label: String) {
    FPS_ONLY("FPS only"),
    ESSENTIAL("Essential"),
    BATTERY("Battery"),
    FULL("Full");

    fun applyTo(config: HudConfig): HudConfig = when (this) {
        FPS_ONLY -> config.copy(
            fps = true, frameTime = false, graph = false, cpu = false, cpuDetail = false, gpu = false,
            ram = false, battery = false, thermal = false, system = false, clock = false,
        )
        ESSENTIAL -> config.copy(
            fps = true, frameTime = true, graph = true, cpu = true, cpuDetail = true, gpu = true,
            ram = true, battery = false, thermal = false, system = false, clock = false,
        )
        BATTERY -> config.copy(
            fps = true, frameTime = false, graph = false, cpu = false, cpuDetail = false, gpu = false,
            ram = false, battery = true, thermal = true, system = false, clock = true,
        )
        FULL -> config.copy(
            fps = true, frameTime = true, graph = true, cpu = true, cpuDetail = true, gpu = true,
            ram = true, battery = true, thermal = true, system = true, clock = true,
        )
    }

    companion object {
        /** The preset whose metrics [config] shows, or null for a custom selection. */
        fun matching(config: HudConfig): HudPreset? = entries.firstOrNull { it.applyTo(config).sameMetrics(config) }
    }
}

fun EmulatorSettings.hudConfig(): HudConfig = HudConfig(
    fps = perfShowFps,
    frameTime = perfShowFrameTime,
    graph = perfShowGraph,
    cpu = perfShowCpu,
    cpuDetail = perfShowCore,
    gpu = perfShowGpu,
    ram = perfShowRam,
    battery = perfShowBattery,
    thermal = perfShowThermal,
    system = perfShowSystem,
    clock = perfShowClock,
    shaderFails = perfShowShaderFails,
    horizontal = perfHudHorizontal,
    opacity = perfHudOpacity,
    scale = perfOverlayScale,
    order = HudItem.parseOrder(perfHudOrder),
)

/** Where the monitor goes. A position dragged in an older version stands in until it's moved again. */
fun EmulatorSettings.hudPlacement(): HudPlacement = HudPlacement(
    position = HudPosition.fromSetting(perfHudPosition, perfOverlayPosX, perfOverlayPosY),
    customLandscape = decodeHudFractions(perfHudPosLandscape),
    customPortrait = decodeHudFractions(perfHudPosPortrait),
    sizeLandscape = HudBoxSize.decode(perfHudSizeLandscape),
    sizePortrait = HudBoxSize.decode(perfHudSizePortrait),
    legacy = Offset(perfOverlayPosX.coerceIn(0f, 1f), perfOverlayPosY.coerceIn(0f, 1f)),
)
