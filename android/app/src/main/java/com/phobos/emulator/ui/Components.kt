package com.phobos.emulator.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.ArrowDropDown
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Pause
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.SkipPrevious
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.DialogProperties
import kotlin.math.roundToInt
import com.phobos.emulator.LogLevel
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.data.RegionPreference
import com.phobos.emulator.util.ZxTape
import com.phobos.emulator.ui.theme.LegibleText
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.glassPanel
import com.phobos.emulator.ui.theme.neon
import com.phobos.emulator.ui.theme.sunsetPlate
import com.phobos.emulator.ui.theme.neonGlow
import com.phobos.emulator.ui.theme.pillShape

/** A titled group of settings rows on a rounded card. */
@Composable
fun SettingsCategory(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        SectionHeader(title)
        SettingsCard(content = content)
    }
}

/**
 * Section title in the primary color; uppercase neon with retrowave effects, on a soft plate and
 * outlined where it is drawn straight on the backdrop ([onBackdrop]; false inside panels such as the
 * pause menu).
 */
@Composable
fun SectionHeader(title: String, modifier: Modifier = Modifier, onBackdrop: Boolean = true, trailing: (@Composable () -> Unit)? = null) {
    val scheme = MaterialTheme.colorScheme
    val primary = scheme.primary
    val theme = LocalPhobosTheme.current
    val retrowave = theme.retrowave
    val solid = theme.solid
    Row(
        modifier = modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp, bottom = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(Modifier.weight(1f), verticalAlignment = Alignment.CenterVertically) {
            val text = if (retrowave || solid?.capitalHeaders == true) title.uppercase() else title
            val style = when {
                retrowave -> MaterialTheme.typography.labelLarge.neon(primary)
                solid != null -> solid.sectionStyle(MaterialTheme.typography.titleSmall)
                else -> MaterialTheme.typography.titleSmall
            }
            if (onBackdrop) {
                LegibleText(
                    text,
                    style = style,
                    color = primary,
                    modifier = solid?.plate() ?: Modifier.sunsetPlate(scheme.background, theme.glass.backdropPlateAlpha),
                )
            } else {
                Text(text, style = style, color = primary)
            }
            solid?.sectionRule()?.let { rule ->
                Spacer(Modifier.padding(start = 12.dp).weight(1f).height(8.dp).then(rule))
            }
        }
        trailing?.invoke()
    }
}

/**
 * Text drawn straight on the page backdrop rather than on a panel (notes, hints, empty states), on
 * the headers' soft plate and outlined so it stays readable over any part of the backdrop.
 */
@Composable
fun BackdropText(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = LocalTextStyle.current,
    color: Color = Color.Unspecified,
) {
    val theme = LocalPhobosTheme.current
    val background = MaterialTheme.colorScheme.background
    LegibleText(
        text,
        modifier = modifier.then(theme.solid?.plate() ?: Modifier.sunsetPlate(background, theme.glass.backdropPlateAlpha)),
        style = style,
        color = color,
    )
}

/**
 * Glass card shared by settings groups, library tiles and the other screens' panels: a translucent
 * surface over the backdrop with a gloss, a shade and a bright top rim, or with the neon edge in
 * place of the rim and shadow when retrowave effects are on. The fill is as see-through as text
 * contrast allows; set [accentText] where the card shows secondary, tertiary, error, success or
 * warning text. A click ripple stays inside [shape], and a clickable card dips slightly when pressed.
 * With glass effects off it is the opaque surface with a faint outline (or the neon edge).
 */
