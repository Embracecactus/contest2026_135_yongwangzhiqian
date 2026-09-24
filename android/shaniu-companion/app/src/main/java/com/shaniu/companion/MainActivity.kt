// SPDX-License-Identifier: Apache-2.0

package com.shaniu.companion

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import com.shaniu.companion.provision.AndroidDeviceControlFactory
import com.shaniu.companion.provision.DeviceControlSession
import com.shaniu.companion.provision.DeviceControlPresentation
import com.shaniu.companion.provision.DeviceControlProtocol
import com.shaniu.companion.provision.DeviceControlScanner
import com.shaniu.companion.gateway.AndroidKeystoreTokenStore
import com.shaniu.companion.gateway.ConsoleGatewayClient
import com.shaniu.companion.gateway.ConsoleEnrollment
import com.shaniu.companion.gateway.GatewayCallResult
import com.shaniu.companion.gateway.GatewayEventConnection
import com.shaniu.companion.gateway.GatewayEventObserver
import com.shaniu.companion.gateway.GatewayFailureReason
import com.shaniu.companion.gateway.GatewayTokenPolicy
import com.shaniu.companion.gateway.GatewayTransportException
import com.shaniu.companion.gateway.GatewayTransportConfiguration
import com.shaniu.companion.gateway.OkHttpConsoleGatewaySession
import com.shaniu.companion.protocol.CompanionState
import com.shaniu.companion.protocol.CompanionStore
import com.shaniu.companion.protocol.ConsoleEventEnvelope
import com.shaniu.companion.protocol.ConsoleMutation
import com.shaniu.companion.protocol.ConsoleMutationArguments
import com.shaniu.companion.protocol.ConsoleOperation
import com.shaniu.companion.protocol.ConsoleErrorCode
import com.shaniu.companion.protocol.ConsolePolicy
import com.shaniu.companion.protocol.EventDisposition
import com.shaniu.companion.protocol.Emotion
import com.shaniu.companion.protocol.TurnPhase
import com.shaniu.companion.protocol.FirmwareRelease
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.GatewayConnection
import com.shaniu.companion.protocol.MutationContext
import com.shaniu.companion.protocol.MutationDecision
import com.shaniu.companion.protocol.MutationReceiptStatus
import com.shaniu.companion.protocol.MemoryDeleteScope
import com.shaniu.companion.protocol.PermissionLevel
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PersonaMode
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.protocol.UpdatePhase
import com.shaniu.companion.provision.ProvisionActivity
import com.shaniu.companion.provision.ProvisionBindingStore
import com.shaniu.companion.provision.ProvisionBootstrap
import com.shaniu.companion.ota.BkpackInspector
import com.shaniu.companion.ota.OtaControlUpload
import com.shaniu.companion.ota.OtaPackageServer
import com.shaniu.companion.ota.OtaPackageServerException
import com.shaniu.companion.ota.OtaPackageServerStage
import com.shaniu.companion.ota.OtaStartGate
import com.shaniu.companion.ota.OtaUpdatePolicy
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.atomic.AtomicLong

/**
 * Production console-v1 entry point.
 *
 * Endpoint metadata is stored in ordinary preferences. The bearer credential is
 * handled only by [AndroidKeystoreTokenStore] and is never rendered or logged.
 * Device state is reported-state only: accepted mutations are not applied
 * optimistically and must be confirmed by a later snapshot/event.
 */
class MainActivity : Activity() {
    private data class GatewayRuntime(
        val epoch: Long,
        val deviceId: String,
        val session: OkHttpConsoleGatewaySession,
        val client: ConsoleGatewayClient,
    )

    private lateinit var preferences: SharedPreferences
    private lateinit var tokenStore: AndroidKeystoreTokenStore
    private lateinit var provisionBindingStore: ProvisionBindingStore
    private lateinit var content: LinearLayout
    private lateinit var contentScroll: ScrollView
    private lateinit var discoveryHost: android.widget.FrameLayout
    private lateinit var cloudEditorHost: LinearLayout
    private var cloudEditorOpening = false
    private var cloudEditorEmbedded = false
    private lateinit var pageRoot: LinearLayout
    private var renderedTab: Int? = null
    private var renderTicket = 0L
    private var touchActive = false
    private var renderDeferred = false

    private val ioExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val requestCounter = AtomicLong()
    private var inspectedFirmware: BkpackInspector.Metadata? = null
    private var selectedFirmwareFile: java.io.File? = null
    private var firmwareInspectionPending = false
    private var firmwareInspectionMessage: String? = null
    private var firmwareInspectionEpoch = 0L
    private var otaServer: OtaPackageServer? = null
    private var otaUpload: OtaControlUpload? = null
    private var otaStatus: DeviceControlProtocol.OtaStatus? = null
    private var otaStatusGeneration: Long? = null
    private var otaMessage = ""
    private var otaStatusReadError: String? = null
    private var otaVerificationPending = false
    private val otaStartGate = OtaStartGate()

    private var runtime: GatewayRuntime? = null
    private var eventConnection: GatewayEventConnection? = null
    private var store: CompanionStore? = null
    private var releaseCatalog: FirmwareReleaseCatalog? = null
    private var connectionEpoch = 0L
    private var eventStreamEpoch = 0L
    private var currentTab = TAB_OVERVIEW
    private var expressionPreview = 0
    private var updateResources = false
    private var resourcesBackTab = TAB_PERSONALITY
    private var companionSheet: android.app.Dialog? = null
    private var refreshCompanionSheet: (() -> Unit)? = null
    private var busy = false
    private var eventConnected = false
    private var foreground = false
    private var autoReconnectAllowed = true
    private var reconnectAttempt = 0
    private var reconnectTicket = 0L
    private var destroyed = false
    private var developerPanel = false
    private var legacyConsoleMode = false
    private val directSession by lazy {
        DeviceControlSession(
            nowMs = { android.os.SystemClock.elapsedRealtime() },
            post = { action -> mainHandler.post { action() }; Unit },
            schedule = { delay, action ->
                val task = Runnable { action() }
                mainHandler.postDelayed(task, delay)
                object : DeviceControlSession.Cancel {
                    override fun cancel() { mainHandler.removeCallbacks(task) }
                }
            },
            pollCommand = {
                if (currentTab == TAB_UPDATE || otaUpload != null ||
                    otaVerificationPending || otaStatus?.state in 1L..2L)
                    DeviceControlProtocol.Command.OTA_STATUS else DeviceControlProtocol.Command.STATUS
            },
        )
    }
    private val directConnection: DeviceControlSession?
        get() = directSession.takeIf { it.current().authenticated }
    private val directSubscriptions = mutableListOf<DeviceControlSession.Cancel>()
    private var directObservedGeneration = 0L
    private var directServiceWasReady = false
    private var directScanner: DeviceControlScanner? = null
    private var directDialog: AlertDialog? = null
    private data class DirectCandidate(
        val device: android.bluetooth.BluetoothDevice,
        val name: String,
        val epoch: Long,
    )
    // Scan records are selection hints only. A candidate is never a claimed or
    // authenticated device until the existing control session verifies it.
    private val directCandidates = mutableListOf<DirectCandidate>()
    private var directScanFinished = false
    private var directDiscoveryDismissed = false
    private var directDiscoveryVisible = false
    private val directSnapshot get() = directSession.current().snapshot
    private val directFirmwareInfo get() = directSession.current().firmwareInfo
    private var directEpoch = 0L
    private val directPending get() = directSession.current().writePending
    private var memoryResultMessage: String? = null
    private var memoryDesiredEnabled: Boolean? = null
    private var memoryRequestAccepted = false
    private var directConnecting = false
    private var directMessage = "尚未连接设备"
    private data class CloudModels(val asr: String, val chat: String, val tts: String)
    private var cloudModels: CloudModels? = null
    private var cloudModelsGeneration: Long? = null
    private var cloudModelsFailedGeneration: Long? = null
    private var cloudModelsReadError: String? = null
    private var cloudModelsWire: ByteArray? = null
    private var cloudModelsOffset = 0
    private var cloudModelsTotal = -1
    private var cloudModelsExpected: CloudModels? = null
    private var cloudModelsReadDeadline = 0L
    private var cloudModelsReadTicket = 0L
    private var cloudModelsCanceling = false
    private var responseMode: Int? = null
    private var responseModeGeneration: Long? = null
    private var responseModeFailedGeneration: Long? = null
    private var responseModeExpected: Int? = null
    private var responseModeError: String? = null
    private var responseModeCanceling = false
    private var wakeSensitivityPercent: Int? = null
    private var wakeSensitivityGeneration: Long? = null
    private var wakeSensitivityFailedGeneration: Long? = null
    private var wakeSensitivityExpected: Int? = null
    private var wakeSensitivityError: String? = null
    private var wakeSensitivityCanceling = false
    private enum class ConfigFlow { SETTINGS, NONE, CAPABILITIES, CLOUD, WAKE, RESPONSE, SENSITIVITY, EYES }
    private var settingsEditor: com.shaniu.companion.provision.DeviceSettingsEditor? = null
    private var factoryReset: com.shaniu.companion.provision.FactoryResetController? = null
    private var configFlow = ConfigFlow.NONE
    private var configAppendMax = 32
    private var configCapabilitiesGeneration: Long? = null
    private var wakePackage: WakeModelPackage? = null
    private var wakePayload: ByteArray? = null
    private var wakeExpectedSha: ByteArray? = null
    private var wakeStatus: WakeModelPackage.Companion.Status? = null
    private var wakeStatusGeneration: Long? = null
    private var wakeRestoreRequested = false
    private var wakeApplied = false
    private var wakeCanceling = false
    private var wakeReadTicket = 0L
    private var wakeOffset = 0
    private var wakeRead = ByteArray(0)
    private var wakeReadTotal = -1
    private var wakeDeadline = 0L
    private var wakeMessage: String? = null
    private var pendingWakeImport: WakeModelPackage? = null
    private var wakeImportDeviceId = ""
    private var wakeImportDeadline = 0L
    private var wakeImportTicket = 0L
    private var selectedEyePack: EyePack? = null
    private var selectedEyeFile: java.io.File? = null
    private var selectedEyeAssetSha256: ByteArray? = null
    private var eyeMessage: String? = null
    private var eyeImportPending = false
    private var eyeServer: OtaPackageServer? = null
    private var eyeRecord: ByteArray? = null
    private var eyeOffset = 0
    private var eyeRead = ByteArray(0)
    private var eyeReadingOnly = false
    private val navigation = mutableMapOf<Int, TextView>()
    private var lastAction = "请配置 HTTPS Gateway、设备 ID 和访问令牌"

    private var draftOrigin = ""
    private var draftDeviceId = ""
    private var draftPins = ""
    private var provisionedDeviceId = ""
    private var controlCredentialRequired = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = BACKGROUND
        window.navigationBarColor = design.surface
        window.isStatusBarContrastEnforced = false
        window.decorView.systemUiVisibility = if (design.dark) 0 else
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        currentTab = savedInstanceState?.getInt("navigation", TAB_OVERVIEW) ?: TAB_OVERVIEW
        expressionPreview = savedInstanceState?.getInt("expression_preview", 0) ?: 0
        updateResources = savedInstanceState?.getBoolean("update_resources", false) ?: false
        resourcesBackTab = savedInstanceState?.getInt("resources_back_tab", TAB_PERSONALITY) ?: TAB_PERSONALITY
        preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        tokenStore = AndroidKeystoreTokenStore(applicationContext)
        provisionBindingStore = ProvisionBindingStore(applicationContext)
        legacyConsoleMode = BuildConfig.LEGACY_SERVICE_DEMO &&
            (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0 &&
            intent.getBooleanExtra("legacy_console", false)
        reloadLocalConfiguration()
        setContentView(buildRoot().apply { applySystemInsets() })
        installSystemBack { navigateBack() }
        directSubscriptions += directSession.observe { state ->
            if (state.generation != directObservedGeneration) {
                directObservedGeneration = state.generation
                directServiceWasReady = false
                directEpoch++
                // A transport generation invalidates discovery hints before a
                // delayed callback can select a candidate from the old scan.
                clearDirectDiscovery(keepStatusCard = directDiscoveryVisible)
                cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsExpected = null
                cloudModelsOffset = 0; cloudModelsTotal = -1
                cloudModelsReadDeadline = 0; cloudModelsReadTicket++
                cloudModelsCanceling = false
                cloudModelsGeneration = null
                cloudModelsFailedGeneration = null
                responseModeGeneration = null; responseModeFailedGeneration = null
                responseModeExpected = null; responseModeCanceling = false
                responseModeError = "连接恢复后读取设备实际回答模式；未完成的设置不会重发"
                wakeSensitivityGeneration = null; wakeSensitivityFailedGeneration = null
                wakeSensitivityExpected = null
                wakeSensitivityCanceling = false
                wakeSensitivityError = "连接恢复后读取实际唤醒门限；未完成的设置不会重发"
                configFlow = ConfigFlow.NONE
                configAppendMax = 32; configCapabilitiesGeneration = null
                wakePackage = null; wakePayload = null; wakeExpectedSha = null
                wakeStatusGeneration = null; wakeRestoreRequested = false
                wakeApplied = false; wakeCanceling = false; wakeReadTicket++
                wakeRead = ByteArray(0); wakeReadTotal = -1; wakeOffset = 0
                wakeMessage = "连接恢复后读取实际唤醒词模型；未完成的操作不会重发"
                finishEyeInstall("连接已变化；未完成的眼睛安装不会重发")
                cloudModelsReadError = "连接恢复后读取设备实际模型配置"
                otaStatusReadError = null
                // A new transport cannot resume a partially sent OTA record.
                // An accepted HTTP source may finish during the foreground or
                // the bounded Activity grace; the next connection reads status.
                val accepted = otaVerificationPending &&
                    (otaUpload?.state == OtaControlUpload.State.ACCEPTED || otaStatus?.state in 1L..2L)
                otaUpload?.close(); otaUpload = null
                if (otaServer != null && (!accepted || !foreground || destroyed)) {
                    otaStatus = null; otaStatusGeneration = null; otaVerificationPending = false
                    otaStartGate.release()
                    setOtaKeepAwake(false)
                    closeOtaServer("控制会话已结束；返回后读取设备实际升级状态，未完成的请求不会重发。")
                }
            }
            if (state.authenticated) directConnecting = false
            if (foreground && !destroyed) render()
        }
        directSubscriptions += directSession.observeResults(::onDirectResult)
        render()
    }

    private fun navigateBack() {
        when {
            directDiscoveryVisible -> dismissDirectDiscovery()
            developerPanel -> developerPanel = false
            currentTab == TAB_SERVICES -> selectTab(TAB_SETTINGS)
            currentTab == TAB_ADVANCED -> selectTab(TAB_SERVICES)
            currentTab == TAB_RESOURCES -> selectTab(resourcesBackTab)
            currentTab == TAB_PERSONA -> selectTab(TAB_PERSONALITY)
            currentTab == TAB_PRIVACY || currentTab == TAB_UPDATE -> selectTab(TAB_SETTINGS)
            currentTab != TAB_OVERVIEW -> selectTab(TAB_OVERVIEW)
            else -> { finish(); return }
        }
        render()
    }

    @Deprecated("Platform back callback")
    override fun onBackPressed() = navigateBack()

    override fun onStart() {
        super.onStart()
        foreground = true
        directSession.setForeground(true)
        reloadLocalConfiguration()
        if (legacyConsoleMode && autoReconnectAllowed && !busy && runtime == null && !controlCredentialRequired &&
            draftOrigin.isNotBlank() && draftDeviceId.isNotBlank()) {
            connect(draftOrigin, draftDeviceId, draftPins, "", automatic = true)
        } else render()
    }

    override fun onStop() {
        // The shared session keeps a bounded grace period for file pickers and
        // other Activities. Foreground return resumes the same authenticated link.
        foreground = false
        cancelPendingWakeImport("已取消等待导入，尚未向设备发送模型")
        touchActive = false
        renderDeferred = false
        firmwareInspectionEpoch++
        firmwareInspectionPending = false
        closeEyeServer()
        if (configFlow == ConfigFlow.EYES) finishEyeInstall("已离开前台；未完成的眼睛安装不会重发")
        directSession.setForeground(false)
        if (otaUpload?.state == OtaControlUpload.State.WAITING) {
            // Do not leave a half-sent source record waiting on an invisible UI.
            directSession.disconnect(user = false)
        }
        clearDirectDiscovery()
        directDialog?.dismiss(); directDialog = null
        directConnecting = false
        closeRuntime(clearReportedState = true)
        super.onStop()
    }

