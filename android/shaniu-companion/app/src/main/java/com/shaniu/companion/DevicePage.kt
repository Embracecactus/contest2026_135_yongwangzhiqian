// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.KEY_OTA_EXPECTED_PENDING
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONALITY
import com.shaniu.companion.MainActivity.Companion.TAB_UPDATE
import com.shaniu.companion.MainActivity.Companion.TAB_SETTINGS
import com.shaniu.companion.MainActivity.Companion.TAB_SERVICES
import com.shaniu.companion.MainActivity.Companion.TAB_PERSONA
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.shaniu.companion.provision.DeviceControlSession
import com.shaniu.companion.provision.DeviceControlPresentation
import com.shaniu.companion.provision.DeviceControlProtocol

// Device tab: home, presence, connection chain, persona and the discovery sheet.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

/** Projects verified session state onto her eyes. Never optimistic: an
 * unauthenticated or stale link cannot look awake. */
internal fun MainActivity.presenceMood(): CompanionPresenceView.Mood {
    val state = directSession.current()
    val snapshot = state.snapshot.takeIf { state.authenticated && state.snapshotFresh }
    return when {
        provisionedDeviceId.isBlank() && !directConnecting && directScanner == null -> CompanionPresenceView.Mood.ABSENT
        state.connection == DeviceControlSession.Connection.CONNECTING ||
            state.connection == DeviceControlSession.Connection.RECONNECT_WAIT ||
            (!state.authenticated && (directConnecting || directScanner != null)) -> CompanionPresenceView.Mood.SEARCHING
        !state.authenticated -> CompanionPresenceView.Mood.OFFLINE
        snapshot == null -> CompanionPresenceView.Mood.UNSURE
        snapshot.busy -> CompanionPresenceView.Mood.ENGAGED
        snapshot.wifiReady == false || !snapshot.ready -> CompanionPresenceView.Mood.DROWSY
        else -> CompanionPresenceView.Mood.AWAKE
    }
}

/** Phone → device → voice service, each hop only as strong as its evidence. */
internal fun MainActivity.connectionChain(page: CompanionPage, action: (() -> Unit)?) {
    val state = directSession.current()
    val snapshot = state.snapshot.takeIf { state.authenticated && state.snapshotFresh }
    val phoneLink = when {
        state.authenticated -> CompanionPage.Link.LIVE
        state.connection == DeviceControlSession.Connection.CONNECTING ||
            state.connection == DeviceControlSession.Connection.RECONNECT_WAIT || directConnecting -> CompanionPage.Link.PENDING
        else -> CompanionPage.Link.OFF
    }
    val networkLink = when (snapshot?.wifiReady) {
        true -> CompanionPage.Link.LIVE
        false -> CompanionPage.Link.BROKEN
        null -> if (state.authenticated) CompanionPage.Link.UNKNOWN else CompanionPage.Link.OFF
    }
    val management = if (state.authenticated) "管理已连接" else "管理未连接"
    val network = when (snapshot?.wifiReady) {
        true -> "Wi-Fi 已连接"
        false -> "设备未联网"
        null -> "联网待确认"
    }
    page.connectionChain(
        nodes = listOf(
            CompanionPage.ChainNode("phone", "手机", when (phoneLink) {
                CompanionPage.Link.LIVE -> "蓝牙已验证"
                CompanionPage.Link.PENDING -> "正在连接"
                else -> "未连接"
            }),
            CompanionPage.ChainNode("pair", "傻妞", when {
                snapshot?.busy == true -> "对话中"
                snapshot != null -> "在线"
                state.authenticated -> "读取中"
                else -> "—"
            }),
            CompanionPage.ChainNode("cloud", "语音服务", when {
                snapshot == null -> "待确认"
                snapshot.wifiReady == false -> "无网络"
                snapshot.ready -> "就绪"
                else -> "未就绪"
            }),
        ),
        links = listOf(phoneLink, networkLink),
        summary = "$management · $network",
        description = "$management，$network，查看网络设置",
        action = action,
    )
}

