package com.phobos.emulator.ui

import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import coil.compose.AsyncImage
import com.phobos.emulator.R

@Composable
fun LibraryScreen(viewModel: MainViewModel, onSystemClick: (String) -> Unit) {
    val systems by viewModel.visibleSystems.collectAsState()

    Box(modifier = Modifier.fillMaxSize()) {
        Image(
            painter = painterResource(id = R.drawable.phobos_logo),
            contentDescription = null,
            modifier = Modifier
                .size(400.dp)
                .align(Alignment.BottomEnd)
                .offset(x = 100.dp, y = 100.dp)
                .alpha(0.10f),
            contentScale = ContentScale.Fit
        )

        if (systems.isEmpty()) {
            Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Image(
                        painter = painterResource(id = R.drawable.phobos_logo),
                        contentDescription = "Phobos Logo",
                        modifier = Modifier.size(120.dp),
                        alpha = 0.3f
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    Text(
                        "No systems found",
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f)
                    )
                }
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 140.dp),
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                items(systems) { system ->
                    SystemCard(system, onClick = { 
                        onSystemClick(Uri.encode(system)) 
                    })
                }
            }
        }
    }
}

@Composable
fun SystemCard(system: String, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .height(160.dp)
            .clickable { onClick() },
        elevation = CardDefaults.cardElevation(defaultElevation = 2.dp),
        shape = RoundedCornerShape(12.dp)
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween
        ) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                contentAlignment = Alignment.Center
            ) {
                AsyncImage(
                    model = getSystemIcon(system),
                    contentDescription = system,
                    modifier = Modifier.size(80.dp),
                    contentScale = ContentScale.Fit
                )
            }
            Text(
                system,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2
            )
        }
    }
}

private fun getSystemIcon(system: String): Any {
    val assetName = when {
        system.contains("Neo Geo Pocket Color", ignoreCase = true) -> "neo-geo-pocket-color"
        system.contains("Neo Geo Pocket", ignoreCase = true) -> "neo-geo-pocket"
        system.contains("Neo Geo", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "neo-geo-cd"
        system.contains("Neo Geo", ignoreCase = true) -> "neogeomvs"
        system.contains("Mega Drive", ignoreCase = true) || system.contains("Genesis", ignoreCase = true) -> "genesis"
        system.contains("SNES", ignoreCase = true) || system.contains("Super Famicom", ignoreCase = true) -> "snes"
        system.contains("NES", ignoreCase = true) || system.contains("Famicom", ignoreCase = true) -> "nes"
        system.contains("Nintendo 64", ignoreCase = true) -> "n64"
        system.contains("Game Boy Advance", ignoreCase = true) -> "gba"
        system.contains("Game Boy Color", ignoreCase = true) -> "gbc"
        system.contains("Game Boy", ignoreCase = true) -> "gb"
        system.contains("PlayStation", ignoreCase = true) -> "psx"
        system.contains("Game Gear", ignoreCase = true) -> "gamegear"
        system.contains("MSX2", ignoreCase = true) -> "msx2"
        system.contains("MSX", ignoreCase = true) -> "msx"
        system.contains("PC Engine", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "pcecd"
        system.contains("PC Engine", ignoreCase = true) || system.contains("PCE", ignoreCase = true) || system.contains("TurboGrafx", ignoreCase = true) -> "pce"
        system.contains("Sega 32X", ignoreCase = true) || system.contains("32X", ignoreCase = true) -> "sega32"
        system.contains("Mega CD", ignoreCase = true) -> "segacd"
        system.contains("Master System", ignoreCase = true) || system.contains("Mark III", ignoreCase = true) || system.contains("SG-1000", ignoreCase = true) -> "sms"
        system.contains("SC-3000", ignoreCase = true) -> "sc-3000"
        system.contains("ColecoVision", ignoreCase = true) -> "colecovision"
        system.contains("Atari 2600", ignoreCase = true) -> "atari2600"
        system.contains("LaserActive", ignoreCase = true) -> "laseractive"
        system.contains("SuperGrafx", ignoreCase = true) -> "supergrafx"
        system.contains("WonderSwan Color", ignoreCase = true) -> "wonderswan-color"
        system.contains("WonderSwan", ignoreCase = true) || system.contains("Pocket Challenge", ignoreCase = true) -> "wonderswan"
        system.contains("ZX Spectrum", ignoreCase = true) -> "zx-spectrum"
        system.contains("Arcade", ignoreCase = true) -> "arcade"
        else -> null
    }

    return if (assetName != null) {
        "file:///android_asset/platforms/$assetName.svg"
    } else {
        R.drawable.phobos_logo
    }
}
