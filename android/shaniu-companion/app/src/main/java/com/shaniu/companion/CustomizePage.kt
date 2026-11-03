// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.TAB_UPDATE
import com.shaniu.companion.MainActivity.Companion.WAKE_MODEL_REQUEST
import com.shaniu.companion.MainActivity.Companion.EYE_PACK_REQUEST
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONA
import com.shaniu.companion.MainActivity.Companion.TAB_ADVANCED
import com.shaniu.companion.MainActivity.ConfigFlow
import android.content.Intent
import android.graphics.Color
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

// Customize tab: preview stage, expression, wake and resource entry points.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.renderAppearance() {
    val page = CompanionPage(this, content)
    page.pageTitle("定制", "一点点，变成你熟悉的她。")
    val moods = listOf(CompanionPresenceView.Mood.AWAKE, CompanionPresenceView.Mood.HAPPY, CompanionPresenceView.Mood.ASLEEP)
    val names = listOf("日常", "开心", "晚安")
    // The stage is always dark: it stands for the device glass, not a theme.
    val stage = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(18), dp(20), dp(16))
        background = design.shape(design.stage, dp(CompanionDesign.RADIUS_SHEET.toInt()).toFloat())
    }
    val portrait = CompanionPresenceView(this).apply { mood = moods[expressionPreview.coerceIn(0, 2)] }
    stage.addView(portrait, LinearLayout.LayoutParams(-1, dp(150)).apply { topMargin = dp(8) })
    stage.addView(TextView(this).apply {
        text = "薄荷眼睛"; design.style(this, CompanionDesign.Type.TITLE, design.stageInk)
        gravity = Gravity.CENTER; setPadding(0, dp(12), 0, 0)
    })
    // States plainly that this is a phone-side preview, under the name it qualifies.
    stage.addView(TextView(this).apply {
        text = "手机上的示意预览，不会改变设备"; design.style(this, CompanionDesign.Type.CAPTION, design.stageMuted)
        gravity = Gravity.CENTER; setPadding(0, dp(4), 0, dp(16))
    })
    val expressions = LinearLayout(this)
    val tiles = mutableListOf<LinearLayout>()
    fun refreshSelection() {
        tiles.forEachIndexed { index, tile ->
            tile.isSelected = index == expressionPreview
            tile.background = design.outline(if (tile.isSelected) Color.parseColor("#1C1C21") else Color.TRANSPARENT,
                if (tile.isSelected) design.eye else design.stageLine, dp(18).toFloat(),
                dp(if (tile.isSelected) 2 else 1).toFloat())
            tile.contentDescription = names[index] + "，仅预览" + if (tile.isSelected) "，当前已选择" else ""
        }
    }
    names.forEachIndexed { index, label ->
        val tile = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; setPadding(dp(8), dp(10), dp(8), dp(10))
            isClickable = true; isFocusable = true; minimumHeight = dp(72)
            addView(CompanionPresenceView(this@renderAppearance).apply { mood = moods[index]; still = true },
                LinearLayout.LayoutParams(-1, dp(30)))
            addView(TextView(this@renderAppearance).apply {
                text = label; design.style(this, CompanionDesign.Type.LABEL, design.stageInk); gravity = Gravity.CENTER
                setPadding(0, dp(6), 0, 0)
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            })
            setOnClickListener { expressionPreview = index; portrait.mood = moods[index]; refreshSelection() }
        }
        tiles.add(tile)
        expressions.addView(tile, LinearLayout.LayoutParams(0, -2, 1f).apply { if (index < 2) marginEnd = dp(8) })
    }
    refreshSelection()
    stage.addView(expressions, LinearLayout.LayoutParams(-1, -2))
    content.addView(stage, LinearLayout.LayoutParams(-1, -2))
    val editable = configAvailable() && settingsEditor == null && factoryReset == null
    page.sectionTitle("表情与外观")
    page.settingsRow("在设备上限时试用", "指定时长，到期恢复；不更改默认", enabled = editable, iconName = "eye") { showExpressionTrial() }
    page.settingsRow("默认表情", "刷新设备默认，或将已安装素材设为默认", enabled = editable, iconName = "eye") { showDefaultSelection() }
    page.settingsRow("资源更新", "眼睛、唤醒模型与应答音", iconName = "download") { updateResources = true; selectTab(TAB_UPDATE); render() }
    page.sectionTitle("声音与唤醒")
    val currentWake = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
    page.settingsRow(currentWake?.active?.let(::wakeModelSummary) ?: "当前唤醒词", if (currentWake == null) "连接后回读设备当前模型" else "设备当前唤醒模型", iconName = "mic") { showWakeSheet() }
    page.settingsRow("聊天风格", directSnapshot?.persona?.let { directPersonas.getOrNull(it) } ?: "选择你喜欢的陪伴方式",
        iconName = "spark") { selectTab(TAB_PERSONA); render() }
    page.settingsRow("回答与唤醒", "回答模式、回答长度与唤醒灵敏度", iconName = "speaker") { selectTab(TAB_ADVANCED); render() }
    page.settingsRow("唤醒应答", "查看本地应答设置", iconName = "speaker") {
        showCompanionSheet("唤醒应答", "本地应答与云端语音分开管理。") { body, _ ->
            CompanionPage(this, body).addCard("设备能力", "当前固件未提供独立的唤醒应答设置，不能在 App 中修改应答语。")
        }
    }
    page.sectionTitle("专注与陪伴")
    page.settingsRow("专注计时", "由设备计时，手机可随时回读", enabled = editable, iconName = "moon") { showFocusTimer() }
    page.settingsRow("场景卡片", "专注操作与本地节律内容", enabled = editable, iconName = "link") { showNfcBindings() }
    page.settingsRow("运行时技能", "查看场景、权限与启用状态", enabled = editable, iconName = "spark") { showAgentData(27) }
    page.settingsRow("原生功能", "安装经签名验证的设备功能", enabled = editable, iconName = "device") { showAgentData(28) }
    page.addMuted(if (editable) "实验模型单独标记，不会自动替换稳定默认。"
        else "连接设备且没有其他操作进行时，才能修改设备上的设置。实验模型不会自动替换稳定默认。")
}

