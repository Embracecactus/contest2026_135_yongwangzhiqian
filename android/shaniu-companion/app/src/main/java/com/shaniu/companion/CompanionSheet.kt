// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.ScrollView

// Bottom sheet host shared by every page.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.dismissCompanionSheet() {
    val previous = companionSheet
    companionSheet = null; refreshCompanionSheet = null
    previous?.dismiss()
}

internal fun MainActivity.showCompanionSheet(title: String, subtitle: String, done: Boolean = true,
                               onClosed: () -> Unit = {},
                               populate: (LinearLayout, android.app.Dialog) -> Unit) {
    dismissCompanionSheet()
    val dialog = android.app.Dialog(this)
    companionSheet = dialog
    val body = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(10), dp(24), dp(24))
    }
    CompanionPage(this, body).sheetHeader(title, subtitle) { dialog.dismiss() }
    populate(body, dialog)
    if (done) CompanionPage(this, body).primaryButton("完成", true) { dialog.dismiss() }
    val scroll = object : ScrollView(this) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val screenLimit = dp((resources.configuration.screenHeightDp - 48).coerceAtLeast(160))
            // 键盘缩小窗口时遵守父布局约束，不能继续按全屏高度测量。
            val available = if (View.MeasureSpec.getMode(heightMeasureSpec) == View.MeasureSpec.UNSPECIFIED)
                screenLimit else minOf(screenLimit, View.MeasureSpec.getSize(heightMeasureSpec))
            super.onMeasure(widthMeasureSpec, View.MeasureSpec.makeMeasureSpec(available, View.MeasureSpec.AT_MOST))
        }
    }.apply { isFillViewport = false; addView(body); clipToOutline = true }
    dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
    dialog.setContentView(scroll)
    dialog.window?.apply {
        val radius = dp(CompanionDesign.RADIUS_SHEET.toInt()).toFloat()
        setBackgroundDrawable(android.graphics.drawable.GradientDrawable().apply {
            setColor(design.background)
            cornerRadii = floatArrayOf(radius, radius, radius, radius, 0f, 0f, 0f, 0f)
        })
        setGravity(Gravity.BOTTOM); addFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND)
        setDimAmount(if (design.dark) 0.6f else 0.36f)
        setWindowAnimations(R.style.ShaniuSheetAnimation)
        setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
    }
    dialog.setOnDismissListener {
        onClosed()
        if (companionSheet === dialog) { companionSheet = null; refreshCompanionSheet = null }
    }
    dialog.show()
    dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    var visibleSignature = 0
    scroll.viewTreeObserver.addOnGlobalLayoutListener {
        if (dialog.isShowing) {
            val visible = android.graphics.Rect()
            dialog.window?.decorView?.getWindowVisibleDisplayFrame(visible)
            val signature = 31 * visible.height() + visible.width()
            if (visible.height() > 0 && signature != visibleSignature) {
                visibleSignature = signature
                // Floating windows may keep their full height with the IME open.
                // Use the display frame, never the already constrained decor size.
                dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                    minOf(body.measuredHeight, visible.height()))
            }
        }
    }
}
