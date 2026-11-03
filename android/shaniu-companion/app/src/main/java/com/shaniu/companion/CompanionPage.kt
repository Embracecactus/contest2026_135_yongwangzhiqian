// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.drawable.ClipDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.graphics.drawable.RippleDrawable
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.*
import com.google.android.material.button.MaterialButton
import com.shaniu.companion.CompanionDesign.Type

/**
 * Reusable page construction only. Device transactions remain with the owner.
 *
 * Every screen is composed from these pieces so spacing, type and states stay
 * consistent: an 8 dp spacing grid, 24 dp page gutters, 48 dp minimum touch
 * targets and grouped rows on a single surface.
 */
internal class CompanionPage(private val context: Context, private val content: LinearLayout) {
    data class DiscoveryCandidate(
        val title: String,
        val detail: String,
        val contentDescription: String,
        val enabled: Boolean,
        val select: () -> Unit,
    )

    /** One hop in the phone → device → voice service chain. */
    enum class Link { LIVE, PENDING, BROKEN, UNKNOWN, OFF }
    data class ChainNode(val icon: String, val label: String, val detail: String)

    private val design = CompanionDesign(context)
    private val INK get() = design.ink
    private val MUTED get() = design.muted
    private val largeText get() = context.resources.configuration.fontScale > 1.3f
    private fun dp(value: Int) = (value * context.resources.displayMetrics.density).toInt()
    private fun text(value: CharSequence, type: Type, color: Int = INK) = TextView(context).apply {
        text = value; design.style(this, type, color)
    }
    private fun ripple(fill: Int, radius: Float, mask: Boolean = true) = RippleDrawable(
        ColorStateList.valueOf(design.selected),
        design.shape(fill, radius),
        if (mask) design.shape(Color.WHITE, radius) else null)

