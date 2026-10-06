package com.phobos.emulator.ui

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** One notice from the bundled LICENSE: what it covers, from its first line, and the rest of it. */
internal data class LicenseNotice(val title: String, val text: String)

private val NOTICE_RULE = "-".repeat(70)

/** The notices in [license], the repository's LICENSE, whose blocks sit between rules of 70 dashes. */
internal fun parseLicenseNotices(license: String): List<LicenseNotice> {
    val blocks = mutableListOf<List<String>>()
    var block = mutableListOf<String>()
    for (line in license.lines()) {
        if (line == NOTICE_RULE) {
            if (block.any { it.isNotBlank() }) blocks += block
            block = mutableListOf()
        } else {
            block += line
        }
    }
    if (block.any { it.isNotBlank() }) blocks += block
    return blocks.map { lines ->
        val first = lines.indexOfFirst { it.isNotBlank() }
        val body = lines.drop(first + 1).dropWhile { it.isBlank() }.dropLastWhile { it.isBlank() }
        LicenseNotice(lines[first].trim(), body.joinToString("\n"))
    }
}

/** Settings → About → Open-source licenses: Phobos's notice, those of ares and every component it uses, and the GPL's text, from the LICENSE in the APK. */
@Composable
fun LicensesScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val notices by produceState<List<LicenseNotice>?>(initialValue = null) {
        value = withContext(Dispatchers.IO) {
            runCatching { context.assets.open("licenses/LICENSE.txt").bufferedReader().use { it.readText() } }
                .map(::parseLicenseNotices)
                .getOrDefault(emptyList())
        }
    }
    PhobosScaffold(title = "Open-source licenses", onBack = onBack) { innerPadding ->
        LazyColumn(
            modifier = Modifier.padding(innerPadding).fillMaxSize(),
            contentPadding = PaddingValues(start = 16.dp, top = 16.dp, end = 16.dp, bottom = 16.dp + LocalDockInset.current),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            val loaded = notices ?: return@LazyColumn
            item {
                Text(
                    text = if (loaded.isEmpty()) {
                        "This build is missing its license notices. They're in the LICENSE file of Phobos's source code, at github.com/pwnedbygary/phobos."
                    } else {
                        "Phobos's own code is under the GNU General Public License, version 3 or later. It's based on ares and uses the components below, each under the license shown. Its source code is at github.com/pwnedbygary/phobos."
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 4.dp),
                )
            }
            items(loaded) { notice -> LicenseNoticeCard(notice) }
        }
    }
}

@Composable
private fun LicenseNoticeCard(notice: LicenseNotice) {
    var open by rememberSaveable(notice.title) { mutableStateOf(false) }
    SettingsCard(Modifier.animateContentSize()) {
        ListItem(
            headlineContent = { Text(notice.title) },
            trailingContent = {
                Icon(
                    if (open) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = if (open) "Close" else "Read",
                )
            },
            colors = transparentListItemColors(),
            modifier = Modifier.fillMaxWidth().focusRing(MaterialTheme.colorScheme.primary).clickable { open = !open },
        )
        if (open) {
            SelectionContainer {
                Text(
                    text = notice.text,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 16.dp, end = 16.dp, bottom = 12.dp),
                )
            }
        }
    }
}
