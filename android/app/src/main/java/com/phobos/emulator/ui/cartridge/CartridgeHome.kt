package com.phobos.emulator.ui.cartridge

import android.graphics.Bitmap
import android.net.Uri
import androidx.compose.animation.Crossfade
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import coil.compose.AsyncImage
import com.phobos.emulator.R
import com.phobos.emulator.art.hasArt
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.ui.DialogSystemBars
import com.phobos.emulator.ui.DiscChoiceDialog
import com.phobos.emulator.ui.GameMenu
import com.phobos.emulator.ui.HoldScreensaver
import com.phobos.emulator.ui.GameMenuItem
import com.phobos.emulator.ui.PhobosAlertDialog
import com.phobos.emulator.ui.RecordStart
import com.phobos.emulator.ui.RunningGame
import com.phobos.emulator.ui.gameMenuItems
import com.phobos.emulator.ui.carts.rememberCartCardFile
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.carts.CartBitmapCache
import com.phobos.emulator.ui.carts.CartLabelSource
import com.phobos.emulator.ui.carts.CartShapes
import com.phobos.emulator.ui.carts.CartThumbnailer
import com.phobos.emulator.ui.carts.CartsEngine
import com.phobos.emulator.util.gameTitle
import com.phobos.emulator.util.romTitle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/**
 * The Library in Cartridge's language: blurred art behind, Continue Playing at the top, a carousel of the
 * systems' own media, the selected system's name and marks, then the action pills and controller hints. The
 * existing styled Library remains for the other Appearance effects.
 */