@Composable
fun ThemedCard(
    modifier: Modifier = Modifier,
    shape: Shape = MaterialTheme.shapes.large,
    onClick: (() -> Unit)? = null,
    accentText: Boolean = false,
    fill: Color = MaterialTheme.colorScheme.surfaceContainer,
    content: @Composable () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val glass = theme.glass
    val glassOff = glass.level == GlassEffects.OFF
    val solid = theme.solid
    val panel = if (solid != null) {
        solid.panel(shape, fill)
    } else {
        Modifier.glassPanel(
            shape = shape,
            fill = fill,
            alpha = if (accentText) glass.accentPanelAlpha else glass.panelAlpha,
            style = glass,
            isDark = theme.isDark,
            rim = !theme.retrowave,
            shadow = !theme.retrowave,
            outline = if (glassOff && !theme.retrowave) scheme.outlineVariant.copy(alpha = 0.45f) else Color.Unspecified,
        )
    }
    val edge = if (theme.retrowave) Modifier.neonGlow(scheme.primary, shape, intensity = 0.45f) else Modifier
    if (onClick != null) {
        val interactionSource = remember { MutableInteractionSource() }
        val pressed by interactionSource.collectIsPressedAsState()
        val scale by animateFloatAsState(if (pressed && (solid != null || !glassOff)) 0.97f else 1f, spring(dampingRatio = 0.5f, stiffness = Spring.StiffnessMedium), label = "cardPress")
        Surface(
            onClick = onClick,
            modifier = modifier.graphicsLayer { scaleX = scale; scaleY = scale }.then(edge).then(panel).focusRing(scheme.primary, shape),
            shape = shape,
            color = Color.Transparent,
            contentColor = scheme.onSurface,
            interactionSource = interactionSource,
            content = content,
        )
    } else {
        Surface(modifier = modifier.then(edge).then(panel), shape = shape, color = Color.Transparent, contentColor = scheme.onSurface, content = content)
    }
}

/** Glass card for settings rows; see [ThemedCard] for [accentText]. */
@Composable
fun SettingsCard(modifier: Modifier = Modifier, accentText: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
    ThemedCard(modifier.fillMaxWidth(), accentText = accentText) {
        Column(Modifier.padding(vertical = 6.dp), content = content)
    }
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
        shape = pillShape(),
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
            val solid = LocalPhobosTheme.current.solid
            if (solid != null) {
                solid.Switch(checked)
            } else {
                Switch(
                    checked = checked,
                    onCheckedChange = null,
                    thumbContent = if (checked) { { Icon(Icons.Rounded.Check, contentDescription = null, modifier = Modifier.size(SwitchDefaults.IconSize)) } } else null,
                )
            }
        },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().focusRing(MaterialTheme.colorScheme.primary).toggleable(value = checked, role = Role.Switch, onValueChange = onCheckedChange),
    )
}

@Composable
fun SettingsCheckboxItem(title: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        trailingContent = { Checkbox(checked = checked, onCheckedChange = null) },
        colors = transparentListItemColors(),
        modifier = Modifier.fillMaxWidth().focusRing(MaterialTheme.colorScheme.primary).toggleable(value = checked, role = Role.Checkbox, onValueChange = onCheckedChange),
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
        modifier = Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.4f).focusRing(MaterialTheme.colorScheme.primary).clickable(enabled = enabled) { expanded = true },
        headlineContent = { Text(title) },
        supportingContent = if (description != null) { { Text(description) } } else null,
        trailingContent = {
            Box {
                ValuePill(label(current), trailingIcon = Icons.Rounded.ArrowDropDown)
                PhobosDropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
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
@OptIn(ExperimentalMaterial3Api::class)
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
        val solid = LocalPhobosTheme.current.solid
        if (solid != null) {
            Slider(
                value = local,
                onValueChange = { local = it },
                enabled = enabled,
                onValueChangeFinished = { onCommit(local) },
                thumb = { solid.SliderThumb() },
                track = { state ->
                    val span = state.valueRange.endInclusive - state.valueRange.start
                    solid.SliderTrack(if (span > 0f) (state.value - state.valueRange.start) / span else 0f)
                },
                valueRange = range,
            )
        } else {
            Slider(
                value = local,
                onValueChange = { local = it },
                onValueChangeFinished = { onCommit(local) },
                valueRange = range,
                enabled = enabled,
            )
        }
    }
}

/** Material's alert dialog; a pixel window, with a hard border and shadow, with pixel art effects. */
@Composable
fun PhobosAlertDialog(
    onDismissRequest: () -> Unit,
    confirmButton: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    dismissButton: @Composable (() -> Unit)? = null,
    title: @Composable (() -> Unit)? = null,
    text: @Composable (() -> Unit)? = null,
    properties: DialogProperties = DialogProperties(),
) {
    HoldScreensaver()
    val shape = AlertDialogDefaults.shape
    AlertDialog(
        onDismissRequest = onDismissRequest,
        confirmButton = confirmButton,
        modifier = modifier.then(dialogEdge(shape)),
        dismissButton = dismissButton,
        title = title,
        text = text,
        shape = shape,
        properties = properties,
    )
}

/** The edge of a dialog panel in [shape]: the solid style's edge, such as a pixel panel's border and shadow. */
@Composable
fun dialogEdge(shape: Shape): Modifier {
    val theme = LocalPhobosTheme.current
    return theme.solid?.edge(shape) ?: Modifier
}