internal fun MainActivity.renderHome(bound: Boolean) {
    val page = CompanionPage(this, content)
    val state = directSession.current()
    val fresh = state.authenticated && state.snapshotFresh
    val snapshot = state.snapshot.takeIf { fresh }
    page.homeHeader("傻妞", "添加或查找设备") {
        if (bound) scanDirect() else startProvisioning()
    }
    page.presence(presenceMood(), if (resources.configuration.fontScale > 1.3f) 132 else 168)
    content.addView(View(this), LinearLayout.LayoutParams(-1, dp(12)))
    page.hero(when {
        !bound -> "认识一下，傻妞。"
        !state.authenticated -> "让傻妞，回到你身边。"
        !fresh -> "正在了解傻妞的状态。"
        snapshot?.wifiReady == false -> "先帮我连上网。"
        snapshot?.busy == true -> "正在处理这次对话。"
        snapshot?.ready == false -> "语音服务尚未就绪。"
        else -> "我在，随时听你说。"
    }, if (!bound) "确认是你的设备，再开始连接。" else directStatus(),
        // A failed verification is something to act on, not ambient status.
        warning = bound && state.connection == DeviceControlSession.Connection.DISCONNECTED && state.error != null)
    if (!bound) {
        primaryButton("添加傻妞 · 扫码连接", !busy) { startProvisioning() }
        page.secondaryButton(if (directConnecting) "正在查找附近设备…" else "查找附近的傻妞", !busy && !directConnecting) { scanDirect() }
        content.addView(TextView(this).apply {
            text = "无需云账号 · 认领后再设置网络和语音服务"
            design.style(this, CompanionDesign.Type.CAPTION, MUTED)
            gravity = Gravity.CENTER; setPadding(0, dp(16), 0, dp(8))
        })
        page.sectionTitle("傻妞能做什么")
        page.informationRow("一句你好，唤醒她", "设备独立对话，关闭 App 也不打断", "mic")
        page.informationRow("一点点，变成你熟悉的她", "眼睛、表情、聊天风格和唤醒词", "eye")
        page.informationRow("每次进步，都清楚可见", "固件和资源更新，装好后回读确认", "download")
        return
    }
    // One contextual action at a time, in order of what unblocks her most.
    if (!state.authenticated && state.connection != DeviceControlSession.Connection.CONNECTING &&
        state.connection != DeviceControlSession.Connection.RECONNECT_WAIT)
        primaryButton(if (directConnecting) "正在连接…" else "连接我的傻妞", !directConnecting) { scanDirect() }
    if (snapshot?.busy == true && snapshot.memoryPending != true)
        page.secondaryButton("停止这次对话", !directPending) { directRequest(DeviceControlProtocol.Command.CANCEL) }
    if (snapshot?.wifiReady == false) {
        val notice = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(18), dp(20), dp(16))
            background = design.shape(design.warningSurface, dp(CompanionDesign.RADIUS_GROUP.toInt()).toFloat())
            addView(TextView(this@renderHome).apply {
                text = "连接还在，不必重新认领"
                design.style(this, CompanionDesign.Type.HEADLINE, design.warning)
                setCompoundDrawablesRelativeWithIntrinsicBounds(CompanionIcons.drawable(this@renderHome, "wifi", design.warning).apply {
                    setBounds(0, 0, dp(20), dp(20))
                }, null, null, null)
                compoundDrawablePadding = dp(10)
            })
        }
        CompanionPage(this, notice).addMuted("手机仍可通过蓝牙为傻妞更换网络。")
        CompanionPage(this, notice).primaryButton("更换 Wi-Fi", configAvailable()) { editDeviceSettings() }
        content.addView(notice, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
    }
    connectionChain(page) {
        if (configAvailable()) editDeviceSettings() else { selectTab(TAB_SETTINGS); render() }
    }
    if (snapshot?.wifiReady != false) {
        val volume = DeviceControlPresentation.volume(state)
        page.volumeCard(snapshot?.volume, volume.enabled, volume.reason) { value ->
            // Recheck current ownership and freshness after the user's gesture.
            if (DeviceControlPresentation.volume(directSession.current()).enabled)
                directRequest(DeviceControlProtocol.Command.VOLUME, value)
            render()
        }
    }
    page.sectionTitle("她的设定")
    page.settingsRow("她的模样", "眼睛 · 唤醒 · 应答", iconName = "eye") { selectTab(TAB_PERSONALITY); render() }
    page.settingsRow("聊天风格", snapshot?.persona?.let { directPersonas.getOrNull(it) } ?: "连接后读取设备当前风格",
        iconName = "spark") { selectTab(TAB_PERSONA); render() }
    page.settingsRow("云服务与模型", "听懂你 · 对话 · 说给你听", iconName = "cloud") { selectTab(TAB_SERVICES); render() }
    val firmware = directFirmwareInfo
    val updateSubtitle = when {
        preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false) -> "正在确认更新结果"
        otaUpload != null || otaStatus?.state in 1L..2L -> "正在更新，请保持在前台"
        firmware != null && state.authenticated -> "当前 ${firmware.major}.${firmware.minor}.${firmware.revision} · ${firmware.build}"
        else -> "连接后读取设备版本"
    }
    page.settingsRow("固件更新", updateSubtitle, iconName = "download") {
        updateResources = false; selectTab(TAB_UPDATE); render()
    }
}

