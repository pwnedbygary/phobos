package com.phobos.emulator.ui

import android.content.res.Configuration
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import java.text.DateFormat
import java.util.Date

/** In-game pause menu. [onEditTouchLayout] opens the touch layout editor over the paused game. */
@Composable
fun EmulationMenu(
    viewModel: MainViewModel, systemName: String, romName: String,
    showKeyboard: Boolean, onKeyboardToggle: (Boolean) -> Unit,
    onResume: () -> Unit, onQuit: () -> Unit, onLibrary: () -> Unit,
    onEditTouchLayout: () -> Unit,
    zxControlScheme: Int = 0, onZxControlScheme: (Int) -> Unit = {},
) {
    val settings by viewModel.settings.collectAsState()
    val currentSlot by viewModel.currentSlot.collectAsState()
    val context = LocalContext.current
    val diskLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.loadSecondaryRom(context, systemName, RomFile(uri.lastPathSegment ?: "Disk", uri))
    }
    // The N64 Experimental screen replaces the main list (with a back arrow) to keep the menu short.
    var experimentalOpen by remember { mutableStateOf(false) }
    BackHandler(enabled = experimentalOpen) { experimentalOpen = false }

    Box(
        modifier = Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.7f)).clickable(enabled = false) {},
        contentAlignment = Alignment.Center,
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(0.85f).fillMaxHeight(0.8f),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                if (experimentalOpen) {
                    N64ExperimentalSection(viewModel, settings, onBack = { experimentalOpen = false })
                } else {
                    Text("Emulation Paused", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 8.dp))
                    LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        item {
                            QuickActions(
                                onSave = { viewModel.saveState(systemName, romName, currentSlot); onResume() },
                                onLoad = { viewModel.loadState(systemName, romName, currentSlot); onResume() },
                                onScreenshot = { viewModel.takeScreenshot(systemName, romName) },
                                onEditControls = onEditTouchLayout,
                                onReset = { viewModel.resetSystem(); onResume() },
                            )
                        }
                        item { SaveStateSection(viewModel, settings, systemName, romName, currentSlot, onResume) }
                        if (supportsFastBoot(systemName)) {
                            item {
                                MenuSection("Boot Options") {
                                    SettingsSwitchItem("Fast Boot", "Skip BIOS and intro animations", settings.fastBoot) { viewModel.setFastBoot(it) }
                                }
                            }
                        }
                        if (systemName.contains("Nintendo 64") || systemName.contains("PlayStation")) {
                            item {
                                MenuSection("Disc Management") {
                                    Button(onClick = { diskLauncher.launch(arrayOf("*/*")) }, modifier = Modifier.fillMaxWidth()) {
                                        Icon(Icons.Default.Add, null); Spacer(Modifier.width(8.dp))
                                        Text(if (systemName.contains("Nintendo 64")) "Insert 64DD Disk" else "Change Disc")
                                    }
                                }
                            }
                        }
                        if (systemName.contains("Nintendo 64", ignoreCase = true)) {
                            item { N64Section(viewModel, settings, onOpenExperimental = { experimentalOpen = true }) }
                        }
                        if (systemName.contains("PlayStation", ignoreCase = true)) {
                            item {
                                MenuSection("DualShock") {
                                    var ps1Analog by remember { mutableStateOf(settings.ps1AnalogMode) }
                                    LaunchedEffect(settings.ps1AnalogMode) { ps1Analog = settings.ps1AnalogMode }
                                    SettingsSwitchItem("Analog Mode", "Toggle DualShock analog mode.", ps1Analog) {
                                        viewModel.togglePs1AnalogMode()
                                    }
                                }
                            }
                        }
                        if (systemName.contains("ZX Spectrum", ignoreCase = true)) {
                            item { ZxKeyboardSection(viewModel, settings, showKeyboard, onKeyboardToggle, zxControlScheme, onZxControlScheme) }
                        }
                        item { TouchControlsSection(viewModel, settings, onEditTouchLayout) }
                        item { DisplaySection(viewModel, settings) }
                        item {
                            MenuSection("System") {
                                Button(
                                    onClick = { viewModel.resetSystem(); onResume() },
                                    modifier = Modifier.fillMaxWidth(),
                                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
                                ) {
                                    Icon(Icons.Default.Refresh, null); Spacer(Modifier.width(8.dp)); Text("Reset System")
                                }
                            }
                        }
                    }
                }

                Spacer(Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    OutlinedButton(onClick = onQuit, modifier = Modifier.weight(1f)) { Text("Quit Game") }
                    OutlinedButton(onClick = onLibrary, modifier = Modifier.weight(1f)) { Text("Library") }
                    Button(onClick = onResume, modifier = Modifier.weight(1f)) { Text("Resume") }
                }
            }
        }
    }
}

