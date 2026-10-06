package com.phobos.emulator.ui

import android.text.format.DateUtils
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Description
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.BuildConfig
import com.phobos.emulator.R
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.neonBloom
import com.phobos.emulator.ui.theme.pillShape
import java.util.Locale

private val flavorName = BuildConfig.FLAVOR.replaceFirstChar { it.uppercase() }

@Composable
fun AboutScreen(viewModel: MainViewModel, onBack: () -> Unit, onOpenLicenses: () -> Unit) {
    val settings by viewModel.settings.collectAsState()
    val update by viewModel.appUpdate.collectAsState()
    PhobosScaffold(title = "About", onBack = onBack) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp)
                .padding(bottom = LocalDockInset.current),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            SettingsCard(accentText = true) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Image(
                        painter = painterResource(id = R.drawable.phobos_logo),
                        contentDescription = "Phobos Logo",
                        modifier = Modifier
                            .size(120.dp)
                            .then(if (LocalPhobosTheme.current.retrowave) Modifier.neonBloom(MaterialTheme.colorScheme.primary) else Modifier)
                            .padding(bottom = 8.dp)
                    )
                    Text(
                        text = "Phobos",
                        style = MaterialTheme.typography.headlineLarge,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = "Multi-system Emulator",
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.secondary
                    )
                    Spacer(modifier = Modifier.height(16.dp))
                    ValuePill("v${BuildConfig.VERSION_NAME}")
                    Spacer(modifier = Modifier.height(6.dp))
                    Text(
                        text = "$flavorName build ${BuildConfig.VERSION_CODE}",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.height(20.dp))
                    Text(
                        text = "Phobos is a multi-system emulator built for performance and accuracy.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                        lineHeight = 24.sp
                    )
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
            SettingsCategory("Updates") {
                UpdateStatus(update, onCheck = { viewModel.checkForAppUpdate(automatic = false) }, onInstall = viewModel::installAppUpdate)
                SettingsSwitchItem(
                    title = "Check automatically",
                    description = "At most once a day, when Phobos starts",
                    checked = settings.appUpdateAutoCheck,
                    onCheckedChange = { viewModel.setAppUpdateAutoCheck(it) },
                )
                SettingsSwitchItem(
                    title = "Nightly builds",
                    description = "Also offer the build of every change to Phobos, not only releases. Nightly builds are less tested.",
                    checked = settings.appUpdateNightly,
                    onCheckedChange = { viewModel.setAppUpdateNightly(it) },
                )
            }
            Spacer(modifier = Modifier.height(16.dp))
            SettingsCategory("Licenses") {
                SettingsClickableItem(
                    title = "Open-source licenses",
                    description = "Phobos's own code is under the GPL, version 3 or later. It's based on ares (© 2004–2025 ares team, Near et al., ISC license) and uses open-source components under their own licenses",
                    onClick = onOpenLicenses,
                    icon = Icons.Rounded.Description,
                )
            }
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "© 2026 Phobos Team",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

/** The update row: where the check or update stands, and the button that moves it on. */
@Composable
private fun UpdateStatus(state: AppUpdateState, onCheck: () -> Unit, onInstall: () -> Unit) {
    val (title, detail) = when (state) {
        AppUpdateState.Idle -> "Check for updates" to "GitHub releases of the $flavorName build"
        AppUpdateState.Checking -> "Checking for updates…" to null
        is AppUpdateState.UpToDate -> "Phobos is up to date" to "Checked ${checkedAgo(state.checkedAt)}"
        is AppUpdateState.Available -> {
            val kind = if (state.update.release.prerelease) "Nightly build" else "Release"
            "Phobos ${state.update.manifest.versionName} is available" to
                "$kind, ${String.format(Locale.US, "%.1f", state.update.apk.size / 1_000_000.0)} MB"
        }
        is AppUpdateState.Downloading -> "Downloading ${state.update.manifest.versionName}" to
            (if (state.progress >= 0f) "${(state.progress * 100).toInt()}%" else null)
        is AppUpdateState.Installing -> "Installing ${state.update.manifest.versionName}" to "Confirm the update in Android's installer"
        is AppUpdateState.Failed -> (if (state.update == null) "Couldn't check for updates" else "The update didn't finish") to state.message
    }
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = detail?.let { { Text(it) } },
        trailingContent = {
            when (state) {
                AppUpdateState.Checking, is AppUpdateState.Downloading, is AppUpdateState.Installing ->
                    CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
                is AppUpdateState.Available -> Button(onClick = onInstall, shape = pillShape()) { Text("Update") }
                is AppUpdateState.Failed -> Button(onClick = if (state.update != null) onInstall else onCheck, shape = pillShape()) { Text("Try again") }
                is AppUpdateState.UpToDate -> OutlinedButton(onClick = onCheck, shape = pillShape()) { Text("Check again") }
                AppUpdateState.Idle -> OutlinedButton(onClick = onCheck, shape = pillShape()) { Text("Check") }
            }
        },
        colors = transparentListItemColors(),
    )
    if (state is AppUpdateState.Downloading) {
        ProgressBar(
            progress = if (state.progress >= 0f) ({ state.progress }) else null,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
        )
    }
}

private fun checkedAgo(time: Long): String {
    val now = System.currentTimeMillis()
    return if (now - time < DateUtils.MINUTE_IN_MILLIS) "just now"
    else DateUtils.getRelativeTimeSpanString(time, now, DateUtils.MINUTE_IN_MILLIS).toString()
}
