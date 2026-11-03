// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.TAB_INTERACTION
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONALITY
import com.shaniu.companion.MainActivity.Companion.TAB_PRIVACY
import com.shaniu.companion.MainActivity.Companion.TAB_UPDATE
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import com.shaniu.companion.protocol.CompanionState
import com.shaniu.companion.protocol.ConsoleMutationArguments
import com.shaniu.companion.protocol.ConsoleOperation
import com.shaniu.companion.protocol.TurnPhase
import com.shaniu.companion.protocol.FirmwareRelease
import com.shaniu.companion.protocol.MemoryDeleteScope
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.protocol.UpdatePhase

// Pages for the legacy HTTPS console mode (legacyConsoleMode).
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.statusStrip() {
    val state = store?.state
    val transport = when {
        busy -> "连接中"
        runtime == null -> "未连接"
        eventConnected -> "HTTPS + WSS 在线"
        else -> "HTTPS 在线 / WSS 断开"
    }
    addCard(
        "连接状态",
        "$transport\n设备：${state?.deviceId ?: draftDeviceId.ifBlank { "未配置" }}" +
            "\n最近操作：$lastAction",
    )
}

internal fun MainActivity.renderOverview() {
    content.addView(CompanionPresenceView(this).apply {
        mood = when { busy -> CompanionPresenceView.Mood.SEARCHING; runtime == null -> CompanionPresenceView.Mood.OFFLINE
            else -> CompanionPresenceView.Mood.AWAKE }
    }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(200)).apply { bottomMargin = dp(24) })
    val hasBinding = draftDeviceId.isNotBlank() && !controlCredentialRequired
    val needsControlBinding = expectedControlDeviceId().isNotBlank() && !hasBinding
    sectionTitle(if (runtime == null && !hasBinding && !needsControlBinding) "让陪伴，从这里开始" else "今天，也在你身边")
    addMuted(if (busy) "正在连接你的傻妞，稍等一下。"
             else if (runtime == null && needsControlBinding) "傻妞已保存网络设置；绑定 App 控制凭据后即可连接。"
             else if (runtime == null && hasBinding) "暂时没有连上，请确认傻妞和手机都已联网。"
             else if (runtime == null) "连接你的傻妞，一起说说今天的事。"
             else if (!eventConnected) "正在恢复连接，稍等一下。"
             else "已连接。可以查看设备状态、调节音量或停止当前对话。")
    val primaryAction = when {
        runtime != null -> "对话与音量"
        needsControlBinding -> "绑定 App 控制"
        hasBinding -> "重新连接"
        else -> "添加傻妞"
    }
    actionButton(primaryAction, !busy) {
        when {
            runtime != null -> { selectTab(TAB_PERSONALITY); render() }
            needsControlBinding -> startConsoleEnrollmentImport()
            hasBinding -> connect(draftOrigin, draftDeviceId, draftPins, "")
            else -> startProvisioning()
        }
    }
    if (runtime != null) {
        actionButton("对话状态") { selectTab(TAB_INTERACTION); render() }
    } else if (!hasBinding && !needsControlBinding) {
        actionButton("连接已有设备", !busy) { startConsoleEnrollmentImport() }
    }
}