/** ares supports Fast Boot only on GB/GBC, NGP/NGPC and PS1. */
private fun supportsFastBoot(systemName: String): Boolean =
    systemName == "Game Boy" || systemName == "Game Boy Color" ||
        systemName.contains("Neo Geo Pocket") || systemName == "PlayStation"

@Composable
private fun QuickActions(
    onSave: () -> Unit, onLoad: () -> Unit, onScreenshot: () -> Unit, onEditControls: () -> Unit, onReset: () -> Unit,
) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        QuickAction(Icons.Default.Save, "Save", onSave, Modifier.weight(1f))
        QuickAction(Icons.Default.Download, "Load", onLoad, Modifier.weight(1f))
        QuickAction(Icons.Default.CameraAlt, "Shot", onScreenshot, Modifier.weight(1f))
        QuickAction(Icons.Default.TouchApp, "Controls", onEditControls, Modifier.weight(1f))
        QuickAction(Icons.Default.Refresh, "Reset", onReset, Modifier.weight(1f))
    }
}

@Composable
private fun QuickAction(icon: ImageVector, label: String, onClick: () -> Unit, modifier: Modifier) {
    FilledTonalButton(onClick = onClick, modifier = modifier, contentPadding = ButtonDefaults.TextButtonContentPadding) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(icon, contentDescription = null)
            Text(label, style = MaterialTheme.typography.labelSmall, maxLines = 1)
        }
    }
}

@Composable
private fun SaveStateSection(
    viewModel: MainViewModel, settings: EmulatorSettings, systemName: String, romName: String,
    currentSlot: Int, onResume: () -> Unit,
) {
    val stateRevision by viewModel.stateRevision.collectAsState()
    var preview by remember { mutableStateOf<StateSlotPreview?>(null) }
    LaunchedEffect(systemName, romName, currentSlot, stateRevision) {
        preview = viewModel.stateSlotPreview(systemName, romName, currentSlot)
    }
    MenuSection("Save / Load States") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { viewModel.decrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Prev") }
                Text(if (currentSlot < 0) "Slot Auto" else "Slot $currentSlot", style = MaterialTheme.typography.bodyLarge)
                IconButton(onClick = { viewModel.incrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Next") }
            }
            Row {
                Button(onClick = { viewModel.saveState(systemName, romName, currentSlot); onResume() }) { Text("Save") }
                Spacer(Modifier.width(8.dp))
                Button(onClick = { viewModel.loadState(systemName, romName, currentSlot); onResume() }) { Text("Load") }
                Spacer(Modifier.width(8.dp))
                OutlinedButton(
                    onClick = { viewModel.deleteState(systemName, romName, currentSlot) },
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) { Text("Delete") }
            }
        }
        StateSlotPreviewRow(preview)
        // Auto-Save / Auto-Load STATE toggles (all cores), distinct from cartridge/flash save flushing.
        SettingsSwitchItem("Auto-Save State", "Save a state snapshot automatically when you quit a game", settings.autoSaveState) {
            viewModel.setAutoSaveState(it)
        }
        SettingsSwitchItem("Auto-Load State", "Restore the auto-saved state when a game is loaded", settings.autoLoadState) {
            viewModel.setAutoLoadState(it)
        }
    }
}

