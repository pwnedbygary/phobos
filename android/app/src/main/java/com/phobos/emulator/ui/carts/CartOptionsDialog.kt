package com.phobos.emulator.ui.carts

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.phobos.emulator.util.displayTitle

/**
 * "Cart Options" for one game: shell finish and colour, written through [CartLooks] as a per-game entry in the
 * settings' look map ("g:<id>" -> "finish|#rrggbb"). "System default" removes the per-game entry so the system's
 * look (or a special edition) applies again. Colours and finishes are Cartridge's own lists.
 */
@Composable
fun CartOptionsDialog(
    system: String,
    romName: String,
    romTitle: String? = null,
    looksJson: String,
    onSave: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val key = CartLooks.gameKey(system, romName)
    val current = remember(looksJson, key) { CartLooks.parse(looksJson)[key] }
    var finish by remember(current) { mutableStateOf(current?.finish ?: "solid") }
    var color by remember(current) {
        mutableStateOf(current?.color ?: LookResolver.defaultShellColors["solid"] ?: "#6f4fb0")
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Cart Options") },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(displayTitle(romName, romTitle), style = MaterialTheme.typography.labelLarge)
                Spacer(Modifier.height(12.dp))
                Text("Finish", style = MaterialTheme.typography.labelMedium)
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    LookResolver.shellFinishes.take(3).forEach { f ->
                        FilterChip(selected = finish == f.id, onClick = { finish = f.id }, label = { Text(f.label) })
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    LookResolver.shellFinishes.drop(3).forEach { f ->
                        FilterChip(selected = finish == f.id, onClick = { finish = f.id }, label = { Text(f.label) })
                    }
                }
                Spacer(Modifier.height(12.dp))
                Text("Color", style = MaterialTheme.typography.labelMedium)
                Spacer(Modifier.height(4.dp))
                LookResolver.shellColors.chunked(5).forEach { row ->
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        row.forEach { c ->
                            val selected = color.equals(c.hex, ignoreCase = true)
                            Box(
                                Modifier
                                    .size(if (selected) 30.dp else 26.dp)
                                    .clip(CircleShape)
                                    .background(runCatching { Color(android.graphics.Color.parseColor(c.hex)) }.getOrDefault(Color.Gray))
                                    .clickable { color = c.hex },
                                contentAlignment = Alignment.Center,
                            ) {
                                if (selected) {
                                    Box(Modifier.size(8.dp).clip(CircleShape).background(MaterialTheme.colorScheme.onSurface))
                                }
                            }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                }
                Spacer(Modifier.height(12.dp))
                TextButton(onClick = {
                    onSave(CartLooks.with(looksJson, CartLooks.systemKey(system), LookResolver.Look(finish, color)))
                    onDismiss()
                }) { Text("Apply this look to every game in $system") }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                onSave(CartLooks.with(looksJson, key, LookResolver.Look(finish, color)))
                onDismiss()
            }) { Text("Apply") }
        },
        dismissButton = {
            Row {
                TextButton(onClick = {
                    onSave(CartLooks.with(looksJson, key, null))
                    onDismiss()
                }) { Text("System default") }
                TextButton(onClick = onDismiss) { Text("Cancel") }
            }
        },
    )
}
