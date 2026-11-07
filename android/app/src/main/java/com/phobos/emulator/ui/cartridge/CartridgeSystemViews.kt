package com.phobos.emulator.ui.cartridge

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyGridState
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.itemsIndexed
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.phobos.emulator.data.EmulatorSettings
import com.phobos.emulator.data.GameKey
import com.phobos.emulator.data.GameRecord
import com.phobos.emulator.ui.RomFile
import com.phobos.emulator.ui.carts.CartBitmapCache
import com.phobos.emulator.ui.carts.CartThumbnailer
import com.phobos.emulator.ui.carts.LiveCartView
import com.phobos.emulator.ui.carts.LookResolver
import com.phobos.emulator.ui.carts.rememberCartCardFile
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.util.romTitle
import java.io.File

/** One game in a system page: its 3D cart (or box art), with a system badge, title and, when known, its time. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun CartridgeMediaTile(
    system: String,
    title: String,
    picture: File?,
    box: File?,
    modifier: Modifier = Modifier,
    width: Dp? = null,
    selected: Boolean = false,
    favourite: Boolean = false,
    showTitle: Boolean = true,
    bare: Boolean = false,
    caption: String? = null,
    onClick: () -> Unit,
    onLongClick: () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val facts = factsFor(system)
    Column(
        modifier = modifier
            .then(if (width != null) Modifier.width(width) else Modifier.fillMaxWidth())
            .clip(MaterialTheme.shapes.medium)
            .combinedClickable(onClick = onClick, onLongClick = onLongClick)
            .padding(2.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(MaterialTheme.shapes.medium)
                .background(if (bare) Color.Transparent else scheme.onBackground.copy(alpha = if (selected) 0.10f else 0.045f))
                .border(
                    1.dp,
                    if (bare) Color.Transparent else if (selected) facts.color.copy(alpha = 0.8f) else scheme.onBackground.copy(alpha = 0.06f),
                    MaterialTheme.shapes.medium,
                ),
            contentAlignment = Alignment.Center,
        ) {
            val image = picture ?: box
            if (image != null) {
                AsyncImage(
                    model = image,
                    contentDescription = title,
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize().padding(6.dp),
                )
            } else {
                Text(facts.short, style = MaterialTheme.typography.titleMedium, color = scheme.onBackground.copy(alpha = 0.4f))
            }
            if (favourite) {
                Icon(
                    Icons.Rounded.Star,
                    contentDescription = "Favorite",
                    tint = scheme.primary,
                    modifier = Modifier.align(Alignment.TopEnd).padding(6.dp).size(14.dp),
                )
            }
        }
        if (showTitle) Text(
            title,
            style = MaterialTheme.typography.labelMedium,
            color = scheme.onBackground.copy(alpha = if (selected) 0.95f else 0.78f),
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth().padding(top = 5.dp),
        )
        if (showTitle && caption != null) {
            Text(
                caption,
                style = MaterialTheme.typography.labelSmall,
                color = scheme.onBackground.copy(alpha = 0.45f),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.fillMaxWidth(),
                textAlign = TextAlign.Center,
            )
        }
    }
}

/** Cartridge's carousel: the focused cart large and centred, the rest of the row scrolling beside it. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun CarouselView(
    system: String,
    roms: List<RomFile>,
    focused: Int,
    labels: Map<GameKey, File>,
    boxes: Map<GameKey, File>,
    looks: Map<GameKey, LookResolver.Look>,
    liveCart: Boolean,
    looksJson: String,
    cartsEnabled: Boolean,
    specialEditions: Boolean,
    wearOn: Boolean,
    engraving: String,
    themeKey: String,
    playtimeSeconds: (RomFile) -> Long,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    onFocus: (Int) -> Unit,
    onStart: (RomFile) -> Unit,
    onMenu: (RomFile) -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    Column(Modifier.fillMaxSize()) {
        BoxWithConstraints(
            Modifier
                .fillMaxWidth()
                .weight(1f),
            contentAlignment = Alignment.Center,
        ) {
            val rowWidth = maxWidth
            val thumb = 210.dp
            val state = rememberLazyListState()
            LaunchedEffect(focused) { runCatching { state.animateScrollToItem(focused) } }
            LazyRow(
                state = state,
                contentPadding = PaddingValues(horizontal = ((rowWidth - thumb * 1.06f) / 2).coerceAtLeast(0.dp)),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.fillMaxWidth(),
            ) {
                itemsIndexed(roms, key = { _, rom -> rom.name }) { index, rom ->
                    val key = GameKey(system, rom.name)
                    val picture = rememberCartCardFile(
                        system = system,
                        name = rom.name,
                        title = cartTitle(rom.name, rom.title),
                        labelFile = labels[key],
                        box = boxes[key],
                        looksJson = looksJson,
                        specialEditions = specialEditions,
                        wearOn = wearOn,
                        playTimeMs = playtimeSeconds(rom) * 1000,
                        themeKey = themeKey,
                        faceKey = themeKey,
                        engraving = engraving,
                        thumbnailer = thumbnailer,
                        cache = cache,
                        enabled = cartsEnabled,
                        size = 384,
                    )
                    if (index == focused && liveCart) {
                        val facts = factsFor(system)
                        Box(
                            Modifier
                                .width(thumb)
                                .height(thumb)
                                .graphicsLayer { scaleX = 1.06f; scaleY = 1.06f }
                                .clip(MaterialTheme.shapes.medium)
                                .border(1.dp, facts.color.copy(alpha = 0.8f), MaterialTheme.shapes.medium)
                                .combinedClickable(
                                    onClick = { onStart(rom) },
                                    onLongClick = { onMenu(rom) },
                                ),
                        ) {
                            LiveCartView(
                                system = system,
                                rom = rom,
                                labels = labels,
                                boxes = boxes,
                                themeKey = themeKey,
                                looks = looks,
                                specialEditions = specialEditions,
                                wearOn = wearOn,
                                engraving = engraving,
                                playtimeSeconds = { playtimeSeconds(rom) },
                                title = cartTitle(rom.name, rom.title),
                                zoom = 0.9f,
                                studio = false,
                                autoSpin = false,
                                modifier = Modifier.matchParentSize(),
                            )
                            // Keep the plate tappable over the surface.
                            Box(Modifier.matchParentSize().combinedClickable(
                                onClick = { onStart(rom) },
                                onLongClick = { onMenu(rom) },
                            ))
                        }
                    } else {
                        CartridgeMediaTile(
                            system = system,
                            title = cartTitle(rom.name, rom.title),
                            picture = picture,
                            box = boxes[key],
                            width = thumb,
                            selected = index == focused,
                            showTitle = false,
                            bare = index != focused,
                            onClick = { if (index == focused) onStart(rom) else onFocus(index) },
                            onLongClick = { onMenu(rom) },
                        )
                    }
                }
            }
        }
        Box(Modifier.height(4.dp).fillMaxWidth().background(scheme.onBackground.copy(alpha = 0.04f)))
    }
}

/** The whole system as a grid of carts. */
@Composable
fun CartridgeGrid(
    system: String,
    roms: List<RomFile>,
    focused: Int,
    labels: Map<GameKey, File>,
    boxes: Map<GameKey, File>,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    settings: EmulatorSettings,
    themeKey: String,
    recorded: Map<String, GameRecord>,
    onFocus: (Int) -> Unit,
    onStart: (RomFile) -> Unit,
    onMenu: (RomFile) -> Unit,
) {
    val gridState = rememberLazyGridState()
    LaunchedEffect(focused) { runCatching { gridState.animateScrollToItem(focused) } }
    LazyVerticalGrid(
        state = gridState,
        columns = GridCells.Adaptive(132.dp),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize(),
    ) {
        itemsIndexed(roms, key = { _, rom -> rom.name }) { index, rom ->
            val key = GameKey(system, rom.name)
            val record = recorded[rom.name]
            val picture = rememberCartCardFile(
                system = system,
                name = rom.name,
                title = cartTitle(rom.name, rom.title),
                labelFile = if (settings.realLabels) labels[key] else null,
                box = boxes[key],
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
            )
            CartridgeMediaTile(
                system = system,
                title = cartTitle(rom.name, rom.title),
                picture = picture,
                box = boxes[key],
                selected = index == focused,
                favourite = record?.favourite == true,
                caption = record?.let { if (it.plays > 0) if (it.plays == 1) "1 play" else "${it.plays} plays" else null },
                onClick = { if (index == focused) onStart(rom) else onFocus(index) },
                onLongClick = { onMenu(rom) },
            )
        }
    }
}

