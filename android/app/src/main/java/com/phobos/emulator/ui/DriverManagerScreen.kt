package com.phobos.emulator.ui

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DriverManagerScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val context = LocalContext.current

    // File picker: pick a single driver file (.so, .zip, .adpkg). We use
    // OpenDocument with */* so all files show (the MIME filter hid some zips).
    // installCustomDriver resolves the real filename via DISPLAY_NAME.
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            viewModel.installCustomDriver(context, uri)
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
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { launcher.launch(arrayOf("*/*")) }) {
                Icon(Icons.Default.Add, contentDescription = "Install Driver")
            }
        }
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.padding(innerPadding).fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Text("Current Driver", style = MaterialTheme.typography.titleMedium)
                        Spacer(modifier = Modifier.height(8.dp))
                        if (settings.customDriverPath.isEmpty()) {
                            Text("System Default (Adreno)", color = MaterialTheme.colorScheme.secondary)
                        } else {
                            Text(settings.customDriverPath.substringAfterLast("/"), color = MaterialTheme.colorScheme.primary)
                            Text(settings.customDriverPath, style = MaterialTheme.typography.bodySmall)
                            Spacer(modifier = Modifier.height(8.dp))
                            Button(
                                onClick = { viewModel.setCustomDriverPath("") },
                                colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.errorContainer, contentColor = MaterialTheme.colorScheme.onErrorContainer)
                            ) {
                                Icon(Icons.Default.Clear, contentDescription = null)
                                Spacer(modifier = Modifier.width(8.dp))
                                Text("Revert to Default")
                            }
                        }
                    }
                }
            }

            item {
                Text(
                    "You can install custom GPU drivers (e.g., Turnip) by uploading a .adpkg.zip file. These drivers can significantly improve performance and fix graphical glitches in N64 and other Vulkan-based cores.",
                    style = MaterialTheme.typography.bodySmall
                )
            }
        }
    }
}
