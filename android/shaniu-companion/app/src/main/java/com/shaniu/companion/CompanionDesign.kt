// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.content.res.Configuration
import android.graphics.Color
import android.graphics.drawable.GradientDrawable

/** Shared semantic colors, independent of transport and device state. */
internal class CompanionDesign(context: Context) {
    val dark = context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK == Configuration.UI_MODE_NIGHT_YES
    val background = Color.parseColor(if (dark) "#111716" else "#F4F7F6")
    val surface = Color.parseColor(if (dark) "#1D2624" else "#FFFFFF")
    val ink = Color.parseColor(if (dark) "#E5EEEA" else "#182D28")
    val muted = Color.parseColor(if (dark) "#ADBCB6" else "#536B62")
    val accent = Color.parseColor(if (dark) "#8EDCC5" else "#176B55")
    val onAccent = Color.parseColor(if (dark) "#10392D" else "#FFFFFF")
    val selected = Color.parseColor(if (dark) "#29483E" else "#DDEFE7")
    fun shape(color: Int, radius: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = radius
    }
}
