package com.phobos.emulator.ui

import android.content.res.Configuration
import android.net.Uri
import android.widget.Toast
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
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Album
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.DoNotTouch
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.TouchApp
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
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
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import com.phobos.emulator.data.AspectRatioMode
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.input.ControlLevel
import com.phobos.emulator.ui.hud.hudConfig
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neonGlow
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.N64SaveFormat
import com.phobos.emulator.util.ZX_SCHEMES
import com.phobos.emulator.util.zxScheme
import kotlinx.coroutines.launch
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
    val slotPreview = rememberSlotPreview(viewModel, systemName, romName, currentSlot)
    // The 64DD takes disks and the PlayStation discs; other systems have no Disc button.
    val discLabel = when {
        systemName.contains("Nintendo 64") -> "Disk"
        systemName.contains("PlayStation") -> "Disc"
        else -> null
    }
    // A multi-disc game's Disc button lists its discs; otherwise it picks a file.
    val discs by viewModel.loadedDiscs.collectAsState()
    val discInDrive by viewModel.currentDisc.collectAsState()
    var choosingDisc by remember { mutableStateOf(false) }
    if (choosingDisc) {
        DiscChoiceDialog(
            title = "Change disc",
            discs = discs,
            selected = discInDrive,
            confirmLabel = "Insert",
            fullScreen = settings.fullScreenMode,
            inGame = true,
            onDismiss = { choosingDisc = false },
            onChoose = { disc ->
                choosingDisc = false
                if (disc != discInDrive) viewModel.changeDisc(context, disc)
            },
            inDrive = discInDrive,
            otherFile = { choosingDisc = false; diskLauncher.launch(arrayOf("*/*")) },
        )
    }
    // Composed here, not in the list, so the list can't dispose its pickers' results or dialogs.
    val saveTransfer = if (systemName.contains("Nintendo 64", ignoreCase = true)) rememberSaveTransfer(viewModel, settings) else null
    // The N64 Experimental, Performance Monitor and controller pages replace the main list (with a
    // back arrow) to keep the menu short.
    var experimentalOpen by remember { mutableStateOf(false) }
    var perfHudOpen by remember { mutableStateOf(false) }
    var controllerPage by remember { mutableStateOf<ControllerPage?>(null) }
    val game by viewModel.loadedGame.collectAsState()
    // Kept here, so coming back from a page returns to the same place in the list.
    val menuListState = rememberLazyListState()
    BackHandler(enabled = experimentalOpen || perfHudOpen || controllerPage != null) {
        experimentalOpen = false; perfHudOpen = false; controllerPage = null
    }

    Box(
        modifier = Modifier.fillMaxSize().background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.7f)).clickable(enabled = false) {},
        contentAlignment = Alignment.Center,
    ) {
        val menuShape = MaterialTheme.shapes.extraLarge
        Surface(
            modifier = Modifier
                .fillMaxWidth(0.85f)
                .fillMaxHeight(0.8f)
                .then(
                    when {
                        LocalPhobosTheme.current.retrowave -> Modifier.neonGlow(MaterialTheme.colorScheme.primary, menuShape, 0.8f)
                        else -> dialogEdge(menuShape)
                    },
                ),
            shape = menuShape,
            color = MaterialTheme.colorScheme.surfaceContainer,
            contentColor = MaterialTheme.colorScheme.onSurface,
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                val openPage = controllerPage
                val openGame = game
                if (openPage != null && openGame != null) {
                    ControllerMenuPage(viewModel, openGame, openPage, onBack = { controllerPage = null })
                } else if (experimentalOpen) {
                    N64ExperimentalSection(viewModel, settings, onBack = { experimentalOpen = false })
                } else if (perfHudOpen) {
                    PerfHudMenuSection(viewModel, settings, onBack = { perfHudOpen = false })
                } else {
                    Text("Emulation Paused", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 8.dp))
                    LazyColumn(modifier = Modifier.weight(1f), state = menuListState, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        item {
                            QuickActions(
                                canLoad = slotPreview != null,
                                touchControlsShown = settings.showTouchControls,
                                discLabel = discLabel,
                                onSave = { viewModel.saveState(systemName, romName, currentSlot); onResume() },
                                onLoad = { viewModel.loadState(systemName, romName, currentSlot); onResume() },
                                onScreenshot = { viewModel.takeScreenshot(systemName, romName) },
                                onToggleTouchControls = { viewModel.setShowTouchControls(!settings.showTouchControls) },
                                onDisc = { if (discs.size > 1) choosingDisc = true else diskLauncher.launch(arrayOf("*/*")) },
                                onReset = { viewModel.resetSystem(); onResume() },
                            )
                        }
                        item { SaveStateSection(viewModel, settings, systemName, romName, currentSlot, slotPreview) }
                        if (supportsFastBoot(systemName)) {
                            item {
                                MenuSection("Boot Options") {
                                    SettingsSwitchItem("Fast Boot", "Skip BIOS and intro animations", settings.fastBoot) { viewModel.setFastBoot(it) }
                                }
                            }
                        }
                        if (systemName == "Neo Geo CD") {
                            item {
                                MenuSection("Neo Geo CD") {
                                    NgcdLoadSpeedItem(settings.ngcdLoadSpeed) { viewModel.setNgcdLoadSpeed(it) }
                                }
                            }
                        }
                        if (systemName.contains("Nintendo 64", ignoreCase = true)) {
                            item { N64Section(viewModel, settings, onOpenExperimental = { experimentalOpen = true }) }
                            saveTransfer?.let { transfer -> item { SaveDataSection(transfer) } }
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
                        game?.let { running -> item { ControllerSection(running, settings, onOpen = { controllerPage = it }) } }
                        item { TouchControlsSection(viewModel, settings, onEditTouchLayout) }
                        item { DisplaySection(viewModel, settings, onOpenPerfHud = { perfHudOpen = true }) }
                    }
                }

                Spacer(Modifier.height(16.dp))
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    val ring = MaterialTheme.colorScheme.primary
                    OutlinedButton(onClick = onQuit, modifier = Modifier.weight(1f).focusRing(ring, pillShape()), shape = pillShape()) { Text("Quit Game") }
                    OutlinedButton(onClick = onLibrary, modifier = Modifier.weight(1f).focusRing(ring, pillShape()), shape = pillShape()) { Text("Library") }
                    Button(onClick = onResume, modifier = Modifier.weight(1f).focusRing(MaterialTheme.colorScheme.onPrimary, pillShape()), shape = pillShape()) { Text("Resume") }
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
    canLoad: Boolean, touchControlsShown: Boolean, discLabel: String?,
    onSave: () -> Unit, onLoad: () -> Unit, onScreenshot: () -> Unit,
    onToggleTouchControls: () -> Unit, onDisc: () -> Unit, onReset: () -> Unit,
) {
    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        QuickAction(Icons.Default.Save, "Save", onSave, Modifier.weight(1f))
        QuickAction(Icons.Default.Download, "Load", onLoad, Modifier.weight(1f), enabled = canLoad)
        QuickAction(Icons.Default.CameraAlt, "Shot", onScreenshot, Modifier.weight(1f))
        QuickAction(
            if (touchControlsShown) Icons.Default.TouchApp else Icons.Default.DoNotTouch,
            "Touch",
            onToggleTouchControls,
            Modifier.weight(1f).semantics { stateDescription = if (touchControlsShown) "Touch controls shown" else "Touch controls hidden" },
            outlined = !touchControlsShown,
        )
        if (discLabel != null) QuickAction(Icons.Default.Album, discLabel, onDisc, Modifier.weight(1f))
        QuickAction(Icons.Default.Refresh, "Reset", onReset, Modifier.weight(1f))
    }
}

