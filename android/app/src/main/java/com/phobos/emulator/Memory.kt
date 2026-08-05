package com.phobos.emulator

object Memory {
    /**
     * Mirrors an address within a given size.
     * Ported from ares/ares/memory/memory.hpp
     */
    fun mirror(address: Int, size: Int): Int {
        if (size == 0) return 0
        var addr = address.toUInt()
        var sz = size.toUInt()
        var base = 0u
        var mask = 1u shl 31
        while (addr >= sz) {
            while (addr and mask == 0u) mask = mask shr 1
            addr -= mask
            if (sz > mask) {
                sz -= mask
                base += mask
            }
            mask = mask shr 1
        }
        return (base + addr).toInt()
    }

    /**
     * Reduces an address based on a bitmask.
     * Ported from ares/ares/memory/memory.hpp
     */
    fun reduce(address: Int, mask: Int): Int {
        var addr = address.toUInt()
        var m = mask.toUInt()
        while (m != 0u) {
            val bits = (m and (0u - m)) - 1u
            addr = (addr shr 1 and bits.inv()) or (addr and bits)
            m = (m and (m - 1u)) shr 1
        }
        return addr.toInt()
    }
}
