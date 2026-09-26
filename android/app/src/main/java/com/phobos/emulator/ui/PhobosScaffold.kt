package com.phobos.emulator.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neon
import com.phobos.emulator.ui.theme.neonBar

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
    val retrowave = LocalPhobosTheme.current.retrowave
    TopAppBar(
        title = {
            Text(
                text = if (retrowave) title.uppercase() else title,
                style = if (retrowave) MaterialTheme.typography.titleLarge.neon(scheme.primary) else MaterialTheme.typography.titleLarge,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        },
        navigationIcon = {
            if (onBack != null) {
                IconButton(onClick = onBack) {
                    Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = "Back")
                }
            }
        },
        actions = actions,
        colors = TopAppBarDefaults.topAppBarColors(
            containerColor = Color.Transparent,
            scrolledContainerColor = if (retrowave) scheme.surfaceContainer.copy(alpha = 0.92f) else scheme.surfaceContainer,
            titleContentColor = if (retrowave) scheme.primary else scheme.onSurface,
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
    val retrowave = LocalPhobosTheme.current.retrowave
    Column(modifier.fillMaxWidth().padding(start = 4.dp, top = 8.dp, bottom = 8.dp)) {
        Text(
            text = if (retrowave) title.uppercase() else title,
            style = if (retrowave) MaterialTheme.typography.headlineMedium.neon(scheme.primary) else MaterialTheme.typography.headlineMedium,
            color = if (retrowave) scheme.primary else scheme.onBackground,
        )
        if (subtitle != null) {
            Text(subtitle, style = MaterialTheme.typography.bodyMedium, color = scheme.onSurfaceVariant)
        }
    }
}