internal fun MainActivity.renderCustomizationResources() {
    sectionTitle("声音与显示资源")
    val configMutationReady = configAvailable() && pendingWakeImport == null
    settingsRow("导入唤醒词模型", wakeMessage ?: "从本地导入已训练的唤醒词模型",
        enabled = configMutationReady) {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "application/octet-stream"; addCategory(Intent.CATEGORY_OPENABLE)
        }, WAKE_MODEL_REQUEST)
    }
    val bundled = listOf("nihao_openvela", "nihao_bingbing", "nihao_shaniu")
    val wakeNames = listOf("你好，openvela", "你好冰冰", "你好傻妞")
    bundled.forEachIndexed { index, label ->
        val available = try { assets.open("wake-models/$label.wkm").use(WakeModelPackage::read) != null } catch (_: Exception) { false }
        settingsRow(wakeNames[index], if (available) "切换到此唤醒词" else "此版本暂未提供", enabled = available && configMutationReady) { selectBundledWakeModel(label) }
    }
    val currentWake = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation }
    settingsRow("读取当前唤醒词", currentWake?.active?.let(::wakeModelSummary) ?: "从设备读取实际模型", enabled = configMutationReady) { requestWakeStatus() }
    if (pendingWakeImport != null)
        settingsRow("取消模型导入", "取消等待；模型尚未发送", enabled = true) {
            cancelPendingWakeImport("已取消导入，尚未向设备发送模型"); render()
        }
    if (configFlow == ConfigFlow.WAKE && wakePayload != null && !wakeApplied)
        settingsRow("取消模型传输", "保留设备当前模型", enabled = !wakeCanceling) {
            failWake("模型传输已取消，保留原模型"); render()
        }
    settingsRow("导入眼睛素材包", eyeMessage ?: "从本地导入 .bkep 眼睛素材包",
        enabled = !eyeImportPending && configFlow == ConfigFlow.NONE) {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "application/octet-stream"; addCategory(Intent.CATEGORY_OPENABLE)
        }, EYE_PACK_REQUEST)
    }
    val selectedEyes = selectedEyePack
    settingsRow("通过 Wi-Fi 安装所选眼睛", selectedEyes?.let { "${it.packId} · 版本 ${it.revision}" }
        ?: "请先导入眼睛素材包", enabled = selectedEyes != null && !eyeImportPending && configAvailable()) {
        startEyeInstall()
    }
    settingsRow("读取当前眼睛", "读取设备实际安装状态", enabled = configAvailable()) { readCurrentEyes() }
    settingsRow("恢复上一唤醒词模型", currentWake?.previous?.let { "恢复为 ${wakeModelSummary(it)}" }
        ?: "恢复前先读取设备保存的上一模型", enabled = configMutationReady) { restoreWakeModel() }
}
