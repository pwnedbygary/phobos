package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.FirmwareIds
import com.phobos.emulator.util.FirmwareStatus
import com.phobos.emulator.util.PspFonts

data class FirmwareInfo(
    val emulator: String,
    val type: String,
    val region: String,
    val systemKey: String // Key used in SettingsStore
)

/** The file the BIOS behind a key from `PhobosCore.missingFirmware` usually comes as. */
fun firmwareFileName(key: String): String = when (key) {
    "fw_mcd" -> "Mega CD BIOS (US, Japan or Europe)"
    "fw_sgb" -> "Super Game Boy or Super Game Boy 2 cartridge ROM"
    "fw_pce_cd" -> "PC Engine CD System Card 3.0 (Japan)"
    "fw_laseractive_sega" -> "LaserActive SEGA PAC BIOS (US v1.04 or Japan v1.02)"
    "fw_laseractive_nec" -> "LaserActive NEC PAC BIOS (PAC-N10, PAC-N1 or PCE-LP1)"
    "fw_msx_basic" -> "An MSX BIOS with BASIC (such as MSX.ROM), which tapes load through; the built-in C-BIOS has none"
    "fw_msx2_basic" -> "MSX2 main and sub BIOS (such as MSX2.ROM and MSX2EXT.ROM), which tapes load through; the built-in C-BIOS has no BASIC"
    else -> key
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FirmwareSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val status by viewModel.firmwareStatus.collectAsState()
    val context = LocalContext.current
    var selectedFirmwareKey by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(settings.systemFirmwarePaths) { viewModel.refreshFirmwareStatus(context, settings.systemFirmwarePaths) }
    val pspFonts by viewModel.pspFonts.collectAsState()
    LaunchedEffect(Unit) { viewModel.refreshPspFonts() }
    // The PSP's fonts come as a folder (their flash0's font folder): its .pgf files are copied into the app's own files.
    val fontsLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) viewModel.importPspFonts(context, uri)
    }

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && selectedFirmwareKey != null) {
            // Without a persisted grant the pick stops working after a reboot or an app update.
            runCatching { context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            viewModel.setSystemFirmwarePath(selectedFirmwareKey!!, uri.toString())
        }
        selectedFirmwareKey = null
    }

    // Full list based on Ares desktop screenshots
    val firmwareList = remember {
        listOf(
            FirmwareInfo("ColecoVision", "BIOS", "World", "fw_coleco"),
            FirmwareInfo("Famicom Disk System", "BIOS", "Japan", "fw_fds"),
            FirmwareInfo("Game Boy Advance", "BIOS", "World", "fw_gba"),
            FirmwareInfo("Game Gear", "BIOS", "World", "fw_gg"),
            FirmwareInfo("LaserActive (NEC PAC)", "Games Express", "Japan", "fw_laseractive_nec_ge"),
            FirmwareInfo("LaserActive (NEC PAC)", "PAC-N10", "US", "fw_laseractive_nec_us"),
            FirmwareInfo("LaserActive (NEC PAC)", "PAC-N1", "Japan", "fw_laseractive_nec_jp"),
            FirmwareInfo("LaserActive (NEC PAC)", "PCE-LP1", "Japan", "fw_laseractive_nec_lp"),
            FirmwareInfo("LaserActive (SEGA PAC)", "BIOS", "US", "fw_laseractive_sega_us"),
            FirmwareInfo("LaserActive (SEGA PAC)", "BIOS", "Japan", "fw_laseractive_sega_jp"),
            FirmwareInfo("Master System", "BIOS", "US", "fw_ms_us"),
            FirmwareInfo("Master System", "BIOS", "Japan", "fw_ms_jp"),
            FirmwareInfo("Master System", "BIOS", "Europe", "fw_ms_eu"),
            FirmwareInfo("Mega 32X", "68000 BIOS", "World", "fw_32x_g"),
            FirmwareInfo("Mega 32X", "SH-2 Master", "World", "fw_32x_m"),
            FirmwareInfo("Mega 32X", "SH-2 Slave", "World", "fw_32x_s"),
            FirmwareInfo("Mega CD", "BIOS", "US", "fw_mcd_us"),
            FirmwareInfo("Mega CD", "BIOS", "Japan", "fw_mcd_jp"),
            FirmwareInfo("Mega CD", "BIOS", "Europe", "fw_mcd_eu"),
            FirmwareInfo("MSX", "BIOS", "Japan", "fw_msx"),
            FirmwareInfo("MSX2", "MAIN", "Japan", "fw_msx2_main"),
            FirmwareInfo("MSX2", "SUB", "Japan", "fw_msx2_sub"),
            FirmwareInfo("Neo Geo AES", "BIOS", "World", "fw_ng_aes"),
            FirmwareInfo("Neo Geo MVS", "BIOS", "World", "fw_ng_mvs"),
            FirmwareInfo("Neo Geo", "Universal BIOS", "World", "fw_ng_bios"),
            FirmwareInfo("Neo Geo CD", "BIOS", "World", "fw_ng_cd"),
            FirmwareInfo("Neo Geo Pocket", "BIOS", "World", "fw_ngp"),
            FirmwareInfo("Neo Geo Pocket Color", "BIOS", "World", "fw_ngpc"),
            FirmwareInfo("Nintendo 64", "PIF", "US", "fw_n64_pif_ntsc"),
            FirmwareInfo("Nintendo 64", "PIF", "Japan", "fw_n64_pif_ntsc"),
            FirmwareInfo("Nintendo 64", "PIF", "Europe", "fw_n64_pif_pal"),
            FirmwareInfo("Nintendo 64DD", "BIOS", "US", "fw_n64dd_us"),
            FirmwareInfo("Nintendo 64DD", "BIOS", "Japan", "fw_n64dd_jp"),
            FirmwareInfo("Nintendo 64DD", "BIOS", "DEV", "fw_n64dd_dev"),
            FirmwareInfo("PC Engine CD", "System Card 1.0", "Japan", "fw_pce_cd_1_jp"),
            FirmwareInfo("PC Engine CD", "System Card 3.0", "Japan", "fw_pce_cd_3_jp"),
            FirmwareInfo("PC Engine CD", "System Card 3.0", "US", "fw_pce_cd_3_us"),
            FirmwareInfo("PC Engine CD", "Games Express", "Japan", "fw_pce_cd_ge_jp"),
            FirmwareInfo("PlayStation", "BIOS", "US", "fw_psx_us"),
            FirmwareInfo("PlayStation", "BIOS", "Japan", "fw_psx_jp"),
            FirmwareInfo("PlayStation", "BIOS", "Europe", "fw_psx_eu"),
            FirmwareInfo("Super Game Boy", "SGB cartridge", "World", "fw_sgb1"),
            FirmwareInfo("Super Game Boy", "SGB2 cartridge", "Japan", "fw_sgb2"),
            FirmwareInfo("Arcade", "Aleck64 PIF set", "World", "fw_aleck64"),
            FirmwareInfo("SuperGrafx CD", "Arcade Card", "Japan", "fw_supergrafx_ac_jp"),
            FirmwareInfo("ZX Spectrum", "BIOS (48K)", "World", "fw_zx48"),
            FirmwareInfo("ZX Spectrum 128", "BIOS (128-0)", "World", "fw_zx128"),
            FirmwareInfo("ZX Spectrum 128", "SUB (128-1)", "World", "fw_zx128_sub")
        )
    }

    PhobosScaffold(
        title = "BIOS Firmware Locations",
        onBack = onBack,
        bottomBar = {
            ThemedCard(Modifier.padding(horizontal = 16.dp, vertical = 8.dp).fillMaxWidth()) {
                Row(
                    modifier = Modifier
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                        .fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Button(onClick = { viewModel.scanFirmware(context) }, shape = pillShape()) {
                        Text("Scan Folder")
                    }
                    Row {
                        TextButton(onClick = { viewModel.clearAllFirmware() }) {
                            Text("Clear All")
                        }
                    }
                }
            }
        }
    ) { innerPadding ->
        ThemedCard(Modifier.padding(innerPadding).padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = LocalDockInset.current).fillMaxSize()) {
            Column {
                // Header
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(MaterialTheme.colorScheme.surfaceVariant)
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    HeaderText("Emulator", Modifier.weight(2f))
                    HeaderText("Type", Modifier.weight(1.5f))
                    HeaderText("Region", Modifier.weight(1f))
                    HeaderText("Location", Modifier.weight(3f))
                    HeaderText("Status", Modifier.weight(1.4f))
                }

                LazyColumn(modifier = Modifier.weight(1f)) {
                    item {
                        PspFontsRow(pspFonts) { fontsLauncher.launch(null) }
                        HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant)
                    }
                    items(firmwareList) { info ->
                        val path = settings.systemFirmwarePaths[info.systemKey] ?: ""
                        FirmwareRow(info, path, status[info.systemKey]) {
                            selectedFirmwareKey = info.systemKey
                            launcher.launch(arrayOf("*/*"))
                        }
                        HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant)
                    }
                }
            }
        }
    }
}