internal fun MainActivity.renderSettings() {
    if (developerPanel) { renderDeveloperConnection(); return }
    sectionTitle("我的傻妞")
    val expectedDevice = expectedControlDeviceId()
    val hasBinding = draftDeviceId.isNotBlank() && !controlCredentialRequired
    addCard("设备", when {
        runtime != null -> "已连接"
        expectedDevice.isNotBlank() && !hasBinding -> "网络已设置，等待绑定 App 控制"
        else -> "尚未连接"
    })
    if (expectedDevice.isNotBlank()) {
        actionButton(if (hasBinding) "更换 App 控制凭据" else "绑定 App 控制凭据", !busy) {
            startConsoleEnrollmentImport()
        }
    } else {
        actionButton("连接已有设备", !busy) { startConsoleEnrollmentImport() }
    }
    actionButton("添加设备") {
        startProvisioning()
    }
    actionButton("隐私与权限") { selectTab(TAB_PRIVACY); render() }
    actionButton("设备更新") { selectTab(TAB_UPDATE); render() }
    if (expectedDevice.isNotBlank() || draftDeviceId.isNotBlank()) {
        actionButton("清除本机连接资料", !busy) { confirmClearProvisioning() }
    }
    if (BuildConfig.LEGACY_SERVICE_DEMO &&
        (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
        sectionTitle("开发工具")
        actionButton("开发者联调") { developerPanel = true; render() }
    }
}

internal fun MainActivity.renderDeveloperConnection() {
    actionButton("认领设备与 Wi-Fi 配网") {
        startProvisioning(developerMode = true)
    }
    sectionTitle("连接配置")
    val originInput = editField("HTTPS Gateway，例如 https://gateway.example.com", draftOrigin)
    val deviceInput = editField("设备 ID", draftDeviceId)
    val pinsInput = editField(
        "可选 SPKI pin，每行一个 sha256/...",
        draftPins,
        multiline = true,
    )
    val tokenInput = editField(
        "访问令牌（留空则使用已保存令牌）",
        "",
        secret = true,
    )
    actionButton(if (runtime == null) "保存并连接" else "重新连接", enabled = !busy) {
        draftOrigin = originInput.text.toString().trim()
        draftDeviceId = deviceInput.text.toString().trim()
        draftPins = pinsInput.text.toString().trim()
        val suppliedToken = tokenInput.text.toString()
        tokenInput.setText("")
        connect(draftOrigin, draftDeviceId, draftPins, suppliedToken)
    }
    actionButton("刷新设备快照", enabled = runtime != null && !busy) { refreshSnapshot() }
    actionButton("断开", enabled = runtime != null || busy) {
        autoReconnectAllowed = false
        closeRuntime(clearReportedState = true)
        lastAction = "已断开；本机配置和加密令牌仍保留"
        render()
    }
    actionButton("清除本机绑定资料", enabled = !busy) {
        confirmClearProvisioning()
    }

    sectionTitle("设备报告")
    val state = store?.state
    if (state == null) {
        addMuted("连接成功后显示 Gateway 返回的设备快照。")
    } else {
        addCard(
            "${state.presence} · ${state.turn}",
            "Gateway：${state.gateway}\n" +
                "固件：${state.firmwareVersion ?: "未知"}\n" +
                "电量：${state.batteryPercent?.let { "$it%" } ?: "未知"}" +
                (when (state.charging) {
                    true -> "（充电中）"
                    false -> ""
                    null -> "（充电状态未知）"
                }) +
                    "\n代次/事件：${state.generation}/${state.lastSequence}\n" +
                    "修订：${state.revision}\n" +
                    "需全量同步：${if (state.needsSnapshot) "是" else "否"}",
        )
    }
}

internal fun MainActivity.renderInteraction() {
    sectionTitle("语音交互")
    addMuted("在这里查看对话状态，或停止当前对话。")
    val state = requireReportedState("查看对话状态") ?: return
    addCard("当前对话", "${turnLabel(state.turn)}\n当前心情：${emotionLabel(state.emotion)}")
    val cancellable = state.turn == TurnPhase.LISTENING ||
        state.turn == TurnPhase.THINKING || state.turn == TurnPhase.SPEAKING
    actionButton("停止当前对话", canMutate() && cancellable) {
        submitMutation(ConsoleOperation.CANCEL_TURN)
    }
}

internal fun MainActivity.renderPersonality() {
    sectionTitle("心情与声音")
    val state = requireReportedState("调整相处方式和声音") ?: return
    addCard(
        "当前设置",
        "声音：${state.volumePercent?.let { "$it%" } ?: "未知"}\n" +
            "相处方式：${personaLabel(state.personaMode)}\n" +
            "设备确认后，界面才会显示为已生效。",
    )
    val volumeLabel = TextView(this).apply {
        text = state.volumePercent?.let { getString(R.string.volume_slider_format, it) }
            ?: "等待设备上报音量"
        textSize = 16f
        setTextColor(INK)
    }
    content.addView(volumeLabel)
    content.addView(
        SeekBar(this).apply {
            max = 100
            progress = state.volumePercent ?: 0
            isEnabled = canMutate() && state.volumePercent != null
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    volumeLabel.text = getString(R.string.volume_slider_format, value)
                }

                override fun onStartTrackingTouch(bar: SeekBar?) = Unit

                override fun onStopTrackingTouch(bar: SeekBar?) {
                    submitMutation(
                        ConsoleOperation.SET_VOLUME,
                        ConsoleMutationArguments.SetVolume(bar?.progress ?: return),
                    )
                }
            })
        },
        LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        ),
    )
    PersonaMode.values().forEach { mode ->
        actionButton(
            if (mode == state.personaMode) "${personaLabel(mode)}（当前）" else personaLabel(mode),
            canMutate() && mode != state.personaMode,
        ) {
            submitMutation(
                ConsoleOperation.SET_PERSONA_MODE,
                ConsoleMutationArguments.SetPersonaMode(mode),
            )
        }
    }
}

internal fun MainActivity.renderPrivacy() {
    sectionTitle("隐私权限")
    val state = requireReportedState("管理隐私与权限") ?: return
    PrivacyCapability.values().forEach { capability ->
        val current = state.permissions[capability] ?: PermissionState.NOT_GRANTED
        addCard(capabilityLabel(capability), "当前状态：${permissionStateLabel(current)}")
        if (capability == PrivacyCapability.LONG_TERM_MEMORY) {
            renderLongTermMemoryControls(current)
        }
    }
}