internal fun MainActivity.renderPersona(bound: Boolean) {
    val page = CompanionPage(this, content)
    page.pageTitle("聊天风格", "选择陪伴的语气。", ::navigateBack)
    if (directSession.current().authenticated && directSnapshot != null) {
        val ready = !directPending && directSession.current().snapshotFresh && directSnapshot?.busy == false
        directPersonas.forEachIndexed { index, name ->
            page.settingsRow(name, directPersonaDescriptions[index], enabled = ready,
                selected = directSnapshot?.persona == index) {
                directRequest(DeviceControlProtocol.Command.PERSONA, index)
            }
        }
        if (!ready) addMuted(if (directSnapshot?.busy == true) "对话进行中，结束后可切换风格。" else "正在等待设备确认当前状态。")
        addMuted("傻妞是 AI 伴侣，声音由模型合成。切换以设备回读为准。")
    } else {
        page.featureCard("spark", "连接后，选择她说话的方式。", "连接设备后查看和设置人物风格。")
    }
    if (!directSession.current().authenticated || directSnapshot == null) {
        if (bound) primaryButton("连接傻妞", !directConnecting) { scanDirect() }
        else primaryButton("先添加我的傻妞", !busy) { startProvisioning() }
    }
}

internal fun MainActivity.renderDirectDiscoveryCard() {
    val appearing = directDiscoveryVisible && discoveryHost.visibility != View.VISIBLE
    discoveryHost.removeAllViews()
    discoveryHost.visibility = if (directDiscoveryVisible) View.VISIBLE else View.GONE
    pageRoot.importantForAccessibility = if (directDiscoveryVisible)
        View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS else View.IMPORTANT_FOR_ACCESSIBILITY_AUTO
    if (!directDiscoveryVisible) return
    discoveryHost.setBackgroundColor(design.scrim)
    discoveryHost.isClickable = true
    discoveryHost.setOnClickListener { dismissDirectDiscovery() }
    val sheet = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; isClickable = true
    }
    val scroll = object : ScrollView(this) {
        override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
            val maximum = (discoveryHost.height.takeIf { it > 0 } ?: dp(resources.configuration.screenHeightDp)) - dp(60)
            super.onMeasure(widthMeasureSpec, View.MeasureSpec.makeMeasureSpec(maximum.coerceAtLeast(dp(160)), View.MeasureSpec.AT_MOST))
        }
    }.apply {
        addView(sheet); isFillViewport = false
        val radius = dp(CompanionDesign.RADIUS_SHEET.toInt()).toFloat()
        background = android.graphics.drawable.GradientDrawable().apply {
            setColor(design.surface); cornerRadii = floatArrayOf(radius, radius, radius, radius, 0f, 0f, 0f, 0f)
        }
        clipToOutline = true
    }
    discoveryHost.addView(scroll, android.widget.FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM))
    val state = directSession.current()
    val title = when {
        state.connection == DeviceControlSession.Connection.CONNECTING -> "正在连接并验证设备"
        state.connection == DeviceControlSession.Connection.RECONNECT_WAIT -> "正在重新连接设备"
        state.connection == DeviceControlSession.Connection.CONNECTED && state.authenticated -> "已验证连接傻妞"
        directScanner != null -> "正在查找附近设备"
        directScanFinished -> "附近设备查找结束"
        else -> "设备连接状态"
    }
    val summary = when {
        state.connection != DeviceControlSession.Connection.DISCONNECTED -> directStatus()
        directScanner != null && directCandidates.isEmpty() -> "仅显示同一服务的蓝牙广播；设备身份将在选择后验证。"
        directScanner != null -> "发现 ${directCandidates.size} 台候选设备；请选择一台进行身份验证。"
        directCandidates.isEmpty() -> directMessage
        else -> "请选择一台候选设备进行身份验证。"
    }
    CompanionPage(this, sheet).addDiscoveryCard(
        title = title,
        summary = summary,
        candidates = directCandidates.map { candidate ->
            val suffix = candidate.device.address.takeLast(5)
            CompanionPage.DiscoveryCandidate(
                title = "${candidate.name} · $suffix",
                detail = if (provisionedDeviceId.isBlank()) "未验证设备 · 选择后扫描设备屏幕认领码" else "未验证设备 · 选择后核对认领身份",
                contentDescription = "${candidate.name}，未验证设备，身份以安全认领验证为准",
                enabled = !directDiscoveryDismissed,
                select = { connectDirect(candidate.device, candidate.epoch) },
            )
        },
        dismissLabel = if (directScanner != null) "停止查找" else "收起",
        dismissDescription = if (directScanner != null)
            "停止查找附近设备，不会自动重新扫描" else "收起设备发现状态卡",
        dismiss = ::dismissDirectDiscovery,
        mood = when {
            state.authenticated -> CompanionPresenceView.Mood.AWAKE
            directScanner != null || state.connection == DeviceControlSession.Connection.CONNECTING ||
                state.connection == DeviceControlSession.Connection.RECONNECT_WAIT -> CompanionPresenceView.Mood.SEARCHING
            else -> CompanionPresenceView.Mood.OFFLINE
        },
    )
    if (appearing) {
        // Slide in once when the overlay opens; refreshes re-render in place.
        discoveryHost.alpha = 0f
        discoveryHost.animate().alpha(1f).setDuration(180).start()
        scroll.translationY = dp(360).toFloat()
        scroll.animate().translationY(0f).setDuration(260).setInterpolator(CompanionDesign.emphasized()).start()
    }
}

