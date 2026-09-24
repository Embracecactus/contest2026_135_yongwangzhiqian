// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.View

/** Local illustration only; never writes an expression to the device. */
internal class CompanionExpressionView(context: Context, private val device: Boolean = false) : View(context) {
    var expression = 0
        set(value) { field = value; invalidate() }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val design = CompanionDesign(context)
    init { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.save()
        canvas.translate(width / 2f, height / 2f)
        val scale = if (device) minOf(width / 310f, height / 160f) else minOf(width / 90f, height / 52f)
        canvas.scale(scale, scale)
        if (device) {
            paint.style = Paint.Style.STROKE; paint.strokeWidth = 1f; paint.color = Color.parseColor("#305345")
            canvas.drawCircle(0f, 0f, 73f, paint)
            paint.style = Paint.Style.FILL; paint.color = Color.parseColor("#AAC4B7")
            canvas.drawRoundRect(-85f, -34f, 85f, 45f, 42f, 42f, paint)
            paint.color = Color.parseColor("#EBF4ED")
            canvas.drawRoundRect(-85f, -41f, 85f, 39f, 42f, 42f, paint)
            paint.color = Color.parseColor("#112F25")
            canvas.drawRoundRect(-78f, -34f, 78f, 32f, 34f, 34f, paint)
            paint.color = Color.parseColor("#1A3D30")
            canvas.drawCircle(-36f, -1f, 24f, paint); canvas.drawCircle(36f, -1f, 24f, paint)
            paint.color = Color.parseColor("#A1DDBD"); canvas.drawCircle(0f, 26f, 1.5f, paint)
        }
        for (x in if (device) listOf(-36f, 36f) else listOf(-13f, 13f)) {
            paint.color = if (device) Color.parseColor("#9AE0C1") else design.accent
            val w = if (device) 7f else 5f
            when (expression) {
                1 -> { canvas.drawRoundRect(x - w - 3f, -7f, x + w + 3f, 9f, w + 3f, w + 3f, paint)
                    canvas.drawRect(x - w - 3f, 2f, x + w + 3f, 9f, paint) }
                2 -> canvas.drawRoundRect(x - w - 5f, -1f, x + w + 5f, 3f, 2f, 2f, paint)
                else -> canvas.drawRoundRect(x - w, if (device) -16f else -13f, x + w, if (device) 14f else 13f, w, w, paint)
            }
            if (device && expression == 0) {
                paint.color = Color.parseColor("#C6F5DF")
                canvas.drawRoundRect(x - 5f, -14f, x - 2f, 3f, 2f, 2f, paint)
            }
        }
        canvas.restore()
    }
}