@Composable
fun HeaderText(text: String, modifier: Modifier) {
    Text(
        text = text,
        modifier = modifier,
        style = MaterialTheme.typography.labelMedium,
        fontWeight = FontWeight.Bold,
        color = MaterialTheme.colorScheme.onSurfaceVariant
    )
}

/** One firmware slot; [status] is what its file is to it, null while unchecked or unset. */
@Composable
fun FirmwareRow(info: FirmwareInfo, path: String, status: FirmwareStatus?, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RowText(info.emulator, Modifier.weight(2f))
        RowText(info.type, Modifier.weight(1.5f))
        RowText(info.region, Modifier.weight(1f))
        RowText(
            if (path.isEmpty()) "(unset)" else Uri.parse(path).lastPathSegment ?: path,
            Modifier.weight(3f),
            color = if (path.isEmpty()) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.primary
        )
        RowText(
            when {
                path.isNotEmpty() -> status?.name.orEmpty()
                info.systemKey in FirmwareIds.builtIn -> "Built-in"
                else -> ""
            },
            Modifier.weight(1.4f),
            color = when {
                path.isEmpty() -> MaterialTheme.colorScheme.onSurfaceVariant
                status == FirmwareStatus.Verified -> MaterialTheme.colorScheme.primary
                else -> MaterialTheme.colorScheme.error
            }
        )
    }
}

