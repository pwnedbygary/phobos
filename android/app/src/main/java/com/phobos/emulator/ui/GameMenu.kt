package com.phobos.emulator.ui

import android.view.KeyEvent
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material.icons.rounded.StarBorder
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalViewConfiguration
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * The menu of the game titled [title] on [system]: [items], each a focus-ringed row. [openedByHeldA] when a held A
 * opened it, so that A's repeats and release can't pick an item (see [HeldKeyGuard]).
 */
@Composable
fun GameMenu(
    title: String,
    system: String,
    items: List<GameMenuItem>,
    openedByHeldA: Boolean,
    fullScreen: Boolean,
    onPick: (GameMenuItem) -> Unit,
    onDismiss: () -> Unit,
) {
    val guard = remember { HeldKeyGuard(armed = openedByHeldA) }
    PhobosAlertDialog(
        onDismissRequest = onDismiss,
        modifier = Modifier.onPreviewKeyEvent { event ->
            val key = event.nativeKeyEvent
            key.keyCode == KeyEvent.KEYCODE_BUTTON_A &&
                guard.drops(key.action == KeyEvent.ACTION_DOWN, key.repeatCount)
        },
        title = { DialogSystemBars(fullScreen, inGame = false); Text(title) },
        text = {
            Column {
                Text(
                    system,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                items.forEach { item ->
                    ListItem(
                        headlineContent = { Text(item.label) },
                        leadingContent = { Icon(item.icon, contentDescription = null) },
                        colors = transparentListItemColors(),
                        modifier = Modifier
                            .fillMaxWidth()
                            .focusRing(MaterialTheme.colorScheme.primary)
                            .clickable { onPick(item) },
                    )
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

private val GameMenuItem.label: String
    get() = when (this) {
        GameMenuItem.ADD_TO_FAVORITES -> "Add to Favorites"
        GameMenuItem.REMOVE_FROM_FAVORITES -> "Remove from Favorites"
        GameMenuItem.HIDE_FROM_CONTINUE_PLAYING -> "Hide from Continue Playing"
        GameMenuItem.REDOWNLOAD_ARTWORK -> "Re-download Artwork"
    }

/** An item's icon: the star filled while the game is a favorite, as its card's is, and outlined while it isn't. */
private val GameMenuItem.icon: ImageVector
    get() = when (this) {
        GameMenuItem.ADD_TO_FAVORITES -> Icons.Rounded.StarBorder
        GameMenuItem.REMOVE_FROM_FAVORITES -> Icons.Rounded.Star
        GameMenuItem.HIDE_FROM_CONTINUE_PLAYING -> Icons.Rounded.VisibilityOff
        GameMenuItem.REDOWNLOAD_ARTWORK -> Icons.Rounded.Refresh
    }

/**
 * A controller's buttons on a game. X toggles it as a favorite ([onFavorite]). A held for the long-press timeout opens
 * its menu ([onMenu]), and let go of sooner starts it ([onClick]); while it's down the press shows on
 * [interactionSource], as a touch's does. Only A's down and up count (see [KeyHold]). Goes before the game's clickable,
 * which still handles the other keys that click (a keyboard's Enter, the centre Android makes of an unhandled Start).
 */
@Composable
fun Modifier.gameButtons(
    interactionSource: MutableInteractionSource,
    onClick: () -> Unit,
    onMenu: () -> Unit,
    onFavorite: () -> Unit,
): Modifier {
    val scope = rememberCoroutineScope()
    val timeout = LocalViewConfiguration.current.longPressTimeoutMillis
    val hold = remember { KeyHold() }
    val pending = remember { PendingPress() }
    val click by rememberUpdatedState(onClick)
    val menu by rememberUpdatedState(onMenu)
    val favorite by rememberUpdatedState(onFavorite)
    fun endPress() {
        pending.press?.let { press -> scope.launch { interactionSource.emit(PressInteraction.Release(press)) } }
        pending.press = null
    }
    fun stopTimer() {
        pending.timer?.cancel()
        pending.timer = null
    }
    return this
        .onSizeChanged { pending.centre = Offset(it.width / 2f, it.height / 2f) }
        .onFocusChanged {
            if (!it.hasFocus) {
                stopTimer()
                endPress()
                hold.cancel()
            }
        }
        .onPreviewKeyEvent { event ->
            val key = event.nativeKeyEvent
            when (key.keyCode) {
                KeyEvent.KEYCODE_BUTTON_A -> {
                    if (key.action == KeyEvent.ACTION_DOWN && hold.press(key.repeatCount)) {
                        // A press whose up went elsewhere ends here, its timer and its feedback with it.
                        stopTimer()
                        endPress()
                        val press = PressInteraction.Press(pending.centre)
                        pending.press = press
                        scope.launch { interactionSource.emit(press) }
                        pending.timer = scope.launch {
                            delay(timeout)
                            if (hold.timeout()) {
                                endPress()
                                menu()
                            }
                        }
                    } else if (key.action == KeyEvent.ACTION_UP) {
                        stopTimer()
                        endPress()
                        if (key.isCanceled) hold.cancel() else if (hold.release()) click()
                    }
                    true
                }
                KeyEvent.KEYCODE_BUTTON_X -> {
                    if (key.action == KeyEvent.ACTION_DOWN && key.repeatCount == 0) favorite()
                    true
                }
                else -> false
            }
        }
}

/** A's press under way: shown as [press] from [centre], with [timer] running out at the long-press timeout. */
private class PendingPress {
    var centre = Offset.Zero
    var press: PressInteraction.Press? = null
    var timer: Job? = null
}
