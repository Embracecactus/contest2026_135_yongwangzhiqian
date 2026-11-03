// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.TAB_SERVICES
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONA
import com.shaniu.companion.MainActivity.Companion.TAB_ADVANCED
import com.shaniu.companion.MainActivity.ConfigFlow
import android.app.AlertDialog
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import com.shaniu.companion.provision.DeviceControlPresentation
import com.shaniu.companion.provision.DeviceControlProtocol

// Settings tab and its detail pages: voice, services, privacy and network.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.renderSettingsHome(bound: Boolean) {
    val page = CompanionPage(this, content)
    page.pageTitle("设置", "让连接、声音和隐私都由你掌握。")
    page.settingsRow("我的傻妞", if (bound) "设备归属 · 当前手机已认领" else "尚未添加设备", iconName = "pair") { showConnectionSheet() }
    page.settingsRow("Wi-Fi", when (directSnapshot?.wifiReady) {
        true -> "设备已连接网络"; false -> "设备未联网"; else -> "连接设备后读取"
    }, iconName = "wifi") { openNetworkSettings() }
    page.settingsRow("云服务与模型", "对话、语音识别、语音合成", iconName = "cloud") { selectTab(TAB_SERVICES); render() }
    page.settingsRow("回答与唤醒", "音量、回答模式、唤醒灵敏度", iconName = "speaker") { selectTab(TAB_ADVANCED); render() }
    sectionTitle("隐私与授权")
    val editable = configAvailable() && settingsEditor == null && factoryReset == null
    page.settingsRow("隐私与记忆", "管理对话记忆与数据", iconName = "shield") { showPrivacySheet() }
    page.settingsRow("电脑授权", "配对、查看或撤销电脑的访问权限", enabled = editable, iconName = "phone") { showPcAuthorization() }
    page.settingsRow("电脑审批", "核对并批准或拒绝电脑上 AI 助手请求的操作", enabled = editable, iconName = "check") { showPcApproval() }
    sectionTitle("使用偏好")
    page.settingsRow("外观模式", "跟随系统 · " + if (design.dark) "深色" else "浅色", iconName = "moon") {
        showCompanionSheet("外观模式", "跟随手机系统的浅色或深色模式。") { body, _ ->
            CompanionPage(this, body).addCard("系统外观", "在手机的显示设置中切换外观，App 将自动跟随。")
        }
    }
    page.settingsRow("帮助与诊断", "连接问题与设备状态", iconName = "help") {
        showCompanionSheet("连接问题与设备状态", "从手机、网络到云服务，逐项检查。") { body, _ ->
            val details = CompanionPage(this, body)
            connectionChain(details, null)
            details.addCard("蓝牙连接", if (directSession.current().authenticated) "设备身份已验证。" else "请保持设备在附近，先扫码认领，再连接蓝牙。")
            details.addCard("设备网络", "蓝牙连通不代表设备已联网。网络中断时，可通过蓝牙重新配网，无需重新认领。")
            details.settingsRow("云服务", "检查服务地址、模型与密钥", iconName = "cloud") { selectTab(TAB_SERVICES); render() }
            details.settingsRow("声音与回答", "音量、回答模式、唤醒灵敏度", iconName = "speaker") { selectTab(TAB_ADVANCED); render() }
            if (directMessage.isNotBlank()) details.addMuted(directMessage)
        }
    }
    sectionTitle("设备归属")
    if (!bound) page.settingsRow("添加傻妞", "扫描设备屏幕，离线安全认领", iconName = "plus") { startProvisioning() }
    else {
        page.settingsRow("转交或恢复出厂", "清除用户配置，撤销旧控制凭据",
            enabled = directSession.current().authenticated && directSession.current().snapshotFresh &&
                !directPending && settingsEditor == null && factoryReset == null,
            danger = true, iconName = "refresh") { confirmFactoryReset() }
        if (com.shaniu.companion.provision.FactoryResetController.hasPending(this, provisionedDeviceId))
            page.settingsRow("核对恢复出厂结果", "只查询回执，不重复清理", directSession.current().authenticated && !directPending,
                iconName = "info") { queryFactoryReset() }
    }
    content.addView(LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL
        setPadding(0, dp(32), 0, dp(8))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        addView(CompanionPresenceView(this@renderSettingsHome).apply { mood = presenceMood(); still = true },
            LinearLayout.LayoutParams(dp(56), dp(26)))
    }, LinearLayout.LayoutParams(-1, -2))
    content.addView(TextView(this).apply {
        text = "傻妞 App ${BuildConfig.VERSION_NAME} · 设备状态以回读为准"
        design.style(this, CompanionDesign.Type.CAPTION, MUTED); gravity = Gravity.CENTER
    })
}

