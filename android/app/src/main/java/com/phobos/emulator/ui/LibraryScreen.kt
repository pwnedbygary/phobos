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
import androidx.compose.ui.draw.clip
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
import com.phobos.emulator.data.PlatformIconPack
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.ui.theme.ConsoleArtPalette
import com.phobos.emulator.ui.theme.CrtUi
import com.phobos.emulator.ui.theme.LegibleText
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.MangaUi
import com.phobos.emulator.ui.theme.PixelUi
import com.phobos.emulator.ui.theme.PlatformGlyphIcon
import com.phobos.emulator.ui.theme.PlatformGlyphStyle
import com.phobos.emulator.ui.theme.RpgUi
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
    val iconPack = remember(settings.platformIconPack, settings.uiEffects) {
        resolveIconPack(settings.platformIconPack, settings.uiEffects)
    }
    val mangaPage = theme.style is MangaUi
    val gridSpacing = when {
        mangaPage -> 10.dp
        theme.style is PixelUi || theme.style is RpgUi -> 12.dp
        else -> 16.dp
    }

    Box(modifier = Modifier.fillMaxSize()) {
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
                columns = GridCells.Adaptive(minSize = if (mangaPage) 150.dp else 140.dp),
                contentPadding = pageContentPadding(),
                verticalArrangement = Arrangement.spacedBy(gridSpacing),
                horizontalArrangement = Arrangement.spacedBy(gridSpacing)
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
                    SystemCard(
                        system = system,
                        artPalette = artPalette,
                        fill = tileFill,
                        iconPack = iconPack,
                        onClick = { onSystemClick(Uri.encode(system)) },
                    )
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

/** Resolves Match style to a concrete pack from the active style effects. */
fun resolveIconPack(pack: PlatformIconPack, effects: UiEffects): PlatformIconPack = when (pack) {
    PlatformIconPack.MATCH_STYLE -> when (effects) {
        UiEffects.PIXEL_ART -> PlatformIconPack.PIXEL
        UiEffects.MANGA -> PlatformIconPack.MANGA
        UiEffects.RPG -> PlatformIconPack.PIXEL
        else -> PlatformIconPack.SYSTEMATIC
    }
    else -> pack
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

/**
 * Console tile. Manga ink turns each system into a panel with a caption strip; Pixel art uses a
 * chunky pixel box; 16-bit RPG uses a framed menu window; Retrowave / glass keep the glowing cards.
 */
@Composable
fun SystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    when (LocalPhobosTheme.current.style) {
        is MangaUi -> MangaPanelSystemCard(system, artPalette, fill, iconPack, onClick)
        is PixelUi -> PixelBoxSystemCard(system, artPalette, fill, iconPack, onClick)
        is RpgUi -> RpgWindowSystemCard(system, artPalette, fill, iconPack, onClick)
        is CrtUi -> CrtPanelSystemCard(system, artPalette, fill, iconPack, onClick)
        else -> GlassSystemCard(system, artPalette, fill, iconPack, onClick)
    }
}

@Composable
private fun GlassSystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(160.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = fill,
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
                SystemIcon(system, artPalette, iconPack, size = 80.dp)
            }
            LegibleText(
                system,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
            )
        }
    }
}

/** A manga panel: paper fill, ink border (from [MangaUi]), art above a caption box for the name. */
@Composable
private fun MangaPanelSystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    val paper = MaterialTheme.colorScheme.background
    val panelFill = fill.copy(alpha = 0.92f).compositeOver(paper)
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(168.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = panelFill,
    ) {
        Column(Modifier.fillMaxSize()) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .padding(horizontal = 10.dp, vertical = 8.dp),
                contentAlignment = Alignment.Center,
            ) {
                SystemIcon(system, artPalette, iconPack, size = 78.dp)
            }
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(paper)
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    system.uppercase(),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

