package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun N64ExperimentalSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("N64 Experimental") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                SettingsCategory("Rendering") {
                    var upscaleExpanded by remember { mutableStateOf(false) }
                    val currentUpscaleText = when(settings.n64Upscale) {
                        2 -> "2x HD (480p/960i)"
                        4 -> "4x UHD (4K)"
                        else -> "1x Native (SD)"
                    }
                    ListItem(
                        headlineContent = { Text("N64 Resolution / Upscaling") },
                        supportingContent = { Text("Parallel-RDP internal rendering resolution") },
                        trailingContent = {
                            Box {
                                TextButton(onClick = { upscaleExpanded = true }) { Text(currentUpscaleText) }
                                DropdownMenu(expanded = upscaleExpanded, onDismissRequest = { upscaleExpanded = false }) {
                                    DropdownMenuItem(text = { Text("1x Native (SD)") }, onClick = { viewModel.setN64Upscale(1); upscaleExpanded = false })
                                    DropdownMenuItem(text = { Text("2x HD (480p/960i)") }, onClick = { viewModel.setN64Upscale(2); upscaleExpanded = false })
                                    DropdownMenuItem(text = { Text("4x UHD (4K)") }, onClick = { viewModel.setN64Upscale(4); upscaleExpanded = false })
                                }
                            }
                        }
                    )
                    SettingsSwitchItem(
                        title = "Disable VI Processing",
                        description = "Bypass VI post-processing. May fix shader compile errors on some GPUs.",
                        checked = settings.n64DisableVIProcessing,
                        onCheckedChange = { viewModel.setN64DisableVIProcessing(it) }
                    )
                    SettingsSwitchItem(
                        title = "Supersample Scanout",
                        description = "Downscale output from internal upscale for native-res display. Changes deinterlacing pipeline.",
                        checked = settings.n64SupersampleScanout,
                        onCheckedChange = { viewModel.setN64SupersampleScanout(it) }
                    )
                    SettingsSwitchItem(
                        title = "Weave Deinterlacing",
                        description = "Blend previous frame instead of upscale deinterlacing. Requires Supersample Scanout OFF.",
                        checked = settings.n64WeaveDeinterlacing,
                        onCheckedChange = { viewModel.setN64WeaveDeinterlacing(it) }
                    )
                }
            }
            item {
                SettingsCategory("Overclocking") {
                    var overclockExpanded by remember { mutableStateOf(false) }
                    ListItem(
                        headlineContent = { Text("VI Overclock") },
                        supportingContent = { Text("Run the N64's video interface faster so games render above 50/60Hz (game logic speeds up too, like Mupen64Plus FZ). Applies on next reset.") },
                        trailingContent = {
                            Box {
                                TextButton(onClick = { overclockExpanded = true }) { Text("${settings.n64ViOverclock / 100.0f}x") }
                                DropdownMenu(expanded = overclockExpanded, onDismissRequest = { overclockExpanded = false }) {
                                    listOf(100, 125, 150, 175, 200).forEach { pct ->
                                        DropdownMenuItem(
                                            text = { Text("${pct / 100.0f}x") },
                                            onClick = { viewModel.setN64ViOverclock(pct); overclockExpanded = false }
                                        )
                                    }
                                }
                            }
                        }
                    )
                    SettingsSwitchItem(
                        title = "Use default count per operation",
                        description = "Modifying this can change game timing. Lower values can overclock the game but cause instability.",
                        checked = settings.n64UseDefaultCountPerOp,
                        onCheckedChange = { viewModel.setN64UseDefaultCountPerOp(it) }
                    )
                    var countPerOpExpanded by remember { mutableStateOf(false) }
                    val countPerOpEnabled = !settings.n64UseDefaultCountPerOp
                    ListItem(
                        modifier = Modifier.alpha(if (countPerOpEnabled) 1f else 0.4f),
                        headlineContent = { Text("Count Per Operation") },
                        supportingContent = { Text("Count register advance per op. Default 2. 1 = overclock (may be unstable), 3 = underclock.") },
                        trailingContent = {
                            Box {
                                TextButton(
                                    enabled = countPerOpEnabled,
                                    onClick = { countPerOpExpanded = true }
                                ) { Text("${settings.n64CountPerOp}") }
                                DropdownMenu(expanded = countPerOpExpanded, onDismissRequest = { countPerOpExpanded = false }) {
                                    listOf(1, 2, 3).forEach { v ->
                                        DropdownMenuItem(
                                            text = { Text("$v") },
                                            onClick = { viewModel.setN64CountPerOp(v); countPerOpExpanded = false }
                                        )
                                    }
                                }
                            }
                        }
                    )
                    SettingsSwitchItem(
                        title = "Use default overclocking factor",
                        description = "Modifying this overclocks the R4300 processor by a factor of 2 each time the value is incremented. 0 is no overclock.",
                        checked = settings.n64UseDefaultCpuOverclock,
                        onCheckedChange = { viewModel.setN64UseDefaultCpuOverclock(it) }
                    )
                    var cpuOverclockExpanded by remember { mutableStateOf(false) }
                    val cpuOverclockEnabled = !settings.n64UseDefaultCpuOverclock
                    ListItem(
                        modifier = Modifier.alpha(if (cpuOverclockEnabled) 1f else 0.4f),
                        headlineContent = { Text("Overclocking Factor") },
                        supportingContent = { Text("Overclocks the R4300 by 2^factor (0 = none). Game logic runs faster at the same frame rate.") },
                        trailingContent = {
                            Box {
                                TextButton(
                                    enabled = cpuOverclockEnabled,
                                    onClick = { cpuOverclockExpanded = true }
                                ) { Text("${settings.n64CpuOverclock}") }
                                DropdownMenu(expanded = cpuOverclockExpanded, onDismissRequest = { cpuOverclockExpanded = false }) {
                                    listOf(0, 1, 2, 3, 4, 5).forEach { v ->
                                        DropdownMenuItem(
                                            text = { Text("$v") },
                                            onClick = { viewModel.setN64CpuOverclock(v); cpuOverclockExpanded = false }
                                        )
                                    }
                                }
                            }
                        }
                    )
                }
            }
            item {
                SettingsCategory("Diagnostics") {
                    SettingsSwitchItem(
                        title = "N64 Debug Logging",
                        description = "Per-second N64 PC + stall/hang dumps to logcat. Only for debugging freezes.",
                        checked = settings.n64DebugLogging,
                        onCheckedChange = { viewModel.setN64DebugLogging(it) }
                    )
                }
            }
        }
    }
}