internal fun MainActivity.showConnectionSheet() {
    showCompanionSheet("我的傻妞", "手机与设备，设备与网络。") { body, _ ->
        val page = CompanionPage(this, body)
        page.presence(presenceMood(), 120)
        connectionChain(page, null)
        page.informationRow("手机 ↔ 设备", if (directSession.current().authenticated) "蓝牙已连接 · 身份已验证" else "未建立已验证的蓝牙连接", "bluetooth")
        page.informationRow("设备 ↔ 网络", when (directSnapshot?.wifiReady) {
            true -> "设备已联网"; false -> "设备未联网"; else -> "等待设备回读"
        }, "wifi")
        page.sectionTitle("连接管理")
        page.settingsRow("设置网络", "蓝牙负责管理，Wi-Fi 负责云端对话", iconName = "wifi") { openNetworkSettings() }
        page.settingsRow("核对认领结果", "恢复中断或不确定的认领", !busy && settingsEditor == null, iconName = "qr") {
            dismissCompanionSheet(); startProvisioning()
        }
        if (directConnection != null) page.settingsRow("断开手机连接", "设备仍可独立对话", iconName = "bluetooth") {
            dismissCompanionSheet(); closeDirect(); render()
        }
        if (provisionedDeviceId.isNotBlank()) page.settingsRow("移除本机连接资料", "保留设备网络和服务配置", !busy && !directPending,
            danger = true, iconName = "close") {
            dismissCompanionSheet(); confirmClearProvisioning()
        }
    }
}
