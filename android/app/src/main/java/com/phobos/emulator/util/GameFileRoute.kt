package com.phobos.emulator.util

/** Systems whose games are only read where they are: one that can't be read by path isn't started. */
val NO_COPY_SYSTEMS = setOf("PC Engine CD")

/** How a game's file reaches native code. */
sealed interface GameFileRoute {
    /** Read where it is, at [path]. */
    data class InPlace(val path: String) : GameFileRoute

    /** Copied into mia_temp from a descriptor, for a file with no path native code can read. */
    data object Copy : GameFileRoute

    /** Not started: the file has no readable path and its system's games aren't copied. */
    data object Refused : GameFileRoute
}

/** The route for a game of [system] whose file can be read at [readablePath], or null when it can't. */
fun gameFileRoute(system: String, readablePath: String?): GameFileRoute = when {
    readablePath != null -> GameFileRoute.InPlace(readablePath)
    system in NO_COPY_SYSTEMS -> GameFileRoute.Refused
    else -> GameFileRoute.Copy
}