internal fun MainActivity.renderLongTermMemoryControls(state: PermissionState) {
    when (state) {
        PermissionState.ALLOWED -> {
            addMuted("仅在傻妞确认播放完成后保存对话文本。关闭会停止后续保存；清除会删除已保存的对话记忆。")
            actionButton("关闭长期记忆", canMutate()) {
                confirm(
                    title = "关闭长期记忆",
                    message = "将停止傻妞后续保存对话记忆，并删除已经保存的对话内容。此操作需要本机确认。",
                ) {
                    submitMutation(
                        ConsoleOperation.CONFIGURE_PERMISSION,
                        ConsoleMutationArguments.ConfigurePermission(
                            PrivacyCapability.LONG_TERM_MEMORY,
                            PermissionState.DENIED,
                        ),
                        locallyConfirmed = true,
                    )
                }
            }
            actionButton("清除对话记忆", canMutate()) {
                confirm(
                    title = "清除对话记忆",
                    message = "将删除傻妞已经保存的对话记忆，但长期记忆仍保持开启。此操作需要本机确认。",
                ) {
                    submitMutation(
                        ConsoleOperation.DELETE_MEMORY,
                        ConsoleMutationArguments.DeleteMemory(MemoryDeleteScope.CONVERSATIONS),
                        locallyConfirmed = true,
                    )
                }
            }
        }
        PermissionState.NOT_GRANTED ->
            addMuted("长期记忆默认关闭；当前版本需由设备所有者在受控服务配置中明确启用，App 不提供远程开启。")
        PermissionState.DENIED ->
            addMuted("长期记忆已关闭并清除；当前 App 不提供重新开启入口。")
    }
}

internal fun MainActivity.renderUpdate() {
    sectionTitle("固件更新")
    val state = requireReportedState("查看设备更新") ?: return
    addCard(
        updatePhaseLabel(state.update.phase),
        "当前：${state.firmwareVersion ?: "未知"}\n" +
            "目标：${state.update.targetVersion ?: if (state.update.phase == UpdatePhase.UNKNOWN) "未知" else "无"}\n" +
            "进度：${if (state.update.phase == UpdatePhase.UNKNOWN) "未知" else "${state.update.progressPercent}%"}\n" +
            (state.update.error?.let { "原因：${updateErrorLabel(it)}\n" } ?: "") +
            "只有设备确认完成，才表示升级成功。",
    )
    actionButton("刷新已验证发布列表", canMutate() && state.generation > 0) {
        fetchReleases()
    }
    val catalog = releaseCatalog
    if (catalog == null) {
        addMuted("尚未从 Gateway 获取发布清单。")
        return
    }
    if (catalog.releases.isEmpty()) {
        addMuted("Gateway 当前没有适用于该设备的发布。")
    }
    catalog.releases.forEach { release -> renderRelease(release, state) }
}

internal fun MainActivity.renderRelease(release: FirmwareRelease, state: CompanionState) {
    val phaseAllowsStart = state.update.phase in setOf(
        UpdatePhase.IDLE,
        UpdatePhase.FAILED,
        UpdatePhase.ROLLED_BACK,
    )
    val sourceMatches = state.firmwareVersion == release.requiredSourceVersion
    val canStart = canMutate() &&
        state.turn == TurnPhase.IDLE &&
        phaseAllowsStart &&
        sourceMatches
    addCard(
        "版本 ${release.targetVersion}",
        "当前版本要求：${release.requiredSourceVersion}\n" +
            "发布清单和安装包摘要已由 Gateway 验证。",
    )
    addMuted(
        when {
            state.update.phase in setOf(
                UpdatePhase.AWAITING_LOCAL_CONFIRMATION,
                UpdatePhase.DOWNLOADING,
                UpdatePhase.VERIFYING,
                UpdatePhase.STAGED,
                UpdatePhase.REBOOTING,
                UpdatePhase.TRIAL,
            ) -> "已有更新任务进行中；请等待设备上报最终结果。"
            !sourceMatches -> "设备当前版本与该增量包的来源版本不匹配。"
            state.turn != TurnPhase.IDLE -> "请等当前对话结束后再开始更新。"
            else -> "请求只携带已验证发布的摘要；设备会独立下载并校验签名。"
        },
    )
    actionButton("更新到 ${release.targetVersion}", canStart) {
        confirm(
            title = "安装固件 ${release.targetVersion}",
            message = "更新期间傻妞会停止对话并重启。只有设备完成试运行并上报确认后，App 才会显示更新成功。",
        ) {
            submitMutation(
                ConsoleOperation.REQUEST_FIRMWARE_UPDATE,
                ConsoleMutationArguments.FirmwareUpdate(release.manifestSha256),
                locallyConfirmed = true,
            )
        }
    }
}
