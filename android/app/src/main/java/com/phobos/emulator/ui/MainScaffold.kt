package com.phobos.emulator.ui

import android.app.Activity
import android.net.Uri
import android.os.Build
import android.widget.Toast
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.GridView
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Terminal
import androidx.compose.material.icons.rounded.GridView
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Terminal
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.rememberGraphicsLayer
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.navigation.NavBackStackEntry
import com.phobos.emulator.data.GlassEffects
import com.phobos.emulator.ui.theme.GlassBackdrop
import com.phobos.emulator.ui.theme.GlassCapture
import com.phobos.emulator.ui.theme.LocalGlassCapture
import com.phobos.emulator.ui.theme.LocalPhobosTheme
import com.phobos.emulator.ui.theme.RetrowaveBackdrop
import com.phobos.emulator.ui.theme.glassPanel
import com.phobos.emulator.ui.theme.recordForGlass
import com.phobos.emulator.ui.theme.neonBloom
import com.phobos.emulator.ui.theme.neonGlow
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.DialogProperties
import androidx.navigation.compose.*
import androidx.navigation.NavType
import androidx.navigation.navArgument
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map

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
        // Requests queued before this composition can arrive before the NavHost sets its graph.
        navController.currentBackStackEntryFlow.first()
        viewModel.navEvents.collect { route ->
            if (route == "library" || route == "console" || route == "settings") {
                navController.navigate(route) {
                    popUpTo(navController.graph.startDestinationId)
                    launchSingleTop = true
                }
            } else if (route.startsWith("emulator/")) {
                // One game screen at a time: a game a frontend starts over the running one replaces its screen.
                navController.navigate(route) { popUpTo(EMULATOR_ROUTE) { inclusive = true } }
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
    // And for a newer Phobos (at most once a day, when allowed).
    LaunchedEffect(Unit) {
        viewModel.checkForAppUpdate(automatic = true)
        viewModel.appUpdateEvent.collect { msg ->
            Toast.makeText(context, msg, Toast.LENGTH_LONG).show()
        }
    }

    // L1/R1 outside the game (MainActivity): the dock tab before or after the current page's
    // section, wrapping around; the game screen and its pause menu keep them.
    LaunchedEffect(Unit) {
        viewModel.tabSteps.collect { step ->
            val current = navController.currentDestination?.route ?: return@collect
            if (current.startsWith("emulator/")) return@collect
            val section = NAV_TABS.indexOfFirst { current == it.route || current.startsWith(it.route + "/") }
                .takeIf { it >= 0 } ?: NAV_TABS.indexOfFirst { it.route == "library" }
            val target = NAV_TABS[(section + step).mod(NAV_TABS.size)].route
            navController.navigate(target) {
                popUpTo(navController.graph.startDestinationId)
                launchSingleTop = true
            }
        }
    }

    val route = currentDestination?.route
    val theme = LocalPhobosTheme.current
    val retrowave = theme.retrowave
    val style = theme.style

    // Full Screen Mode also hides the status bar in the menus (a swipe down shows it for a moment). The game
    // screen hides both bars and, when it closes, restores this state.
    val fullScreen by remember { viewModel.settings.map { it.fullScreenMode }.distinctUntilChanged() }
        .collectAsState(initial = viewModel.settings.value.fullScreenMode)
    val view = LocalView.current
    DisposableEffect(fullScreen) {
        (view.context as? Activity)?.window?.let { window ->
            val controller = WindowCompat.getInsetsController(window, view)
            controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            if (fullScreen) controller.hide(WindowInsetsCompat.Type.statusBars()) else controller.show(WindowInsetsCompat.Type.statusBars())
        }
        onDispose {}
    }
    // Faded rather than removed so leaving for the game doesn't pop mid-transition; nothing is
    // drawn behind the running game.
    val backdropFade = animateFloatAsState(
        targetValue = if (route != EMULATOR_ROUTE && route != TOUCH_EDITOR_ROUTE) 1f else 0f,
        animationSpec = tween(500),
        label = "backdrop",
    )
    val backdropAlpha by backdropFade

    // The dock refracts what's behind it: the backdrop and the pages, recorded only while its lens can be on.
    val lens = theme.solid == null && theme.glass.refraction > 0f && Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
    val backdropLayer = rememberGraphicsLayer()
    val pagesLayer = rememberGraphicsLayer()
    val glassCapture = remember(backdropLayer, pagesLayer) {
        GlassCapture(backdropLayer, pagesLayer).also { it.backdropAlpha = { backdropFade.value } }
    }

    Box(Modifier.fillMaxSize().background(MaterialTheme.colorScheme.background).onGloballyPositioned { glassCapture.root = it }) {
        if (backdropAlpha > 0f) {
            val backdrop = Modifier.fillMaxSize()
                .then(if (lens) Modifier.recordForGlass(backdropLayer) { glassCapture.onRecorded?.invoke() } else Modifier)
                .graphicsLayer { alpha = backdropAlpha }
            if (style.ownBackdrop) style.Backdrop(backdrop)
            else if (retrowave) RetrowaveBackdrop(backdrop)
            else if (theme.glass.level != GlassEffects.OFF) GlassBackdrop(backdrop)
        }
        CompositionLocalProvider(LocalGlassCapture provides glassCapture) {
            Scaffold(
                containerColor = Color.Transparent,
                contentColor = MaterialTheme.colorScheme.onBackground,
                bottomBar = {
                    if (route !in FULL_SCREEN_ROUTES) {
                        PhobosDock(route, retrowave) { target ->
                            navController.navigate(target) {
                                popUpTo(navController.graph.startDestinationId)
                                launchSingleTop = true
                            }
                        }
                    }
                }
            ) { innerPadding ->
                // Pages with the dock run under it to the bottom of the screen; LocalDockInset tells
                // them how far to pad their ends instead.
                val docked = route !in FULL_SCREEN_ROUTES
                val direction = LocalLayoutDirection.current
                val pagePadding = if (!docked) innerPadding else PaddingValues(
                    start = innerPadding.calculateStartPadding(direction),
                    top = innerPadding.calculateTopPadding(),
                    end = innerPadding.calculateEndPadding(direction),
                )
                CompositionLocalProvider(LocalDockInset provides if (docked) innerPadding.calculateBottomPadding() else 0.dp) {
                    NavHost(
                        navController,
                        startDestination = "library",
                        // Only docked pages are recorded for the dock to refract; the game's surface never goes through a layer.
                        modifier = Modifier.padding(pagePadding).consumeWindowInsets(innerPadding)
                            .onGloballyPositioned { coordinates ->
                                glassCapture.root?.takeIf { it.isAttached }?.let { glassCapture.pagesOrigin = it.localPositionOf(coordinates, Offset.Zero) }
                            }
                            .then(if (docked && lens) Modifier.recordForGlass(pagesLayer) { glassCapture.onRecorded?.invoke() } else Modifier),
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
                            AboutScreen(viewModel, onBack = { navController.popBackStack() })
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
    }

    val launchChoice by viewModel.launchChoice.collectAsState()
    val inGame by viewModel.emulatorScreenVisible.collectAsState()
    launchChoice?.let { choice ->
        LaunchSystemDialog(choice, fullScreen, inGame, onChoose = viewModel::chooseLaunchSystem, onCancel = viewModel::cancelLaunchChoice)
    }
}

/** Asks which system a game a frontend started is for, when neither the frontend nor the file says. */
@Composable
private fun LaunchSystemDialog(
    choice: LaunchChoice,
    fullScreen: Boolean,
    inGame: Boolean,
    onChoose: (String) -> Unit,
    onCancel: () -> Unit,
) {
    PhobosAlertDialog(
        onDismissRequest = onCancel,
        // Material's width range (up to 560 dp) rather than the platform's narrower one, for more columns.
        properties = DialogProperties(usePlatformDefaultWidth = false),
        modifier = Modifier.padding(horizontal = 24.dp),
        title = { DialogSystemBars(fullScreen, inGame); Text("Choose a system") },
        text = {
            Column {
                Text(choice.rom.name, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(8.dp))
                LazyVerticalGrid(columns = GridCells.Adaptive(minSize = 160.dp), modifier = Modifier.heightIn(max = 280.dp)) {
                    items(choice.candidates) { system ->
                        TextButton(onClick = { onChoose(system) }) {
                            Text(system, modifier = Modifier.fillMaxWidth())
                        }
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = onCancel) { Text("Cancel") } },
    )
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

/**
 * Floating dock for the top-level tabs, over the pages, which scroll under it. Custom items rather
 * than Material's NavigationBarItem, whose layout assumes the 80 dp NavigationBar height. Glass at
 * the Full and Subtle levels; with glass effects off, solid with the cards' faint outline.
 */
@Composable
private fun PhobosDock(route: String?, retrowave: Boolean, onNavigate: (String) -> Unit) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val glassOff = theme.glass.level == GlassEffects.OFF
    val solid = theme.solid
    val shape = solid?.dockShape ?: RoundedCornerShape(28.dp)
    val panel = if (solid != null) {
        solid.panel(shape, scheme.surfaceContainer)
    } else {
        Modifier
            .then(if (retrowave) Modifier.neonGlow(scheme.primary, shape, intensity = 0.45f) else Modifier)
            .glassPanel(
                shape, scheme.surfaceContainer, theme.glass.dockAlpha, theme.glass, theme.isDark, rim = !retrowave, shadow = !retrowave,
                outline = if (glassOff && !retrowave) scheme.outlineVariant.copy(alpha = 0.45f) else Color.Unspecified,
                overPages = true,
            )
    }
    Box(
        Modifier
            .fillMaxWidth()
            .windowInsetsPadding(WindowInsets.navigationBars)
            .padding(horizontal = 24.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            modifier = Modifier
                .widthIn(max = 440.dp)
                .fillMaxWidth()
                .then(panel)
                .selectableGroup()
                .padding(horizontal = 6.dp, vertical = 6.dp),
        ) {
            NAV_TABS.forEach { tab ->
                DockItem(tab, selected = route == tab.route, retrowave, onClick = { onNavigate(tab.route) }, Modifier.weight(1f))
            }
        }
    }
}

/** A dock tab: the selected one's icon sits on a translucent primary pill that springs open. */
@Composable
private fun DockItem(tab: NavTab, selected: Boolean, retrowave: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier) {
    val scheme = MaterialTheme.colorScheme
    val theme = LocalPhobosTheme.current
    val solid = theme.solid
    val pillAlpha = if (solid != null) 1f else theme.glass.indicatorAlpha
    val selection by animateFloatAsState(
        targetValue = if (selected) 1f else 0f,
        animationSpec = spring(dampingRatio = 0.45f, stiffness = Spring.StiffnessMediumLow),
        label = "dockSelection",
    )
    val iconColor by animateColorAsState(
        if (selected) solid?.dockIconColor() ?: scheme.primary else scheme.onSurfaceVariant,
        label = "dockIcon",
    )
    val tabShape = solid?.dockTabShape ?: RoundedCornerShape(22.dp)
    Column(
        modifier = modifier
            .clip(tabShape)
            .focusRing(scheme.primary, tabShape)
            .selectable(selected = selected, onClick = onClick, role = Role.Tab)
            .padding(vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(
            Modifier
                .size(width = 56.dp, height = 30.dp)
                .drawBehind {
                    // The spring overshoots; the pill grows past its width but never past its verified alpha.
                    val width = size.width * (0.4f + 0.6f * selection)
                    val alpha = pillAlpha * selection.coerceIn(0f, 1f)
                    if (solid != null) {
                        translate(left = (size.width - width) / 2f) { with(solid) { drawDockIndicator(Size(width, size.height), alpha, scheme) } }
                    } else {
                        drawRoundRect(
                            color = scheme.primary,
                            topLeft = Offset((size.width - width) / 2f, 0f),
                            size = Size(width, size.height),
                            cornerRadius = CornerRadius(size.height / 2f),
                            alpha = alpha,
                        )
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                if (selected) tab.selectedIcon else tab.icon,
                contentDescription = null,
                tint = iconColor,
                modifier = if (selected && retrowave) Modifier.neonBloom(scheme.primary) else Modifier,
            )
        }
        Spacer(Modifier.height(2.dp))
        Text(
            tab.label,
            style = MaterialTheme.typography.labelMedium,
            color = if (selected) scheme.onSurface else scheme.onSurfaceVariant,
            maxLines = 1,
        )
    }
}
