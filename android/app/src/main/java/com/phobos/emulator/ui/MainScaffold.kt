package com.phobos.emulator.ui

import android.net.Uri
import android.widget.Toast
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.GridView
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Terminal
import androidx.compose.material.icons.rounded.GridView
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Terminal
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.navigation.NavBackStackEntry
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.RetrowaveBackdrop
import com.phobos.emulator.ui.theme.neonBar
import com.phobos.emulator.ui.theme.neonBloom
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.compose.*
import androidx.navigation.NavType
import androidx.navigation.navArgument

private const val TOUCH_EDITOR_ROUTE = "settings/touch-editor/{family}"
private const val EMULATOR_ROUTE = "emulator/{system}/{rom}"
private val TOP_LEVEL_ROUTES = setOf("library", "console", "settings")

/** Routes drawn edge to edge without the bottom navigation bar. */
private val FULL_SCREEN_ROUTES = setOf("system/{name}", EMULATOR_ROUTE, TOUCH_EDITOR_ROUTE)

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun MainScaffold(viewModel: MainViewModel) {
    val navController = rememberNavController()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination
    val context = LocalContext.current

    // Navigation requests from outside the NavHost (debug intent loader,
    // activity key fallback for the swap-screen hotkey).
    LaunchedEffect(Unit) {
        viewModel.navEvents.collect { route ->
            if (route == "library" || route == "console" || route == "settings") {
                navController.navigate(route) {
                    popUpTo(navController.graph.startDestinationId)
                    launchSingleTop = true
                }
            } else {
                navController.navigate(route)
            }
        }
    }

    // Check for newer GPU driver releases on launch and surface as a toast.
    LaunchedEffect(Unit) {
        viewModel.checkForDriverUpdates()
        viewModel.driverUpdateEvent.collect { msg ->
            Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
        }
    }

    val route = currentDestination?.route
    val retrowave = LocalPhobosTheme.current.retrowave
    // Faded rather than removed so leaving for the game doesn't pop mid-transition; nothing is
    // drawn behind the running game.
    val backdropAlpha by animateFloatAsState(
        targetValue = if (retrowave && route != EMULATOR_ROUTE && route != TOUCH_EDITOR_ROUTE) 1f else 0f,
        animationSpec = tween(500),
        label = "backdrop",
    )

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background)) {
        if (backdropAlpha > 0f) {
            RetrowaveBackdrop(Modifier.fillMaxSize().graphicsLayer { alpha = backdropAlpha })
        }
        Scaffold(
            containerColor = Color.Transparent,
            contentColor = MaterialTheme.colorScheme.onBackground,
            bottomBar = {
                if (route !in FULL_SCREEN_ROUTES) {
                    PhobosNavigationBar(
                        route = route,
                        retrowave = retrowave,
                        onNavigate = { target ->
                            navController.navigate(target) {
                                popUpTo(navController.graph.startDestinationId)
                                launchSingleTop = true
                            }
                        },
                    )
                }
            }
        ) { innerPadding ->
            NavHost(
                navController,
                startDestination = "library",
                modifier = Modifier.padding(innerPadding).consumeWindowInsets(innerPadding),
                enterTransition = { if (involvesEmulator()) LegacyEnter else if (betweenTabs()) TabEnter else fadeIn(tween(220, delayMillis = 60)) + slideInHorizontally(tween(300)) { it / 10 } },
                exitTransition = { if (involvesEmulator()) LegacyExit else if (betweenTabs()) TabExit else fadeOut(tween(160)) },
                popEnterTransition = { if (involvesEmulator()) LegacyEnter else fadeIn(tween(220, delayMillis = 60)) },
                popExitTransition = { if (involvesEmulator()) LegacyExit else fadeOut(tween(160)) + slideOutHorizontally(tween(300)) { it / 10 } },
            ) {
                composable("library") {
                    LibraryScreen(viewModel, onSystemClick = { name ->
                        navController.navigate("system/$name")
                    })
                }
                composable("console") { ConsoleScreen(viewModel) }
                composable("settings") {
                    SettingsScreen(
                        viewModel = viewModel,
                        onNavigateToAppearance = { navController.navigate("settings/appearance") },
                        onNavigateToEmulation = { navController.navigate("settings/emulation") },
                        onNavigateToVideo = { navController.navigate("settings/video") },
                        onNavigateToN64Experimental = { navController.navigate("settings/n64-experimental") },
                        onNavigateToAudio = { navController.navigate("settings/audio") },
                        onNavigateToPerformance = { navController.navigate("settings/performance") },
                        onNavigateToInputs = { navController.navigate("settings/inputs") },
                        onNavigateToPaths = { navController.navigate("settings/paths") },
                        onNavigateToVisibility = { navController.navigate("settings/visibility") },
                        onNavigateToAbout = { navController.navigate("settings/about") }
                    )
                }
                composable("settings/appearance") {
                    AppearanceSettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/emulation") {
                    EmulationSettingsScreen(
                        viewModel = viewModel,
                        onBack = { navController.popBackStack() },
                        onNavigateToFirmware = { navController.navigate("settings/firmware") },
                        onNavigateToDrivers = { navController.navigate("settings/drivers") }
                    )
                }
                composable("settings/video") {
                    VideoSettingsScreen(
                        viewModel = viewModel,
                        onBack = { navController.popBackStack() },
                        onNavigateToShaders = { navController.navigate("settings/shaders") }
                    )
                }
                composable("settings/n64-experimental") {
                    N64ExperimentalSettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/audio") {
                    AudioSettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/performance") {
                    PerformanceMonitorSettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/inputs") {
                    InputsSettingsScreen(
                        viewModel = viewModel,
                        onBack = { navController.popBackStack() },
                        onNavigateToInputs = { navController.navigate("settings/input-mapping") },
                        onNavigateToHotkeys = { navController.navigate("settings/hotkeys") },
                        onNavigateToTouch = { navController.navigate("settings/touch") }
                    )
                }
                composable("settings/touch") {
                    TouchSettingsScreen(
                        viewModel = viewModel,
                        onBack = { navController.popBackStack() },
                        onEditLayout = { family -> navController.navigate("settings/touch-editor/${family.key}") }
                    )
                }
                composable(
                    route = TOUCH_EDITOR_ROUTE,
                    arguments = listOf(navArgument("family") { type = NavType.StringType })
                ) { backStackEntry ->
                    TouchLayoutEditorScreen(
                        viewModel = viewModel,
                        familyKey = backStackEntry.arguments?.getString("family") ?: "",
                        onBack = { navController.popBackStack() }
                    )
                }
                composable("settings/visibility") {
                    VisibilitySettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/firmware") {
                    FirmwareSettingsScreen(viewModel, onBack = { navController.popBackStack() })
                }
                composable("settings/input-mapping") {
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
}

// navigation-compose's default transitions, kept for the emulator route so its SurfaceView timing is unchanged.
private val LegacyEnter: EnterTransition = fadeIn(animationSpec = tween(700))
private val LegacyExit: ExitTransition = fadeOut(animationSpec = tween(700))
private val TabEnter: EnterTransition = fadeIn(tween(200))
private val TabExit: ExitTransition = fadeOut(tween(120))

private fun AnimatedContentTransitionScope<NavBackStackEntry>.involvesEmulator() =
    initialState.destination.route == EMULATOR_ROUTE || targetState.destination.route == EMULATOR_ROUTE

private fun AnimatedContentTransitionScope<NavBackStackEntry>.betweenTabs() =
    initialState.destination.route in TOP_LEVEL_ROUTES && targetState.destination.route in TOP_LEVEL_ROUTES

private class NavTab(val route: String, val label: String, val selectedIcon: ImageVector, val icon: ImageVector)

private val NAV_TABS = listOf(
    NavTab("library", "Library", Icons.Rounded.GridView, Icons.Outlined.GridView),
    NavTab("console", "Console", Icons.Rounded.Terminal, Icons.Outlined.Terminal),
    NavTab("settings", "Settings", Icons.Rounded.Settings, Icons.Outlined.Settings),
)

@Composable
private fun PhobosNavigationBar(route: String?, retrowave: Boolean, onNavigate: (String) -> Unit) {
    val scheme = MaterialTheme.colorScheme
    NavigationBar(
        containerColor = if (retrowave) scheme.surfaceContainer.copy(alpha = 0.92f) else scheme.surfaceContainer,
        tonalElevation = 0.dp,
        modifier = if (retrowave) Modifier.neonBar(scheme, lineAtBottom = false) else Modifier,
    ) {
        NAV_TABS.forEach { tab ->
            val selected = route == tab.route
            NavigationBarItem(
                selected = selected,
                onClick = { onNavigate(tab.route) },
                icon = {
                    Icon(
                        if (selected) tab.selectedIcon else tab.icon,
                        contentDescription = tab.label,
                        modifier = if (selected && retrowave) Modifier.neonBloom(scheme.primary) else Modifier,
                    )
                },
                label = { Text(tab.label) },
            )
        }
    }
}
