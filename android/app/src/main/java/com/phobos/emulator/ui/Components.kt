package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.ArrowDropDown
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt
import com.phobos.emulator.LogLevel
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neon
import com.phobos.emulator.ui.theme.neonGlow

/** A titled group of settings rows on a rounded card. */
@Composable
fun SettingsCategory(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        SectionHeader(title)
        SettingsCard(content = content)
    }
}

/** Section title in the primary color; uppercase neon with retrowave effects. */
@Composable
fun SectionHeader(title: String, modifier: Modifier = Modifier, trailing: (@Composable () -> Unit)? = null) {
    val primary = MaterialTheme.colorScheme.primary
    val retrowave = LocalPhobosTheme.current.retrowave
    Row(
        modifier = modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = if (retrowave) title.uppercase() else title,
            style = if (retrowave) MaterialTheme.typography.labelLarge.neon(primary) else MaterialTheme.typography.titleSmall,
            color = primary,
            modifier = Modifier.weight(1f),
        )
        trailing?.invoke()
    }
}

/** Rounded card for settings rows; frosted with a neon edge when retrowave effects are on. */
@Composable
fun SettingsCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    val scheme = MaterialTheme.colorScheme
    val retrowave = LocalPhobosTheme.current.retrowave
    val shape = MaterialTheme.shapes.large
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .then(if (retrowave) Modifier.neonGlow(scheme.primary, shape, intensity = 0.45f) else Modifier),
        shape = shape,
        color = if (retrowave) scheme.surfaceContainer.copy(alpha = 0.92f) else scheme.surfaceContainer,
        contentColor = scheme.onSurface,
        border = if (retrowave) null else BorderStroke(1.dp, scheme.outlineVariant.copy(alpha = 0.45f)),
    ) {
        Column(Modifier.padding(vertical = 6.dp), content = content)
    }
}

/**
 * Background for one row of a card spanning several lazy-list items: large corners at the ends,
 * small ones between rows (pair with a 2 dp item spacing).
 */
fun Modifier.groupedCard(index: Int, count: Int, color: Color, outer: Dp = 20.dp, inner: Dp = 6.dp): Modifier {
    val top = if (index == 0) outer else inner
    val bottom = if (index == count - 1) outer else inner
    return clip(RoundedCornerShape(topStart = top, topEnd = top, bottomStart = bottom, bottomEnd = bottom)).background(color)
}

/** List rows drawn on a card leave the card's color showing. */
@Composable
fun transparentListItemColors(): ListItemColors = ListItemDefaults.colors(containerColor = Color.Transparent)

/** Tinted rounded square holding a row's icon. */
@Composable
fun IconBadge(icon: ImageVector, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    Box(
        modifier = modifier.size(40.dp).clip(MaterialTheme.shapes.small).background(scheme.secondaryContainer),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = scheme.onSecondaryContainer, modifier = Modifier.size(22.dp))
    }
}

/** Compact tonal pill showing a current value. */
@Composable
fun ValuePill(text: String, modifier: Modifier = Modifier, trailingIcon: ImageVector? = null) {
    Surface(
        modifier = modifier.widthIn(max = 200.dp),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.secondaryContainer,
        contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
    ) {
        Row(
            modifier = Modifier.padding(start = 12.dp, end = if (trailingIcon != null) 6.dp else 12.dp, top = 6.dp, bottom = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(text, style = MaterialTheme.typography.labelLarge, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false))
            if (trailingIcon != null) Icon(trailingIcon, contentDescription = null, modifier = Modifier.size(20.dp))
        }
    }
}

@Composable
fun SettingsSwitchItem(title: String, description: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = if (description.isNotEmpty()) { { Text(description) } } else null,
        trailingContent = {
            Switch(
                checked = checked,
                onCheckedChange = null,
                thumbContent = if (checked) { { Icon(Icons.Rounded.Check, contentDescription = null, modifier = Modifier.size(SwitchDefaults.IconSize)) } } else null,
            )
        },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().toggleable(value = checked, role = Role.Switch, onValueChange = onCheckedChange),
    )
}

@Composable
fun SettingsCheckboxItem(title: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        trailingContent = { Checkbox(checked = checked, onCheckedChange = null) },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().toggleable(value = checked, role = Role.Checkbox, onValueChange = onCheckedChange),
    )
}

