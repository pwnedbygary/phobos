package com.phobos.emulator.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.launch.LaunchSystems
import com.phobos.emulator.ui.theme.pillShape

/**
 * Systems whose cores are still experimental, by the names the Library lists them under. The Library's tiles,
 * Platform Visibility and the systems' settings mark them Experimental; only what's shown changes, not the names
 * the cores, folders, saves and settings go by.
 */
val experimentalSystems: Set<String> = setOf(LaunchSystems.PSP)

/** "Experimental" on a small tonal pill when [system] is in [experimentalSystems]; nothing otherwise. */
@Composable
fun ExperimentalBadge(system: String, modifier: Modifier = Modifier) {
    if (system in experimentalSystems) {
        Surface(
            modifier = modifier,
            shape = pillShape(),
            color = MaterialTheme.colorScheme.tertiaryContainer,
            contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
        ) {
            Text(
                "Experimental",
                style = MaterialTheme.typography.labelSmall,
                maxLines = 1,
                modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            )
        }
    }
}
