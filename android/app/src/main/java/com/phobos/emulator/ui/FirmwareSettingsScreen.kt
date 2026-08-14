package com.phobos.emulator.ui

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
import androidx.compose.material.icons.filled.ArrowBack
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
import androidx.compose.ui.unit.sp

data class FirmwareInfo(
    val emulator: String,
    val type: String,
    val region: String,
    val systemKey: String // Key used in SettingsStore
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FirmwareSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val context = LocalContext.current
    var selectedFirmwareKey by remember { mutableStateOf<String?>(null) }

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && selectedFirmwareKey != null) {
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
            FirmwareInfo("Mega CD", "BIOS", "US", "fw_mcd_us"),
            FirmwareInfo("Mega CD", "BIOS", "Japan", "fw_mcd_jp"),
            FirmwareInfo("Mega CD", "BIOS", "Europe", "fw_mcd_eu"),
            FirmwareInfo("MSX", "BIOS", "Japan", "fw_msx"),
            FirmwareInfo("MSX2", "MAIN", "Japan", "fw_msx2_main"),
            FirmwareInfo("MSX2", "SUB", "Japan", "fw_msx2_sub"),
            FirmwareInfo("Neo Geo AES", "BIOS", "World", "fw_ng_aes"),
            FirmwareInfo("Neo Geo MVS", "BIOS", "World", "fw_ng_mvs"),
            FirmwareInfo("Neo Geo", "Universal BIOS", "World", "fw_ng_bios"),
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
            FirmwareInfo("SuperGrafx CD", "Arcade Card", "Japan", "fw_supergrafx_ac_jp"),
            FirmwareInfo("ZX Spectrum", "BIOS (48K)", "World", "fw_zx48"),
            FirmwareInfo("ZX Spectrum 128", "BIOS (128-0)", "World", "fw_zx128"),
            FirmwareInfo("ZX Spectrum 128", "SUB (128-1)", "World", "fw_zx128_sub")
        )
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("BIOS Firmware Locations") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        },
        bottomBar = {
            Surface(tonalElevation = 2.dp) {
                Row(
                    modifier = Modifier
                        .padding(16.dp)
                        .fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Button(onClick = { viewModel.scanFirmware(context) }) {
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
        Column(modifier = Modifier.padding(innerPadding).fillMaxSize()) {
            // Header
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(MaterialTheme.colorScheme.surfaceVariant)
                    .padding(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                HeaderText("Emulator", Modifier.weight(2f))
                HeaderText("Type", Modifier.weight(1.5f))
                HeaderText("Region", Modifier.weight(1f))
                HeaderText("Location", Modifier.weight(3f))
            }

            LazyColumn(modifier = Modifier.weight(1f)) {
                items(firmwareList) { info ->
                    val path = settings.systemFirmwarePaths[info.systemKey] ?: ""
                    FirmwareRow(info, path) {
                        selectedFirmwareKey = info.systemKey
                        launcher.launch(arrayOf("*/*"))
                    }
                    HorizontalDivider(thickness = 0.5.dp, color = MaterialTheme.colorScheme.outlineVariant)
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

@Composable
fun FirmwareRow(info: FirmwareInfo, path: String, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        RowText(info.emulator, Modifier.weight(2f))
        RowText(info.type, Modifier.weight(1.5f))
        RowText(info.region, Modifier.weight(1f))
        RowText(
            if (path.isEmpty()) "(unset)" else Uri.parse(path).lastPathSegment ?: path,
            Modifier.weight(3f),
            color = if (path.isEmpty()) Color.Gray else MaterialTheme.colorScheme.primary
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
        fontSize = 12.sp,
        maxLines = 1,
        color = color
    )
}
