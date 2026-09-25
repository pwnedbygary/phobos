package com.phobos.emulator.ui

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue

/**
 * Dialogs shown over the emulator: quit confirmation, GPU driver suggestion, and load failures
 * (unsupported core, missing Neo Geo BIOS, Neo Geo ROM that failed to load). Load-failure
 * dialogs return to the previous screen via [onLeave] because nothing was loaded.
 */
@Composable
fun EmulatorDialogs(
    viewModel: MainViewModel,
    romName: String,
    showQuitDialog: Boolean,
    onQuitDismissed: () -> Unit,
    onQuitConfirmed: () -> Unit,
    onLeave: () -> Unit,
) {
    val showDriverSuggestion by viewModel.showDriverSuggestion.collectAsState()
    val unsupportedSystem by viewModel.unsupportedSystem.collectAsState()
    val biosRequired by viewModel.biosRequired.collectAsState()
    val neoGeoRomLoadFailed by viewModel.neoGeoRomLoadFailed.collectAsState()

    if (showQuitDialog) {
        AlertDialog(
            onDismissRequest = onQuitDismissed,
            title = { Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating $romName?") },
            confirmButton = { TextButton(onClick = onQuitConfirmed) { Text("Quit") } },
            dismissButton = { TextButton(onClick = onQuitDismissed) { Text("Cancel") } },
        )
    }

    if (showDriverSuggestion) {
        AlertDialog(
            onDismissRequest = { viewModel.dismissDriverSuggestion() },
            title = { Text("GPU Driver Issue Detected") },
            text = {
                Text(
                    "The built-in GPU driver is unable to compile shaders needed by this game. " +
                        "You may see visual glitches, missing graphics, or reduced performance.\n\n" +
                        "For best results, install a Turnip Mesa driver via:\n" +
                        "Settings → GPU Driver Manager"
                )
            },
            confirmButton = { TextButton(onClick = { viewModel.dismissDriverSuggestion() }) { Text("OK") } },
        )
    }

    unsupportedSystem?.let { system ->
        LoadFailureDialog(
            title = "$system Unsupported",
            message = "$system games are currently unsupported in this build. " +
                "This is a known issue being worked on.\n\nPlease try a different system or game.",
            onDismiss = { viewModel.dismissUnsupportedSystem(); onLeave() },
        )
    }

    biosRequired?.let {
        LoadFailureDialog(
            title = "Neo Geo BIOS Required",
            message = "The Neo Geo core cannot boot without a BIOS.\n\n" +
                "Add neogeo.zip (containing sp-e.sp1 and 000-lo.lo) by setting the Neo Geo BIOS in " +
                "Settings, or place neogeo.zip next to your ROMs, then try loading the game again.",
            onDismiss = { viewModel.dismissBiosRequired(); onLeave() },
        )
    }

    neoGeoRomLoadFailed?.let {
        LoadFailureDialog(
            title = "Neo Geo ROM Failed to Load",
            message = "The Neo Geo BIOS was found, but this ROM could not be loaded. It is likely " +
                "not a valid Neo Geo MVS/AES game (for example, 1941 is a CPS-1 Capcom title, not " +
                "Neo Geo).\n\nVerify the ROM is a real Neo Geo cartridge (e.g. kof2003) and that " +
                "neogeo.zip is set in Settings, then try again.",
            onDismiss = { viewModel.dismissNeoGeoRomLoadFailed(); onLeave() },
        )
    }
}

@Composable
private fun LoadFailureDialog(title: String, message: String, onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(message) },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
    )
}
