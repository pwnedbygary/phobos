package com.phobos.emulator.ui.cartridge

import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StarBorder
import androidx.compose.material.icons.rounded.ViewInAr
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.ui.DiscChoiceDialog
import com.phobos.emulator.ui.DialogSystemBars
import com.phobos.emulator.ui.GameMenu
import com.phobos.emulator.ui.GameMenuItem
import com.phobos.emulator.ui.MainViewModel
import com.phobos.emulator.ui.PhobosAlertDialog
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.ui.carts.CartBitmapCache
import com.phobos.emulator.ui.carts.CartLooks
import com.phobos.emulator.ui.carts.CartShapes
import com.phobos.emulator.ui.carts.CartThumbnailer
import com.phobos.emulator.ui.carts.LookResolver
import com.phobos.emulator.ui.carts.CartsEngine
import com.phobos.emulator.ui.carts.rememberCartCardFile
import com.phobos.emulator.ui.gameMenuItems
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.util.displayTitle
import com.phobos.emulator.util.romTitle
import java.io.File

/** The four ways Cartridge shows a system's games. */
enum class CartridgeView(val label: String) {
    Carousel("Carousel"),
    Grid("Grid"),
    Shelf("Shelf"),
    Tower("Tower"),
}

/**
 * A system's page in Cartridge's language: a live focused cart, the game's name and marks, and the four view
 * modes. It replaces the styled system page only for the Cartridge effect; scanning, the folder picker, the game
 * menu and the start path are the same.
 */