    fun settingsRow(title: String, subtitle: String, enabled: Boolean = true,
                    selected: Boolean = false, danger: Boolean = false, iconName: String? = null, checked: Boolean? = null, action: () -> Unit) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            minimumHeight = dp(64)
            background = ripple(if (selected) design.selected else Color.TRANSPARENT, 0f)
            isEnabled = enabled; isClickable = enabled; isFocusable = enabled
            contentDescription = "$title，$subtitle" + if (selected) "，当前已选择" else ""
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
            if (enabled) setOnClickListener { action() }
        }
        if (iconName != null) row.addView(icon(iconName, 20, when {
            danger -> design.danger
            enabled -> INK
            else -> MUTED
        }).apply {
            scaleType = ImageView.ScaleType.CENTER_INSIDE
            setPadding(dp(9), dp(9), dp(9), dp(9))
            background = design.shape(if (danger) design.dangerSurface else design.selected, dp(19).toFloat())
            alpha = if (enabled) 1f else 0.6f
        }, LinearLayout.LayoutParams(dp(38), dp(38)).apply { rightMargin = dp(14) })
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
            addView(text(title, Type.HEADLINE, if (danger && enabled) design.danger else if (enabled) INK else MUTED))
            if (subtitle.isNotBlank()) addView(text(subtitle, Type.CAPTION, MUTED).apply { setPadding(0, dp(3), 0, 0) })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        if (checked != null) {
            row.addView(com.google.android.material.materialswitch.MaterialSwitch(context).apply {
                isChecked = checked; isEnabled = enabled; isClickable = false; isFocusable = false
                thumbTintList = ColorStateList(arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()),
                    intArrayOf(design.onAccent, design.muted))
                trackTintList = ColorStateList(
                    arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(design.accent, design.selected))
                trackDecorationTintList = ColorStateList.valueOf(design.divider)
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            })
            row.accessibilityDelegate = object : View.AccessibilityDelegate() {
                override fun onInitializeAccessibilityNodeInfo(host: View, info: android.view.accessibility.AccessibilityNodeInfo) {
                    super.onInitializeAccessibilityNodeInfo(host, info)
                    info.className = "android.widget.Switch"; info.isCheckable = true; info.isChecked = checked
                }
            }
        } else row.addView(ImageView(context).apply {
            // The trailing indicator is always the last child; informationRow hides it.
            visibility = if (selected || enabled) View.VISIBLE else View.INVISIBLE
            setImageDrawable(CompanionIcons.drawable(context, if (selected) "check" else "chevron", if (selected) INK else MUTED))
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(dp(if (selected) 20 else 18), dp(if (selected) 20 else 18)).apply { leftMargin = dp(12) })
        // Consecutive rows form one section; intervening headings end the group.
        val last = content.getChildAt(content.childCount - 1)
        val group = if (last is LinearLayout && last.tag == "settings-group") last else
            LinearLayout(context).apply {
                tag = "settings-group"; orientation = LinearLayout.VERTICAL
                background = design.shape(design.surface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
                clipToOutline = true
                content.addView(this, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
            }
        // Hairlines align with the text column, not the icon well.
        if (group.childCount > 0) group.addView(View(context).apply {
            setBackgroundColor(design.divider)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(-1, 1).apply {
            leftMargin = if (iconName != null) dp(68) else dp(16)
        })
        group.addView(row, LinearLayout.LayoutParams(-1, -2))
    }

    fun informationRow(title: String, subtitle: String, iconName: String? = null) {
        settingsRow(title, subtitle, iconName = iconName) { }
        val group = content.getChildAt(content.childCount - 1) as LinearLayout
        val row = group.getChildAt(group.childCount - 1) as LinearLayout
        row.isClickable = false; row.isFocusable = false; row.background = null
        row.getChildAt(row.childCount - 1).visibility = View.GONE
    }

    fun notice(message: String) {
        content.addView(text(message, Type.CALLOUT, INK).apply {
            setPadding(dp(16), dp(14), dp(16), dp(14))
            background = design.shape(design.selected, dp(CompanionDesign.RADIUS_CONTROL.toInt()).toFloat())
            setCompoundDrawablesRelativeWithIntrinsicBounds(CompanionIcons.drawable(context, "info", MUTED).apply {
                setBounds(0, 0, dp(18), dp(18))
            }, null, null, null)
            compoundDrawablePadding = dp(12)
        }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(16); bottomMargin = dp(8) })
    }

    /** A filled pill. Always appended as the last child of [content]. */
    fun primaryButton(label: String, enabled: Boolean, action: () -> Unit) {
        content.addView(button(label, enabled, primary = true, action = action),
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                topMargin = dp(8)
            })
    }

    fun secondaryButton(label: String, enabled: Boolean, action: () -> Unit) {
        content.addView(button(label, enabled, primary = false, action = action),
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                topMargin = dp(8)
            })
    }

    fun button(label: String, enabled: Boolean, primary: Boolean, action: () -> Unit) = MaterialButton(context).apply {
        text = label; isAllCaps = false
        design.style(this, Type.HEADLINE, if (!enabled) MUTED else if (primary) design.onAccent else INK)
        isEnabled = enabled
        minHeight = dp(56); minimumHeight = dp(56)
        setPadding(dp(20), dp(12), dp(20), dp(12))
        stateListAnimator = null; elevation = 0f
        backgroundTintList = ColorStateList.valueOf(when {
            !enabled -> design.selected
            primary -> design.accent
            else -> Color.TRANSPARENT
        })
        if (!primary && enabled) {
            strokeColor = ColorStateList.valueOf(design.divider); strokeWidth = dp(1)
        }
        rippleColor = ColorStateList.valueOf(if (primary) Color.argb(40, 255, 255, 255) else design.selected)
        cornerRadius = dp(28)
        insetTop = 0; insetBottom = 0
        setOnClickListener { action() }
    }

    fun sectionTitle(title: String) {
        content.addView(text(title, Type.LABEL, MUTED).apply {
            setPadding(dp(4), dp(28), 0, dp(4))
            isAccessibilityHeading = true
        })
    }

    private fun icon(name: String, size: Int = 24, color: Int = INK) = ImageView(context).apply {
        setImageDrawable(CompanionIcons.drawable(context, name, color))
        layoutParams = LinearLayout.LayoutParams(dp(size), dp(size))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    private fun circleButton(iconName: String, description: String?, filled: Boolean, action: () -> Unit) = ImageButton(context).apply {
        setImageDrawable(CompanionIcons.drawable(context, iconName, INK))
        contentDescription = description; setPadding(dp(12), dp(12), dp(12), dp(12))
        scaleType = ImageView.ScaleType.CENTER_INSIDE
        background = RippleDrawable(ColorStateList.valueOf(design.divider),
            if (filled) design.shape(design.surface, dp(24).toFloat()) else null,
            design.shape(Color.WHITE, dp(24).toFloat()))
        setOnClickListener { action() }
    }

    /** Home top bar: eyebrow + wordmark with one circular action. */
    fun homeHeader(eyebrow: String, title: String, actionLabel: String?, action: (() -> Unit)?) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(12), 0, dp(4))
        }
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            addView(text(eyebrow, Type.CAPTION, MUTED))
            addView(text(title, Type.TITLE).apply { isAccessibilityHeading = true; setPadding(0, dp(2), 0, 0) })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, -2, 1f))
        if (action != null) row.addView(circleButton("plus", actionLabel, true, action),
            LinearLayout.LayoutParams(dp(48), dp(48)).apply { leftMargin = dp(12) })
        content.addView(row, LinearLayout.LayoutParams(-1, -2))
    }

    fun header(title: String, subtitle: String = "", actionLabel: String? = null, back: (() -> Unit)? = null, action: (() -> Unit)? = null) {
        if (back != null) pageTitle(title, subtitle, back) else homeHeader(subtitle.ifBlank { "SHANIU" }, title, actionLabel, action)
    }

    fun pageTitle(title: String, subtitle: String, back: (() -> Unit)? = null) {
        if (back != null) {
            val breadcrumb = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
            breadcrumb.addView(circleButton("back", "返回", false, back).apply { translationX = -dp(12).toFloat() },
                LinearLayout.LayoutParams(dp(48), dp(48)))
            content.addView(breadcrumb, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(4) })
        }
        content.addView(text(title, Type.DISPLAY).apply {
            isAccessibilityHeading = true
            setPadding(0, if (back != null) dp(4) else dp(24), 0, 0)
        })
        if (subtitle.isNotBlank()) content.addView(text(subtitle, Type.BODY, MUTED).apply {
            setPadding(0, dp(8), 0, dp(16))
        }) else content.addView(View(context), LinearLayout.LayoutParams(-1, dp(16)))
    }

    /** Sheet heading: left-aligned so long Chinese subtitles read naturally. */
    fun sheetHeader(title: String, subtitle: String, close: () -> Unit) {
        content.addView(View(context).apply { background = design.shape(design.divider, dp(2).toFloat()) },
            LinearLayout.LayoutParams(dp(36), dp(4)).apply { gravity = Gravity.CENTER_HORIZONTAL; bottomMargin = dp(8) })
        val top = LinearLayout(context).apply { gravity = Gravity.TOP }
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            addView(text(title, Type.TITLE).apply { isAccessibilityHeading = true; setPadding(0, dp(10), 0, 0) })
            if (subtitle.isNotBlank()) addView(text(subtitle, Type.CALLOUT, MUTED).apply { setPadding(0, dp(6), 0, 0) })
        }
        top.addView(labels, LinearLayout.LayoutParams(0, -2, 1f))
        top.addView(circleButton("close", "关闭", true, close), LinearLayout.LayoutParams(dp(48), dp(48)).apply { leftMargin = dp(12) })
        content.addView(top, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
    }

    fun presence(mood: CompanionPresenceView.Mood, heightDp: Int): CompanionPresenceView =
        CompanionPresenceView(context).apply { this.mood = mood }.also {
            content.addView(it, LinearLayout.LayoutParams(-1, dp(heightDp)))
        }

    /**
     * The relationship between phone, device and voice service, drawn as one
     * chain. Each link is a projection of state the caller has verified; an
     * unknown link is dotted, never shown as connected.
     */
    fun connectionChain(nodes: List<ChainNode>, links: List<Link>, summary: String,
                        description: String, action: (() -> Unit)?) {
        val box = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(12))
            background = if (action != null) ripple(design.surface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
                else design.shape(design.surface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
            contentDescription = description
            isClickable = action != null; isFocusable = action != null
            action?.let { run -> setOnClickListener { run() } }
        }
        val chain = LinearLayout(context).apply {
            gravity = Gravity.TOP
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        }
        nodes.forEachIndexed { index, node ->
            val before = links.getOrNull(index - 1)
            val after = links.getOrNull(index)
            val lit = before == Link.LIVE || after == Link.LIVE
            val column = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
            }
            column.addView(icon(node.icon, 20, if (lit) INK else MUTED).apply {
                scaleType = ImageView.ScaleType.CENTER_INSIDE
                setPadding(dp(10), dp(10), dp(10), dp(10))
                background = if (lit) design.outline(design.surface, design.ink, dp(20).toFloat(), design.dp(1.5f))
                    else design.shape(design.selected, dp(20).toFloat())
            }, LinearLayout.LayoutParams(dp(40), dp(40)))
            column.addView(text(node.label, Type.LABEL, INK).apply {
                gravity = Gravity.CENTER; setPadding(0, dp(8), 0, 0); maxLines = 1
            })
            column.addView(text(node.detail, Type.CAPTION, MUTED).apply {
                gravity = Gravity.CENTER; setPadding(0, dp(2), 0, 0)
            })
            chain.addView(column, LinearLayout.LayoutParams(0, -2, 1f))
            if (index < nodes.size - 1) {
                chain.addView(LinkView(context, links.getOrElse(index) { Link.UNKNOWN }),
                    LinearLayout.LayoutParams(0, dp(40), 0.55f))
            }
        }
        box.addView(chain, LinearLayout.LayoutParams(-1, -2))
        val footer = LinearLayout(context).apply {
            gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(14), 0, 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        }
        footer.addView(View(context).apply { setBackgroundColor(design.divider) }, LinearLayout.LayoutParams(-1, 1))
        box.addView(footer, LinearLayout.LayoutParams(-1, -2))
        val caption = LinearLayout(context).apply {
            gravity = Gravity.CENTER_VERTICAL; setPadding(0, dp(12), 0, dp(2))
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        }
        caption.addView(text(summary, Type.CAPTION, MUTED), LinearLayout.LayoutParams(0, -2, 1f))
        if (action != null) {
            caption.addView(text("网络设置", Type.LABEL, INK))
            caption.addView(icon("chevron", 14, INK), LinearLayout.LayoutParams(dp(14), dp(14)).apply { leftMargin = dp(2) })
        }
        box.addView(caption, LinearLayout.LayoutParams(-1, -2))
        content.addView(box, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8); bottomMargin = dp(4) })
    }

    /** A link between two chain nodes. A live link carries a slow travelling
     * glint; the frame loop runs only while it is visible and animations are on. */
    private class LinkView(context: Context, private val link: Link) : View(context) {
        private val design = CompanionDesign(context)
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeCap = Paint.Cap.ROUND; style = Paint.Style.STROKE }
        private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
        init { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO }
        override fun onDraw(canvas: Canvas) {
            super.onDraw(canvas)
            val y = height / 2f
            val inset = design.dp(2f)
            paint.strokeWidth = design.dp(2f)
            paint.pathEffect = null
            when (link) {
                Link.LIVE -> paint.color = design.ink
                Link.PENDING -> { paint.color = design.muted; paint.pathEffect = DashPathEffect(floatArrayOf(design.dp(6f), design.dp(5f)), 0f) }
                Link.BROKEN -> paint.color = design.warning
                Link.UNKNOWN -> { paint.color = design.muted; paint.pathEffect = DashPathEffect(floatArrayOf(0.1f, design.dp(5f)), 0f) }
                Link.OFF -> paint.color = design.divider
            }
            if (link == Link.BROKEN) {
                // A visible break in the middle of the link.
                val mid = width / 2f; val gap = design.dp(6f)
                canvas.drawLine(inset, y, mid - gap, y, paint)
                canvas.drawLine(mid + gap, y, width - inset, y, paint)
                paint.strokeWidth = design.dp(1.6f)
                canvas.drawLine(mid - design.dp(3f), y - design.dp(5f), mid + design.dp(3f), y + design.dp(5f), paint)
            } else canvas.drawLine(inset, y, width - inset, y, paint)
            if (link == Link.LIVE && android.animation.ValueAnimator.areAnimatorsEnabled()) {
                val p = (SystemClock.uptimeMillis() % 2400L) / 2400f
                dot.color = design.eye
                canvas.drawCircle(inset + (width - 2 * inset) * p, y, design.dp(3f), dot)
                postInvalidateDelayed(40L)
            }
        }
    }

    fun featureCard(iconName: String, title: String, body: String) {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(24), dp(28), dp(24), dp(20))
            background = design.shape(design.surface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
            addView(icon(iconName, 56).apply {
                scaleType = ImageView.ScaleType.CENTER_INSIDE
                setPadding(dp(16), dp(16), dp(16), dp(16))
                background = design.shape(design.selected, dp(28).toFloat())
            }, LinearLayout.LayoutParams(dp(56), dp(56)).apply { bottomMargin = dp(16) })
        }
        CompanionPage(context, card).hero(title, body)
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
    }

    fun segments(labels: List<String>, selected: Int, select: (Int) -> Unit) {
        val tabs = LinearLayout(context).apply {
            setPadding(dp(4), dp(4), dp(4), dp(4))
            background = design.shape(design.selected, dp(26).toFloat())
        }
        labels.forEachIndexed { index, label ->
            tabs.addView(text(label, if (index == selected) Type.HEADLINE else Type.CALLOUT, if (index == selected) INK else MUTED).apply {
                gravity = Gravity.CENTER; minimumHeight = dp(44)
                setPadding(dp(4), dp(8), dp(4), dp(8))
                background = if (index == selected) design.shape(design.surface, dp(22).toFloat()) else
                    ripple(Color.TRANSPARENT, dp(22).toFloat())
                elevation = if (index == selected) dp(1).toFloat() else 0f
                isSelected = index == selected; isClickable = true; isFocusable = true
                contentDescription = label + if (isSelected) "，当前已选择" else ""
                setOnClickListener { select(index) }
            }, LinearLayout.LayoutParams(0, -2, 1f))
        }
        content.addView(tabs, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(20) })
    }

    private fun panel(): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(20), dp(20), dp(20))
        background = design.shape(design.surface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
    }

    fun updateIntroduction(version: String, source: String) {
        val card = panel()
        card.addView(text("当前设备版本", Type.OVERLINE, MUTED))
        card.addView(text(version, Type.TITLE).apply { setPadding(0, dp(8), 0, 0) })
        card.addView(View(context).apply { setBackgroundColor(design.divider) },
            LinearLayout.LayoutParams(-1, 1).apply { topMargin = dp(18); bottomMargin = dp(14) })
        val sourceRow = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        sourceRow.addView(icon("download", 18, MUTED), LinearLayout.LayoutParams(dp(18), dp(18)).apply { rightMargin = dp(10) })
        sourceRow.addView(text("更新来源", Type.CALLOUT, MUTED), LinearLayout.LayoutParams(0, -2, 1f))
        sourceRow.addView(text(source, Type.CALLOUT, INK).apply { gravity = Gravity.END })
        card.addView(sourceRow)
        content.addView(card, LinearLayout.LayoutParams(-1, -2))
        sectionTitle("更新会这样进行")
        val steps = panel().apply { setPadding(dp(20), dp(8), dp(20), dp(8)) }
        listOf("先核对设备、签名、布局与安全计数" to "shield", "使用 Wi-Fi 传输，蓝牙负责协调" to "wifi",
            "重启后回读版本，确认完成再显示成功" to "refresh").forEach { (line, iconName) ->
            val row = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL; minimumHeight = dp(52) }
            row.addView(icon(iconName, 18, INK), LinearLayout.LayoutParams(dp(18), dp(18)).apply { rightMargin = dp(14) })
            row.addView(text(line, Type.CALLOUT, INK), LinearLayout.LayoutParams(0, -2, 1f))
            steps.addView(row)
        }
        content.addView(steps, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8); bottomMargin = dp(16) })
    }

    fun updateProgress(title: String, body: String, percent: Int?, stage: Int?, confirmed: Boolean, failed: Boolean = false) {
        val card = panel()
        if (failed) card.background = design.shape(design.dangerSurface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
        card.addView(text(title, Type.TITLE, if (failed) design.danger else INK).apply { accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE })
        card.addView(text(body, Type.CALLOUT, MUTED).apply { setPadding(0, dp(6), 0, 0) })
        card.addView(com.google.android.material.progressindicator.LinearProgressIndicator(context).apply {
            max = 100
            isIndeterminate = !confirmed && percent == null && stage != null
            progress = if (confirmed) 100 else percent ?: 0
            trackThickness = dp(6); trackCornerRadius = dp(3)
            setIndicatorColor(if (failed) design.danger else design.accent); trackColor = if (failed) design.surface else design.selected
            contentDescription = if (confirmed) "设备版本已确认" else percent?.let { "传输 $it%，尚未确认安装" } ?: "等待设备确认"
        }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(18); bottomMargin = dp(8) })
        card.addView(text(if (confirmed) "新版本已回读确认" else percent?.let { "传输进度 $it%" } ?: "以设备回读为准", Type.CAPTION, MUTED))
        val labels = listOf("校验更新包", "准备传输通道", "传输到设备", "设备验证与安装", "等待设备重连", "回读版本并确认")
        val timeline = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(16), 0, 0) }
        labels.forEachIndexed { index, label ->
            val done = confirmed || (stage != null && index < stage)
            val current = !confirmed && stage == index
            val row = LinearLayout(context).apply { gravity = Gravity.TOP }
            val rail = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL }
            rail.addView(FrameLayout(context).apply {
                background = when {
                    done -> design.shape(design.accent, dp(12).toFloat())
                    current -> design.outline(design.surface, design.accent, dp(12).toFloat(), dp(2).toFloat())
                    else -> design.outline(design.surface, design.divider, dp(12).toFloat(), dp(1).toFloat())
                }
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                if (done) addView(icon("check", 14, design.onAccent), FrameLayout.LayoutParams(dp(14), dp(14), Gravity.CENTER))
                else addView(text((index + 1).toString(), Type.LABEL, if (current) INK else MUTED).apply { gravity = Gravity.CENTER },
                    FrameLayout.LayoutParams(-1, -1))
            }, LinearLayout.LayoutParams(dp(24), dp(24)))
            if (index < labels.size - 1) rail.addView(View(context).apply {
                setBackgroundColor(if (done) design.accent else design.divider)
            }, LinearLayout.LayoutParams(dp(2), dp(22)))
            row.addView(rail, LinearLayout.LayoutParams(dp(24), -2).apply { rightMargin = dp(14) })
            row.addView(text(label, if (current) Type.HEADLINE else Type.CALLOUT, if (current || done) INK else MUTED).apply {
                setPadding(0, dp(2), 0, 0)
                contentDescription = label + when { done -> "，已完成"; current -> "，进行中"; else -> "，待确认" }
            }, LinearLayout.LayoutParams(0, -2, 1f))
            timeline.addView(row, LinearLayout.LayoutParams(-1, -2))
        }
        card.addView(timeline)
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(8) })
    }

    fun addCard(title: String, body: String) {
        content.addView(panel().apply {
            setPadding(dp(20), dp(18), dp(20), dp(18))
            addView(text(title, Type.HEADLINE))
            addView(text(body, Type.CALLOUT, MUTED).apply { setPadding(0, dp(6), 0, 0) })
        }, LinearLayout.LayoutParams(-1, -2).apply { setMargins(0, dp(8), 0, dp(4)) })
    }

    fun hero(title: String, subtitle: String) {
        content.addView(text(title, Type.TITLE).apply {
            gravity = Gravity.CENTER; isAccessibilityHeading = true
        })
        content.addView(text(subtitle, Type.CALLOUT, MUTED).apply {
            gravity = Gravity.CENTER
            setPadding(dp(8), dp(8), dp(8), dp(16))
            accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
        })
    }

    fun volumeCard(value: Int?, enabled: Boolean, reason: String, submit: (Int) -> Unit) {
        val card = panel().apply { setPadding(dp(20), dp(18), dp(20), dp(14)) }
        val heading = LinearLayout(context).apply {
            orientation = if (largeText) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
            gravity = if (largeText) Gravity.START else Gravity.BOTTOM
        }
        val labels = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        labels.addView(text("扬声器音量", Type.HEADLINE))
        val detail = text(if (enabled) "设备已确认" else reason, Type.CAPTION, MUTED).apply {
            setPadding(0, dp(4), 0, 0)
            accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
        }
        labels.addView(detail)
        heading.addView(labels, if (heading.orientation == LinearLayout.HORIZONTAL) LinearLayout.LayoutParams(0, -2, 1f)
            else LinearLayout.LayoutParams(-1, -2))
        val label = text(value?.let { "$it%" } ?: "—", Type.NUMERAL, if (enabled) INK else MUTED).apply {
            setPadding(if (largeText) 0 else dp(12), 0, 0, 0)
        }
        heading.addView(label); card.addView(heading)
        val sliderRow = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        sliderRow.addView(icon("speaker", 18, MUTED))
        sliderRow.addView(SeekBar(context).apply {
            max = 100; progress = value ?: 0; isEnabled = enabled
            minimumHeight = dp(48); contentDescription = "扬声器音量"
            splitTrack = false
            setPadding(dp(14), 0, dp(14), 0)
            progressDrawable = sliderTrack(enabled)
            thumb = GradientDrawable().apply {
                shape = GradientDrawable.OVAL; setSize(dp(24), dp(24))
                setColor(if (enabled) design.accent else design.muted)
                setStroke(dp(3), design.surface)
            }
            minHeight = dp(8); maxHeight = dp(8)
            var dragging = false
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onStartTrackingTouch(bar: SeekBar?) { dragging = true }
                override fun onProgressChanged(bar: SeekBar?, progress: Int, user: Boolean) {
                    if (!user) return
                    label.text = "$progress%"
                    detail.text = "待设备确认"
                    // Accessibility and keyboard changes do not get a touch-end callback.
                    if (!dragging) submit(progress)
                }
                override fun onStopTrackingTouch(bar: SeekBar) {
                    dragging = false
                    if (bar.progress != value) submit(bar.progress)
                    else detail.text = "设备已确认"
                }
            })
        }, LinearLayout.LayoutParams(0, -2, 1f))
        card.addView(sliderRow, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(10) })
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
    }

    private fun sliderTrack(enabled: Boolean): LayerDrawable {
        val radius = dp(4).toFloat()
        val track = design.shape(design.selected, radius)
        val fill = ClipDrawable(design.shape(if (enabled) design.accent else design.muted, radius), Gravity.START, ClipDrawable.HORIZONTAL)
        return LayerDrawable(arrayOf(track, fill)).apply {
            setId(0, android.R.id.background); setId(1, android.R.id.progress)
        }
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
        mood: CompanionPresenceView.Mood = CompanionPresenceView.Mood.SEARCHING,
    ) {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(12), dp(24), dp(24))
        }
        card.addView(View(context).apply {
            background = design.shape(design.divider, dp(2).toFloat())
        }, LinearLayout.LayoutParams(dp(36), dp(4)).apply { gravity = Gravity.CENTER_HORIZONTAL; bottomMargin = dp(8) })
        val top = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        top.addView(text(title, Type.LABEL, MUTED).apply { isAccessibilityHeading = true }, LinearLayout.LayoutParams(0, -2, 1f))
        if (dismiss != null) top.addView(circleButton("close", dismissDescription ?: dismissLabel ?: "关闭连接卡片", true, dismiss).apply {
            background = RippleDrawable(ColorStateList.valueOf(design.divider),
                design.shape(design.background, dp(24).toFloat()), null)
        }, LinearLayout.LayoutParams(dp(48), dp(48)))
        card.addView(top)
        val page = CompanionPage(context, card)
        page.presence(mood, if (largeText) 120 else 150)
        card.addView(View(context), LinearLayout.LayoutParams(-1, dp(12)))
        page.hero(if (candidates.isNotEmpty()) "认识一下，傻妞。" else title, summary)
        candidates.forEach { candidate ->
            card.addView(TextView(context).apply {
                text = "${candidate.title}\n${candidate.detail}"
                design.style(this, Type.BODY, if (candidate.enabled) INK else MUTED)
                minimumHeight = dp(64); gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(16), dp(10), dp(16), dp(10))
                setCompoundDrawablesRelativeWithIntrinsicBounds(CompanionIcons.drawable(context, "pair", if (candidate.enabled) INK else MUTED).apply {
                    setBounds(0, 0, dp(22), dp(22))
                }, null, CompanionIcons.drawable(context, "chevron", MUTED).apply { setBounds(0, 0, dp(18), dp(18)) }, null)
                compoundDrawablePadding = dp(14)
                background = RippleDrawable(ColorStateList.valueOf(design.selected),
                    design.shape(design.background, dp(CompanionDesign.RADIUS_CONTROL.toInt()).toFloat()), null)
                isEnabled = candidate.enabled; isClickable = candidate.enabled; isFocusable = candidate.enabled
                contentDescription = candidate.contentDescription
                if (candidate.enabled) setOnClickListener { candidate.select() }
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
        }
        if (candidates.size == 1) {
            val candidate = candidates.single()
            page.primaryButton("连接傻妞", candidate.enabled, candidate.select)
        }
        page.addMuted("首次连接需要扫描设备屏幕；已认领设备通过现有凭据验证。")
        content.addView(card, LinearLayout.LayoutParams(-1, -2))
    }

    fun addMuted(message: String) {
        content.addView(text(message, Type.CAPTION, MUTED).apply {
            setPadding(dp(4), dp(12), dp(4), dp(12))
        })
    }
}
