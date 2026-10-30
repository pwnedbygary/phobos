package com.phobos.emulator.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CloudDownload
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.art.ART_SERVER

/**
 * Settings → Artwork: whether games' pictures are downloaded, which kinds, what's sent for them and to whom, and the
 * two ways to download them again.
 */
@Composable
fun ArtworkSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val stats by viewModel.artStats.collectAsState()
    var confirmRedownload by remember { mutableStateOf(false) }

    PhobosScaffold(title = "Artwork", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = pageContentPadding(),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                SettingsCategory("Downloads") {
                    SettingsSwitchItem(
                        title = "Download Artwork",
                        description = "Each game's box art, for the Library's rows and a system's list: after each " +
                            "folder scan, and for the rows' games when the Library opens, on any network. Nothing " +
                            "starts while a game is loading or running.",
                        checked = settings.artDownload,
                        onCheckedChange = { viewModel.setArtDownload(it) }
                    )
                    SettingsSwitchItem(
                        title = "Screenshots and Logos Too",
                        description = "Each game's screenshot and title logo as well, kept for later: nothing " +
                            "shows them yet.",
                        checked = settings.artExtras,
                        onCheckedChange = { viewModel.setArtExtras(it) }
                    )
                }
            }
            item {
                SettingsCategory("Downloaded") {
                    SettingsClickableItem(
                        title = "Download Missing Artwork",
                        description = "Look again now for the games with no picture yet. Phobos otherwise looks " +
                            "again at the next scan or Library visit once a week has passed since a game wasn't " +
                            "found, or an hour since a download failed.",
                        onClick = { viewModel.downloadMissingArt() },
                        icon = Icons.Rounded.CloudDownload,
                    )
                    SettingsClickableItem(
                        title = "Re-download All Artwork",
                        description = "${stats.pictures} pictures, ${megabytes(stats.bytes)} of " +
                            "${megabytes(viewModel.artCapBytes)}. Deletes them all, and the server's lists, then " +
                            "downloads them again.",
                        onClick = { confirmRedownload = true },
                        icon = Icons.Rounded.Refresh,
                    )
                }
            }
            item {
                SettingsCategory("What's Sent") {
                    Text(
                        "Pictures come from libretro's thumbnail server, ${ART_SERVER.removePrefix("https://")}, " +
                            "the libretro-thumbnails project's, behind Cloudflare. No account is needed. Phobos asks " +
                            "it for a system's list of pictures, then for the pictures of the games whose names it " +
                            "found there: the server receives those systems' and games' names, your device's IP " +
                            "address, and \"Phobos\" with its version. The names of games not on the list never " +
                            "leave your device. The libretro-thumbnails project publishes no license or terms for " +
                            "its pictures; the art itself is the work of each game's developers and publishers.",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(16.dp),
                    )
                }
            }
        }
    }

    if (confirmRedownload) {
        PhobosAlertDialog(
            onDismissRequest = { confirmRedownload = false },
            title = { DialogSystemBars(settings.fullScreenMode, inGame = false); Text("Re-download All Artwork") },
            text = { Text("Every downloaded picture is deleted, then downloaded again from the server.") },
            confirmButton = {
                TextButton(onClick = { confirmRedownload = false; viewModel.redownloadAllArt() }) {
                    Text("Re-download")
                }
            },
            dismissButton = { TextButton(onClick = { confirmRedownload = false }) { Text("Cancel") } },
        )
    }
}

private fun megabytes(bytes: Long): String = "${(bytes + 512 * 1024) / (1024 * 1024)} MiB"