@Composable
fun CartridgeSystemScreen(
    encodedSystemName: String,
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onRomClick: (String, String) -> Unit,
) {
    val systemName = remember(encodedSystemName) { Uri.decode(encodedSystemName) }
    val context = LocalContext.current
    val settings by viewModel.settings.collectAsState()
    val gameLoaded by viewModel.gameLoaded.collectAsState()
    val roms by viewModel.roms.collectAsState()
    val records by viewModel.gameRecords.collectAsState()
    val boxes by viewModel.artBoxes.collectAsState()
    val labels by viewModel.artLabels.collectAsState()
    val theme = LocalPhobosTheme.current
    val themeKey = theme.theme.name
    val facts = factsFor(systemName)
    val shape = CartShapes.cartShape(systemName)
    val canLive = settings.carts && !gameLoaded && shape != null
    val engine = remember(canLive) { if (canLive) CartsEngine.get(context) else null }
    val live = canLive && engine != null
    val thumbnailer = remember(engine) { engine?.let { CartThumbnailer(context, it) } }
    val cache = remember { CartBitmapCache(File(context.cacheDir, "cart-render"), 64L * 1024 * 1024) }
    DisposableEffect(thumbnailer) { onDispose { thumbnailer?.release() } }
    val recorded = remember(records, systemName) {
        records.orEmpty().filter { it.key.system == systemName }.associateBy { it.key.name }
    }
    var chooseView by rememberSaveable(systemName) { mutableStateOf(CartridgeView.Carousel.name) }
    val view = CartridgeView.entries.firstOrNull { it.name == chooseView } ?: CartridgeView.Carousel
    var alphabetical by rememberSaveable(systemName) { mutableStateOf(true) }
    var focused by rememberSaveable(systemName) { mutableIntStateOf(0) }
    var menuFor by remember { mutableStateOf<Pair<RomFile, Boolean>?>(null) }
    var cartOptionsFor by remember { mutableStateOf<RomFile?>(null) }
    var choosingDisc by remember { mutableStateOf<RomFile?>(null) }
    var notFound by remember { mutableStateOf<Pair<String, String>?>(null) }

    val sorted = remember(roms, alphabetical, recorded) {
        if (alphabetical) {
            roms.sortedWith(compareBy({ displayTitle(it.name, it.title).lowercase() }, { it.name }))
        } else {
            roms.sortedByDescending { recorded[it.name]?.lastPlayed ?: 0L }
        }
    }
    LaunchedEffect(sorted.size) { if (focused > sorted.lastIndex) focused = sorted.lastIndex.coerceAtLeast(0) }

    val directoryUris = remember(settings.systemRomPaths[systemName]) {
        settings.systemRomPaths[systemName]?.map { Uri.parse(it) } ?: emptyList()
    }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(
                uri,
                android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION or android.content.Intent.FLAG_GRANT_WRITE_URI_PERMISSION,
            )
            viewModel.addSystemRomPath(systemName, uri.toString())
        }
    }
    LaunchedEffect(directoryUris) {
        if (directoryUris.isNotEmpty()) viewModel.scanRoms(context, systemName, directoryUris)
        else viewModel.clearRoms()
    }

    val start: (RomFile) -> Unit = { rom ->
        if (viewModel.isRunning(systemName, rom.name)) {
            viewModel.swapBackToGame()
        } else if (rom.discs.size > 1) {
            choosingDisc = rom
        } else {
            viewModel.loadRom(context, systemName, rom)
            onRomClick(Uri.encode(systemName), Uri.encode(rom.name))
        }
    }

    BackHandler { onBack() }

    val root = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { root.requestFocus() } }
    fun move(delta: Int) {
        if (sorted.isEmpty()) return
        focused = (focused + delta).coerceIn(sorted.indices)
    }
    fun cycle(step: Int) {
        val modes = CartridgeView.entries
        chooseView = modes[(modes.indexOf(view) + step + modes.size) % modes.size].name
    }
    Box(
        Modifier
            .fillMaxSize()
            .focusRequester(root)
            .focusable()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val key = event.nativeKeyEvent.keyCode
                val accept = key == android.view.KeyEvent.KEYCODE_BUTTON_A ||
                    key == android.view.KeyEvent.KEYCODE_ENTER ||
                    key == android.view.KeyEvent.KEYCODE_NUMPAD_ENTER ||
                    key == android.view.KeyEvent.KEYCODE_DPAD_CENTER
                when (key) {
                    android.view.KeyEvent.KEYCODE_DPAD_LEFT -> move(-1)
                    android.view.KeyEvent.KEYCODE_DPAD_RIGHT -> move(1)
                    android.view.KeyEvent.KEYCODE_DPAD_UP -> move(-1)
                    android.view.KeyEvent.KEYCODE_DPAD_DOWN -> move(1)
                    android.view.KeyEvent.KEYCODE_BUTTON_L1 -> cycle(-1)
                    android.view.KeyEvent.KEYCODE_BUTTON_R1 -> cycle(1)
                    android.view.KeyEvent.KEYCODE_BUTTON_X -> {
                        sorted.getOrNull(focused)?.let { rom ->
                            val key = GameKey(systemName, rom.name)
                            viewModel.setFavourite(key, !(recorded[rom.name]?.favourite == true), rom)
                        }
                    }
                    android.view.KeyEvent.KEYCODE_BUTTON_Y,
                    android.view.KeyEvent.KEYCODE_BUTTON_START,
                    android.view.KeyEvent.KEYCODE_MENU -> sorted.getOrNull(focused)?.let { menuFor = it to false }
                    android.view.KeyEvent.KEYCODE_Q -> alphabetical = !alphabetical
                    else -> {
                        if (accept) {
                            sorted.getOrNull(focused)?.let(start)
                        } else {
                            return@onPreviewKeyEvent false
                        }
                    }
                }
                true
            },
    ) {
        Box(
            Modifier
                .fillMaxSize()
                .background(
                    androidx.compose.ui.graphics.Brush.radialGradient(
                        colors = listOf(facts.color.copy(alpha = 0.30f), MaterialTheme.colorScheme.background),
                        radius = 1500f,
                    ),
                ),
        )
        CartridgeScrim(Modifier.fillMaxSize())
        Column(Modifier.fillMaxSize()) {
            SystemHeader(
                system = systemName,
                facts = facts,
                view = view,
                position = if (sorted.isEmpty()) 0 else focused + 1,
                count = sorted.size,
                alphabetical = alphabetical,
                onBack = onBack,
                onView = { chooseView = it.name },
                onSort = { alphabetical = !alphabetical },
                onFolder = { launcher.launch(null) },
            )
            if (directoryUris.isEmpty() || sorted.isEmpty()) {
                EmptySystem(
                    system = systemName,
                    hasFolders = directoryUris.isNotEmpty(),
                    folders = directoryUris.map { it.toString() },
                    onAdd = { launcher.launch(null) },
                    onRemove = { uri -> viewModel.removeSystemRomPath(systemName, uri.toString()) },
                    modifier = Modifier.weight(1f),
                )
            } else {
                val focusedRom = sorted.getOrNull(focused)
                val record = focusedRom?.let { recorded[it.name] }
                val box = focusedRom?.let { boxes[GameKey(systemName, it.name)] }
                val label = if (settings.realLabels) focusedRom?.let { labels[GameKey(systemName, it.name)] } else null
                val focusedLook = remember(settings.cartsLooks, systemName, focusedRom, settings.cartsSpecialEditions) {
                    focusedRom?.let {
                        LookResolver.resolve(
                            LookResolver.GameInput(CartLooks.gameId(systemName, it.name), it.name),
                            systemName,
                            CartLooks.parse(settings.cartsLooks),
                            settings.cartsSpecialEditions,
                        )
                    }
                }
                val focusedLooks = remember(focusedLook, focusedRom) {
                    if (focusedRom != null && focusedLook != null) mapOf(GameKey(systemName, focusedRom.name) to focusedLook) else emptyMap()
                }
                val cartPicture = if (settings.carts) {
                    focusedRom?.let {
                        rememberCartCardFile(
                            system = systemName,
                            name = it.name,
                            title = cartTitle(it.name, it.title),
                            labelFile = label,
                            box = box,
                            looksJson = settings.cartsLooks,
                            specialEditions = settings.cartsSpecialEditions,
                            wearOn = settings.cartsWear,
                            playTimeMs = record?.playTimeMs ?: 0,
                            themeKey = themeKey,
                            faceKey = themeKey,
                            engraving = settings.cartsEngraving,
                            thumbnailer = thumbnailer,
                            cache = cache,
                            enabled = settings.carts,
                            size = 512,
                        )
                    }
                } else null
                Box(Modifier.weight(1f)) {
                    when (view) {
                        CartridgeView.Carousel -> CarouselView(
                            system = systemName,
                            roms = sorted,
                            focused = focused,
                            labels = if (settings.realLabels) labels else emptyMap(),
                            boxes = boxes,
                            looks = focusedLooks,
                            liveCart = live,
                            looksJson = settings.cartsLooks,
                            cartsEnabled = settings.carts,
                            specialEditions = settings.cartsSpecialEditions,
                            wearOn = settings.cartsWear,
                            engraving = settings.cartsEngraving,
                            themeKey = themeKey,
                            playtimeSeconds = { rom -> (recorded[rom.name]?.playTimeMs ?: 0L) / 1000 },
                            thumbnailer = thumbnailer,
                            cache = cache,
                            onFocus = { focused = it },
                            onStart = start,
                            onMenu = { menuFor = it to false },
                        )
                        CartridgeView.Grid -> CartridgeGrid(
                            system = systemName,
                            roms = sorted,
                            focused = focused,
                            labels = if (settings.realLabels) labels else emptyMap(),
                            boxes = boxes,
                            thumbnailer = thumbnailer,
                            cache = cache,
                            settings = settings,
                            themeKey = themeKey,
                            recorded = recorded,
                            onFocus = { focused = it },
                            onStart = start,
                            onMenu = { menuFor = it to false },
                        )
                        CartridgeView.Shelf -> CartridgeShelf(
                            system = systemName,
                            roms = sorted,
                            focused = focused,
                            labels = if (settings.realLabels) labels else emptyMap(),
                            boxes = boxes,
                            thumbnailer = thumbnailer,
                            cache = cache,
                            settings = settings,
                            themeKey = themeKey,
                            recorded = recorded,
                            onFocus = { focused = it },
                            onStart = start,
                            onMenu = { menuFor = it to false },
                        )
                        CartridgeView.Tower -> CartridgeTower(
                            system = systemName,
                            roms = sorted,
                            focused = focused,
                            facts = facts,
                            focusedRom = focusedRom,
                            cartPicture = cartPicture,
                            box = box,
                            labels = if (settings.realLabels) labels else emptyMap(),
                            boxes = boxes,
                            looks = focusedLooks,
                            liveCart = live,
                            specialEditions = settings.cartsSpecialEditions,
                            wearOn = settings.cartsWear,
                            engraving = settings.cartsEngraving,
                            themeKey = themeKey,
                            playtimeSeconds = { rom -> (recorded[rom.name]?.playTimeMs ?: 0L) / 1000 },
                            onFocus = { focused = it },
                            onStart = start,
                            onMenu = { menuFor = it to false },
                        )
                    }
                }
                GameInfo(
                    system = systemName,
                    facts = facts,
                    rom = focusedRom,
                    record = record,
                    multiDisc = focusedRom?.discs?.size ?: 0,
                )
            }
            CartridgeHints(
                items = listOf(
                    "A" to "Play",
                    "B" to "Back",
                    "X" to "Favorite",
                    "Y" to "Options",
                    "L/R" to "View",
                    "R" to "Stick · Rotate",
                    "Q" to "A–Z",
                ),
            )
        }
    }

    menuFor?.let { (game, heldA) ->
        val key = GameKey(systemName, game.name)
        GameMenu(
            title = displayTitle(game.name, game.title),
            system = systemName,
            items = gameMenuItems(
                recorded[game.name],
                artwork = settings.artDownload && com.phobos.emulator.art.hasArt(systemName),
                carts = settings.carts && shape != null,
            ),
            openedByHeldA = heldA,
            fullScreen = settings.fullScreenMode,
            onPick = { item ->
                menuFor = null
                when (item) {
                    GameMenuItem.ADD_TO_FAVORITES -> viewModel.setFavourite(key, true, game)
                    GameMenuItem.REMOVE_FROM_FAVORITES -> viewModel.setFavourite(key, false, game)
                    GameMenuItem.HIDE_FROM_CONTINUE_PLAYING -> viewModel.hideFromContinuePlaying(key)
                    GameMenuItem.REDOWNLOAD_ARTWORK -> viewModel.redownloadArt(key, game.title)
                    GameMenuItem.CART_OPTIONS -> cartOptionsFor = game
                }
            },
            onDismiss = { menuFor = null },
        )
    }
    cartOptionsFor?.let { rom ->
        com.phobos.emulator.ui.carts.CartOptionsDialog(
            system = systemName,
            romName = rom.name,
            romTitle = rom.title,
            looksJson = settings.cartsLooks,
            onSave = viewModel::setCartsLooks,
            onDismiss = { cartOptionsFor = null },
        )
    }
    choosingDisc?.let { rom ->
        DiscChoiceDialog(
            title = romTitle(rom.name),
            discs = rom.discs,
            selected = viewModel.lastDisc(systemName, rom),
            confirmLabel = "Start",
            fullScreen = settings.fullScreenMode,
            inGame = false,
            onDismiss = { choosingDisc = null },
            onChoose = { disc ->
                choosingDisc = null
                viewModel.loadRom(context, systemName, rom, disc)
                onRomClick(Uri.encode(systemName), Uri.encode(rom.name))
            },
        )
    }
}