@Composable
fun CartridgeLibraryScreen(
    viewModel: com.phobos.emulator.ui.MainViewModel,
    onSystemClick: (String) -> Unit,
) {
    val context = LocalContext.current
    val settings by viewModel.settings.collectAsState()
    val systems by viewModel.visibleSystems.collectAsState()
    val rows by viewModel.libraryRows.collectAsState()
    val running by viewModel.runningGame.collectAsState()
    val records by viewModel.gameRecords.collectAsState()
    val boxes by viewModel.artBoxes.collectAsState()
    val labels by viewModel.artLabels.collectAsState()
    val gameLoaded by viewModel.gameLoaded.collectAsState()
    val theme = LocalPhobosTheme.current
    val themeKey = theme.theme.name
    val cartsOn = settings.carts && !gameLoaded
    val engine = remember(cartsOn) { if (cartsOn) CartsEngine.get(context) else null }
    val thumbnailer = remember(engine) { engine?.let { CartThumbnailer(context, it) } }
    val bitmapCache = remember { CartBitmapCache(File(context.cacheDir, "cart-render"), 64L * 1024 * 1024) }
    DisposableEffect(thumbnailer) { onDispose { thumbnailer?.release() } }

    val recents = rows?.continuePlaying.orEmpty()
    val allRecords = records.orEmpty()
    val initialSystem = remember(systems, recents) {
        val recent = recents.firstOrNull()?.key?.system
        val recentIndex = if (recent != null) systems.indexOf(recent) else -1
        if (recentIndex >= 0) {
            recentIndex
        } else {
            // A fresh library opens on a familiar console rather than the alphabetical first entry (Arcade).
            val preferred = listOf("Super Famicom", "Famicom", "Mega Drive", "PlayStation", "Nintendo 64", "Game Boy Advance")
            preferred.firstNotNullOfOrNull { name -> systems.indexOf(name).takeIf { it >= 0 } } ?: 0
        }
    }
    var systemIndex by rememberSaveable(systems) { mutableIntStateOf(initialSystem.coerceIn(0, (systems.size - 1).coerceAtLeast(0))) }
    var row by rememberSaveable { mutableIntStateOf(1) }
    var recentIndex by rememberSaveable { mutableIntStateOf(0) }
    var recentsExpanded by rememberSaveable { mutableStateOf(false) }
    val recentsState = rememberLazyListState()
    var actionIndex by rememberSaveable { mutableIntStateOf(0) }
    var overlay by remember { mutableStateOf<Overlay?>(null) }
    var menu by remember { mutableStateOf<MenuState?>(null) }
    var cartOptionsFor by remember { mutableStateOf<GameRecord?>(null) }
    var choosingDisc by remember { mutableStateOf<RecordStart.Ready?>(null) }
    var notFound by remember { mutableStateOf<GameRecord?>(null) }
    var starting by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    LaunchedEffect(systems.size) {
        if (systemIndex > systems.lastIndex) systemIndex = systems.lastIndex.coerceAtLeast(0)
    }
    LaunchedEffect(recents.size) {
        if (recentIndex > recents.lastIndex) recentIndex = recents.lastIndex.coerceAtLeast(0)
    }
    LaunchedEffect(recentIndex, recentsExpanded) {
        if (recentsExpanded) runCatching { recentsState.animateScrollToItem(recentIndex) }
    }
    LaunchedEffect(recents.isEmpty(), recentsExpanded) {
        if (recents.isEmpty() && recentsExpanded) {
            recentsExpanded = false
            if (row == 0) row = 1
        }
    }

    fun start(record: GameRecord) {
        if (starting) return
        starting = true
        scope.launch {
            try {
                when (val next = viewModel.recordStart(record)) {
                    RecordStart.Running -> viewModel.swapBackToGame()
                    RecordStart.SystemPage -> onSystemClick(Uri.encode(record.key.system))
                    RecordStart.NotFound -> notFound = record
                    is RecordStart.Ready -> {
                        if (next.rom.discs.size > 1) choosingDisc = next
                        else viewModel.startFromRow(context, next.system, next.rom)
                    }
                }
            } finally {
                starting = false
            }
        }
    }

    val selectedSystem = systems.getOrNull(systemIndex) ?: systems.firstOrNull()
    val facts = selectedSystem?.let(::factsFor)
    val selectedArt = remember(boxes, selectedSystem, recents) {
        val key = recents.firstOrNull { it.key.system == selectedSystem }?.key
            ?: allRecords.firstOrNull { it.key.system == selectedSystem && boxes[it.key] != null }?.key
        key?.let(boxes::get)
    }
    val artworkCount = remember(boxes, labels) { boxes.size + labels.size }
    val actions = remember(running) {
        buildList {
            if (running != null) add(Action.Resume)
            add(Action.Favorites)
            add(Action.Switcher)
            add(Action.Search)
            add(Action.Settings)
        }
    }
    LaunchedEffect(actions.size) { if (actionIndex > actions.lastIndex) actionIndex = actions.lastIndex.coerceAtLeast(0) }

    fun act(action: Action) {
        when (action) {
            Action.Resume -> viewModel.swapBackToGame()
            Action.Favorites -> overlay = Overlay.Favorites
            Action.Switcher -> overlay = Overlay.Switcher
            Action.Search -> overlay = Overlay.Search
            Action.Settings -> viewModel.navigateTo("settings")
        }
    }

    val rootFocus = remember { FocusRequester() }
    LaunchedEffect(Unit) { runCatching { rootFocus.requestFocus() } }

    // Cartridge's pad model: two moving rows and a row of actions; A opens, B returns, Start is Settings,
    // Select is the switcher. Touch still does everything on its own.
    Box(
        Modifier
            .fillMaxSize()
            .focusRequester(rootFocus)
            .focusable()
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val key = event.nativeKeyEvent.keyCode
                val down = key == android.view.KeyEvent.KEYCODE_DPAD_DOWN
                val up = key == android.view.KeyEvent.KEYCODE_DPAD_UP
                val left = key == android.view.KeyEvent.KEYCODE_DPAD_LEFT
                val right = key == android.view.KeyEvent.KEYCODE_DPAD_RIGHT
                val accept = key == android.view.KeyEvent.KEYCODE_BUTTON_A ||
                    key == android.view.KeyEvent.KEYCODE_ENTER ||
                    key == android.view.KeyEvent.KEYCODE_NUMPAD_ENTER ||
                    key == android.view.KeyEvent.KEYCODE_DPAD_CENTER
                val back = key == android.view.KeyEvent.KEYCODE_BUTTON_B || key == android.view.KeyEvent.KEYCODE_ESCAPE
                val start = key == android.view.KeyEvent.KEYCODE_BUTTON_START || key == android.view.KeyEvent.KEYCODE_MENU
                val select = key == android.view.KeyEvent.KEYCODE_BUTTON_SELECT || key == android.view.KeyEvent.KEYCODE_TAB
                val search = key == android.view.KeyEvent.KEYCODE_SEARCH || key == android.view.KeyEvent.KEYCODE_SLASH
                if (overlay != null) {
                    if (back) overlay = null
                    return@onPreviewKeyEvent back
                }
                if (menu != null || cartOptionsFor != null || choosingDisc != null || notFound != null) return@onPreviewKeyEvent false
                when {
                    up && row == 1 && recents.isNotEmpty() && !recentsExpanded -> {
                        recentsExpanded = true
                        row = 0
                    }
                    up && row > (if (recents.isEmpty()) 1 else 0) -> row--
                    down && row == 0 && recentsExpanded -> {
                        recentsExpanded = false
                        row = 1
                    }
                    down && row < 2 -> row++
                    left && row == 0 && recents.isNotEmpty() -> recentIndex = (recentIndex - 1).coerceAtLeast(0)
                    right && row == 0 && recents.isNotEmpty() -> recentIndex = (recentIndex + 1).coerceAtMost(recents.lastIndex)
                    left && row == 1 && systems.isNotEmpty() -> systemIndex = (systemIndex - 1).coerceAtLeast(0)
                    right && row == 1 && systems.isNotEmpty() -> systemIndex = (systemIndex + 1).coerceAtMost(systems.lastIndex)
                    left && row == 2 && actions.isNotEmpty() -> actionIndex = (actionIndex - 1).coerceAtLeast(0)
                    right && row == 2 && actions.isNotEmpty() -> actionIndex = (actionIndex + 1).coerceAtMost(actions.lastIndex)
                    accept && row == 0 && recents.isNotEmpty() -> start(recents[recentIndex.coerceIn(recents.indices)])
                    accept && row == 1 && selectedSystem != null -> onSystemClick(Uri.encode(selectedSystem))
                    accept && row == 2 && actions.isNotEmpty() -> act(actions[actionIndex.coerceIn(actions.indices)])
                    back && row != 1 -> row = 1
                    start -> viewModel.navigateTo("settings")
                    select -> overlay = Overlay.Switcher
                    search -> overlay = Overlay.Search
                    else -> return@onPreviewKeyEvent false
                }
                true
            },
    ) {
        CartridgeRoom(selectedArt, facts?.color ?: MaterialTheme.colorScheme.primary)
        Column(Modifier.fillMaxSize()) {
            CartridgeTopBar(artwork = if (settings.artDownload && artworkCount > 0) artworkCount else null)
            Column(
                Modifier
                    .weight(1f)
                    .fillMaxWidth(),
            ) {
                if (recents.isNotEmpty()) {
                    CartridgeContinueHandle(expanded = recentsExpanded, count = recents.size) {
                        recentsExpanded = !recentsExpanded
                        row = if (recentsExpanded) 0 else 1
                    }
                    if (recentsExpanded) LazyRow(
                        state = recentsState,
                        contentPadding = PaddingValues(horizontal = 20.dp),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        modifier = Modifier.padding(bottom = 10.dp),
                    ) {
                        items(recents, key = { "${it.key.system}\n${it.key.name}" }) { record ->
                            val cart = if (settings.carts) {
                                rememberCartCardFile(
                                    system = record.key.system,
                                    name = record.key.name,
                                    title = cartTitle(record.key.name, record.title),
                                    labelFile = if (settings.realLabels) labels[record.key] else null,
                                    box = boxes[record.key],
                                    looksJson = settings.cartsLooks,
                                    specialEditions = settings.cartsSpecialEditions,
                                    wearOn = settings.cartsWear,
                                    playTimeMs = record.playTimeMs,
                                    themeKey = themeKey,
                                    faceKey = themeKey,
                                    engraving = settings.cartsEngraving,
                                    thumbnailer = thumbnailer,
                                    cache = bitmapCache,
                                    enabled = settings.carts,
                                )
                            } else null
                            val selected = row == 0 && recents.indexOf(record) == recentIndex
                            CartridgeGameTile(
                                record = record,
                                picture = cart,
                                box = boxes[record.key],
                                width = 84.dp,
                                modifier = Modifier
                                    .then(if (selected) Modifier.border(1.dp, MaterialTheme.colorScheme.onBackground.copy(alpha = 0.3f), MaterialTheme.shapes.medium) else Modifier),
                                onClick = { row = 0; recentIndex = recents.indexOf(record); start(record) },
                                onLongClick = { menu = MenuState(record) },
                            )
                        }
                    }
                }
                if (!recentsExpanded) {
                if (facts != null) {
                    SystemHeading(facts, systems.size, allRecords.count { it.key.system == selectedSystem })
                }
                BoxWithConstraints(
                    Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                ) {
                    val plate = (maxWidth / 9.8f).coerceIn(80.dp, 120.dp)
                    val stageWidth = maxWidth
                    if (systems.isEmpty()) {
                        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                "No systems found",
                                style = MaterialTheme.typography.titleMedium,
                                color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.6f),
                            )
                        }
                    } else {
                        val listState = rememberLazyListState()
                        LaunchedEffect(systemIndex, plate) {
                            runCatching { listState.animateScrollToItem(systemIndex) }
                        }
                        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Center) {
                            LazyRow(
                                state = listState,
                                contentPadding = PaddingValues(horizontal = ((stageWidth - plate * 1.24f) / 2).coerceAtLeast(0.dp)),
                                horizontalArrangement = Arrangement.spacedBy(6.dp),
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                items(systems.size) { index ->
                                    val system = systems[index]
                                    val picture = rememberSystemPlate(
                                        system = system,
                                        size = 256,
                                        themeKey = themeKey,
                                        thumbnailer = thumbnailer,
                                        cache = bitmapCache,
                                        enabled = settings.carts,
                                    )
                                    CartridgeSystemPlate(
                                        system = system,
                                        picture = picture,
                                        selected = index == systemIndex,
                                        size = plate,
                                        onClick = {
                                            row = 1
                                            if (index == systemIndex) onSystemClick(Uri.encode(system)) else systemIndex = index
                                        },
                                    )
                                }
                            }
                        }
                    }
                }
                }
            }
            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 20.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                actions.forEachIndexed { index, action ->
                    CartridgePill(
                        icon = action.icon,
                        label = action.label,
                        selected = row == 2 && actionIndex == index,
                        onClick = { row = 2; actionIndex = index; act(action) },
                        modifier = Modifier.padding(horizontal = 4.dp),
                    )
                }
            }
            CartridgeHints(
                items = buildList {
                    add("←→" to "Browse")
                    add("A" to if (row == 0) "Resume" else if (row == 1) "Open" else "Select")
                    add("B" to "Back")
                    add("Y" to "Switcher")
                    add("/" to "Search")
                    add("≡" to "Settings")
                },
            )
        }
    }

    overlay?.let { shown ->
        // A dialog, not a sibling Box: it owns the window's focus, so a covered page's tiles cannot be reached
        // with the D-pad and cannot start a game behind the overlay.
        Dialog(
            onDismissRequest = { overlay = null },
            properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false),
        ) {
            DialogSystemBars(settings.fullScreenMode, inGame = false)
            HoldScreensaver()
            CartridgeOverlay(
                overlay = shown,
                records = allRecords,
                recents = recents,
                running = running,
                boxes = boxes,
                labels = labels,
                settings = settings,
                themeKey = themeKey,
                thumbnailer = thumbnailer,
                cache = bitmapCache,
                onClose = { overlay = null },
                onStart = { start(it) },
                onMenu = { menu = MenuState(it) },
            )
        }
    }

    menu?.let { state ->
        val record = state.record
        GameMenu(
            title = cartTitle(record.key.name, record.title),
            system = record.key.system,
            items = gameMenuItems(
                record,
                artwork = settings.artDownload && hasArt(record.key.system),
                carts = settings.carts && CartShapes.cartShape(record.key.system) != null,
            ),
            openedByHeldA = false,
            fullScreen = settings.fullScreenMode,
            onPick = { item ->
                menu = null
                when (item) {
                    GameMenuItem.ADD_TO_FAVORITES -> viewModel.setFavourite(record.key, true)
                    GameMenuItem.REMOVE_FROM_FAVORITES -> viewModel.setFavourite(record.key, false)
                    GameMenuItem.HIDE_FROM_CONTINUE_PLAYING -> viewModel.hideFromContinuePlaying(record.key)
                    GameMenuItem.REDOWNLOAD_ARTWORK -> viewModel.redownloadArt(record.key, record.title)
                    GameMenuItem.CART_OPTIONS -> cartOptionsFor = record
                }
            },
            onDismiss = { menu = null },
        )
    }
    cartOptionsFor?.let { record ->
        com.phobos.emulator.ui.carts.CartOptionsDialog(
            system = record.key.system,
            romName = record.key.name,
            romTitle = record.title,
            looksJson = settings.cartsLooks,
            onSave = viewModel::setCartsLooks,
            onDismiss = { cartOptionsFor = null },
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
        PhobosAlertDialog(
            onDismissRequest = { notFound = null },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = false); Text("Game Not Found") },
            text = {
                Text(
                    "Phobos couldn't open ${cartTitle(record.key.name, record.title)} where it was last found. Its " +
                        "file may have moved or been deleted, or Phobos may no longer reach its folder.",
                )
            },
            confirmButton = {
                TextButton(onClick = { notFound = null; onSystemClick(Uri.encode(record.key.system)) }) {
                    Text("Open ${record.key.system}")
                }
            },
            dismissButton = { TextButton(onClick = { notFound = null }) { Text("Cancel") } },
        )
    }
}

