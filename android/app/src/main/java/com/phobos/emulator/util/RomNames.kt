package com.phobos.emulator.util

private val extension = Regex("""\.[A-Za-z0-9]{1,5}$""")

/**
 * A ROM file's name without its extension ("Knuckles' Chaotix (USA).zip" → "Knuckles' Chaotix
 * (USA)"). Only a short run of letters and digits after the last dot counts, so a title such as
 * "Dr. Mario" keeps its dot.
 */
fun romTitle(fileName: String): String = fileName.replace(extension, "").ifEmpty { fileName }

private val discNumber = Regex("""\s*[(\[]\s*(?:disc|disk|cd)\s*\d+(?:\s*of\s*\d+)?\s*[)\]]""", RegexOption.IGNORE_CASE)

/**
 * A game's title without its disc number ("Metal Gear Solid (USA) (Disc 1) (Rev 1)" → "Metal Gear
 * Solid (USA) (Rev 1)"), so all of a game's discs share one memory card.
 */
fun withoutDiscNumber(title: String): String = title.replace(discNumber, "").trim().ifEmpty { title }