/** The page header: back, the system's eyebrow and name, the index, and the four views. */
@Composable
private fun SystemHeader(
    system: String,
    facts: SystemFacts,
    view: CartridgeView,
    position: Int,
    count: Int,
    alphabetical: Boolean,
    onBack: () -> Unit,
    onView: (CartridgeView) -> Unit,
    onSort: () -> Unit,
    onFolder: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Row(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .size(38.dp)
                .clip(CircleShape)
                .background(scheme.onBackground.copy(alpha = 0.07f))
                .clickable(onClick = onBack),
            contentAlignment = Alignment.Center,
        ) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back", tint = scheme.onBackground) }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(
                facts.eyebrow.uppercase(),
                style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 1.8.sp),
                color = scheme.onBackground.copy(alpha = 0.58f),
            )
            Text(
                system,
                style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                color = scheme.onBackground,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        IconButtonBox(Icons.Rounded.Folder, "ROM folder", onFolder)
        Spacer(Modifier.width(8.dp))
        Box(
            Modifier
                .clip(RoundedCornerShape(percent = 50))
                .background(facts.color.copy(alpha = 0.25f))
                .border(1.dp, facts.color.copy(alpha = 0.5f), RoundedCornerShape(percent = 50))
                .clickable(onClick = onSort)
                .padding(horizontal = 11.dp, vertical = 6.dp),
        ) {
            Text(
                if (alphabetical) "A–Z  $position/$count" else "RECENT  $position/$count",
                style = MaterialTheme.typography.labelSmall,
                color = scheme.onBackground,
            )
        }
        Spacer(Modifier.width(12.dp))
        CartridgeSegmented(
            options = CartridgeView.entries,
            selected = view,
            label = { it.label },
            onSelect = onView,
        )
    }
}