/**
 * The PSP's system fonts, from the user's own PSP: tapping picks the font folder of their flash0 dump (or the dump,
 * or its flash0), whose .pgf files the app copies into its own files. [held] is how many of the eighteen it holds,
 * and whether they were found by themselves in Download/FLASH0DUMP rather than picked.
 */
@Composable
fun PspFontsRow(held: PspFonts.Held, onClick: () -> Unit) {
    val count = held.count
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RowText("PlayStation Portable", Modifier.weight(2f))
        RowText("PSP fonts (from your PSP's flash0)", Modifier.weight(1.5f))
        RowText("World", Modifier.weight(1f))
        RowText(
            when {
                count == 0 -> "(unset): pick flash0's font folder"
                held.found -> "$count of 18 fonts, found in ${PspFonts.DUMP_FOLDER}"
                else -> "$count of 18 fonts copied"
            },
            Modifier.weight(3f),
            color = if (count == 0) MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.primary
        )
        RowText(
            when (count) {
                0 -> ""
                18 -> "Complete"
                else -> "Partial"
            },
            Modifier.weight(1.4f),
            color = if (count == 18) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun RowText(text: String, modifier: Modifier, color: Color = Color.Unspecified) {
    Text(
        text = text,
        modifier = modifier.basicMarquee(),
        style = MaterialTheme.typography.bodySmall,
        maxLines = 1,
        color = color
    )
}