@Composable
private fun StateSlotPreviewRow(preview: StateSlotPreview?) {
    val image = remember(preview) { preview?.image?.asImageBitmap() }
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier.width(120.dp).aspectRatio(4f / 3f)
                .clip(RoundedCornerShape(8.dp)).background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center,
        ) {
            if (image != null) {
                Image(image, contentDescription = "Save state preview", contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
            } else {
                Text(
                    if (preview == null) "Empty" else "No preview",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        Spacer(Modifier.width(12.dp))
        Text(
            text = preview?.let { "Saved " + DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(it.savedAtMillis)) }
                ?: "No state in this slot",
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
private fun N64Section(viewModel: MainViewModel, settings: EmulatorSettings, onOpenExperimental: () -> Unit) {
    MenuSection("N64 Settings") {
        SettingsSwitchItem("Expansion Pak", "Increase RDRAM to 8MB.", settings.n64ExpansionPak) { viewModel.setN64ExpansionPak(it) }
        SettingsSwitchItem("CPU Recompiler", "Use JIT recompiler.", settings.n64Recompiler) { viewModel.setN64Recompiler(it) }
        SettingsDropdownItem(
            title = "Controller Pak",
            description = "Peripheral for Player 1 (hot-swappable).",
            current = settings.n64Pak,
            options = listOf("None", "Rumble Pak", "Controller Pak"),
            label = { it },
            onSelect = { viewModel.setN64Pak(it) },
        )
        ListItem(
            headlineContent = { Text("N64 Experimental") },
            supportingContent = { Text("Overclocking, VI rendering, debug logging") },
            trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
            modifier = Modifier.clickable(onClick = onOpenExperimental),
        )
    }
}

@Composable
private fun ZxKeyboardSection(
    viewModel: MainViewModel, settings: EmulatorSettings,
    showKeyboard: Boolean, onKeyboardToggle: (Boolean) -> Unit,
    zxControlScheme: Int, onZxControlScheme: (Int) -> Unit,
) {
    MenuSection("Keyboard") {
        SettingsSwitchItem("On-Screen Keyboard", "Show the compact keyboard to type LOAD etc.", showKeyboard) { onKeyboardToggle(it) }
        // Control schemes: 0=Kempston, 1=QAOP, 2=ZXZX, 3=ELITE, 4=CUSTOM (long-press a key to rebind).
        SettingsDropdownItem(
            title = "Control Scheme",
            description = "Kempston / QAOP / ZXZX / ELITE / CUSTOM",
            current = zxControlScheme,
            options = listOf(0, 1, 2, 3, 4),
            label = { ZX_SCHEME_LABELS[it] },
            onSelect = onZxControlScheme,
        )
        if (zxControlScheme == 4) {
            Text(
                "CUSTOM: long-press a key on the keyboard to bind it to a gamepad control.",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 4.dp),
            )
        }
        SettingsSliderItem("Keyboard Opacity", settings.zxKeyboardOpacity, 0.2f..1.0f) { viewModel.setZxKeyboardOpacity(it) }
        // Silences the tape-loading screech; the game still receives the EAR bit.
        SettingsSwitchItem("Mute Tape Audio", "Silence the loud tape-loading screech.", settings.zxTapeMuted) { viewModel.setZxTapeMuted(it) }
    }
}

private val ZX_SCHEME_LABELS = listOf("KEMP", "QAOP", "ZXZX", "ELITE", "CUSTOM")

@Composable
private fun TouchControlsSection(viewModel: MainViewModel, settings: EmulatorSettings, onEditTouchLayout: () -> Unit) {
    // Opacity is kept per orientation; adjust the one in use.
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    MenuSection("Touch Controls") {
        SettingsSwitchItem("Show Touch Controls", "", settings.showTouchControls) { viewModel.setShowTouchControls(it) }
        if (landscape) {
            SettingsSliderItem("Opacity", settings.touch.opacity, 0.1f..1f) { v -> viewModel.updateTouchPrefs { it.copy(opacity = v) } }
        } else {
            SettingsSliderItem("Opacity", settings.touch.opacityPortrait, 0.1f..1f) { v ->
                viewModel.updateTouchPrefs { it.copy(opacityPortrait = v) }
            }
        }
        SettingsSliderItem("Size", settings.touch.scale, 0.6f..1.6f) { v -> viewModel.updateTouchPrefs { it.copy(scale = v) } }
        FilledTonalButton(onClick = onEditTouchLayout, modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
            Icon(Icons.Default.TouchApp, null); Spacer(Modifier.width(8.dp)); Text("Edit Layout")
        }
    }
}

@Composable
private fun DisplaySection(viewModel: MainViewModel, settings: EmulatorSettings) {
    MenuSection("Display") {
        SettingsSwitchItem("Full Screen", "", settings.fullScreenMode) { viewModel.setFullScreenMode(it) }
        SettingsSwitchItem("Performance Monitor", "", settings.showPerformanceMonitor) { viewModel.setShowPerformanceMonitor(it) }
        SettingsDropdownItem(
            title = "Aspect Ratio",
            description = null,
            current = settings.aspectRatioMode,
            options = AspectRatioMode.entries,
            label = { it.label },
            onSelect = { viewModel.setAspectRatioMode(it) },
        )
    }
}

// ─── Shared menu building blocks ─────────────────────────────────────────────

@Composable
fun MenuSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier = Modifier.padding(vertical = 8.dp)) {
        Text(title, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
        Spacer(Modifier.height(4.dp)); content()
        HorizontalDivider(modifier = Modifier.padding(top = 8.dp), thickness = 0.5.dp)
    }
}

// ─── N64 Experimental sub-menu ───────────────────────────────────────────────
// Shown in place of the pause list (with a back arrow), mirroring the "Experimental (N64
// Vulkan)" section in Settings so every N64 tuning knob stays one tap away.

@Composable
fun ColumnScope.N64ExperimentalSection(viewModel: MainViewModel, settings: EmulatorSettings, onBack: () -> Unit) {
    Row(modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
        Text("N64 Experimental", style = MaterialTheme.typography.headlineSmall)
    }
    LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item {
            MenuSection("Rendering") {
                SettingsDropdownItem(
                    title = "Internal Upscale", description = null, current = settings.n64Upscale,
                    options = listOf(1, 2, 4), label = { "${it}x" }, onSelect = { viewModel.setN64Upscale(it) },
                )
                SettingsSwitchItem("Disable VI Process", "Bypass VI post-processing. May fix GPU shader errors.", settings.n64DisableVIProcessing) { viewModel.setN64DisableVIProcessing(it) }
                SettingsSwitchItem("Supersample Scanout", "Downscale internal upscale for native output.", settings.n64SupersampleScanout) { viewModel.setN64SupersampleScanout(it) }
                SettingsSwitchItem("Weave Deinterlace", "Blend deinterlace (needs Supersample OFF).", settings.n64WeaveDeinterlacing) { viewModel.setN64WeaveDeinterlacing(it) }
                SettingsSwitchItem("Asynchronous RDP", "Skip the GPU wait at each full sync. Faster; frame read-back effects may glitch. Applies immediately.", settings.n64AsyncRdp) { viewModel.setN64AsyncRdp(it) }
            }
        }
        item {
            MenuSection("Overclocking") {
                SettingsDropdownItem(
                    title = "VI Overclock",
                    description = "Run VI faster so games render above 50/60Hz (game logic speeds up too).",
                    current = settings.n64ViOverclock,
                    options = listOf(100, 125, 150, 175, 200),
                    label = { "${it / 100.0f}x" },
                    onSelect = { viewModel.setN64ViOverclock(it) },
                )
                SettingsSwitchItem("Use default count per op", "Modifying this can change game timing. Lower values can overclock the game but cause instability.", settings.n64UseDefaultCountPerOp) { viewModel.setN64UseDefaultCountPerOp(it) }
                SettingsDropdownItem(
                    title = "Count Per Operation",
                    description = "Default 2. 1 = overclock (may be unstable), 3 = underclock.",
                    current = settings.n64CountPerOp,
                    options = listOf(1, 2, 3),
                    label = { "$it" },
                    onSelect = { viewModel.setN64CountPerOp(it) },
                    enabled = !settings.n64UseDefaultCountPerOp,
                )
                SettingsSwitchItem("Use default overclocking factor", "Modifying this overclocks the R4300 by a factor of 2 each increment. 0 is no overclock.", settings.n64UseDefaultCpuOverclock) { viewModel.setN64UseDefaultCpuOverclock(it) }
                SettingsDropdownItem(
                    title = "Overclocking Factor",
                    description = "Overclocks the R4300 by 2^factor (0 = none). Game logic faster at same frame rate.",
                    current = settings.n64CpuOverclock,
                    options = listOf(0, 1, 2, 3, 4, 5),
                    label = { "$it" },
                    onSelect = { viewModel.setN64CpuOverclock(it) },
                    enabled = !settings.n64UseDefaultCpuOverclock,
                )
            }
        }
        item {
            MenuSection("Diagnostics") {
                SettingsSwitchItem("N64 Debug Logging", "Per-second N64 PC + stall dumps to logcat.", settings.n64DebugLogging) { viewModel.setN64DebugLogging(it) }
            }
        }
    }
}
