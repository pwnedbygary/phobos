package com.phobos.emulator.ui.theme

/** Original, compact platform silhouettes for the built-in Canvas icon packs. */
object PlatformGlyphs {
    /** `.` is transparent; X, W, S and A respectively mean ink, body, shade and accent. */
    val bySlug: Map<String, List<String>> = mapOf(
        "atari2600" to home("..XXXXXXXXXXXXXX....", "..XWWWWWWWWWWWWX....", ".XWSWSWSWSWSWSWX...", ".XWWWWWWWWWWWWWWX...", ".XWAAAAAAWWAAAAWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "colecovision" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWWWWWWWWWWWWX...", ".XWAAWAAWWAAWAAWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "nes" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWAAWWWWWWAAWWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "snes" to home("...XXXXXXXXXXXX....", ".XXWWWWWWWWWWWWXX..", ".XWSSSSSSSSSSSSWX..", ".XWWWWWWWWWWWWWWX..", ".XWWAAWWWWWWAAWWX..", ".XXXXXXXXXXXXXXXXXX.."),
        "sgb" to handheld("...XXXXXXXXXXXX....", "..XWWWWWWWWWWWWX...", "..XWSSSSSSSSSSSWX...", "..XWWWWWWWWWWWWX...", "..XWAAXWWXAAWWX...", "...XXXXXXXXXXXX...."),
        "arcade" to glyph(".......XXXXXX.......", "......XWWWWWWX......", "......XWSSSSWX......", ".....XWWAAWWWWX.....", "....XWWWWWWWWWWX....", "...XWXXXXXXXXXXWX...", "...XXXXXXXXXXXXXX..."),
        "laseractive" to disc("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWXXXAAAXXXWWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "pceld" to disc("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWXXXAAAAAXXXWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "n64" to home("....XXXXXXXXXXXX....", "..XXWWWWWWWWWWWWXX..", ".XWWWWWWWWWWWWWWWWX.", ".XWSSSSSWWWWSSSSSWX.", ".XWWAAWWWWWWWWAAWWX.", "..XXXXXXXXXXXXXXXX.."),
        "gb" to handheld("....XXXXXXXXXXXX....", "...XWWWWWWWWWWWWX...", "...XWSSSSSSSSSSWX...", "...XWWWWWWWWWWWWX...", "...XWAAWWWWWWAAWX...", "....XXXXXXXXXXXX...."),
        "gbc" to handheld("....XXXXXXXXXXXX....", "...XWWWWWWWWWWWWX...", "...XWSSSSSSSSSSWX...", "...XWWWWAAWWWWWWX...", "...XWWWWWWWWAAWWX...", "....XXXXXXXXXXXX...."),
        "gba" to handheld(".XXXXXXXXXXXXXXXXXX.", "XWWWWWWWWWWWWWWWWWWX", "XWSSSSSSSSSSSSSSSSWX", "XWWWWWWAAAAWWWWWWWWX", "XWWAAWWWWWWWWWWAAWWX", ".XXXXXXXXXXXXXXXXXX."),
        "sms" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWWAAAWWAAAWWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "genesis" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWXXXXXXWWWWWX...", ".XWWAAWWWWWWAAWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "sega32" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWXXXXXXWWWWWX...", ".XWAAAAWWWWAAAAWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "gamegear" to handheld(".XXXXXXXXXXXXXXXXXX.", "XWWWWWWWWWWWWWWWWWWX", "XWSSSSSSSSSSSSSSSSWX", "XWWWWWWWWWWWWWWWWWWX", "XWAAWWWWWWWWWWAAWWX", ".XXXXXXXXXXXXXXXXXX."),
        "segacd" to disc("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWWXAAAAAXWWWWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "psx" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWAAWWWWWWAAWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "neogeomvs" to home(".XXXXXXXXXXXXXXXXXX.", "XWWWWWWWWWWWWWWWWWWX", "XWSSSSSSSSSSSSSSSSWX", "XWWWWWWWWWAAWWWWWWWX", "XWAAWWWWWWWWWWWWAAWX", ".XXXXXXXXXXXXXXXXXX."),
        "neo-geo-cd" to disc(".XXXXXXXXXXXXXXXXXX.", "XWWWWWWWWWWWWWWWWWWX", "XWSSSSSSSSSSSSSSSSWX", "XWWWXXXXAAAAXXXXWWWX", "XWWWWWWWWWWWWWWWWWWX", ".XXXXXXXXXXXXXXXXXX."),
        "neo-geo-pocket" to handheld("....XXXXXXXXXXXX....", "...XWWWWWWWWWWWWX...", "...XWSSSSSSSSSSWX...", "...XWWWWWWWWWWWWX...", "...XWAAWWWWWWAAWX...", "....XXXXXXXXXXXX...."),
        "neo-geo-pocket-color" to handheld("....XXXXXXXXXXXX....", "...XWWWWWWWWWWWWX...", "...XWSSSSSSSSSSWX...", "...XWWWWAAWWAAWWX...", "...XWWWWWWWWWWWWX...", "....XXXXXXXXXXXX...."),
        "zx-spectrum" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWAAAAAAAAAAAAAWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "pce" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWWWWAAWWWWWWX...", ".XWAAWWWWWWWWAAWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "pcecd" to disc("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWXXXAAAXXXWWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "supergrafx" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWAAAAWWAAAAWWX...", ".XWSSSSSSSSSSSSWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "wonderswan" to handheld("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWWWWWWWWWWWWWX...", ".XWAAWWWWWWWWAAWX...", "..XXXXXXXXXXXXXXXX.."),
        "wonderswan-color" to handheld("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWWAAWWWWWWAAWWX...", ".XWWWWWWWWWWWWWWX...", "..XXXXXXXXXXXXXXXX.."),
        "msx" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWAAAAAAAAAAAAAWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
        "msx2" to home("..XXXXXXXXXXXXXXXX..", ".XWWWWWWWWWWWWWWX...", ".XWSSSSSSSSSSSSWX...", ".XWAAAAWWWWAAAAWX...", ".XWWWWWWWWWWWWWWX...", ".XXXXXXXXXXXXXXXXXX..."),
    )

    private fun home(vararg rows: String): List<String> = glyph(*rows)

    private fun handheld(vararg rows: String): List<String> = glyph(*rows)

    private fun disc(vararg rows: String): List<String> = glyph(*rows)

    /** Gives hand-drawn rows a consistent transparent right edge. */
    private fun glyph(vararg rows: String): List<String> {
        val width = rows.maxOf(String::length)
        return rows.map { it.padEnd(width, '.') }
    }
}
