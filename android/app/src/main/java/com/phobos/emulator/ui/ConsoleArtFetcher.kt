package com.phobos.emulator.ui

import android.content.Context
import android.graphics.Bitmap
import coil.ImageLoader
import coil.decode.DataSource
import coil.decode.ImageSource
import coil.fetch.FetchResult
import coil.fetch.Fetcher
import coil.fetch.SourceResult
import coil.key.Keyer
import coil.request.ImageRequest
import coil.request.Options
import coil.size.Size
import coil.transform.Transformation
import com.phobos.emulator.ui.theme.ConsoleArtPalette
import com.phobos.emulator.ui.theme.ConsoleColors
import com.phobos.emulator.ui.theme.ConsoleFinish
import com.phobos.emulator.ui.theme.ConsoleTreatment
import com.phobos.emulator.ui.theme.withoutHalo
import okio.Buffer

/**
 * A console illustration from `assets/platforms`: drawn in [palette]'s theme colors for [ConsoleTreatment.SYSTEMATIC],
 * or, for a treatment drawn from the SVG, in its own greys without its halo, for [ConsoleFinishing] to finish.
 */
data class ConsoleArt(val asset: String, val palette: ConsoleArtPalette, val treatment: ConsoleTreatment)

/** Caches each illustration once per theme and treatment. */
class ConsoleArtKeyer : Keyer<ConsoleArt> {
    override fun key(data: ConsoleArt, options: Options) = "${data.asset}:${data.palette.key}:${data.treatment}"
}

/** Reads the illustration's SVG and recolors it, or takes its halo off, for Coil's SvgDecoder. */
class ConsoleArtFetcher(private val art: ConsoleArt, private val options: Options) : Fetcher {
    override suspend fun fetch(): FetchResult {
        val svg = options.context.assets.open("platforms/${art.asset}.svg").use { it.readBytes().decodeToString() }
        val text = if (art.treatment == ConsoleTreatment.SYSTEMATIC) art.palette.recolorSvg(svg) else withoutHalo(svg)
        return SourceResult(
            source = ImageSource(Buffer().writeUtf8(text), options.context),
            mimeType = "image/svg+xml",
            dataSource = DataSource.DISK,
        )
    }

    class Factory : Fetcher.Factory<ConsoleArt> {
        override fun create(data: ConsoleArt, options: Options, imageLoader: ImageLoader): Fetcher = ConsoleArtFetcher(data, options)
    }
}

/** Finishes an illustration drawn without its halo in [treatment] ([ConsoleFinish]), in [colors]. */
class ConsoleFinishing(private val treatment: ConsoleTreatment, private val colors: ConsoleColors) : Transformation {
    override val cacheKey: String = "finish:$treatment:${colors.key}"

    override fun equals(other: Any?) = other is ConsoleFinishing && other.cacheKey == cacheKey

    override fun hashCode() = cacheKey.hashCode()

    override suspend fun transform(input: Bitmap, size: Size): Bitmap {
        val pixels = IntArray(input.width * input.height)
        input.getPixels(pixels, 0, input.width, 0, 0, input.width, input.height)
        val finished = ConsoleFinish.apply(treatment, pixels, input.width, input.height, colors)
        return Bitmap.createBitmap(finished.pixels, finished.width, finished.height, Bitmap.Config.ARGB_8888)
    }
}

/** The request for [asset]'s console in [art]'s treatment and colors, shown in a [box] pixels square. */
fun consoleArtRequest(context: Context, asset: String, art: LibraryArt, box: Int): ImageRequest =
    ImageRequest.Builder(context)
        .data(ConsoleArt(asset, art.palette, art.treatment))
        .apply {
            if (art.treatment != ConsoleTreatment.SYSTEMATIC) {
                size(ConsoleFinish.inside(box))
                transformations(ConsoleFinishing(art.treatment, art.colors))
            }
        }
        .build()
