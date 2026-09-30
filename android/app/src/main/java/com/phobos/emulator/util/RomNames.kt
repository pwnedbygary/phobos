package com.phobos.emulator.util

private val extension = Regex("""\.[A-Za-z0-9]{1,5}$""")

/**
 * A ROM file's name without its extension ("Knuckles' Chaotix (USA).zip" → "Knuckles' Chaotix
 * (USA)"). Only a short run of letters and digits after the last dot counts, so a title such as
 * "Dr. Mario" keeps its dot.
 */
fun romTitle(fileName: String): String = fileName.replace(extension, "").ifEmpty { fileName }
