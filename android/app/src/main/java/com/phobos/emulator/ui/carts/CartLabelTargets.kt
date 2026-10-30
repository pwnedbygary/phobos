package com.phobos.emulator.ui.carts

import com.phobos.emulator.art.ArtDownloader
import com.phobos.emulator.art.LabelTarget

/**
 * The label-scan target for a Phobos system, through [CartShapes]: the Cartridge system id that keys the bundled
 * label index, its media (which decides the crop shape exactly as Cartridge's scraper does) and the region.
 *
 * Region: Phobos does not record a game's region, so the downloader asks in Cartridge's default order (`US`),
 * whose order list carries the other regions behind it.
 */
object CartLabelTargets {

    const val DEFAULT_REGION = "US"

    /** Null when Cartridge has no such system: those games never request a label scan. */
    fun of(system: String, region: String = DEFAULT_REGION): LabelTarget? {
        val entry = CartShapes.entry(system) ?: return null
        return LabelTarget(entry.id, entry.mediaType, entry.mediaShape, region)
    }
}
