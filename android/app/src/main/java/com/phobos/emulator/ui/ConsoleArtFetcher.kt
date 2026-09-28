package com.phobos.emulator.ui

import coil.ImageLoader
import coil.decode.DataSource
import coil.decode.ImageSource
import coil.fetch.FetchResult
import coil.fetch.Fetcher
import coil.fetch.SourceResult
import coil.key.Keyer
import coil.request.Options
import com.phobos.emulator.ui.theme.ConsoleArtPalette
import okio.Buffer

/** A console illustration from `assets/platforms`, drawn in [palette]'s theme colors. */
data class ConsoleArt(val asset: String, val palette: ConsoleArtPalette)

/** Caches each illustration once per theme. */
class ConsoleArtKeyer : Keyer<ConsoleArt> {
    override fun key(data: ConsoleArt, options: Options) = "${data.asset}:${data.palette.key}"
}

/** Reads the illustration's SVG and recolors it for Coil's SvgDecoder. */
class ConsoleArtFetcher(private val art: ConsoleArt, private val options: Options) : Fetcher {
    override suspend fun fetch(): FetchResult {
        val svg = options.context.assets.open("platforms/${art.asset}.svg").use { it.readBytes().decodeToString() }
        return SourceResult(
            source = ImageSource(Buffer().writeUtf8(art.palette.recolorSvg(svg)), options.context),
            mimeType = "image/svg+xml",
            dataSource = DataSource.DISK,
        )
    }

    class Factory : Fetcher.Factory<ConsoleArt> {
        override fun create(data: ConsoleArt, options: Options, imageLoader: ImageLoader): Fetcher = ConsoleArtFetcher(data, options)
    }
}
