package com.phobos.emulator.ui

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.clickable
import androidx.compose.foundation.background
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.phobos.emulator.util.DRIVER_SOURCES
import com.phobos.emulator.util.DriverAsset
import com.phobos.emulator.util.DriverSource
import kotlinx.coroutines.launch

private enum class DownloadStep { Sources, Assets }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DriverManagerScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) viewModel.installCustomDriver(context, uri)
    }

    // Download modal state
    var showDownload by remember { mutableStateOf(false) }
    var downloadStep by remember { mutableStateOf(DownloadStep.Sources) }
    var assets by remember { mutableStateOf<List<DriverAsset>>(emptyList()) }
    var assetsSource by remember { mutableStateOf<DriverSource?>(null) }
    var isFetching by remember { mutableStateOf(false) }
    var fetchError by remember { mutableStateOf<String?>(null) }
    val progress by viewModel.downloadProgress.collectAsState()

    // Delete modal state — `installed` also drives the Active Driver selector list.
    var showDelete by remember { mutableStateOf(false) }
    var installed by remember { mutableStateOf<List<InstalledDriver>>(emptyList()) }

    fun refreshInstalled() { scope.launch { installed = viewModel.getInstalledDrivers() } }

    // Surface download/delete outcomes as toasts.
    LaunchedEffect(Unit) {
        refreshInstalled()
        launch {
            viewModel.driverErrorEvent.collect { Toast.makeText(context, it, Toast.LENGTH_LONG).show() }
        }
        launch {
            viewModel.driverSuccessEvent.collect {
                Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
                showDownload = false
                refreshInstalled()
            }
        }
    }

    fun openDelete() {
        scope.launch { installed = viewModel.getInstalledDrivers() }
        showDelete = true
    }

    fun openAssets(source: DriverSource) {
        isFetching = true
        fetchError = null
        scope.launch {
            runCatching { viewModel.fetchDriverReleases(source) }
                .onSuccess { result ->
                    assets = result
                    assetsSource = source
                    downloadStep = DownloadStep.Assets
                }
                .onFailure { fetchError = it.message ?: "Failed to fetch drivers" }
            isFetching = false
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("GPU Driver Manager") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.padding(innerPadding).fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                DriverActionsRow(
                    onDownload = { showDownload = true },
                    onInstall = { launcher.launch(arrayOf("*/*")) },
                    onDelete = { openDelete() }
                )
            }

            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Active Driver", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))
                        DriverChoiceRow(
                            label = "System Default (Adreno)",
                            sublabel = "Built-in driver",
                            selected = settings.customDriverPath.isEmpty(),
                            onClick = { viewModel.setCustomDriverPath("") }
                        )
                        HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                        Column(
                            modifier = Modifier.fillMaxWidth().heightIn(max = 320.dp).verticalScroll(rememberScrollState())
                        ) {
                            if (installed.isEmpty() && settings.customDriverPath.isNotEmpty()) {
                                DriverChoiceRow(
                                    label = settings.customDriverPath.substringAfterLast("/"),
                                    sublabel = "Currently active (not found in downloads)",
                                    selected = true,
                                    onClick = {}
                                )
                            } else {
                                installed.forEach { drv ->
                                    DriverChoiceRow(
                                        label = drv.name,
                                        sublabel = "${drv.source}${if (drv.tag.isNotEmpty()) "  ·  ${drv.tag}" else ""}",
                                        selected = settings.customDriverPath == drv.path,
                                        onClick = { viewModel.setCustomDriverPath(drv.path) }
                                    )
                                }
                            }
                        }
                    }
                }
            }

            item {
                SettingsSwitchItem(
                    title = "Driver Update Notifications",
                    description = "Notify when a newer driver is available from the source you installed",
                    checked = settings.driverUpdateNotifications,
                    onCheckedChange = { viewModel.setDriverUpdateNotifications(it) }
                )
            }

            item {
                Text(
                    "Install custom GPU drivers (e.g., Turnip) by downloading from a GitHub source or uploading a .adpkg.zip / .so file. These drivers can significantly improve performance and fix graphical glitches in N64 and other Vulkan-based cores.",
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
    }

    // ── Download modal ───────────────────────────────────────────────────────
    if (showDownload) {
        Dialog(onDismissRequest = { showDownload = false }) {
            Surface(
                shape = MaterialTheme.shapes.medium,
                tonalElevation = 4.dp,
                modifier = Modifier.fillMaxWidth().heightIn(max = 560.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        if (downloadStep == DownloadStep.Assets) {
                            IconButton(onClick = {
                                downloadStep = DownloadStep.Sources
                                assets = emptyList()
                                fetchError = null
                            }) {
                                Icon(Icons.Default.ArrowBack, contentDescription = "Back to sources")
                            }
                        }
                        Text(
                            if (downloadStep == DownloadStep.Assets) assetsSource?.name ?: "Assets"
                            else "Driver Sources",
                            style = MaterialTheme.typography.titleMedium,
                            modifier = Modifier.weight(1f)
                        )
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    when {
                        isFetching -> {
                            Box(modifier = Modifier.fillMaxWidth().padding(32.dp), contentAlignment = Alignment.Center) {
                                CircularProgressIndicator()
                            }
                        }
                        fetchError != null -> {
                            Text(fetchError ?: "", color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
                            Spacer(modifier = Modifier.height(8.dp))
                            TextButton(onClick = { downloadStep = DownloadStep.Sources; fetchError = null }) {
                                Text("Choose another source")
                            }
                        }
                        downloadStep == DownloadStep.Sources -> {
                            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                items(DRIVER_SOURCES) { src ->
                                    Card(modifier = Modifier.fillMaxWidth().clickable { openAssets(src) }) {
                                        Column(modifier = Modifier.padding(12.dp)) {
                                            Text(src.name, style = MaterialTheme.typography.titleSmall)
                                            Text(src.description, style = MaterialTheme.typography.bodySmall)
                                        }
                                    }
                                }
                            }
                        }
                        else -> {
                            if (assets.isEmpty()) {
                                Text("No driver releases found for this source.", style = MaterialTheme.typography.bodyMedium)
                            } else {
                                LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                    items(assets) { asset ->
                                        val (downloaded, total) = progress
                                        val downloading = downloaded >= 0 && assetsSource != null
                                        Card(
                                            modifier = Modifier.fillMaxWidth()
                                                .clickable(enabled = !downloading) { viewModel.downloadAndInstallDriver(context, assetsSource!!, asset) }
                                        ) {
                                            Column(modifier = Modifier.padding(12.dp)) {
                                                Text(asset.name, style = MaterialTheme.typography.titleSmall)
                                                Text(
                                                    "${asset.tag}  ·  ${formatDriverSize(asset.size)}",
                                                    style = MaterialTheme.typography.bodySmall
                                                )
                                                if (downloading) {
                                                    Spacer(modifier = Modifier.height(8.dp))
                                                    if (total > 0) {
                                                        LinearProgressIndicator(
                                                            progress = { (downloaded.toFloat() / total).coerceIn(0f, 1f) },
                                                            modifier = Modifier.fillMaxWidth()
                                                        )
                                                    } else {
                                                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Delete modal ─────────────────────────────────────────────────────────
    if (showDelete) {
        Dialog(onDismissRequest = { showDelete = false }) {
            Surface(
                shape = MaterialTheme.shapes.medium,
                tonalElevation = 4.dp,
                modifier = Modifier.fillMaxWidth().heightIn(max = 560.dp)
            ) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("Delete Driver", style = MaterialTheme.typography.titleMedium)
                    Spacer(modifier = Modifier.height(8.dp))
                    if (installed.isEmpty()) {
                        Text("No installed drivers found.", style = MaterialTheme.typography.bodyMedium)
                    } else {
                        LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                            items(installed) { drv ->
                                Card(modifier = Modifier.fillMaxWidth()
                                    .clickable {
                                        viewModel.deleteDriver(drv.name)
                                        scope.launch { installed = viewModel.getInstalledDrivers() }
                                        Toast.makeText(context, "Deleted ${drv.name}", Toast.LENGTH_SHORT).show()
                                        if (installed.isEmpty()) showDelete = false
                                    }) {
                                    Column(modifier = Modifier.padding(12.dp)) {
                                        Text(drv.name, style = MaterialTheme.typography.titleSmall)
                                        Text("${drv.source}${if (drv.tag.isNotEmpty()) "  ·  ${drv.tag}" else ""}", style = MaterialTheme.typography.bodySmall)
                                    }
                                }
                            }
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    TextButton(onClick = { showDelete = false }, modifier = Modifier.align(Alignment.End)) {
                        Text("Close")
                    }
                }
            }
        }
    }
}

@Composable
private fun DriverActionsRow(onDownload: () -> Unit, onInstall: () -> Unit, onDelete: () -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Button(onClick = onDownload, modifier = Modifier.weight(1f)) {
            Icon(Icons.Default.Download, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(modifier = Modifier.width(6.dp))
            Text("Download")
        }
        Button(onClick = onInstall, modifier = Modifier.weight(1f)) {
            Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(modifier = Modifier.width(6.dp))
            Text("Install")
        }
        Button(
            onClick = onDelete,
            modifier = Modifier.weight(1f),
            colors = ButtonDefaults.buttonColors(
                containerColor = Color(0xFFD32F2F),
                contentColor = Color.White
            )
        ) {
            Box(
                modifier = Modifier
                    .size(width = 16.dp, height = 2.dp)
                    .background(Color.White, MaterialTheme.shapes.extraSmall)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text("Delete")
        }
    }
}

@Composable
private fun DriverChoiceRow(label: String, sublabel: String, selected: Boolean, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(label) },
        supportingContent = { if (sublabel.isNotEmpty()) Text(sublabel) },
        leadingContent = { RadioButton(selected = selected, onClick = null) },
        modifier = Modifier.fillMaxWidth().clickable { onClick() },
        colors = ListItemDefaults.colors(
            containerColor = if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface
        )
    )
}

private fun formatDriverSize(size: Long): String {
    if (size <= 0) return ""
    return if (size < 1024 * 1024) "${size / 1024} KB" else "${size / (1024 * 1024)} MB"
}
