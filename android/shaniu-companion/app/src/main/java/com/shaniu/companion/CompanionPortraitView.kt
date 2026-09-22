// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.View

/** Decorative illustration; never represents live device status. */
class CompanionPortraitView(context: Context) : View(context) {
    private val design = CompanionDesign(context)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val scale = minOf(width / 280f, height / 180f)
        canvas.save()
        canvas.translate(width / 2f, height / 2f)
        canvas.scale(scale, scale)
        paint.color = design.selected
        canvas.drawCircle(0f, 0f, 86f, paint)
        paint.color = design.surface
        canvas.drawRoundRect(-105f, -63f, 105f, 63f, 56f, 56f, paint)
        paint.color = android.graphics.Color.rgb(24, 45, 40)
        canvas.drawRoundRect(-94f, -52f, 94f, 52f, 46f, 46f, paint)
        paint.color = android.graphics.Color.rgb(142, 220, 197)
        canvas.drawRoundRect(-54f, -22f, -29f, 22f, 13f, 13f, paint)
        canvas.drawRoundRect(29f, -22f, 54f, 22f, 13f, 13f, paint)
        canvas.restore()
    }
}