/** A list row with a dropdown picker on the right. */
@Composable
fun <T> SettingsDropdownItem(
    title: String,
    description: String?,
    current: T,
    options: List<T>,
    label: (T) -> String,
    onSelect: (T) -> Unit,
    enabled: Boolean = true,
) {
    var expanded by remember { mutableStateOf(false) }
    ListItem(
        modifier = Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.4f).clickable(enabled = enabled) { expanded = true },
        headlineContent = { Text(title) },
        supportingContent = if (description != null) { { Text(description) } } else null,
        trailingContent = {
            Box {
                ValuePill(label(current), trailingIcon = Icons.Rounded.ArrowDropDown)
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    options.forEach { option ->
                        val selected = option == current
                        DropdownMenuItem(
                            text = { Text(label(option), fontWeight = if (selected) FontWeight.SemiBold else null) },
                            onClick = { onSelect(option); expanded = false },
                            trailingIcon = if (selected) { { Icon(Icons.Rounded.Check, contentDescription = null) } } else null,
                        )
                    }
                }
            }
        },
        colors = transparentListItemColors(),
    )
}

/** A labeled slider that follows the finger locally and persists once the drag ends. */
@Composable
fun SettingsSliderItem(
    title: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    format: (Float) -> String = { "${(it * 100).roundToInt()}%" },
    enabled: Boolean = true,
    onCommit: (Float) -> Unit,
) {
    var local by remember { mutableFloatStateOf(value) }
    LaunchedEffect(value) { local = value }
    Column(
        Modifier
            .fillMaxWidth()
            .alpha(if (enabled) 1f else 0.4f)
            .padding(start = 16.dp, end = 16.dp, top = 10.dp, bottom = 2.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(title, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            ValuePill(format(local))
        }
        Slider(
            value = local,
            onValueChange = { local = it },
            onValueChangeFinished = { onCommit(local) },
            valueRange = range,
            enabled = enabled,
        )
    }
}

@Composable
fun SettingsClickableItem(title: String, description: String, onClick: () -> Unit, icon: ImageVector? = null) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = if (description.isNotEmpty()) { { Text(description) } } else null,
        leadingContent = if (icon != null) { { IconBadge(icon) } } else null,
        trailingContent = { Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, contentDescription = null) },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
    )
}

@Composable
fun LogVerbositySelectorItem(currentLevel: LogLevel, onLevelSelected: (LogLevel) -> Unit) {
    SettingsDropdownItem(
        title = "Log Verbosity",
        description = "Filter logs by level (None turns off logging)",
        current = currentLevel,
        options = LogLevel.entries,
        label = { it.name },
        onSelect = onLevelSelected,
    )
}

@Composable
fun FastForwardSpeedSelectorItem(currentSpeed: Float, onSpeedSelected: (Float) -> Unit) {
    SettingsDropdownItem(
        title = "Fast Forward Speed",
        description = "Speed limit during fast forward",
        current = currentSpeed,
        options = listOf(1.5f, 2.0f, 3.0f, 5.0f, 0.0f), // 0.0f = Unlimited
        label = { if (it == 0.0f) "Unlimited" else "%.1fx".format(it) },
        onSelect = onSpeedSelected,
    )
}

@Composable
fun RegionSelectorItem(currentPref: RegionPreference, onPrefSelected: (RegionPreference) -> Unit) {
    SettingsDropdownItem(
        title = "Region Preference",
        description = "Preferred system region priority",
        current = currentPref,
        options = RegionPreference.entries,
        label = { it.label },
        onSelect = onPrefSelected,
    )
}

@Composable
fun PathSelectorItem(title: String, currentPath: String, onPathSelected: (String) -> Unit) {
    val context = LocalContext.current
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(uri, flags)
            onPathSelected(uri.toString())
        }
    }
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = {
            Text(if (currentPath.isEmpty()) "Not set" else Uri.parse(currentPath).path ?: currentPath, maxLines = 2, overflow = TextOverflow.Ellipsis)
        },
        leadingContent = { IconBadge(Icons.Rounded.FolderOpen) },
        trailingContent = { Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, contentDescription = null) },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().clickable(onClickLabel = "Select Path") { launcher.launch(null) },
    )
}

@Composable
fun FirmwareSelectorItem(system: String, currentPath: String, onPathSelected: (String) -> Unit) {
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) onPathSelected(uri.toString())
    }
    ListItem(
        headlineContent = { Text(system) },
        supportingContent = {
            Text(if (currentPath.isEmpty()) "BIOS not set" else Uri.parse(currentPath).path ?: currentPath, maxLines = 2, overflow = TextOverflow.Ellipsis)
        },
        leadingContent = { IconBadge(Icons.Rounded.Description) },
        trailingContent = { Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, contentDescription = null) },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().clickable(onClickLabel = "Select BIOS") { launcher.launch(arrayOf("*/*")) },
    )
}
