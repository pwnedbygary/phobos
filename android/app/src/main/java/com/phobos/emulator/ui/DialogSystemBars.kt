package com.phobos.emulator.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.window.DialogWindowProvider
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

/**
 * Keeps a dialog as full screen as the screen under it. A dialog has a window of its own, which
 * brings the system bars back unless it hides them too: with Full Screen Mode on, the menus hide the
 * status bar and a game ([inGame]) hides both bars. Call from inside the dialog's content.
 */
@Composable
fun DialogSystemBars(fullScreen: Boolean, inGame: Boolean) {
    val view = LocalView.current
    val window = ((view as? DialogWindowProvider) ?: (view.parent as? DialogWindowProvider))?.window ?: return
    DisposableEffect(window, fullScreen, inGame) {
        if (fullScreen) {
            val controller = WindowCompat.getInsetsController(window, window.decorView)
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.hide(if (inGame) WindowInsetsCompat.Type.systemBars() else WindowInsetsCompat.Type.statusBars())
        }
        onDispose {}
    }
}
