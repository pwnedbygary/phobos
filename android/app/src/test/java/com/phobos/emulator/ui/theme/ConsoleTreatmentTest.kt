package com.phobos.emulator.ui.theme

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import com.phobos.emulator.data.CrtBackdropScene
import com.phobos.emulator.data.UiEffects
import com.phobos.emulator.launch.LaunchSystems
import com.phobos.emulator.ui.CONSOLE_ART
import com.phobos.emulator.ui.consoleArtFor
import com.phobos.emulator.ui.systemIconSlug
import com.phobos.emulator.ui.systemIconSlugs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class ConsoleTreatmentTest {
    private val svgs: Map<String, String> = CONSOLE_ART.associateWith { File("src/main/assets/platforms/$it.svg").readText() }

    private fun colors(id: String): ConsoleColors {
        val theme = ThemeRegistry.all.first { it.id == id }
        val c = theme.colors(theme.isDark)
        val tile = libraryTileFill(c.scheme, theme.isDark).compositeOver(c.scheme.background)
        return ConsoleColors(ConsoleArtPalette(c.scheme, c.success, c.warning, tile), c.scheme, theme.isDark, tile, crtBloomColor(c.scheme, CrtBackdropScene.GREEN))
    }

    private val themes = listOf("gruvbox_dark", "synthwave_84", "dracula", "catppuccin_latte", "github_light").map(::colors)

    /** A stand-in for the SVG drawn at a tile's size: the picture's cells, six pixels each, in its own colors. */
    private fun raster(picture: ConsolePixels.Picture): Triple<IntArray, Int, Int> {
        val w = picture.width * 6
        val h = picture.height * 6
        return Triple(IntArray(w * h) { i -> picture.at(i % w / 6, i / w / 6).let { if (it < 0) 0 else picture.palette[it] } }, w, h)
    }

    @Test fun matchStyleGivesEveryStyleALookOfItsOwn() {
        val looks = UiEffects.entries.map { it.consoleTreatment }
        assertEquals(looks.size, looks.toSet().size)
        assertTrue(ConsoleTreatment.SYSTEMATIC !in looks)
    }

    @Test fun everySystemPhobosRunsHasAConsoleToShow() {
        for (system in LaunchSystems.systemNames) {
            val slug = systemIconSlug(system)
            assertTrue("$system has no console", slug != null && consoleArtFor(slug) != null)
        }
    }

    @Test fun everySystemTheLibraryListsHasItsConsoleInEveryLook() {
        for (slug in systemIconSlugs) {
            val art = consoleArtFor(slug)
            assertTrue("no art for $slug", art != null && art in svgs && art in ConsolePixels.pictures)
        }
        val colors = themes.first()
        for ((art, picture) in ConsolePixels.pictures) for (treatment in ConsoleTreatment.entries - ConsoleTreatment.SYSTEMATIC) {
            val sprite = if (treatment.pixelated) ConsolePixelArt.sprite(picture, treatment, colors)
            else raster(picture).let { (pixels, w, h) -> ConsoleFinish.apply(treatment, pixels, w, h, colors) }
            val shown = sprite.pixels.count { it ushr 24 > 0 }
            assertTrue("$art in $treatment shows $shown pixels", shown > sprite.pixels.size / 10)
            // The edges are left clear: the finishes' margin, the pixel art's outline cell.
            val corners = listOf(0, sprite.width - 1, sprite.pixels.size - sprite.width, sprite.pixels.size - 1)
            if (treatment != ConsoleTreatment.RPG) assertTrue("$art in $treatment fills its corners", corners.all { sprite.pixels[it] ushr 24 < 40 })
        }
    }

    @Test fun eachConsoleLosesItsHaloAndNothingElse() {
        val white = Regex("""fill(?::|=")\s*(?:#fff\b|#ffffff\b|white\b)""")
        val translucent = Regex("""opacity(?::|=")\s*\.[56]\b""")
        for ((art, svg) in svgs) {
            val stripped = withoutHalo(svg)
            assertNotEquals("$art kept its halo", svg, stripped)
            val removed = svg.length - stripped.length
            val gone = svg.indices.first { it >= stripped.length || svg[it] != stripped[it] }.let { svg.substring(it, it + removed) }
            assertTrue("$art lost more than its halo: $gone", white.containsMatchIn(gone) && translucent.containsMatchIn(gone))
            assertEquals("$art lost more than one piece", svg.removeRange(svg.indexOf(gone), svg.indexOf(gone) + removed), stripped)
        }
    }

    @Test fun everyPictureIsItsArtOnTheGridInItsOwnColors() {
        val colorValue = Regex("""(?<![\w-])(?:fill|stroke|stop-color)\s*(?::|=\s*["'])\s*(#[0-9a-fA-F]{3,6}|[a-z]+)""")
        val named = mapOf("black" to "000000", "white" to "ffffff", "gray" to "808080", "grey" to "808080", "silver" to "c0c0c0", "red" to "ff0000")
        assertEquals(CONSOLE_ART, ConsolePixels.pictures.keys)
        for ((art, picture) in ConsolePixels.pictures) {
            assertEquals(art, ConsolePixels.CELLS, maxOf(picture.width, picture.height))
            assertTrue(art, picture.rows.all { it.length == picture.width })
            val own = colorValue.findAll(withoutHalo(svgs.getValue(art))).map { it.groupValues[1].lowercase() }.mapNotNull { v ->
                val hex = named[v] ?: v.removePrefix("#").takeIf { v.startsWith("#") } ?: return@mapNotNull null
                (if (hex.length == 3) hex.map { "$it$it" }.joinToString("") else hex).toLong(16).toInt() or (0xFF shl 24)
            }.toSet()
            for (color in picture.palette) assertTrue("$art's ${Integer.toHexString(color)} isn't one of its SVG's", color in own)
        }
    }

    private fun cellCounts(picture: ConsolePixels.Picture) = IntArray(picture.palette.size).also { cells ->
        for (y in 0 until picture.height) for (x in 0 until picture.width) picture.at(x, y).let { if (it >= 0) cells[it]++ }
    }

    @Test fun aConsoleKeepsItsLightnessAndItsPartsApartInTheTones() {
        for ((art, picture) in ConsolePixels.pictures) for (levels in listOf(ConsolePixelArt.PIXEL_TONES, ConsolePixelArt.RPG_TONES)) {
            val tones = toneLevels(picture, levels)
            val cells = cellCounts(picture)
            val total = tones.keys.sumOf { cells[it] }
            fun lightness(grey: Int) = Oklab.of(Color(picture.palette[grey])).l
            val main = tones.keys.filter { cells[it] * 20 >= total }.sortedBy(::lightness)
            val shown = main.map { tones.getValue(it) }
            val name = "$art in ${levels.size} tones"
            assertEquals("$name: lighter greys came out darker", shown.sorted(), shown)
            if (main.size >= 2) assertTrue("$name: its parts all in one tone", shown.toSet().size >= 2)
            val own = tones.keys.sumOf { lightness(it).toDouble() * cells[it] } / total
            val drawn = tones.entries.sumOf { (grey, tone) -> levels[tone].toDouble() * cells[grey] } / total
            assertTrue("$name: lightness $own drawn at $drawn", Math.abs(drawn - own) < 0.25)
        }
    }

    @Test fun theArtIsRenderedAsLargeAsFitsItsBoxFinished() {
        for (box in 16..480) {
            val side = ConsoleFinish.inside(box)
            assertTrue("$box: $side", side + 2 * ConsoleFinish.margin(side) <= box)
            assertTrue("$box: ${side + 1} fits too", side + 1 + 2 * ConsoleFinish.margin(side + 1) > box)
        }
    }

    @Test fun pixelArtKeepsAConsolesPartsApartInALimitedPalette() {
        for (colors in themes) for ((art, picture) in ConsolePixels.pictures) {
            val sprite = ConsolePixelArt.sprite(picture, ConsoleTreatment.PIXEL, colors)
            val cells = (0 until picture.height).flatMap { y -> (0 until picture.width).filter { x -> picture.at(x, y) >= 0 }.map { x -> x to y } }
            fun shown(cell: Pair<Int, Int>) = sprite.pixels[(cell.second + 1) * sprite.width + cell.first + 1]
            val greyCells = cells.filter { (x, y) -> Oklab.of(Color(picture.palette[picture.at(x, y)])).chroma < 0.05f }
            val greys = greyCells.map { (x, y) -> picture.palette[picture.at(x, y)] }.toSet()
            val tones = greyCells.map(::shown).toSet()
            // A dark console's greys don't all fall into one tone: the blobs this replaced.
            assertTrue("$art in pixel art: ${greys.size} greys came out as ${tones.size} tones", greys.size < 2 || tones.size >= 2)
            val palette = cells.map(::shown).toSet()
            assertTrue("$art in pixel art uses ${palette.size} colors", tones.size <= 4 && palette.size <= 4 + colors.accents.size * 2)
        }
    }

    @Test fun rpgPixelArtHasItsOutlineHighlightAndShadow() {
        for (colors in themes) for ((art, picture) in ConsolePixels.pictures) {
            val sprite = ConsolePixelArt.sprite(picture, ConsoleTreatment.RPG, colors)
            assertEquals(art, picture.width + 3, sprite.width)
            assertTrue("$art: no drop shadow", sprite.pixels.any { it == 0x66000000 })
            assertTrue("$art: no outline", sprite.pixels.any { it == 0xFF120F1C.toInt() })
            fun filled(x: Int, y: Int) = x in 0 until picture.width && y in 0 until picture.height && picture.at(x, y) >= 0
            fun lightness(x: Int, y: Int) = Oklab.of(Color(sprite.pixels[(y + 1) * sprite.width + x + 1])).l
            val cells = (0 until picture.height).flatMap { y -> (0 until picture.width).filter { x -> filled(x, y) }.map { x -> x to y } }
            val (rim, inner) = cells.partition { (x, y) -> !filled(x - 1, y) || !filled(x, y - 1) }
            val innerByColor = inner.groupBy { (x, y) -> picture.at(x, y) }
            val compared = rim.mapNotNull { (x, y) -> innerByColor[picture.at(x, y)]?.first()?.let { (ix, iy) -> lightness(x, y) - lightness(ix, iy) } }
            assertTrue("$art: no highlight along its top and left", compared.isNotEmpty() && compared.all { it > 0f })
        }
    }

    private fun contrastOverTile(pixel: Int, colors: ConsoleColors): Float {
        val shown = Color(pixel).compositeOver(Color(colors.tile))
        return contrastRatio(shown, Color(colors.tile))
    }

    @Test fun neonAndPhosphorStandOutOnLightAndDarkTiles() {
        val worst = HashMap<String, Pair<Float, String>>()
        for (colors in themes) for ((art, picture) in ConsolePixels.pictures) {
            val (pixels, w, h) = raster(picture)
            fun inside(x: Int, y: Int) = x in 0 until w && y in 0 until h && pixels[y * w + x] ushr 24 > 0
            val outline = pixels.indices.count { i ->
                val x = i % w
                val y = i / w
                inside(x, y) && !(inside(x - 1, y) && inside(x + 1, y) && inside(x, y - 1) && inside(x, y + 1))
            }
            for (treatment in listOf(ConsoleTreatment.NEON, ConsoleTreatment.CRT)) {
                val finished = ConsoleFinish.apply(treatment, pixels, w, h, colors).pixels
                val standing = finished.count { contrastOverTile(it, colors) >= 3f }
                val share = standing.toFloat() / outline
                val key = "$treatment ${if (colors.isDark) "dark" else "light"}"
                if (share < (worst[key]?.first ?: Float.MAX_VALUE)) worst[key] = share to art
            }
        }
        for ((key, value) in worst) {
            val (share, art) = value
            assertTrue("$key: $art stands out along ${share}x its outline", share >= 2f)
        }
    }

    @Test fun theSmoothLooksAreInTheirColors() {
        for (colors in themes) {
            val (pixels, w, h) = raster(ConsolePixels.pictures.getValue("snes"))
            val manga = ConsoleFinish.apply(ConsoleTreatment.MANGA, pixels, w, h, colors).pixels.filter { it ushr 24 > 0 }
            assertTrue("manga is all ink", manga.isNotEmpty() && manga.all { it and 0xFFFFFF == colors.ink and 0xFFFFFF })
            val crt = ConsoleFinish.apply(ConsoleTreatment.CRT, pixels, w, h, colors).pixels.filter { it ushr 24 > 128 }
            val phosphor = Oklab.of(Color(colors.phosphor)).hue
            assertTrue("crt glows in its phosphor", crt.isNotEmpty() && crt.all { Oklab.of(Color(it or (0xFF shl 24))).let { c -> c.chroma < 0.03f || hueGap(c.hue, phosphor) < 0.5f } })
            val neon = ConsoleFinish.apply(ConsoleTreatment.NEON, pixels, w, h, colors).pixels.filter { it ushr 24 > 200 }
            val tubes = colors.neon.map { Oklab.of(Color(it)).hue }
            val inHue = neon.count { Oklab.of(Color(it or (0xFF shl 24))).let { c -> c.chroma < 0.03f || tubes.any { t -> hueGap(c.hue, t) < 0.5f } } }
            assertTrue("neon has its tubes in its colors: $inHue of ${neon.size}", neon.size > 50 && inHue >= neon.size * 9 / 10)
        }
    }

    private fun hueGap(a: Float, b: Float): Float {
        val d = Math.abs(a - b) % (2 * Math.PI.toFloat())
        return minOf(d, 2 * Math.PI.toFloat() - d)
    }
}
