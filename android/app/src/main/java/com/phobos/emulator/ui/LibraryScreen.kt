package com.phobos.emulator.ui

import android.net.Uri
import android.view.KeyEvent
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.*
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Star
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import coil.compose.AsyncImage
import com.phobos.emulator.R
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.data.PlatformIconPack
import com.phobos.emulator.ui.theme.ConsoleArtPalette
import com.phobos.emulator.ui.theme.ConsoleColors
import com.phobos.emulator.ui.theme.ConsoleTreatment
import com.phobos.emulator.ui.theme.CrtUi
import com.phobos.emulator.ui.theme.LegibleText
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.MangaUi
import com.phobos.emulator.ui.theme.PixelConsole
import com.phobos.emulator.ui.theme.PixelUi
import com.phobos.emulator.ui.theme.RpgUi
import com.phobos.emulator.ui.theme.consoleTreatment
import com.phobos.emulator.ui.theme.crtBloomColor
import com.phobos.emulator.ui.theme.libraryTileFill
import com.phobos.emulator.ui.theme.pillShape
import com.phobos.emulator.util.gameTitle
import com.phobos.emulator.util.romTitle
import kotlinx.coroutines.launch

@Composable
fun LibraryScreen(viewModel: MainViewModel, onSystemClick: (String) -> Unit) {
    val systems by viewModel.visibleSystems.collectAsState()
    val running by viewModel.runningGame.collectAsState()
    val settings by viewModel.settings.collectAsState()
    val rows by viewModel.libraryRows.collectAsState()
    var confirmQuit by remember { mutableStateOf(false) }
    var menu by remember { mutableStateOf<GameMenuRequest?>(null) }
    var choosingDisc by remember { mutableStateOf<RecordStart.Ready?>(null) }
    var notFound by remember { mutableStateOf<GameRecord?>(null) }
    var starting by remember { mutableStateOf(false) }
    val continueFocus = remember { RowFocus() }
    val favoritesFocus = remember { RowFocus() }
    val firstSystem = remember { FocusRequester() }
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val theme = LocalPhobosTheme.current
    // A solid style's tiles are opaque, whatever the glass level.
    val tileAlpha = if (theme.solid != null) 1f else theme.glass.panelAlpha
    val treatment = if (settings.platformIconPack == PlatformIconPack.SYSTEMATIC) ConsoleTreatment.SYSTEMATIC else settings.uiEffects.consoleTreatment
    val art = remember(theme.scheme, theme.isDark, theme.success, theme.warning, tileAlpha, theme.crtBackdrop, treatment) {
        val tile = libraryTileFill(theme.scheme, theme.isDark).copy(alpha = tileAlpha).compositeOver(theme.scheme.background)
        val palette = ConsoleArtPalette(theme.scheme, theme.success, theme.warning, tile)
        LibraryArt(treatment, palette, ConsoleColors(palette, theme.scheme, theme.isDark, tile, crtBloomColor(theme.scheme, theme.crtBackdrop)))
    }
    val scheme = MaterialTheme.colorScheme
    val tileFill = remember(scheme, theme.isDark) { libraryTileFill(scheme, theme.isDark) }
    val mangaPage = theme.style is MangaUi
    val gridSpacing = when {
        mangaPage -> 10.dp
        theme.style is PixelUi || theme.style is RpgUi -> 12.dp
        else -> 16.dp
    }

    // A row's game starts as a system's list starts it, once its files are found to open; the game behind the Library
    // resumes. One start at a time: a second press while the files are checked does nothing.
    fun start(record: GameRecord) {
        if (starting) return
        starting = true
        scope.launch {
            try {
                when (val next = viewModel.recordStart(record)) {
                    RecordStart.Running -> viewModel.swapBackToGame()
                    RecordStart.SystemPage -> onSystemClick(Uri.encode(record.key.system))
                    RecordStart.NotFound -> {
                        notFound = record
                    }
                    is RecordStart.Ready -> {
                        if (next.rom.discs.size > 1) {
                            choosingDisc = next
                        } else {
                            viewModel.startFromRow(context, next.system, next.rom)
                        }
                    }
                }
            } finally {
                starting = false
            }
        }
    }

    // D-pad down from the Running card goes to the first card under it, where Compose's own search would take the one
    // nearest the card's middle: Continue Playing's first, or with that row empty Favorites' first, or with both empty
    // the first system's tile. One out of view isn't composed, and asking it for the focus throws: the press is then
    // left to Compose.
    fun focusBelowRunning(): Boolean {
        val card = cardBelowRunning(rows)
        val requester = when {
            card == null -> firstSystem
            card.inFavorites -> favoritesFocus.requester(card.key)
            else -> continueFocus.requester(card.key)
        }
        return runCatching { requester.requestFocus() }.isSuccess
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
                        colorFilter = art.palette.logoFilter
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
                item(key = "header", span = { GridItemSpan(maxLineSpan) }) {
                    ScreenHeader("Library", "${systems.size} ${if (systems.size == 1) "system" else "systems"}")
                }
                running?.let { game ->
                    item(key = "running", span = { GridItemSpan(maxLineSpan) }) {
                        RunningGameCard(
                            game, tileFill, onResume = { viewModel.swapBackToGame() }, onQuit = { confirmQuit = true },
                            onDown = { focusBelowRunning() },
                        )
                    }
                }
                // Both rows are always there, with a line when empty, so the grid doesn't shift as the records arrive.
                item(key = "continuePlaying", span = { GridItemSpan(maxLineSpan) }, contentType = "gameRow") {
                    GameRow(
                        title = "Continue Playing",
                        games = rows?.continuePlaying,
                        empty = "Games you play show up here.",
                        focus = continueFocus,
                        running = running,
                        art = art,
                        fill = tileFill,
                        spacing = gridSpacing,
                        onStart = { start(it) },
                        onMenu = { record, heldA -> menu = GameMenuRequest(record, heldA, fromFavorites = false) },
                        onFavorite = { viewModel.setFavourite(it.key, !it.favourite) },
                    )
                }
                item(key = "favorites", span = { GridItemSpan(maxLineSpan) }, contentType = "gameRow") {
                    GameRow(
                        title = "Favorites",
                        games = rows?.favorites,
                        empty = "Games you favorite show up here.",
                        focus = favoritesFocus,
                        running = running,
                        art = art,
                        fill = tileFill,
                        spacing = gridSpacing,
                        onStart = { start(it) },
                        onMenu = { record, heldA -> menu = GameMenuRequest(record, heldA, fromFavorites = true) },
                        onFavorite = { record ->
                            if (record.favourite) favoritesFocus.leaving(record.key)
                            viewModel.setFavourite(record.key, !record.favourite)
                        },
                    )
                }
                items(systems, key = { "system/$it" }) { system ->
                    Box(if (system == systems.first()) Modifier.focusRequester(firstSystem) else Modifier) {
                        SystemCard(
                            system = system,
                            art = art,
                            fill = tileFill,
                            onClick = { onSystemClick(Uri.encode(system)) },
                        )
                    }
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

    menu?.let { request ->
        val record = request.record
        GameMenu(
            title = gameTitle(record.key.name, record.title),
            system = record.key.system,
            items = gameMenuItems(record),
            openedByHeldA = request.heldA,
            fullScreen = settings.fullScreenMode,
            onPick = { item ->
                menu = null
                when (item) {
                    GameMenuItem.ADD_TO_FAVORITES -> viewModel.setFavourite(record.key, true)
                    GameMenuItem.REMOVE_FROM_FAVORITES -> {
                        if (request.fromFavorites) favoritesFocus.leaving(record.key)
                        viewModel.setFavourite(record.key, false)
                    }
                    GameMenuItem.HIDE_FROM_CONTINUE_PLAYING -> {
                        if (!request.fromFavorites) continueFocus.leaving(record.key)
                        viewModel.hideFromContinuePlaying(record.key)
                    }
                }
            },
            onDismiss = { menu = null },
        )
    }

    choosingDisc?.let { ready ->
        DiscChoiceDialog(
            title = romTitle(ready.rom.name),
            discs = ready.rom.discs,
            selected = viewModel.lastDisc(ready.system, ready.rom),
            confirmLabel = "Start",
            fullScreen = settings.fullScreenMode,
            inGame = false,
            onDismiss = { choosingDisc = null },
            onChoose = { disc ->
                choosingDisc = null
                viewModel.startFromRow(context, ready.system, ready.rom, disc)
            },
        )
    }

    notFound?.let { record ->
        val system = record.key.system
        PhobosAlertDialog(
            onDismissRequest = { notFound = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = false); Text("Game Not Found") },
            text = {
                Text(
                    "Phobos couldn't open ${gameTitle(record.key.name, record.title)} where it was last found. Its " +
                        "file may have moved or been deleted, or Phobos may no longer reach its folder. Opening " +
                        "$system looks for it again, and its folder can be added there again.",
                )
            },
            confirmButton = {
                TextButton(onClick = { notFound = null; onSystemClick(Uri.encode(system)) }) { Text("Open $system") }
            },
            dismissButton = { TextButton(onClick = { notFound = null }) { Text("Cancel") } },
        )
    }
}

/** A game's menu, opened from a card by [heldA] or a long press; [fromFavorites] for a card in Favorites. */
private class GameMenuRequest(val record: GameRecord, val heldA: Boolean, val fromFavorites: Boolean)

private val GAME_CARD_WIDTH = 144.dp
private val GAME_CARD_HEIGHT = 176.dp

/**
 * One of the Library's rows of games under [title]: a card for each of [games], scrolling sideways, or the [empty] line
 * when it has none. Until the store has read its file ([games] null) the line keeps its place without showing.
 */
@Composable
private fun GameRow(
    title: String,
    games: List<GameRecord>?,
    empty: String,
    focus: RowFocus,
    running: RunningGame?,
    art: LibraryArt,
    fill: Color,
    spacing: Dp,
    onStart: (GameRecord) -> Unit,
    onMenu: (GameRecord, Boolean) -> Unit,
    onFavorite: (GameRecord) -> Unit,
) {
    LaunchedEffect(games) { games?.let(focus::shown) }
    Column(Modifier.fillMaxWidth()) {
        SectionHeader(title)
        if (games.isNullOrEmpty()) {
            BackdropText(
                empty,
                Modifier.padding(start = 16.dp).alpha(if (games == null) 0f else 1f),
                style = MaterialTheme.typography.bodyMedium,
            )
        } else {
            LazyRow(horizontalArrangement = Arrangement.spacedBy(spacing)) {
                // A lazy list's keys have to fit in a Bundle, so the key is a string; a ROM name can't hold a newline.
                items(games, key = { "${it.key.system}\n${it.key.name}" }) { record ->
                    GameCard(
                        record = record,
                        running = running != null && running.systemName == record.key.system &&
                            running.romName == record.key.name,
                        art = art,
                        fill = fill,
                        modifier = Modifier.focusRequester(focus.requester(record.key)),
                        onStart = { onStart(record) },
                        onMenu = { heldA -> onMenu(record, heldA) },
                        onFavorite = { onFavorite(record) },
                    )
                }
            }
        }
    }
}

/**
 * The focus in one of the Library's rows: each card's requester, and where the focus goes when a card leaves the row
 * ([LeavingCard]).
 */
private class RowFocus {
    private val requesters = HashMap<GameKey, FocusRequester>()
    private val leavingCard = LeavingCard()

    fun requester(key: GameKey): FocusRequester = requesters.getOrPut(key) { FocusRequester() }

    /** [key]'s card has the focus and is about to leave the row. */
    fun leaving(key: GameKey) = leavingCard.leaving(key)

    /** The row now shows [now]; a leaving card that has gone hands the focus on. */
    fun shown(now: List<GameRecord>) {
        val keys = now.map { it.key }
        // A card not composed (scrolled out of view) can't take the focus, nor can any in touch mode; the next D-pad
        // press finds one.
        leavingCard.shown(keys)?.let { next -> runCatching { requester(next).requestFocus() } }
        requesters.keys.retainAll(keys.toSet())
    }
}

/**
 * A game in one of the Library's rows: its system's console art, its title and its system, marked Running or Not found
 * when so, with a star when it's a favorite. A CRT card blinks the cursor after its title while it has the focus.
 */
@Composable
private fun GameCard(
    record: GameRecord,
    running: Boolean,
    art: LibraryArt,
    fill: Color,
    modifier: Modifier,
    onStart: () -> Unit,
    onMenu: (heldA: Boolean) -> Unit,
    onFavorite: () -> Unit,
) {
    val theme = LocalPhobosTheme.current
    val scheme = MaterialTheme.colorScheme
    val crt = theme.style is CrtUi
    val source = remember { MutableInteractionSource() }
    var focused by remember { mutableStateOf(false) }
    val titleStyle = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold)
    val titleColor = if (crt) scheme.primary else scheme.onSurface
    val title = gameTitle(record.key.name, record.title)
    ThemedCard(
        modifier = modifier
            .width(GAME_CARD_WIDTH)
            .height(GAME_CARD_HEIGHT)
            .onFocusChanged { focused = it.hasFocus }
            .gameButtons(source, onClick = onStart, onMenu = { onMenu(true) }, onFavorite = onFavorite),
        shape = MaterialTheme.shapes.small,
        onClick = onStart,
        accentText = record.missing,
        fill = fill,
        onLongClick = { onMenu(false) },
        interactionSource = source,
    ) {
        Column(modifier = Modifier.padding(10.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
                SystemIcon(record.key.system, art, size = 64.dp)
                if (record.favourite) {
                    Icon(
                        Icons.Rounded.Star,
                        contentDescription = "Favorite",
                        tint = scheme.primary,
                        modifier = Modifier.align(Alignment.TopEnd).size(18.dp),
                    )
                }
            }
            when {
                running -> Text("Running", style = MaterialTheme.typography.labelSmall, color = scheme.primary)
                record.missing -> Text("Not found", style = MaterialTheme.typography.labelSmall, color = scheme.error)
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                val titleModifier = Modifier.weight(1f, fill = false)
                // As the system tiles: outlined on glass, plain on a solid style's opaque panel.
                if (theme.solid == null) {
                    LegibleText(
                        title, titleModifier, titleColor, titleStyle, TextAlign.Center,
                        maxLines = 2, overflow = TextOverflow.Ellipsis,
                    )
                } else {
                    Text(
                        title, titleModifier, titleColor, style = titleStyle, textAlign = TextAlign.Center,
                        maxLines = 2, overflow = TextOverflow.Ellipsis,
                    )
                }
                if (crt) TitleCursor(titleStyle, titleColor, shown = focused)
            }
            Text(
                record.key.system,
                style = MaterialTheme.typography.bodySmall,
                color = scheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

/** How the Library's consoles are drawn: in [treatment], from [palette]'s recoloring and [colors]. */
@Immutable
class LibraryArt(val treatment: ConsoleTreatment, val palette: ConsoleArtPalette, val colors: ConsoleColors)

/**
 * The game paused behind the Library: its last frame, name and system, with Resume and Quit. Tapping it resumes too.
 * [onDown] takes D-pad down while the card itself has the focus, true when it moved the focus.
 */
@Composable
private fun RunningGameCard(
    game: RunningGame,
    fill: Color,
    onResume: () -> Unit,
    onQuit: () -> Unit,
    onDown: () -> Boolean = { false },
) {
    val frame = remember(game.frame) { game.frame?.asImageBitmap() }
    val frameAspect = game.frame?.let { it.width.toFloat() / it.height } ?: (4f / 3f)
    // The card itself, not its Resume or Quit (which a keyboard's Tab reaches).
    var focused by remember { mutableStateOf(false) }
    ThemedCard(
        modifier = Modifier
            .fillMaxWidth()
            .onFocusChanged { focused = it.isFocused }
            .onPreviewKeyEvent { event ->
                val key = event.nativeKeyEvent
                focused && key.keyCode == KeyEvent.KEYCODE_DPAD_DOWN && key.action == KeyEvent.ACTION_DOWN && onDown()
            },
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
    art: LibraryArt,
    fill: Color,
    onClick: () -> Unit,
) {
    when (LocalPhobosTheme.current.style) {
        is MangaUi -> MangaPanelSystemCard(system, art, fill, onClick)
        is PixelUi -> PixelBoxSystemCard(system, art, fill, onClick)
        is RpgUi -> RpgWindowSystemCard(system, art, fill, onClick)
        is CrtUi -> CrtPanelSystemCard(system, art, fill, onClick)
        else -> GlassSystemCard(system, art, fill, onClick)
    }
}

@Composable
private fun GlassSystemCard(
    system: String,
    art: LibraryArt,
    fill: Color,
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
                SystemIcon(system, art, size = 80.dp)
            }
            LegibleText(
                system,
                style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
            )
            ExperimentalBadge(system, Modifier.padding(top = 4.dp))
        }
    }
}

/** A manga panel: paper fill, ink border (from [MangaUi]), art above a caption box for the name. */
@Composable
private fun MangaPanelSystemCard(
    system: String,
    art: LibraryArt,
    fill: Color,
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
                SystemIcon(system, art, size = 78.dp)
            }
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(paper)
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    system.uppercase(),
                    style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
                ExperimentalBadge(system, Modifier.padding(top = 4.dp))
            }
        }
    }
}

