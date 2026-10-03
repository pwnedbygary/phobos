package com.phobos.emulator.ui

import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import com.phobos.emulator.util.romTitle

/**
 * Dialogs shown over the emulator: quit confirmation, GPU driver suggestion, and load failures
 * (missing BIOS, a game that can't be read where it is, Neo Geo ROM that failed to load, any other game that didn't
 * start). Load-failure dialogs return to the previous screen via [onLeave] because nothing was loaded.
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
    val settings by viewModel.settings.collectAsState()
    val showDriverSuggestion by viewModel.showDriverSuggestion.collectAsState()
    val biosRequired by viewModel.biosRequired.collectAsState()
    val firmwareRequired by viewModel.firmwareRequired.collectAsState()
    val discNotReadable by viewModel.discNotReadable.collectAsState()
    val neoGeoRomLoadFailed by viewModel.neoGeoRomLoadFailed.collectAsState()
    val gameLoadFailed by viewModel.gameLoadFailed.collectAsState()
    val fullScreen = settings.fullScreenMode

    if (showQuitDialog) {
        PhobosAlertDialog(
            onDismissRequest = onQuitDismissed,
            title = { DialogSystemBars(fullScreen, inGame = true); Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating ${romTitle(romName)}?") },
            confirmButton = { TextButton(onClick = onQuitConfirmed) { Text("Quit") } },
            dismissButton = { TextButton(onClick = onQuitDismissed) { Text("Cancel") } },
        )
    }

    if (showDriverSuggestion) {
        PhobosAlertDialog(
            onDismissRequest = { viewModel.dismissDriverSuggestion() },
            title = { DialogSystemBars(fullScreen, inGame = true); Text("GPU Driver Issue Detected") },
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

    biosRequired?.let {
        LoadFailureDialog(
            title = "Neo Geo BIOS Required",
            message = "The Neo Geo core cannot boot without a BIOS.\n\n" +
                "Add neogeo.zip (containing sp-e.sp1 and 000-lo.lo) by setting the Neo Geo BIOS in " +
                "Settings, or place neogeo.zip next to your ROMs, then try loading the game again.",
            fullScreen = fullScreen,
            onDismiss = { viewModel.dismissBiosRequired(); onLeave() },
        )
    }

    firmwareRequired?.let { required ->
        val one = required.keys.size == 1
        LoadFailureDialog(
            title = "BIOS Required",
            message = "${required.system} games need ${if (one) "this BIOS file" else "these BIOS files"}, " +
                "which Phobos doesn't include:\n\n" +
                required.keys.joinToString("\n") { "• ${firmwareFileName(it)}" } +
                "\n\nAdd ${if (one) "it" else "them"} in Settings → Emulation → Firmware (BIOS).",
            fullScreen = fullScreen,
            onDismiss = { viewModel.dismissFirmwareRequired(); onLeave() },
        )
    }

    discNotReadable?.let { disc ->
        LoadFailureDialog(
            title = "Game Can't Be Read Where It Is",
            message = "Phobos reads ${disc.system} games straight from storage instead of copying them, " +
                "and Android didn't let it open this one directly:\n\n${disc.location}\n\n" +
                "Start it from the Library, with its folder on the device's storage or SD card.",
            fullScreen = fullScreen,
            onDismiss = { viewModel.dismissDiscNotReadable(); onLeave() },
        )
    }

    neoGeoRomLoadFailed?.let {
        LoadFailureDialog(
            title = "Neo Geo ROM Failed to Load",
            message = "The Neo Geo BIOS was found, but this ROM could not be loaded. It is likely " +
                "not a valid Neo Geo MVS/AES game (for example, 1941 is a CPS-1 Capcom title, not " +
                "Neo Geo).\n\nVerify the ROM is a real Neo Geo cartridge (e.g. kof2003) and that " +
                "neogeo.zip is set in Settings, then try again.",
            fullScreen = fullScreen,
            onDismiss = { viewModel.dismissNeoGeoRomLoadFailed(); onLeave() },
        )
    }

    gameLoadFailed?.let { failed ->
        LoadFailureDialog(
            title = "Game Didn't Start",
            message = "${romTitle(failed.name)} couldn't be started as a ${failed.system} game" +
                (failed.reason?.let { ":\n\n$it" } ?: ". The file may be damaged, or in a format Phobos doesn't read."),
            fullScreen = fullScreen,
            onDismiss = { viewModel.dismissGameLoadFailed(); onLeave() },
        )
    }
}

@Composable
private fun LoadFailureDialog(title: String, message: String, fullScreen: Boolean, onDismiss: () -> Unit) {
    PhobosAlertDialog(
        onDismissRequest = onDismiss,
        title = { DialogSystemBars(fullScreen, inGame = true); Text(title) },
        text = { Text(message, Modifier.verticalScroll(rememberScrollState())) },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
    )
}
