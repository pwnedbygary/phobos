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
            XXXXXXXXXXXXXXXX
            XAAAAAAAAAAAAAAX
            XXXXXXXXXXXXXXXX
            XSSXXSSXXSSXXSSX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "colecovision" to art(
            """
            XXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWX
            XWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXX
            XAASAXXXXXXAASAX
            XAASAXXXXXXAASAX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "nes" to art(
            """
            XXXXXXXXXXXXXXXX
            XWWWXXSSSSXXWWWX
            XWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXX
            XSSXXXXXXXXXXSSX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "snes" to art(
            """
            XXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWX
            XWWWWWWWWWWWWWWX
            XWWWWXXAXAXAXAXX
            XWWWWXXAXAXAXAXX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "sgb" to art(
            """
            .....XXXXXX.....
            .....XSSSSX.....
            .....XXXXXX.....
            XXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWX
            XWWWWSSSSSSWWWWX
            XWWWWSSSSSSWWWWX
            XWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "arcade" to art(
            """
            ....XXXXXXXXXX....
            ....XAAAAAAAAX....
            ....XXXXXXXXXX....
            ....XXWWWWWWXX....
            ....XXWSSSSWXX....
            ....XXWSSSSWXX....
            ....XXWWWWWWXX....
            ....XXXXXXXXXX....
            ...XXAXXXXXAXXX...
            ...XXXXXXXXXXXX...
            ...XXXXXXXXXXXX...
            ...XXXXWWWWXXXX...
            ...XXXXWAAWXXXX...
            ...XXXXWWWWXXXX...
            ...XXXXXXXXXXXX...
            """,
        ),
        "laseractive" to art(
            """
            XXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWX
            XWWSSSSSSSSWWWAX
            XWWSSSSSSSSWWWAX
            XWWSSSSSSSSWWWAX
            XWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "pceld" to art(
            """
            XXXXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWWWX
            XWWWXXXXXXXXXXWWWX
            XWWWXXXXXXXXXXWWWX
            XWWWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXXXX
            """,
        ),
        "n64" to art(
            """
            ......XXXX......
            ....XXSSSSXX....
            ..XXXXXXXXXXXX..
            XXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXX
            XXSSXXXSSXXXSSXX
            XXXXXXXXXXXXXXXX
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
            XXXXXXXXXXX.
            XXXXXXXXXX..
            """,
        ),
        "gbc" to art(
            """
            .XXXXXXXXXXXX.
            XAAAAAAAAAAAAX
            XAWWWWWWWWWWXA
            XAWSSSSSSSSWXA
            XAWSSSSSSSSWXA
            XAWWWWWWWWWWXA
            XAXXXXXXXXXXAX
            XAAXXXXXXXXAAX
            XAAAXXXXXXAAAX
            XAAXXXXXXXXAAX
            XAXXWXWXXXXXAX
            XXXXXXXXXXXXXX
            XXXXXXXXXXXXX.
            """,
        ),
        "gba" to art(
            """
            XXXXX..........XXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWWWXXXX
            XXWXXXWSSSSSSSSWXXXX
            XWWWXXWSSSSSSSSWXXAX
            XXWXXXWSSSSSSSSWXXXX
            XXXXXXWWWWWWWWWWXXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "sms" to art(
            """
            ....................
            ....................
            ....XXXXSSSSXXXXXXXX
            ...XXXXWWWWWWWXXXXXX
            ..XXXXXWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXAXXXXXXXXXXXXXAXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "genesis" to art(
            """
            XXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWXXXXXX
            XXXXWWWWWWWWXXXXXX
            XXXWWWWSSSSWWXXXXX
            XXXWWWSSSSSSWXXXXX
            XXXWWWSSSSSSWXXXXX
            XXXWWWWSSSSWWXXXXX
            XXXXWWWWWWWWXXXXXX
            XXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXX
            """,
        ),
        "sega32" to art(
            """
            ......XXXXXXXX......
            ....XXAAAAAAAAXX....
            ...XAAAAAAAAAAAAX...
            ...XXXXXXXXXXXXXXX..
            XXXXXXXXXXXXXXXXXXXX
            XXXXXWWWWWWWWXXXXXXX
            XXXXXWWSSSSSSWWXXXXX
            XXXXXWWSSSSSSWWXXXXX
            XXXXXWWWWWWWWXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "gamegear" to art(
            """
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXWWWWWWWWXXXXXX
            XXWXXXWSSSSSSSSWXXXX
            XWWWXXWSSSSSSSSWXXAX
            XXWXXXWSSSSSSSSWXXXX
            XXXXXXWAAAAAAAAXXXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "segacd" to art(
            """
            ........XXXXXXXX....
            ........XWWWWWWX....
            ........XXXXXXXX....
            ..XXXXXXXXXXXXXXXX..
            ..XWWWSSSSSSSSWWWXXX
            ..XWWSSSSSSSSSSWWXXX
            ..XWWWSSSSSSSSWWWXXX
            ..XWWWWWWWWWWWWWWXXX
            ..XXXXXXXXXXXXXXXX..
            """,
        ),
        "psx" to art(
            """
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXWWWWWXXX
            XXXXXXXXXXXSSSSSSXXX
            XXAXXAXXXXSSSSSSSSXX
            XXXXXXXXXXSSSSSSSSXX
            XXXXXXXXXXSSSSSSSSXX
            XXXXXXXXXXXSSSSSSXXX
            XXXXXXXXXXXXWWWWWXXX
            XXXXXXXXXXXXXXXXXXXX
            SSSSSSSSSSSSSSSSSSSS
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "neogeomvs" to art(
            """
            ...XXXXXXXXXXXXXXXX...
            ...XAAAAAAAAAAAAAAX...
            ...XXXXXXXXXXXXXXXX...
            ...XXXXXXXXXXXXXXXX...
            ...XXWWWWWWWWWWWWXXX..
            ...XXWSSSSSSSSSSWXXX..
            ...XXWSSSSSSSSSSWXXX..
            ...XXWWWWWWWWWWWWXXX..
            ...XXXXXXXXXXXXXXXX...
            ...XXXXXXXXXXXXXXXX...
            ...XXXXXXXXXXXXXXXX...
            ...XXXXXXXXXXXXXXXX...
            """,
        ),
        "neo-geo-cd" to art(
            """
            XXXXXXXXXXXXXXXXXXXX
            XXXXWWWWWWXXXXXXWWXX
            XXXXWWWWWWXXXXXXWWXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXWWSSSSSSSSWWXXX
            XXXXXWWSSSSSSSSWWXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "neo-geo-pocket" to art(
            """
            ...XXXXXXXXXXXXXX...
            .XXXXXXXXXXXXXXXXXX.
            XXXXXXWWWWWWWWXXXXXX
            XXWWXXWSSSSSSSSWXXXX
            XWSSWXWSSSSSSSSWXXAX
            XWSSWXWSSSSSSSSWXAXX
            XXWWXXWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXX...
            """,
        ),
        "neo-geo-pocket-color" to art(
            """
            ...XXXXXXXXXXXXXX...
            .XXAAAXXXXXXXXAAAXX.
            XXXXXXWWWWWWWWXXXXXX
            XXWWXXWSSSSSSSSWXXXX
            XWAAWXWSSSSSSSSWXXAX
            XWAAWXWSSSSSSSSWXAXX
            XXWWXXWWWWWWWWXXXXXX
            .XXXXXXXXXXXXXXXXXX.
            ...XXXXXXXXXXXXXX...
            """,
        ),
        "zx-spectrum" to art(
            """
            XXXXXXXXXXXXXXXXXXXX
            XWSWSWSWSWSWSWSWSWSX
            XWSWSWSWSWSWSWSWSWSX
            XWSWSWSWSWSWSWSWSWSX
            XXXXWSWSWSWSWXXXXAAX
            XXXXXXXXXXXXXXXXXAAX
            XXXXXXXXXXXXXXXXXAAX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "pce" to art(
            """
            XXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWX
            XAAAAAAAAAAAAAAX
            XXXXWWWWWWWWXXXX
            XXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXX
            """,
        ),
        "pcecd" to art(
            """
            XXXXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWWWX
            XWWWSSSSSSSSWWWWAX
            XWWWSSSSSSSSWWWWAX
            XWWWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXXXX
            """,
        ),
        "supergrafx" to art(
            """
            ....XXXXXXXXXXXX....
            ..XXXXXXXXXXXXXXXX..
            .XXXXXXXXXXXXXXXXXX.
            XXXXXXXXXXXXXXXXXXXX
            XAAAAAAAAAAAAAAAAAAX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "wonderswan" to art(
            """
            XXXXXXXXXX
            XXXXXXXXXX
            XXSSSSSSXX
            XXSSSSSSXX
            XXSSSSSSXX
            XXSSSSSSXX
            XXXXXXXXXX
            XAXXXXXXXA
            XXXXXXXXXX
            XXXXXXXXXX
            .XXXXXXXX.
            ..XXXXXX..
            ...XXXX...
            ....XX....
            ..........
            """,
        ),
        "wonderswan-color" to art(
            """
            XXXXXXXXXXXX
            XAAAAAAAAAAX
            XASSSSSSSSXA
            XASSSSSSSSXA
            XASSSSSSSSXA
            XASSSSSSSSXA
            XAXXXXXXXXAX
            XAAXXXXXXXAX
            XAXXXXXXXXAX
            XAXXXXXXXXAX
            .XXXXXXXXXX.
            ..XXXXXXXX..
            ...XXXXXX...
            ....XXXX....
            """,
        ),
        "pcv2" to art(
            """
            XXXXXXXXXXXX
            XWWWWWWWWWWX
            XWWSSSSSSSWX
            XWWSSSSSSSWX
            XWWSSSSSSSWX
            XWWWWWWWWWWX
            XWWWWWWWWWWX
            XWWWWWWWWAAX
            XWWWWWWWWAAX
            XWWWWWWWWWWX
            XXXXXXXXXXXX
            XXXXXXXXXXXX
            """,
        ),
        "msx" to art(
            """
            ..............XXXX..
            XXXXXXXXXXXXXXXXXXXX
            XWWWWWWWWWWWWWWWWWWX
            XWSWSWSWSWSWSWSWSWSX
            XWSWSWSWSWSWSWSWSWSX
            XWWWWWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            XXXXXXXXXXXXXXXXXXXX
            """,
        ),
        "msx2" to art(
            """
            ....XXXXXXXXXXXX....
            ..XXXXXXXXXXXXXXXX..
            .XXXXXXXXXXXXXXXXXX.
            XXXXXXXXXXXXXXXXXXXX
            XWSWSWSWSWSWSWSWSWSX
            XWSWSWSWSWSWSWSWSWSX
            XWWWWWWWWWWWWWWWWWWX
            XXXXXXXXXXXXXXXXXAAX
            XXXXXXXXXXXXXXXXXAAX
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
