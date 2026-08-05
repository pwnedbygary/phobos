package com.phobos.emulator.ui

import android.net.Uri
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.navigation.compose.*
import androidx.navigation.NavType
import androidx.navigation.navArgument

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScaffold(viewModel: MainViewModel) {
    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination

    Scaffold(
        bottomBar = {
            if (currentDestination?.route != "system/{name}" && currentDestination?.route != "emulator/{system}/{rom}") {
                NavigationBar {
                    NavigationBarItem(
                        icon = { Icon(Icons.Default.Home, contentDescription = "Library") },
                        label = { Text("Library") },
                        selected = currentDestination?.route == "library",
                        onClick = { 
                            navController.navigate("library") {
                                popUpTo(navController.graph.startDestinationId)
                                launchSingleTop = true
                            }
                        }
                    )
                    NavigationBarItem(
                        icon = { Icon(Icons.Default.List, contentDescription = "Console") },
                        label = { Text("Console") },
                        selected = currentDestination?.route == "console",
                        onClick = {
                            navController.navigate("console") {
                                popUpTo(navController.graph.startDestinationId)
                                launchSingleTop = true
                            }
                        }
                    )
                    NavigationBarItem(
                        icon = { Icon(Icons.Default.Settings, contentDescription = "Settings") },
                        label = { Text("Settings") },
                        selected = currentDestination?.route == "settings",
                        onClick = { 
                            navController.navigate("settings") {
                                popUpTo(navController.graph.startDestinationId)
                                launchSingleTop = true
                            }
                        }
                    )
                }
            }
        }
    ) { innerPadding ->
        NavHost(navController, startDestination = "library", modifier = Modifier.padding(innerPadding)) {
            composable("library") { 
                LibraryScreen(viewModel, onSystemClick = { name ->
                    navController.navigate("system/$name")
                }) 
            }
            composable("console") { ConsoleScreen(viewModel) }
            composable("settings") { 
                SettingsScreen(
                    viewModel = viewModel,
                    onNavigateToVisibility = { navController.navigate("settings/visibility") },
                    onNavigateToFirmware = { navController.navigate("settings/firmware") },
                    onNavigateToInputs = { navController.navigate("settings/inputs") },
                    onNavigateToHotkeys = { navController.navigate("settings/hotkeys") },
                    onNavigateToShaders = { navController.navigate("settings/shaders") },
                    onNavigateToPaths = { navController.navigate("settings/paths") },
                    onNavigateToDrivers = { navController.navigate("settings/drivers") },
                    onNavigateToAbout = { navController.navigate("settings/about") }
                ) 
            }
            composable("settings/visibility") {
                VisibilitySettingsScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/firmware") {
                FirmwareSettingsScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/inputs") {
                InputMappingScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/hotkeys") {
                HotkeyMappingScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/shaders") {
                ShaderSettingsScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/paths") {
                PathSettingsScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/drivers") {
                DriverManagerScreen(viewModel, onBack = { navController.popBackStack() })
            }
            composable("settings/about") {
                AboutScreen(onBack = { navController.popBackStack() })
            }
            composable(
                route = "system/{name}",
                arguments = listOf(navArgument("name") { type = NavType.StringType })
            ) { backStackEntry ->
                val systemName = backStackEntry.arguments?.getString("name") ?: ""
                SystemDetailScreen(
                    encodedSystemName = systemName,
                    viewModel = viewModel,
                    onBack = { navController.popBackStack() },
                    onRomClick = { system, rom ->
                        navController.navigate("emulator/$system/$rom")
                    }
                )
            }
            composable(
                route = "emulator/{system}/{rom}",
                arguments = listOf(
                    navArgument("system") { type = NavType.StringType },
                    navArgument("rom") { type = NavType.StringType }
                )
            ) { backStackEntry ->
                val system = backStackEntry.arguments?.getString("system") ?: ""
                val rom = backStackEntry.arguments?.getString("rom") ?: ""
                EmulatorScreen(
                    viewModel = viewModel,
                    systemName = Uri.decode(system),
                    romName = Uri.decode(rom),
                    onBack = { navController.popBackStack() }
                )
            }
        }
    }
}
