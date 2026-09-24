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
                            selected: Boolean = false, danger: Boolean = false, iconName: String? = null, checked: Boolean? = null, action: () -> Unit) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(16), dp(14), dp(16), dp(14))
            minimumHeight = dp(75)
            background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(if (selected) design.selected else design.surface, dp(16).toFloat()),
                design.shape(Color.WHITE, dp(16).toFloat()))
            isEnabled = enabled; isClickable = enabled; isFocusable = enabled
            contentDescription = "$title，$subtitle" + if (selected) "，当前已选择" else ""
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
            if (enabled) setOnClickListener { action() }
        }
        if (iconName != null) row.addView(icon(iconName, 22, if (danger) design.danger else design.accent).apply {
            setPadding(dp(9), dp(9), dp(9), dp(9))
            background = design.shape(if (danger) design.dangerSurface else design.selected, dp(14).toFloat())
        }, LinearLayout.LayoutParams(dp(42), dp(42)).apply { rightMargin = dp(12) })
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
            addView(TextView(context).apply {
                text = title; textSize = 15f; setTextColor(if (danger && enabled) design.danger else if (enabled) INK else MUTED)
                typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            })
            addView(TextView(context).apply {
                text = subtitle; textSize = 12f; setTextColor(MUTED)
                setPadding(0, dp(5), 0, 0)
            })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        if (checked != null) {
            row.addView(com.google.android.material.materialswitch.MaterialSwitch(context).apply {
                isChecked = checked; isEnabled = enabled; isClickable = false; isFocusable = false
                thumbTintList = android.content.res.ColorStateList.valueOf(design.surface)
                trackTintList = android.content.res.ColorStateList(
                    arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf()), intArrayOf(design.accent, design.divider))
                trackDecorationTintList = android.content.res.ColorStateList.valueOf(design.divider)
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            })
            row.accessibilityDelegate = object : View.AccessibilityDelegate() {
                override fun onInitializeAccessibilityNodeInfo(host: View, info: android.view.accessibility.AccessibilityNodeInfo) {
                    super.onInitializeAccessibilityNodeInfo(host, info)
                    info.className = "android.widget.Switch"; info.isCheckable = true; info.isChecked = checked
                }
            }
        } else row.addView(TextView(context).apply {
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
            setBackgroundColor(design.divider)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(-1, dp(1)).apply { leftMargin = dp(16); rightMargin = dp(16) })
        group.addView(row, LinearLayout.LayoutParams(-1, -2))
    }

    fun informationRow(title: String, subtitle: String, iconName: String? = null) {
        settingsRow(title, subtitle, iconName = iconName) { }
        val group = content.getChildAt(content.childCount - 1) as LinearLayout
        val row = group.getChildAt(group.childCount - 1) as LinearLayout
        row.isClickable = false; row.isFocusable = false
        row.getChildAt(row.childCount - 1).visibility = View.GONE
    }

    fun notice(message: String) {
        content.addView(TextView(context).apply {
            text = message; textSize = 13f; setTextColor(design.accent)
            setPadding(dp(16), dp(14), dp(16), dp(14))
            background = design.shape(design.selected, dp(16).toFloat())
        }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(16); bottomMargin = dp(8) })
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
                textSize = 14f
                typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
                setTextColor(INK)
                setPadding(dp(4), dp(24), 0, dp(8))
                isAccessibilityHeading = true
            },
        )
    }

    private fun icon(name: String, size: Int = 24, color: Int = design.accent) = ImageView(context).apply {
        setImageDrawable(CompanionIcons.drawable(context, name, color))
        layoutParams = LinearLayout.LayoutParams(dp(size), dp(size))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    fun header(title: String, subtitle: String = "", actionLabel: String? = null, back: (() -> Unit)? = null, action: (() -> Unit)? = null) {
        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(0, dp(10), 0, dp(16))
        }
        if (back != null) row.addView(ImageButton(context).apply {
            setImageDrawable(CompanionIcons.drawable(context, "back", INK))
            contentDescription = "返回"; setPadding(dp(12), dp(12), dp(12), dp(12))
            background = android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(design.selected), null,
                design.shape(Color.WHITE, dp(24).toFloat()))
            setOnClickListener { back() }
        }, LinearLayout.LayoutParams(dp(48), dp(48)))
        val labels = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            if (back == null) addView(TextView(context).apply {
                text = "SHANIU"; textSize = 11f; letterSpacing = 0.18f; setTextColor(MUTED)
                setPadding(0, 0, 0, dp(5)); importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            })
            addView(TextView(context).apply {
                text = title; textSize = 30f; setTextColor(INK)
                typeface = android.graphics.Typeface.DEFAULT_BOLD; isAccessibilityHeading = true
            })
            if (subtitle.isNotBlank()) addView(TextView(context).apply {
                text = subtitle; textSize = 14f; setTextColor(MUTED); setPadding(0, dp(7), 0, 0)
            })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, -2, 1f))
        if (action != null) row.addView(ImageButton(context).apply {
            setImageDrawable(CompanionIcons.drawable(context, "plus", INK))
            contentDescription = actionLabel; setPadding(dp(12), dp(12), dp(12), dp(12))
            background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(design.surface, dp(24).toFloat()), null)
            setOnClickListener { action() }
        }, LinearLayout.LayoutParams(dp(48), dp(48)).apply { leftMargin = dp(12) })
        content.addView(row, LinearLayout.LayoutParams(-1, -2))
    }

    fun pageTitle(title: String, subtitle: String, back: (() -> Unit)? = null) {
        if (back != null) {
            val breadcrumb = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
            breadcrumb.addView(ImageButton(context).apply {
                setImageDrawable(CompanionIcons.drawable(context, "back", INK))
                contentDescription = "返回"; setPadding(dp(12), dp(12), dp(12), dp(12))
                background = android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(design.selected), null,
                    design.shape(Color.WHITE, dp(24).toFloat()))
                setOnClickListener { back() }
            }, LinearLayout.LayoutParams(dp(48), dp(48)))
            breadcrumb.addView(TextView(context).apply {
                text = "傻妞 · 设备设置"; textSize = 12f; setTextColor(MUTED)
            })
            content.addView(breadcrumb, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
        }
        content.addView(TextView(context).apply {
            text = title; textSize = 28f; setTextColor(INK)
            typeface = android.graphics.Typeface.DEFAULT_BOLD; isAccessibilityHeading = true
        })
        content.addView(TextView(context).apply {
            text = subtitle; textSize = 14f; setTextColor(MUTED)
            setPadding(0, dp(10), 0, dp(24))
        })
    }

    fun connectionStrip(management: String, network: String, warning: Boolean, action: () -> Unit) {
        val row = LinearLayout(context).apply {
            orientation = if (context.resources.configuration.fontScale > 1.3f) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL; minimumHeight = dp(52)
            setPadding(dp(14), dp(12), dp(14), dp(12))
            background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(design.surface, dp(18).toFloat()).apply { setStroke(dp(1), design.divider) }, null)
            isClickable = true; isFocusable = true
            contentDescription = "$management，$network，查看网络设置"
            setOnClickListener { action() }
        }
        listOf(Triple("bluetooth", management, MUTED), Triple("wifi", network, if (warning) design.warning else MUTED)).forEach { (name, text, color) ->
            val status = TextView(context).apply {
                this.text = text; textSize = 12f; setTextColor(color)
                setCompoundDrawablesWithIntrinsicBounds(CompanionIcons.drawable(context, name, color).apply {
                    setBounds(0, 0, dp(18), dp(18))
                }, null, null, null)
                compoundDrawablePadding = dp(6)
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }
            row.addView(status, if (row.orientation == LinearLayout.HORIZONTAL) LinearLayout.LayoutParams(0, -2, 1f)
                else LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(4) })
        }
        if (row.orientation == LinearLayout.HORIZONTAL) row.addView(icon("chevron", 16, MUTED))
        content.addView(row, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(4); bottomMargin = dp(12) })
    }

    fun featureCard(iconName: String, title: String, body: String) {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
            setPadding(dp(20), dp(20), dp(20), dp(20))
            background = design.shape(design.surface, dp(26).toFloat())
            addView(icon(iconName, 56).apply {
                setPadding(dp(14), dp(14), dp(14), dp(14))
                background = design.shape(design.selected, dp(28).toFloat())
            })
        }
        CompanionPage(context, card).hero(title, body)
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(12) })
    }

    fun segments(labels: List<String>, selected: Int, select: (Int) -> Unit) {
        val tabs = LinearLayout(context).apply {
            setPadding(dp(4), dp(4), dp(4), dp(4))
            background = design.shape(design.divider, dp(14).toFloat())
        }
        labels.forEachIndexed { index, label ->
            tabs.addView(TextView(context).apply {
                text = label; textSize = 14f; gravity = Gravity.CENTER; minimumHeight = dp(48)
                setPadding(dp(4), dp(8), dp(4), dp(8))
                setTextColor(if (index == selected) INK else MUTED)
                background = design.shape(if (index == selected) design.surface else Color.TRANSPARENT, dp(10).toFloat())
                isSelected = index == selected; isClickable = true; isFocusable = true
                contentDescription = label + if (isSelected) "，当前已选择" else ""
                setOnClickListener { select(index) }
            }, LinearLayout.LayoutParams(0, -2, 1f))
        }
        content.addView(tabs, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(20) })
    }

    private fun updateCard(title: String, body: String): LinearLayout {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(24), dp(20), dp(24))
            background = design.shape(design.surface, dp(25).toFloat())
        }
        card.addView(icon("download", 70).apply {
            setPadding(dp(21), dp(21), dp(21), dp(21))
            background = design.shape(design.selected, dp(24).toFloat())
        }, LinearLayout.LayoutParams(dp(70), dp(70)).apply { gravity = Gravity.CENTER_HORIZONTAL; bottomMargin = dp(20) })
        CompanionPage(context, card).hero(title, body)
        content.addView(card, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(8) })
        return card
    }

    fun updateIntroduction(version: String, source: String) {
        val card = updateCard("让她更懂你", "固件更新 · 安装后回读确认")
        val specs = LinearLayout(context).apply {
            orientation = if (context.resources.configuration.fontScale > 1.3f) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
        }
        listOf("当前设备版本" to version, "更新来源" to source).forEach { (label, value) ->
            specs.addView(LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL; setPadding(dp(12), dp(12), dp(12), dp(12))
                background = design.shape(design.background, dp(16).toFloat())
                addView(TextView(context).apply { text = label; textSize = 12f; setTextColor(MUTED) })
                addView(TextView(context).apply {
                    text = value; textSize = 14f; setTextColor(INK); setPadding(0, dp(6), 0, 0)
                    typeface = android.graphics.Typeface.DEFAULT_BOLD
                })
            }, if (specs.orientation == LinearLayout.HORIZONTAL) LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = dp(6) }
                else LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(6) })
        }
        card.addView(specs, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
        val page = CompanionPage(context, card)
        page.sectionTitle("更新会这样进行")
        listOf("先核对设备、签名、布局与安全计数", "使用 Wi-Fi 传输，蓝牙负责协调", "重启后回读版本，确认完成再显示成功").forEach {
            page.addMuted("•  $it")
        }
    }

    fun updateProgress(title: String, body: String, percent: Int?, stage: Int?, confirmed: Boolean) {
        val card = updateCard(title, body)
        card.addView(ProgressBar(context, null, android.R.attr.progressBarStyleHorizontal).apply {
            max = 100; progress = if (confirmed) 100 else percent ?: 0
            progressTintList = android.content.res.ColorStateList.valueOf(design.accent)
            progressBackgroundTintList = android.content.res.ColorStateList.valueOf(design.divider)
            contentDescription = if (confirmed) "设备版本已确认" else percent?.let { "传输 $it%，尚未确认安装" } ?: "等待设备确认"
        }, LinearLayout.LayoutParams(-1, dp(7)).apply { topMargin = dp(8); bottomMargin = dp(10) })
        CompanionPage(context, card).addMuted(if (confirmed) "新版本已回读确认" else percent?.let { "传输进度 $it%" } ?: "以设备回读为准")
        listOf("校验更新包", "准备传输通道", "传输到设备", "设备验证与安装", "等待设备重连", "回读版本并确认").forEachIndexed { index, text ->
            val done = confirmed || (stage != null && index < stage)
            val current = !confirmed && stage == index
            val row = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL; minimumHeight = dp(48) }
            row.addView(TextView(context).apply {
                this.text = if (done) "✓" else (index + 1).toString(); textSize = 12f; gravity = Gravity.CENTER
                setTextColor(if (current) design.onAccent else if (done) design.accent else MUTED)
                background = design.shape(if (current) design.accent else design.selected, dp(14).toFloat())
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }, LinearLayout.LayoutParams(dp(26), dp(26)).apply { rightMargin = dp(12) })
            row.addView(TextView(context).apply {
                this.text = text; textSize = 14f; setTextColor(if (current || done) INK else MUTED)
                setPadding(0, dp(8), 0, dp(8))
                contentDescription = text + when { done -> "，已完成"; current -> "，进行中"; else -> "，待确认" }
            }, LinearLayout.LayoutParams(0, -2, 1f))
            card.addView(row, LinearLayout.LayoutParams(-1, -2))
        }
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

    fun hero(title: String, subtitle: String) {
        content.addView(TextView(context).apply {
            text = title; textSize = 25f; setTextColor(INK); gravity = Gravity.CENTER
            typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            isAccessibilityHeading = true
        })
        content.addView(TextView(context).apply {
            text = subtitle; textSize = 14f; setTextColor(MUTED); gravity = Gravity.CENTER
            setPadding(0, dp(8), 0, dp(16))
        })
    }

    fun volumeCard(value: Int?, enabled: Boolean, reason: String, submit: (Int) -> Unit) {
        val card = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(16), dp(20), dp(8))
            background = design.shape(design.surface, dp(24).toFloat())
        }
        val heading = LinearLayout(context).apply {
            orientation = if (context.resources.configuration.fontScale > 1.3f) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val labels = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }
        labels.addView(TextView(context).apply {
            text = "扬声器音量"; textSize = 16f; setTextColor(INK)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
        })
        val detail = TextView(context).apply {
            text = if (enabled) "设备已确认" else reason
            textSize = 14f; setTextColor(MUTED); setPadding(0, dp(4), 0, 0)
        }
        labels.addView(detail)
        heading.addView(labels, if (heading.orientation == LinearLayout.HORIZONTAL) LinearLayout.LayoutParams(0, -2, 1f)
            else LinearLayout.LayoutParams(-1, -2))
        val label = TextView(context).apply {
            text = value?.let { "$it%" } ?: "—"; textSize = 29f; setTextColor(INK)
            setPadding(dp(12), 0, 0, 0)
        }
        heading.addView(label); card.addView(heading)
        val sliderRow = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        sliderRow.addView(icon("speaker", 18, MUTED))
        sliderRow.addView(SeekBar(context).apply {
            max = 100; progress = value ?: 0; isEnabled = enabled
            minimumHeight = dp(48); contentDescription = "扬声器音量"
            progressTintList = android.content.res.ColorStateList.valueOf(design.accent)
            thumbTintList = android.content.res.ColorStateList.valueOf(design.accent)
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
        sliderRow.addView(icon("speaker", 18, MUTED))
        card.addView(sliderRow, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(10) })
        content.addView(card, LinearLayout.LayoutParams(-1, -2))
    }

    fun quickActions(first: () -> Unit, second: () -> Unit) {
        val stacked = context.resources.configuration.fontScale > 1.3f
        val row = LinearLayout(context).apply {
            orientation = if (stacked) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
        }
        listOf(Triple("她的模样", "眼睛 · 唤醒 · 应答", first),
            Triple("对话偏好", "听懂你，说给你听", second)).forEachIndexed { index, (title, subtitle, action) ->
            val column = LinearLayout(context).apply {
                orientation = LinearLayout.VERTICAL; minimumHeight = dp(106)
                setPadding(dp(16), dp(16), dp(16), dp(16))
                background = android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(design.selected),
                    design.shape(design.surface, dp(22).toFloat()), null)
                isClickable = true; isFocusable = true; contentDescription = "$title，$subtitle"
                addView(icon(if (index == 0) "eye" else "cloud"))
                addView(TextView(context).apply {
                    text = title; textSize = 16f; setTextColor(INK); typeface = android.graphics.Typeface.DEFAULT_BOLD
                    setPadding(0, dp(9), 0, dp(6))
                })
                addView(TextView(context).apply { text = subtitle; textSize = 14f; setTextColor(MUTED) })
                setOnClickListener { action() }
            }
            row.addView(column, if (stacked) LinearLayout.LayoutParams(-1, -2).apply { if (index > 0) topMargin = dp(12) } else
                LinearLayout.LayoutParams(0, -2, 1f).apply { if (index > 0) leftMargin = dp(12) })
        }
        content.addView(row, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
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
            setPadding(dp(24), dp(12), dp(24), dp(20))
        }
        card.addView(View(context).apply {
            background = design.shape(design.divider, dp(2).toFloat())
        }, LinearLayout.LayoutParams(dp(34), dp(4)).apply { gravity = Gravity.CENTER_HORIZONTAL; bottomMargin = dp(10) })
        val top = LinearLayout(context).apply { gravity = Gravity.CENTER_VERTICAL }
        top.addView(TextView(context).apply {
            text = title; textSize = 14f; setTextColor(MUTED); isAccessibilityHeading = true
        }, LinearLayout.LayoutParams(0, -2, 1f))
        if (dismiss != null) top.addView(ImageButton(context).apply {
            setImageDrawable(CompanionIcons.drawable(context, "close", INK))
            contentDescription = dismissDescription ?: "关闭连接卡片"
            setPadding(dp(14), dp(14), dp(14), dp(14))
            background = android.graphics.drawable.RippleDrawable(android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(design.background, dp(24).toFloat()), null)
            setOnClickListener { dismiss() }
        }, LinearLayout.LayoutParams(dp(48), dp(48)))
        card.addView(top)
        card.addView(CompanionPortraitView(context), LinearLayout.LayoutParams(-1, dp(204)))
        CompanionPage(context, card).hero(if (candidates.isNotEmpty()) "认识一下，傻妞。" else title, summary)
        candidates.forEach { candidate ->
            card.addView(TextView(context).apply {
                text = "${candidate.title}\n${candidate.detail}"
                textSize = 15f; setTextColor(if (candidate.enabled) INK else MUTED)
                minimumHeight = dp(64); gravity = Gravity.CENTER_VERTICAL
                setPadding(dp(12), dp(8), dp(12), dp(8))
                background = android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf(design.selected),
                    design.shape(design.background, dp(14).toFloat()), null)
                isEnabled = candidate.enabled; isClickable = candidate.enabled; isFocusable = candidate.enabled
                contentDescription = candidate.contentDescription
                if (candidate.enabled) setOnClickListener { candidate.select() }
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(4) })
        }
        if (candidates.size == 1) {
            val candidate = candidates.single()
            CompanionPage(context, card).primaryButton("连接傻妞", candidate.enabled, candidate.select)
        }
        CompanionPage(context, card).addMuted("首次连接需要扫描设备屏幕；已认领设备通过现有凭据验证。")
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
