package com.phobos.emulator.input

/**
 * Where a controller setting applies. A game is its system and ROM file name, the pair its save
 * states go by; [AllConsoles] is the global settings.
 */
sealed class ControlLevel {
    data object AllConsoles : ControlLevel()
    data class Console(val system: String) : ControlLevel()
    data class Game(val system: String, val rom: String) : ControlLevel()

    /** The key the level's settings are stored under; null for all consoles. */
    val scope: String?
        get() = when (this) {
            AllConsoles -> null
            is Console -> system
            is Game -> "$system/$rom"
        }

    /** The level this one inherits from. */
    val parent: ControlLevel?
        get() = when (this) {
            AllConsoles -> null
            is Console -> AllConsoles
            is Game -> Console(system)
        }
}

/**
 * What console and game levels change, by [ControlLevel.scope]: button bindings ("k:<keycode>" or
 * "a:<axis>:<dir>", as the global ones) and hotkeys, each action's bindings ([HotkeyBindings]) as a
 * whole. An empty binding, or an action with none, unbinds at that level; a missing one inherits.
 */
data class ControlOverrides(
    val mappings: Map<String, Map<Int, String>> = emptyMap(),
    val hotkeys: Map<String, Map<String, List<List<Int>>>> = emptyMap(),
) {
    /** Whether a console or game [level] changes any button. */
    fun hasMappings(level: ControlLevel) = level.scope?.let { !mappings[it].isNullOrEmpty() } == true
    fun hasHotkeys(level: ControlLevel) = level.scope?.let { !hotkeys[it].isNullOrEmpty() } == true

    /** Whether a console or game [level] itself binds or unbinds [bit]. */
    fun changesMapping(level: ControlLevel, bit: Int) = level.scope?.let { mappings[it]?.containsKey(bit) } == true
    fun changesHotkey(level: ControlLevel, action: String) = level.scope?.let { hotkeys[it]?.containsKey(action) } == true
}

/** The button bindings and hotkeys in effect at a level. */
data class ActiveControls(val mappings: Map<Int, String>, val hotkeys: Map<String, List<List<Int>>>) {
    companion object {
        fun at(level: ControlLevel, mappings: Map<Int, String>, hotkeys: Map<String, List<List<Int>>>, overrides: ControlOverrides) =
            ActiveControls(Controls.mappings(level, mappings, overrides), Controls.hotkeys(level, hotkeys, overrides))
    }
}

/** A level's controls: its own changes over its console's, over everyone's. */
object Controls {
    /** The core's "bit\tname" entries as the console's button names per pad bit, " + " joining the buttons one bit presses. */
    fun buttonNames(entries: List<String>): Map<Int, String> =
        entries.mapNotNull { entry ->
            val tab = entry.indexOf('\t')
            val bit = if (tab > 0) entry.substring(0, tab).toIntOrNull() else null
            if (bit != null && tab < entry.length - 1) bit to entry.substring(tab + 1) else null
        }.groupBy({ it.first }, { it.second }).mapValues { (_, names) -> names.distinct().joinToString(" + ") }

    // All consoles first, the level last.
    private fun chain(level: ControlLevel): List<ControlLevel> = generateSequence(level) { it.parent }.toList().asReversed()

    fun mappings(level: ControlLevel, global: Map<Int, String>, overrides: ControlOverrides): Map<Int, String> =
        resolve(level, global) { overrides.mappings[it] }.mapNotNull { (bit, binding) -> binding.takeIf { it.isNotEmpty() }?.let { bit to it } }.toMap()

    fun hotkeys(level: ControlLevel, global: Map<String, List<List<Int>>>, overrides: ControlOverrides): Map<String, List<List<Int>>> =
        resolve(level, global) { overrides.hotkeys[it] }.filterValues { bindings -> bindings.any { it.isNotEmpty() } }

    private fun <K, V> resolve(level: ControlLevel, global: Map<K, V>, changes: (String) -> Map<K, V>?): Map<K, V> {
        val result = global.toMutableMap()
        for (l in chain(level)) l.scope?.let(changes)?.let(result::putAll)
        return result
    }

    /** The level that decides [bit] at [level] (binds or unbinds it), or null when none does. */
    fun mappingSource(level: ControlLevel, bit: Int, global: Map<Int, String>, overrides: ControlOverrides): ControlLevel? =
        source(level, global.containsKey(bit)) { overrides.mappings[it]?.containsKey(bit) == true }