/** A pixel-art box: stepped corners and a solid pixel panel around the console. */
@Composable
private fun PixelBoxSystemCard(
    system: String,
    art: LibraryArt,
    fill: Color,
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
                SystemIcon(system, art, size = 72.dp)
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
            ExperimentalBadge(system, Modifier.padding(top = 4.dp))
        }
    }
}

/** A 16-bit RPG text window: framed panel with the console and name laid out like a menu entry. */
@Composable
private fun RpgWindowSystemCard(
    system: String,
    art: LibraryArt,
    fill: Color,
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
                SystemIcon(system, art, size = 72.dp)
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
            ExperimentalBadge(system, Modifier.padding(top = 4.dp))
        }
    }
}

/**
 * A CRT terminal panel: boxed window with the console name, and a blinking block cursor after it, like LIBRARY's,
 * while the panel has the focus (one cursor among the panels, not one in each).
 */
@Composable
private fun CrtPanelSystemCard(
    system: String,
    art: LibraryArt,
    fill: Color,
    onClick: () -> Unit,
) {
    val nameStyle = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold)
    val nameColor = MaterialTheme.colorScheme.primary
    var focused by remember { mutableStateOf(false) }
    ThemedCard(
        modifier = Modifier.fillMaxWidth().height(160.dp).onFocusChanged { focused = it.hasFocus },
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
                SystemIcon(system, art, size = 80.dp)
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
                TitleCursor(nameStyle, nameColor, shown = focused)
            }
            ExperimentalBadge(system, Modifier.padding(top = 4.dp))
        }
    }
}

@Composable
private fun SystemIcon(system: String, art: LibraryArt, size: androidx.compose.ui.unit.Dp) {
    val asset = systemIconSlug(system)?.let(::consoleArtFor)
    when {
        asset == null -> Image(
            painter = painterResource(id = R.drawable.phobos_logo),
            contentDescription = system,
            modifier = Modifier.size(size),
            contentScale = ContentScale.Fit,
            colorFilter = art.palette.logoFilter,
        )
        art.treatment.pixelated -> PixelConsole(asset, art.treatment, art.colors, system, Modifier.size(size))
        else -> {
            val context = LocalContext.current
            val box = with(LocalDensity.current) { size.roundToPx() }
            AsyncImage(
                model = remember(asset, art, box) { consoleArtRequest(context, asset, art, box) },
                contentDescription = system,
                modifier = Modifier.size(size),
                contentScale = if (art.treatment == ConsoleTreatment.SYSTEMATIC) ContentScale.Fit else ContentScale.Inside,
            )
        }
    }
}
