package com.phobos.emulator.ui.theme

/**
 * Original platform silhouettes for the built-in Canvas icon packs, each centred on one 22x15 grid so that every tile
 * shares a cell size.
 */
object PlatformGlyphs {
    const val WIDTH = 22
    const val HEIGHT = 15

    /** `.` is transparent; X is the silhouette (ink), cut into with W (body) and S (shade); A is the accent. */
    val bySlug: Map<String, List<String>> = mapOf(
        "atari2600" to art(
            """
            ...XXXXXXXXXXXXXXXX...
            ..XSXSXSXWWWWWXSXSXSX.
            .XSXSXSXXWSSSWXXSXSXSX
            XSXSXSXSXWWWWWXSXSXSXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXAXXAXXXXXXXXXXXAXXAX
            AAAAAAAAAAAAAAAAAAAAAA
            ASAAASAAASAAASAAASAAAS
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "colecovision" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXWWWWWWWWWWX
            XWWWWWWWXXXWSSSWWSSSWX
            XWSSSSSWXXXWSASWWSASWX
            XWWWWWWWXXXWSSSWWSSSWX
            XXXXXXXXXXXWSASWWSASWX
            XXAXXXXXXXXWSSSWWSSSWX
            XXXXXXXXXXXWWWWWWWWWWX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "nes" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWWWWWWWWWWWWWWWWWXX
            XXWSSSSSSSSSSSSSSSSWXX
            XXWSSSSSSSSSSSSSSSSWXX
            XXWWWWWWWWWWWWWWWWWWXX
            XXXXXXXXXXXXXXXXXXXXXX
            XSSSSSSSSSSSSSSSSSSSSX
            XXXXXXXXXXXXXXXXXXXXXX
            XXAAXAAXXWWXXWWXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "snes" to art(
            """
            ...XXXXXXXXXXXXXXXX...
            .XXXXXXXXXXXXXXXXXXXX.
            XXXAXXXWWWWWWWWXXXAXXX
            XXXAXXXWSSSSSSWXXXAXXX
            XXXXXXXWWWWWWWWXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            SSSSSSSSSSSSSSSSSSSSSS
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWXWWXXXXXXXXXAXAXAX
            XXXXXXXXXXXXXXXXXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXXXX...
            """,
        ),
        "sgb" to art(
            """
            .......XXXXXXXX.......
            .......XWWWWWWX.......
            .......XWSSSSWX.......
            ..XXXXXXXXXXXXXXXXXX..
            .XXXXXXXXXXXXXXXXXXXX.
            XXXXXXXXXXXXXXXXXXXXXX
            XXXWWWWWWWWWWWWWWWWXXX
            XXXWAAAAAAAAAAAAAAWXXX
            XXXWWWWWWWWWWWWWWWWXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "arcade" to art(
            """
            .XXXXXXXXXXXX.
            .XAAAAAAAAAAX.
            .XXXXXXXXXXXX.
            .XXWWWWWWWWXX.
            .XXWSSSSSSWXX.
            .XXWSSSSSSWXX.
            .XXWWWWWWWWXX.
            XXXXXXXXXXXXXX
            XXAXXXXXXAXAXX
            XXXXXXXXXXXXXX
            .XXXXXXXXXXXX.
            .XXXXWWWWXXXX.
            .XXXXWAAWXXXX.
            .XXXXWWWWXXXX.
            .XXXXXXXXXXXX.
            """,
        ),
        "laseractive" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXAXAXXXXXXXXXXXWWWWXX
            XXXXXXXXXXXXXXXXWAAWXX
            XXXXXXXXXXXXXXXXXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            .XXXXXWWWWWWWWWWXXXXX.
            .XXXWWSSSSSSSSSSWWXXX.
            .XXXWSSSSSSXSSSSSWXXX.
            .XXXWWSSSSSSSSSSWWXXX.
            .XXXXXWWWWWWWWWWXXXXX.
            """,
        ),
        "pceld" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XAAAAAAAAAAAAAAAWWWWXX
            XXXXXXXXXXXXXXXXWSSWXX
            XXXXXXXXXXXXXXXXWWWWXX
            XXXXXXXXXXXXXXXXXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            .XXXXXWWWWWWWWWWXXXXX.
            .XXXWWSSSSSSSSSSWWXXX.
            .XXXWSSSSSSXSSSSSWXXX.
            .XXXWWSSSSSSSSSSWWXXX.
            .XXXXXWWWWWWWWWWXXXXX.
            """,
        ),
        "n64" to art(
            """
            .......XXXXXXXX.......
            ......XXXXXXXXXX......
            ......XXWWWWWWXX......
            ...XXXXXWSSSSWXXXXX...
            .XXXXXXXWWWWWWXXXXXXX.
            XXXAXXXXXXXXXXXXXXAXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWXXWWXXXXXXWWXXWWXX
            .XXXXXXXXXXXXXXXXXXXX.
            ..XXXXXXXXXXXXXXXXXX..
            """,
        ),
        "gb" to art(
            """
            XXXXXXXXXXXX
            XXXXXXXXXXXX
            XXWWWWWWWWXX
            XXWSSSSSSWXX
            XXWSSSSSSWXX
            XXWSSSSSSWXX
            XXWWWWWWWWXX
            XXXXXXXXXXXX
            XXWXXXXXXXXX
            XWWWXXXXXAXX
            XXWXXXXXAXXX
            XXXXXXXXXXXX
            XXXXWXWXXXXX
            XXXXXXXXXXX.
            XXXXXXXXXX..
            """,
        ),
        "gbc" to art(
            """
            .XXXXXXXXXX.
            XXAAAAAAAAXX
            XXWWWWWWWWXX
            XXWSSSSSSWXX
            XXWSSSSSSWXX
            XXWWWWWWWWXX
            XXXXXXXXXXXX
            XXWXXXXXXAXX
            XWWWXXXXAXXX
            XXWXXXXXXXXX
            XXXXWXWXXXXX
            .XXXXXXXXXX.
            """,
        ),
        "gba" to art(
            """
            .XXXXX..........XXXXX.
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXWXXXWSSSSSSSSWXXXXXX
            XWWWXXWSSSSSSSSWXXAXXX
            XXWXXXWSSSSSSSSWXAXXXX
            XXXXXXWWWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXXXX...
            """,
        ),
        "sms" to art(
            """
            ....XXXXXXXXXXXXXXXXXX
            ...XXXXXXWWWWWWWXXXXXX
            ..XXXXXXXWSSSSSWXXXXXX
            .XXXXXXXXWWWWWWWXXXXXX
            XAAAAAAAAAAAAAAAAAAAAX
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWWWWWXXXXXXXXXAXAXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "genesis" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXWWWWWWXXXXXXXX
            XXXXXXWWSSSSSSWWXXXXXX
            XXAXXWSSWWWWWWSSWXXXXX
            XXAXXWSSWSSSSWSSWXXAXX
            XXXXXWSSWWWWWWSSWXXAXX
            XXXXXXWWSSSSSSWWXXXXXX
            XXXXXXXXWWWWWWXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWXWWXXXXXXXXXXAAAXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "sega32" to art(
            """
            ......XXXXXXXXXX......
            ....XXAAAAAAAAAAXX....
            ...XAAAAAAAAAAAAAAX...
            ...XXXXXXXXXXXXXXXX...
            .......XXXXXXXX.......
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXWWSSSSSSSSWWXXXXX
            XXAXWSSWWWWWWWWSSWXXXX
            XXXXXWWSSSSSSSSWWXXAXX
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWXWWXXXXXXXXXXAAAXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "gamegear" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXXXXXWSSSSSSSSWXXXXXX
            XXWXXXWSSSSSSSSWXXXXXX
            XWWWXXWSSSSSSSSWXXXAXX
            XXWXXXWSSSSSSSSWXXAXXX
            XXXXXXWSSSSSSSSWXXXXXX
            XXXXXXWWWWWWWWWWXAXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "segacd" to art(
            """
            .XXXXXXXXXXXXXXXXXXXX.
            .XXXXXXWWWWWWWWXXXXXX.
            .XXAXWWSSSSSSSSWWXXXX.
            .XXXXXXWWWWWWWWXXXXXX.
            .XXXXXXXXXXXXXXXXXXXX.
            ......................
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWWWWWWWWWWWWWWWWWXX
            XXXXXXXXXXXXXXXXXXXAXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "psx" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXWWWWWXXXXX
            XXAXXAXXXXXWSSSSSWXXXX
            XXXXXXXXXXWSSSSSSSWXXX
            XXXXXXXXXXWSSSXSSSWXXX
            XXXXXXXXXXWSSSSSSSWXXX
            XXXXXXXXXXXWSSSSSWXXXX
            XXXXXXXXXXXXWWWWWXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            SSSSSSSSSSSSSSSSSSSSSS
            XXWWXWWXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "neogeomvs" to art(
            """
            .................XX...
            ................XAAX..
            ................XAAX..
            .................XX...
            XXXXXXXXXXXXXX...XX...
            XXXXXXXXXXXXXX...XX...
            XXWWWWWWWWWWXX.XXXXXXX
            XXWSSSSSSSSWXX.XXXXXXX
            XXWWWWWWWWWWXX.XXAAAAX
            XXXXXXXXXXXXXX.XXXXXXX
            XAAAAAAAAAAAAX.XXXXXXX
            XXXXXXXXXXXXXX........
            """,
        ),
        "neo-geo-cd" to art(
            """
            .................XX...
            ................XAAX..
            ................XAAX..
            .................XX...
            XXXXXXXXXXXXXX...XX...
            XXXXWWWWWWXXXX...XX...
            XXXWSSSSSSWXXX.XXXXXXX
            XXXWSSXXSSWXXX.XXXXXXX
            XXXWSSSSSSWXXX.XXAAAAX
            XXXXWWWWWWXXXX.XXXXXXX
            XAAAAAAAAAAAAX.XXXXXXX
            XXXXXXXXXXXXXX........
            """,
        ),
        "neo-geo-pocket" to art(
            """
            ...XXXXXXXXXXXXXXXX...
            .XXXXXXXXXXXXXXXXXXXX.
            XXXXXXWWWWWWWWWWXXXXXX
            XXWWXXWSSSSSSSSWXXXXXX
            XWSSWXWSSSSSSSSWXXAXXX
            XWSSWXWSSSSSSSSWXAXXXX
            XXWWXXWWWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXXXX...
            """,
        ),
        "neo-geo-pocket-color" to art(
            """
            ...XXXXXXXXXXXXXXXX...
            .XXAAAXXXXXXXXXXAAAXX.
            XXXXXXWWWWWWWWWWXXXXXX
            XXWWXXWSSSSSSSSWXXXXXX
            XWAAWXWSSSSSSSSWXXAXXX
            XWAAWXWSSSSSSSSWXAXXXX
            XXWWXXWWWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXXXX...
            """,
        ),
        "zx-spectrum" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XWSWSWSWSWSWSWSWSWSWXX
            XXWSWSWSWSWSWSWSWSWSWX
            XWSWSWSWSWSWSWSWSWSWXX
            XXWSWSWSWSWSWSWXXXXAAX
            XXXXWSWSWSWSWXXXXXAAXX
            XXXXXXXXXXXXXXXXAAXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "pce" to art(
            """
            .XXXXXXXXXXXXXX.
            XXXXXXXXXXXXXXXX
            XXWWWWWWWWWWWWXX
            XXXXXXXXXXXXXXXX
            XAAAAAAAAAAAAAAX
            XXXXWWWWWWWWXXXX
            XXAXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "pcecd" to art(
            """
            ..XXXXXXX..XXXXXXXXXXX
            .XXXXXXXXX.XXXWWWWWXXX
            .XXAAAAAXX.XXWSSSSSWXX
            .XXWWWWWXX.XWSSSXSSSWX
            XXXXXXXXXXXXXWSSSSSWXX
            XXXXXXXXXXXXXXWWWWWXXX
            XXAXXXXXXXXXXXXXXXXAXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "supergrafx" to art(
            """
            ....XXXXXXXXXXXXXX....
            ..XXXXXXXXXXXXXXXXXX..
            .XXXWWWWWWWWWWWWWWXXX.
            XXXXXXXXXXXXXXXXXXXXXX
            XAAAAAAAAAAAAAAAAAAAAX
            XXXXXXXWWWWWWWWXXXXXXX
            XXAXXXXXXXXXXXXXXXXAXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "wonderswan" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXXWXXWSSSSSSSSWXXXXXX
            XXWXWXWSSSSSSSSWXXXAXX
            XXXWXXWSSSSSSSSWXXAXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "wonderswan-color" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXXAXXWSSSSSSSSWXXXXXX
            XXAXAXWSSSSSSSSWXXXAXX
            XXXAXXWSSSSSSSSWXXAXXX
            XXXXXXWWWWWWWWWWXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "pcv2" to art(
            """
            .XXXXXXXXXXXXXXXXXXXX.
            XXXXXXWWWWWWWWWWXXXXXX
            XXXAXXWSSSSSSSSWXXAXXX
            XXAAAXWSSSSSSSSWXAXAXX
            XXXAXXWSSSSSSSSWXXAXXX
            XXXXXXWWWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXXXX.
            """,
        ),
        "msx" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWXXXXXXXX
            XAAAAAXXXXXXXXXXXXXXXX
            XWSWSWSWSWSWSWSWSWSWXX
            XXWSWSWSWSWSWSWSWSWSWX
            XWSWSWSWSWSWSWSWSWSWXX
            XXXXXWWWWWWWWWWXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "msx2" to art(
            """
            XXXXXXXXXXXXXXXXXXXXXX
            XXWWWWWWXXXXXXWWWWWWXX
            XAAAAAXXXXXXXXXXAAAAAX
            XWSWSWSWSWSWSWSWSWSWXX
            XXWSWSWSWSWSWSWSWSWSWX
            XWSWSWSWSWSWSWSWSWSWXX
            XXXXXWWWWWWWWWWXXXXXXX
            XXXXXXXXXXXXXXXXXXXXXX
            """,
        ),
    )

    /** Centres a drawing on the shared grid. */
    private fun art(drawing: String): List<String> {
        val rows = drawing.trimIndent().lines()
        val width = rows.maxOf(String::length)
        require(width <= WIDTH && rows.size <= HEIGHT && rows.all { it.length == width }) {
            "A glyph's rows must be even and fit $WIDTH by $HEIGHT"
        }
        val left = (WIDTH - width) / 2
        val top = (HEIGHT - rows.size) / 2
        return List(HEIGHT) { row ->
            val line = rows.getOrNull(row - top) ?: return@List ".".repeat(WIDTH)
            ".".repeat(left) + line + ".".repeat(WIDTH - width - left)
        }
    }
}
