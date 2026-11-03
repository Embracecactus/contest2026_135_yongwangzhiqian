// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.TAB_OVERVIEW
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONALITY
import com.shaniu.companion.MainActivity.Companion.TAB_UPDATE
import android.app.AlertDialog
import android.graphics.Color
import android.text.InputType
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import com.shaniu.companion.protocol.ConsoleErrorCode
import com.shaniu.companion.protocol.Emotion
import com.shaniu.companion.protocol.TurnPhase
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.protocol.UpdatePhase

// Small view builders and label helpers used across MainActivity pages.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.settingsRow(title: String, subtitle: String, enabled: Boolean = true,
                        selected: Boolean = false, danger: Boolean = false, action: () -> Unit) =
    CompanionPage(this, content).settingsRow(title, subtitle, enabled, selected, danger, action = action)

internal fun MainActivity.primaryButton(label: String, enabled: Boolean, action: () -> Unit) =
    CompanionPage(this, content).primaryButton(label, enabled, action)

/** Tab glyph with a pill indicator. A newly selected pill grows from the
 * icon outward over 240 ms; animator scale 0 shows it immediately. */
internal fun MainActivity.navigationIcon(tab: Int, selected: Boolean = false, animate: Boolean = false): android.graphics.drawable.Drawable =
    object : android.graphics.drawable.Drawable() {
        private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
        private val icon = CompanionIcons.drawable(this@navigationIcon, when (tab) {
            TAB_OVERVIEW -> "pair"
            TAB_PERSONALITY -> "spark"
            TAB_UPDATE -> "download"
            else -> "settings"
        }, if (selected) INK else MUTED)
        private var reveal = if (animate) 0f else 1f
        private val rect = android.graphics.RectF()
        init {
            if (animate) android.animation.ValueAnimator.ofFloat(0f, 1f).apply {
                duration = 200; interpolator = CompanionDesign.emphasized()
                addUpdateListener { reveal = it.animatedValue as Float; invalidateSelf() }
                start()
            }
        }
        override fun getIntrinsicWidth() = dp(56)
        override fun getIntrinsicHeight() = dp(32)
        override fun setAlpha(alpha: Int) { paint.alpha = alpha; icon.alpha = alpha }
        override fun setColorFilter(filter: android.graphics.ColorFilter?) { icon.colorFilter = filter }
        @Deprecated("Drawable contract")
        override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
        override fun draw(canvas: android.graphics.Canvas) {
            if (selected) {
                paint.color = design.selected
                val half = bounds.width() / 2f * (0.45f + 0.55f * reveal)
                rect.set(bounds.exactCenterX() - half, bounds.top.toFloat(), bounds.exactCenterX() + half, bounds.bottom.toFloat())
                canvas.drawRoundRect(rect, bounds.height() / 2f, bounds.height() / 2f, paint)
            }
            val cx = bounds.centerX(); val cy = bounds.centerY(); val half = dp(12)
            icon.setBounds(cx - half, cy - half, cx + half, cy + half)
            icon.draw(canvas)
        }
    }

/**
 * Page transition on navigation only; status refreshes never animate.
 * Sibling tabs fade through (switched tens of times a day, so short and
 * without travel). Drilling in or backing out uses a shared X axis, so the
 * direction of travel says where the page sits in the hierarchy.
 */
internal fun MainActivity.revealPage(direction: Int) {
    content.animate().cancel()
    if (direction == 0) {
        content.alpha = 0.35f; content.translationX = 0f
        content.animate().alpha(1f).setDuration(140).setInterpolator(CompanionDesign.standard()).start()
    } else {
        content.alpha = 0.2f; content.translationX = dp(24 * direction).toFloat()
        content.animate().alpha(1f).translationX(0f).setDuration(220)
            .setInterpolator(CompanionDesign.emphasized()).start()
    }
}

internal fun MainActivity.sectionTitle(title: String) = CompanionPage(this, content).sectionTitle(title)

internal fun MainActivity.addCard(title: String, body: String) = CompanionPage(this, content).addCard(title, body)

internal fun MainActivity.addMuted(message: String) = CompanionPage(this, content).addMuted(message)

internal fun MainActivity.editField(
    hintText: String,
    initial: String,
    secret: Boolean = false,
    multiline: Boolean = false,
): EditText = EditText(this).apply {
    hint = hintText
    setText(initial)
    setTextColor(INK)
    setHintTextColor(MUTED)
    inputType = when {
        secret -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        multiline -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
        else -> InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
    }
    if (multiline) minLines = 2
    content.addView(
        this,
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        ).apply { setMargins(0, 0, 0, dp(8)) },
    )
}

internal fun MainActivity.actionButton(label: String, enabled: Boolean = true, action: () -> Unit) {
    content.addView(
        compactButton(label, enabled, action),
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        ).apply { setMargins(0, dp(3), 0, dp(3)) },
    )
}

internal fun MainActivity.compactButton(label: String, enabled: Boolean, action: () -> Unit): Button =
    CompanionPage(this, content).button(label, enabled, primary = false, action = action)

