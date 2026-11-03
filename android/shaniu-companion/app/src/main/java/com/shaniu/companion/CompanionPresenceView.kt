// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.os.SystemClock
import android.view.View
import kotlin.math.PI
import kotlin.math.min
import kotlin.math.sin

/**
 * The phone-side rendition of the device's two round displays.
 *
 * Presentation only: a mood is a projection of state the caller already owns.
 * This view never reads or writes device state and never sends an expression.
 *
 * Animation is a pure function of [SystemClock.uptimeMillis], so the view can
 * be recreated on every render (the Activity rebuilds its tree on each status
 * poll) without restarting a blink or scan. Frames are only scheduled from
 * [onDraw], which stops on its own when the view is detached or hidden, and
 * the system "remove animations" setting freezes every mood on its rest pose.
 */
internal class CompanionPresenceView(context: Context) : View(context) {
    enum class Mood {
        /** No device has been claimed on this phone yet. */
        ABSENT,
        /** Claimed, but there is no authenticated phone link. */
        OFFLINE,
        /** Scanning, connecting or reconnecting. */
        SEARCHING,
        /** Authenticated, fresh, ready for a conversation. */
        AWAKE,
        /** A conversation turn is in progress on the device. */
        ENGAGED,
        /** Linked to the phone but not on Wi-Fi / voice service not ready. */
        DROWSY,
        /** Linked, but the status snapshot is not fresh yet. */
        UNSURE,
        /** Preview-only expressions for customization. */
        HAPPY,
        ASLEEP,
    }

    var mood = Mood.AWAKE
        set(value) { if (field != value) { field = value; invalidate() } }
    /** Static rendering for small tiles and lists. */
    var still = false
        set(value) { field = value; invalidate() }

