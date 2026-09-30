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
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.LegibleIcon
import com.phobos.emulator.ui.theme.pillShape

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ShaderSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val context = LocalContext.current

    val launcher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(
                uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION
            )
            viewModel.setShaderPath(uri.toString())
        }
    }

    PhobosScaffold(
        title = "Shaders (Slang)",
        onBack = onBack,
        actions = {
            if (settings.shaderPath.isNotEmpty()) {
                IconButton(onClick = { viewModel.setShaderPath("") }) {
                    LegibleIcon(Icons.Default.Clear, contentDescription = "Clear Shader")
                }
            }
        },
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = pageContentPadding(),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                SettingsCategory("Current Shader") {
                    Column(modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 10.dp)) {
                        Text(
                            if (settings.shaderPath.isEmpty()) "No shader active" 
                            else settings.shaderPath.substringAfterLast("%2F").substringAfterLast("/"),
                            style = MaterialTheme.typography.bodyLarge
                        )
                        if (settings.shaderPath.isNotEmpty()) {
                            Text(
                                settings.shaderPath,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            }

            item {
                Button(
                    onClick = { launcher.launch(arrayOf("*/*")) },
                    modifier = Modifier.fillMaxWidth(),
                    shape = pillShape()
                ) {
                    Icon(Icons.Default.Settings, contentDescription = null)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Select .slangp Preset")
                }
            }

            item {
                BackdropText(
                    "Note: Librashader supports standard RetroArch Slang presets. Ensure all referenced .slang and texture files are in the same relative directories as the .slangp file.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            
            // TODO: Dynamically list shader parameters here using librashader reflection
        }
    }
}