/** Carts standing on shelves, four or five to a row. */
@Composable
fun CartridgeShelf(
    system: String,
    roms: List<RomFile>,
    focused: Int,
    labels: Map<GameKey, File>,
    boxes: Map<GameKey, File>,
    thumbnailer: CartThumbnailer?,
    cache: CartBitmapCache?,
    settings: EmulatorSettings,
    themeKey: String,
    recorded: Map<String, GameRecord>,
    onFocus: (Int) -> Unit,
    onStart: (RomFile) -> Unit,
    onMenu: (RomFile) -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val shelfState = rememberLazyListState()
    LaunchedEffect(focused) { runCatching { shelfState.animateScrollToItem(focused / 5) } }
    LazyColumn(
        state = shelfState,
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(18.dp),
        modifier = Modifier.fillMaxSize(),
    ) {
        itemsIndexed(roms.chunked(5), key = { _, row -> row.first().name }) { rowIndex, row ->
            Column {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    row.forEachIndexed { column, rom ->
                        val index = rowIndex * 5 + column
                        val key = GameKey(system, rom.name)
                        val record = recorded[rom.name]
                        val picture = rememberCartCardFile(
                            system = system,
                            name = rom.name,
                            title = cartTitle(rom.name, rom.title),
                            labelFile = if (settings.realLabels) labels[key] else null,
                            box = boxes[key],
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
                        )
                        CartridgeMediaTile(
                            system = system,
                            title = cartTitle(rom.name, rom.title),
                            picture = picture,
                            box = boxes[key],
                            modifier = Modifier.weight(1f),
                            selected = index == focused,
                            favourite = record?.favourite == true,
                            onClick = { if (index == focused) onStart(rom) else onFocus(index) },
                            onLongClick = { onMenu(rom) },
                        )
                    }
                    repeat(5 - row.size) { Spacer(Modifier.weight(1f)) }
                }
                Box(
                    Modifier
                        .fillMaxWidth()
                        .padding(top = 4.dp)
                        .height(1.dp)
                        .background(scheme.onBackground.copy(alpha = 0.10f)),
                )
            }
        }
    }
}