    /** The level that decides [action] at [level] (binds or unbinds it), or null when none does. */
    fun hotkeySource(level: ControlLevel, action: String, global: Map<String, List<List<Int>>>, overrides: ControlOverrides): ControlLevel? =
        source(level, global.containsKey(action)) { overrides.hotkeys[it]?.containsKey(action) == true }

    private fun source(level: ControlLevel, inGlobal: Boolean, decidesAt: (String) -> Boolean): ControlLevel? {
        var l: ControlLevel? = level
        while (l != null) {
            val scope = l.scope ?: return if (inGlobal) l else null
            if (decidesAt(scope)) return l
            l = l.parent
        }
        return null
    }

    /**
     * What binding [bit] to [binding] at a console or game [level] writes there: like Settings, a
     * button already using [binding] at that level takes [bit]'s old binding (or none). A value equal
     * to what the level inherits is stored as null, inherit, so the level keeps only what it changes.
     */
    fun bindButton(level: ControlLevel, bit: Int, binding: String, global: Map<Int, String>, overrides: ControlOverrides): Map<Int, String?> {
        val current = mappings(level, global, overrides)
        val changes = mutableMapOf<Int, String>(bit to binding)
        current.forEach { (other, b) -> if (other != bit && b == binding) changes[other] = current[bit] ?: "" }
        return keepOnlyChanges(level, changes, global, overrides)
    }

    /** What unbinding [bit] at a console or game [level] writes there. */
    fun unbindButton(level: ControlLevel, bit: Int, global: Map<Int, String>, overrides: ControlOverrides): Map<Int, String?> =
        keepOnlyChanges(level, mapOf(bit to ""), global, overrides)

    private fun keepOnlyChanges(level: ControlLevel, changes: Map<Int, String>, global: Map<Int, String>, overrides: ControlOverrides): Map<Int, String?> {
        val inherited = level.parent?.let { mappings(it, global, overrides) }.orEmpty()
        return changes.mapValues { (bit, value) -> value.takeIf { it != (inherited[bit] ?: "") } }
    }

    /** What setting [action]'s [bindings] (none to unbind) at a console or game [level] writes there; null inherits. */
    fun hotkeyChange(
        level: ControlLevel, action: String, bindings: List<List<Int>>, global: Map<String, List<List<Int>>>, overrides: ControlOverrides,
    ): List<List<Int>>? {
        val inherited = level.parent?.let { hotkeys(it, global, overrides) }.orEmpty()
        return bindings.takeIf { it != inherited[action].orEmpty() }
    }

    /**
     * The actions binding [action]'s [slot] to [combo] (empty: clearing it) at [level] changes, each with its bindings
     * as the level then has them: [action]'s, and those of the [overwrite] bindings (action and slot), which are
     * cleared. An action the level inherits comes whole, so overwriting an inherited binding leaves the level the
     * action's others as its own.
     */
    fun bindHotkey(
        level: ControlLevel, action: String, slot: Int, combo: List<Int>, overwrite: List<Pair<String, Int>>,
        global: Map<String, List<List<Int>>>, overrides: ControlOverrides,
    ): Map<String, List<List<Int>>> {
        val current = hotkeys(level, global, overrides)
        val changes = linkedMapOf<String, List<List<Int>>>()
        fun bindings(of: String) = changes[of] ?: current[of].orEmpty()
        changes[action] = HotkeyBindings.with(bindings(action), slot, combo)
        for ((other, otherSlot) in overwrite) changes[other] = HotkeyBindings.with(bindings(other), otherSlot, emptyList())
        return changes
    }
}

/** How console and game settings are named in storage: "<prefix><scope>|<bit or action>". */
object ScopedKeys {
    const val MAPPING_PREFIX = "scoped_mapping|"
    const val HOTKEY_PREFIX = "scoped_hotkey|"

    fun mapping(scope: String, bit: Int) = "$MAPPING_PREFIX$scope|$bit"
    fun hotkey(scope: String, action: String) = "$HOTKEY_PREFIX$scope|$action"

    /** The scope and bit or action a key names, or null for any other key. ROM names may contain "|". */
    fun parse(name: String, prefix: String): Pair<String, String>? {
        if (!name.startsWith(prefix)) return null
        val rest = name.substring(prefix.length)
        val cut = rest.lastIndexOf('|')
        return if (cut > 0 && cut < rest.length - 1) rest.substring(0, cut) to rest.substring(cut + 1) else null
    }
}
