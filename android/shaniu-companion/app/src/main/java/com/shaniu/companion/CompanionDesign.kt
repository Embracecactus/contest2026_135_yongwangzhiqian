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

    private val source = context
    private fun token(id: Int) = source.getColor(id)

    // Single source: res/values[-night]/colors.xml, which the Material theme roles also use.
    val background = token(R.color.shaniu_background)
    val surface = token(R.color.shaniu_surface)
    val ink = token(R.color.shaniu_ink)
    val muted = token(R.color.shaniu_muted)
    /** Primary action fill. Ink in light mode, porcelain in dark mode. */
    val accent = ink
    val onAccent = token(R.color.shaniu_on_accent)
    /** Soft fill for icon wells, selected rows and secondary actions. */
    val selected = token(R.color.shaniu_selected)
    val divider = token(R.color.shaniu_divider)
    val warning = token(R.color.shaniu_warning)
    val warningSurface = token(R.color.shaniu_warning_surface)
    val danger = token(R.color.shaniu_danger)
    val dangerSurface = token(R.color.shaniu_danger_surface)
    val scrim = token(R.color.shaniu_scrim)

    /** Device display glow. Fill/glow only; never used for body text. */
    val eye = token(R.color.shaniu_eye)
    /** Contrast-safe variant of [eye] for small live marks and labels. */
    val eyeInk = token(R.color.shaniu_eye_ink)
    /** The display glass is black on both themes, exactly like the hardware. */
    val glass = Color.parseColor("#09090B")
    val bezel = token(R.color.shaniu_bezel)
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