/** [outlined] marks a toggle that is off. */
@Composable
private fun QuickAction(
    icon: ImageVector, label: String, onClick: () -> Unit, modifier: Modifier,
    enabled: Boolean = true, outlined: Boolean = false,
) {
    val buttonModifier = modifier.focusRing(MaterialTheme.colorScheme.primary, pillShape())
    val content: @Composable RowScope.() -> Unit = {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(icon, contentDescription = null)
            Text(label, style = MaterialTheme.typography.labelSmall, maxLines = 1)
        }
    }
    if (outlined) {
        OutlinedButton(
            onClick = onClick, modifier = buttonModifier, enabled = enabled, shape = pillShape(),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.onSurfaceVariant),
            contentPadding = ButtonDefaults.TextButtonContentPadding, content = content,
        )
    } else {
        FilledTonalButton(
            onClick = onClick, modifier = buttonModifier, enabled = enabled, shape = pillShape(),
            contentPadding = ButtonDefaults.TextButtonContentPadding, content = content,
        )
    }
}

/** The current slot's preview; null for an empty slot and while it loads. */
@Composable
private fun rememberSlotPreview(viewModel: MainViewModel, systemName: String, romName: String, slot: Int): StateSlotPreview? {
    val stateRevision by viewModel.stateRevision.collectAsState()
    // A preview belongs to the slot and save revision it was loaded for; while another one loads
    // there is none, so the menu doesn't show the previous slot and Load and Delete stay disabled.
    val previewKey = listOf(systemName, romName, slot, stateRevision)
    var loadedPreview by remember { mutableStateOf<Pair<List<Any>, StateSlotPreview?>?>(null) }
    LaunchedEffect(previewKey) {
        loadedPreview = previewKey to viewModel.stateSlotPreview(systemName, romName, slot)
    }
    return loadedPreview?.takeIf { it.first == previewKey }?.second
}

