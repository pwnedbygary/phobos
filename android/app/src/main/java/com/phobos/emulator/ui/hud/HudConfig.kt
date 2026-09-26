package com.phobos.emulator.ui.hud

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
) {
    /** True when this config shows the same metrics as [other] (layout and appearance ignored). */
    fun sameMetrics(other: HudConfig): Boolean =
        fps == other.fps && frameTime == other.frameTime && graph == other.graph &&
            cpu == other.cpu && cpuDetail == other.cpuDetail && gpu == other.gpu && ram == other.ram &&
            battery == other.battery && thermal == other.thermal && system == other.system &&
            clock == other.clock
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
)
