package com.phobos.emulator.art

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The pure parts of [LabelPicture]: the downscale decision and the round-UMD mask geometry. */
class LabelPictureTest {

    @Test
    fun labelsLargerThanTheKindWidthAreDownscaledKeepingTheAspect() {
        assertEquals(2048 to 512, LabelPicture.scaledSize(4096, 1024, 2048))
        assertEquals(1024 to 512, LabelPicture.scaledSize(2048, 1024, 1024))
        assertEquals(256 to 1024, LabelPicture.scaledSize(512, 2048, 1024))
    }

    @Test
    fun labelsWithinTheWidthKeepTheirSize() {
        assertEquals(800 to 400, LabelPicture.scaledSize(800, 400, 1024))
        assertEquals(1024 to 1024, LabelPicture.scaledSize(1024, 1024, 1024))
        assertEquals(1 to 1, LabelPicture.scaledSize(1, 1, 1024))
    }

    @Test
    fun theRoundMaskKeepsTheCentreAndClearsTheCorners() {
        val w = 64
        val h = 64
        assertTrue(LabelPicture.inCircle(w / 2, h / 2, w, h))
        assertTrue(LabelPicture.inCircle(w / 2, 0, w, h))
        assertFalse(LabelPicture.inCircle(0, 0, w, h))
        assertFalse(LabelPicture.inCircle(w - 1, h - 1, w, h))
        assertTrue(LabelPicture.inCircle(8, 8, w, h).not())
    }

    @Test
    fun onlyASquareScanWithBackgroundCornersIsTreatedAsRound() {
        val w = 32
        val h = 32
        // A disc on transparent corners: opaque inside radius 12, transparent outside.
        val disc = ByteArray(w * h * 4)
        for (y in 0 until h) for (x in 0 until w) {
            val dx = x - w / 2.0
            val dy = y - h / 2.0
            val a = if (dx * dx + dy * dy <= 12.0 * 12.0) 255 else 0
            disc[(y * w + x) * 4 + 3] = a.toByte()
        }
        val box = LabelCrop.Rect(0, 0, w, h)
        assertTrue(LabelPicture.isRoundLabel("umd", disc, w, h, box))

        // The same scan for a square cart shape is not round.
        assertFalse(LabelPicture.isRoundLabel("gb", disc, w, h, box))

        // An opaque square scan whose probes fall on ink is not round: Cartridge's background test compares each probe
        // with the pixel at (2, 2), so a scan with ink at the probes is not background (a uniform scan counts as
        // background everywhere - accepted, it is not a shape Cartridge sees in practice).
        val square = ByteArray(w * h * 4)
        for (i in 0 until w * h) {
            square[i * 4] = 200.toByte()
            square[i * 4 + 1] = 200.toByte()
            square[i * 4 + 2] = 200.toByte()
            square[i * 4 + 3] = 255.toByte()
        }
        val ref = (2 * w + 2) * 4
        square[ref] = 10
        square[ref + 1] = 10
        square[ref + 2] = 10
        assertFalse(LabelPicture.isRoundLabel("umd", square, w, h, box))
    }
}