@Composable
private fun SaveStateSection(
    viewModel: MainViewModel, settings: EmulatorSettings, systemName: String, romName: String,
    currentSlot: Int, preview: StateSlotPreview?,
) {
    var confirmDelete by remember { mutableStateOf(false) }
    val slotName = if (currentSlot < 0) "Slot Auto" else "Slot $currentSlot"
    MenuSection("Save / Load States") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { viewModel.decrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Prev") }
                Text(slotName, style = MaterialTheme.typography.bodyLarge)
                IconButton(onClick = { viewModel.incrementSlot() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Next") }
            }
            OutlinedButton(
                onClick = { confirmDelete = true },
                enabled = preview != null,
                shape = pillShape(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
            ) { Text("Delete") }
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

    if (confirmDelete) {
        PhobosAlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = true); Text("Delete this save state?") },
            text = { Text("The state in $slotName is deleted for good.") },
            confirmButton = {
                TextButton(
                    onClick = { confirmDelete = false; viewModel.deleteState(systemName, romName, currentSlot) },
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("Cancel") } },
        )
    }
}

@Composable
private fun StateSlotPreviewRow(preview: StateSlotPreview?) {
    val image = remember(preview) { preview?.image?.asImageBitmap() }
    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier.width(120.dp).aspectRatio(4f / 3f)
                .clip(MaterialTheme.shapes.extraSmall).background(MaterialTheme.colorScheme.surfaceVariant),
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
            colors = transparentListItemColors(),
            modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary).clickable(onClick = onOpenExperimental),
        )
    }
}

/** N64 save import and export: [pickImport] opens the file picker, [chooseExport] the format dialog. */
private class SaveTransfer(val pickImport: () -> Unit, val chooseExport: () -> Unit)

@Composable
private fun SaveDataSection(transfer: SaveTransfer) {
    MenuSection("Save Data") {
        ListItem(
            headlineContent = { Text("Import Save") },
            supportingContent = { Text("From Mupen64Plus, RetroArch or a Phobos backup") },
            leadingContent = { Icon(Icons.Default.FileDownload, null) },
            colors = transparentListItemColors(),
            modifier = Modifier.clickable(onClick = transfer.pickImport),
        )
        ListItem(
            headlineContent = { Text("Export Save") },
            supportingContent = { Text("For Mupen64Plus, RetroArch or a backup") },
            leadingContent = { Icon(Icons.Default.FileUpload, null) },
            colors = transparentListItemColors(),
            modifier = Modifier.clickable(onClick = transfer.chooseExport),
        )
    }
}

private val N64SaveFormat.description: String
    get() = when (this) {
        N64SaveFormat.MUPEN64PLUS -> ".eep, .sra, .fla and .mpk files"
        N64SaveFormat.RETROARCH -> "One .srm file, named after the ROM"
        N64SaveFormat.PHOBOS -> "A folder of Phobos's save files, to keep as a backup"
    }