private data class MenuState(val record: GameRecord)

private enum class Overlay { Favorites, Switcher, Search }

private enum class Action(val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    Resume("Resume", Icons.Rounded.PlayArrow),
    Favorites("Favorites", Icons.Rounded.Star),
    Switcher("Game Switcher", Icons.Rounded.SwapHoriz),
    Search("Search", Icons.Rounded.Search),
    Settings("Settings", Icons.Rounded.Settings),
}

/** The room: the selected system's latest box art, blurred, over a tint of the system's colour. */
@Composable
private fun CartridgeRoom(art: File?, color: Color) {
    val scheme = MaterialTheme.colorScheme
    Box(Modifier.fillMaxSize()) {
        Box(
            Modifier
                .fillMaxSize()
                .background(
                    Brush.radialGradient(
                        colors = listOf(color.copy(alpha = 0.34f), scheme.background),
                        radius = 1400f,
                    ),
                ),
        )
        Crossfade(art, label = "cartridgeRoomArt", modifier = Modifier.fillMaxSize()) { picture ->
            if (picture != null) {
                AsyncImage(
                    model = picture,
                    contentDescription = null,
                    contentScale = ContentScale.Crop,
                    modifier = Modifier
                        .fillMaxSize()
                        .blur(46.dp)
                        .graphicsLayer { alpha = 0.42f },
                )
            }
        }
        CartridgeScrim(Modifier.fillMaxSize())
    }
}