@Composable
private fun IconButtonBox(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, onClick: () -> Unit) {
    Box(
        Modifier
            .size(32.dp)
            .clip(CircleShape)
            .background(MaterialTheme.colorScheme.onBackground.copy(alpha = 0.06f))
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, contentDescription = label, tint = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.7f), modifier = Modifier.size(16.dp)) }
}

/** A segmented control in the room's pill language, for the view switcher and settings tabs. */
@Composable
fun <T> CartridgeSegmented(
    options: List<T>,
    selected: T,
    label: (T) -> String,
    onSelect: (T) -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    Row(
        modifier
            .clip(RoundedCornerShape(percent = 50))
            .background(scheme.onBackground.copy(alpha = 0.06f))
            .border(1.dp, scheme.onBackground.copy(alpha = 0.08f), RoundedCornerShape(percent = 50))
            .padding(3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        options.forEach { option ->
            val on = option == selected
            Box(
                Modifier
                    .clip(RoundedCornerShape(percent = 50))
                    .background(if (on) scheme.onBackground.copy(alpha = 0.16f) else Color.Transparent)
                    .clickable { onSelect(option) }
                    .padding(horizontal = 12.dp, vertical = 6.dp),
            ) {
                Text(
                    label(option),
                    style = MaterialTheme.typography.labelSmall,
                    color = scheme.onBackground.copy(alpha = if (on) 0.95f else 0.55f),
                )
            }
        }
    }
}

/** The setup state: no folder yet, or no compatible games. */
@Composable
private fun EmptySystem(
    system: String,
    hasFolders: Boolean,
    folders: List<String>,
    onAdd: () -> Unit,
    onRemove: (Uri) -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    Box(modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .width(440.dp)
                .clip(MaterialTheme.shapes.large)
                .background(scheme.onBackground.copy(alpha = 0.05f))
                .border(1.dp, scheme.onBackground.copy(alpha = 0.1f), MaterialTheme.shapes.large)
                .padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                if (hasFolders) "No compatible games found" else "Add your $system folder",
                style = MaterialTheme.typography.titleLarge,
                color = scheme.onBackground,
            )
            Text(
                if (hasFolders) "The folder is set, but Phobos found no game files it can run there."
                else "Pick the folder that holds this system's ROMs. Phobos keeps the grant, so it can read them again next time.",
                style = MaterialTheme.typography.bodyMedium,
                color = scheme.onBackground.copy(alpha = 0.6f),
                modifier = Modifier.padding(top = 8.dp),
            )
            folders.forEach { path ->
                Text(path, style = MaterialTheme.typography.labelSmall, color = scheme.onBackground.copy(alpha = 0.45f), modifier = Modifier.padding(top = 6.dp))
            }
            Box(
                Modifier
                    .padding(top = 18.dp)
                    .clip(RoundedCornerShape(percent = 50))
                    .background(scheme.primary.copy(alpha = 0.9f))
                    .clickable(onClick = onAdd)
                    .padding(horizontal = 22.dp, vertical = 12.dp),
            ) { Text(if (hasFolders) "Choose another folder" else "Choose folder", color = scheme.onPrimary, style = MaterialTheme.typography.labelLarge) }
            if (hasFolders) {
                Text(
                    "Remove the folder",
                    style = MaterialTheme.typography.labelSmall,
                    color = scheme.onBackground.copy(alpha = 0.45f),
                    modifier = Modifier
                        .padding(top = 12.dp)
                        .clickable {
                            folders.firstOrNull()?.let { onRemove(Uri.parse(it)) }
                        },
                )
            }
        }
    }
}

