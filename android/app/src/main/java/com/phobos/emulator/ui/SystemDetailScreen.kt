package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.ui.unit.dp

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

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(systemName) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        },
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
                        Text("No directories selected", style = MaterialTheme.typography.bodyLarge)
                        Spacer(modifier = Modifier.height(8.dp))
                        Button(onClick = { launcher.launch(null) }) {
                            Text("Add ROM Folder")
                        }
                    }
                }
            } else {
                Card(
                    modifier = Modifier.padding(16.dp).fillMaxWidth(),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
                ) {
                    Column(modifier = Modifier.padding(8.dp)) {
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
                        Text("No compatible ROMs found in these folders", style = MaterialTheme.typography.bodyMedium)
                    }
                } else {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(roms) { rom ->
                            ListItem(
                                headlineContent = { Text(rom.name) },
                                leadingContent = { Icon(Icons.Default.PlayArrow, contentDescription = null) },
                                modifier = Modifier.clickable { 
                                    viewModel.loadRom(context, systemName, rom)
                                    onRomClick(Uri.encode(systemName), Uri.encode(rom.name))
                                }
                            )
                        }
                    }
                }
            }
        }
    }
}