@Composable
private fun SystemHeading(facts: SystemFacts, systemCount: Int, playedCount: Int) {
    val scheme = MaterialTheme.colorScheme
    Column(
        Modifier
            .fillMaxWidth()
            .padding(top = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            facts.eyebrow.uppercase(),
            style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 2.sp),
            color = scheme.onBackground.copy(alpha = 0.58f),
        )
        Text(
            facts.system,
            style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
            color = scheme.onBackground,
        )
        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
            modifier = Modifier.padding(top = 6.dp),
        ) {
            Chip(if (facts.isDisc) "Disc" else if (facts.mediaShape == "tape") "Tape" else "Cartridge")
            Chip(if (facts.handheld) "Handheld" else "Console")
            if (playedCount > 0) Chip("$playedCount played")
        }
    }
}

@Composable
private fun Chip(text: String) {
    // A Box with an explicit minimum height: a Text whose own modifier draws the pill measured to zero height on
    // this Compose version, leaving only the border lines.
    Box(
        Modifier
            .clip(RoundedCornerShape(percent = 50))
            .background(MaterialTheme.colorScheme.onBackground.copy(alpha = 0.07f))
            .border(1.dp, MaterialTheme.colorScheme.onBackground.copy(alpha = 0.1f), RoundedCornerShape(percent = 50))
            .heightIn(min = 24.dp)
            .padding(horizontal = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onBackground.copy(alpha = 0.82f),
        )
    }
}

