package com.phobos.emulator.ui.cartridge

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Folder
import androidx.compose.material.icons.rounded.Image
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Science
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Tv
import androidx.compose.material.icons.rounded.ViewInAr
import androidx.compose.material.icons.rounded.Visibility
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.BuildConfig
import com.phobos.emulator.ui.MainViewModel
import com.phobos.emulator.ui.SettingsCard
import com.phobos.emulator.ui.SettingsClickableItem
import com.phobos.emulator.ui.theme.LocalPhobosTheme

/**
 * Settings in Cartridge's language: a narrow sidebar of groups and a pane of the group's rows. The sub-screens are
 * the app's own settings, restyled by the Cartridge effect; this page is only the hub.
 */
@Composable
fun CartridgeSettingsScreen(
    viewModel: MainViewModel,
    onBack: () -> Unit,
    onNavigateToAppearance: () -> Unit,
    onNavigateToEmulation: () -> Unit,
    onNavigateToVideo: () -> Unit,
    onNavigateToN64Experimental: () -> Unit,
    onNavigateToAudio: () -> Unit,
    onNavigateToPerformance: () -> Unit,
    onNavigateToConsole: () -> Unit,
    onNavigateToInputs: () -> Unit,
    onNavigateToPaths: () -> Unit,
    onNavigateToVisibility: () -> Unit,
    onNavigateToArtwork: () -> Unit,
    onNavigateToCarts: () -> Unit,
    onNavigateToAbout: () -> Unit,
) {
    val theme = LocalPhobosTheme.current
    val scheme = MaterialTheme.colorScheme
    data class Entry(val title: String, val description: String, val icon: ImageVector, val onClick: () -> Unit)
    data class Group(val title: String, val entry: List<Entry>)

    val groups = listOf(
        Group(
            "Library",
            listOf(
                Entry("Appearance", "Theme: ${theme.theme.name} · colours, style effects", Icons.Rounded.Palette, onNavigateToAppearance),
                Entry("Platform Visibility", "Choose which systems appear in the room", Icons.Rounded.Visibility, onNavigateToVisibility),
                Entry("Artwork", "Box art downloads, and what they send", Icons.Rounded.Image, onNavigateToArtwork),
                Entry("Carts & Boxes", "3D cartridges, labels, shells and wear", Icons.Rounded.ViewInAr, onNavigateToCarts),
            ),
        ),
        Group(
            "System",
            listOf(
                Entry("Emulation", "Region, firmware, GPU drivers, auto-save, CPU core", Icons.Rounded.Memory, onNavigateToEmulation),
                Entry("Video", "Colour emulation, interframe blending, shaders", Icons.Rounded.Tv, onNavigateToVideo),
                Entry("N64 Experimental", "Rendering, overclocking, debug logging", Icons.Rounded.Science, onNavigateToN64Experimental),
                Entry("Audio", "Mute and audio options", Icons.AutoMirrored.Rounded.VolumeUp, onNavigateToAudio),
                Entry("Performance Monitor", "In-game HUD: FPS, frame times, CPU/GPU, battery", Icons.Rounded.Speed, onNavigateToPerformance),
                Entry("Console", "The emulator's log and core messages", Icons.Rounded.BugReport, onNavigateToConsole),
            ),
        ),
        Group(
            "Controls",
            listOf(
                Entry("Inputs & Hotkeys", "Touch controls, controller mapping, hotkeys", Icons.Rounded.SportsEsports, onNavigateToInputs),
                Entry("Paths", "Firmware and save directories", Icons.Rounded.Folder, onNavigateToPaths),
            ),
        ),
        Group(
            "About",
            listOf(
                Entry("About Phobos", "Version, updates, credits and licences", Icons.Rounded.Info, onNavigateToAbout),
            ),
        ),
    )
    var selected by rememberSaveable { mutableIntStateOf(0) }
    val group = groups[selected.coerceIn(groups.indices)]
    Box(Modifier.fillMaxSize()) {
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 16.dp),
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
                Column {
                    Text(
                        "SETTINGS",
                        style = MaterialTheme.typography.labelSmall.copy(letterSpacing = 2.sp),
                        color = scheme.onBackground.copy(alpha = 0.55f),
                    )
                    Text(
                        "Phobos",
                        style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                        color = scheme.onBackground,
                    )
                }
                Spacer(Modifier.weight(1f))
                Text(
                    "Version ${BuildConfig.VERSION_NAME}",
                    style = MaterialTheme.typography.labelSmall,
                    color = scheme.onBackground.copy(alpha = 0.45f),
                )
            }
            Row(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 20.dp)) {
                Column(
                    Modifier
                        .width(232.dp)
                        .fillMaxHeight()
                        .padding(end = 18.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    groups.forEachIndexed { index, item ->
                        val on = index == selected
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .background(if (on) scheme.onBackground.copy(alpha = 0.12f) else Color.Transparent)
                                .clickable { selected = index }
                                .padding(horizontal = 14.dp, vertical = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Text(
                                item.title,
                                style = MaterialTheme.typography.titleSmall,
                                color = scheme.onBackground.copy(alpha = if (on) 0.95f else 0.58f),
                            )
                        }
                    }
                    Spacer(Modifier.weight(1f))
                    Text(
                        "Phobos · ares cores",
                        style = MaterialTheme.typography.labelSmall,
                        color = scheme.onBackground.copy(alpha = 0.35f),
                        modifier = Modifier.padding(horizontal = 14.dp, vertical = 8.dp),
                    )
                }
                Box(
                    Modifier
                        .width(1.dp)
                        .fillMaxHeight()
                        .background(scheme.onBackground.copy(alpha = 0.08f)),
                )
                LazyColumn(
                    Modifier.weight(1f).fillMaxHeight(),
                    contentPadding = PaddingValues(start = 22.dp, end = 8.dp, bottom = 24.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    item {
                        Text(
                            group.title,
                            style = MaterialTheme.typography.headlineSmall.copy(fontWeight = FontWeight.Bold),
                            color = scheme.onBackground,
                            modifier = Modifier.padding(top = 6.dp, bottom = 6.dp),
                        )
                    }
                    item {
                        SettingsCard {
                            group.entry.forEach { entry ->
                                SettingsClickableItem(
                                    title = entry.title,
                                    description = entry.description,
                                    onClick = entry.onClick,
                                    icon = entry.icon,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
