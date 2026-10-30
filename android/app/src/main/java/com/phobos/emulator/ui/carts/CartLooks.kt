package com.phobos.emulator.ui.carts

import org.json.JSONObject

/**
 * Stored cart looks: "s:<system>" for a whole system, "g:<id>" for one game, each "finish:color" as
 * `finish|<rrggbb>`. One JSON object in settings (and later the game record), so a look can be set for a system or
 * for a single game; [LookResolver.resolve] applies per-game over special editions over per-system.
 */
object CartLooks {

    const val FINISH_SEPARATOR = '|'

    fun encode(look: LookResolver.Look): String = look.finish + FINISH_SEPARATOR + look.color

    fun decode(value: String?): LookResolver.Look? {
        if (value.isNullOrEmpty()) return null
        val at = value.indexOf(FINISH_SEPARATOR)
        if (at <= 0 || at == value.length - 1) return null
        val finish = value.substring(0, at)
        val color = value.substring(at + 1)
        if (finish !in LookResolver.shellFinishes.map { it.id } || !color.matches(Regex("#[0-9a-fA-F]{6}"))) return null
        return LookResolver.Look(finish, color)
    }

    /** Parses the stored JSON object into the map `resolve` takes; malformed entries are dropped, never thrown. */
    fun parse(json: String?): Map<String, LookResolver.Look> {
        if (json.isNullOrBlank()) return emptyMap()
        return runCatching {
            val obj = JSONObject(json)
            buildMap {
                for (key in obj.keys()) {
                    decode(obj.optString(key))?.let { put(key, it) }
                }
            }
        }.getOrDefault(emptyMap())
    }

    /** Adds or removes one entry, returning the new JSON (an empty look removes the key). */
    fun with(json: String?, key: String, look: LookResolver.Look?): String {
        val obj = runCatching { JSONObject(json ?: "{}") }.getOrDefault(JSONObject())
        if (look == null) obj.remove(key) else obj.put(key, encode(look))
        return obj.toString()
    }

    fun systemKey(system: String) = "s:$system"

    fun gameKey(system: String, name: String) = "g:" + gameId(system, name)

    /** The same stable id the render cache uses for a game, so "g:" keys and cache keys agree. */
    fun gameId(system: String, name: String): Long = ("$system/$name").hashCode().toLong()
}
