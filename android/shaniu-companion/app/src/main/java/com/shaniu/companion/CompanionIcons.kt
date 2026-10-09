// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.graphics.drawable.Drawable

/** Native vectors from the supplied V2 design; no remote assets or icon font. */
internal object CompanionIcons {
    fun drawable(context: Context, name: String, color: Int): Drawable =
        requireNotNull(context.getDrawable(when (name) {
        "plus" -> R.drawable.ic_ui_plus
        "back" -> R.drawable.ic_ui_back
        "chevron" -> R.drawable.ic_ui_chevron
        "close" -> R.drawable.ic_ui_close
        "bluetooth" -> R.drawable.ic_ui_bluetooth
        "wifi" -> R.drawable.ic_ui_wifi
        "cloud" -> R.drawable.ic_ui_cloud
        "mic" -> R.drawable.ic_ui_mic
        "speaker" -> R.drawable.ic_ui_speaker
        "spark" -> R.drawable.ic_ui_spark
        "device" -> R.drawable.ic_ui_device
        "download" -> R.drawable.ic_ui_download
        "settings" -> R.drawable.ic_ui_settings
        "shield" -> R.drawable.ic_ui_shield
        "info" -> R.drawable.ic_ui_info
        "refresh" -> R.drawable.ic_ui_refresh
        "moon" -> R.drawable.ic_ui_moon
        "eye" -> R.drawable.ic_ui_eye
        "help" -> R.drawable.ic_ui_help
        "qr" -> R.drawable.ic_ui_qr
        else -> R.drawable.ic_ui_info
    })).mutate().apply { setTint(color) }
}
