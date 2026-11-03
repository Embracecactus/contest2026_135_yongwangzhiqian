// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import com.shaniu.companion.MainActivity.Companion.KEY_OTA_EXPECTED_PENDING
import com.shaniu.companion.MainActivity.Companion.FIRMWARE_PACKAGE_REQUEST
import com.shaniu.companion.MainActivity.Companion.TAB_RESOURCES
import android.content.Intent
import com.shaniu.companion.provision.DeviceControlProtocol
import com.shaniu.companion.ota.OtaControlUpload
import com.shaniu.companion.ota.OtaUpdatePolicy

// Update tab: firmware update flow and resource overview.
// Moved verbatim from MainActivity; state and protocol ownership stay there.

internal fun MainActivity.renderFirmwareUpdate() {
    CompanionPage(this, content).pageTitle("更新", "每次进步，都清楚可见。")
    CompanionPage(this, content).segments(listOf("固件更新", "资源更新"), if (updateResources) 1 else 0) {
        updateResources = it == 1; render()
    }
    if (updateResources) { renderResourceOverview(); return }
    val sessionState = directSession.current()
    val localState = sessionState.snapshot
    val ota = otaStatus
    if (ota != null && ota.state != 0L) {
        val otaCurrent = sessionState.authenticated &&
            otaStatusGeneration == sessionState.generation && otaStatusReadError == null
        val otaStaleReason = when {
            otaCurrent -> ""
            !sessionState.authenticated -> "\n当前未连接；连接后读取设备实际升级状态。"
            otaStatusReadError != null -> "\n当前连接读取升级状态失败；正在重新读取。"
            else -> "\n当前连接正在重新读取升级状态。"
        }
        val percent = if (ota.progress != null && ota.total != null && ota.total > 0)
            ((ota.progress * 100L) / ota.total).coerceIn(0L, 100L) else null
        val label = when (ota.state) {
            0L -> "空闲"
            1L -> "已排队"
            2L -> "升级中"
            3L -> when {
                ota.phase == 7L -> "已回滚，请核对当前版本"
                ota.phase == 8L || ota.result != 0 -> "升级失败，设备未确认安装成功"
                expectedOtaConfirmed() -> "已确认完成升级"
                else -> "已结束，等待版本核对"
            }
            else -> "状态未知"
        }
        val phase = when (ota.phase) {
            1L -> "正在下载"
            2L -> "正在校验"
            3L -> "已写入，等待重启"
            4L -> "正在重启"
            5L -> "试运行中"
            6L -> if (expectedOtaConfirmed()) "已核对新版本" else "设备已确认，等待版本核对"
            7L -> "已回滚"
            8L -> "升级失败"
            null -> "未知"
            else -> "未知阶段（${ota.phase}）"
        }
        val result = when {
            ota.result == 0 -> "无错误"
            ota.state in 1L..2L && ota.result == -115 -> "进行中"
            else -> "设备错误 ${ota.result}"
        }
        val confirmed = otaCurrent && expectedOtaConfirmed()
        val stage = if (!otaCurrent) null else when (ota.phase) {
            1L -> 2
            2L, 3L -> 3
            4L, 5L -> 4
            6L -> 5
            else -> null
        }
        CompanionPage(this, content).updateProgress(label, "$phase · $result$otaStaleReason",
            percent?.toInt().takeIf { otaCurrent && ota.phase == 1L }, stage, confirmed,
            failed = ota.state == 3L && (ota.phase == 7L || ota.phase == 8L || ota.result != 0))
    }
    val info = directFirmwareInfo
    if (ota == null || ota.state == 0L) CompanionPage(this, content).updateIntroduction(
        info?.let { "${it.major}.${it.minor}.${it.revision} · ${it.build}" } ?: "待真实设备回读",
        inspectedFirmware?.let { "本地包 · ${it.version}" } ?: "尚未选择本地包")
    val canStart = inspectedFirmware != null && selectedFirmwareFile != null &&
        sessionState.snapshotFresh && localState?.otaSupported == true && directConnection != null &&
        otaUpload == null && !directPending && !preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)
    val choosePackage = {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
            putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                android.provider.DocumentsContract.buildDocumentUri(
                    "com.android.externalstorage.documents", "primary:Download"))
        }, FIRMWARE_PACKAGE_REQUEST)
    }
    val chooseLabel = if (firmwareInspectionPending) "正在检查固件包…" else "选择本地更新包"
    // One filled action per view, and it is always the next step: choose a
    // package until one has been inspected, then start. The start control
    // stays visible (disabled) so the gate is never hidden from the user.
    val page = CompanionPage(this, content)
    if (inspectedFirmware == null) {
        page.primaryButton(chooseLabel, !firmwareInspectionPending) { choosePackage() }
        page.secondaryButton("开始固件更新", canStart) { startLocalOta() }
    } else {
        page.primaryButton("开始固件更新", canStart) { startLocalOta() }
        page.secondaryButton(chooseLabel, !firmwareInspectionPending) { choosePackage() }
    }
    inspectedFirmware?.let { pack ->
        addCard("所选固件包", "版本 ${pack.version}\n设备 ${pack.board}\n镜像共 ${pack.ap.size + pack.cp.size} 字节")
        addCard("升级说明", "此固件包未提供升级说明。")
        addMuted("文件完整性检查通过。设备兼容性与签名仍需由连接的设备确认。")
    }
    firmwareInspectionMessage?.let { addMuted(it) }
    if (otaMessage.isNotBlank()) addMuted(otaMessage)
    otaStatusReadError?.let(::addMuted)
    if (preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)) {
        addCard("正在确认更新结果", "传输完成后，还需要设备安装、重连并核对新版本。连接同一设备即可继续查询，无需重复安装。")
        actionButton("查询设备更新结果", directSession.current().authenticated && !directPending) {
            directOtaRequest(DeviceControlProtocol.Command.OTA_STATUS)
        }
    }
    if (otaUpload?.state == OtaControlUpload.State.WAITING ||
        otaUpload?.state == OtaControlUpload.State.ACCEPTED || ota?.state in 1L..2L) {
        val uploading = otaUpload?.state == OtaControlUpload.State.WAITING
        val cancellable = OtaUpdatePolicy.mayRequestCancellation(
            uploading, directPending, ota?.state, ota?.phase)
        actionButton("取消升级", cancellable) { cancelLocalOta() }
        if (!uploading && !cancellable && !directPending)
            addMuted("设备已进入安装提交阶段，请保持供电并等待重连；此时不能立即取消。")
    }
    addMuted("更新期间请保持 App 在前台。蓝牙发送更新指令，设备通过局域网 HTTPS 拉取镜像；重连并核对版本后才确认完成。")
    if (!canStart) addMuted(when {
        inspectedFirmware == null -> "先选择普通 OTA 包；工厂全量包不能用于此入口。"
        directConnection == null -> "需要连接已认领的设备，才能核对更新能力。"
        !sessionState.snapshotFresh -> "设备状态已过期，正在重新读取；暂不能开始升级。"
        localState?.otaSupported != true -> "当前固件未提供此更新能力。"
        else -> "当前设备事务尚未结束，请等待后重试。"
    })
    if (localState?.otaSupported != true && directConnection != null)
        addMuted("设备固件未声明本地升级能力，不能开始升级。")
    if (directConnection == null && provisionedDeviceId.isNotBlank())
        primaryButton(if (directConnecting) "正在连接…" else "连接设备读取版本", !directConnecting) { scanDirect() }
}

internal fun MainActivity.renderResourceOverview() {
    val page = CompanionPage(this, content)
    page.settingsRow("眼睛资源", "查看、导入与确认当前外观", iconName = "eye") { selectTab(TAB_RESOURCES); render() }
    val current = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
    page.settingsRow("唤醒模型", current?.active?.let(::wakeModelSummary) ?: "设备当前模型待回读", iconName = "mic") { selectTab(TAB_RESOURCES); render() }
    page.settingsRow("应答音", "当前固件未提供独立资源安装", enabled = false, iconName = "speaker") { }
    actionButton("导入资源包", true) { selectTab(TAB_RESOURCES); render() }
    page.notice("先校验兼容性，再安装和确认生效。")
}
