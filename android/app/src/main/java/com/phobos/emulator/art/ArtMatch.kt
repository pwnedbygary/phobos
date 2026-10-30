// Ported from Cartridge 0.4.9.3 (MIT; docs/third-party/cartridge.md): the match key and tag reading from
// electron/titles.js (matchKey, parseTags), and the listing parser, sanitize, bigrams, dice, pickBest and match from
// electron/scraper.js. Changed here: a fuzzy match must have the game's numbers (see matchArt).
package com.phobos.emulator.art

import com.phobos.emulator.data.RegionPreference
import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.text.Normalizer
import kotlin.math.abs
import kotlin.math.max

/** A kind of picture: its folder on libretro's thumbnail server, the width it's kept at, and whether it's a PNG. */
enum class ArtKind(val folder: String, val width: Int, val png: Boolean) {
    BOX("Named_Boxarts", 520, false),
    SNAP("Named_Snaps", 640, false),
    LOGO("Named_Logos", 480, true),
}

private val regions = listOf(
    "USA" to "US", "World" to "W", "Europe" to "EU", "Japan" to "JP", "Australia" to "AU", "Korea" to "KR",
    "Germany" to "DE", "France" to "FR", "Spain" to "ES", "Italy" to "IT", "Brazil" to "BR", "China" to "CN",
    "Asia" to "AS", "Canada" to "CA", "Netherlands" to "NL", "Sweden" to "SE",
)
private val tagSeparator = Regex(",\\s*")
private val flagTag = Regex("^(Beta|Proto|Demo|Sample|Hack|Unl|Pirate|Kiosk)", RegexOption.IGNORE_CASE)
private val revisionTag = Regex("^Rev\\s", RegexOption.IGNORE_CASE)

/** What a name's tags say: its [region] code (US, EU, JP, W ...) and how many [flags] (Beta, Proto, Rev ...) it has. */
internal class ArtTags(val region: String?, val flags: Int)

internal fun parseTags(tags: List<String>): ArtTags {
    var region: String? = null
    var flags = 0
    for (raw in tags) {
        val tag = raw.trim()
        if (region == null) {
            val parts = tag.split(tagSeparator)
            region = regions.firstOrNull { it.first in parts }?.second
        }
        if (flagTag.containsMatchIn(tag)) flags++
        if (revisionTag.containsMatchIn(tag)) flags++
    }
    return ArtTags(region, flags)
}

// JavaScript's \b counts only ASCII letters, digits and _ as word characters; Java's counts any letter.
private const val NOT_WORD_AFTER = "(?![A-Za-z0-9_])"
private const val NOT_WORD_BEFORE = "(?<![A-Za-z0-9_])"
private val keyVersion = Regex("\\s+v\\d+(\\.\\d+)+$", RegexOption.IGNORE_CASE)
private val keyExtension = Regex("\\.[a-z0-9]{1,4}$", RegexOption.IGNORE_CASE)
private val keyTags = Regex("\\s*[(\\[][^)\\]]*[)\\]]")
private val keyVersionAfterTags = Regex("\\s+v\\d+(\\.\\d+)+\\s*$", RegexOption.IGNORE_CASE)
private val keyDumpWords = Regex("\\s+(decrypted|encrypted|trimmed)\\s*$", RegexOption.IGNORE_CASE)
private val keyArticle = Regex("^(.+?), (the|a|an)$NOT_WORD_AFTER", RegexOption.IGNORE_CASE)
private val keyMarks = Regex("[\\u0300-\\u036f]")
private val keyEditionWords = Regex("$NOT_WORD_BEFORE(version|edition)$NOT_WORD_AFTER")
private val keyOther = Regex("[^a-z0-9]")

/**
 * The name [name] is matched by when it isn't on the server exactly: lower case letters and digits only, without its
 * extension, tags, version, dump words or "version"/"edition", its trailing article moved back to the front
 * ("Legend of Zelda, The (USA)" → "thelegendofzelda").
 */
fun artMatchKey(name: String): String {
    val plain = name
        .replace(keyVersion, "")
        .replace(keyExtension, "")
        .replace(keyTags, "")
        .replace(keyVersionAfterTags, "")
        .replace(keyDumpWords, "")
        .replace(keyArticle, "$2 $1")
        .lowercase()
    return Normalizer.normalize(plain, Normalizer.Form.NFKD)
        .replace(keyMarks, "")
        .replace("&", "and")
        .replace(keyEditionWords, "")
        .replace(keyOther, "")
}

private val unsafe = Regex("[&*/:`<>?\\\\|\"]")

/** [name] as the server names its picture: each character a file name can't hold becomes "_". */
fun sanitizeArtName(name: String): String = name.replace(unsafe, "_")

private val listingLink = Regex("href=\"([^\"?/][^\"]*?)\\.png\"")

/**
 * The picture names in one of the server's folder listings (an Apache index page): each link to a .png, decoded. A
 * page that holds none is no listing at all, which the caller treats as a failure rather than as a folder of nothing.
 */
fun parseArtListing(html: String): List<String> =
    listingLink.findAll(html).mapNotNull { decodeUriComponent(it.groupValues[1]) }.toList()