@Composable
private fun rememberSaveTransfer(viewModel: MainViewModel, settings: EmulatorSettings): SaveTransfer {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var importUris by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var cartridgeOrder by remember { mutableStateOf(false) }
    var preview by remember { mutableStateOf<SaveImportPreview?>(null) }
    var choosingFormat by remember { mutableStateOf(false) }
    var exportFormat by remember { mutableStateOf(N64SaveFormat.RETROARCH) }
    var conflict by remember { mutableStateOf<Pair<Uri, List<String>>?>(null) }
    var failure by remember { mutableStateOf<String?>(null) }

    fun refreshPreview() {
        val uris = importUris
        val order = cartridgeOrder
        scope.launch { preview = viewModel.previewSaveImport(uris, order) }
    }
    fun export(folder: Uri, replace: Boolean) {
        val format = exportFormat
        scope.launch {
            when (val result = viewModel.exportSave(folder, format, replace)) {
                is SaveExportResult.Done -> Toast.makeText(context, "Exported ${result.files.joinToString()}", Toast.LENGTH_LONG).show()
                is SaveExportResult.Existing -> conflict = folder to result.files
                is SaveExportResult.Failed -> failure = result.reason
            }
        }
    }
    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            importUris = uris
            cartridgeOrder = false
            refreshPreview()
        }
    }
    val exportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { folder ->
        if (folder != null) export(folder, replace = false)
    }

    preview?.let { current ->
        SaveImportDialog(
            preview = current, settings = settings, cartridgeOrder = cartridgeOrder,
            onCartridgeOrder = { cartridgeOrder = it; refreshPreview() },
            onImport = { preview = null; viewModel.importSave(context, current.plan) },
            onDismiss = { preview = null },
        )
    }
    if (choosingFormat) {
        PhobosAlertDialog(
            onDismissRequest = { choosingFormat = false },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = true); Text("Export save") },
            text = {
                Column {
                    for (format in N64SaveFormat.entries) {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth()
                                .selectable(selected = exportFormat == format, onClick = { exportFormat = format })
                                .padding(vertical = 4.dp),
                        ) {
                            RadioButton(selected = exportFormat == format, onClick = null)
                            Spacer(Modifier.width(8.dp))
                            Column {
                                Text(format.label, style = MaterialTheme.typography.bodyLarge)
                                Text(format.description, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { choosingFormat = false; exportLauncher.launch(null) }) { Text("Choose Folder") } },
            dismissButton = { TextButton(onClick = { choosingFormat = false }) { Text("Cancel") } },
        )
    }
    conflict?.let { (folder, files) ->
        PhobosAlertDialog(
            onDismissRequest = { conflict = null },
            title = {
                DialogSystemBars(settings.fullScreenMode, inGame = true)
                Text(if (files.size == 1) "Replace the existing file?" else "Replace ${files.size} existing files?")
            },
            text = { Text(files.joinToString("\n")) },
            confirmButton = {
                TextButton(
                    onClick = { conflict = null; export(folder, replace = true) },
                    colors = ButtonDefaults.textButtonColors(contentColor = MaterialTheme.colorScheme.error),
                ) { Text("Replace") }
            },
            dismissButton = { TextButton(onClick = { conflict = null }) { Text("Cancel") } },
        )
    }
    failure?.let { reason ->
        PhobosAlertDialog(
            onDismissRequest = { failure = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = true); Text("Couldn't export the save") },
            text = { Text(reason) },
            confirmButton = { TextButton(onClick = { failure = null }) { Text("OK") } },
        )
    }
    return remember(importLauncher) { SaveTransfer(pickImport = { importLauncher.launch(arrayOf("*/*")) }, chooseExport = { choosingFormat = true }) }
}

