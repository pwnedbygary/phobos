package com.phobos.emulator.ui

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusEventModifierNode
import androidx.compose.ui.focus.FocusState
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.drawOutline
import androidx.compose.ui.graphics.drawscope.ContentDrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.inset
import androidx.compose.ui.node.DrawModifierNode
import androidx.compose.ui.node.ModifierNodeElement
import androidx.compose.ui.node.invalidateDraw
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * A ring in [color] around the element while it has focus, so a controller shows where the D-pad
 * is; Material's own focus tint is too faint on the glass panels. It follows the focus of the
 * clickable after it in the chain, so it goes before that.
 */
fun Modifier.focusRing(color: Color, shape: Shape = RoundedCornerShape(12.dp), width: Dp = 3.dp): Modifier =
    this then FocusRingElement(color, shape, width)

private data class FocusRingElement(val color: Color, val shape: Shape, val width: Dp) : ModifierNodeElement<FocusRingNode>() {
    override fun create() = FocusRingNode(color, shape, width)

    override fun update(node: FocusRingNode) {
        node.color = color
        node.shape = shape
        node.width = width
        node.invalidateDraw()
    }
}

private class FocusRingNode(var color: Color, var shape: Shape, var width: Dp) : Modifier.Node(), FocusEventModifierNode, DrawModifierNode {
    private var focused = false

    override fun onFocusEvent(focusState: FocusState) {
        if (focusState.isFocused != focused) {
            focused = focusState.isFocused
            invalidateDraw()
        }
    }

    override fun ContentDrawScope.draw() {
        drawContent()
        if (!focused) return
        val stroke = width.toPx()
        // Half the stroke in from the edge, so the ring stays inside what clips the element
        inset(stroke / 2) { drawOutline(shape.createOutline(size, layoutDirection, this), color, style = Stroke(stroke)) }
    }
}
