package com.phobos.emulator.ui

import android.net.Uri
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import coil.compose.AsyncImage
import com.phobos.emulator.R
import com.phobos.emulator.ui.theme.ConsoleArtPalette
import com.phobos.emulator.ui.theme.LegibleText
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.libraryTileFill
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.romTitle

@Composable
fun LibraryScreen(viewModel: MainViewModel, onSystemClick: (String) -> Unit) {
    val systems by viewModel.visibleSystems.collectAsState()
    val running by viewModel.runningGame.collectAsState()
    val settings by viewModel.settings.collectAsState()
    var confirmQuit by remember { mutableStateOf(false) }
    val theme = LocalPhobosTheme.current
    // A solid style's tiles are opaque, whatever the glass level.
    val tileAlpha = if (theme.solid != null) 1f else theme.glass.panelAlpha
    val artPalette = remember(theme.scheme, theme.isDark, theme.success, theme.warning, tileAlpha) {
        val tile = libraryTileFill(theme.scheme, theme.isDark).copy(alpha = tileAlpha).compositeOver(theme.scheme.background)
        ConsoleArtPalette(theme.scheme, theme.success, theme.warning, tile)
    }
    val scheme = MaterialTheme.colorScheme
    val tileFill = remember(scheme, theme.isDark) { libraryTileFill(scheme, theme.isDark) }

    Box(modifier = Modifier.fillMaxSize()) {
        Image(
            painter = painterResource(id = R.drawable.phobos_logo),
            contentDescription = null,
            modifier = Modifier
                .size(400.dp)
                .align(Alignment.BottomEnd)
                .offset(x = 100.dp, y = 100.dp)
                .alpha(0.10f),
            contentScale = ContentScale.Fit,
            colorFilter = artPalette.logoFilter
        )

        if (systems.isEmpty()) {
            // A game a frontend started can be running with no ROM folders set up here.
            running?.let { game ->
                Box(Modifier.padding(pageContentPadding())) {
                    RunningGameCard(game, tileFill, onResume = { viewModel.swapBackToGame() }, onQuit = { confirmQuit = true })
                }
            }
            Box(modifier = Modifier.fillMaxSize().padding(bottom = LocalDockInset.current), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Image(
                        painter = painterResource(id = R.drawable.phobos_logo),
                        contentDescription = "Phobos Logo",
                        modifier = Modifier.size(120.dp),
                        alpha = 0.3f,
                        colorFilter = artPalette.logoFilter
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    LegibleText(
                        "No systems found",
                        style = MaterialTheme.typography.titleLarge,
                        color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f)
                    )
                }
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 140.dp),
                contentPadding = pageContentPadding(),
                verticalArrangement = Arrangement.spacedBy(16.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                item(span = { GridItemSpan(maxLineSpan) }) {
                    ScreenHeader("Library", "${systems.size} ${if (systems.size == 1) "system" else "systems"}")
                }
                running?.let { game ->
                    item(span = { GridItemSpan(maxLineSpan) }) {
                        RunningGameCard(game, tileFill, onResume = { viewModel.swapBackToGame() }, onQuit = { confirmQuit = true })
                    }
                }
                items(systems) { system ->
                    SystemCard(system, artPalette, tileFill, onClick = { 
                        onSystemClick(Uri.encode(system)) 
                    })
                }
            }
        }
    }

    val quitting = running
    if (confirmQuit && quitting != null) {
        PhobosAlertDialog(
            onDismissRequest = { confirmQuit = false },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = false); Text("Quit Emulation") },
            text = { Text("Are you sure you want to stop emulating ${romTitle(quitting.romName)}?") },
            confirmButton = {
                // As from the pause menu: a game another app started returns there
                TextButton(onClick = { confirmQuit = false; viewModel.unloadSystem(); viewModel.leaveGame() }) { Text("Quit") }
            },
            dismissButton = { TextButton(onClick = { confirmQuit = false }) { Text("Cancel") } },
        )
    }
}

