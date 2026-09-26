package com.phobos.emulator.ui

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.phobos.emulator.ui.touch.AnalogMode
import com.phobos.emulator.ui.touch.DpadMode
import com.phobos.emulator.ui.touch.HapticLevel
import com.phobos.emulator.ui.touch.TouchFamily
import com.phobos.emulator.ui.touch.TouchLayoutEditor
import com.phobos.emulator.ui.touch.TouchLayouts
import java.util.Locale

/** Global touch-control preferences plus a per-system entry point to the layout editor. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TouchSettingsScreen(viewModel: MainViewModel, onBack: () -> Unit, onEditLayout: (TouchFamily) -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val touch = settings.touch

    PhobosScaffold(title = "Touch Controls", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier.padding(innerPadding).fillMaxSize(),
            contentPadding = PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item {
                SettingsCategory("General") {
                    SettingsSwitchItem("Show Touch Controls", "Display on-screen controls in games", settings.showTouchControls) {
                        viewModel.setShowTouchControls(it)
                    }
                    SettingsSwitchItem(
                        "Hide While Using a Controller",
                        "Touch controls disappear when a gamepad is used and return when you touch the screen",
                        touch.hideOnController,
                    ) { v -> viewModel.updateTouchPrefs { it.copy(hideOnController = v) } }
                    SettingsSwitchItem("Menu Button", "On-screen button that opens the pause menu", touch.showMenuButton) { v ->
                        viewModel.updateTouchPrefs { it.copy(showMenuButton = v) }
                    }
                    SettingsSwitchItem("Fast-Forward Button", "On-screen fast-forward toggle", touch.showFastForwardButton) { v ->
                        viewModel.updateTouchPrefs { it.copy(showFastForwardButton = v) }
                    }
                    SettingsSwitchItem("Swap Hands", "Mirror the layout: D-pad on the right, buttons on the left", touch.swapHands) { v ->
                        viewModel.updateTouchPrefs { it.copy(swapHands = v) }
                    }
                }
            }
            item {
                SettingsCategory("Appearance") {
                    SettingsSliderItem("Opacity (landscape)", touch.opacity, 0.1f..1f) { v -> viewModel.updateTouchPrefs { it.copy(opacity = v) } }
                    SettingsSliderItem("Opacity (portrait)", touch.opacityPortrait, 0.1f..1f) { v ->
                        viewModel.updateTouchPrefs { it.copy(opacityPortrait = v) }
                    }
                    SettingsSliderItem("Size", touch.scale, 0.6f..1.6f) { v -> viewModel.updateTouchPrefs { it.copy(scale = v) } }
                    SettingsSwitchItem("Fade When Idle", "Dim the controls after 5 seconds without touches", touch.idleFade) { v ->
                        viewModel.updateTouchPrefs { it.copy(idleFade = v) }
                    }
                }
            }
            item {
                SettingsCategory("Feel") {
                    SettingsDropdownItem(
                        "Haptic Feedback", "Vibration when a control is pressed",
                        touch.haptics, HapticLevel.entries, { it.label },
                        { v -> viewModel.updateTouchPrefs { it.copy(haptics = v) } },
                    )
                    SettingsSwitchItem(
                        "Slide Between Buttons",
                        "Rolling a finger onto another button presses it (e.g. B to A)",
                        touch.slideBetweenButtons,
                    ) { v -> viewModel.updateTouchPrefs { it.copy(slideBetweenButtons = v) } }
                    SettingsSwitchItem(
                        "Auto-Hold",
                        "Hold a button for about a second to keep it pressed (e.g. accelerate); tap it again to release",
                        touch.autoHold,
                    ) { v -> viewModel.updateTouchPrefs { it.copy(autoHold = v) } }
                }
            }
            item {
                SettingsCategory("D-Pad") {
                    SettingsDropdownItem(
                        "Directions", "4-way ignores diagonals (some puzzle and RPG games prefer it)",
                        touch.dpadMode, DpadMode.entries, { it.label },
                        { v -> viewModel.updateTouchPrefs { it.copy(dpadMode = v) } },
                    )
                    SettingsSliderItem(
                        "Diagonal Zone", touch.dpadDiagonal, 0f..1f,
                        enabled = touch.dpadMode == DpadMode.EIGHT_WAY,
                    ) { v -> viewModel.updateTouchPrefs { it.copy(dpadDiagonal = v) } }
                }
            }
            item {
                SettingsCategory("Analog Stick") {
                    SettingsDropdownItem(
                        "Stick Position", "Floating centers the stick where your thumb lands",
                        touch.analogMode, AnalogMode.entries, { it.label },
                        { v -> viewModel.updateTouchPrefs { it.copy(analogMode = v) } },
                    )
                    SettingsSliderItem(
                        "Sensitivity", touch.analogSensitivity, 0.5f..2f,
                        format = { String.format(Locale.ROOT, "%.2fx", it) },
                    ) { v -> viewModel.updateTouchPrefs { it.copy(analogSensitivity = v) } }
                    SettingsSliderItem("Dead Zone", touch.analogDeadzone, 0f..0.5f) { v ->
                        viewModel.updateTouchPrefs { it.copy(analogDeadzone = v) }
                    }
                }
            }
            item {
                SettingsCategory("Layouts") {
                    Text(
                        "Customize where each control sits, per system. Landscape and portrait are saved separately; " +
                            "rotate the device while editing to switch.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                    )
                    TouchFamily.entries.forEach { family ->
                        SettingsClickableItem(family.displayName, "", onClick = { onEditLayout(family) })
                    }
                }
            }
            item {
                OutlinedButton(
                    onClick = {
                        TouchFamily.entries.forEach { family ->
                            viewModel.resetTouchLayout(family, landscape = true)
                            viewModel.resetTouchLayout(family, landscape = false)
                        }
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Reset All Layouts") }
            }
        }
    }
}

/** Layout editor opened from Settings (no game running): edits over a stand-in game picture. */
@Composable
fun TouchLayoutEditorScreen(viewModel: MainViewModel, familyKey: String, onBack: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val family = TouchFamily.entries.firstOrNull { it.key == familyKey } ?: TouchFamily.GENERIC
    val layout = remember(
        family, settings.touch.showMenuButton, settings.touch.showFastForwardButton, settings.ps1AnalogMode,
        settings.orientationVertical,
    ) {
        TouchLayouts.forFamily(
            family,
            TouchLayouts.Options(
                showMenu = settings.touch.showMenuButton,
                showFastForward = settings.touch.showFastForwardButton,
                ps1Analog = settings.ps1AnalogMode,
                wonderSwanVertical = settings.orientationVertical,
            ),
        )
    }
    BackHandler(onBack = onBack)
    TouchLayoutEditor(
        layout = layout,
        prefs = settings.touch,
        overridesFor = { landscape -> viewModel.touchLayoutOverrides(family, landscape) },
        onSave = { landscape, overrides ->
            viewModel.saveTouchLayout(family, landscape, overrides)
            onBack()
        },
        onCancel = onBack,
        showGamePlaceholder = true,
    )
}