/** A pixel-art box: stepped corners and a solid pixel panel around the console glyph. */
@Composable
private fun PixelBoxSystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(156.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = fill,
    ) {
        Column(
            modifier = Modifier.padding(10.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .clip(MaterialTheme.shapes.extraSmall)
                    .background(fill.copy(alpha = 0.55f).compositeOver(MaterialTheme.colorScheme.background)),
                contentAlignment = Alignment.Center,
            ) {
                SystemIcon(system, artPalette, iconPack, size = 72.dp)
            }
            Spacer(Modifier.height(8.dp))
            Text(
                system.uppercase(),
                style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.Bold),
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/** A 16-bit RPG text window: framed panel with the console and name laid out like a menu entry. */
@Composable
private fun RpgWindowSystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(156.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = fill,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 12.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
                SystemIcon(system, artPalette, iconPack, size = 72.dp)
            }
            Spacer(Modifier.height(6.dp))
            Text(
                system,
                style = MaterialTheme.typography.labelLarge,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/** A CRT terminal panel: boxed window with the console name and a blinking block cursor, like LIBRARY. */
@Composable
private fun CrtPanelSystemCard(
    system: String,
    artPalette: ConsoleArtPalette,
    fill: Color,
    iconPack: PlatformIconPack,
    onClick: () -> Unit,
) {
    val nameStyle = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold)
    val nameColor = MaterialTheme.colorScheme.primary
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(160.dp),
        shape = MaterialTheme.shapes.small,
        onClick = onClick,
        fill = fill,
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
                SystemIcon(system, artPalette, iconPack, size = 80.dp)
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    system.uppercase(),
                    style = nameStyle,
                    textAlign = TextAlign.Center,
                    color = nameColor,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                )
                TitleCursor(nameStyle, nameColor)
            }
        }
    }
}

@Composable
private fun SystemIcon(
    system: String,
    artPalette: ConsoleArtPalette,
    iconPack: PlatformIconPack,
    size: androidx.compose.ui.unit.Dp,
) {
    val slug = systemIconSlug(system)
    val glyphStyle = when (iconPack) {
        PlatformIconPack.PIXEL -> PlatformGlyphStyle.PIXEL
        PlatformIconPack.MANGA -> PlatformGlyphStyle.MANGA
        PlatformIconPack.PHOBOS -> PlatformGlyphStyle.PHOBOS
        else -> null
    }
    if (glyphStyle != null && slug != null) {
        val scheme = MaterialTheme.colorScheme
        PlatformGlyphIcon(
            slug = slug,
            style = glyphStyle,
            accent = scheme.primary,
            ink = scheme.onSurface,
            fill = scheme.surfaceContainerHighest,
            // The silhouettes are wider than they are tall, so a wider box draws them larger in the same tile.
            modifier = Modifier.size(width = size * 1.4f, height = size),
        )
    } else if (slug != null) {
        val asset = systematicAssetFor(slug)
        if (asset != null) {
            AsyncImage(
                model = ConsoleArt(asset, artPalette),
                contentDescription = system,
                modifier = Modifier.size(size),
                contentScale = ContentScale.Fit,
            )
        } else {
            // Newer systems without a Systematic SVG still get a Phobos glyph.
            PlatformGlyphIcon(
                slug = slug,
                style = PlatformGlyphStyle.PHOBOS,
                accent = MaterialTheme.colorScheme.primary,
                ink = MaterialTheme.colorScheme.onSurface,
                fill = MaterialTheme.colorScheme.surfaceContainerHighest,
                modifier = Modifier.size(size),
            )
        }
    } else {
        Image(
            painter = painterResource(id = R.drawable.phobos_logo),
            contentDescription = system,
            modifier = Modifier.size(size),
            contentScale = ContentScale.Fit,
            colorFilter = artPalette.logoFilter,
        )
    }
}

/**
 * Basename under assets/platforms for the Systematic pack, or null when none ships.
 * Glyph-only slugs (e.g. `sgb`) alias to the closest shipped SVG so Systematic stays Systematic.
 */
private fun systematicAssetFor(slug: String): String? = when (slug) {
    "sgb" -> "snes"
    "pceld" -> "laseractive"
    "pcv2" -> "wonderswan"
    else -> slug.takeIf { it in SYSTEMATIC_ASSETS }
}
private val SYSTEMATIC_ASSETS = setOf(
    "atari2600", "colecovision", "nes", "snes", "arcade", "laseractive", "n64",
    "gb", "gbc", "gba", "sms", "genesis", "sega32", "gamegear", "segacd", "psx",
    "neogeomvs", "neo-geo-cd", "neo-geo-pocket", "neo-geo-pocket-color", "zx-spectrum",
    "pce", "pcecd", "supergrafx", "wonderswan", "wonderswan-color", "msx", "msx2",
)
