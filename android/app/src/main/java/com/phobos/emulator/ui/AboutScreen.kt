package com.phobos.emulator.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.phobos.emulator.R

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AboutScreen(onBack: () -> Unit) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("About") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        }
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Image(
                painter = painterResource(id = R.drawable.phobos_logo),
                contentDescription = "Phobos Logo",
                modifier = Modifier
                    .size(120.dp)
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
            Spacer(modifier = Modifier.height(24.dp))
            Text(
                text = "Version 1.0",
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(modifier = Modifier.height(32.dp))
            Text(
                text = "Phobos is a multi-system emulator built for performance and accuracy.",
                style = MaterialTheme.typography.bodyMedium,
                lineHeight = 24.sp
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = "© 2026 Phobos Team",
                style = MaterialTheme.typography.labelSmall
            )
        }
    }
}