internal fun MainActivity.confirm(title: String, message: String, consent: String? = null, confirmed: () -> Unit) {
    val builder = AlertDialog.Builder(this)
        .setTitle(title)
        .setNegativeButton("取消", null)
        .setPositiveButton("确认") { _, _ -> confirmed() }
    if (consent == null) { builder.setMessage(message).show(); return }
    showCompanionSheet(title, "清除设备上的用户配置，\n并撤销现有手机的控制凭据。", done = false) { body, dialog ->
        val page = CompanionPage(this, body)
        body.addView(TextView(this).apply {
            text = "此操作无法撤销。请先确认设备与清理范围。"
            design.style(this, CompanionDesign.Type.CALLOUT, design.warning)
            setPadding(dp(16), dp(14), dp(16), dp(14))
            background = design.shape(design.warningSurface, dp(CompanionDesign.RADIUS_CONTROL.toInt()).toFloat())
        }, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(14) })
        page.informationRow("将清除", "当前归属、Wi-Fi、云服务凭据、记忆、偏好和显示选择")
        page.informationRow("将保留", "当前固件；本机旧凭据保留至设备回执确认完成")
        val acknowledgment = CheckBox(this).apply {
            text = consent; design.style(this, CompanionDesign.Type.CALLOUT, INK); minHeight = dp(56)
            buttonTintList = android.content.res.ColorStateList.valueOf(design.ink)
            setPadding(dp(8), dp(8), 0, dp(8))
        }
        body.addView(acknowledgment, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12); bottomMargin = dp(4) })
        val submit = page.button("恢复出厂", false, primary = true) { dialog.dismiss(); confirmed() }.apply {
            backgroundTintList = android.content.res.ColorStateList.valueOf(design.selected)
        }
        body.addView(submit, LinearLayout.LayoutParams(-1, -2))
        acknowledgment.setOnCheckedChangeListener { _, checked ->
            submit.isEnabled = checked
            submit.backgroundTintList = android.content.res.ColorStateList.valueOf(if (checked) design.danger else design.selected)
            submit.setTextColor(if (checked) (if (design.dark) Color.parseColor("#2A0B08") else Color.WHITE) else MUTED)
        }
        page.secondaryButton("保留我的设置", true) { dialog.dismiss() }
    }
}

internal fun MainActivity.personaLabel(mode: PersonaMode): String = when (mode) {
    PersonaMode.GENTLE -> "温柔"
    PersonaMode.PLAYFUL -> "活泼"
    PersonaMode.QUIET -> "安静"
    PersonaMode.SERIOUS -> "认真"
    PersonaMode.TSUNDERE_LITE -> "轻傲娇"
}

internal fun MainActivity.turnLabel(phase: TurnPhase): String = when (phase) {
    TurnPhase.IDLE -> "等待你说话"
    TurnPhase.LISTENING -> "正在听你说"
    TurnPhase.THINKING -> "正在思考"
    TurnPhase.SPEAKING -> "正在回答"
    TurnPhase.CANCELLING -> "正在停止"
    TurnPhase.CANCEL_UNCONFIRMED -> "停止结果待确认"
    TurnPhase.PLAYBACK_UNCONFIRMED -> "播放结果待确认"
    TurnPhase.OFFLINE -> "设备离线"
    TurnPhase.ERROR -> "对话出现异常"
}

internal fun MainActivity.emotionLabel(emotion: Emotion): String = when (emotion) {
    Emotion.UNKNOWN -> "未知"
    Emotion.NEUTRAL -> "平静"
    Emotion.HAPPY -> "开心"
    Emotion.SHY -> "害羞"
    Emotion.SAD -> "难过"
    Emotion.SURPRISED -> "惊讶"
    Emotion.THINKING -> "思考中"
}

internal fun MainActivity.permissionStateLabel(state: PermissionState): String = when (state) {
    PermissionState.NOT_GRANTED -> "尚未选择"
    PermissionState.ALLOWED -> "已允许"
    PermissionState.DENIED -> "已拒绝"
}

internal fun MainActivity.updatePhaseLabel(phase: UpdatePhase): String = when (phase) {
    UpdatePhase.UNKNOWN -> "更新状态未知"
    UpdatePhase.IDLE -> "暂无更新任务"
    UpdatePhase.AWAITING_LOCAL_CONFIRMATION -> "等待设备确认"
    UpdatePhase.DOWNLOADING -> "正在下载"
    UpdatePhase.VERIFYING -> "正在验证"
    UpdatePhase.STAGED -> "已准备安装"
    UpdatePhase.REBOOTING -> "正在重启"
    UpdatePhase.TRIAL -> "正在试运行"
    UpdatePhase.CONFIRMED -> "更新已完成"
    UpdatePhase.ROLLED_BACK -> "已恢复旧版本"
    UpdatePhase.FAILED -> "更新失败"
}

internal fun MainActivity.updateErrorLabel(error: ConsoleErrorCode): String = when (error) {
    ConsoleErrorCode.UPDATE_MANIFEST_INVALID -> "发布清单无效或不适用于当前设备"
    ConsoleErrorCode.UPDATE_SIGNATURE_INVALID -> "签名校验失败"
    ConsoleErrorCode.UPDATE_PAIR_MISMATCH -> "CP/AP 固件配对不匹配"
    ConsoleErrorCode.UPDATE_TRIAL_FAILED -> "新固件试运行失败"
    ConsoleErrorCode.UPDATE_DEVICE_ERROR -> "设备执行更新失败"
    else -> error.wireValue
}

internal fun MainActivity.capabilityLabel(capability: PrivacyCapability): String = when (capability) {
    PrivacyCapability.MICROPHONE -> "麦克风"
    PrivacyCapability.CAMERA -> "摄像头"
    PrivacyCapability.LOCATION -> "位置"
    PrivacyCapability.LONG_TERM_MEMORY -> "长期记忆"
    PrivacyCapability.AUTHORIZED_VOICE -> "授权声音"
}

internal fun MainActivity.dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()
