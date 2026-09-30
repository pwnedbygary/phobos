package com.phobos.emulator.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.phobos.emulator.util.discNumber
import com.phobos.emulator.util.romTitle

/**
 * Picks one of a multi-disc game's [discs], starting on [selected]. [inDrive] marks the disc the running
 * game has in; [otherFile] adds a button for a file that isn't one of the discs.
 */
@Composable
fun DiscChoiceDialog(
    title: String,
    discs: List<RomFile>,
    selected: Int,
    confirmLabel: String,
    fullScreen: Boolean,
    inGame: Boolean,
    onDismiss: () -> Unit,
    onChoose: (Int) -> Unit,
    inDrive: Int = -1,
    otherFile: (() -> Unit)? = null,
) {
    var choice by remember(discs, selected) { mutableIntStateOf(selected.coerceIn(0, (discs.size - 1).coerceAtLeast(0))) }
    PhobosAlertDialog(
        onDismissRequest = onDismiss,
        title = { DialogSystemBars(fullScreen, inGame); Text(title) },
        text = {
            Column(Modifier.selectableGroup()) {
                discs.forEachIndexed { index, disc ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .focusRing(MaterialTheme.colorScheme.primary)
                            .selectable(selected = index == choice, role = Role.RadioButton) { choice = index }
                            .padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        RadioButton(selected = index == choice, onClick = null)
                        Column(Modifier.padding(start = 12.dp).weight(1f)) {
                            Text("Disc ${discNumber(disc.name) ?: (index + 1)}", style = MaterialTheme.typography.bodyLarge)
                            Text(romTitle(disc.name), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        if (index == inDrive) {
                            Text("In the drive", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                        }
                    }
                }
                if (otherFile != null) TextButton(onClick = otherFile) { Text("Another file…") }
            }
        },
        confirmButton = { TextButton(onClick = { onChoose(choice) }) { Text(confirmLabel) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