internal fun MainActivity.renderVoiceAdvanced() {
    CompanionPage(this, content).pageTitle("回答与唤醒", "她怎样回应你，多容易被唤醒。", ::navigateBack)
    val configSupported = directSnapshot?.publicConfigSupported == true
    val configMutationReady = configAvailable() && pendingWakeImport == null
    settingsRow("服务连接与凭据", "查看服务地址，按需更新密钥", configMutationReady) { openDeviceSettings(true) }
    sectionTitle("声音与对话")
    val volumeControl = DeviceControlPresentation.volume(directSession.current())
    settingsRow("扬声器音量", volumeControl.reason, enabled = volumeControl.enabled) { editDirectVolume() }
    settingsRow("聊天风格", directSnapshot?.persona?.let { directPersonas[it] }
        ?: "选择你喜欢的陪伴方式") { selectTab(TAB_PERSONA); render() }
    val responseCurrent = responseModeGeneration == directSession.current().generation
    val responseText = when {
        !directSession.current().authenticated -> "连接并验证设备后读取"
        !directSession.current().snapshotFresh -> "设备状态待刷新，尚未确认"
        !configSupported -> "当前固件未提供此设置"
        configFlow == ConfigFlow.RESPONSE -> "正在读取或保存回答模式…"
        !responseCurrent -> responseModeError ?: "点击读取设备实际回答模式"
        responseMode == 0 -> "快速对话 · 关闭深度思考\n识别和语音合成仍需网络等待"
        else -> "深度思考 · 回答可能更慢"
    }
    settingsRow("回答模式", responseText, enabled = configMutationReady) {
        if (responseCurrent) editResponseMode() else { requestResponseModeRead(); render() }
    }
    val responseLengthCurrent = responseLengthGeneration == directSession.current().generation
    val responseLengthText = when {
        !directSession.current().authenticated -> "连接并验证设备后读取"
        !directSession.current().snapshotFresh -> "设备状态待刷新，尚未确认"
        !configSupported -> "当前固件未提供此设置"
        configFlow == ConfigFlow.RESPONSE_LENGTH -> "正在读取或保存回答长度…"
        !responseLengthCurrent -> responseLengthError ?: "点击读取设备实际回答长度"
        responseLengthApplied != responseLength -> "${responseLengthLabel(responseLength ?: 0)}；服务尚未应用，点击重新读取"
        else -> "${responseLengthLabel(responseLength ?: 0)}；明确的本轮用户要求优先"
    }
    settingsRow("回答长度", responseLengthText, enabled = configMutationReady) {
        if (responseLengthCurrent && responseLengthApplied == responseLength) editResponseLength()
        else { requestResponseLengthRead(); render() }
    }
    val sensitivityCurrent = wakeSensitivityGeneration == directSession.current().generation
    val sensitivityText = when {
        !directSession.current().authenticated -> "连接并验证设备后读取"
        !directSession.current().snapshotFresh -> "设备状态待刷新，尚未确认"
        !configSupported -> "当前固件未提供此设置"
        configFlow == ConfigFlow.SENSITIVITY -> "正在读取或保存唤醒门限…"
        !sensitivityCurrent -> wakeSensitivityError ?: "点击读取设备实际门限"
        wakeSensitivityPercent != null -> "设备门限 %.2f（越低越易唤醒，也可能误唤醒）".format(wakeSensitivityPercent!! / 100.0)
        else -> "尚未读取设备门限"
    }
    settingsRow("唤醒灵敏度", sensitivityText, enabled = configMutationReady) {
        if (sensitivityCurrent) editWakeSensitivity()
        else { requestWakeSensitivityRead(); render() }
    }
    if (cloudModelsExpected != null) settingsRow("取消模型保存", "停止当前配置事务；不会重放未完成写入", enabled = true) {
        failCloudModels("模型配置已取消")
        directMessage = if (cloudModelsCanceling) "正在取消模型配置" else "模型配置已取消"
        render()
    }
}

internal fun MainActivity.renderServices(bound: Boolean) {
    val page = CompanionPage(this, content)
    page.pageTitle("云服务与模型", "听、想、说，分别选择适合的服务。", ::navigateBack)
    if (configAvailable() && pendingWakeImport == null && settingsEditor == null) {
        // Reading configuration publishes state; construct after this render.
        mainHandler.post {
            if (!destroyed && currentTab == TAB_SERVICES && settingsEditor == null &&
                configAvailable() && pendingWakeImport == null) openDeviceSettings(true)
        }
    } else {
        page.featureCard("cloud", "连接后，选择陪伴的声音。", when {
            !bound -> "先添加你的傻妞，再读取云服务与模型。"
            !directSession.current().authenticated -> "连接并验证设备后，显示已保存的配置。"
            !directSession.current().snapshotFresh -> "正在读取设备能力，请稍候。"
            directSnapshot?.publicConfigSupported != true -> "当前固件未提供云服务配置能力。"
            else -> "请等待当前设备操作结束。"
        })
        if (!bound) primaryButton("添加我的傻妞", !busy) { startProvisioning() }
        else if (!directSession.current().authenticated)
            primaryButton(if (directConnecting) "正在连接…" else "连接我的傻妞", !directConnecting) { scanDirect() }
    }
}

