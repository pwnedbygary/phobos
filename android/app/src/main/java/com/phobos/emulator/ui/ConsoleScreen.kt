package com.phobos.emulator.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.LogEntry
import com.phobos.emulator.LogLevel
import com.phobos.emulator.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConsoleScreen(viewModel: MainViewModel) {
    val logs by viewModel.logs.collectAsState()
    val listState = rememberLazyListState()
    var autoScroll by remember { mutableStateOf(true) }
    val context = LocalContext.current

    LaunchedEffect(logs.size) {
        if (autoScroll && logs.isNotEmpty()) {
            listState.animateScrollToItem(logs.size - 1)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Log Console") },
                actions = {
                    IconButton(onClick = { autoScroll = !autoScroll }) {
                        Icon(
                            imageVector = Icons.Default.KeyboardArrowDown,
                            contentDescription = "Toggle Auto-scroll",
                            tint = if (autoScroll) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                        )
                    }
                    IconButton(onClick = { viewModel.exportLogs(context) }) {
                        Icon(painterResource(R.drawable.ic_share), contentDescription = "Share Logs")
                    }
                    IconButton(onClick = { viewModel.clearLogs() }) {
                        Icon(Icons.Default.Clear, contentDescription = "Clear Logs")
                    }
                }
            )
        }
    ) { innerPadding ->
        LazyColumn(
            state = listState,
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize()
                .background(Color(0xFF1E1E1E))
                .padding(8.dp)
        ) {
            items(logs) { entry ->
                LogEntryItem(entry)
            }
        }
    }
}

@Composable
fun LogEntryItem(entry: LogEntry) {
    val color = when (entry.level) {
        LogLevel.TRACE.ordinal -> Color.Gray
        LogLevel.DEBUG.ordinal -> Color.Cyan
        LogLevel.INFO.ordinal -> Color.Green
        LogLevel.WARN.ordinal -> Color.Yellow
        LogLevel.ERROR.ordinal -> Color.Red
        LogLevel.FATAL.ordinal -> Color.Magenta
        else -> Color.White
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
            style = MaterialTheme.typography.bodySmall.copy(
                fontFamily = FontFamily.Monospace,
                fontSize = 10.sp
            ),
            modifier = Modifier.width(60.dp)
        )
        Text(
            text = entry.message,
            color = Color.White,
            style = MaterialTheme.typography.bodySmall.copy(
                fontFamily = FontFamily.Monospace,
                fontSize = 12.sp
            )
        )
    }
}
