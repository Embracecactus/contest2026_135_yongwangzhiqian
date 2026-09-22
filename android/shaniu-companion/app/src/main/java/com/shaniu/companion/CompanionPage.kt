// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.graphics.Color
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*

/** Reusable page construction only. Device transactions remain with the owner. */
internal class CompanionPage(private val context: Context, private val content: LinearLayout) {
    private val design = CompanionDesign(context)
    private val INK get() = design.ink
    private val MUTED get() = design.muted
    private fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()

    fun settingsRow(title: String, subtitle: String, enabled: Boolean = true,
                            selected: Boolean = false, action: () -> Unit) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(18), dp(16), dp(18), dp(16))
            minimumHeight = dp(72)
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (selected) design.selected else design.surface)
                cornerRadius = dp(18).toFloat()
            }
            isEnabled = enabled; isClickable = enabled; isFocusable = enabled
            contentDescription = "$title，$subtitle" + if (selected) "，当前已选择" else ""
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
            if (enabled) setOnClickListener { action() }
        }
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
            addView(TextView(context).apply {
                text = title; textSize = 16f; setTextColor(if (enabled) INK else MUTED)
                typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
            })
            addView(TextView(context).apply {
                text = subtitle; textSize = 12f; setTextColor(MUTED)
                setPadding(0, dp(5), 0, 0)
            })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(TextView(context).apply {
            text = if (selected) "✓" else if (enabled) "›" else ""
            textSize = 22f; setTextColor(MUTED); setPadding(dp(12), 0, 0, 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        })
        content.addView(row, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(6); bottomMargin = dp(2) })
    }

    fun primaryButton(label: String, enabled: Boolean, action: () -> Unit) {
        content.addView(com.google.android.material.button.MaterialButton(context).apply {
            text = label; isAllCaps = false; textSize = 16f
            typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
            isEnabled = enabled
            setTextColor(design.onAccent)
            minHeight = dp(56)
            setPadding(dp(16), dp(12), dp(16), dp(12))
            stateListAnimator = null
            backgroundTintList = android.content.res.ColorStateList.valueOf(if (enabled) design.accent else MUTED)
            cornerRadius = dp(20)
            insetTop = 0; insetBottom = 0
            setOnClickListener { action() }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(8)
        })
    }

    fun sectionTitle(title: String) {
        content.addView(
            TextView(context).apply {
                text = title
                textSize = 23f
                typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
                setTextColor(INK)
                setPadding(0, dp(18), 0, dp(8))
            },
        )
    }

    fun addCard(title: String, body: String) {
        content.addView(
            LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL
                background = android.graphics.drawable.GradientDrawable().apply {
                    setColor(design.surface); cornerRadius = dp(20).toFloat()
                }
                setPadding(dp(20), dp(18), dp(20), dp(18))
                addView(TextView(context).apply {
                    text = title
                    textSize = 17f
                    typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
                    setTextColor(INK)
                })
                addView(TextView(context).apply {
                    text = body
                    textSize = 14f
                    setTextColor(MUTED)
                    setPadding(0, dp(6), 0, 0)
                })
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, 0, 0, dp(10)) },
        )
    }

    fun addMuted(message: String) {
        content.addView(TextView(context).apply {
            text = message
            textSize = 14f
            setTextColor(MUTED)
            setPadding(0, dp(4), 0, dp(12))
        })
    }

}
