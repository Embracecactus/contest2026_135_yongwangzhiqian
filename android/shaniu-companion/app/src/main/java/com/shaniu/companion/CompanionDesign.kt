// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.widget.TextView

/**
 * Shared design tokens, independent of transport and device state.
 *
 * The interface is deliberately monochrome (porcelain and ink). The only
 * chromatic colour is [eye], the glow of the device's displays, and it is
 * reserved for things that are actually live: her eyes and established links.
 * When the device is unreachable the app therefore reads as grey, which is the
 * intended connection signal rather than decoration.
 */
internal class CompanionDesign(context: Context) {
    val dark = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    private val density = context.resources.displayMetrics.density

    val background = Color.parseColor(if (dark) "#0E0E11" else "#F4F1EC")
    val surface = Color.parseColor(if (dark) "#18181C" else "#FFFDFA")
    val ink = Color.parseColor(if (dark) "#F2EFEA" else "#17161B")
    val muted = Color.parseColor(if (dark) "#A29EA7" else "#6A6670")
    /** Primary action fill. Ink in light mode, porcelain in dark mode. */
    val accent = ink
    val onAccent = Color.parseColor(if (dark) "#121215" else "#FFFFFF")
    /** Soft fill for icon wells, selected rows and secondary actions. */
    val selected = Color.parseColor(if (dark) "#26262C" else "#EBE6DF")
    val divider = Color.parseColor(if (dark) "#2B2B31" else "#E2DDD5")
    val warning = Color.parseColor(if (dark) "#F2C27B" else "#8F5200")
    val warningSurface = Color.parseColor(if (dark) "#2E2416" else "#FAEFD9")
    val danger = Color.parseColor(if (dark) "#FFB4AB" else "#B3261E")
    val dangerSurface = Color.parseColor(if (dark) "#3A1F1D" else "#FBE9E7")
    val scrim = Color.parseColor(if (dark) "#B3000000" else "#6617161B")

    /** Device display glow. Fill/glow only; never used for body text. */
    val eye = Color.parseColor(if (dark) "#7CF0BF" else "#4FD8A0")
    /** Contrast-safe variant of [eye] for small live labels and dots. */
    val eyeInk = Color.parseColor(if (dark) "#7CF0BF" else "#127A56")
    /** The display glass is black on both themes, exactly like the hardware. */
    val glass = Color.parseColor("#09090B")
    val bezel = Color.parseColor(if (dark) "#2A2A30" else "#1D1D22")
    val sleepingEye = Color.parseColor("#3A3A42")
    val stage = Color.parseColor("#0C0C0F")
    val stageInk = Color.parseColor("#F2EFEA")
    val stageMuted = Color.parseColor("#8E8A93")
    val stageLine = Color.parseColor("#2A2A30")

    fun dp(value: Float) = value * density

    fun shape(color: Int, radius: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
    }

    fun outline(fill: Int, stroke: Int, radius: Float, width: Float = dp(1f)) = shape(fill, radius).apply {
        setStroke(width.toInt().coerceAtLeast(1), stroke)
    }

    /** Type scale. Sizes are in sp so system font scaling still applies. */
    enum class Type(val size: Float, val weight: Int, val tracking: Float = 0f, val leading: Float = 1.18f) {
        DISPLAY(30f, 700, -0.01f, 1.12f),
        TITLE(22f, 650, -0.005f, 1.15f),
        HEADLINE(16f, 600),
        BODY(15f, 400, leading = 1.3f),
        CALLOUT(14f, 400, leading = 1.3f),
        CAPTION(12.5f, 400, leading = 1.25f),
        LABEL(12f, 600, 0.02f),
        OVERLINE(11f, 650, 0.14f),
        NUMERAL(40f, 300, -0.02f, 1f),
    }

    fun style(view: TextView, type: Type, color: Int = ink) {
        view.textSize = type.size
        view.typeface = Typeface.create(Typeface.DEFAULT, type.weight, false)
        view.letterSpacing = type.tracking
        view.setLineSpacing(0f, type.leading)
        view.setTextColor(color)
        view.includeFontPadding = true
    }

    companion object {
        const val RADIUS_SHEET = 28f
        const val RADIUS_GROUP = 22f
        const val RADIUS_CONTROL = 16f
        /** Standard easing for entrances (Material emphasized decelerate). */
        fun emphasized() = android.view.animation.PathInterpolator(0.05f, 0.7f, 0.1f, 1f)
        fun standard() = android.view.animation.PathInterpolator(0.2f, 0f, 0f, 1f)
    }
}
