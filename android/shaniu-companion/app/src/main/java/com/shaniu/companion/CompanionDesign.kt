// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.drawable.GradientDrawable

/** Shared semantic colors, independent of transport and device state. */
internal class CompanionDesign(context: Context) {
    val dark = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    val background = Color.parseColor(if (dark) "#111A17" else "#F4F7F6")
    val surface = Color.parseColor(if (dark) "#1B2822" else "#FFFFFF")
    val ink = Color.parseColor(if (dark) "#E7F0EA" else "#182D28")
    val muted = Color.parseColor(if (dark) "#ACBEB3" else "#5E716A")
    val accent = Color.parseColor(if (dark) "#9AE0C1" else "#176B55")
    val onAccent = Color.parseColor(if (dark) "#123827" else "#FFFFFF")
    val selected = Color.parseColor(if (dark) "#283E32" else "#E2F1EB")
    val divider = Color.parseColor(if (dark) "#31463A" else "#DDE6E1")
    val warning = Color.parseColor(if (dark) "#F4D59F" else "#845414")
    val warningSurface = Color.parseColor(if (dark) "#342B1C" else "#FFF3DB")
    val danger = Color.parseColor(if (dark) "#FFB4AB" else "#A63737")
    val dangerSurface = Color.parseColor(if (dark) "#422823" else "#FBEAEA")
    fun shape(color: Int, radius: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
    }
}