@Composable
private fun SaveImportDialog(
    preview: SaveImportPreview, settings: EmulatorSettings, cartridgeOrder: Boolean,
    onCartridgeOrder: (Boolean) -> Unit, onImport: () -> Unit, onDismiss: () -> Unit,
) {
    val canImport = preview.plan.writes.isNotEmpty()
    PhobosAlertDialog(
        onDismissRequest = onDismiss,
        title = { DialogSystemBars(settings.fullScreenMode, inGame = true); Text(if (canImport) "Import this save?" else "Nothing to import") },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((kind, source) in preview.plan.sources) Text("${kind.label}: $source", style = MaterialTheme.typography.bodyMedium)
                if (canImport) {
                    Text(
                        "What it replaces moves to a Backups folder beside the save first, with this game's auto-save " +
                            "state, which would bring the old save back. Then the game restarts with the imported save.",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
                val notes = preview.plan.notes + preview.unreadable.map { "${it.name}: ${it.reason}." }
                for (note in notes) Text(note, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                if (preview.offersCartridgeOrder) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth().clickable { onCartridgeOrder(!cartridgeOrder) },
                    ) {
                        Checkbox(checked = cartridgeOrder, onCheckedChange = onCartridgeOrder)
                        Text(
                            "The SRAM or FlashRAM file is a cartridge dump or from ares, already in N64 byte order",
                            style = MaterialTheme.typography.bodySmall,
                        )
                    }
                }
            }
        },
        confirmButton = {
            if (canImport) TextButton(onClick = onImport) { Text("Import") } else TextButton(onClick = onDismiss) { Text("OK") }
        },
        dismissButton = if (canImport) ({ TextButton(onClick = onDismiss) { Text("Cancel") } }) else null,
    )
}

@Composable
private fun ZxKeyboardSection(
    viewModel: MainViewModel, settings: EmulatorSettings,
    showKeyboard: Boolean, onKeyboardToggle: (Boolean) -> Unit,
    zxControlScheme: Int, onZxControlScheme: (Int) -> Unit,
) {
    MenuSection("ZX Spectrum") {
        SettingsSwitchItem("On-Screen Keyboard", "Show the compact keyboard to type LOAD etc.", showKeyboard) { onKeyboardToggle(it) }
        SettingsDropdownItem(
            title = "Control Scheme",
            description = "What the gamepad plays this game as: a joystick, or keys",
            current = zxControlScheme,
            options = ZX_SCHEMES.map { it.id },
            label = { zxScheme(it).label },
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
        val tape by viewModel.zxTape.collectAsState()
        if (tape.inserted) ZxTapeItem(tape, onPlaying = { viewModel.setZxTapePlaying(it) }, onRewind = { viewModel.rewindZxTape() })
        ZxTapeControlItem(settings.zxTapeAuto) { viewModel.setZxTapeAuto(it) }
        ZxLoadSpeedItem(settings.zxLoadSpeed) { viewModel.setZxLoadSpeed(it) }
        // Silences the tape-loading screech; the game still receives the EAR bit.
        SettingsSwitchItem("Mute Tape Audio", "Silence the loud tape-loading screech.", settings.zxTapeMuted) { viewModel.setZxTapeMuted(it) }
    }
}

/** Opens the Buttons and Hotkeys pages, and says when the game has settings of its own. */
@Composable
private fun ControllerSection(game: ControlLevel.Game, settings: EmulatorSettings, onOpen: (ControllerPage) -> Unit) {
    val overrides = settings.controlOverrides
    MenuSection("Controller") {
        ControllerPageLink(
            ControllerPage.BUTTONS,
            if (overrides.hasMappings(game)) "This game has buttons of its own" else "What each button does, here or for every game",
            onOpen,
        )
        ControllerPageLink(
            ControllerPage.HOTKEYS,
            if (overrides.hasHotkeys(game)) "This game has hotkeys of its own" else "Button combos for the menu, states and more",
            onOpen,
        )
    }
}

@Composable
private fun ControllerPageLink(page: ControllerPage, description: String, onOpen: (ControllerPage) -> Unit) {
    ListItem(
        headlineContent = { Text(page.title) },
        supportingContent = { Text(description) },
        trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
        colors = transparentListItemColors(),
        modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary).clickable { onOpen(page) },
    )
}

