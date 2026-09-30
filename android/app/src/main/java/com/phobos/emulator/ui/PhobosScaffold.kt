package com.phobos.emulator.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.IconButton
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.LegibleIcon
import com.phobos.emulator.ui.theme.LegibleText
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neon
import com.phobos.emulator.ui.theme.neonBar
import com.phobos.emulator.ui.theme.rememberCursorBlink
import com.phobos.emulator.ui.theme.sunsetPlate

/**
 * Height of the floating dock, which pages run under to the bottom of the screen: lists pad their
 * ends by it so the last item can scroll clear, and fixed layouts stop above it. Zero on pages
 * without the dock.
 */
val LocalDockInset = compositionLocalOf { 0.dp }

/** A page list's content padding: [padding] on every side, plus room at the end to scroll clear of the dock. */
@Composable
fun pageContentPadding(padding: Dp = 16.dp): PaddingValues =
    PaddingValues(start = padding, top = padding, end = padding, bottom = padding + LocalDockInset.current)

/**
 * Frame shared by every page: a transparent Scaffold, so the app background and the retrowave
 * backdrop show through, under the Phobos top bar. A null [onBack] hides the back button.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PhobosScaffold(
    title: String,
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    floatingActionButton: @Composable () -> Unit = {},
    bottomBar: @Composable () -> Unit = {},
    content: @Composable (PaddingValues) -> Unit,
) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior()
    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = { PhobosTopBar(title, onBack, actions, scrollBehavior) },
        bottomBar = bottomBar,
        floatingActionButton = floatingActionButton,
        // Transparent containers don't imply a content color, so set it explicitly.
        containerColor = Color.Transparent,
        contentColor = MaterialTheme.colorScheme.onBackground,
        content = content,
    )
}

/** Top bar that tints once content scrolls under it; neon title and edge with retrowave effects. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PhobosTopBar(
    title: String,
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null,
) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val retrowave = theme.retrowave
    val solid = theme.solid
    TopAppBar(
        title = {
            val style = when {
                retrowave -> MaterialTheme.typography.titleLarge.neon(scheme.primary)
                solid != null -> solid.titleStyle(MaterialTheme.typography.titleLarge)
                else -> MaterialTheme.typography.titleLarge
            }
            Row {
                LegibleText(
                    text = if (retrowave || solid?.capitalHeaders == true) title.uppercase() else title,
                    style = style,
                    color = LocalContentColor.current,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false).alignByBaseline(),
                )
                if (solid?.titleCursor == true) TitleCursor(style, LocalContentColor.current)
            }
        },
        navigationIcon = {
            if (onBack != null) {
                IconButton(onClick = onBack) {
                    LegibleIcon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back")
                }
            }
        },
        actions = actions,
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = Color.Transparent,
            scrolledContainerColor = if (retrowave) scheme.surfaceContainer.copy(alpha = 0.92f) else scheme.surfaceContainer,
            titleContentColor = if (retrowave) scheme.primary else solid?.titleColor(onPanel = true) ?: scheme.onSurface,
            navigationIconContentColor = scheme.onSurface,
            actionIconContentColor = scheme.onSurfaceVariant,
        ),
        scrollBehavior = scrollBehavior,
        modifier = if (retrowave) Modifier.neonBar(scheme, lineAtBottom = true) else Modifier,
    )
}

/** Large title for the top-level tabs, scrolling with their content. */
@Composable
fun ScreenHeader(title: String, subtitle: String? = null, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val retrowave = theme.retrowave
    val solid = theme.solid
    val plate = solid?.plate() ?: Modifier.sunsetPlate(scheme.background, theme.glass.backdropPlateAlpha)
    val style = when {
        retrowave -> MaterialTheme.typography.headlineMedium.neon(scheme.primary)
        solid != null -> solid.titleStyle(MaterialTheme.typography.headlineMedium)
        else -> MaterialTheme.typography.headlineMedium
    }
    val color = if (retrowave) scheme.primary else solid?.titleColor(onPanel = false) ?: scheme.onBackground
    Column(modifier.fillMaxWidth().padding(start = 4.dp, top = 8.dp, bottom = 8.dp)) {
        Row(plate) {
            LegibleText(
                text = if (retrowave || solid?.capitalHeaders == true) title.uppercase() else title,
                style = style,
                color = color,
                modifier = Modifier.weight(1f, fill = false).alignByBaseline(),
            )
            if (solid?.titleCursor == true) TitleCursor(style, color)
        }
        if (subtitle != null) {
            LegibleText(subtitle, style = MaterialTheme.typography.bodyMedium, color = scheme.onSurfaceVariant, modifier = plate)
        }
    }
}

/** A terminal's block cursor after a title in [style], blinking; hidden from accessibility services. */
@Composable
private fun RowScope.TitleCursor(style: TextStyle, color: Color) {
    val on = rememberCursorBlink(enabled = true)
    val density = LocalDensity.current
    val width = with(density) { (style.fontSize * 0.5f).toDp() }
    val height = with(density) { (style.fontSize * 0.62f).toDp() }
    Spacer(
        Modifier
            .padding(start = width / 3)
            .size(width, height)
            .alignBy { it.measuredHeight }
            .drawBehind { if (on.value) drawRect(color) },
    )
}