/** The game paused behind the Library: its last frame, name and system, with Resume and Quit. Tapping it resumes too. */
@Composable
private fun RunningGameCard(game: RunningGame, fill: Color, onResume: () -> Unit, onQuit: () -> Unit) {
    val frame = remember(game.frame) { game.frame?.asImageBitmap() }
    val frameAspect = game.frame?.let { it.width.toFloat() / it.height } ?: (4f / 3f)
    ThemedCard(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.small,
        onClick = onResume,
        fill = fill,
    ) {
        Row(modifier = Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .height(96.dp)
                    .aspectRatio(frameAspect)
                    .clip(MaterialTheme.shapes.extraSmall)
                    .background(Color.Black),
            ) {
                if (frame != null) Image(frame, contentDescription = null, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
            }
            Spacer(Modifier.width(16.dp))
            Column(modifier = Modifier.weight(1f)) {
                Text("Running", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                Text(romTitle(game.romName), style = MaterialTheme.typography.titleMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(game.systemName, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Spacer(Modifier.width(12.dp))
            Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(onClick = onResume, modifier = Modifier.focusRing(MaterialTheme.colorScheme.onPrimary, pillShape()), shape = pillShape()) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                    Spacer(Modifier.width(4.dp))
                    Text("Resume")
                }
                OutlinedButton(onClick = onQuit, modifier = Modifier.focusRing(MaterialTheme.colorScheme.primary, pillShape()), shape = pillShape()) { Text("Quit") }
            }
        }
    }
}

/** Console tile: a themed card in [fill] ([libraryTileFill]), with the console in the theme's colors and an outlined name. */
@Composable
fun SystemCard(system: String, artPalette: ConsoleArtPalette, fill: Color, onClick: () -> Unit) {
    ThemedCard(
        modifier = Modifier
            .fillMaxWidth()
            .height(160.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = fill,
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
                val asset = getSystemIcon(system)
                if (asset != null) {
                    AsyncImage(
                        model = ConsoleArt(asset, artPalette),
                        contentDescription = system,
                        modifier = Modifier.size(80.dp),
                        contentScale = ContentScale.Fit
                    )
                } else {
                    Image(
                        painter = painterResource(id = R.drawable.phobos_logo),
                        contentDescription = system,
                        modifier = Modifier.size(80.dp),
                        contentScale = ContentScale.Fit,
                        colorFilter = artPalette.logoFilter
                    )
                }
            }
            LegibleText(
                system,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2
            )
        }
    }
}

/** The system's illustration in `assets/platforms`, or null for systems without one. */
private fun getSystemIcon(system: String): String? {
    return when {
        system.contains("Neo Geo Pocket Color", ignoreCase = true) -> "neo-geo-pocket-color"
        system.contains("Neo Geo Pocket", ignoreCase = true) -> "neo-geo-pocket"
        system.contains("Neo Geo", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "neo-geo-cd"
        system.contains("Neo Geo", ignoreCase = true) -> "neogeomvs"
        system.contains("Mega Drive", ignoreCase = true) || system.contains("Genesis", ignoreCase = true) -> "genesis"
        system.contains("SNES", ignoreCase = true) || system.contains("Super Famicom", ignoreCase = true) -> "snes"
        system.contains("Super Game Boy", ignoreCase = true) -> "snes"
        system.contains("NES", ignoreCase = true) || system.contains("Famicom", ignoreCase = true) -> "nes"
        system.contains("Nintendo 64", ignoreCase = true) -> "n64"
        system.contains("Game Boy Advance", ignoreCase = true) -> "gba"
        system.contains("Game Boy Color", ignoreCase = true) -> "gbc"
        system.contains("Game Boy", ignoreCase = true) -> "gb"
        system.contains("PlayStation", ignoreCase = true) -> "psx"
        system.contains("Game Gear", ignoreCase = true) -> "gamegear"
        system.contains("MSX2", ignoreCase = true) -> "msx2"
        system.contains("MSX", ignoreCase = true) -> "msx"
        system == "Mega LD" || system == "PC Engine LD" -> "laseractive"
        system.contains("PC Engine", ignoreCase = true) && system.contains("CD", ignoreCase = true) -> "pcecd"
        system.contains("PC Engine", ignoreCase = true) || system.contains("PCE", ignoreCase = true) || system.contains("TurboGrafx", ignoreCase = true) -> "pce"
        system.contains("Sega 32X", ignoreCase = true) || system.contains("32X", ignoreCase = true) -> "sega32"
        system.contains("Mega CD", ignoreCase = true) -> "segacd"
        system.contains("Master System", ignoreCase = true) || system.contains("Mark III", ignoreCase = true) || system.contains("SG-1000", ignoreCase = true) -> "sms"
        system.contains("ColecoVision", ignoreCase = true) -> "colecovision"
        system.contains("Atari 2600", ignoreCase = true) -> "atari2600"
        system.contains("LaserActive", ignoreCase = true) -> "laseractive"
        system.contains("SuperGrafx", ignoreCase = true) -> "supergrafx"
        system.contains("WonderSwan Color", ignoreCase = true) -> "wonderswan-color"
        system.contains("WonderSwan", ignoreCase = true) || system.contains("Pocket Challenge", ignoreCase = true) -> "wonderswan"
        system.contains("ZX Spectrum 128", ignoreCase = true) || system.contains("ZX Spectrum", ignoreCase = true) -> "zx-spectrum"
        system.contains("Arcade", ignoreCase = true) -> "arcade"
        else -> null
    }
}