/** Material's dropdown menu; in a solid style, its border in place of the menu's soft shadow. */
@Composable
fun PhobosDropdownMenu(expanded: Boolean, onDismissRequest: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    if (expanded) HoldScreensaver()
    val theme = LocalPhobosTheme.current
    val solid = theme.solid
    if (solid != null) {
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = onDismissRequest,
            shadowElevation = 0.dp,
            border = solid.menuBorder(),
            content = content,
        )
    } else {
        DropdownMenu(expanded = expanded, onDismissRequest = onDismissRequest, content = content)
    }
}

/** Linear progress, indeterminate while [progress] is null; the solid style's own bar in a solid style. */
@Composable
fun ProgressBar(progress: (() -> Float)?, modifier: Modifier = Modifier) {
    val solid = LocalPhobosTheme.current.solid
    when {
        solid != null -> solid.ProgressBar(progress, modifier)
        progress != null -> LinearProgressIndicator(progress = progress, modifier = modifier)
        else -> LinearProgressIndicator(modifier = modifier)
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
        modifier = Modifier.fillMaxWidth().focusRing(MaterialTheme.colorScheme.primary).clickable(onClick = onClick),
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
fun NgcdLoadSpeedItem(current: Int, onSelect: (Int) -> Unit) {
    SettingsDropdownItem(
        title = "Neo Geo CD Loading Speed",
        description = "Runs the game faster while the disc loads, up to this rate if the device keeps up. The emulated drive keeps its real speed.",
        current = current,
        options = LOAD_SPEEDS,
        label = ::loadSpeedLabel,
        onSelect = onSelect,
    )
}

@Composable
fun ZxLoadSpeedItem(current: Int, onSelect: (Int) -> Unit) {
    SettingsDropdownItem(
        title = "ZX Spectrum Tape Loading Speed",
        description = "Runs the game faster while a tape plays, up to this rate if the device keeps up. The tape keeps its real speed, so every loader works.",
        current = current,
        options = LOAD_SPEEDS,
        label = ::loadSpeedLabel,
        onSelect = onSelect,
    )
}

@Composable
fun MsxLoadSpeedItem(current: Int, onSelect: (Int) -> Unit) {
    SettingsDropdownItem(
        title = "MSX Tape Loading Speed",
        description = "Runs the game faster while the cassette motor runs the tape, up to this rate if the device keeps up. The tape keeps its real speed, so every loader works.",
        current = current,
        options = LOAD_SPEEDS,
        label = ::loadSpeedLabel,
        onSelect = onSelect,
    )
}

@Composable
fun ZxTapeControlItem(automatic: Boolean, onSelect: (Boolean) -> Unit) {
    SettingsDropdownItem(
        title = "ZX Spectrum Tape Control",
        description = "Automatic plays the tape while a game's loader reads it and stops it when the game moves on, so multi-load games find their next part. Manual leaves the tape to its buttons.",
        current = automatic,
        options = listOf(true, false),
        label = { if (it) "Automatic" else "Manual" },
        onSelect = onSelect,
    )
}

/** The tape's counter with its rewind button, and play or stop unless the machine runs the tape itself. */
@Composable
fun ZxTapeItem(tape: ZxTape, onPlaying: ((Boolean) -> Unit)?, onRewind: () -> Unit) {
    val ring = MaterialTheme.colorScheme.primary
    ListItem(
        headlineContent = { Text("Tape") },
        supportingContent = { Text(tape.status) },
        trailingContent = {
            Row {
                IconButton(onClick = onRewind, enabled = tape.positionMs > 0, modifier = Modifier.focusRing(ring, CircleShape)) {
                    Icon(Icons.Rounded.SkipPrevious, contentDescription = "Rewind the tape")
                }
                if (onPlaying != null) {
                    IconButton(onClick = { onPlaying(!tape.playing) }, modifier = Modifier.focusRing(ring, CircleShape)) {
                        if (tape.playing) Icon(Icons.Rounded.Pause, contentDescription = "Stop the tape")
                        else Icon(Icons.Rounded.PlayArrow, contentDescription = "Play the tape")
                    }
                }
            }
        },
        colors = transparentListItemColors(),
    )
}

/** The speeds a game can run at while it loads (a Neo Geo CD's disc, a ZX Spectrum's tape). */
val LOAD_SPEEDS = listOf(1, 2, 4, 8)

fun loadSpeedLabel(speed: Int): String = if (speed == 1) "Accurate (1x)" else "${speed}x"

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