/** JavaScript's decodeURIComponent: null where it would throw (a bad escape, or bytes that aren't UTF-8). */
internal fun decodeUriComponent(text: String): String? {
    if ('%' !in text) return text
    val out = StringBuilder()
    var i = 0
    while (i < text.length) {
        if (text[i] != '%') {
            out.append(text[i++])
            continue
        }
        val bytes = ByteArrayOutputStream()
        while (i < text.length && text[i] == '%') {
            if (i + 3 > text.length) return null
            val byte = text.substring(i + 1, i + 3).takeIf { s -> s.all { it.isHexDigit() } }?.toInt(16) ?: return null
            bytes.write(byte)
            i += 3
        }
        val decoder = Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
        try {
            out.append(decoder.decode(ByteBuffer.wrap(bytes.toByteArray())))
        } catch (_: CharacterCodingException) {
            return null
        }
    }
    return out.toString()
}

private fun Char.isHexDigit() = this in '0'..'9' || this in 'a'..'f' || this in 'A'..'F'

/** One of the server's folders for one kind of picture, as its listing named them, ready to match against. */
class ArtListing(val names: List<String>) {
    internal val exact: Set<String> = names.toHashSet()
    internal val byKey: Map<String, List<String>> = LinkedHashMap<String, MutableList<String>>().also { map ->
        for (name in names) map.getOrPut(artMatchKey(name)) { mutableListOf() }.add(name)
    }
    internal val grams: List<Pair<String, Set<String>>> by lazy { byKey.keys.map { it to bigrams(it) } }
}

private fun bigrams(text: String): Set<String> {
    val out = LinkedHashSet<String>()
    for (i in 0 until text.length - 1) out.add(text.substring(i, i + 2))
    return out
}

private fun dice(a: Set<String>, b: Set<String>): Double {
    if (a.isEmpty() || b.isEmpty()) return 0.0
    val shared = a.count { it in b }
    return 2.0 * shared / (a.size + b.size)
}

private val digits = Regex("\\d+")

/** The numbers in a match key, as numbers ("007" and "7" are one), in order. */
private fun numbersIn(key: String): List<String> =
    digits.findAll(key).map { it.value.trimStart('0').ifEmpty { "0" } }.toList()

private val parenthesised = Regex("\\(([^)]*)\\)")

/**
 * The best of [candidates], names that match one game equally: the one whose region comes first in [regionOrder], then
 * the one with fewer flags (Beta, Proto, Rev ...) and tags. The first such in the listing wins a tie.
 */
internal fun pickBest(candidates: List<String>, regionOrder: List<String>): String = candidates.minBy { name ->
    val tags = parenthesised.findAll(name).map { it.groupValues[1] }.toList()
    val info = parseTags(tags)
    val r = regionOrder.indexOf(info.region)
    (if (r < 0) 50 else r * 10) + info.flags * 3 + tags.size
}

/**
 * The picture in [listing] for the game called [base] (its file's name without the extension), or null: the name
 * itself, else the names with its match key, else ([fuzzy]) the names whose key shares at least 84% of the key's
 * letter pairs (Dice) and is about as long, as Cartridge matches. Among several, [regionOrder] picks ([pickBest]).
 *
 * Unlike Cartridge's, a fuzzy match must have the same numbers as the game's key: Cartridge takes "Metal Slug 3" for
 * "Metal Slug" and "The King of Fighters '94" for "'98", as checked against the server's listings on 2026-10-28.
 */
fun matchArt(listing: ArtListing, base: String, regionOrder: List<String>, fuzzy: Boolean = true): String? {
    if (listing.names.isEmpty()) return null
    val sanitized = sanitizeArtName(base)
    if (sanitized in listing.exact) return sanitized
    val key = artMatchKey(base)
    if (key.isEmpty()) return null
    listing.byKey[key]?.let { return pickBest(it, regionOrder) }
    if (!fuzzy) return null
    val grams = bigrams(key)
    var best: String? = null
    var bestScore = 0.0
    for ((candidate, candidateGrams) in listing.grams) {
        if (abs(candidate.length - key.length) > max(6.0, key.length * 0.4)) continue
        val score = dice(grams, candidateGrams)
        if (score > bestScore) {
            bestScore = score
            best = candidate
        }
    }
    if (best == null || bestScore < 0.84 || numbersIn(best) != numbersIn(key)) return null
    return pickBest(listing.byKey.getValue(best), regionOrder)
}

/**
 * The region order pictures are picked in, from the console region order Phobos already has: NTSC-U is the USA's,
 * NTSC-J Japan's and PAL Europe's, with World's second, after the first choice.
 */
fun artRegionOrder(preference: RegionPreference): List<String> {
    val order = when (preference) {
        RegionPreference.NTSC_U_NTSC_J_PAL -> listOf("US", "JP", "EU")
        RegionPreference.NTSC_U_PAL_NTSC_J -> listOf("US", "EU", "JP")
        RegionPreference.NTSC_J_NTSC_U_PAL -> listOf("JP", "US", "EU")
        RegionPreference.NTSC_J_PAL_NTSC_U -> listOf("JP", "EU", "US")
        RegionPreference.PAL_NTSC_U_NTSC_J -> listOf("EU", "US", "JP")
        RegionPreference.PAL_NTSC_J_NTSC_U -> listOf("EU", "JP", "US")
    }
    return listOf(order[0], "W") + order.drop(1)
}
