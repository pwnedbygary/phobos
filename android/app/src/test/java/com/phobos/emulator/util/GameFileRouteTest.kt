package com.phobos.emulator.util

import org.junit.Assert.assertEquals
import org.junit.Test

class GameFileRouteTest {
    @Test
    fun everySystemLoadsInPlaceWhenThePathCanBeRead() {
        for (system in listOf("Famicom", "Nintendo 64", "Neo Geo", "Arcade", "PlayStation", "PC Engine CD")) {
            assertEquals(
                GameFileRoute.InPlace("/storage/EBFF-F6C0/ROMs/game.bin"),
                gameFileRoute(system, "/storage/EBFF-F6C0/ROMs/game.bin"),
            )
        }
    }

    @Test
    fun aFileWithoutAPathIsCopied() {
        assertEquals(GameFileRoute.Copy, gameFileRoute("Super Famicom", null))
        assertEquals(GameFileRoute.Copy, gameFileRoute("PlayStation", null))
    }

    @Test
    fun cdAndLaserdiscImagesWithoutAPathAreNotCopied() {
        for (system in listOf("PC Engine CD", "Mega LD", "PC Engine LD")) {
            assertEquals(GameFileRoute.Refused, gameFileRoute(system, null))
        }
        assertEquals(GameFileRoute.InPlace("/sdcard/game.mmi"), gameFileRoute("Mega LD", "/sdcard/game.mmi"))
    }
}