    @Deprecated("Platform activity result callback")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK) return
        when (requestCode) {
            FIRMWARE_PACKAGE_REQUEST -> inspectFirmwarePackage(data?.data)
            WAKE_MODEL_REQUEST -> data?.data?.let(::selectWakeModel)
            EYE_PACK_REQUEST -> data?.data?.let(::selectEyePack)
            PROVISION_REQUEST -> {
                directSession.releaseIdentity()
                val deviceId = ProvisionBootstrap.validDeviceId(
                    data?.getStringExtra(ProvisionActivity.EXTRA_PROVISIONED_DEVICE_ID),
                ) ?: return
                val durableDeviceId = try {
                    provisionBindingStore.boundDeviceId()
                } catch (_: Exception) {
                    null
                }
                if (durableDeviceId != deviceId) {
                    provisionedDeviceId = ""
                    controlCredentialRequired = true
                    lastAction = "设备已返回认领结果，但本机没有对应的持久化回执；请重新打开认领核对结果"
                    selectTab(TAB_OVERVIEW)
                    render()
                    return
                }
                provisionedDeviceId = durableDeviceId
                if (!legacyConsoleMode) {
                    lastAction = "设备已确认保存设置，可在首页查看连接和语音服务状态。"
                    selectTab(TAB_OVERVIEW)
                    render()
                    return
                }
                controlCredentialRequired = true
                if (preferences.edit()
                        .putString(KEY_PROVISIONED_DEVICE_ID, deviceId)
                        .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, true)
                        .commit()) {
                    lastAction = "傻妞已保存网络设置；请绑定 App 控制凭据"
                } else {
                    // The canonical binding is already durable. Reopening the App
                    // restores it even if this compatibility mirror could not be saved.
                    lastAction = "傻妞已保存网络设置；本机状态将在重新打开后恢复，请再绑定 App 控制凭据"
                }
                selectTab(TAB_OVERVIEW)
                render()
            }
            CONSOLE_ENROLLMENT_REQUEST -> importConsoleEnrollment(data)
        }
    }

    override fun onDestroy() {
        dismissCompanionSheet()
        factoryReset?.close(); factoryReset = null
        settingsEditor?.close(); settingsEditor = null
        destroyed = true
        firmwareInspectionEpoch++
        selectedFirmwareFile?.delete()
        selectedFirmwareFile = null
        inspectedFirmware = null
        selectedEyeFile?.delete()
        selectedEyeFile = null
        closeEyeServer()
        directSubscriptions.forEach { it.cancel() }
        directSubscriptions.clear()
        closeDirect()
        closeRuntime(clearReportedState = true)
        // Let the queued TLS/session close finish off the main thread.
        ioExecutor.shutdown()
        mainHandler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    /**
     * Restores the canonical provisioning outcome before deciding whether an
     * existing console credential may reconnect. The old App preference is a
     * compatibility mirror only; new commits come from ProvisionBindingStore.
     */
    private fun reloadLocalConfiguration() {
        if (!legacyConsoleMode) {
            try {
                provisionedDeviceId = provisionBindingStore.boundDeviceId().orEmpty()
                lastAction = ""
            } catch (_: Exception) {
                provisionedDeviceId = ""
                lastAction = "读取设备资料失败，请重新核对认领结果。"
            }
            return
        }
        try {
            draftOrigin = preferences.getString(KEY_ORIGIN, "").orEmpty()
            draftDeviceId = preferences.getString(KEY_DEVICE_ID, "").orEmpty()
            draftPins = preferences.getString(KEY_PINS, "").orEmpty()
            val legacyDeviceId = ProvisionBootstrap.validDeviceId(
                preferences.getString(KEY_PROVISIONED_DEVICE_ID, ""),
            )
            val durableDeviceId = provisionBindingStore.boundDeviceId()
            // The old preference was written after the receipt had already
            // been removed, so it cannot prove the atomic durable outcome.
            // Keep it only as a migration warning, never as a bound locator.
            provisionedDeviceId = durableDeviceId.orEmpty()
            controlCredentialRequired = preferences.getBoolean(
                KEY_CONTROL_CREDENTIAL_REQUIRED,
                false,
            )
            if (durableDeviceId != null) {
                val needsFreshCredential = draftDeviceId != durableDeviceId
                controlCredentialRequired = controlCredentialRequired || needsFreshCredential
                if (legacyDeviceId != durableDeviceId || needsFreshCredential) {
                    val mirrored = preferences.edit()
                        .putString(KEY_PROVISIONED_DEVICE_ID, durableDeviceId)
                        .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, controlCredentialRequired)
                        .commit()
                    if (!mirrored) {
                        lastAction = "已恢复设备认领结果，但本机控制状态未能持久化"
                    }
                }
            } else if (legacyDeviceId != null) {
                controlCredentialRequired = true
                val blocked = preferences.edit()
                    .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, true)
                    .commit()
                lastAction = if (blocked) {
                    "检测到旧版认领记录；请重新核对设备，或导入 App 控制凭据"
                } else {
                    "旧版认领记录无法核对；已停止自动连接"
                }
            }
        } catch (_: Exception) {
            draftOrigin = ""
            draftDeviceId = ""
            draftPins = ""
            provisionedDeviceId = ""
            controlCredentialRequired = true
            lastAction = "本机认领状态无法读取；已停止自动连接以避免绑定错误"
        }
    }

    private fun buildRoot(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BACKGROUND)
        }
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(8), dp(24), dp(24))
        }
        contentScroll = ScrollView(this).apply { addView(content) }
        root.addView(
            contentScroll,
            LinearLayout.LayoutParams(0, 0).apply {
                width = ViewGroup.LayoutParams.MATCH_PARENT
                height = 0
                weight = 1f
            },
        )
        cloudEditorHost = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; visibility = View.GONE
        }
        root.addView(cloudEditorHost, LinearLayout.LayoutParams(-1, 0, 1f))
        val tabRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(design.surface)
            setPadding(dp(8), dp(4), dp(8), dp(4))
        }
        TABS.forEach { (tab, title) ->
            tabRow.addView(
                TextView(this).apply {
                    text = title
                    textSize = 12f
                    minimumHeight = dp(64)
                    setPadding(dp(4), dp(4), dp(4), dp(4))
                    compoundDrawablePadding = dp(3)
                    setCompoundDrawablesWithIntrinsicBounds(null, navigationIcon(tab), null, null)
                    gravity = Gravity.CENTER
                    isClickable = true
                    isFocusable = true
                    navigation[tab] = this
                    setOnClickListener {
                        selectTab(tab)
                        developerPanel = false
                        render()
                    }
                },
                LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f),
            )
        }
        root.addView(
            tabRow,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )
        pageRoot = root
        discoveryHost = android.widget.FrameLayout(this).apply { visibility = View.GONE }
        return android.widget.FrameLayout(this).apply {
            addView(root, android.widget.FrameLayout.LayoutParams(-1, -1))
            addView(discoveryHost, android.widget.FrameLayout.LayoutParams(-1, -1))
        }
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_DOWN) touchActive = true
        val handled = super.dispatchTouchEvent(event)
        if (event.actionMasked == MotionEvent.ACTION_UP ||
            event.actionMasked == MotionEvent.ACTION_CANCEL) {
            touchActive = false
            if (renderDeferred) {
                renderDeferred = false
                mainHandler.post { render() }
            }
        }
        return handled
    }

    private fun render() {
        if (destroyed) return
        refreshCompanionSheet?.invoke()
        // Polling must not remove the target between touch-down and click.
        if (touchActive) { renderDeferred = true; return }
        val scrollY = if (renderedTab == currentTab) contentScroll.scrollY else 0
        renderedTab = currentTab
        val ticket = ++renderTicket
        contentScroll.post {
            if (!destroyed && ticket == renderTicket && !touchActive)
                contentScroll.scrollTo(0, scrollY)
        }
        val selectedTab = when (currentTab) {
            TAB_INTERACTION -> TAB_OVERVIEW
            TAB_PRIVACY, TAB_SERVICES, TAB_ADVANCED -> TAB_SETTINGS
            TAB_RESOURCES -> resourcesBackTab
            TAB_PERSONA -> TAB_PERSONALITY
            else -> currentTab
        }
        navigation.forEach { (tab, label) ->
            label.isSelected = tab == selectedTab
            label.setTextColor(if (tab == selectedTab) design.accent else MUTED)
            label.setCompoundDrawablesWithIntrinsicBounds(null, navigationIcon(tab, tab == selectedTab), null, null)
            label.background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(Color.TRANSPARENT, dp(18).toFloat()),
                design.shape(Color.WHITE, dp(18).toFloat()))
        }
        if (!legacyConsoleMode && currentTab == TAB_SERVICES &&
            (cloudEditorEmbedded || cloudEditorOpening)) {
            contentScroll.visibility = View.GONE
            cloudEditorHost.visibility = View.VISIBLE
            renderDirectDiscoveryCard()
            return
        }
        contentScroll.visibility = View.VISIBLE
        cloudEditorHost.visibility = View.GONE
        content.removeAllViews()
        if (!legacyConsoleMode) {
            renderDirectCompanion()
            if (lastAction.isNotBlank()) addMuted(lastAction)
            return
        }
        if (developerPanel) statusStrip()
        when (currentTab) {
            TAB_OVERVIEW -> renderOverview()
            TAB_INTERACTION -> renderInteraction()
            TAB_PERSONALITY -> renderPersonality()
            TAB_PRIVACY -> renderPrivacy()
            TAB_UPDATE -> renderUpdate()
            TAB_SETTINGS -> renderSettings()
        }
        if (!developerPanel && lastAction.contains("失败")) addMuted(lastAction)
    }

    private val directPersonas = listOf("温柔陪伴", "活泼俏皮", "安静倾听", "认真交流", "轻轻嘴硬")
    private val directPersonaDescriptions = listOf("慢慢聊，温柔回应", "轻松一点，多一点趣味",
        "留些空间，听你说完", "一起理清思路", "带一点俏皮的小别扭")

    private fun directStatus(): String {
        val state = directSession.current()
        return when (state.connection) {
            DeviceControlSession.Connection.CONNECTING -> "正在连接并验证设备…"
            DeviceControlSession.Connection.RECONNECT_WAIT -> "连接已中断，正在重新连接…"
            DeviceControlSession.Connection.SUSPENDED -> "连接已暂停，返回后重新验证设备"
            DeviceControlSession.Connection.DISCONNECTED -> state.error?.let { "连接已断开（$it），可重新连接" } ?: directMessage
            DeviceControlSession.Connection.CONNECTED -> when {
                !state.snapshotFresh -> state.error ?: "已验证设备，正在读取状态…"
                else -> listOfNotNull(state.snapshot?.statusText(), state.operationMessage).joinToString("\n")
            }
        }
    }

    private fun closeDirect(preserveAcceptedOtaSource: Boolean = false): Boolean {
        cancelPendingWakeImport("连接已关闭，尚未向设备发送模型")
        closeEyeServer()
        if (configFlow == ConfigFlow.EYES) finishEyeInstall("连接已关闭；未完成的眼睛安装不会重发")
        val preserveOtaSource = preserveAcceptedOtaSource && foreground && !destroyed &&
            otaServer?.running == true && otaVerificationPending &&
            (otaUpload?.state == OtaControlUpload.State.ACCEPTED || otaStatus?.state in 1L..2L)
        directEpoch++
        clearDirectDiscovery()
        directDialog?.dismiss(); directDialog = null
        directSession.disconnect()
        directConnecting = false
        memoryResultMessage = null; memoryDesiredEnabled = null; memoryRequestAccepted = false
        cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsExpected = null
        cloudModelsOffset = 0; cloudModelsTotal = -1
        cloudModelsReadDeadline = 0; cloudModelsReadTicket++
        if (preserveOtaSource) {
            otaMessage = "蓝牙已断开，手机仍在提供固件；请保持此页并重新连接查看进度。"
            directMessage = otaMessage
        } else {
            otaUpload?.close(); otaUpload = null; otaStatus = null; otaStatusGeneration = null
            otaVerificationPending = false
            otaStartGate.release()
            setOtaKeepAwake(false)
            closeOtaServer("升级传输已中断；重新连接后会核对设备状态。")
        }
        if (!preserveOtaSource) directMessage = "手机未连接；设备可继续独立对话。"
        return preserveOtaSource
    }

    private fun clearDirectDiscovery(keepStatusCard: Boolean = false) {
        directScanner?.close(); directScanner = null
        directCandidates.clear()
        directScanFinished = false
        directDiscoveryDismissed = false
        directDiscoveryVisible = keepStatusCard
    }

    private fun selectTab(value: Int) {
        dismissCompanionSheet()
        if (value == TAB_RESOURCES && currentTab != TAB_RESOURCES)
            resourcesBackTab = if (currentTab == TAB_UPDATE) TAB_UPDATE else TAB_PERSONALITY
        currentTab = value
        if (value != TAB_SERVICES && cloudEditorEmbedded) {
            cloudEditorEmbedded = false
            settingsEditor?.close()
            settingsEditor = null
        }
    }

    private fun closeOtaServer(message: String? = null) {
        val server = otaServer ?: return
        otaServer = null
        ioExecutor.execute { server.close() }
        if (message != null) otaMessage = message
    }

    private fun closeEyeServer() {
        val server = eyeServer ?: return
        eyeServer = null
        ioExecutor.execute { server.close() }
    }

    private fun setOtaKeepAwake(keep: Boolean) {
        if (keep) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun directRequest(command: DeviceControlProtocol.Command, value: Int = 0): Boolean {
        if (!foreground) return false
        return directSession.request(command, value)
    }

    private fun directOtaRequest(command: DeviceControlProtocol.Command,
                                 payload: ByteArray = ByteArray(0)): Boolean {
        if (!foreground) return false
        return directSession.requestOta(command, payload)
    }

    private fun directConfigRequest(command: DeviceControlProtocol.Command, payload: ByteArray): Boolean =
        foreground && directSession.requestPayload(command, payload)

    private fun configAvailable(): Boolean = foreground && directSession.current().authenticated &&
        directSnapshot?.publicConfigSupported == true && configFlow == ConfigFlow.NONE && !directPending &&
        otaUpload == null && !otaVerificationPending && otaStatus?.state !in 1L..2L

    private fun requestConfigCapabilities() {
        if (!configAvailable()) return
        configFlow = ConfigFlow.CAPABILITIES
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt(0x7fff shl 16).array())) configFlow = ConfigFlow.NONE
    }

    private fun handleConfigCapabilities(snapshot: DeviceControlProtocol.Snapshot) {
        // A capability belongs to this authenticated connection, never an INFO
        // version guess. Old firmware can reject the read without disconnecting.
        configAppendMax = 32
        val chunk = snapshot.configChunk
        if (snapshot.error == 0 && chunk?.totalLength == 12 && chunk.bytes.size >= 12) {
            val record = ByteBuffer.wrap(chunk.bytes)
            if (record.int == 0x43415031 && record.int == 1) {
                val maximum = record.int
                if (maximum in 32..512) configAppendMax = maximum
            }
        }
        configCapabilitiesGeneration = directSession.current().generation
        configFlow = ConfigFlow.NONE
    }

    private fun beginWakePackage(pack: WakeModelPackage) {
        if (!configAvailable()) { wakeMessage = "设备忙或当前固件不支持模型部署"; render(); return }
        if (pack.bytes[3] == '2'.code.toByte() &&
            (wakeStatusGeneration != directSession.current().generation ||
             wakeStatus?.supportsFrontendV2 != true)) {
            wakeMessage = "此模型需要支持前端 v2 的固件；请先读取设备模型状态，旧固件不能仅替换模型。"
            render(); return
        }
        configFlow = ConfigFlow.WAKE
        wakePackage = pack; wakePayload = pack.bytes; wakeExpectedSha = pack.sha256
        wakeStatusGeneration = null
        wakeOffset = 0; wakeApplied = false; wakeCanceling = false
        wakeMessage = "正在发送 ${pack.phrase} 模型"
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(2).putInt(pack.bytes.size).array()))
            finishWake("设备忙，尚未开始模型传输")
        render()
    }

    private fun selectWakeModel(uri: android.net.Uri) {
        val pack = try { contentResolver.openInputStream(uri)?.use(WakeModelPackage::read) } catch (_: Exception) { null }
        if (pack == null) { wakeMessage = "唤醒词模型包格式、长度或校验值无效"; render(); return }
        // A long file picker expires the foreground grace. Keep only this
        // unsent user selection while the existing session authenticates and
        // reads fresh capabilities; never resume a transmitted transaction.
        pendingWakeImport = pack
        wakeImportDeviceId = provisionedDeviceId
        wakeImportDeadline = android.os.SystemClock.elapsedRealtime() + 30_000
        val ticket = ++wakeImportTicket
        wakeMessage = "等待设备验证后导入 ${pack.phrase}"
        mainHandler.post { advanceWakeImport(ticket) }
    }

    private fun cancelPendingWakeImport(message: String) {
        if (pendingWakeImport == null) return
        pendingWakeImport = null
        wakeImportDeviceId = ""
        wakeImportDeadline = 0L
        wakeImportTicket++
        wakeMessage = message
    }

    private fun advanceWakeImport(ticket: Long) {
        if (destroyed || ticket != wakeImportTicket) return
        val pack = pendingWakeImport ?: return
        val state = directSession.current()
        val failure = when {
            wakeImportDeviceId.isBlank() || wakeImportDeviceId != provisionedDeviceId -> "设备已改变，请重新选择模型"
            android.os.SystemClock.elapsedRealtime() >= wakeImportDeadline -> "设备未及时就绪，尚未发送模型；请重新选择"
            foreground && state.connection == DeviceControlSession.Connection.DISCONNECTED -> "连接已关闭，尚未向设备发送模型"
            state.authenticated && state.snapshotFresh && directSnapshot?.publicConfigSupported != true -> "当前固件不支持模型部署"
            else -> null
        }
        if (failure != null) {
            cancelPendingWakeImport(failure)
            if (foreground) render()
            return
        }
        if (state.snapshotFresh && configCapabilitiesGeneration == state.generation && configAvailable()) {
            // Consume the selection before CONFIG_BEGIN can invoke callbacks.
            // Later connection loss follows the normal no-replay cleanup.
            cancelPendingWakeImport("")
            beginWakePackage(pack)
            return
        }
        val message = when {
            !state.authenticated -> "正在验证设备，模型尚未发送"
            !state.snapshotFresh || configCapabilitiesGeneration != state.generation -> "正在读取设备能力，模型尚未发送"
            else -> "等待当前设备操作结束，模型尚未发送"
        }
        if (wakeMessage != message) {
            wakeMessage = message
            if (foreground) render()
        }
        mainHandler.postDelayed({ advanceWakeImport(ticket) }, 200)
    }

    private fun selectBundledWakeModel(label: String) {
        val pack = try { assets.open("wake-models/$label.wkm").use(WakeModelPackage::read) } catch (_: Exception) { null }
        if (pack == null) { wakeMessage = "尚未随 APK 提供有效模型包"; render(); return }
        beginWakePackage(pack)
    }

    private fun wakeModelSummary(descriptor: WakeModelPackage.Companion.Descriptor): String =
        "${descriptor.phrase} · 模型 ${descriptor.sha256.take(4).joinToString("") { "%02x".format(it.toInt() and 0xff) }}"

    private fun restoreWakeModel() {
        if (!configAvailable()) return
        configFlow = ConfigFlow.WAKE; wakeRestoreRequested = true
        wakeDeadline = android.os.SystemClock.elapsedRealtime() + 10_000
        wakeMessage = "正在读取设备保存的上一模型"
        requestWakeStatus()
    }

    private fun requestWakeStatus() {
        if (configFlow == ConfigFlow.NONE) {
            if (!configAvailable()) return
            configFlow = ConfigFlow.WAKE
            wakeDeadline = android.os.SystemClock.elapsedRealtime() + 10_000
        }
        if (configFlow != ConfigFlow.WAKE || wakeCanceling) return
        wakeOffset = 0; wakeReadTotal = -1; wakeRead = ByteArray(0)
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt(2 shl 16).array())) retryWakeRead()
    }

    private fun retryWakeRead() {
        if (android.os.SystemClock.elapsedRealtime() >= wakeDeadline) {
            finishWake("模型状态未在期限内确认；请重新读取，未重发设置")
            return
        }
        val ticket = ++wakeReadTicket
        val generation = directSession.current().generation
        mainHandler.postDelayed({
            if (!destroyed && foreground && ticket == wakeReadTicket &&
                generation == directSession.current().generation && configFlow == ConfigFlow.WAKE)
                requestWakeStatus()
        }, 250)
    }

    private fun finishWake(message: String) {
        wakeMessage = message; wakePackage = null; wakePayload = null; wakeExpectedSha = null
        wakeRestoreRequested = false; wakeApplied = false; wakeCanceling = false
        wakeRead = ByteArray(0); wakeReadTotal = -1; wakeOffset = 0; wakeReadTicket++
        configFlow = ConfigFlow.NONE
        directSession.finishConfigTransaction()
    }

    private fun failWake(message: String) {
        wakeMessage = message; wakeReadTicket++
        wakeStatusGeneration = null
        if (!wakeApplied && directSession.cancelConfigTransaction()) {
            wakeCanceling = true
        } else finishWake(message)
    }

    private fun requestCloudModelsRead() {
        val snapshot = directSnapshot ?: return
        if (!foreground || !directSession.current().authenticated || !snapshot.publicConfigSupported ||
            configFlow !in listOf(ConfigFlow.NONE, ConfigFlow.CLOUD) || cloudModelsWire != null || directPending ||
            otaUpload != null || otaVerificationPending || otaStatus?.state in 1L..2L) return
        configFlow = ConfigFlow.CLOUD
        cloudModelsOffset = 0; cloudModelsTotal = -1; cloudModelsReadError = null; cloudModelsWire = ByteArray(0)
        if (cloudModelsReadDeadline == 0L) cloudModelsReadDeadline = android.os.SystemClock.elapsedRealtime() + 10_000
        val request = ByteBuffer.allocate(4).putInt(1 shl 16).array()
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ, request)) {
            cloudModelsWire = null
            if (cloudModelsExpected == null) configFlow = ConfigFlow.NONE
        }
    }

    private fun validModelId(value: String): Boolean = value.length in 1..127 &&
        value.all { it.code in 0x21..0x7e && (it.isLetterOrDigit() || it in "._:/-") }

    private fun requestResponseModeRead() {
        if (configFlow == ConfigFlow.NONE) {
            if (!configAvailable()) return
            configFlow = ConfigFlow.RESPONSE
        }
        if (configFlow != ConfigFlow.RESPONSE || responseModeCanceling) return
        responseModeGeneration = null
        responseModeError = null
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt(4 shl 16).array()))
            failResponseMode("设备忙，尚未读取回答模式")
    }

    private fun requestWakeSensitivityRead() {
        if (configFlow == ConfigFlow.NONE) {
            if (!configAvailable()) return
            configFlow = ConfigFlow.SENSITIVITY
        }
        if (configFlow != ConfigFlow.SENSITIVITY) return
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt(6 shl 16).array()))
            failWakeSensitivity("设备忙，尚未读取唤醒灵敏度")
    }

    private fun editWakeSensitivity() {
        val old = wakeSensitivityPercent ?: return
        val generation = directSession.current().generation
        if (wakeSensitivityGeneration != generation) return
        val values = listOf(50, 60, 65, 75, 85, 90)
        var desired = old
        directDialog = AlertDialog.Builder(this).setTitle("唤醒灵敏度")
            .setSingleChoiceItems(values.map { "%.2f".format(it / 100.0) }.toTypedArray(),
                values.indexOf(old).takeIf { it >= 0 } ?: 1) { _, which -> desired = values[which] }
            .setNegativeButton("取消", null).setPositiveButton("保存") { _, _ ->
                if (generation != directSession.current().generation || wakeSensitivityGeneration != generation ||
                    !directSession.current().snapshotFresh || !configAvailable() || directSnapshot?.busy == true) {
                    directMessage = "设备忙或连接状态已变化，请稍后读取再设置"; render(); return@setPositiveButton
                }
                configFlow = ConfigFlow.SENSITIVITY
                wakeSensitivityExpected = desired; wakeSensitivityError = null
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                        ByteBuffer.allocate(8).putInt(6).putInt(12).array()))
                    failWakeSensitivity("设备忙，尚未发送唤醒灵敏度")
            }.show()
    }

    private fun failWakeSensitivity(message: String) {
        wakeSensitivityGeneration = null; wakeSensitivityExpected = null; wakeSensitivityError = message
        wakeSensitivityFailedGeneration = directSession.current().generation
        if (!wakeSensitivityCanceling && directSession.cancelConfigTransaction()) {
            wakeSensitivityCanceling = true
            return
        }
        wakeSensitivityCanceling = false
        configFlow = ConfigFlow.NONE
        directSession.finishConfigTransaction(message)
    }

    private fun handleWakeSensitivityResult(command: DeviceControlProtocol.Command,
                                            snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) {
            wakeSensitivityCanceling = true
            failWakeSensitivity(if (snapshot.error == 0) wakeSensitivityError ?: "唤醒灵敏度设置已取消"
                else "唤醒灵敏度取消未确认（${snapshot.error}），请重新读取")
            return
        }
        if (wakeSensitivityCanceling) return
        if (snapshot.error != 0) {
            val message = if (snapshot.error == -95 || snapshot.error == -138)
                "当前固件不支持唤醒灵敏度设置" else "唤醒灵敏度操作未确认（${snapshot.error}）；请重新读取"
            failWakeSensitivity(message); return
        }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN -> {
                val desired = wakeSensitivityExpected ?: run { failWakeSensitivity("唤醒灵敏度设置已取消"); return }
                val record = ByteBuffer.allocate(12).put("KWT1".toByteArray())
                    .putInt(desired).putInt(0).array()
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, record))
                    failWakeSensitivity("设备忙，无法发送唤醒灵敏度")
            }
            DeviceControlProtocol.Command.CONFIG_APPEND ->
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0)))
                    failWakeSensitivity("设备忙，无法保存唤醒灵敏度")
            DeviceControlProtocol.Command.CONFIG_APPLY -> requestWakeSensitivityRead()
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk
                if (chunk?.totalLength != 12 || chunk.bytes.size < 12) {
                    failWakeSensitivity("设备返回的唤醒灵敏度格式无效"); return
                }
                val record = ByteBuffer.wrap(chunk.bytes)
                val magic = record.int; val percent = record.int; val reserved = record.int
                if (magic != 0x4b575431 || percent !in 50..90 || reserved != 0) {
                    failWakeSensitivity("设备返回的唤醒灵敏度格式无效"); return
                }
                val expected = wakeSensitivityExpected
                wakeSensitivityPercent = percent
                wakeSensitivityGeneration = directSession.current().generation
                wakeSensitivityFailedGeneration = null; wakeSensitivityExpected = null; wakeSensitivityError = null
                configFlow = ConfigFlow.NONE
                val message = if (expected == null) "已读取设备唤醒门限：%.2f".format(percent / 100.0)
                    else if (percent == expected) "唤醒灵敏度已保存并回读确认"
                    else "设备回读门限与所选值不一致，请核对；未自动重发"
                directSession.finishConfigTransaction(message)
                if (expected != null) android.widget.Toast.makeText(this, message, android.widget.Toast.LENGTH_SHORT).show()
            }
            else -> Unit
        }
    }

    private fun editResponseMode() {
        val old = responseMode ?: return
        val generation = directSession.current().generation
        if (responseModeGeneration != generation) return
        var desired = old
        directDialog = AlertDialog.Builder(this).setTitle("回答模式")
            .setSingleChoiceItems(arrayOf("快速对话 · 关闭深度思考", "深度思考 · 回答可能更慢"), old) { _, which -> desired = which }
            .setNegativeButton("取消", null).setPositiveButton("保存") { _, _ ->
                if (generation != directSession.current().generation || responseModeGeneration != generation ||
                    !directSession.current().snapshotFresh || !configAvailable() || directSnapshot?.busy == true) {
                    directMessage = "设备忙或连接状态已变化，请稍后读取再设置"; render(); return@setPositiveButton
                }
                configFlow = ConfigFlow.RESPONSE
                responseModeGeneration = null; responseModeExpected = desired; responseModeError = null
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                        ByteBuffer.allocate(8).putInt(4).putInt(12).array()))
                    failResponseMode("设备忙，尚未发送回答模式")
            }.show()
    }

    private fun failResponseMode(message: String) {
        responseModeGeneration = null; responseModeExpected = null; responseModeError = message
        responseModeFailedGeneration = directSession.current().generation
        if (!responseModeCanceling && directSession.cancelConfigTransaction()) {
            responseModeCanceling = true
            return
        }
        responseModeCanceling = false; configFlow = ConfigFlow.NONE
        directMessage = message
        directSession.finishConfigTransaction(message)
    }

    private fun handleResponseModeResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) {
            responseModeCanceling = true
            failResponseMode(if (snapshot.error == 0) responseModeError ?: "回答模式设置已取消"
                else "回答模式取消未确认（${snapshot.error}），请重新读取")
            return
        }
        if (responseModeCanceling) return
        if (snapshot.error != 0) {
            failResponseMode(when (snapshot.error) {
                -138, -95 -> "当前固件或所选服务不支持回答模式"
                -16 -> "设备正在交互，请结束交互后再设置"
                else -> "回答模式操作未确认（${snapshot.error}），请重新读取；不会自动重发设置"
            })
            return
        }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN -> {
                val desired = responseModeExpected ?: run { failResponseMode("回答模式设置已取消"); return }
                val record = ByteBuffer.allocate(12).putInt(0x52535031).putInt(desired).putInt(0).array()
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, record))
                    failResponseMode("设备忙，无法发送回答模式")
            }
            DeviceControlProtocol.Command.CONFIG_APPEND ->
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0)))
                    failResponseMode("设备忙，无法保存回答模式")
            DeviceControlProtocol.Command.CONFIG_APPLY -> requestResponseModeRead()
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk
                if (chunk?.totalLength != 12 || chunk.bytes.size < 12) {
                    failResponseMode("回答模式响应长度无效"); return
                }
                val record = ByteBuffer.wrap(chunk.bytes)
                val magic = record.int; val mode = record.int; val reserved = record.int
                if (magic != 0x52535031 || mode !in 0..1 || reserved != 0) {
                    failResponseMode("回答模式响应格式无效"); return
                }
                val expected = responseModeExpected
                responseMode = mode; responseModeGeneration = directSession.current().generation
                responseModeFailedGeneration = null; responseModeExpected = null; responseModeError = null
                configFlow = ConfigFlow.NONE
                if (expected != null) {
                    directMessage = if (mode == expected) "回答模式已保存并回读确认"
                        else "设备回读与所选回答模式不一致，请核对；未自动重发"
                    directSession.finishConfigTransaction(directMessage)
                    android.widget.Toast.makeText(this, directMessage, android.widget.Toast.LENGTH_SHORT).show()
                }
            }
            else -> Unit
        }
    }

    private fun encodeCloudModels(models: CloudModels): ByteArray? {
        if (!listOf(models.asr, models.chat, models.tts).all(::validModelId)) return null
        val a = models.asr.toByteArray(StandardCharsets.US_ASCII)
        val c = models.chat.toByteArray(StandardCharsets.US_ASCII)
        val t = models.tts.toByteArray(StandardCharsets.US_ASCII)
        return ByteBuffer.allocate(12 + a.size + c.size + t.size).put("MCP1".toByteArray())
            .putShort(a.size.toShort()).putShort(c.size.toShort()).putShort(t.size.toShort()).putShort(0)
            .put(a).put(c).put(t).array()
    }

    private fun decodeCloudModels(bytes: ByteArray): CloudModels? = try {
        if (bytes.size < 12 || !bytes.copyOfRange(0, 4).contentEquals("MCP1".toByteArray())) null else {
            val b = ByteBuffer.wrap(bytes); b.position(4)
            val al = b.short.toInt() and 0xffff; val cl = b.short.toInt() and 0xffff; val tl = b.short.toInt() and 0xffff
            if (b.short.toInt() != 0 || al !in 1..127 || cl !in 1..127 || tl !in 1..127 || 12 + al + cl + tl != bytes.size) null
            else CloudModels(String(bytes, 12, al, StandardCharsets.US_ASCII),
                String(bytes, 12 + al, cl, StandardCharsets.US_ASCII),
                String(bytes, 12 + al + cl, tl, StandardCharsets.US_ASCII)).takeIf {
                    validModelId(it.asr) && validModelId(it.chat) && validModelId(it.tts) }
        }
    } catch (_: Exception) { null }

    private fun editCloudModels() {
        val old = cloudModels ?: return
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20), 0, dp(20), 0) }
        fun field(label: String, value: String) = EditText(this).apply {
            hint = label
            // Public IDs stay visible. Disable IME composition as well as
            // suggestions: URI mode still rewrites hyphens with some IMEs.
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = android.view.inputmethod.EditorInfo.IME_FLAG_FORCE_ASCII or
                android.view.inputmethod.EditorInfo.IME_ACTION_DONE
            isSingleLine = true
            setText(value)
        }.also { box.addView(it) }
        val asr = field("语音识别模型", old.asr); val chat = field("对话模型", old.chat); val tts = field("语音合成模型", old.tts)
        directDialog = AlertDialog.Builder(this).setTitle("云端模型") .setMessage("仅可保存公开模型 ID，不包含地址或密钥。")
            .setView(box).setNegativeButton("取消", null).setPositiveButton("保存") { _, _ ->
                val desired = CloudModels(asr.text.toString().trim(), chat.text.toString().trim(), tts.text.toString().trim())
                val wire = encodeCloudModels(desired)
                if (wire == null) { directMessage = "模型 ID 需为 1–127 位 ASCII 字符（字母、数字及 . _ : / -）"; render(); return@setPositiveButton }
                if (!configAvailable()) { directMessage = "设备忙，无法开始模型配置"; render(); return@setPositiveButton }
                configFlow = ConfigFlow.CLOUD
                cloudModelsGeneration = null
                cloudModelsExpected = desired; cloudModelsWire = wire; cloudModelsOffset = 0; cloudModelsReadError = null
                cloudModelsReadDeadline = 0; cloudModelsReadTicket++
                val begin = ByteBuffer.allocate(8).putInt(1).putInt(wire.size).array()
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN, begin)) {
                    cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsExpected = null
                    configFlow = ConfigFlow.NONE
                    directMessage = "设备忙，无法开始模型配置"; render()
                }
            }.show()
    }

    private fun otaErrorSnapshot(error: Int) = DeviceControlProtocol.Snapshot(
        error, false, false, null, null, null, null)

    private fun handleOtaResult(command: DeviceControlProtocol.Command,
                                snapshot: DeviceControlProtocol.Snapshot, epoch: Long) {
        if (epoch != directEpoch || !foreground || destroyed) return
        if (command == DeviceControlProtocol.Command.OTA_STATUS) {
            if (snapshot.error == 0 && snapshot.otaStatus != null) {
                otaStatus = snapshot.otaStatus
                otaStatusGeneration = directSession.current().generation
                otaStatusReadError = null
                if (snapshot.otaStatus.state == 0L && otaUpload == null &&
                    preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)) {
                    preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
                    otaMessage = "设备报告当前没有更新任务；这不代表上次目标版本已安装。"
                }
                confirmExpectedOta()
            } else {
                otaStatusReadError = if (snapshot.error != 0) {
                    "无法读取升级状态（${snapshot.error}）。"
                } else {
                    "设备未返回升级状态。"
                }
            }
            render()
            return
        }

        if (command == DeviceControlProtocol.Command.OTA_CANCEL && snapshot.error == -114) {
            otaUpload?.close(); otaUpload = null
            otaVerificationPending = true
            closeOtaServer()
            otaMessage = "设备已进入后续升级阶段，取消未被接受；正在保留设备状态供重新核对。"
            render()
            return
        }
        if (command == DeviceControlProtocol.Command.OTA_CANCEL &&
            otaUpload?.state != OtaControlUpload.State.WAITING) {
            if (snapshot.error == 0) {
                otaUpload?.close()
                otaUpload = null
                otaVerificationPending = false
                closeOtaServer()
                otaStartGate.release()
                setOtaKeepAwake(false)
                otaMessage = "设备已确认取消升级请求。"
                preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
            } else {
                otaMessage = "设备拒绝取消升级请求（${snapshot.error}）。"
            }
            render()
            return
        }
        val upload = otaUpload
        upload?.response(command, snapshot)
        otaMessage = when {
            upload?.state == OtaControlUpload.State.FAILED -> {
                val message = "设备拒绝${otaCommandLabel(command)}（${upload.error ?: snapshot.error}）；升级来源未完成。"
                closeOtaServer()
                /* The board retains a rejected source record until this GATT
                 * session closes.  Do not retry BEGIN on the same channel. */
                closeDirect()
                "$message 已断开设备连接，请重新连接后重试。"
            }
            upload?.state == OtaControlUpload.State.ACCEPTED -> {
                otaStatus = null; otaStatusGeneration = null
                otaVerificationPending = true
                "设备已接受升级来源，正在等待设备报告升级状态。"
            }
            upload?.state == OtaControlUpload.State.CANCELED -> "设备已确认取消升级请求。"
            command == DeviceControlProtocol.Command.OTA_BEGIN ->
                "设备已接受升级来源准备，正在发送来源记录（0/${upload?.totalBytes ?: 0} 字节）。"
            command == DeviceControlProtocol.Command.OTA_APPEND && upload != null &&
                (upload.uploadedBytes == upload.totalBytes || upload.uploadedBytes % 256 == 0) ->
                "正在发送升级来源记录（${upload.uploadedBytes}/${upload.totalBytes} 字节，${upload.appendCount} 段）。"
            else -> otaMessage
        }
        render()
    }

    private fun otaCommandLabel(command: DeviceControlProtocol.Command) = when (command) {
        DeviceControlProtocol.Command.OTA_BEGIN -> "升级来源准备"
        DeviceControlProtocol.Command.OTA_APPEND -> "升级来源记录"
        DeviceControlProtocol.Command.OTA_START -> "升级开始请求"
        DeviceControlProtocol.Command.OTA_CANCEL -> "取消升级请求"
        else -> "升级请求"
    }

    private fun otaSourceOpenFailureMessage(failure: Throwable?): String = when (
        (failure as? OtaPackageServerException)?.stage
    ) {
        OtaPackageServerStage.WIFI -> "无法建立本机升级来源：手机需要连接 Wi‑Fi 并获得 IPv4 地址。"
        OtaPackageServerStage.PACKAGE -> "升级包无法重新核验或准备，请重新选择已验证的固件包。"
        OtaPackageServerStage.KEYSTORE -> "手机无法创建本机升级证书，请解锁设备后重试。"
        OtaPackageServerStage.TLS -> "手机无法启动本机 HTTPS 升级服务，请检查 Wi‑Fi 后重试。"
        OtaPackageServerStage.LOCAL_IO -> "手机本地存储不可用，无法准备升级来源。"
        null -> "无法建立本机升级来源，请检查 Wi‑Fi 和已验证的固件包。"
    }

    private fun startLocalOta() {
        val file = selectedFirmwareFile ?: return
        val pack = inspectedFirmware ?: return
        val snapshot = directSnapshot
        val connection = directConnection
        val info = directFirmwareInfo
        android.util.Log.i("ShaniuOta", "start generation=$directEpoch authenticated=${directSession.current().authenticated} supported=${snapshot?.otaSupported} writePending=$directPending upload=${otaUpload?.state}")
        if (connection == null || snapshot?.otaSupported != true || directPending || otaUpload != null ||
            preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)) return
        if (info == null) { otaMessage = "正在读取设备版本，暂不能开始升级。"; render(); return }
        if (!OtaUpdatePolicy.mayStart(pack.board, DEVICE_BOARD, info.securityCounter, pack.securityCounter)) {
            otaMessage = if (pack.board != DEVICE_BOARD) "固件包不适用于当前设备。"
                else "设备安全计数不低于目标固件，不能降级或重复升级。"
            render(); return
        }
        if (!otaStartGate.acquire()) {
            android.util.Log.i("ShaniuOta", "start generation=$directEpoch skipped=preparing")
            return
        }
        val epoch = directEpoch
        otaMessage = "正在准备本机升级来源…"
        setOtaKeepAwake(true)
        render()
        ioExecutor.execute {
            val started = android.os.SystemClock.elapsedRealtime()
            val opened = runCatching { OtaPackageServer.open(applicationContext, file) }
            val failure = opened.exceptionOrNull()
            android.util.Log.i("ShaniuOta", "source generation=$epoch elapsedMs=${android.os.SystemClock.elapsedRealtime() - started} stage=${(failure as? OtaPackageServerException)?.stage ?: "READY"} error=${failure?.javaClass?.simpleName ?: "none"}")
            mainHandler.post {
                if (epoch != directEpoch || !foreground || destroyed) {
                    opened.getOrNull()?.let { server -> ioExecutor.execute { server.close() } }
                    return@post
                }
                val server = opened.getOrNull()
                if (server == null) {
                    otaMessage = otaSourceOpenFailureMessage(opened.exceptionOrNull())
                    otaStartGate.release(); setOtaKeepAwake(false)
                    render()
                    return@post
                }
                val metadata = server.metadata
                if (metadata == null || metadata.catalogSha256 != pack.catalogSha256 || !persistExpected(metadata)) {
                    ioExecutor.execute { server.close() }
                    otaStartGate.release(); setOtaKeepAwake(false)
                    otaMessage = "无法保存本次升级核验目标，未向设备发送升级请求。"
                    render()
                    return@post
                }
                otaServer = server
                lateinit var upload: OtaControlUpload
                upload = OtaControlUpload(server.requestRecord) { command, payload ->
                    if (epoch != directEpoch || !foreground || directConnection == null) false
                    else if (command == DeviceControlProtocol.Command.OTA_START &&
                        !preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, true).commit()) false
                    else directOtaRequest(command, payload)
                }
                otaUpload = upload
                if (!upload.start()) {
                    otaUpload = null; closeOtaServer(); otaStartGate.release(); setOtaKeepAwake(false)
                    otaMessage = "设备控制通道不可用，未开始升级。"
                } else {
                    otaMessage = "升级来源已准备，正在请求设备接受升级。"
                }
                render()
            }
        }
    }

    private fun cancelLocalOta() {
        val upload = otaUpload
        if (upload?.state == OtaControlUpload.State.WAITING) {
            upload.cancel()
            otaMessage = "正在请求取消升级…"
        } else if (upload?.state == OtaControlUpload.State.ACCEPTED || otaStatus?.state in 1L..2L) {
            if (!directOtaRequest(DeviceControlProtocol.Command.OTA_CANCEL))
                otaMessage = "控制通道不可用；请重新连接后核对设备升级状态。"
            else otaMessage = "正在请求设备取消升级…"
        }
        render()
    }

    private fun expectedOtaConfirmed(): Boolean {
        val sessionState = directSession.current()
        if (!sessionState.authenticated || otaStatusGeneration != sessionState.generation ||
            otaStatusReadError != null) return false
        val info = directFirmwareInfo ?: return false
        val expectedVersion = preferences.getString(KEY_OTA_EXPECTED_VERSION, null) ?: return false
        val expectedCounter = preferences.getLong(KEY_OTA_EXPECTED_COUNTER, -1L)
        val expectedDeviceId = preferences.getString(KEY_OTA_EXPECTED_DEVICE, null) ?: return false
        val version = "${info.major}.${info.minor}.${info.revision}+${info.build}"
        val status = otaStatus ?: return false
        return OtaUpdatePolicy.confirmed(expectedDeviceId, provisionedDeviceId,
            expectedVersion, expectedCounter, version, info.securityCounter,
            status.state, status.phase, status.result)
    }

    private fun confirmExpectedOta() {
        val sessionState = directSession.current()
        if (!sessionState.authenticated || otaStatusGeneration != sessionState.generation ||
            otaStatusReadError != null) return
        val info = directFirmwareInfo ?: return
        val status = otaStatus ?: return
        val version = "${info.major}.${info.minor}.${info.revision}+${info.build}"
        val confirmed = expectedOtaConfirmed()
        if (status.state == 3L && (status.phase == 7L || status.phase == 8L || status.result != 0))
            preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
        if (confirmed) {
            otaMessage = "设备已确认完成升级：$version。"
            preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
            otaUpload = null
            otaVerificationPending = false
            closeOtaServer()
            otaStartGate.release(); setOtaKeepAwake(false)
        } else if (status.state == 3L && otaVerificationPending) {
            otaMessage = "设备未确认本次升级完成，请根据设备状态重试或恢复。"
            otaUpload?.close(); otaUpload = null; otaVerificationPending = false
            closeOtaServer(); otaStartGate.release(); setOtaKeepAwake(false)
        }
    }

    private fun persistExpected(pack: BkpackInspector.Metadata): Boolean {
        if (provisionedDeviceId.isBlank()) return false
        return preferences.edit()
            .putString(KEY_OTA_EXPECTED_VERSION, pack.version)
            .putLong(KEY_OTA_EXPECTED_COUNTER, pack.securityCounter)
            .putString(KEY_OTA_EXPECTED_CATALOG, pack.catalogSha256)
            .putString(KEY_OTA_EXPECTED_DEVICE, provisionedDeviceId)
            .commit() && preferences.getString(KEY_OTA_EXPECTED_CATALOG, null) == pack.catalogSha256
    }

    private fun editDirectVolume() {
        var volume = directSnapshot?.volume ?: return
        val label = TextView(this).apply { text = "音量 $volume%"; setPadding(dp(24), dp(12), dp(24), 0) }
        val slider = SeekBar(this).apply {
            max = 100; progress = volume
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, user: Boolean) {
                    if (user) { volume = value; label.text = "音量 $value%" }
                }
                override fun onStartTrackingTouch(bar: SeekBar?) { }
                override fun onStopTrackingTouch(bar: SeekBar?) { }
            })
        }
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; addView(label); addView(slider) }
        directDialog = AlertDialog.Builder(this).setTitle("扬声器音量").setView(box)
            .setNegativeButton("取消", null).setPositiveButton("设置") { _, _ ->
                directRequest(DeviceControlProtocol.Command.VOLUME, volume)
            }.show()
    }

    @Suppress("MissingPermission")
    private fun scanDirect() {
        if (!foreground) return
        val permissions = if (android.os.Build.VERSION.SDK_INT >= 31)
            arrayOf(android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        if (permissions.any { checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED }) {
            requestPermissions(permissions, 6042); return
        }
        closeDirect(preserveAcceptedOtaSource = true)
        val epoch = directEpoch
        directDiscoveryVisible = true
        directConnecting = true; directMessage = "正在寻找附近的傻妞…"
        directScanner = DeviceControlScanner(this, { device, name ->
            if (epoch == directEpoch && foreground) {
                if (directCandidates.none { it.device.address == device.address }) {
                    directCandidates += DirectCandidate(device, name, epoch)
                    render()
                }
            }
        }, { failed ->
            if (epoch == directEpoch && foreground) {
                directScanner = null
                directConnecting = false
                directScanFinished = true
                directMessage = if (failed) "无法扫描，请检查蓝牙和附近设备权限。"
                    else if (directCandidates.isEmpty()) "未发现设备，请确认傻妞已开机并在附近。" else "已完成查找，请选择要验证的设备。"
                render()
            }
        })
        directScanner!!.start()
        render()
    }

    private fun connectDirect(device: android.bluetooth.BluetoothDevice, epoch: Long) {
        if (epoch != directEpoch || !foreground || destroyed) return
        directScanner?.close(); directScanner = null
        if (provisionedDeviceId.isBlank()) {
            // 广播仅用于发现，不能凭名称/地址建立 owner。首次添加仍由
            // 设备屏幕二维码提供身份与持有证明，不把候选地址当信任输入。
            clearDirectDiscovery()
            startProvisioning()
            return
        }
        directConnecting = false
        directScanFinished = true
        directDiscoveryDismissed = false
        directCandidates.clear()
        directMessage = "正在验证并连接傻妞…"
        directSession.connect(AndroidDeviceControlFactory(applicationContext, device, provisionedDeviceId,
            ioExecutor, { action -> mainHandler.post { action() }; Unit }))
    }

    private fun dismissDirectDiscovery() {
        if (!directDiscoveryVisible) return
        directScanner?.close(); directScanner = null
        val wasScanning = directConnecting
        directCandidates.clear()
        directScanFinished = !wasScanning
        directDiscoveryDismissed = true
        directDiscoveryVisible = false
        if (wasScanning) {
            directConnecting = false
            directMessage = "已停止查找；不会自动重新扫描。"
        }
        render()
    }

    private fun onDirectResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (destroyed) return
        if (command == DeviceControlProtocol.Command.STATUS && snapshot.error == 0) {
            if (snapshot.ready && !directServiceWasReady) {
                // Service ready only after the BLE connection: retry the config
                // reads that failed during startup; do not resend settings.
                cloudModelsFailedGeneration = null
                responseModeFailedGeneration = null
            }
            directServiceWasReady = snapshot.ready
        }
        if (command.wire in DeviceControlProtocol.Command.CONFIG_READ.wire..DeviceControlProtocol.Command.CONFIG_CANCEL.wire) {
            when (configFlow) {
                ConfigFlow.CAPABILITIES -> handleConfigCapabilities(snapshot)
                ConfigFlow.WAKE -> handleWakeResult(command, snapshot)
                ConfigFlow.CLOUD -> handleCloudModelsResult(command, snapshot)
                ConfigFlow.RESPONSE -> handleResponseModeResult(command, snapshot)
                ConfigFlow.SENSITIVITY -> handleWakeSensitivityResult(command, snapshot)
                ConfigFlow.EYES -> handleEyeResult(command, snapshot)
                ConfigFlow.SETTINGS, ConfigFlow.NONE -> Unit
            }
            if (foreground) render()
            return
        }
        if (command.wire in DeviceControlProtocol.Command.OTA_BEGIN.wire..DeviceControlProtocol.Command.OTA_CANCEL.wire) {
            handleOtaResult(command, snapshot, directEpoch)
            return
        }
        if (command == DeviceControlProtocol.Command.INFO) {
            confirmExpectedOta()
            if (snapshot.error == 0 && snapshot.firmwareInfo != null &&
                (currentTab == TAB_UPDATE || otaStatus != null ||
                 preferences.contains(KEY_OTA_EXPECTED_VERSION)) &&
                otaStatusGeneration != directSession.current().generation) {
                directOtaRequest(DeviceControlProtocol.Command.OTA_STATUS)
            }
        }
        if (command == DeviceControlProtocol.Command.MEMORY_SET || command == DeviceControlProtocol.Command.MEMORY_DELETE) {
            if (snapshot.error != 0) {
                memoryResultMessage = null; memoryDesiredEnabled = null; memoryRequestAccepted = false
            } else memoryRequestAccepted = true
        }
        if (command == DeviceControlProtocol.Command.STATUS && memoryRequestAccepted &&
            memoryResultMessage != null && snapshot.error == 0 && !snapshot.memoryPending) {
            val completed = snapshot.memoryEnabled != null && snapshot.memoryEnabled == memoryDesiredEnabled
            if (snapshot.memoryFailed || completed) {
                directMessage = if (snapshot.memoryFailed) "记忆操作未确认，请核对设备状态" else memoryResultMessage!!
                if (foreground) android.widget.Toast.makeText(this, directMessage, android.widget.Toast.LENGTH_LONG).show()
                memoryResultMessage = null; memoryDesiredEnabled = null; memoryRequestAccepted = false
            }
        }
        if (command == DeviceControlProtocol.Command.STATUS && snapshot.error == 0 &&
            currentTab in listOf(TAB_SETTINGS, TAB_SERVICES) && snapshot.publicConfigSupported &&
            configFlow == ConfigFlow.NONE) {
            if (configCapabilitiesGeneration != directSession.current().generation) requestConfigCapabilities()
            else if (responseModeGeneration != directSession.current().generation &&
                responseModeFailedGeneration != directSession.current().generation) requestResponseModeRead()
            else if (wakeStatusGeneration != directSession.current().generation) requestWakeStatus()
            else if (wakeSensitivityGeneration != directSession.current().generation &&
                wakeSensitivityFailedGeneration != directSession.current().generation) requestWakeSensitivityRead()
        }
        if (foreground && command != DeviceControlProtocol.Command.STATUS && snapshot.error != 0) {
            android.widget.Toast.makeText(this, DeviceControlSession.operationError(snapshot.error), android.widget.Toast.LENGTH_SHORT).show()
        }
        if (foreground) render()
    }

    private fun handleWakeResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) {
            finishWake(if (snapshot.error == 0) wakeMessage ?: "模型传输已取消"
                else "模型取消未确认（${snapshot.error}）；请重新读取状态")
            return
        }
        if (wakeCanceling) return
        if (command == DeviceControlProtocol.Command.CONFIG_READ && snapshot.error == -11) {
            retryWakeRead(); return
        }
        if (snapshot.error != 0) {
            failWake("唤醒词模型操作失败（${snapshot.error}）；未确认模型变更")
            return
        }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN, DeviceControlProtocol.Command.CONFIG_APPEND -> {
                val payload = wakePayload ?: run { failWake("模型传输数据已取消"); return }
                wakeMessage = "正在发送模型（${wakeOffset * 100 / payload.size}%）"
                if (wakeOffset < payload.size) {
                    val end = minOf(payload.size, wakeOffset + configAppendMax)
                    if (directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, payload.copyOfRange(wakeOffset, end)))
                        wakeOffset = end
                    else failWake("设备忙，模型传输已停止")
                } else if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0)))
                    failWake("设备忙，模型尚未应用")
            }
            DeviceControlProtocol.Command.CONFIG_APPLY -> {
                wakeApplied = true
                wakeDeadline = android.os.SystemClock.elapsedRealtime() + 60_000
                wakeMessage = "设备正在激活模型，正在回读确认"
                requestWakeStatus()
            }
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk ?: run { failWake("设备未返回模型状态"); return }
                if (chunk.totalLength !in setOf(284, 292) ||
                    (wakeReadTotal >= 0 && chunk.totalLength != wakeReadTotal)) {
                    failWake("模型状态长度无效"); return
                }
                if (wakeReadTotal < 0) { wakeReadTotal = chunk.totalLength; wakeRead = ByteArray(wakeReadTotal) }
                if (wakeOffset !in wakeRead.indices) { failWake("模型状态偏移无效"); return }
                val count = minOf(16, wakeRead.size - wakeOffset)
                chunk.bytes.copyInto(wakeRead, wakeOffset, 0, count); wakeOffset += count
                if (wakeOffset < wakeRead.size) {
                    if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                            ByteBuffer.allocate(4).putInt((2 shl 16) or wakeOffset).array())) retryWakeRead()
                    return
                }
                val status = WakeModelPackage.status(wakeRead) ?: run { failWake("模型状态格式无效"); return }
                wakeStatus = status; wakeStatusGeneration = directSession.current().generation
                if (status.busy) { retryWakeRead(); return }
                if (wakeRestoreRequested) {
                    wakeRestoreRequested = false
                    val previous = status.previous ?: run { finishWake("设备没有可恢复的上一模型"); return }
                    wakeExpectedSha = previous.sha256; wakePayload = "WKR1".toByteArray()
                    wakeOffset = 0; wakeApplied = false
                    wakeMessage = "正在恢复 ${previous.phrase}"
                    if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                            ByteBuffer.allocate(8).putInt(3).putInt(4).array()))
                        finishWake("设备忙，尚未开始恢复")
                    return
                }
                val expected = wakeExpectedSha
                if (status.error != 0) {
                    finishWake("当前为 ${status.active.phrase}；上次模型操作失败（${status.error}）")
                } else if (expected == null) {
                    finishWake("当前：${wakeModelSummary(status.active)}")
                } else if (status.active.sha256.contentEquals(expected)) {
                    finishWake("${wakeModelSummary(status.active)} 已生效并回读确认")
                } else {
                    finishWake("设备当前仍为 ${status.active.phrase}；未确认所选模型生效")
                }
            }
            else -> Unit
        }
    }

    private fun failCloudModels(message: String) {
        cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsTotal = -1; cloudModelsOffset = 0
        cloudModelsExpected = null; cloudModelsReadError = message
        cloudModelsGeneration = null
        // A failed optional read must not starve other configuration kinds.
        // A new connection or an explicit read can retry; no write is replayed.
        cloudModelsFailedGeneration = directSession.current().generation
        cloudModelsReadDeadline = 0; cloudModelsReadTicket++
        if (!cloudModelsCanceling && directSession.cancelConfigTransaction()) {
            cloudModelsCanceling = true
            return
        }
        cloudModelsCanceling = false
        configFlow = ConfigFlow.NONE
        directSession.finishConfigTransaction(message)
    }

    private fun handleCloudModelsResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) {
            cloudModelsCanceling = true
            failCloudModels(if (snapshot.error == 0) cloudModelsReadError ?: "模型配置已取消"
                else "模型配置取消未确认（${snapshot.error}）；请重新读取状态")
            return
        }
        if (cloudModelsCanceling) return
        if (command == DeviceControlProtocol.Command.CONFIG_READ && snapshot.error == -11 &&
            android.os.SystemClock.elapsedRealtime() < cloudModelsReadDeadline) {
            cloudModelsWire?.fill(0); cloudModelsWire = null
            cloudModelsOffset = 0; cloudModelsTotal = -1
            cloudModelsReadError = "设备正在保存或恢复模型配置，正在回读…"
            val generation = directSession.current().generation
            val ticket = ++cloudModelsReadTicket
            mainHandler.postDelayed({
                if (!destroyed && foreground && generation == directSession.current().generation &&
                    ticket == cloudModelsReadTicket) requestCloudModelsRead()
            }, 250)
            return
        }
        if (snapshot.error != 0) {
            failCloudModels(if (snapshot.error == -11 || snapshot.error == -115)
                "保存结果尚未确认，请重新读取设备配置；未自动重发设置"
                else "模型配置操作失败（${snapshot.error}），请读取设备当前配置")
            return
        }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk ?: run { failCloudModels("设备未返回模型配置"); return }
                if (cloudModelsTotal < 0) {
                    cloudModelsTotal = chunk.totalLength
                    cloudModelsWire = ByteArray(chunk.totalLength)
                    cloudModelsOffset = 0
                }
                val target = cloudModelsWire
                if (target == null || chunk.totalLength != cloudModelsTotal || cloudModelsOffset >= target.size) {
                    failCloudModels("模型配置响应无效"); return
                }
                val count = minOf(16, target.size - cloudModelsOffset)
                chunk.bytes.copyInto(target, cloudModelsOffset, 0, count); cloudModelsOffset += count
                if (cloudModelsOffset < target.size) {
                    val next = ByteBuffer.allocate(4).putInt((1 shl 16) or cloudModelsOffset).array()
                    if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ, next)) failCloudModels("设备忙，无法继续读取模型配置")
                } else {
                    val decoded = decodeCloudModels(target)
                    target.fill(0); cloudModelsWire = null
                    if (decoded == null) { failCloudModels("设备返回的模型配置格式无效"); return }
                    cloudModels = decoded; cloudModelsGeneration = directSession.current().generation; cloudModelsReadError = null
                    cloudModelsReadDeadline = 0; cloudModelsReadTicket++
                    val expected = cloudModelsExpected
                    configFlow = ConfigFlow.NONE
                    if (expected != null) {
                        cloudModelsExpected = null
                        directMessage = if (decoded == expected) "云端模型已保存并回读确认" else "设备回读的模型配置未确认保存"
                        directSession.finishConfigTransaction(directMessage)
                    }
                }
            }
            DeviceControlProtocol.Command.CONFIG_BEGIN -> {
                val payload = cloudModelsWire ?: run { failCloudModels("模型配置数据已取消"); return }
                val n = minOf(configAppendMax, payload.size)
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, payload.copyOfRange(0, n))) failCloudModels("设备忙，无法写入模型配置")
                else cloudModelsOffset = n
            }
            DeviceControlProtocol.Command.CONFIG_APPEND -> {
                val payload = cloudModelsWire ?: run { failCloudModels("模型配置数据已取消"); return }
                if (cloudModelsOffset < payload.size) {
                    val end = minOf(payload.size, cloudModelsOffset + configAppendMax)
                    if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, payload.copyOfRange(cloudModelsOffset, end))) failCloudModels("设备忙，无法继续写入模型配置")
                    else cloudModelsOffset = end
                } else if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))) failCloudModels("设备忙，无法应用模型配置")
            }
            DeviceControlProtocol.Command.CONFIG_APPLY -> {
                cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsOffset = 0; cloudModelsTotal = -1
                requestCloudModelsRead()
            }
            DeviceControlProtocol.Command.CONFIG_CANCEL -> failCloudModels("模型配置已取消")
            else -> Unit
        }
    }

    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, permissions, results)
        if (code == 6042 && foreground && !destroyed) {
            if (results.isNotEmpty() && results.all { it == android.content.pm.PackageManager.PERMISSION_GRANTED }) scanDirect()
            else { directMessage = "连接需要附近设备权限，可在系统设置中开启。"; render() }
        }
    }

    private fun editDeviceSettings() {
        openDeviceSettings(false)
    }

    private fun openDeviceSettings(cloud: Boolean, modelFocus: String? = null) {
        if (settingsEditor != null) return
        val embedded = cloud && currentTab == TAB_SERVICES
        cloudEditorOpening = embedded
        cloudEditorEmbedded = embedded
        configFlow = ConfigFlow.SETTINGS
        settingsEditor = com.shaniu.companion.provision.DeviceSettingsEditor(
            this, directSession, provisionedDeviceId, configAppendMax, cloud, modelFocus,
            embeddedHost = cloudEditorHost.takeIf { embedded },
            navigateBack = if (embedded) ({ navigateBack() }) else null) { outcome ->
            settingsEditor = null; cloudEditorEmbedded = false; configFlow = ConfigFlow.NONE
            directMessage = outcome; cloudModelsGeneration = null
            // Closing can happen during session notification or navigation.
            mainHandler.post { if (!destroyed) render() }
        }
        cloudEditorOpening = false
        if (embedded) {
            contentScroll.visibility = View.GONE
            cloudEditorHost.visibility = View.VISIBLE
        }
    }

    private fun renderDirectCompanion() {
        val bound = provisionedDeviceId.isNotBlank()
        // Discovery belongs to the shared session, not the tab that started it.
        // Keep candidates selectable from every connection entry and tab.
        renderDirectDiscoveryCard()
        when (currentTab) {
            TAB_OVERVIEW, TAB_INTERACTION -> {
                val page = CompanionPage(this, content)
                val state = directSession.current()
                val fresh = state.authenticated && state.snapshotFresh
                val snapshot = state.snapshot.takeIf { fresh }
                page.header("傻妞", actionLabel = "添加或查找设备") {
                    if (bound) scanDirect() else startProvisioning()
                }
                content.addView(TextView(this).apply {
                    text = if (bound) "我的傻妞" else "等待与你相遇"; textSize = 14f; setTextColor(MUTED)
                    setPadding(0, dp(3), 0, 0)
                })
                content.addView(CompanionPortraitView(this).apply { sleeping = snapshot?.wifiReady == false },
                    LinearLayout.LayoutParams(-1, dp(if (resources.configuration.fontScale > 1.3f) 140 else 204)))
                page.hero(when {
                    !bound -> "认识一下，傻妞。"
                    !state.authenticated -> "让傻妞，回到你身边。"
                    !fresh -> "正在了解傻妞的状态。"
                    snapshot?.wifiReady == false -> "先帮我连上网。"
                    snapshot?.busy == true -> "正在处理这次对话。"
                    snapshot?.ready == false -> "语音服务尚未就绪。"
                    else -> "我在，随时听你说。"
                }, when {
                    !bound -> "确认是你的设备，再开始连接。"
                    snapshot?.wifiReady == false -> "手机仍可管理，云端对话暂不可用"
                    else -> directStatus()
                })
                if (bound) {
                    val management = if (state.authenticated) "管理已连接" else "管理未连接"
                    val network = when (snapshot?.wifiReady) {
                        true -> "Wi-Fi 已连接"
                        false -> "设备未联网"
                        null -> "联网待确认"
                    }
                    page.connectionStrip(management, network, snapshot?.wifiReady == false) {
                        if (configAvailable()) editDeviceSettings() else { selectTab(TAB_SETTINGS); render() }
                    }
                    if (snapshot?.wifiReady == false) {
                        val notice = LinearLayout(this).apply {
                            orientation = LinearLayout.VERTICAL; setPadding(dp(18), dp(18), dp(18), dp(18))
                            background = design.shape(design.warningSurface, dp(22).toFloat())
                            addView(TextView(this@MainActivity).apply {
                                text = "连接还在，不必重新认领"; textSize = 16f; setTextColor(design.warning)
                                typeface = android.graphics.Typeface.DEFAULT_BOLD
                            })
                        }
                        CompanionPage(this, notice).addMuted("手机仍可通过蓝牙为傻妞更换网络。")
                        CompanionPage(this, notice).primaryButton("更换 Wi-Fi", configAvailable()) { editDeviceSettings() }
                        content.addView(notice, LinearLayout.LayoutParams(-1, -2))
                    }
                    if (snapshot?.wifiReady != false) {
                        val volume = DeviceControlPresentation.volume(state)
                        page.volumeCard(snapshot?.volume, volume.enabled, volume.reason) { value ->
                            // Recheck current ownership and freshness after the user's gesture.
                            if (DeviceControlPresentation.volume(directSession.current()).enabled)
                                directRequest(DeviceControlProtocol.Command.VOLUME, value)
                            render()
                        }
                        page.quickActions(
                            { selectTab(TAB_PERSONALITY); render() },
                            { selectTab(TAB_SERVICES); render() },
                        )
                    }
                    if (!state.authenticated && state.connection != DeviceControlSession.Connection.CONNECTING &&
                        state.connection != DeviceControlSession.Connection.RECONNECT_WAIT)
                        primaryButton(if (directConnecting) "正在连接…" else "连接我的傻妞", !directConnecting) { scanDirect() }
                    if (snapshot?.busy == true && snapshot.memoryPending != true)
                        actionButton("停止这次对话", !directPending) { directRequest(DeviceControlProtocol.Command.CANCEL) }
                } else {
                    primaryButton("添加傻妞 · 扫码连接", !busy) { startProvisioning() }
                    actionButton(if (directConnecting) "正在查找附近设备…" else "查找附近的傻妞", !busy && !directConnecting) { scanDirect() }
                    content.addView(TextView(this).apply {
                        text = "无需云账号 · 认领后再设置网络和语音服务"
                        textSize = 14f; gravity = Gravity.CENTER; setTextColor(MUTED)
                        setPadding(0, dp(16), 0, dp(8))
                    })
                }
            }
            TAB_PERSONALITY -> renderAppearance()
            TAB_RESOURCES -> {
                CompanionPage(this, content).pageTitle("资源包与唤醒词", "先检查兼容性，再发送到设备。", ::navigateBack)
                renderCustomizationResources()
            }
            TAB_PERSONA -> {
                CompanionPage(this, content).pageTitle("聊天风格", "选择陪伴的语气。", ::navigateBack)
                addMuted("傻妞是 AI 伴侣，声音由模型合成。")
                addCard("人物设置", directSnapshot?.persona?.let { directPersonas[it] } ?: "连接设备后查看和设置人物风格。")
                if (directSession.current().authenticated && directSnapshot != null) {
                    directPersonas.forEachIndexed { index, name ->
                        settingsRow(name, directPersonaDescriptions[index],
                            enabled = !directPending && directSession.current().snapshotFresh && directSnapshot?.busy == false,
                            selected = directSnapshot?.persona == index) {
                            directRequest(DeviceControlProtocol.Command.PERSONA, index)
                        }
                    }
                } else if (bound) primaryButton("连接傻妞", !directConnecting) { scanDirect() }
                else primaryButton("先添加我的傻妞", !busy) { startProvisioning() }
            }
            TAB_PRIVACY -> {
                CompanionPage(this, content).pageTitle("隐私与记忆", "有用的陪伴，也应有清楚的边界。", ::navigateBack)
                renderPrivacyControls(content)
            }
            TAB_UPDATE -> {
                CompanionPage(this, content).pageTitle("更新", "每次进步，都清楚可见。")
                CompanionPage(this, content).segments(listOf("固件更新", "资源更新"), if (updateResources) 1 else 0) {
                    updateResources = it == 1; render()
                }
                if (updateResources) { renderResourceOverview(); return }
                val localState = directSnapshot
                val ota = otaStatus
                if (ota != null && ota.state != 0L) {
                    val sessionState = directSession.current()
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
                        percent?.toInt().takeIf { otaCurrent && ota.phase == 1L }, stage, confirmed)
                }
                val info = directFirmwareInfo
                if (ota == null || ota.state == 0L) CompanionPage(this, content).updateIntroduction(
                    info?.let { "${it.major}.${it.minor}.${it.revision} · ${it.build}" } ?: "待真实设备回读",
                    inspectedFirmware?.let { "本地包 · ${it.version}" } ?: "尚未选择本地包")
                val canStart = inspectedFirmware != null && selectedFirmwareFile != null &&
                    localState?.otaSupported == true && directConnection != null &&
                    otaUpload == null && !directPending && !preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)
                primaryButton("开始固件更新", canStart) { startLocalOta() }
                actionButton(if (firmwareInspectionPending) "正在检查固件包…" else "选择本地更新包", !firmwareInspectionPending) {
                    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
                        putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                            android.provider.DocumentsContract.buildDocumentUri(
                                "com.android.externalstorage.documents", "primary:Download"))
                    }, FIRMWARE_PACKAGE_REQUEST)
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
                    val cancellable = ota?.phase == null || ota.phase in 1L..2L || ota.state == 1L
                    actionButton("取消升级", !directPending && cancellable) { cancelLocalOta() }
                    if (!cancellable) addMuted("设备已进入安装提交阶段，请保持供电并等待重连；此时不能立即取消。")
                }
                addMuted("更新期间请保持 App 在前台。蓝牙发送更新指令，设备通过局域网 HTTPS 拉取镜像；重连并核对版本后才确认完成。")
                if (!canStart) addMuted(when {
                    inspectedFirmware == null -> "先选择普通 OTA 包；工厂全量包不能用于此入口。"
                    directConnection == null -> "需要连接已认领的设备，才能核对更新能力。"
                    localState?.otaSupported != true -> "当前固件未提供此更新能力。"
                    else -> "当前设备事务尚未结束，请等待后重试。"
                })
                if (localState?.otaSupported != true && directConnection != null)
                    addMuted("设备固件未声明本地升级能力，不能开始升级。")
                if (directConnection == null && provisionedDeviceId.isNotBlank())
                    primaryButton(if (directConnecting) "正在连接…" else "连接设备读取版本", !directConnecting) { scanDirect() }
            }
            TAB_ADVANCED -> renderVoiceAdvanced()
            else -> {
                if (currentTab == TAB_SETTINGS) { renderSettingsHome(bound); return }
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
        }
    }

    private fun renderVoiceAdvanced() {
        CompanionPage(this, content).pageTitle("高级对话设置", "连接、声音与回应。", ::navigateBack)
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
            if (!directSession.cancelConfigTransaction()) directMessage = "当前模型配置已结束"
            else directMessage = "正在取消模型配置"
            render()
        }
    }

    private fun renderSettingsHome(bound: Boolean) {
        val page = CompanionPage(this, content)
        page.pageTitle("设置", "让连接、声音和隐私都由你掌握。")
        page.settingsRow("我的傻妞", if (bound) "设备归属 · 当前手机已认领" else "尚未添加设备", iconName = "device") { showConnectionSheet() }
        page.settingsRow("Wi-Fi", when (directSnapshot?.wifiReady) {
            true -> "设备已连接网络"; false -> "设备未联网"; else -> "连接设备后读取"
        }, iconName = "wifi") { openNetworkSettings() }
        page.settingsRow("云服务与模型", "对话、语音识别、语音合成", iconName = "cloud") { selectTab(TAB_SERVICES); render() }
        sectionTitle("使用偏好")
        page.settingsRow("外观模式", "跟随系统 · " + if (design.dark) "深色" else "浅色", iconName = "moon") {
            showCompanionSheet("外观模式", "跟随手机系统的浅色或深色模式。") { body, _ ->
                CompanionPage(this, body).addCard("系统外观", "在手机的显示设置中切换外观，App 将自动跟随。")
            }
        }
        page.settingsRow("隐私与记忆", "管理对话记忆与数据", iconName = "shield") { showPrivacySheet() }
        page.settingsRow("帮助与诊断", "连接问题与设备状态", iconName = "info") {
            showCompanionSheet("连接问题与设备状态", "从手机、网络到云服务，逐项检查。") { body, _ ->
                val details = CompanionPage(this, body)
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
                enabled = directSession.current().authenticated && !directPending && settingsEditor == null && factoryReset == null,
                danger = true, iconName = "refresh") { confirmFactoryReset() }
            if (com.shaniu.companion.provision.FactoryResetController.hasPending(this, provisionedDeviceId))
                settingsRow("核对恢复出厂结果", "只查询回执，不重复清理", directSession.current().authenticated && !directPending) { queryFactoryReset() }
        }
        page.addMuted("App ${BuildConfig.VERSION_NAME} · 设备状态以回读为准")
    }

    private fun openNetworkSettings() {
        dismissCompanionSheet()
        if (configAvailable()) editDeviceSettings()
        else if (provisionedDeviceId.isBlank()) startProvisioning()
        else if (!directSession.current().authenticated) scanDirect()
        else AlertDialog.Builder(this).setTitle("暂时无法修改网络")
            .setMessage("设备配置能力尚未就绪，或另一项操作正在进行。请稍后再试。")
            .setPositiveButton("知道了", null).show()
    }

    private fun showConnectionSheet() {
        showCompanionSheet("我的傻妞", "手机与设备，设备与网络。") { body, _ ->
            val page = CompanionPage(this, body)
            body.addView(CompanionExpressionView(this, true), LinearLayout.LayoutParams(-1, dp(140)))
            page.addCard("手机 ↔ 设备", if (directSession.current().authenticated) "蓝牙已连接 · 身份已验证" else "未建立已验证的蓝牙连接")
            page.addCard("设备 ↔ 网络", when (directSnapshot?.wifiReady) {
                true -> "设备已联网"; false -> "设备未联网"; else -> "等待设备回读"
            })
            page.settingsRow("设置网络", "蓝牙负责管理，Wi-Fi 负责云端对话", iconName = "wifi") { openNetworkSettings() }
            page.settingsRow("核对认领结果", "恢复中断或不确定的认领", !busy && settingsEditor == null, iconName = "qr") {
                dismissCompanionSheet(); startProvisioning()
            }
            if (directConnection != null) page.settingsRow("断开手机连接", "设备仍可独立对话") {
                dismissCompanionSheet(); closeDirect(); render()
            }
            if (provisionedDeviceId.isNotBlank()) page.settingsRow("移除本机连接资料", "保留设备网络和服务配置", !busy && !directPending) {
                dismissCompanionSheet(); confirmClearProvisioning()
            }
        }
    }

    private fun renderAppearance() {
        val page = CompanionPage(this, content)
        page.pageTitle("定制", "一点点，变成你熟悉的她。")
        val preview = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(20), dp(20), dp(20))
            background = design.shape(Color.parseColor("#183E31"), dp(24).toFloat())
        }
        preview.addView(TextView(this).apply {
            text = "HER LITTLE WORLD"; textSize = 11f; letterSpacing = 0.16f; setTextColor(Color.parseColor("#ACC7BC"))
        })
        val portrait = CompanionExpressionView(this, true).apply { expression = expressionPreview }
        preview.addView(portrait, LinearLayout.LayoutParams(-1, dp(160)))
        val caption = LinearLayout(this).apply {
            orientation = if (resources.configuration.fontScale > 1.3f) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        caption.addView(TextView(this).apply {
            text = "薄荷眼睛"; textSize = 18f; setTextColor(Color.parseColor("#E7F0EA")); typeface = android.graphics.Typeface.DEFAULT_BOLD
        }, if (caption.orientation == LinearLayout.HORIZONTAL) LinearLayout.LayoutParams(0, -2, 1f) else LinearLayout.LayoutParams(-1, -2))
        caption.addView(TextView(this).apply {
            text = "示意 · 仅预览"; textSize = 12f; setTextColor(Color.parseColor("#ACC7BC"))
        })
        preview.addView(caption)
        content.addView(preview, LinearLayout.LayoutParams(-1, -2))
        page.sectionTitle("表情预览 · 仅预览")
        val expressions = LinearLayout(this)
        val tiles = mutableListOf<LinearLayout>()
        fun refreshSelection() {
            tiles.forEachIndexed { index, tile ->
                tile.isSelected = index == expressionPreview
                tile.background = design.shape(if (tile.isSelected) design.selected else design.surface, dp(18).toFloat()).apply {
                    setStroke(dp(if (tile.isSelected) 2 else 1), if (tile.isSelected) design.accent else design.divider)
                }
                tile.contentDescription = listOf("日常", "开心", "晚安")[index] + "，仅预览" + if (tile.isSelected) "，当前已选择" else ""
            }
        }
        listOf("日常", "开心", "晚安").forEachIndexed { index, label ->
            val tile = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; setPadding(dp(8), dp(8), dp(8), dp(12))
                isClickable = true; isFocusable = true
                addView(CompanionExpressionView(this@MainActivity).apply { expression = index }, LinearLayout.LayoutParams(-1, dp(48)))
                addView(TextView(this@MainActivity).apply {
                    text = label; textSize = 12f; setTextColor(INK); gravity = Gravity.CENTER
                    importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                })
                setOnClickListener { expressionPreview = index; portrait.expression = index; refreshSelection() }
            }
            tiles.add(tile)
            expressions.addView(tile, LinearLayout.LayoutParams(0, -2, 1f).apply { if (index < 2) marginEnd = dp(10) })
        }
        refreshSelection()
        content.addView(expressions, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
        page.sectionTitle("声音与唤醒")
        val currentWake = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
        page.settingsRow(currentWake?.active?.let(::wakeModelSummary) ?: "当前唤醒词", if (currentWake == null) "连接后回读设备当前模型" else "设备当前唤醒模型", iconName = "mic") { showWakeSheet() }
        page.settingsRow("唤醒应答", "查看本地应答设置", iconName = "speaker") {
            showCompanionSheet("唤醒应答", "本地应答与云端语音分开管理。") { body, _ ->
                CompanionPage(this, body).addCard("设备能力", "当前固件未提供独立的唤醒应答设置，不能在 App 中修改应答语。")
            }
        }
        page.sectionTitle("资源管理")
        page.settingsRow("资源更新", "眼睛、唤醒模型与应答音", iconName = "download") { updateResources = true; selectTab(TAB_UPDATE); render() }
        page.settingsRow("对话偏好", "聊天风格、声音与回答模式", iconName = "spark") { selectTab(TAB_ADVANCED); render() }
        page.addMuted("实验模型单独标记，不会自动替换稳定默认。")
    }

    private fun showWakeSheet() {
        showCompanionSheet("一句你好，唤醒她", "当前唤醒词以设备回读为准。", done = false) { body, dialog ->
            val page = CompanionPage(this, body)
            val wave = LinearLayout(this).apply { gravity = Gravity.CENTER; importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }
            repeat(13) { index ->
                wave.addView(View(this).apply { background = design.shape(design.accent, dp(2).toFloat()) },
                    LinearLayout.LayoutParams(dp(3), dp(when { (index + 1) % 3 == 0 -> 27; (index + 1) % 2 == 0 -> 18; else -> 7 })).apply {
                        marginStart = dp(2); marginEnd = dp(2)
                    })
            }
            body.addView(wave, LinearLayout.LayoutParams(-1, dp(48)).apply { bottomMargin = dp(12) })
            val current = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
            page.informationRow("当前唤醒模型", current?.active?.let(::wakeModelSummary) ?: "尚未读取设备当前模型", "mic")
            page.informationRow("候选模型单独验证", "先校验兼容性，不会自动替换当前模型", "shield")
            page.primaryButton("管理唤醒资源", true) { selectTab(TAB_RESOURCES); render() }
            body.addView(TextView(this).apply {
                text = "完成"; textSize = 16f; gravity = Gravity.CENTER; minimumHeight = dp(54)
                setTextColor(design.accent); isClickable = true; isFocusable = true
                setOnClickListener { dialog.dismiss() }
            }, LinearLayout.LayoutParams(-1, -2))
        }
    }

    private fun renderResourceOverview() {
        val page = CompanionPage(this, content)
        page.settingsRow("眼睛资源", "查看、导入与确认当前外观", iconName = "eye") { selectTab(TAB_RESOURCES); render() }
        val current = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
        page.settingsRow("唤醒模型", current?.active?.let(::wakeModelSummary) ?: "设备当前模型待回读", iconName = "mic") { selectTab(TAB_RESOURCES); render() }
        page.settingsRow("应答音", "当前固件未提供独立资源安装", enabled = false, iconName = "speaker") { }
        actionButton("导入资源包", true) { selectTab(TAB_RESOURCES); render() }
        page.notice("先校验兼容性，再安装和确认生效。")
    }

    private fun renderPrivacyControls(body: LinearLayout) {
        val page = CompanionPage(this, body)
        if (directSession.current().authenticated && directSnapshot != null) {
            val memory = directSnapshot!!
            val canManage = directSession.current().snapshotFresh && !directPending && memoryResultMessage == null && memory.ready && !memory.busy && memory.memorySupported && !memory.memoryPending && memory.memoryEnabled != null
            page.settingsRow("对话记忆", when {
                !memory.memorySupported -> "设备固件尚未提供此功能"
                memory.memoryPending -> "正在处理，请稍候"
                memory.memoryFailed -> "上次操作未确认，请核对设备状态"
                memory.memoryEnabled == true -> "已开启 · 加密保存最近三轮对话"
                memory.memoryEnabled == false -> "已关闭 · 不读取或新增保存"
                else -> "正在读取设备设置"
            }, enabled = canManage, iconName = "shield", checked = memory.memoryEnabled == true) {
                val enable = memory.memoryEnabled != true
                confirm(if (enable) "开启跨重启记忆" else "关闭跨重启记忆",
                    if (enable) "允许设备加密保存最近三轮对话，并在下次启动后继续使用。" else "停止读取和保存跨重启记忆。已保存的内容会保留，可通过下方入口删除。") {
                    memoryResultMessage = if (enable) "跨重启记忆已开启" else "跨重启记忆已关闭"
                    memoryDesiredEnabled = enable
                    memoryRequestAccepted = false
                    if (!directRequest(DeviceControlProtocol.Command.MEMORY_SET, if (enable) 1 else 0)) {
                        memoryResultMessage = null; memoryDesiredEnabled = null
                    }
                }
            }
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
        page.notice("默认仅保留本次开机的近期上下文。开启记忆后，设备加密保存最近三轮对话。语音会发送到你配置的云服务；云端保留的记录需在对应服务中管理。服务密钥不提供明文回读。")
    }

    private fun showPrivacySheet() {
        showCompanionSheet("隐私与记忆", "有用的陪伴，也应有清楚的边界。") { body, _ ->
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            var previous: List<Any?>? = null
            refreshCompanionSheet = {
                val state = directSession.current()
                val signature = listOf(state.authenticated, state.snapshotFresh, state.snapshot, directPending, memoryResultMessage)
                if (signature != previous) {
                    previous = signature; controls.removeAllViews(); renderPrivacyControls(controls)
                }
            }
            refreshCompanionSheet?.invoke()
        }
    }

    private fun dismissCompanionSheet() {
        val previous = companionSheet
        companionSheet = null; refreshCompanionSheet = null
        previous?.dismiss()
    }

    private fun showCompanionSheet(title: String, subtitle: String, done: Boolean = true,
                                   populate: (LinearLayout, android.app.Dialog) -> Unit) {
        dismissCompanionSheet()
        val dialog = android.app.Dialog(this)
        companionSheet = dialog
        val body = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(22), dp(12), dp(22), dp(20))
        }
        body.addView(View(this).apply { background = design.shape(design.divider, dp(2).toFloat()) },
            LinearLayout.LayoutParams(dp(36), dp(4)).apply { gravity = Gravity.CENTER_HORIZONTAL; bottomMargin = dp(12) })
        val top = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL }
        top.addView(TextView(this).apply {
            text = "SHANIU"; textSize = 11f; letterSpacing = 0.18f; setTextColor(MUTED)
        }, LinearLayout.LayoutParams(0, -2, 1f))
        top.addView(android.widget.ImageButton(this).apply {
            setImageDrawable(CompanionIcons.drawable(this@MainActivity, "close", MUTED))
            contentDescription = "关闭"; setPadding(dp(12), dp(12), dp(12), dp(12))
            background = design.shape(design.selected, dp(24).toFloat()); setOnClickListener { dialog.dismiss() }
        }, LinearLayout.LayoutParams(dp(48), dp(48)))
        body.addView(top)
        CompanionPage(this, body).hero(title, subtitle)
        populate(body, dialog)
        if (done) CompanionPage(this, body).primaryButton("完成", true) { dialog.dismiss() }
        val scroll = object : ScrollView(this) {
            override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
                super.onMeasure(widthMeasureSpec, View.MeasureSpec.makeMeasureSpec(
                    dp((resources.configuration.screenHeightDp - 48).coerceAtLeast(160)), View.MeasureSpec.AT_MOST))
            }
        }.apply { isFillViewport = false; addView(body); clipToOutline = true }
        dialog.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
        dialog.setContentView(scroll)
        dialog.window?.apply {
            setBackgroundDrawable(design.shape(design.background, dp(30).toFloat()))
            setGravity(Gravity.BOTTOM); addFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            setDimAmount(0.32f)
        }
        dialog.setOnDismissListener {
            if (companionSheet === dialog) { companionSheet = null; refreshCompanionSheet = null }
        }
        dialog.show()
        dialog.window?.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
    }

    private fun renderDirectDiscoveryCard() {
        discoveryHost.removeAllViews()
        discoveryHost.visibility = if (directDiscoveryVisible) View.VISIBLE else View.GONE
        pageRoot.importantForAccessibility = if (directDiscoveryVisible)
            View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS else View.IMPORTANT_FOR_ACCESSIBILITY_AUTO
        if (!directDiscoveryVisible) return
        discoveryHost.setBackgroundColor(Color.parseColor("#610F2118"))
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
            background = design.shape(design.surface, dp(32).toFloat()); clipToOutline = true
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
        )
    }

    private fun renderCustomizationResources() {
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

    private fun selectEyePack(uri: android.net.Uri) {
        if (eyeImportPending) return
        eyeImportPending = true; eyeMessage = "正在检查眼睛素材包…"; render()
        ioExecutor.execute {
            val result = runCatching {
                val file = java.io.File.createTempFile("eyes-selected-", ".bkep", cacheDir)
                try {
                    contentResolver.openInputStream(uri).use { input ->
                        requireNotNull(input)
                        file.outputStream().use { output ->
                            val buffer = ByteArray(8192); var total = 0
                            while (true) {
                                val count = input.read(buffer)
                                if (count < 0) break
                                total += count; require(total <= 131072)
                                output.write(buffer, 0, count)
                            }
                        }
                    }
                    val bytes = file.readBytes()
                    val pack = requireNotNull(EyePack.parse(bytes))
                    Triple(pack, file, MessageDigest.getInstance("SHA-256").digest(bytes))
                } catch (error: Exception) { file.delete(); throw error }
            }
            mainHandler.post {
                eyeImportPending = false
                if (destroyed || !foreground) { result.getOrNull()?.second?.delete(); return@post }
                val accepted = result.getOrNull()
                if (accepted == null) eyeMessage = "眼睛素材包格式、长度或校验无效"
                else {
                    selectedEyeFile?.takeIf { it != accepted.second }?.delete()
                    selectedEyePack = accepted.first
                    selectedEyeAssetSha256 = accepted.third
                    selectedEyeFile = accepted.second
                    eyeMessage = "已选择 ${accepted.first.packId}（版本 ${accepted.first.revision}）"
                }
                render()
            }
        }
    }

    private fun startEyeInstall() {
        val file = selectedEyeFile ?: return
        selectedEyePack ?: return
        val assetSha256 = selectedEyeAssetSha256 ?: return
        if (!configAvailable() || eyeImportPending) return
        val epoch = directEpoch
        configFlow = ConfigFlow.EYES; eyeMessage = "正在准备 Wi-Fi 眼睛素材来源…"; render()
        ioExecutor.execute {
            val opened = runCatching { OtaPackageServer.openAsset(applicationContext, file, assetSha256) }
            mainHandler.post {
                if (epoch != directEpoch || !foreground || destroyed || configFlow != ConfigFlow.EYES) {
                    opened.getOrNull()?.let { server -> ioExecutor.execute { server.close() } }; return@post
                }
                val server = opened.getOrNull()
                if (server == null) { finishEyeInstall(otaSourceOpenFailureMessage(opened.exceptionOrNull())); render(); return@post }
                if (server.requestRecord.size !in 44..3371) {
                    ioExecutor.execute { server.close() }
                    finishEyeInstall("本机眼睛素材来源请求长度无效"); render(); return@post
                }
                eyeServer = server; eyeRecord = server.requestRecord; eyeOffset = 0; eyeRead = ByteArray(0)
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                        ByteBuffer.allocate(8).putInt(5).putInt(server.requestRecord.size).array()))
                    finishEyeInstall("设备控制通道不可用，未开始眼睛安装")
                else eyeMessage = "正在发送 Wi-Fi 素材来源…"
                render()
            }
        }
    }

    private fun requestEyeRead(offset: Int = 0) {
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt((5 shl 16) or offset).array()))
            finishEyeInstall("无法读取设备当前眼睛状态")
    }

    private fun readCurrentEyes() {
        if (!configAvailable()) return
        configFlow = ConfigFlow.EYES; eyeReadingOnly = true; eyeOffset = 0; eyeRead = ByteArray(0)
        eyeMessage = "正在读取设备当前眼睛…"
        requestEyeRead(); render()
    }

    private fun finishEyeInstall(message: String) {
        eyeRecord = null; eyeOffset = 0; eyeRead = ByteArray(0); eyeReadingOnly = false
        closeEyeServer()
        if (configFlow == ConfigFlow.EYES) {
            configFlow = ConfigFlow.NONE
            directSession.finishConfigTransaction(message)
        }
        eyeMessage = message
    }

    private fun handleEyeResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (snapshot.error != 0) { finishEyeInstall("眼睛安装未确认（${snapshot.error}）；请读取设备实际状态"); return }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN, DeviceControlProtocol.Command.CONFIG_APPEND -> {
                val record = eyeRecord ?: run { finishEyeInstall("眼睛安装请求已取消"); return }
                if (eyeOffset < record.size) {
                    val end = minOf(record.size, eyeOffset + configAppendMax)
                    if (directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, record.copyOfRange(eyeOffset, end))) eyeOffset = end
                    else finishEyeInstall("设备忙，眼睛安装请求未完成")
                } else if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))) {
                    finishEyeInstall("设备忙，未开始 Wi-Fi 安装")
                }
            }
            DeviceControlProtocol.Command.CONFIG_APPLY -> { eyeOffset = 0; eyeRead = ByteArray(0); requestEyeRead() }
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk ?: run { finishEyeInstall("设备未返回眼睛状态"); return }
                if (chunk.totalLength != 108) { finishEyeInstall("设备返回的眼睛状态长度无效"); return }
                if (eyeRead.isEmpty()) eyeRead = ByteArray(108)
                if (eyeOffset !in eyeRead.indices) { finishEyeInstall("眼睛状态偏移无效"); return }
                val count = minOf(16, eyeRead.size - eyeOffset)
                chunk.bytes.copyInto(eyeRead, eyeOffset, 0, count); eyeOffset += count
                if (eyeOffset < eyeRead.size) { requestEyeRead(eyeOffset); return }
                val selected = selectedEyePack
                val b = ByteBuffer.wrap(eyeRead)
                val idBytes = eyeRead.copyOfRange(20, 52); val zero = idBytes.indexOfFirst { it == 0.toByte() }
                val id = if (zero in 1..31 && idBytes.copyOfRange(zero + 1, 32).all { it == 0.toByte() })
                    String(idBytes, 0, zero, StandardCharsets.US_ASCII) else ""
                val state = b.getInt(4); val error = b.getInt(8); val revision = b.getInt(12).toUInt().toLong()
                if (eyeReadingOnly) {
                    finishEyeInstall(if (id.isNotEmpty()) "当前眼睛：$id（版本 $revision，状态 $state，错误 $error）"
                        else "设备返回的眼睛状态格式无效")
                    return
                }
                val ready = selected != null && eyeRead.copyOfRange(0, 4).contentEquals("EYE1".toByteArray()) && state == 3 &&
                    error == 0 && revision == selected.revision && id == selected.packId &&
                    eyeRead.copyOfRange(52, 84).contentEquals(selected.sourceSha256)
                finishEyeInstall(if (ready) "眼睛素材已通过 Wi-Fi 安装并回读确认" else "设备眼睛状态与所选素材不一致；未自动重发")
            }
            else -> Unit
        }
    }

    private fun inspectFirmwarePackage(uri: android.net.Uri?) {
        if (uri == null || firmwareInspectionPending) return
        val epoch = ++firmwareInspectionEpoch
        inspectedFirmware = null
        firmwareInspectionPending = true
        firmwareInspectionMessage = null
        ioExecutor.execute {
            val inspected = runCatching {
                val file = java.io.File.createTempFile("firmware-selected-", ".bkpack", cacheDir)
                try {
                    requireNotNull(contentResolver.openInputStream(uri)).use { input ->
                        file.outputStream().use { output ->
                            val buffer = ByteArray(8192)
                            var total = 0L
                            while (true) {
                                val count = input.read(buffer)
                                if (count < 0) break
                                total += count
                                require(total <= 20L * 1024 * 1024)
                                output.write(buffer, 0, count)
                            }
                        }
                    }
                    BkpackInspector.inspect(file) to file
                } catch (error: Exception) {
                    file.delete()
                    throw error
                }
            }
            mainHandler.post {
                finishFirmwareInspection(epoch, inspected.getOrNull(), inspected.isFailure)
            }
        }
    }

    private fun finishFirmwareInspection(
        epoch: Long,
        accepted: Pair<BkpackInspector.Metadata, java.io.File>?,
        failed: Boolean,
    ) {
        val current = epoch == firmwareInspectionEpoch && !destroyed
        if (!current) {
            accepted?.second?.delete()
            return
        }
        firmwareInspectionPending = false
        inspectedFirmware = accepted?.first
        accepted?.let { pair ->
            val file = pair.second
            selectedFirmwareFile?.takeIf { it != file }?.delete()
            selectedFirmwareFile = file
        }
        firmwareInspectionMessage = if (failed)
            "无法验证此固件包，请选择完整的 .bkpack 文件后重试。" else null
        render()
    }

    private fun statusStrip() {
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

    private fun renderOverview() {
        content.addView(CompanionPortraitView(this), LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dp(230)).apply { bottomMargin = dp(24) })
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

    private fun renderSettings() {
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

    private fun renderDeveloperConnection() {
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

    private fun renderInteraction() {
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

    private fun renderPersonality() {
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

    private fun renderPrivacy() {
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

    private fun renderLongTermMemoryControls(state: PermissionState) {
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

    private fun renderUpdate() {
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

    private fun renderRelease(release: FirmwareRelease, state: CompanionState) {
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

    private fun connect(
        originValue: String,
        deviceId: String,
        pinsValue: String,
        token: String,
        automatic: Boolean = false,
    ) {
        if (!automatic) {
            autoReconnectAllowed = true
            reconnectAttempt = 0
        }
        val pins = try {
            parsePins(pinsValue)
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：证书 pin 格式无效"
            render()
            return
        }
        val configuration = try {
            GatewayTransportConfiguration.fromExplicit(originValue, pins)
                ?: throw IllegalArgumentException()
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：必须填写合法的 HTTPS Gateway 根地址"
            render()
            return
        }
        try {
            CompanionStore(deviceId)
            if (token.isNotEmpty()) GatewayTokenPolicy.requireValid(token)
        } catch (_: IllegalArgumentException) {
            lastAction = "连接失败：设备 ID 或访问令牌格式无效"
            render()
            return
        }

        closeRuntime(clearReportedState = true, resetReconnectAttempt = !automatic)
        busy = true
        lastAction = "正在认证并获取设备快照"
        render()
        val epoch = connectionEpoch
        ioExecutor.execute {
            var sessionForCleanup: OkHttpConsoleGatewaySession? = null
            try {
                val provider = if (token.isNotEmpty())
                    com.shaniu.companion.gateway.GatewayAccessTokenProvider { token }
                else tokenStore.providerFor(configuration.origin.toString(), deviceId)
                val candidateSession = OkHttpConsoleGatewaySession(configuration, provider)
                sessionForCleanup = candidateSession
                val candidateClient = ConsoleGatewayClient(configuration.origin, candidateSession)
                val snapshot = when (val result = candidateClient.fetchSnapshot(deviceId)) {
                    is GatewayCallResult.Failure -> {
                        sessionForCleanup = null
                        candidateSession.close()
                        postToMain {
                            if (!isCurrent(epoch)) return@postToMain
                            busy = false
                            handleGatewayFailure("连接失败", result, token.isNotEmpty())
                            render()
                            if (token.isEmpty() && result.retryable) {
                                scheduleGatewayReconnect(epoch)
                            }
                        }
                        return@execute
                    }
                    is GatewayCallResult.Success -> result.value
                }

                val newStore = CompanionStore(deviceId)
                val disposition = newStore.apply(snapshot)
                if (disposition != EventDisposition.APPLIED) {
                    sessionForCleanup = null
                    postConnectionFailure(
                        epoch, candidateSession, "连接失败：快照未通过状态校验",
                    )
                    return@execute
                }

                var activeSession = candidateSession
                var activeClient = candidateClient
                if (token.isNotEmpty()) {
                    // Persist endpoint metadata in a fail-closed state before replacing
                    // the scoped credential. Keystore and disk work stays off the UI thread.
                    val pendingSaved = preferences.edit()
                        .putString(KEY_ORIGIN, configuration.origin.toString())
                        .putString(KEY_DEVICE_ID, deviceId)
                        .putString(KEY_PINS, pinsValue)
                        .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, true)
                        .commit()
                    if (!pendingSaved) {
                        sessionForCleanup = null
                        postConnectionFailure(
                            epoch, candidateSession,
                            "连接失败：无法保存设备资料，请重试",
                        )
                        return@execute
                    }
                    try {
                        tokenStore.storeFor(configuration.origin.toString(), deviceId, token)
                    } catch (_: Exception) {
                        sessionForCleanup = null
                        postConnectionFailure(
                            epoch, candidateSession,
                            "连接失败：无法保存设备凭据，请重新导入",
                            requireControlCredential = true,
                        )
                        return@execute
                    }
                    if (!preferences.edit()
                            .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, false)
                            .commit()) {
                        sessionForCleanup = null
                        postConnectionFailure(
                            epoch, candidateSession,
                            "凭据已安全写入，但绑定事务未完成；请重新导入",
                            requireControlCredential = true,
                        )
                        return@execute
                    }
                    candidateSession.close()
                    activeSession = OkHttpConsoleGatewaySession(
                        configuration,
                        tokenStore.providerFor(configuration.origin.toString(), deviceId),
                    )
                    activeClient = ConsoleGatewayClient(configuration.origin, activeSession)
                } else {
                    val saved = preferences.edit()
                        .putString(KEY_ORIGIN, configuration.origin.toString())
                        .putString(KEY_DEVICE_ID, deviceId)
                        .putString(KEY_PINS, pinsValue)
                        .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, false)
                        .commit()
                    if (!saved) {
                        sessionForCleanup = null
                        postConnectionFailure(
                            epoch, candidateSession,
                            "连接失败：无法保存设备资料，请重试",
                        )
                        return@execute
                    }
                }

                sessionForCleanup = null
                val preparedSession = activeSession
                val preparedClient = activeClient
                mainHandler.post {
                    if (!isCurrent(epoch)) {
                        closeSession(preparedSession)
                        return@post
                    }
                    controlCredentialRequired = false
                    draftOrigin = configuration.origin.toString()
                    draftDeviceId = deviceId
                    draftPins = pinsValue
                    store = newStore
                    runtime = GatewayRuntime(epoch, deviceId, preparedSession, preparedClient)
                    releaseCatalog = null
                    busy = false
                    lastAction = "快照已同步，正在建立事件流"
                    render()
                    openEventStream(epoch)
                }
            } catch (_: Exception) {
                sessionForCleanup?.close()
                postToMain {
                    if (!isCurrent(epoch)) return@postToMain
                    busy = false
                    lastAction = "连接失败：凭据不可用或本机安全存储异常"
                    render()
                }
            }
        }
    }

    private fun postConnectionFailure(
        epoch: Long,
        session: OkHttpConsoleGatewaySession,
        message: String,
        requireControlCredential: Boolean = false,
    ) {
        session.close()
        postToMain {
            if (!isCurrent(epoch)) return@postToMain
            busy = false
            if (requireControlCredential) controlCredentialRequired = true
            lastAction = message
            render()
        }
    }

    private fun openEventStream(epoch: Long) {
        val active = runtime ?: return
        if (active.epoch != epoch) return
        val state = store?.state ?: return
        val streamEpoch = ++eventStreamEpoch
        ioExecutor.execute {
            try {
                val connection = active.session.openEventStream(
                    active.deviceId,
                    state.generation,
                    state.lastSequence,
                    object : GatewayEventObserver {
                        override fun onOpen() = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            eventConnected = true
                            reconnectAttempt = 0
                            lastAction = "事件流已连接"
                            render()
                        }

                        override fun onEvent(event: ConsoleEventEnvelope) = postToMain {
                            if (!isCurrentStream(epoch, streamEpoch)) return@postToMain
                            val disposition = store?.apply(event) ?: return@postToMain
                            lastAction = "事件 ${event.sequence}：$disposition"
                            render()
                            if (disposition == EventDisposition.NEEDS_SNAPSHOT) refreshSnapshot()
                        }

                        override fun onClosed() = postToMain {
                            if (!retireEventStream(epoch, streamEpoch)) return@postToMain
                            lastAction = "事件流已关闭，正在自动恢复"
                            render()
                            scheduleGatewayReconnect(epoch)
                        }

                        override fun onFailure(failure: GatewayCallResult.Failure) = postToMain {
                            if (!retireEventStream(epoch, streamEpoch)) return@postToMain
                            handleGatewayFailure("事件流中断", failure)
                            render()
                            if (failure.retryable) scheduleGatewayReconnect(epoch)
                        }
                    },
                )
                postToMain {
                    if (!isCurrentStream(epoch, streamEpoch)) {
                        connection.close()
                    } else {
                        eventConnection?.close()
                        eventConnection = connection
                    }
                }
            } catch (error: GatewayTransportException) {
                postToMain {
                    if (!retireEventStream(epoch, streamEpoch)) return@postToMain
                    val failure = GatewayCallResult.Failure(error.reason, error.retryable)
                    handleGatewayFailure("事件流失败", failure)
                    render()
                    if (failure.retryable) scheduleGatewayReconnect(epoch)
                }
            } catch (_: Exception) {
                postToMain {
                    if (!retireEventStream(epoch, streamEpoch)) return@postToMain
                    lastAction = "事件流失败：凭据或传输不可用"
                    render()
                }
            }
        }
    }

    private fun refreshSnapshot() {
        val active = runtime ?: return
        if (busy) return
        busy = true
        lastAction = "正在刷新设备快照"
        render()
        ioExecutor.execute {
            val result = active.client.fetchSnapshot(active.deviceId)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        val refreshed = CompanionStore(active.deviceId)
                        if (refreshed.apply(result.value) == EventDisposition.APPLIED) {
                            store = refreshed
                            releaseCatalog = null
                            lastAction = "设备快照已刷新"
                            eventConnection?.close()
                            eventConnection = null
                            eventConnected = false
                            render()
                            openEventStream(active.epoch)
                        } else {
                            lastAction = "刷新失败：快照未通过状态校验"
                            render()
                        }
                    }
                    is GatewayCallResult.Failure -> {
                        handleGatewayFailure("刷新失败", result)
                        render()
                    }
                }
            }
        }
    }

    private fun fetchReleases() {
        val active = runtime ?: return
        val state = store?.state ?: return
        if (busy || state.generation <= 0) return
        busy = true
        lastAction = "正在获取固件发布列表"
        render()
        ioExecutor.execute {
            val result = active.client.fetchFirmwareReleases(active.deviceId, state.generation)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        releaseCatalog = result.value
                        lastAction = "已获取 ${result.value.releases.size} 个已验证发布"
                    }
                    is GatewayCallResult.Failure -> handleGatewayFailure("发布列表失败", result)
                }
                render()
            }
        }
    }

    private fun submitMutation(
        operation: ConsoleOperation,
        arguments: ConsoleMutationArguments = ConsoleMutationArguments.None,
        locallyConfirmed: Boolean = false,
    ) {
        val active = runtime ?: return
        val state = store?.state ?: return
        if (busy) return
        val now = System.currentTimeMillis()
        val mutation = try {
            ConsoleMutation(
                requestId = "android-$now-${requestCounter.incrementAndGet()}",
                deviceId = active.deviceId,
                generation = state.generation,
                expectedRevision = state.revision,
                issuedAtEpochMs = now,
                expiresAtEpochMs = now + MUTATION_TTL_MS,
                operation = operation,
                arguments = arguments,
            )
        } catch (_: IllegalArgumentException) {
            lastAction = "请求未发送：当前设备状态不可用于该操作"
            render()
            return
        }
        val decision = ConsolePolicy.evaluate(
            mutation,
            MutationContext(
                nowEpochMs = now,
                deviceId = active.deviceId,
                claimed = state.claimed,
                gatewayOnline = state.gateway == GatewayConnection.ONLINE,
                generation = state.generation,
                revision = state.revision,
                grantedLevels = setOf(
                    PermissionLevel.L1_PREFERENCE,
                    PermissionLevel.L2_PRIVACY,
                    PermissionLevel.L3_ADMIN,
                ),
                locallyConfirmed = locallyConfirmed,
            ),
        )
        if (decision is MutationDecision.Rejected) {
            lastAction = "请求未发送：${decision.code.wireValue}"
            render()
            return
        }
        busy = true
        lastAction = "正在提交 ${operation.name}"
        render()
        ioExecutor.execute {
            val result = active.client.submitMutation(mutation)
            postToMain {
                if (!isCurrent(active.epoch)) return@postToMain
                busy = false
                when (result) {
                    is GatewayCallResult.Success -> {
                        lastAction = if (result.value.status == MutationReceiptStatus.ACCEPTED) {
                            if (operation == ConsoleOperation.CANCEL_TURN) {
                                "取消已发送；尚未确认板端停止播放"
                            } else if (operation == ConsoleOperation.SET_VOLUME) {
                                "板端已确认运行期音量；重启仍使用已保存的偏好"
                            } else if (operation == ConsoleOperation.CONFIGURE_PERMISSION &&
                                arguments == ConsoleMutationArguments.ConfigurePermission(
                                    PrivacyCapability.LONG_TERM_MEMORY,
                                    PermissionState.DENIED,
                                )
                            ) {
                                "关闭长期记忆已受理，等待傻妞确认并同步状态"
                            } else if (operation == ConsoleOperation.DELETE_MEMORY &&
                                arguments == ConsoleMutationArguments.DeleteMemory(
                                    MemoryDeleteScope.CONVERSATIONS,
                                )
                            ) {
                                "清除对话记忆已受理，等待傻妞确认"
                            } else if (operation == ConsoleOperation.REQUEST_FIRMWARE_UPDATE) {
                                releaseCatalog = null
                                "设备已接受更新请求；等待下载和签名验证状态"
                            } else {
                                "${operation.name} 已受理，等待设备上报确认"
                            }
                        } else {
                            when (result.value.error) {
                                ConsoleErrorCode.VOLUME_TIMEOUT -> "音量确认超时，生效结果未知；请刷新设备状态"
                                ConsoleErrorCode.VOLUME_DEVICE_ERROR -> "板端音量操作失败，当前音量未知；请刷新设备状态"
                                ConsoleErrorCode.VOLUME_BUSY -> "正在处理音量请求，请稍后刷新"
                                ConsoleErrorCode.VOLUME_NOT_SUPPORTED -> "当前固件不支持远程音量控制"
                                ConsoleErrorCode.UPDATE_MANIFEST_INVALID -> "更新请求被拒绝：发布清单无效或不适用于当前设备"
                                ConsoleErrorCode.UPDATE_SIGNATURE_INVALID -> "更新请求被拒绝：签名校验失败"
                                ConsoleErrorCode.UPDATE_PAIR_MISMATCH -> "更新请求被拒绝：CP/AP 固件配对不匹配"
                                ConsoleErrorCode.UPDATE_TRIAL_FAILED -> "新固件试运行失败，设备将恢复旧版本"
                                ConsoleErrorCode.UPDATE_DEVICE_ERROR -> "设备未能启动更新，请刷新设备状态"
                                ConsoleErrorCode.UNSUPPORTED_OPERATION -> when (operation) {
                                    ConsoleOperation.CONFIGURE_PERMISSION -> "当前 Gateway 尚不支持关闭长期记忆"
                                    ConsoleOperation.DELETE_MEMORY -> "当前 Gateway 尚不支持清除对话记忆"
                                    ConsoleOperation.REQUEST_FIRMWARE_UPDATE -> "当前 Gateway 或设备固件不支持受控更新"
                                    else -> "${operation.name} 被拒绝：${result.value.error.wireValue}"
                                }
                                else -> "${operation.name} 被拒绝：${result.value.error?.wireValue ?: "unknown"}"
                            }
                        }
                    }
                    is GatewayCallResult.Failure -> handleGatewayFailure("请求失败", result)
                }
                render()
            }
        }
    }

    private fun confirmFactoryReset() {
        if (provisionedDeviceId.isBlank() || !directSession.current().authenticated || factoryReset != null) return
        val expectedDevice = provisionedDeviceId
        confirm(
            title = "准备交给新主人？",
            message = "这会撤销当前控制权限，并清除傻妞上的网络、云服务、记忆、偏好和显示选择。完成后必须重新扫描设备二维码认领。本机旧控制凭据会保留到设备回执明确确认完成。",
            consent = "我理解需要重新扫码认领和配网",
        ) {
            if (expectedDevice != provisionedDeviceId || !directSession.current().authenticated || factoryReset != null)
                return@confirm
            val deviceId = provisionedDeviceId
            factoryReset = com.shaniu.companion.provision.FactoryResetController(this, directSession, deviceId) { state ->
                if (state.message.isNotBlank()) directMessage = state.message
                if (state.phase == com.shaniu.companion.provision.FactoryResetController.Phase.COMPLETED) {
                    // Only SRR1/physical read-only proof reaches COMPLETED.
                    if (provisionBindingStore.clearBound(deviceId)) {
                        if (factoryReset?.confirmLocalRevocation() == true) {
                            provisionedDeviceId = ""
                            closeDirect()
                        } else directMessage = "设备已确认恢复出厂；本机回执尚未持久更新，请勿清除 App 数据"
                    } else directMessage = "设备已确认恢复出厂；本机旧凭据尚未删除，请重试本机资料清理"
                }
                if (!destroyed) render()
            }
            if (factoryReset?.begin() != true) {
                factoryReset?.close(); factoryReset = null
                directMessage = "无法读取设备当前配置版本；未发送恢复出厂请求"
            }
            render()
        }
    }

    private fun queryFactoryReset() {
        if (provisionedDeviceId.isBlank()) return
        if (factoryReset == null) factoryReset = com.shaniu.companion.provision.FactoryResetController(this, directSession, provisionedDeviceId) { state ->
            if (state.message.isNotBlank()) directMessage = state.message
            if (state.phase == com.shaniu.companion.provision.FactoryResetController.Phase.COMPLETED) {
                val deviceId = provisionedDeviceId
                if (provisionBindingStore.clearBound(deviceId) && factoryReset?.confirmLocalRevocation() == true) {
                    provisionedDeviceId = ""
                    closeDirect()
                } else directMessage = "设备已确认恢复出厂；本机资料清理未完成，已保留回执定位符"
            }
            if (!destroyed) render()
        }
        if (factoryReset?.queryReceipt() != true) directMessage = "无法发送只读回执查询"
        render()
    }

    private fun confirmClearProvisioning() {
        confirm(
            title = "清除本机连接资料",
            message = "将删除这台手机保存的设备连接资料。傻妞上的网络设置和服务凭据不会改变，尚未核对的认领回执会保留。",
        ) { clearProvisioning() }
    }

    private fun clearProvisioning() {
        closeDirect()
        closeRuntime(clearReportedState = true)
        val epoch = connectionEpoch
        busy = true
        lastAction = "正在清除本机绑定资料"
        render()
        ioExecutor.execute {
            val cleared = try {
                // Remove the authoritative binding first. If this fails, keep
                // the other stores intact so the user can retry coherently.
                val durableDeviceId = provisionBindingStore.boundDeviceId()
                if (durableDeviceId != null &&
                    !provisionBindingStore.clearBound(durableDeviceId)) {
                    false
                } else if (!preferences.edit().clear().commit()) {
                    false
                } else {
                    tokenStore.clearAccessToken()
                    true
                }
            } catch (_: Exception) {
                false
            }
            postToMain {
                if (!isCurrent(epoch)) return@postToMain
                busy = false
                if (cleared) {
                    draftOrigin = ""
                    draftDeviceId = ""
                    draftPins = ""
                    provisionedDeviceId = ""
                    controlCredentialRequired = false
                    lastAction = "本机绑定资料已清除"
                } else {
                    reloadLocalConfiguration()
                    lastAction = "清除未完成：已停止自动连接，请重试"
                }
                render()
            }
        }
    }

    private fun closeRuntime(
        clearReportedState: Boolean,
        resetReconnectAttempt: Boolean = true,
    ) {
        cancelGatewayReconnect(resetReconnectAttempt)
        connectionEpoch += 1
        eventStreamEpoch += 1
        eventConnection?.close()
        eventConnection = null
        runtime?.session?.let(::closeSession)
        runtime = null
        eventConnected = false
        busy = false
        releaseCatalog = null
        if (clearReportedState) store = null
    }

    private fun closeSession(session: OkHttpConsoleGatewaySession) {
        // Connection-pool eviction may close a live TLS socket. Android treats
        // that as network I/O, so lifecycle and button callbacks must not run it
        // on the main thread.
        try {
            ioExecutor.execute { session.close() }
        } catch (_: RejectedExecutionException) {
            Thread({ session.close() }, "shaniu-session-close").apply {
                isDaemon = true
                start()
            }
        }
    }

    private fun canMutate(): Boolean =
        runtime != null && eventConnected && !busy && store?.state?.needsSnapshot == false

    private fun requireReportedState(action: String = "查看和调整设置"): CompanionState? {
        val state = store?.state
        if (state == null) {
            val needsControlBinding = expectedControlDeviceId().isNotBlank() &&
                (draftDeviceId.isBlank() || controlCredentialRequired)
            addMuted(if (needsControlBinding) "傻妞已完成网络设置；绑定 App 控制凭据后即可$action。"
                     else if (draftDeviceId.isBlank()) "连接你的傻妞后，就能在这里$action。"
                     else "暂时没有连上傻妞，重新连接后即可$action。")
            actionButton(if (needsControlBinding) "绑定 App 控制" else if (draftDeviceId.isBlank()) "添加傻妞" else "重新连接", !busy) {
                if (needsControlBinding) startConsoleEnrollmentImport()
                else if (draftDeviceId.isBlank()) startProvisioning()
                else connect(draftOrigin, draftDeviceId, draftPins, "")
            }
        }
        return state
    }

    private fun isCurrent(epoch: Long): Boolean = !destroyed && epoch == connectionEpoch

    private fun startProvisioning(developerMode: Boolean = false) {
        // 认领可能更换身份，不能在返回前台时复用旧身份的重连工厂。
        // 保留持久凭据；返回后由用户选择设备，按最新认领结果重新认证。
        closeDirect()
        directSession.releaseIdentity()
        startActivityForResult(
            Intent(this, ProvisionActivity::class.java)
                .putExtra("developer_mode", developerMode),
            PROVISION_REQUEST,
        )
    }

    private fun startConsoleEnrollmentImport() {
        if (busy) return
        startActivityForResult(
            Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                type = "application/json"
                addCategory(Intent.CATEGORY_OPENABLE)
            },
            CONSOLE_ENROLLMENT_REQUEST,
        )
    }

    private fun importConsoleEnrollment(data: Intent?) {
        val expectedDevice = expectedControlDeviceId()
        val uri = data?.data
        if (uri == null) {
            lastAction = "控制凭据导入失败：未选择文件"
            render()
            return
        }

        closeRuntime(clearReportedState = true)
        busy = true
        lastAction = "正在导入并验证 App 控制凭据"
        render()
        val epoch = connectionEpoch
        ioExecutor.execute {
            var bytes: ByteArray? = null
            var input: CharArray? = null
            try {
                bytes = contentResolver.openInputStream(uri)?.use { stream ->
                    val buffer = ByteArray(ConsoleEnrollment.MAX_BYTES + 1)
                    try {
                        var count = 0
                        while (count < buffer.size) {
                            val read = stream.read(buffer, count, buffer.size - count)
                            if (read < 0) break
                            require(read > 0)
                            count += read
                        }
                        require(count in 1..ConsoleEnrollment.MAX_BYTES)
                        buffer.copyOf(count)
                    } finally {
                        buffer.fill(0)
                    }
                } ?: throw IllegalArgumentException()
                val chars = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes))
                val decoded = CharArray(chars.remaining()).also { chars.get(it) }
                input = decoded
                try {
                    ConsoleEnrollment.parse(
                        decoded,
                        expectedDeviceId = expectedDevice.takeIf { it.isNotBlank() },
                    ).use { enrollment ->
                        val origin = enrollment.gatewayOrigin.toString()
                        val deviceId = enrollment.deviceId
                        val pins = enrollment.certificatePins.joinToString("\n")
                        enrollment.useAccessToken { token ->
                            val suppliedToken = token.concatToString()
                            postToMain {
                                if (!isCurrent(epoch)) return@postToMain
                                busy = false
                                connect(origin, deviceId, pins, suppliedToken)
                            }
                        }
                    }
                } finally {
                    if (chars.hasArray()) chars.array().fill('\u0000')
                }
            } catch (_: Exception) {
                postToMain {
                    if (!isCurrent(epoch)) return@postToMain
                    busy = false
                    lastAction = "控制凭据导入失败：文件无效、已过期或不属于这台设备"
                    render()
                }
            } finally {
                input?.fill('\u0000')
                bytes?.fill(0)
            }
        }
    }

    private fun expectedControlDeviceId(): String =
        provisionedDeviceId.ifBlank { draftDeviceId }

    private fun isCurrentStream(epoch: Long, streamEpoch: Long): Boolean =
        isCurrent(epoch) && streamEpoch == eventStreamEpoch

    private fun retireEventStream(epoch: Long, streamEpoch: Long): Boolean {
        if (!isCurrentStream(epoch, streamEpoch)) return false
        eventStreamEpoch += 1
        eventConnection = null
        eventConnected = false
        return true
    }

    private fun cancelGatewayReconnect(resetAttempt: Boolean) {
        reconnectTicket += 1
        if (resetAttempt) reconnectAttempt = 0
    }

    private fun scheduleGatewayReconnect(epoch: Long) {
        if (!isCurrent(epoch) || !foreground || !autoReconnectAllowed ||
            controlCredentialRequired || draftOrigin.isBlank() || draftDeviceId.isBlank()) {
            return
        }
        val attempt = reconnectAttempt.coerceAtMost(RECONNECT_DELAYS_MS.lastIndex)
        val delayMs = RECONNECT_DELAYS_MS[attempt]
        reconnectAttempt += 1
        val ticket = ++reconnectTicket
        val seconds = (delayMs + 999L) / 1_000L
        lastAction = "连接中断，${seconds} 秒后自动恢复"
        render()
        mainHandler.postDelayed(
            { runGatewayReconnect(ticket, epoch) },
            delayMs,
        )
    }

    private fun runGatewayReconnect(ticket: Long, epoch: Long) {
        if (ticket != reconnectTicket || !isCurrent(epoch) || !foreground ||
            !autoReconnectAllowed || controlCredentialRequired) {
            return
        }
        if (busy) {
            mainHandler.postDelayed(
                { runGatewayReconnect(ticket, epoch) },
                RECONNECT_BUSY_WAIT_MS,
            )
            return
        }
        connect(draftOrigin, draftDeviceId, draftPins, "", automatic = true)
    }

    private fun postToMain(block: () -> Unit) {
        if (!destroyed) mainHandler.post { if (!destroyed) block() }
    }

    private fun parsePins(value: String): Set<String> {
        if (value.isBlank()) return emptySet()
        val pins = value.split(Regex("[\\s,]+"))
            .filter { it.isNotBlank() }
            .toSet()
        require(pins.size <= GatewayTransportConfiguration.MAX_CERTIFICATE_PINS)
        return pins
    }

    private fun failureText(prefix: String, failure: GatewayCallResult.Failure): String =
        "$prefix：${failure.reason.label()}${if (failure.retryable) "（可重试）" else ""}"

    private fun handleGatewayFailure(
        prefix: String,
        failure: GatewayCallResult.Failure,
        credentialWasSupplied: Boolean = false,
    ) {
        val credentialUnavailable = failure.reason == GatewayFailureReason.AUTHORIZATION_REVOKED ||
            failure.reason == GatewayFailureReason.CREDENTIALS_UNAVAILABLE
        if (!credentialUnavailable || credentialWasSupplied) {
            lastAction = failureText(prefix, failure)
            return
        }

        closeRuntime(clearReportedState = true)
        controlCredentialRequired = true
        val markerSaved = preferences.edit()
            .putBoolean(KEY_CONTROL_CREDENTIAL_REQUIRED, true)
            .commit()
        val reason = if (failure.reason == GatewayFailureReason.AUTHORIZATION_REVOKED) {
            "授权已失效"
        } else {
            "本机控制凭据不可用"
        }
        lastAction = if (markerSaved) {
            "$reason；请重新导入 App 控制凭据"
        } else {
            "$reason；本机未能保存停用状态，请重新导入控制凭据"
        }
        val epoch = connectionEpoch
        ioExecutor.execute {
            val cleared = try {
                tokenStore.clearAccessToken()
                true
            } catch (_: Exception) {
                false
            }
            if (!cleared) postToMain {
                if (!isCurrent(epoch)) return@postToMain
                lastAction = "$reason；旧凭据已停用，但安全存储清理失败"
                render()
            }
        }
    }

    private fun GatewayFailureReason.label(): String = when (this) {
        GatewayFailureReason.CREDENTIALS_UNAVAILABLE -> "凭据不可用"
        GatewayFailureReason.TRANSPORT_UNAVAILABLE -> "网络不可用"
        GatewayFailureReason.AUTHORIZATION_REVOKED -> "授权已失效"
        GatewayFailureReason.DEVICE_NOT_FOUND -> "设备不存在"
        GatewayFailureReason.REVISION_CONFLICT -> "状态修订冲突"
        GatewayFailureReason.RATE_LIMITED -> "请求过于频繁"
        GatewayFailureReason.SERVER_ERROR -> "Gateway 服务异常"
        GatewayFailureReason.REQUEST_REJECTED -> "请求被拒绝"
        GatewayFailureReason.PROTOCOL_ERROR -> "协议校验失败"
    }

    private fun settingsRow(title: String, subtitle: String, enabled: Boolean = true,
                            selected: Boolean = false, danger: Boolean = false, action: () -> Unit) =
        CompanionPage(this, content).settingsRow(title, subtitle, enabled, selected, danger, action = action)

    private fun primaryButton(label: String, enabled: Boolean, action: () -> Unit) =
        CompanionPage(this, content).primaryButton(label, enabled, action)

    private fun navigationIcon(tab: Int, selected: Boolean = false): android.graphics.drawable.Drawable =
        object : android.graphics.drawable.Drawable() {
            private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
            private val icon = CompanionIcons.drawable(this@MainActivity, when (tab) {
                TAB_OVERVIEW -> "device"
                TAB_PERSONALITY -> "spark"
                TAB_UPDATE -> "download"
                else -> "settings"
            }, if (selected) design.accent else MUTED)
            override fun getIntrinsicWidth() = dp(52)
            override fun getIntrinsicHeight() = dp(32)
            override fun setAlpha(alpha: Int) { paint.alpha = alpha; icon.alpha = alpha }
            override fun setColorFilter(filter: android.graphics.ColorFilter?) { icon.colorFilter = filter }
            @Deprecated("Drawable contract")
            override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
            override fun draw(canvas: android.graphics.Canvas) {
                if (selected) {
                    paint.color = design.selected
                    canvas.drawRoundRect(android.graphics.RectF(bounds), dp(20).toFloat(), dp(20).toFloat(), paint)
                }
                val cx = bounds.centerX(); val cy = bounds.centerY(); val half = dp(12)
                icon.setBounds(cx - half, cy - half, cx + half, cy + half)
                icon.draw(canvas)
            }
        }

    private fun sectionTitle(title: String) = CompanionPage(this, content).sectionTitle(title)

    private fun addCard(title: String, body: String) = CompanionPage(this, content).addCard(title, body)

    private fun addMuted(message: String) = CompanionPage(this, content).addMuted(message)

    private fun editField(
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

    private fun actionButton(label: String, enabled: Boolean = true, action: () -> Unit) {
        content.addView(
            compactButton(label, enabled, action),
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, dp(3), 0, dp(3)) },
        )
    }

    private fun compactButton(label: String, enabled: Boolean, action: () -> Unit): Button =
        Button(this).apply {
            text = label
            isAllCaps = false
            isEnabled = enabled
            gravity = Gravity.CENTER
            textSize = 16f
            setTextColor(if (enabled) INK else MUTED)
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (enabled) design.selected else design.surface)
                cornerRadius = dp(18).toFloat()
            }
            minHeight = dp(54)
            setOnClickListener { action() }
        }

    private fun confirm(title: String, message: String, consent: String? = null, confirmed: () -> Unit) {
        val builder = AlertDialog.Builder(this)
            .setTitle(title)
            .setNegativeButton("取消", null)
            .setPositiveButton("确认") { _, _ -> confirmed() }
        if (consent == null) { builder.setMessage(message).show(); return }
        showCompanionSheet(title, "清除设备上的用户配置，\n并撤销现有手机的控制凭据。", done = false) { body, dialog ->
            val page = CompanionPage(this, body)
            body.addView(TextView(this).apply {
                text = "此操作无法撤销。请先确认设备与清理范围。"; textSize = 14f; setTextColor(design.warning)
                setPadding(dp(16), dp(14), dp(16), dp(14))
                background = design.shape(design.warningSurface, dp(16).toFloat())
            }, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(14) })
            page.informationRow("将清除", "当前归属、Wi-Fi、云服务凭据、记忆、偏好和显示选择")
            page.informationRow("将保留", "当前固件；本机旧凭据保留至设备回执确认完成")
            val acknowledgment = CheckBox(this).apply {
                text = consent; textSize = 14f; minHeight = dp(54); setTextColor(INK)
                buttonTintList = android.content.res.ColorStateList.valueOf(design.accent)
            }
            body.addView(acknowledgment)
            val submit = com.google.android.material.button.MaterialButton(this).apply {
                text = "恢复出厂"; isAllCaps = false; textSize = 16f; minHeight = dp(54)
                cornerRadius = dp(17); isEnabled = false; stateListAnimator = null
                backgroundTintList = android.content.res.ColorStateList.valueOf(design.divider)
                setTextColor(MUTED)
                setOnClickListener { dialog.dismiss(); confirmed() }
            }
            body.addView(submit, LinearLayout.LayoutParams(-1, -2))
            acknowledgment.setOnCheckedChangeListener { _, checked ->
                submit.isEnabled = checked
                submit.backgroundTintList = android.content.res.ColorStateList.valueOf(if (checked) design.danger else design.divider)
                submit.setTextColor(if (checked) Color.WHITE else MUTED)
            }
            body.addView(TextView(this).apply {
                text = "保留我的设置"; textSize = 16f; gravity = Gravity.CENTER
                setTextColor(design.accent); minimumHeight = dp(54); isClickable = true; isFocusable = true
                setOnClickListener { dialog.dismiss() }
            }, LinearLayout.LayoutParams(-1, -2))
        }
    }

    private fun personaLabel(mode: PersonaMode): String = when (mode) {
        PersonaMode.GENTLE -> "温柔"
        PersonaMode.PLAYFUL -> "活泼"
        PersonaMode.QUIET -> "安静"
        PersonaMode.SERIOUS -> "认真"
        PersonaMode.TSUNDERE_LITE -> "轻傲娇"
    }

    private fun turnLabel(phase: TurnPhase): String = when (phase) {
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

    private fun emotionLabel(emotion: Emotion): String = when (emotion) {
        Emotion.UNKNOWN -> "未知"
        Emotion.NEUTRAL -> "平静"
        Emotion.HAPPY -> "开心"
        Emotion.SHY -> "害羞"
        Emotion.SAD -> "难过"
        Emotion.SURPRISED -> "惊讶"
        Emotion.THINKING -> "思考中"
    }

    private fun permissionStateLabel(state: PermissionState): String = when (state) {
        PermissionState.NOT_GRANTED -> "尚未选择"
        PermissionState.ALLOWED -> "已允许"
        PermissionState.DENIED -> "已拒绝"
    }

    private fun updatePhaseLabel(phase: UpdatePhase): String = when (phase) {
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

    private fun updateErrorLabel(error: ConsoleErrorCode): String = when (error) {
        ConsoleErrorCode.UPDATE_MANIFEST_INVALID -> "发布清单无效或不适用于当前设备"
        ConsoleErrorCode.UPDATE_SIGNATURE_INVALID -> "签名校验失败"
        ConsoleErrorCode.UPDATE_PAIR_MISMATCH -> "CP/AP 固件配对不匹配"
        ConsoleErrorCode.UPDATE_TRIAL_FAILED -> "新固件试运行失败"
        ConsoleErrorCode.UPDATE_DEVICE_ERROR -> "设备执行更新失败"
        else -> error.wireValue
    }

    private fun capabilityLabel(capability: PrivacyCapability): String = when (capability) {
        PrivacyCapability.MICROPHONE -> "麦克风"
        PrivacyCapability.CAMERA -> "摄像头"
        PrivacyCapability.LOCATION -> "位置"
        PrivacyCapability.LONG_TERM_MEMORY -> "长期记忆"
        PrivacyCapability.AUTHORIZED_VOICE -> "授权声音"
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).toInt()

    private val design get() = CompanionDesign(this)
    private val BACKGROUND get() = design.background
    private val INK get() = design.ink
    private val MUTED get() = design.muted

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putInt("navigation", currentTab)
        outState.putInt("expression_preview", expressionPreview)
        outState.putBoolean("update_resources", updateResources)
        outState.putInt("resources_back_tab", resourcesBackTab)
        super.onSaveInstanceState(outState)
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        // Theme/window changes must not destroy the foreground package server or
        // reconnect the authenticated device. Existing sensitive drafts stay
        // in their editor until the user closes it.
        theme.applyStyle(R.style.Theme_Shaniu, true)
        window.statusBarColor = BACKGROUND
        window.navigationBarColor = design.surface
        window.decorView.systemUiVisibility = if (design.dark) 0 else
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        val scroll = contentScroll.scrollY
        navigation.clear()
        setContentView(buildRoot().apply { applySystemInsets() })
        render()
        contentScroll.post { contentScroll.scrollTo(0, scroll) }
    }

    companion object {
        private const val PREFERENCES_NAME = "shaniu_gateway_configuration_v1"
        private const val KEY_ORIGIN = "origin"
        private const val KEY_DEVICE_ID = "device_id"
        private const val KEY_PINS = "certificate_pins"
        private const val KEY_PROVISIONED_DEVICE_ID = "provisioned_device_id"
        private const val KEY_CONTROL_CREDENTIAL_REQUIRED = "control_credential_required"
        private const val KEY_OTA_EXPECTED_VERSION = "ota_expected_version"
        private const val KEY_OTA_EXPECTED_COUNTER = "ota_expected_counter"
        private const val KEY_OTA_EXPECTED_CATALOG = "ota_expected_catalog"
        private const val KEY_OTA_EXPECTED_DEVICE = "ota_expected_device"
        private const val KEY_OTA_EXPECTED_PENDING = "ota_expected_pending"
        private const val DEVICE_BOARD = "aidk_ai_toy"
        private const val PROVISION_REQUEST = 41
        private const val CONSOLE_ENROLLMENT_REQUEST = 42
        private const val MUTATION_TTL_MS = 30_000L
        private const val RECONNECT_BUSY_WAIT_MS = 500L
        private val RECONNECT_DELAYS_MS = longArrayOf(1_000L, 2_000L, 4_000L, 8_000L, 16_000L, 30_000L)

        private const val TAB_OVERVIEW = 0
        private const val TAB_INTERACTION = 1
        private const val TAB_PERSONALITY = 2
        private const val TAB_PRIVACY = 3
        private const val TAB_UPDATE = 4
        private const val FIRMWARE_PACKAGE_REQUEST = 6043
        private const val WAKE_MODEL_REQUEST = 6044
        private const val EYE_PACK_REQUEST = 6045
        private const val TAB_SETTINGS = 5
        private const val TAB_SERVICES = 6
        private const val TAB_RESOURCES = 7
        private const val TAB_PERSONA = 8
        private const val TAB_ADVANCED = 9
        private val TABS = listOf(TAB_OVERVIEW to "设备", TAB_PERSONALITY to "定制", TAB_UPDATE to "更新", TAB_SETTINGS to "设置")
    }
}