internal fun MainActivity.renderPrivacyControls(body: LinearLayout) {
    val page = CompanionPage(this, body)
    if (directSession.current().authenticated && directSnapshot != null) {
        val memory = directSnapshot!!
        val canManage = directSession.current().snapshotFresh && !directPending && memoryResultMessage == null && memory.ready && !memory.busy && memory.memorySupported && !memory.memoryPending && memory.memoryEnabled != null
        page.settingsRow("对话记忆", when {
            !memory.memorySupported -> "设备固件尚未提供此功能"
            memory.memoryPending -> "正在处理，请稍候"
            memory.memoryFailed -> "上次操作未确认，请核对设备状态"
            memory.memoryEnabled == true -> "已开启 · 加密保存近期对话和事实"
            memory.memoryEnabled == false -> "已关闭 · 不读取或新增保存"
            else -> "正在读取设备设置"
        }, enabled = canManage, iconName = "shield", checked = memory.memoryEnabled == true) {
            val enable = memory.memoryEnabled != true
            confirm(if (enable) "开启跨重启记忆" else "关闭跨重启记忆",
                if (enable) "允许设备加密保存最近三轮对话及明确表达的称呼、偏好和近期话题，并在下次启动后继续使用。你可以查看、纠正或删除事实。" else "停止读取和保存跨重启记忆。已保存的内容会保留，可通过下方入口删除。") {
                memoryResultMessage = if (enable) "跨重启记忆已开启" else "跨重启记忆已关闭"
                memoryDesiredEnabled = enable
                memoryRequestAccepted = false
                if (!directRequest(DeviceControlProtocol.Command.MEMORY_SET, if (enable) 1 else 0)) {
                    memoryResultMessage = null; memoryDesiredEnabled = null
                }
            }
        }
        page.settingsRow("查看与编辑事实", "称呼、偏好与近期话题", enabled = canManage && memory.memoryEnabled == true && configAvailable()) { showAgentData(26) }
        page.settingsRow("删除已保存的记忆", "清除本地记忆和近期上下文，并关闭跨重启记忆", enabled = canManage) {
            confirm("删除设备记忆", "此操作无法撤销：设备将使旧的加密记忆失效，清空近期上下文，并关闭跨重启记忆。云端服务保留的记录不在此范围内。") {
                memoryResultMessage = "设备记忆已删除，跨重启记忆已关闭"
                memoryDesiredEnabled = false
                memoryRequestAccepted = false
                if (!directRequest(DeviceControlProtocol.Command.MEMORY_DELETE)) {
                    memoryResultMessage = null; memoryDesiredEnabled = null
                }
            }
        }
        page.settingsRow("清空近期对话", if (directSnapshot?.busy == true)
            "请等待当前对话结束" else "让下一次聊天从新的话题开始",
            enabled = directSession.current().snapshotFresh && !directPending && directSnapshot?.ready == true && directSnapshot?.busy == false) {
            confirm("清空近期对话", "清除设备用于继续聊天的近期上下文。云端服务可能保留的记录不在此清除范围内，人物与配网设置会保留。") {
                directRequest(DeviceControlProtocol.Command.CLEAR_HISTORY)
            }
        }
    } else if (provisionedDeviceId.isNotBlank()) page.primaryButton("连接设备以管理记忆", !directConnecting) { dismissCompanionSheet(); scanDirect() }
    else page.primaryButton("先添加我的傻妞", !busy) { dismissCompanionSheet(); startProvisioning() }

    page.sectionTitle("数据如何使用")
    page.notice("默认仅保留本次开机的近期上下文。开启记忆后，设备加密保存最近三轮对话和事实，并在对话时发送给你配置的云服务。云端保留的记录需在对应服务中管理。服务密钥不提供明文回读。")
}

internal fun MainActivity.openNetworkSettings() {
    dismissCompanionSheet()
    if (configAvailable()) editDeviceSettings()
    else if (provisionedDeviceId.isBlank()) startProvisioning()
    else if (!directSession.current().authenticated) scanDirect()
    else AlertDialog.Builder(this).setTitle("暂时无法修改网络")
        .setMessage("设备配置能力尚未就绪，或另一项操作正在进行。请稍后再试。")
        .setPositiveButton("知道了", null).show()
}