/** The game's title and marks, under the carts. */
@Composable
private fun GameInfo(system: String, facts: SystemFacts, rom: RomFile?, record: GameRecord?, multiDisc: Int) {
    if (rom == null) return
    val scheme = MaterialTheme.colorScheme
    val title = cartTitle(rom.name, rom.title)
    val region = remember(rom.name, rom.title) { regionOf(rom.name) }
    val played = record?.lastPlayed ?: 0L
    Column(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 24.dp, vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            title,
            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
            color = scheme.onBackground,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            buildString {
                append(facts.short)
                if (region.isNotEmpty()) append(" · ").append(region)
                if (multiDisc > 1) append(" · ").append(multiDisc).append(" discs")
                append(" · ")
                append(if (played > 0) "Played ${relativeTime(played)}" else "Not played yet")
            },
            style = MaterialTheme.typography.labelMedium,
            color = scheme.onBackground.copy(alpha = 0.62f),
            modifier = Modifier.padding(top = 4.dp),
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.padding(top = 6.dp)) {
            if ((record?.plays ?: 0) > 0) {
                Box(
                    Modifier
                        .clip(RoundedCornerShape(percent = 50))
                        .background(scheme.primary.copy(alpha = 0.14f))
                        .border(1.dp, scheme.primary.copy(alpha = 0.3f), RoundedCornerShape(percent = 50))
                        .padding(horizontal = 10.dp, vertical = 4.dp),
                ) {
                    Text("Resumes where you left off", style = MaterialTheme.typography.labelSmall, color = scheme.primary)
                }
            }
            if (record?.favourite == true) {
                Icon(Icons.Rounded.Star, contentDescription = "Favorite", tint = scheme.primary, modifier = Modifier.size(16.dp))
            }
        }
    }
}

private fun regionOf(name: String): String {
    val match = Regex("\\((USA|Europe|Japan|World|Asia|Korea|Brazil|Australia|Germany|France|Spain|Italy|China|Taiwan)[^)]*\\)", RegexOption.IGNORE_CASE)
        .find(name) ?: return ""
    return match.groupValues[1].replaceFirstChar { it.uppercase() }
}

private fun relativeTime(millis: Long): String {
    val delta = (System.currentTimeMillis() - millis).coerceAtLeast(0)
    val minutes = delta / 60_000
    val hours = minutes / 60
    val days = hours / 24
    return when {
        minutes < 1 -> "just now"
        minutes < 60 -> "$minutes min ago"
        hours < 24 -> "$hours h ago"
        days < 7 -> "$days d ago"
        else -> java.text.DateFormat.getDateInstance(java.text.DateFormat.MEDIUM).format(java.util.Date(millis))
    }
}
