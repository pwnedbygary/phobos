package com.phobos.emulator.ui.carts

import android.content.Context
import android.graphics.Typeface
import androidx.core.content.res.ResourcesCompat
import com.phobos.emulator.R

/**
 * Resolves the typeface the generated cart label is drawn with.
 *
 * Labels never use a system font: every face here is a bundled `res/font` file the app already ships for its
 * themes (Crt.kt: vt323; Manga.kt: comic_neue_regular/bold; Pixel.kt: galmuri11; Rpg.kt: dotgothic16;
 * Xmb.kt: mplus1). The default Material3 typography (PhobosTypography in ui/theme/Type.kt) deliberately has no
 * bundled body face, so the default style borrows Xmb's mplus1 - already shipped, no new asset, and no per-device
 * substitution. The per-face licence entries are in the repository LICENSE with their copyright lines and the
 * OFL-1.1 text; LicenseNoticesTest asserts them in the licences step.
 *
 * [style] is the name the wiring passes from the app's font setting: "crt", "manga", "pixel", "rpg", "xmb", or
 * null/anything else for the default. The mapping is pure and unit-tested; [typeface] falls back to mplus1 if a
 * resource somehow fails to load.
 */
object CartFaceResolver {

    enum class Face(val fontRes: Int) {
        VT323(R.font.vt323),
        MANGA(R.font.comic_neue_regular),
        GALMURI(R.font.galmuri11),
        DOTGOTHIC(R.font.dotgothic16),
        MPLUS1(R.font.mplus1),
    }

    fun face(style: String?): Face = when (style?.lowercase()) {
        "crt" -> Face.VT323
        "manga" -> Face.MANGA
        "pixel" -> Face.GALMURI
        "rpg" -> Face.DOTGOTHIC
        "xmb" -> Face.MPLUS1
        else -> Face.MPLUS1 // the default Material3 style: borrowed from Xmb, never the system font
    }

    fun typeface(context: Context, style: String?): Typeface {
        val face = face(style)
        return ResourcesCompat.getFont(context, face.fontRes)
            ?: ResourcesCompat.getFont(context, Face.MPLUS1.fontRes)
            ?: Typeface.DEFAULT // unreachable with the bundled fonts; kept so a bad resource cannot crash
    }
}