@Composable
private fun TouchControlsSection(viewModel: MainViewModel, settings: EmulatorSettings, onEditTouchLayout: () -> Unit) {
    // Opacity is kept per orientation; adjust the one in use.
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    MenuSection("Touch Controls") {
        if (landscape) {
            SettingsSliderItem("Opacity", settings.touch.opacity, 0.1f..1f) { v -> viewModel.updateTouchPrefs { it.copy(opacity = v) } }
        } else {
            SettingsSliderItem("Opacity", settings.touch.opacityPortrait, 0.1f..1f) { v ->
                viewModel.updateTouchPrefs { it.copy(opacityPortrait = v) }
            }
        }
        SettingsSliderItem("Size", settings.touch.scale, 0.6f..1.6f) { v -> viewModel.updateTouchPrefs { it.copy(scale = v) } }
        FilledTonalButton(onClick = onEditTouchLayout, modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp), shape = pillShape()) {
            Icon(Icons.Default.TouchApp, null); Spacer(Modifier.width(8.dp)); Text("Edit Layout")
        }
    }
}

@Composable
private fun DisplaySection(viewModel: MainViewModel, settings: EmulatorSettings, onOpenPerfHud: () -> Unit) {
    MenuSection("Display") {
        SettingsSwitchItem("Full Screen", "", settings.fullScreenMode) { viewModel.setFullScreenMode(it) }
        SettingsSwitchItem("Performance Monitor", "", settings.showPerformanceMonitor) { viewModel.setShowPerformanceMonitor(it) }
        ListItem(
            headlineContent = { Text("Performance Monitor Contents") },
            supportingContent = { Text("Preset, layout and which stats it shows") },
            trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
            colors = transparentListItemColors(),
            modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary).clickable(onClick = onOpenPerfHud),
        )
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
    Column(modifier = Modifier.padding(vertical = 4.dp)) {
        SectionHeader(title, onBackdrop = false)
        Surface(
            shape = MaterialTheme.shapes.large,
            color = MaterialTheme.colorScheme.surfaceContainerHigh,
            contentColor = MaterialTheme.colorScheme.onSurface,
        ) {
            Column(Modifier.fillMaxWidth().padding(8.dp), content = content)
        }
    }
}

// ─── Performance Monitor sub-menu ────────────────────────────────────────────
// The same controls as Settings > Performance Monitor, so the HUD can be changed without leaving
// the game.

@Composable
fun ColumnScope.PerfHudMenuSection(viewModel: MainViewModel, settings: EmulatorSettings, onBack: () -> Unit) {
    Row(modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
        Text("Performance Monitor", style = MaterialTheme.typography.headlineSmall)
    }
    LazyColumn(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item { HudPreview(settings.hudConfig(), onBackdrop = false) }
        item {
            MenuSection("Overlay") {
                SettingsSwitchItem("Show Performance Monitor", "Drag it to move.", settings.showPerformanceMonitor) { viewModel.setShowPerformanceMonitor(it) }
            }
        }
        item { MenuSection("Preset") { PerfHudPresetChips(viewModel, settings) } }
        item { MenuSection("Layout") { PerfHudLayoutItems(viewModel, settings) } }
        item { MenuSection("Metrics") { PerfHudMetricItems(viewModel, settings) } }
        item { MenuSection("Order") { PerfHudOrderItems(viewModel, settings) } }
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
            MenuSection("Speed hacks") {
                SettingsSwitchItem("Faster CPU sync", "4× interleave. Timing-sensitive games may misbehave. Applies immediately.", settings.n64FasterSync) { viewModel.setN64FasterSync(it) }
                SettingsSwitchItem("Skip cache timing", "No cache stall cycles (Mupen-style); contents stay emulated. Applies immediately.", settings.n64SkipCaches) { viewModel.setN64SkipCaches(it) }
                SettingsSwitchItem("RSP task mode", "RSP runs ahead of the CPU (Mupen-style). Applies immediately.", settings.n64RspTaskMode) { viewModel.setN64RspTaskMode(it) }
            }
        }
        item {
            MenuSection("Frame pacing") {
                SettingsSwitchItem("Keep the fast core busy", "Waits between frames on the fastest core instead of sleeping, keeping it at full clock. Fewer slowdowns, more battery drain and heat. Applies immediately.", settings.busyWaitPacing) { viewModel.setBusyWaitPacing(it) }
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
