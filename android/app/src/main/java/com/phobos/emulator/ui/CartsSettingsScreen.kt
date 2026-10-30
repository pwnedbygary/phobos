package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.art.BundledLabelIndex
import com.phobos.emulator.art.LabelMatch

/**
 * Settings → Carts: whether a system's games show as cartridges or discs, whether real label scans are downloaded,
 * the shell options, and what the label server receives. Every label the painter draws with is a bundled font;
 * a missing bundle is a build error, not a silent system font.
 */
@Composable
fun CartsSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val labelState by BundledLabelIndex.state.collectAsState()
    val context = androidx.compose.ui.platform.LocalContext.current
    androidx.compose.runtime.LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { BundledLabelIndex.load(context) }
    }

    PhobosScaffold(title = "Carts", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = pageContentPadding(),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                SettingsCategory("Carts") {
                    SettingsSwitchItem(
                        title = "Carts",
                        description = "A system's games as their own cartridges (or discs), the focused one turning " +
                            "slowly, and the Library's Continue Playing and Favorites cards as their carts. Off " +
                            "keeps today's list and box art.",
                        checked = settings.carts,
                        onCheckedChange = { viewModel.setCarts(it) }
                    )
                    SettingsSwitchItem(
                        title = "Real Label Scans",
                        description = "Each cart's printed label from the LaunchBox Games Database, cropped to the " +
                            "shell; box art and a generated label cover the rest. Downloads follow the artwork " +
                            "queue and never start while a game is loading or running.",
                        checked = settings.realLabels,
                        onCheckedChange = { viewModel.setRealLabels(it) }
                    )
                }
            }
            item {
                SettingsCategory("Shells") {
                    SettingsSwitchItem(
                        title = "Special Editions",
                        description = "Cartridge's special shells for known games (gold Zelda, the Pokemon colours) " +
                            "unless switched off.",
                        checked = settings.cartsSpecialEditions,
                        onCheckedChange = { viewModel.setCartsSpecialEditions(it) }
                    )
                    SettingsSwitchItem(
                        title = "Wear",
                        description = "Scuffs, yellowing and worn labels that build with the game's play time; off " +
                            "shows every shell new.",
                        checked = settings.cartsWear,
                        onCheckedChange = { viewModel.setCartsWear(it) }
                    )
                    SettingsSwitchItem(
                        title = "GBA Engraving",
                        description = "Cycles None, Badge, Lines on Game Boy Advance carts: currently " +
                            "${settings.cartsEngraving.replaceFirstChar { it.uppercase() }}. The Cartridge wordmark is never drawn.",
                        checked = settings.cartsEngraving != "none",
                        onCheckedChange = {
                            viewModel.setCartsEngraving(
                                when (settings.cartsEngraving) {
                                    "none" -> "badge"
                                    "badge" -> "lines"
                                    else -> "none"
                                }
                            )
                        }
                    )
                }
            }
            item {
                SettingsCategory("Labels") {
                    val state = labelState
                val text = when (state) {
                    is BundledLabelIndex.State.Ready ->
                        "The bundled label index is ready: ${state.entries} games across the systems it covers."
                    is BundledLabelIndex.State.Unavailable ->
                        "Labels are on, but the bundled index could not be read: ${state.reason}. Carts fall back " +
                            "to box art and generated labels."
                    else -> "The bundled label index loads when it is first needed."
                }
                Text(
                    text,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(16.dp),
                )
                }
            }
            item {
                SettingsCategory("What's Sent") {
                    Text(
                        "When real labels are on, Phobos downloads a game's label scans from " +
                            "${LabelMatch.LABEL_BASE.removePrefix("https://")}. Matching happens on the device: the " +
                            "request carries only the scan's file name from the bundled index (a UUID), not the game's " +
                            "name, plus your device's IP address and \"Phobos\" with its version. Box art is the " +
                            "path that puts names in a URL, as the Artwork page describes. The label index ships " +
                            "with Phobos; it is the LaunchBox Games Database's data, whose right to redistribute is " +
                            "not established - an accepted risk of shipping it, not a licence. Neither the LaunchBox " +
                            "Games Database nor libretro-thumbnails publishes terms for its pictures; the pictures are " +
                            "each game's developers' and publishers' work.",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(16.dp),
                    )
                }
            }
        }
    }
}
