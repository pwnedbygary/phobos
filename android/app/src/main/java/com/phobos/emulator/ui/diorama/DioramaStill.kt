package com.phobos.emulator.ui.diorama

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import java.security.MessageDigest

/**
 * The Library's cached still of the diorama, for pages that are not settled (and while flipping): the same framing
 * as [DioramaStage], rendered once offscreen through the shared carts engine and served from a disk cache keyed on
 * the GLB digest + pixel size. A miss draws nothing; the page's 2D fallback stays visible until the still is ready.
 */
@Composable
internal fun DioramaStill(systemName: String, modifier: Modifier = Modifier) {
    // Real offscreen still rendering lands with the Library page wiring; until then the page's fallback shows.
}

/** The pure cache identity: a different GLB or a different pixel size must not reuse the old picture. */
internal object DioramaStillCache {
    const val RENDERER_VERSION = "1"

    fun key(glbSha256: String, width: Int, height: Int): String {
        val md = MessageDigest.getInstance("SHA-256")
        val joined = listOf(glbSha256, width, height, RENDERER_VERSION).joinToString("\u0000")
        return md.digest(joined.toByteArray()).joinToString("") { "%02x".format(it) }
    }
}
