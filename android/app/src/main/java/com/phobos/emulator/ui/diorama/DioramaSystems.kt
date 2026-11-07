package com.phobos.emulator.ui.diorama

/**
 * One selectable model for a system. The first variant in [DioramaRecord.variants] is the default; the twins the
 * model team is adding (the Japanese SFC/Famicom chassis beside today's US SNES/NES ones) become extra rows, never
 * new code.
 */
internal data class DioramaVariant(val id: String, val label: String, val asset: String)

/** The baked models a system can show: [system] as the app names it, [variants] with the default first. */
internal data class DioramaRecord(val system: String, val variants: List<DioramaVariant>) {
    init {
        require(variants.isNotEmpty()) { "a diorama record needs at least one variant: $system" }
    }
}

/**
 * The systems that have a baked console diorama, as a data table: adding the next system (or a second chassis) is
 * one row here plus its generated GLB, not new code. Asset paths are relative to the APK's assets.
 */
internal object DioramaSystems {

    private val records = listOf(
        DioramaRecord(
            system = "Super Famicom",
            variants = listOf(DioramaVariant(id = "snes", label = "SNES", asset = "carts/consoles/snes.glb")),
        ),
        DioramaRecord(
            system = "Famicom",
            variants = listOf(DioramaVariant(id = "nes", label = "NES", asset = "carts/consoles/nes.glb")),
        ),
        DioramaRecord(
            system = "Sega Mega Drive",
            variants = listOf(DioramaVariant(id = "megadrive", label = "Mega Drive", asset = "carts/consoles/megadrive.glb")),
        ),
    ).associateBy { it.system }

    /** The row for [system], or null when the system has no diorama (the hero is simply absent). */
    fun recordFor(system: String): DioramaRecord? = records[system]

    /**
     * The variant to render: the one the user stored for the system when that id is known, otherwise the record's
     * default (its first variant). An unknown system stays null.
     */
    fun variantFor(system: String, selectedId: String?): DioramaVariant? {
        val record = records[system] ?: return null
        return record.variants.firstOrNull { it.id == selectedId } ?: record.variants.first()
    }

    /** The GLB path for [system]'s selected (or default) variant, or null when it has no diorama. */
    fun assetFor(system: String, selectedId: String?): String? = variantFor(system, selectedId)?.asset

    /** Every system with a diorama, in table order; used by tests and diagnostics. */
    fun systems(): Set<String> = records.keys
}