    private val design = CompanionDesign(context)
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND
    }
    private val glow = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rect = RectF()
    private val path = Path()
    private var radius = 0f
    private var centers = floatArrayOf(0f, 0f)
    private var centerY = 0f
    private var glowShaders = arrayOfNulls<Shader>(2)

    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        val gap = min(w, h) * 0.07f
        // Two displays side by side; keep a small margin for pulse rings.
        radius = min((w - gap) / 4f * 0.82f, h / 2f * 0.86f)
        centerY = h / 2f
        centers = floatArrayOf(w / 2f - radius - gap / 2f, w / 2f + radius + gap / 2f)
        glowShaders = Array(2) { index ->
            RadialGradient(centers[index], centerY, radius * 0.62f,
                intArrayOf(withAlpha(design.eye, 110), withAlpha(design.eye, 0)), null, Shader.TileMode.CLAMP)
        }
    }

    private val animating: Boolean
        get() = !still && ValueAnimator.areAnimatorsEnabled() && when (mood) {
            Mood.AWAKE, Mood.ENGAGED, Mood.SEARCHING, Mood.DROWSY -> true
            else -> false
        }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (radius <= 0f) return
        val t = if (animating) SystemClock.uptimeMillis() / 1000.0 else 0.0
        if (!still) drawGround(canvas)
        for (index in 0..1) {
            val cx = centers[index]
            drawDisplay(canvas, cx)
            drawEye(canvas, index, cx, t)
            drawAura(canvas, cx, t, index)
        }
        // Thirty frames a second is ample for blinks and slow drift.
        if (animating) postInvalidateDelayed(FRAME_MS)
    }

    private fun drawGround(canvas: Canvas) {
        fill.shader = null
        fill.color = withAlpha(if (design.dark) Color.BLACK else design.ink, if (design.dark) 90 else 18)
        val w = (centers[1] - centers[0]) / 2f + radius
        rect.set(width / 2f - w * 0.92f, centerY + radius * 1.04f, width / 2f + w * 0.92f, centerY + radius * 1.14f)
        canvas.drawOval(rect, fill)
    }

    private fun drawDisplay(canvas: Canvas, cx: Float) {
        fill.shader = null
        fill.color = design.bezel
        canvas.drawCircle(cx, centerY, radius, fill)
        fill.color = design.glass
        canvas.drawCircle(cx, centerY, radius * 0.88f, fill)
        if (still) return
        // A restrained specular edge; the glass reads as a real lens without
        // turning the illustration into glassmorphism.
        stroke.shader = null
        // Kept short and faint so it can never be mistaken for the SEARCHING orbit.
        stroke.strokeWidth = radius * 0.018f
        stroke.color = withAlpha(Color.WHITE, 22)
        rect.set(cx - radius * 0.94f, centerY - radius * 0.94f, cx + radius * 0.94f, centerY + radius * 0.94f)
        canvas.drawArc(rect, 215f, 36f, false, stroke)
    }

    private fun drawEye(canvas: Canvas, index: Int, cx: Float, t: Double) {
        val r = radius
        val eyeWidth = r * 0.25f
        val eyeHeight = r * 0.58f
        val live = when (mood) {
            Mood.ABSENT, Mood.OFFLINE -> false
            else -> true
        }
        val color = when (mood) {
            Mood.ABSENT, Mood.OFFLINE -> design.sleepingEye
            Mood.UNSURE -> blend(design.eye, design.sleepingEye, 0.55f)
            else -> design.eye
        }
        // Soft bloom behind a lit eye.
        if (live && mood != Mood.UNSURE) {
            val breathe = if (mood == Mood.DROWSY) (0.55 + 0.25 * sin(t * 2 * PI / 4.0)).toFloat() else 1f
            glow.shader = glowShaders[index]
            glow.alpha = (255 * breathe * if (mood == Mood.ASLEEP) 0.45f else 1f).toInt()
            canvas.drawCircle(cx, centerY, r * 0.62f, glow)
        }
        fill.shader = null
        fill.color = color
        when (mood) {
            Mood.ABSENT -> canvas.drawCircle(cx, centerY, r * 0.06f, fill)
            Mood.OFFLINE, Mood.ASLEEP -> {
                val w = eyeWidth * 0.9f; val h = r * 0.055f; val y = centerY + r * 0.08f
                rect.set(cx - w, y - h, cx + w, y + h)
                canvas.drawRoundRect(rect, h, h, fill)
            }
            Mood.HAPPY -> {
                stroke.shader = null
                stroke.color = color
                stroke.strokeWidth = eyeWidth * 0.62f
                path.reset()
                path.moveTo(cx - eyeWidth * 0.95f, centerY + eyeWidth * 0.35f)
                path.quadTo(cx, centerY - eyeWidth * 1.35f, cx + eyeWidth * 0.95f, centerY + eyeWidth * 0.35f)
                canvas.drawPath(path, stroke)
            }
            Mood.SEARCHING -> {
                val look = (sin(t * 2 * PI / 2.6) * r * 0.16).toFloat()
                canvas.drawCircle(cx + look, centerY, eyeWidth * 0.5f, fill)
            }
            Mood.DROWSY -> {
                // Heavy lids: a low, flat-topped sliver of the open eye.
                val h = eyeHeight * 0.26f
                val y = centerY + eyeHeight * 0.16f
                rect.set(cx - eyeWidth / 2f, y - h / 2f, cx + eyeWidth / 2f, y + h / 2f)
                canvas.drawRoundRect(rect, eyeWidth / 2f, h / 2f, fill)
            }
            Mood.AWAKE, Mood.ENGAGED, Mood.UNSURE -> {
                val open = blink(t)
                val scale = if (mood == Mood.ENGAGED) 1.08f else 1f
                val h = (eyeHeight * scale * open).coerceAtLeast(r * 0.06f)
                val gazeX = if (mood == Mood.AWAKE) (sin(t * 2 * PI / 7.0) * r * 0.05).toFloat() else 0f
                val bob = if (mood == Mood.ENGAGED) (sin(t * 2 * PI / 1.3) * r * 0.02).toFloat() else 0f
                val x = cx + gazeX
                val y = centerY + bob
                rect.set(x - eyeWidth / 2f, y - h / 2f, x + eyeWidth / 2f, y + h / 2f)
                canvas.drawRoundRect(rect, eyeWidth / 2f, min(eyeWidth, h) / 2f, fill)
                if (open > 0.6f && !still) {
                    // Catch light, offset to the upper left like the hardware render.
                    fill.color = withAlpha(Color.WHITE, 150)
                    val cw = eyeWidth * 0.2f
                    rect.set(x - eyeWidth * 0.3f, y - h * 0.38f, x - eyeWidth * 0.3f + cw, y - h * 0.38f + h * 0.28f)
                    canvas.drawRoundRect(rect, cw / 2f, cw / 2f, fill)
                }
            }
        }
    }

    private fun drawAura(canvas: Canvas, cx: Float, t: Double, index: Int) {
        if (!animating) return
        stroke.shader = null
        when (mood) {
            Mood.ENGAGED -> {
                // Listening/thinking pulse expanding from each display.
                val p = (((t + index * 0.15) % 1.8) / 1.8).toFloat()
                stroke.strokeWidth = radius * 0.03f
                stroke.color = withAlpha(design.eye, ((1f - p) * 120).toInt())
                canvas.drawCircle(cx, centerY, radius * (1.0f + 0.14f * p), stroke)
            }
            Mood.SEARCHING -> {
                // A short arc orbits the bezel while the link is being made.
                val angle = ((t * 160.0 + index * 180.0) % 360.0).toFloat()
                stroke.strokeWidth = radius * 0.04f
                stroke.color = withAlpha(design.eye, 190)
                rect.set(cx - radius * 0.94f, centerY - radius * 0.94f, cx + radius * 0.94f, centerY + radius * 0.94f)
                canvas.drawArc(rect, angle, 64f, false, stroke)
            }
            else -> Unit
        }
    }

    /** 1 = fully open. A brief blink every few seconds, occasionally doubled. */
    private fun blink(t: Double): Float {
        if (t == 0.0) return 1f
        val period = 5.2
        val phase = t % period
        val duration = 0.17
        fun lid(start: Double): Float {
            val p = (phase - start) / duration
            return if (p in 0.0..1.0) (1.0 - sin(p * PI) * 0.94).toFloat() else 1f
        }
        val cycle = (t / period).toLong()
        return min(lid(0.0), if (cycle % 3L == 2L) lid(0.28) else 1f)
    }

    private fun withAlpha(color: Int, alpha: Int) = Color.argb(alpha.coerceIn(0, 255), Color.red(color), Color.green(color), Color.blue(color))

    private fun blend(a: Int, b: Int, amount: Float): Int = Color.rgb(
        (Color.red(a) + (Color.red(b) - Color.red(a)) * amount).toInt(),
        (Color.green(a) + (Color.green(b) - Color.green(a)) * amount).toInt(),
        (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * amount).toInt(),
    )

    companion object {
        private const val FRAME_MS = 33L
    }
}
