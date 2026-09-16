// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.app.Activity
import android.os.Build
import android.view.View
import android.view.WindowInsets
import android.window.OnBackInvokedDispatcher

/** Keep controls outside bars, cutouts and the keyboard when Android enforces
 * edge-to-edge for target 35. Older-device layouts retain their window insets.
 */
internal fun View.applySystemInsets() {
    if (Build.VERSION.SDK_INT < 35) return
    val left = paddingLeft
    val top = paddingTop
    val right = paddingRight
    val bottom = paddingBottom
    setOnApplyWindowInsetsListener { view, insets ->
        val safe = insets.getInsets(WindowInsets.Type.systemBars() or
            WindowInsets.Type.displayCutout() or WindowInsets.Type.ime())
        view.setPadding(left + safe.left, top + safe.top,
            right + safe.right, bottom + safe.bottom)
        insets
    }
    requestApplyInsets()
}

internal fun Activity.installSystemBack(action: () -> Unit) {
    if (Build.VERSION.SDK_INT >= 33) {
        onBackInvokedDispatcher.registerOnBackInvokedCallback(
            OnBackInvokedDispatcher.PRIORITY_DEFAULT) { action() }
    }
}