/** One system's media, rendered once and cached offscreen. */
@Composable
private fun rememberSystemPlate(
    system: String,
    size: Int,
    themeKey: String,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    enabled: Boolean,
): File? {
    val context = LocalContext.current
    val shape = CartShapes.cartShape(system) ?: return null
    if (!enabled || cache == null) return null
    val disc = CartShapes.isDisc(system)
    val key = remember(system, shape, disc, themeKey, size) {
        CartBitmapCache.Key(
            gameId = ("system:" + system).hashCode().toLong(),
            kind = (if (disc) "disc:" else "cart:") + shape,
            theme = themeKey,
            face = themeKey,
            finish = "",
            color = "",
            special = false,
            wear = 0,
            width = size,
            height = size,
            label = "system:$system:${factsFor(system).short}:$themeKey",
        )
    }
    var file by remember(key) { mutableStateOf<File?>(null) }
    LaunchedEffect(key) {
        file = withContext(Dispatchers.IO) {
            val thumb = thumbnailer ?: return@withContext cache.cached(key)
            runCatching {
                cache.get(key) { target ->
                    val label = CartLabelSource.label(context, system, factsFor(system).short, null, null, themeKey)
                    val bitmap = try {
                        thumb.render(shape, disc, size, size, null, 0, label)
                    } finally {
                        label?.recycle()
                    } ?: throw IllegalStateException("no system plate")
                    try {
                        FileOutputStream(target).use { out ->
                            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, out)) {
                                throw IllegalStateException("not written")
                            }
                        }
                    } finally {
                        bitmap.recycle()
                    }
                }
            }.getOrNull()
        }
    }
    return file
}

