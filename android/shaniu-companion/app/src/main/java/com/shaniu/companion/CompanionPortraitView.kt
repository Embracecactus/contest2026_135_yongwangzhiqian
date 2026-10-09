// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.graphics.*
import android.view.View

/** Static illustration, independent of device playback and connection state. */
class CompanionPortraitView(context: Context) : View(context) {
    var sleeping = false
        set(value) { field = value; invalidate() }
    var eyeTint = Color.parseColor("#91DDB6")
        set(value) { field = value; invalidate() }
    private val design = CompanionDesign(context)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    init { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        // Match the 364 x 204 design area; do not enlarge the device on tall phones.
        val scale = minOf(width / 364f, height / 204f)
        canvas.save()
        canvas.translate(width / 2f, height / 2f)
        canvas.scale(scale, scale)
        paint.shader = RadialGradient(0f, -12f, 122f,
            if (design.dark) Color.parseColor("#50305139") else Color.parseColor("#90DCEBDC"),
            Color.TRANSPARENT, Shader.TileMode.CLAMP)
        canvas.drawCircle(0f, -12f, 122f, paint)
        paint.shader = null
        paint.color = if (design.dark) Color.parseColor("#40263D30") else Color.parseColor("#16153923")
        canvas.drawOval(-120f, 68f, 120f, 83f, paint)
        canvas.translate(0f, -7f)
        canvas.rotate(-5f)
        paint.color = Color.parseColor("#AFC2B7")
        canvas.drawRoundRect(-136f, -61f, 136f, 77f, 68f, 68f, paint)
        paint.shader = LinearGradient(-110f, -65f, 105f, 67f,
            intArrayOf(Color.WHITE, Color.parseColor("#EEF4EF"), Color.parseColor("#C6D5CD")),
            floatArrayOf(0.15f, 0.4f, 1f), Shader.TileMode.CLAMP)
        canvas.drawRoundRect(-136f, -69f, 136f, 69f, 68f, 68f, paint)
        paint.shader = LinearGradient(-120f, -55f, 115f, 50f,
            Color.parseColor("#273A31"), Color.parseColor("#061A14"), Shader.TileMode.CLAMP)
        canvas.drawRoundRect(-128f, -61f, 128f, 59f, 60f, 60f, paint)
        paint.shader = null
        paint.style = Paint.Style.STROKE; paint.strokeWidth = 1f
        paint.color = Color.parseColor("#506A5E")
        canvas.drawRoundRect(-128f, -61f, 128f, 59f, 60f, 60f, paint)
        paint.style = Paint.Style.FILL
        for (x in listOf(-54.5f, 54.5f)) {
            paint.color = Color.WHITE
            paint.shader = RadialGradient(x - 10f, -20f, 64f,
                Color.parseColor("#294D3C"), Color.parseColor("#10271D"), Shader.TileMode.CLAMP)
            canvas.drawCircle(x, -1f, 44.5f, paint)
            paint.shader = null; paint.style = Paint.Style.STROKE
            paint.color = Color.parseColor("#5C477560")
            canvas.drawCircle(x, -1f, 44.5f, paint)
            paint.style = Paint.Style.FILL; paint.color = Color.WHITE
            val highlight = if (eyeTint == Color.parseColor("#91DDB6")) Color.parseColor("#C9F8CA") else
                Color.rgb((Color.red(eyeTint) + 255) / 2, (Color.green(eyeTint) + 255) / 2, (Color.blue(eyeTint) + 255) / 2)
            paint.shader = LinearGradient(x, -22f, x, 21f, highlight, eyeTint, Shader.TileMode.CLAMP)
            if (sleeping) canvas.drawRoundRect(x - 10f, -2f, x + 10f, 5f, 4f, 4f, paint)
            else canvas.drawRoundRect(x - 9.5f, -22.5f, x + 9.5f, 20.5f, 10f, 10f, paint)
            paint.shader = null
            if (!sleeping) {
                paint.color = Color.parseColor("#CCEDFFF0")
                canvas.drawRoundRect(x - 6.5f, -19.5f, x - 1.5f, -2.5f, 3f, 3f, paint)
            }
        }
        paint.color = Color.parseColor("#829B8E")
        canvas.drawCircle(0f, 50f, 1.5f, paint)
        canvas.restore()
    }
}
