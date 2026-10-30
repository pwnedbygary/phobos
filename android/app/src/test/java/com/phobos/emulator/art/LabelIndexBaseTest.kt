package com.phobos.emulator.art

import org.junit.Test

/**
 * The one-host gate is code, not just the bundle's current bytes: an index naming any other base is not used -
 * [LabelMatch.parse] throws and [BundledLabelIndex] turns that into its unavailable state with the reason logged.
 */
class LabelIndexBaseTest {

    @Test(expected = IllegalArgumentException::class)
    fun aForeignBaseIsRejected() {
        LabelMatch.parse("""{"base":"https://launchbox.example/","built":"2026-09-25","index":{}}""")
    }

    @Test
    fun theApprovedBaseParses() {
        LabelMatch.parse("""{"base":"https://images.launchbox-app.com/","index":{}}""")
    }
}