/** Favorites, the game switcher and search, in the same room. */
@Composable
private fun CartridgeOverlay(
    overlay: Overlay,
    records: List<GameRecord>,
    recents: List<GameRecord>,
    running: RunningGame?,
    boxes: Map<GameKey, File>,
    labels: Map<GameKey, File>,
    settings: com.phobos.emulator.data.EmulatorSettings,
    themeKey: String,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    onClose: () -> Unit,
    onStart: (GameRecord) -> Unit,
    onMenu: (GameRecord) -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    var query by remember { mutableStateOf("") }
    val searchFocus = remember { FocusRequester() }
    LaunchedEffect(overlay) {
        if (overlay == Overlay.Search) runCatching { searchFocus.requestFocus() }
    }
    val shown = remember(overlay, records, recents, query) {
        when (overlay) {
            Overlay.Favorites -> records.filter { it.favourite }
            Overlay.Switcher -> recents
            Overlay.Search -> if (query.isBlank()) emptyList() else records.filter {
                gameTitle(it.key.name, it.title).contains(query, ignoreCase = true) ||
                    it.key.system.contains(query, ignoreCase = true)
            }
        }
    }
    Box(
        Modifier
            .fillMaxSize()
            .background(scheme.background.copy(alpha = 0.96f))
            .onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                val key = event.nativeKeyEvent.keyCode
                if (key == android.view.KeyEvent.KEYCODE_BUTTON_B || key == android.view.KeyEvent.KEYCODE_ESCAPE) {
                    onClose()
                    true
                } else {
                    false
                }
            }
            .clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = {},
            ),
    ) {
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 18.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    Modifier
                        .size(38.dp)
                        .clip(CircleShape)
                        .background(scheme.onBackground.copy(alpha = 0.07f))
                        .clickable(onClick = onClose),
                    contentAlignment = Alignment.Center,
                ) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back", tint = scheme.onBackground) }
                Spacer(Modifier.width(14.dp))
                Text(
                    when (overlay) {
                        Overlay.Favorites -> "Favorites"
                        Overlay.Switcher -> "Game Switcher"
                        Overlay.Search -> "Search"
                    },
                    style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                    color = scheme.onBackground,
                )
                Spacer(Modifier.weight(1f))
                if (overlay == Overlay.Search) {
                    BasicTextField(
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                        textStyle = MaterialTheme.typography.bodyLarge.copy(color = scheme.onBackground),
                        modifier = Modifier
                            .width(280.dp)
                            .focusRequester(searchFocus)
                            .clip(RoundedCornerShape(percent = 50))
                            .background(scheme.onBackground.copy(alpha = 0.07f))
                            .border(1.dp, scheme.onBackground.copy(alpha = 0.12f), RoundedCornerShape(percent = 50))
                            .padding(horizontal = 16.dp, vertical = 10.dp),
                        decorationBox = { inner ->
                            Box {
                                if (query.isEmpty()) {
                                    Text("Search games", color = scheme.onBackground.copy(alpha = 0.45f))
                                }
                                inner()
                            }
                        },
                    )
                }
            }
            if (overlay == Overlay.Switcher && running != null) {
                Text(
                    "Running: ${romTitle(running.romName)} · ${running.systemName}",
                    style = MaterialTheme.typography.titleMedium,
                    color = scheme.primary,
                    modifier = Modifier.padding(horizontal = 22.dp, vertical = 4.dp),
                )
            }
            if (shown.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text(
                        if (overlay == Overlay.Search && query.isBlank()) "Type to search your played games"
                        else "Nothing here yet",
                        color = scheme.onBackground.copy(alpha = 0.55f),
                    )
                }
            } else {
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(132.dp),
                    contentPadding = PaddingValues(20.dp),
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    items(shown, key = { "${it.key.system}\n${it.key.name}" }) { record ->
                        val cart = if (settings.carts) {
                            rememberCartCardFile(
                                system = record.key.system,
                                name = record.key.name,
                                title = cartTitle(record.key.name, record.title),
                                labelFile = if (settings.realLabels) labels[record.key] else null,
                                box = boxes[record.key],
                                looksJson = settings.cartsLooks,
                                specialEditions = settings.cartsSpecialEditions,
                                wearOn = settings.cartsWear,
                                playTimeMs = record.playTimeMs,
                                themeKey = themeKey,
                                faceKey = themeKey,
                                engraving = settings.cartsEngraving,
                                thumbnailer = thumbnailer,
                                cache = cache,
                                enabled = settings.carts,
                            )
                        } else null
                        CartridgeGameTile(
                            record = record,
                            picture = cart,
                            box = boxes[record.key],
                            width = 132.dp,
                            onClick = { onStart(record) },
                            onLongClick = { onMenu(record) },
                        )
                    }
                }
            }
        }
    }
}
