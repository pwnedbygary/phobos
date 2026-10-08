package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import android.graphics.BitmapFactory
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.romTitle
import java.io.File

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SystemDetailScreen(
    encodedSystemName: String,
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onRomClick: (String, String) -> Unit
) {
    val systemName = remember(encodedSystemName) { Uri.decode(encodedSystemName) }
    val settings by viewModel.settings.collectAsState()
    val roms by viewModel.roms.collectAsState()
    val context = LocalContext.current
    // A multi-disc game asks which disc to start from, the last one played picked to begin with.
    var choosingDisc by remember { mutableStateOf<RomFile?>(null) }
    choosingDisc?.let { game ->
        DiscChoiceDialog(
            title = romTitle(game.name),
            discs = game.discs,
            selected = viewModel.lastDisc(systemName, game),
            confirmLabel = "Start",
            fullScreen = settings.fullScreenMode,
            inGame = false,
            onDismiss = { choosingDisc = null },
            onChoose = { disc ->
                choosingDisc = null
                viewModel.loadRom(context, systemName, game, disc)
                onRomClick(Uri.encode(systemName), Uri.encode(game.name))
            },
        )
    }

    val directoryUris = remember(settings.systemRomPaths[systemName]) {
        settings.systemRomPaths[systemName]?.map { Uri.parse(it) } ?: emptyList()
    }

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            )
            viewModel.addSystemRomPath(systemName, uri.toString())
        }
    }

    LaunchedEffect(directoryUris) {
        if (directoryUris.isNotEmpty()) {
            viewModel.scanRoms(context, systemName, directoryUris)
        } else {
            viewModel.clearRoms()
        }
    }

    PhobosScaffold(
        title = systemName,
        onBack = onBack,
        floatingActionButton = {
            FloatingActionButton(onClick = { launcher.launch(null) }) {
                Icon(Icons.Default.Add, contentDescription = "Add ROM Directory")
            }
        }
    ) { innerPadding ->
        Column(modifier = Modifier.padding(innerPadding).fillMaxSize()) {
            if (directoryUris.isEmpty()) {
                Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Icon(Icons.Default.List, contentDescription = null, modifier = Modifier.size(64.dp), tint = MaterialTheme.colorScheme.primary.copy(alpha = 0.5f))
                        Spacer(modifier = Modifier.height(16.dp))
                        BackdropText("No directories selected", style = MaterialTheme.typography.bodyLarge)
                        Spacer(modifier = Modifier.height(8.dp))
                        Button(onClick = { launcher.launch(null) }, shape = pillShape()) {
                            Text("Add ROM Folder")
                        }
                    }
                }
            } else {
                SettingsCard(Modifier.padding(16.dp)) {
                    Column(modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp)) {
                        Text("Search Directories:", style = MaterialTheme.typography.labelMedium, modifier = Modifier.padding(start = 8.dp, bottom = 4.dp))
                        directoryUris.forEach { uri ->
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)
                            ) {
                                Icon(Icons.Default.Place, contentDescription = null, modifier = Modifier.size(16.dp).padding(horizontal = 4.dp))
                                Text(
                                    uri.path ?: "Unknown",
                                    style = MaterialTheme.typography.bodySmall,
                                    modifier = Modifier.weight(1f),
                                    maxLines = 1,
                                    overflow = TextOverflow.Ellipsis
                                )
                                IconButton(onClick = { viewModel.removeSystemRomPath(systemName, uri.toString()) }) {
                                    Icon(Icons.Default.Delete, contentDescription = "Remove Folder", modifier = Modifier.size(16.dp))
                                }
                            }
                        }
                    }
                }
                
                if (roms.isEmpty()) {
                    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        BackdropText("No compatible ROMs found in these folders", style = MaterialTheme.typography.bodyMedium)
                    }
                } else {
                    // The bottom padding lets the last row scroll clear of the add-folder button.
                    SettingsCard(Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp).weight(1f, fill = false)) {
                        LazyColumn(contentPadding = PaddingValues(bottom = 64.dp)) {
                            items(roms) { rom ->
                                val multiDisc = rom.discs.size > 1
                                val displayTitle = if (multiDisc) romTitle(rom.name)
                                    else com.phobos.emulator.util.displayTitle(rom.name, rom.title)
                                val iconBitmap = remember(rom.iconPath) {
                                    rom.iconPath?.let { path ->
                                        runCatching { BitmapFactory.decodeFile(path) }.getOrNull()?.asImageBitmap()
                                    }
                                }
                                ListItem(
                                    headlineContent = { Text(displayTitle) },
                                    supportingContent = if (multiDisc) {
                                        { Text("${rom.discs.size} discs") }
                                    } else if (rom.title != null) {
                                        { Text(rom.name) }
                                    } else null,
                                    leadingContent = if (iconBitmap != null) {
                                        {
                                            Image(
                                                bitmap = iconBitmap,
                                                contentDescription = displayTitle,
                                                modifier = Modifier.size(48.dp)
                                            )
                                        }
                                    } else {
                                        { IconBadge(Icons.Default.PlayArrow) }
                                    },
                                    colors = transparentListItemColors(),
                                    modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary).clickable {
                                        // The game already paused behind the Library carries on instead of restarting
                                        if (viewModel.isRunning(systemName, rom.name)) {
                                            viewModel.swapBackToGame()
                                        } else if (multiDisc) {
                                            choosingDisc = rom
                                        } else {
                                            viewModel.loadRom(context, systemName, rom)
                                            onRomClick(Uri.encode(systemName), Uri.encode(rom.name))
                                        }
                                    }
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
