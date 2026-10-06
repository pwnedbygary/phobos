package com.phobos.emulator.ui.theme

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import kotlin.math.min

/** How [PlatformGlyphIcon] paints a console silhouette from [PlatformGlyphs]. */
enum class PlatformGlyphStyle {
    PHOBOS,
    PIXEL,
    MANGA,
}

/** Draws an original console silhouette from [PlatformGlyphs] using the selected [style]. */
@Composable
fun PlatformGlyphIcon(
    slug: String,
    style: PlatformGlyphStyle,
    accent: Color,
    ink: Color,
    fill: Color,
    modifier: Modifier,
) {
    Canvas(modifier) {
        val glyph = PlatformGlyphs.bySlug[slug]
        if (glyph == null) {
            drawPhobosMark(ink, accent)
        } else {
            drawGlyph(glyph, style, accent, ink, fill)
        }
    }
}

private fun DrawScope.drawGlyph(
    rows: List<String>,
    style: PlatformGlyphStyle,
    accent: Color,
    ink: Color,
    fill: Color,
) {
    val columns = rows.first().length
    val cell = min(size.width / columns, size.height / rows.size)
    val artWidth = columns * cell
    val artHeight = rows.size * cell
    val origin = Offset((size.width - artWidth) / 2f, (size.height - artHeight) / 2f)
    if (style == PlatformGlyphStyle.PHOBOS || style == PlatformGlyphStyle.PIXEL) {
        drawShadow(rows, origin, cell, style, ink)
    }
    rows.forEachIndexed { row, cells ->
        cells.forEachIndexed { column, kind ->
            if (kind == '.') return@forEachIndexed
            val topLeft = origin + Offset(column * cell, row * cell)
            val cellSize = Size(cell, cell)
            val color = when (kind) {
                'X' -> ink
                'W' -> fill
                'S' -> fill.copy(alpha = 0.68f)
                else -> accent
            }
            when (style) {
                PlatformGlyphStyle.PIXEL -> drawRect(color, topLeft, cellSize)
                PlatformGlyphStyle.PHOBOS -> {
                    val inset = cell * 0.06f
                    drawRoundRect(
                        color = color,
                        topLeft = topLeft + Offset(inset, inset),
                        size = Size(cell - inset * 2, cell - inset * 2),
                        cornerRadius = CornerRadius(cell * 0.22f),
                    )
                }
                PlatformGlyphStyle.MANGA -> drawMangaCell(
                    kind,
                    topLeft,
                    cell,
                    color,
                    ink,
                    cellAt(rows, row - 1, column),
                    cellAt(rows, row + 1, column),
                    cellAt(rows, row, column - 1),
                    cellAt(rows, row, column + 1),
                )
            }
        }
    }
}

private fun DrawScope.drawMangaCell(
    kind: Char,
    topLeft: Offset,
    cell: Float,
    color: Color,
    ink: Color,
    above: Char,
    below: Char,
    left: Char,
    right: Char,
) {
    val cellSize = Size(cell, cell)
    when (kind) {
        'X' -> drawRect(ink, topLeft, cellSize)
        'S' -> {
            drawRect(color, topLeft, cellSize)
            val line = cell * 0.1f
            drawLine(ink.copy(alpha = 0.28f), topLeft + Offset(0f, cell), topLeft + Offset(cell, 0f), line)
        }
        else -> {
            drawRect(color, topLeft, cellSize)
            drawRect(ink, topLeft, cellSize, style = Stroke(cell * 0.11f))
        }
    }
    val edge = cell * 0.14f
    val half = edge / 2f
    if (above == '.') drawLine(ink, topLeft + Offset(0f, half), topLeft + Offset(cell, half), edge)
    if (below == '.') drawLine(ink, topLeft + Offset(0f, cell - half), topLeft + Offset(cell, cell - half), edge)
    if (left == '.') drawLine(ink, topLeft + Offset(half, 0f), topLeft + Offset(half, cell), edge)
    if (right == '.') drawLine(ink, topLeft + Offset(cell - half, 0f), topLeft + Offset(cell - half, cell), edge)
}

/** Offset copy of the silhouette behind the cells, for the PHOBOS and PIXEL packs. */
private fun DrawScope.drawShadow(
    rows: List<String>,
    origin: Offset,
    cell: Float,
    style: PlatformGlyphStyle,
    ink: Color,
) {
    val hard = style == PlatformGlyphStyle.PIXEL
    val shift = cell * (if (hard) 0.25f else 0.16f)
    val alpha = if (hard) 0.34f else 0.18f
    val shadow = ink.copy(alpha = alpha)
    rows.forEachIndexed { row, cells ->
        cells.forEachIndexed { column, kind ->
            if (kind == '.') return@forEachIndexed
            val topLeft = origin + Offset(column * cell + shift, row * cell + shift)
            if (hard) {
                drawRect(shadow, topLeft, Size(cell, cell))
            } else {
                val inset = cell * 0.06f
                drawRoundRect(
                    color = shadow,
                    topLeft = topLeft + Offset(inset, inset),
                    size = Size(cell - inset * 2, cell - inset * 2),
                    cornerRadius = CornerRadius(cell * 0.22f),
                )
            }
        }
    }
}

/** Cell at [row]/[column], or `.` outside the drawing. */
private fun cellAt(rows: List<String>, row: Int, column: Int): Char {
    if (row < 0 || row >= rows.size) return '.'
    val line = rows[row]
    if (column < 0 || column >= line.length) return '.'
    return line[column]
}

private fun DrawScope.drawPhobosMark(ink: Color, accent: Color) {
    val radius = min(size.width, size.height) * 0.4f
    val center = Offset(size.width / 2f, size.height / 2f)
    drawCircle(accent, radius, center)
    drawCircle(ink, radius, center, style = Stroke(radius * 0.16f))
    val left = center.x - radius * 0.3f
    val top = center.y - radius * 0.42f
    val stem = radius * 0.84f
    drawLine(ink, Offset(left, top), Offset(left, top + stem), radius * 0.16f)
    drawLine(ink, Offset(left, top), Offset(left + radius * 0.42f, top), radius * 0.16f)
    drawLine(ink, Offset(left + radius * 0.42f, top), Offset(left + radius * 0.42f, center.y), radius * 0.16f)
    drawLine(ink, Offset(left, center.y), Offset(left + radius * 0.42f, center.y), radius * 0.16f)
}
