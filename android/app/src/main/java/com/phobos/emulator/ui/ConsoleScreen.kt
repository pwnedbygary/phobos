package com.phobos.emulator.ui

import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.rounded.VerticalAlignBottom
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.LogEntry
import com.phobos.emulator.LogLevel
import com.phobos.emulator.R
import com.phobos.emulator.ui.theme.LegibleIcon
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import kotlinx.coroutines.launch

@Composable
fun ConsoleScreen(viewModel: MainViewModel) {
    val logs by viewModel.logs.collectAsState()
    val listState = rememberLazyListState()
    // Follows new lines until the user scrolls up; scrolling back to the end, or the button, resumes it.
    var follow by remember { mutableStateOf(true) }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val scheme = MaterialTheme.colorScheme

    // Keyed on the list, which each update replaces: once it holds its 2,000 lines, its size stays put.
    LaunchedEffect(logs) {
        if (follow && logs.isNotEmpty()) listState.scrollToItem(logs.lastIndex)
    }
    LaunchedEffect(listState) {
        listState.interactionSource.interactions.collect { if (it is DragInteraction.Start) follow = false }
    }
    LaunchedEffect(listState) {
        snapshotFlow { !listState.isScrollInProgress && !listState.canScrollForward }.collect { atEnd -> if (atEnd) follow = true }
    }

    PhobosScaffold(
        title = "Log Console",
        actions = {
            IconToggleButton(
                checked = follow,
                onCheckedChange = { on ->
                    follow = on
                    if (on && logs.isNotEmpty()) scope.launch { listState.scrollToItem(logs.lastIndex) }
                },
            ) {
                LegibleIcon(
                    imageVector = Icons.Rounded.VerticalAlignBottom,
                    contentDescription = "Follow new lines",
                )
            }
            IconButton(onClick = { viewModel.exportLogs(context) }) {
                LegibleIcon(painterResource(R.drawable.ic_share), contentDescription = "Share Logs")
            }
            IconButton(onClick = { viewModel.clearLogs() }) {
                LegibleIcon(Icons.Default.Clear, contentDescription = "Clear Logs")
            }
        },
    ) { innerPadding ->
        ThemedCard(
            modifier = Modifier
                .padding(innerPadding)
                .padding(start = 12.dp, end = 12.dp, bottom = 12.dp + LocalDockInset.current)
                .fillMaxSize(),
            accentText = true,
        ) {
            if (logs.isEmpty()) {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Text("No log entries yet", style = MaterialTheme.typography.bodyMedium, color = scheme.onSurfaceVariant)
                }
            } else {
                LazyColumn(state = listState, contentPadding = PaddingValues(12.dp)) {
                    items(logs) { entry ->
                        LogEntryItem(entry)
                    }
                }
            }
        }
    }
}

@Composable
fun LogEntryItem(entry: LogEntry) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val color = when (entry.level) {
        LogLevel.TRACE.ordinal -> scheme.onSurfaceVariant
        LogLevel.DEBUG.ordinal -> scheme.secondary
        LogLevel.INFO.ordinal -> theme.success
        LogLevel.WARN.ordinal -> theme.warning
        LogLevel.ERROR.ordinal, LogLevel.FATAL.ordinal -> scheme.error
        else -> scheme.onSurface
    }

    val tag = when (entry.level) {
        LogLevel.TRACE.ordinal -> "TRACE"
        LogLevel.DEBUG.ordinal -> "DEBUG"
        LogLevel.INFO.ordinal -> "INFO"
        LogLevel.WARN.ordinal -> "WARN"
        LogLevel.ERROR.ordinal -> "ERROR"
        LogLevel.FATAL.ordinal -> "FATAL"
        else -> "LOG"
    }

    Row(modifier = Modifier.padding(vertical = 2.dp)) {
        Text(
            text = "[$tag]",
            color = color,
            fontWeight = if (entry.level == LogLevel.FATAL.ordinal) FontWeight.Bold else null,
            style = MaterialTheme.typography.bodySmall.copy(
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp
            ),
            modifier = Modifier.width(60.dp)
        )
        Text(
            text = entry.message,
            color = scheme.onSurface,
            style = MaterialTheme.typography.bodySmall.copy(
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp
            )
        )
    }
}
