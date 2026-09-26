package com.phobos.emulator.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.phobos.emulator.PerformanceStats
import com.phobos.emulator.ui.hud.HostSample
import com.phobos.emulator.ui.hud.HudConfig
import com.phobos.emulator.ui.hud.HudPreset
import com.phobos.emulator.ui.hud.PerformanceHud
import com.phobos.emulator.ui.hud.hudConfig
import java.util.Locale
import kotlin.math.sin

/**
 * Performance Monitor configuration: presets, layout and per-metric toggles, with a live
 * preview of the HUD using sample values.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PerformanceMonitorSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val config = settings.hudConfig()
    val activePreset = HudPreset.matching(config)

    PhobosScaffold(title = "Performance Monitor", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item { HudPreview(config) }
            item {
                SettingsCategory("Overlay") {
                    SettingsSwitchItem(
                        title = "Show Performance Monitor",
                        description = "In-game HUD with FPS, frame times and system stats. Drag it to move.",
                        checked = settings.showPerformanceMonitor,
                        onCheckedChange = { viewModel.setShowPerformanceMonitor(it) }
                    )
                }
            }
            item {
                SettingsCategory("Preset") {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState())
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        HudPreset.entries.forEach { preset ->
                            FilterChip(
                                selected = activePreset == preset,
                                onClick = { viewModel.applyPerfHudPreset(preset) },
                                label = { Text(preset.label) },
                            )
                        }
                        FilterChip(selected = activePreset == null, onClick = {}, enabled = activePreset == null, label = { Text("Custom") })
                    }
                }
            }
            item {
                SettingsCategory("Layout") {
                    SettingsSwitchItem(
                        title = "Horizontal",
                        description = "One compact line instead of a column",
                        checked = settings.perfHudHorizontal,
                        onCheckedChange = { viewModel.setPerfHudHorizontal(it) }
                    )
                    SettingsSliderItem(
                        title = "Size",
                        value = settings.perfOverlayScale,
                        range = 0.6f..2.0f,
                        format = { String.format(Locale.US, "%.1f\u00D7", it) },
                        onCommit = { viewModel.setPerfOverlayScale(it) },
                    )
                    SettingsSliderItem(
                        title = "Background opacity",
                        value = settings.perfHudOpacity,
                        range = 0f..0.9f,
                        onCommit = { viewModel.setPerfHudOpacity(it) },
                    )
                    OutlinedButton(
                        onClick = { viewModel.resetPerfOverlayPosition() },
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    ) { Text("Reset position (top left)") }
                }
            }
            item {
                SettingsCategory("Metrics") {
                    SettingsSwitchItem("FPS", "Frames per second and speed versus the system's native rate", settings.perfShowFps) { viewModel.setPerfShowFps(it) }
                    SettingsSwitchItem("Frame time", "Average time between frames", settings.perfShowFrameTime) { viewModel.setPerfShowFrameTime(it) }
                    SettingsSwitchItem("Frame-time graph", "Recent frame intervals; red dots mark stutters", settings.perfShowGraph) { viewModel.setPerfShowGraph(it) }
                    SettingsSwitchItem("CPU", "Emulation thread load (100% = one core fully busy) and CPU temperature", settings.perfShowCpu) { viewModel.setPerfShowCpu(it) }
                    SettingsSwitchItem("CPU core & clock", "Which core the emulator runs on and its current clock", settings.perfShowCore) { viewModel.setPerfShowCore(it) }
                    SettingsSwitchItem("GPU", "GPU load, clock and temperature", settings.perfShowGpu) { viewModel.setPerfShowGpu(it) }
                    SettingsSwitchItem("Memory", "Emulator memory and total system RAM in use", settings.perfShowRam) { viewModel.setPerfShowRam(it) }
                    SettingsSwitchItem("Battery", "Charge, power draw and battery temperature", settings.perfShowBattery) { viewModel.setPerfShowBattery(it) }
                    SettingsSwitchItem("Thermal status", "Android's thermal throttling state", settings.perfShowThermal) { viewModel.setPerfShowThermal(it) }
                    SettingsSwitchItem("System & resolution", "Running system and output resolution", settings.perfShowSystem) { viewModel.setPerfShowSystem(it) }
                    SettingsSwitchItem("Clock & session", "Time of day and time played", settings.perfShowClock) { viewModel.setPerfShowClock(it) }
                    SettingsSwitchItem("Shader failures (N64)", "Warn when the GPU driver fails to compile shaders", settings.perfShowShaderFails) { viewModel.setPerfShowShaderFails(it) }
                }
            }
            item {
                Text(
                    "Stats the device doesn't expose to apps (often GPU load and temperatures) are hidden automatically.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 4.dp),
                )
            }
        }
    }
}

/** The HUD over a stand-in game scene, using fixed sample values. */
@Composable
private fun HudPreview(config: HudConfig) {
    val frameTimes = remember {
        FloatArray(180) { i ->
            val wobble = (sin(i * 0.35) * 0.35 + sin(i * 0.09) * 0.25).toFloat()
            when (i) {
                62 -> 29.4f
                131 -> 24.8f
                else -> 16.68f + wobble
            }
        }
    }
    val stats = PerformanceStats(fps = 59.9, frameTime = 9.4, activeCore = 7, targetFps = 60.0)
    val host = HostSample(
        emuThreadCpuPercent = 86f, emuCoreFreqMhz = 3187, cpuTempC = 47f,
        gpuBusyPercent = 62, gpuFreqMhz = 680, gpuTempC = 44f,
        processRssMb = 412, systemUsedMb = 6350, systemTotalMb = 11480,
        batteryPercent = 82, batteryPowerW = 4.3f, batteryTempC = 36f,
        thermalStatus = 0, thermalHeadroom = 0.42f,
    )
    Column {
        Text("Preview (sample values)", style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
        Box(
            modifier = Modifier
                .padding(top = 8.dp)
                .fillMaxWidth()
                .heightIn(min = if (config.horizontal) 64.dp else 120.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(Brush.linearGradient(listOf(Color(0xFF1B3A5C), Color(0xFF3E7A4E), Color(0xFF8C6D3A)))),
            contentAlignment = Alignment.TopStart,
        ) {
            PerformanceHud(
                stats = stats,
                host = host,
                frameTimes = frameTimes,
                config = config,
                systemLabel = "N64",
                resolution = "640\u00D7480",
                sessionSeconds = 754,
                modifier = Modifier.padding(10.dp),
            )
        }
    }
}
