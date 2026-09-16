// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.widget.ImageView

/** Decorative illustration; never represents live device status. */
class CompanionPortraitView(context: Context) : ImageView(context) {
    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        scaleType = ScaleType.FIT_CENTER
        setImageResource(R.drawable.shaniu_companion_hero_v2)
    }
}
