package com.phobos.emulator.art

import android.content.Context
import android.util.Log
import java.util.zip.GZIPInputStream
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The bundled LaunchBox label index, loaded ONCE per process from `assets/carts/labels.json.gz`.
 *
 * The RP6 keeps this flat structure for the life of the process: the gzipped asset is inflated, parsed into
 * [LabelMatch.Index]'s arrays, and the inflated JSON is dropped when [load] returns. Nothing re-parses per request,
 * per game or per screen - callers just [load] again and get the same instance.
 *
 * The bytes are the Cartridge 0.4.9.3 index (sha256 f7f1e8bb...), bundled by the owner's decision; see
 * docs/third-party/cartridge.md.
 *
 * The fallback is OBSERVABLE: [state] carries the load outcome and the reason, the failure is logged, and the Carts
 * settings page shows "labels on but unavailable" with that reason, so a user who enabled real labels and sees box
 * art can tell a fallback from a bug.
 */
object BundledLabelIndex {

    const val ASSET = "carts/labels.json.gz"

    sealed interface State {
        data object NotLoaded : State
        data class Ready(val entries: Int, val inflatedBytes: Int, val loadMs: Long) : State
        data class Unavailable(val reason: String) : State
    }

    private val _state = MutableStateFlow<State>(State.NotLoaded)
    val state: StateFlow<State> = _state

    @Volatile
    private var cached: LabelMatch.Index? = null

    /** Loads the index once; null when the asset is missing or unreadable (labels then fall back to box art). */
    fun load(context: Context, log: (String) -> Unit = { Log.w("Phobos", it) }): LabelMatch.Index? {
        cached?.let { return it }
        synchronized(this) {
            cached?.let { return it }
            return try {
                val started = System.nanoTime()
                val bytes = GZIPInputStream(context.assets.open(ASSET)).use { it.readBytes() }
                val inflated = bytes.size
                val index = LabelMatch.parse(bytes.toString(Charsets.UTF_8))
                val ms = (System.nanoTime() - started) / 1_000_000
                log("Artwork: label index $inflated B JSON parsed in $ms ms (${index.entries} entries)")
                cached = index
                _state.value = State.Ready(index.entries, inflated, ms)
                index
            } catch (e: Exception) {
                val reason = "${e.javaClass.simpleName}: ${e.message ?: "unreadable"}"
                log("Artwork: label index unavailable ($reason); labels fall back to box art")
                _state.value = State.Unavailable(reason)
                null
            }
        }
    }
}
