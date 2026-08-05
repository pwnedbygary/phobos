package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import com.phobos.emulator.LogLevel
import com.phobos.emulator.R
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.data.ThemeMode

@Composable
fun SettingsCategory(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        Text(title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
        Spacer(modifier = Modifier.height(8.dp))
        Card {
            Column(content = content)
        }
    }
}

@Composable
fun SettingsSwitchItem(title: String, description: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { if (description.isNotEmpty()) Text(description) else null },
        trailingContent = {
            Switch(checked = checked, onCheckedChange = onCheckedChange)
        },
        modifier = Modifier.fillMaxWidth().clickable { onCheckedChange(!checked) }
    )
}

@Composable
fun SettingsCheckboxItem(title: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        trailingContent = {
            Checkbox(checked = checked, onCheckedChange = onCheckedChange)
        },
        modifier = Modifier.fillMaxWidth().clickable { onCheckedChange(!checked) }
    )
}

@Composable
fun SettingsClickableItem(title: String, description: String, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { if (description.isNotEmpty()) Text(description) else null },
        trailingContent = {
            Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
        },
        modifier = Modifier.fillMaxWidth().clickable { onClick() }
    )
}

@Composable
fun LogVerbositySelectorItem(currentLevel: LogLevel, onLevelSelected: (LogLevel) -> Unit) {
    var expanded by remember { mutableStateOf(false) }

    ListItem(
        headlineContent = { Text("Log Verbosity") },
        supportingContent = { Text("Filter logs by level (None turns off logging)") },
        trailingContent = {
            Box {
                TextButton(onClick = { expanded = true }) {
                    Icon(Icons.Default.ArrowDropDown, contentDescription = null)
                    Text(currentLevel.name)
                }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    LogLevel.entries.forEach { level ->
                        DropdownMenuItem(
                            text = { Text(level.name) },
                            onClick = { onLevelSelected(level); expanded = false }
                        )
                    }
                }
            }
        },
        modifier = Modifier.fillMaxWidth().clickable { expanded = true }
    )
}

@Composable
fun FastForwardSpeedSelectorItem(currentSpeed: Float, onSpeedSelected: (Float) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val speeds = listOf(1.5f, 2.0f, 3.0f, 5.0f, 0.0f) // 0.0f = Unlimited

    ListItem(
        headlineContent = { Text("Fast Forward Speed") },
        supportingContent = { Text("Speed limit during fast forward") },
        trailingContent = {
            Box {
                TextButton(onClick = { expanded = true }) {
                    Icon(Icons.Default.ArrowDropDown, contentDescription = null)
                    Text(if (currentSpeed == 0.0f) "Unlimited" else "%.1fx".format(currentSpeed))
                }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    speeds.forEach { speed ->
                        DropdownMenuItem(
                            text = { Text(if (speed == 0.0f) "Unlimited" else "%.1fx".format(speed)) },
                            onClick = { onSpeedSelected(speed); expanded = false }
                        )
                    }
                }
            }
        },
        modifier = Modifier.fillMaxWidth().clickable { expanded = true }
    )
}

@Composable
fun ThemeSelectorItem(currentMode: ThemeMode, onModeSelected: (ThemeMode) -> Unit) {
    var expanded by remember { mutableStateOf(false) }

    ListItem(
        headlineContent = { Text("Theme Mode") },
        supportingContent = { Text("App theme preference") },
        trailingContent = {
            Box {
                TextButton(onClick = { expanded = true }) {
                    Icon(Icons.Default.ArrowDropDown, contentDescription = null)
                    Text(currentMode.name)
                }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    ThemeMode.entries.forEach { mode ->
                        DropdownMenuItem(text = { Text(mode.name) }, onClick = { onModeSelected(mode); expanded = false })
                    }
                }
            }
        },
        modifier = Modifier.fillMaxWidth().clickable { expanded = true }
    )
}

@Composable
fun RegionSelectorItem(currentPref: RegionPreference, onPrefSelected: (RegionPreference) -> Unit) {
    var expanded by remember { mutableStateOf(false) }

    ListItem(
        headlineContent = { Text("Region Preference") },
        supportingContent = { Text("Preferred system region priority") },
        trailingContent = {
            Box {
                TextButton(onClick = { expanded = true }) {
                    Icon(Icons.Default.ArrowDropDown, contentDescription = null)
                    Text(currentPref.label)
                }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    RegionPreference.entries.forEach { pref ->
                        DropdownMenuItem(
                            text = { Text(pref.label) },
                            onClick = { onPrefSelected(pref); expanded = false }
                        )
                    }
                }
            }
        },
        modifier = Modifier.fillMaxWidth().clickable { expanded = true }
    )
}

@Composable
fun PathSelectorItem(title: String, currentPath: String, onPathSelected: (String) -> Unit) {
    val context = LocalContext.current
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(uri, flags)
            onPathSelected(uri.toString())
        }
    }
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(if (currentPath.isEmpty()) "Not set" else Uri.parse(currentPath).path ?: currentPath) },
        trailingContent = {
            Icon(painterResource(R.drawable.ic_folder), contentDescription = "Select Path")
        },
        modifier = Modifier.fillMaxWidth().clickable { launcher.launch(null) }
    )
}

@Composable
fun FirmwareSelectorItem(system: String, currentPath: String, onPathSelected: (String) -> Unit) {
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) onPathSelected(uri.toString())
    }
    ListItem(
        headlineContent = { Text(system) },
        supportingContent = { Text(if (currentPath.isEmpty()) "BIOS not set" else Uri.parse(currentPath).path ?: currentPath) },
        trailingContent = {
            Icon(painterResource(R.drawable.ic_file_open), contentDescription = "Select BIOS")
        },
        modifier = Modifier.fillMaxWidth().clickable { launcher.launch(arrayOf("*/*")) }
    )
}
