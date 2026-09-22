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
    data class DiscoveryCandidate(
        val title: String,
        val detail: String,
        val contentDescription: String,
        val enabled: Boolean,
        val select: () -> Unit,
    )
    private val design = CompanionDesign(context)
    private val INK get() = design.ink
    private val MUTED get() = design.muted
    private fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()

    fun settingsRow(title: String, subtitle: String, enabled: Boolean = true,
                            selected: Boolean = false, action: () -> Unit) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            minimumHeight = dp(72)
            background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(if (selected) design.selected else design.surface, dp(16).toFloat()),
                design.shape(Color.WHITE, dp(16).toFloat()))
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
                typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            })
            addView(TextView(context).apply {
                text = subtitle; textSize = 14f; setTextColor(MUTED)
                setPadding(0, dp(5), 0, 0)
            })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(TextView(context).apply {
            text = if (selected) "✓" else if (enabled) "›" else ""
            textSize = 22f; setTextColor(MUTED); setPadding(dp(12), 0, 0, 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        })
        // Consecutive rows form one section; intervening headings end the group.
        val last = content.getChildAt(content.childCount - 1)
        val group = if (last is LinearLayout && last.tag == "settings-group") last else
            LinearLayout(context).apply {
                tag = "settings-group"; orientation = LinearLayout.VERTICAL
                background = design.shape(design.surface, dp(20).toFloat())
                clipToOutline = true
                content.addView(this, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
            }
        if (group.childCount > 0) group.addView(View(context).apply {
            setBackgroundColor(design.background)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(-1, dp(1)).apply { leftMargin = dp(16); rightMargin = dp(16) })
        group.addView(row, LinearLayout.LayoutParams(-1, -2))
    }

    fun primaryButton(label: String, enabled: Boolean, action: () -> Unit) {
        content.addView(com.google.android.material.button.MaterialButton(context).apply {
            text = label; isAllCaps = false; textSize = 16f
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
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
                textSize = 17f
                typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
                setTextColor(INK)
                setPadding(dp(4), dp(24), 0, dp(8))
                isAccessibilityHeading = true
            },
        )
    }

    fun pageTitle(title: String, subtitle: String) {
        content.addView(TextView(context).apply {
            text = title; textSize = 28f; setTextColor(INK)
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            setPadding(0, dp(8), 0, dp(8)); isAccessibilityHeading = true
        })
        addMuted(subtitle)
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
                    typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
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

    /** Presentation only: discovery ownership, authentication, and cancellation
     * remain with the Activity's single foreground control session. */
    fun addDiscoveryCard(
        title: String,
        summary: String,
        candidates: List<DiscoveryCandidate>,
        dismissLabel: String?,
        dismissDescription: String?,
        dismiss: (() -> Unit)?,
    ) {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(design.surface); cornerRadius = dp(20).toFloat()
            }
            setPadding(dp(20), dp(18), dp(20), dp(12))
            addView(TextView(context).apply {
                text = title; textSize = 17f
                typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
                setTextColor(INK); isAccessibilityHeading = true
            })
            addView(TextView(context).apply {
                text = summary; textSize = 14f; setTextColor(MUTED)
                setPadding(0, dp(6), 0, dp(8))
            })
        }
        candidates.forEach { candidate ->
            card.addView(TextView(context).apply {
                text = "${candidate.title}\n${candidate.detail}"
                textSize = 15f; setTextColor(if (candidate.enabled) INK else MUTED)
                minimumHeight = dp(64); gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(12), dp(8), dp(12), dp(8))
                background = android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(design.selected),
                    design.shape(design.background, dp(14).toFloat()), null)
                isClickable = candidate.enabled; isFocusable = candidate.enabled
                contentDescription = candidate.contentDescription
                if (candidate.enabled) setOnClickListener { candidate.select() }
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(4) })
        }
        if (dismissLabel != null && dismiss != null) card.addView(TextView(context).apply {
            text = dismissLabel; textSize = 14f; setTextColor(design.accent)
            gravity = Gravity.CENTER; minimumHeight = dp(48); isClickable = true; isFocusable = true
            contentDescription = dismissDescription ?: dismissLabel
            setOnClickListener { dismiss() }
        })
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply {
            setMargins(0, 0, 0, dp(10))
        })
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