/** The tower: a tall list of coloured game rows, with the focused game's cart beside it. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun CartridgeTower(
    system: String,
    roms: List<RomFile>,
    focused: Int,
    facts: SystemFacts,
    focusedRom: RomFile?,
    cartPicture: File?,
    box: File?,
    labels: Map<GameKey, File>,
    boxes: Map<GameKey, File>,
    looks: Map<GameKey, LookResolver.Look>,
    liveCart: Boolean,
    specialEditions: Boolean,
    wearOn: Boolean,
    engraving: String,
    themeKey: String,
    playtimeSeconds: (RomFile) -> Long,
    onFocus: (Int) -> Unit,
    onStart: (RomFile) -> Unit,
    onMenu: (RomFile) -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val towerState = rememberLazyListState()
    LaunchedEffect(focused) { runCatching { towerState.animateScrollToItem(focused) } }
    Row(Modifier.fillMaxSize().padding(horizontal = 20.dp)) {
        LazyColumn(
            state = towerState,
            modifier = Modifier.weight(0.58f).fillMaxHeight(),
            contentPadding = PaddingValues(vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(7.dp),
        ) {
            itemsIndexed(roms, key = { _, rom -> rom.name }) { index, rom ->
                val selected = index == focused
                val tint = towerColor(index, facts)
                val ink = if (tint.luminance() > 0.35f) Color.Black else Color.White
                Row(
                    Modifier
                        .fillMaxWidth()
                        .height(38.dp)
                        .clip(RoundedCornerShape(9.dp))
                        .background(if (selected) tint.copy(alpha = 0.55f) else tint.copy(alpha = 0.30f))
                        .border(
                            1.dp,
                            if (selected) scheme.onBackground.copy(alpha = 0.25f) else Color.Transparent,
                            RoundedCornerShape(9.dp),
                        )
                        .combinedClickable(
                            onClick = { if (selected) onStart(rom) else onFocus(index) },
                            onLongClick = { onMenu(rom) },
                        )
                        .padding(horizontal = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        facts.short,
                        style = MaterialTheme.typography.labelSmall,
                        color = ink.copy(alpha = 0.85f),
                        modifier = Modifier.width(38.dp),
                    )
                    Text(
                        cartTitle(rom.name, rom.title),
                        style = MaterialTheme.typography.labelLarge,
                        color = ink.copy(alpha = 0.95f),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f),
                    )
                    if (selected) {
                        Text(
                            "▶",
                            style = MaterialTheme.typography.labelSmall,
                            color = ink.copy(alpha = 0.8f),
                        )
                    }
                }
            }
        }
        Spacer(Modifier.width(18.dp))
        Column(
            Modifier.weight(0.42f).fillMaxHeight(),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            val rom = focusedRom
            if (rom != null && liveCart) {
                Box(
                    Modifier
                        .fillMaxHeight(0.82f)
                        .aspectRatio(1f)
                        .clip(MaterialTheme.shapes.medium)
                        .border(1.dp, scheme.onBackground.copy(alpha = 0.16f), MaterialTheme.shapes.medium),
                ) {
                    LiveCartView(
                        system = system,
                        rom = rom,
                        labels = labels,
                        boxes = boxes,
                        themeKey = themeKey,
                        looks = looks,
                        specialEditions = specialEditions,
                        wearOn = wearOn,
                        engraving = engraving,
                        playtimeSeconds = { playtimeSeconds(rom) },
                        title = cartTitle(rom.name, rom.title),
                        zoom = 0.9f,
                        studio = false,
                        autoSpin = false,
                        modifier = Modifier.matchParentSize(),
                    )
                    Box(Modifier.matchParentSize().combinedClickable(
                        onClick = { onStart(rom) },
                        onLongClick = { onMenu(rom) },
                    ))
                }
            } else {
                CartridgeMediaTile(
                    system = system,
                    title = cartTitle(focusedRom?.name ?: "", focusedRom?.title),
                    picture = cartPicture,
                    box = box,
                    width = 220.dp,
                    selected = true,
                    onClick = { focusedRom?.let(onStart) },
                    onLongClick = { focusedRom?.let(onMenu) },
                )
            }
        }
    }
}

private fun towerColor(index: Int, facts: SystemFacts): Color {
    val palette = listOf(
        facts.color,
        Color(0xFFC94F6D),
        Color(0xFF2F9E6B),
        Color(0xFFE8A33D),
        Color(0xFF6A5ACD),
        Color(0xFF4A6FA5),
        Color(0xFF9E5A3A),
        Color(0xFF3A8E9E),
    )
    return palette[index % palette.size]
}
