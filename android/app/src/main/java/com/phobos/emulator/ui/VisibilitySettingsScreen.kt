package com.phobos.emulator.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material.icons.Icons
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun VisibilitySettingsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val allSystems = viewModel.systems

    PhobosScaffold(title = "Platform Visibility", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            item {
                Text(
                    "Select the systems you want to display in your library grid.",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(bottom = 16.dp),
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            itemsIndexed(allSystems) { index, system ->
                val isVisible = system !in settings.hiddenSystems
                ListItem(
                    headlineContent = { Text(system) },
                    trailingContent = {
                        Checkbox(
                            checked = isVisible,
                            onCheckedChange = { viewModel.setSystemVisibility(system, it) }
                        )
                    },
                    colors = transparentListItemColors(),
                    modifier = Modifier
                        .fillMaxWidth()
                        .groupedCard(index, allSystems.size, MaterialTheme.colorScheme.surfaceContainer)
                        .clickable { viewModel.setSystemVisibility(system, !isVisible) }
                )
            }
        }
    }
}
