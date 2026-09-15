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
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
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
    private var directScanner: DeviceControlScanner? = null
    private var directDialog: AlertDialog? = null
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
    private enum class ConfigFlow { NONE, CAPABILITIES, CLOUD, WAKE }
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
        window.navigationBarColor = Color.WHITE
        window.isStatusBarContrastEnforced = false
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or
            View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        preferences = getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)
        tokenStore = AndroidKeystoreTokenStore(applicationContext)
        provisionBindingStore = ProvisionBindingStore(applicationContext)
        legacyConsoleMode = (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0 &&
            intent.getBooleanExtra("legacy_console", false)
        reloadLocalConfiguration()
        setContentView(buildRoot().apply { applySystemInsets() })
        installSystemBack { navigateBack() }
        directSubscriptions += directSession.observe { state ->
            if (state.generation != directObservedGeneration) {
                directObservedGeneration = state.generation
                directEpoch++
                cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsExpected = null
                cloudModelsOffset = 0; cloudModelsTotal = -1
                cloudModelsReadDeadline = 0; cloudModelsReadTicket++
                cloudModelsCanceling = false
                cloudModelsGeneration = null
                cloudModelsFailedGeneration = null
                configFlow = ConfigFlow.NONE
                configAppendMax = 32; configCapabilitiesGeneration = null
                wakePackage = null; wakePayload = null; wakeExpectedSha = null
                wakeStatusGeneration = null; wakeRestoreRequested = false
                wakeApplied = false; wakeCanceling = false; wakeReadTicket++
                wakeRead = ByteArray(0); wakeReadTotal = -1; wakeOffset = 0
                wakeMessage = "连接恢复后读取实际唤醒词模型；未完成的操作不会重发"
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
            developerPanel -> developerPanel = false
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
        directSession.setForeground(false)
        if (otaUpload?.state == OtaControlUpload.State.WAITING) {
            // Do not leave a half-sent source record waiting on an invisible UI.
            directSession.disconnect(user = false)
        }
        directScanner?.close(); directScanner = null
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
            PROVISION_REQUEST -> {
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
        destroyed = true
        firmwareInspectionEpoch++
        selectedFirmwareFile?.delete()
        selectedFirmwareFile = null
        inspectedFirmware = null
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
        root.addView(
            TextView(this).apply {
                text = "傻妞  /  SHANIU"
                textSize = 18f
                letterSpacing = 0.06f
                typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
                setTextColor(INK)
                setPadding(dp(26), dp(18), dp(26), dp(16))
            },
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ),
        )
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(26), dp(8), dp(26), dp(24))
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
        val tabRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            setBackgroundColor(Color.WHITE)
            setPadding(dp(8), dp(6), dp(8), dp(8))
        }
        TABS.forEach { (tab, title) ->
            tabRow.addView(
                TextView(this).apply {
                    text = title
                    textSize = 12f
                    compoundDrawablePadding = dp(5)
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
                LinearLayout.LayoutParams(0, dp(60), 1f),
            )
        }
        root.addView(
            tabRow,
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT),
        )
        return root
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
            TAB_PRIVACY, TAB_UPDATE -> TAB_SETTINGS
            else -> currentTab
        }
        navigation.forEach { (tab, label) ->
            label.isSelected = tab == selectedTab
            label.setTextColor(if (tab == selectedTab) INK else MUTED)
            label.background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (tab == selectedTab) Color.rgb(232, 240, 233) else Color.TRANSPARENT)
                cornerRadius = dp(18).toFloat()
            }
        }
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
        val preserveOtaSource = preserveAcceptedOtaSource && foreground && !destroyed &&
            otaServer?.running == true && otaVerificationPending &&
            (otaUpload?.state == OtaControlUpload.State.ACCEPTED || otaStatus?.state in 1L..2L)
        directEpoch++
        directScanner?.close(); directScanner = null
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

    private fun selectTab(value: Int) {
        if (currentTab == TAB_UPDATE && value != TAB_UPDATE) {
            firmwareInspectionEpoch++
            firmwareInspectionPending = false
        }
        currentTab = value
        if (value == TAB_SETTINGS) requestCloudModelsRead()
    }

    private fun closeOtaServer(message: String? = null) {
        val server = otaServer ?: return
        otaServer = null
        ioExecutor.execute { server.close() }
        if (message != null) otaMessage = message
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
        if (connection == null || snapshot?.otaSupported != true || directPending || otaUpload != null) return
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
                if (server.metadata.catalogSha256 != pack.catalogSha256 || !persistExpected(server.metadata)) {
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
        if (confirmed) {
            otaMessage = "设备已确认完成升级：$version。"
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
        if (!foreground || provisionedDeviceId.isBlank()) return
        val permissions = if (android.os.Build.VERSION.SDK_INT >= 31)
            arrayOf(android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_CONNECT)
        else arrayOf(android.Manifest.permission.ACCESS_FINE_LOCATION)
        if (permissions.any { checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED }) {
            requestPermissions(permissions, 6042); return
        }
        closeDirect(preserveAcceptedOtaSource = true)
        val epoch = directEpoch
        val found = mutableListOf<android.bluetooth.BluetoothDevice>()
        val labels = android.widget.ArrayAdapter<String>(this, android.R.layout.simple_list_item_1)
        directConnecting = true; directMessage = "正在寻找附近的傻妞…"
        directDialog = AlertDialog.Builder(this).setTitle("选择附近的傻妞")
            .setAdapter(labels) { _, index ->
                if (epoch == directEpoch) connectDirect(found[index], epoch)
            }.setNegativeButton("取消") { _, _ -> closeDirect(); render() }
            .setOnCancelListener { closeDirect(); render() }.show()
        directScanner = DeviceControlScanner(this, { device, name ->
            if (epoch == directEpoch && foreground) {
                found += device
                labels.add("$name · ${device.address.takeLast(5)}")
            }
        }, { failed ->
            if (epoch == directEpoch && foreground) {
                directConnecting = false
                directMessage = if (failed) "无法扫描，请检查蓝牙和附近设备权限。"
                    else if (found.isEmpty()) "未发现设备，请确认傻妞已开机并在附近。" else "请选择附近的设备。"
                if (found.isEmpty()) { directDialog?.dismiss(); directDialog = null }
                render()
            }
        })
        directScanner!!.start()
        render()
    }

    private fun connectDirect(device: android.bluetooth.BluetoothDevice, epoch: Long) {
        if (epoch != directEpoch || !foreground || destroyed) return
        directScanner?.close(); directScanner = null
        directDialog?.dismiss(); directDialog = null
        directConnecting = false
        directMessage = "正在验证并连接傻妞…"
        directSession.connect(AndroidDeviceControlFactory(applicationContext, device, provisionedDeviceId,
            ioExecutor, { action -> mainHandler.post { action() }; Unit }))
    }

    private fun onDirectResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (destroyed) return
        if (command.wire in DeviceControlProtocol.Command.CONFIG_READ.wire..DeviceControlProtocol.Command.CONFIG_CANCEL.wire) {
            when (configFlow) {
                ConfigFlow.CAPABILITIES -> handleConfigCapabilities(snapshot)
                ConfigFlow.WAKE -> handleWakeResult(command, snapshot)
                ConfigFlow.CLOUD -> handleCloudModelsResult(command, snapshot)
                ConfigFlow.NONE -> Unit
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
            currentTab == TAB_SETTINGS && snapshot.publicConfigSupported &&
            configFlow == ConfigFlow.NONE) {
            if (configCapabilitiesGeneration != directSession.current().generation) requestConfigCapabilities()
            else if (cloudModelsGeneration != directSession.current().generation &&
                cloudModelsFailedGeneration != directSession.current().generation) requestCloudModelsRead()
            else if (wakeStatusGeneration != directSession.current().generation) requestWakeStatus()
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
                if (chunk.totalLength != 284) { failWake("模型状态长度无效"); return }
                if (wakeReadTotal < 0) { wakeReadTotal = 284; wakeRead = ByteArray(284) }
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
        directSession.finishConfigTransaction()
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
                        directSession.finishConfigTransaction()
                        directMessage = if (decoded == expected) "云端模型已保存并回读确认" else "设备回读的模型配置未确认保存"
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

    private fun renderDirectCompanion() {
        val bound = provisionedDeviceId.isNotBlank()
        when (currentTab) {
            TAB_OVERVIEW, TAB_INTERACTION -> {
                content.addView(TextView(this).apply {
                    text = if (bound) "今天，也在你身边。" else "嗨，我是傻妞。"
                    textSize = 29f
                    typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
                    setTextColor(INK)
                    setPadding(0, dp(14), 0, dp(8))
                })
                addMuted("把日常，说给我听。")
                // Reserve space for copy, the primary action and persistent navigation.
                // Large accessibility text can still use the enclosing scroll view.
                val portraitHeight = (resources.configuration.screenHeightDp - 520).coerceIn(120, 290)
                content.addView(CompanionPortraitView(this), LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, dp(portraitHeight)).apply { bottomMargin = dp(12) })
                if (bound) {
                    addCard(if (directSession.current().authenticated) "已连接傻妞" else "已保存认领结果", directStatus())
                    if (!directSession.current().authenticated && directSession.current().connection != DeviceControlSession.Connection.CONNECTING && directSession.current().connection != DeviceControlSession.Connection.RECONNECT_WAIT)
                        primaryButton(if (directConnecting) "正在连接…" else "连接我的傻妞", !directConnecting) { scanDirect() }
                    if (directSnapshot?.busy == true && directSnapshot?.memoryPending != true)
                        actionButton("停止这次对话", !directPending) { directRequest(DeviceControlProtocol.Command.CANCEL) }
                    actionButton("使用与设置") { selectTab(TAB_SETTINGS); render() }
                } else {
                    addCard("从第一次对话开始", "连上网络，设置语音服务。\n然后，聊聊今天发生的小事。")
                    primaryButton("添加我的傻妞  →", !busy) { startProvisioning() }
                    content.addView(TextView(this).apply {
                        text = "AI 伴侣 · 由你决定如何陪伴"
                        textSize = 11f; gravity = Gravity.CENTER; setTextColor(MUTED)
                        setPadding(0, dp(16), 0, dp(8))
                    })
                }
                settingsRow("固件更新", "查看设备版本与固件包") {
                    selectTab(TAB_UPDATE); render()
                }
            }
            TAB_PERSONALITY -> {
                sectionTitle("今天，想怎样陪你？")
                addMuted("傻妞是 AI 伴侣，声音由模型合成。")
                addCard("每一种心情，都值得被听见", "选择聊天的语气，让陪伴更合心意。")
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
                sectionTitle("隐私与权限")
                addCard("云端语音", "开始对话后，设备采集的语音会发送到你配置的服务，用于识别、回答和合成声音。")
                addCard("服务凭据", "由手机配置到设备。App 不提供密钥明文回读。")
                addCard("对话记忆", "默认只保留本次开机的近期上下文。开启跨重启记忆后，设备会加密保存最近三轮对话；关闭会停止读取和保存，已保存内容可单独删除。")
                if (directSession.current().authenticated && directSnapshot != null) {
                    val memory = directSnapshot!!
                    val canManage = directSession.current().snapshotFresh && !directPending && memoryResultMessage == null && memory.ready && !memory.busy && memory.memoryEnabled != null
                    settingsRow("跨重启记忆", when {
                        !memory.memorySupported -> "设备固件尚未提供此功能"
                        memory.memoryPending -> "正在处理，请稍候"
                        memory.memoryFailed -> "上次操作未确认，请核对设备状态"
                        memory.memoryEnabled == true -> "已开启 · 加密保存最近三轮对话"
                        memory.memoryEnabled == false -> "已关闭 · 不读取或新增保存"
                        else -> "正在读取设备设置"
                    }, enabled = canManage) {
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
                    settingsRow("删除已保存的记忆", "清除本地记忆和近期上下文，并关闭跨重启记忆", enabled = canManage) {
                        confirm("删除设备记忆", "此操作无法撤销：设备将使旧的加密记忆失效，清空近期上下文，并关闭跨重启记忆。云端服务保留的记录不在此范围内。") {
                            memoryResultMessage = "设备记忆已删除，跨重启记忆已关闭"
                            memoryDesiredEnabled = false
                            memoryRequestAccepted = false
                            if (!directRequest(DeviceControlProtocol.Command.MEMORY_DELETE)) {
                                memoryResultMessage = null; memoryDesiredEnabled = null
                            }
                        }
                    }
                    settingsRow("清空近期对话", if (directSnapshot?.busy == true)
                        "请等待当前对话结束" else "让下一次聊天从新的话题开始",
                        enabled = directSession.current().snapshotFresh && !directPending && directSnapshot?.ready == true && directSnapshot?.busy == false) {
                        confirm("清空近期对话", "清除设备用于继续聊天的近期上下文。云端服务可能保留的记录不在此清除范围内，人物与配网设置会保留。") {
                            directRequest(DeviceControlProtocol.Command.CLEAR_HISTORY)
                        }
                    }
                } else if (bound) primaryButton("连接设备以管理记忆", !directConnecting) { scanDirect() }
                else primaryButton("先添加我的傻妞", !busy) { startProvisioning() }
            }
            TAB_UPDATE -> {
                sectionTitle("固件更新")
                val info = directFirmwareInfo
                addCard("设备当前版本", info?.let { "${it.major}.${it.minor}.${it.revision}（构建 ${it.build}，安全计数 ${it.securityCounter}）" }
                    ?: if (directConnection == null) "连接设备后读取" else "版本不可用")
                inspectedFirmware?.let { pack ->
                    addCard("所选固件包", "版本 ${pack.version}\n设备 ${pack.board}\n镜像共 ${pack.ap.size + pack.cp.size} 字节")
                    addCard("升级说明", "此固件包未提供升级说明。")
                    addMuted("文件完整性检查通过。设备兼容性与签名仍需由连接的设备确认。")
                }
                firmwareInspectionMessage?.let { addMuted(it) }
                primaryButton(if (firmwareInspectionPending) "正在检查固件包…" else "选择固件包", !firmwareInspectionPending) {
                    startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                        type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
                        putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI,
                            android.provider.DocumentsContract.buildDocumentUri(
                                "com.android.externalstorage.documents", "primary:Download"))
                    }, FIRMWARE_PACKAGE_REQUEST)
                }
                val localState = directSnapshot
                val ota = otaStatus
                if (ota != null) {
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
                        3L -> if (expectedOtaConfirmed()) "已确认完成升级" else "已结束，等待版本核对"
                        else -> "状态未知"
                    }
                    val phase = when (ota.phase) {
                        1L -> "正在下载"
                        2L -> "正在校验"
                        3L -> "已写入，等待重启"
                        4L -> "正在重启"
                        5L -> "试运行中"
                        6L -> "已确认完成"
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
                    val body = if (ota.state == 0L && ota.result == 0)
                        "当前没有进行中的升级任务。" else "$label\n阶段：$phase\n" +
                        "进度：${percent?.let { "$it%" } ?: "未知"}\n结果：$result"
                    addCard(if (otaCurrent) "设备升级状态" else "设备升级状态（上次读取）",
                        body + otaStaleReason)
                }
                if (otaMessage.isNotBlank()) addMuted(otaMessage)
                otaStatusReadError?.let(::addMuted)
                val canStart = inspectedFirmware != null && selectedFirmwareFile != null &&
                    localState?.otaSupported == true && directConnection != null &&
                    otaUpload == null && !directPending
                primaryButton("从手机开始升级", canStart) { startLocalOta() }
                if (otaUpload?.state == OtaControlUpload.State.WAITING ||
                    otaUpload?.state == OtaControlUpload.State.ACCEPTED || ota?.state in 1L..2L) {
                    actionButton("取消升级", !directPending) { cancelLocalOta() }
                }
                if (localState?.otaSupported != true && directConnection != null)
                    addMuted("设备固件未声明本地升级能力，不能开始升级。")
                if (directConnection == null && provisionedDeviceId.isNotBlank())
                    primaryButton(if (directConnecting) "正在连接…" else "连接设备读取版本", !directConnecting) { scanDirect() }
            }
            else -> {
                sectionTitle("我的傻妞")
                addMuted("把陪伴，调成你喜欢的样子。")
                addCard(if (bound) "我的设备" else "还没有添加傻妞",
                    if (bound) directStatus() else "先连接你的设备，再设置声音和聊天风格。")
                if (!bound) primaryButton("添加我的傻妞", !busy) { startProvisioning() }
                else {
                    if (!directSession.current().authenticated && directSession.current().connection != DeviceControlSession.Connection.CONNECTING && directSession.current().connection != DeviceControlSession.Connection.RECONNECT_WAIT)
                        primaryButton(if (directConnecting) "正在连接…" else "连接我的傻妞", !directConnecting) { scanDirect() }
                    val volumeControl = DeviceControlPresentation.volume(directSession.current())
                    settingsRow("扬声器音量", volumeControl.reason, enabled = volumeControl.enabled) { editDirectVolume() }
                    settingsRow("聊天风格", directSnapshot?.persona?.let { directPersonas[it] }
                        ?: "选择你喜欢的陪伴方式") { selectTab(TAB_PERSONALITY); render() }
                    val configSupported = directSnapshot?.publicConfigSupported == true
                    val configMutationReady = configAvailable() && pendingWakeImport == null
                    val modelsCurrent = cloudModelsGeneration == directSession.current().generation
                    val modelText = when {
                        !directSession.current().authenticated -> "连接并验证设备后读取模型"
                        !directSession.current().snapshotFresh -> "正在读取设备能力…"
                        !configSupported -> "设备固件未提供公开模型设置"
                        cloudModelsWire != null -> "正在读取或保存模型配置…"
                        !modelsCurrent -> cloudModelsReadError ?: "正在读取设备模型配置…"
                        cloudModels != null -> "ASR ${cloudModels!!.asr}\n对话 ${cloudModels!!.chat}\nTTS ${cloudModels!!.tts}"
                        else -> cloudModelsReadError ?: "尚未读取模型配置"
                    }
                    settingsRow("云端模型", modelText, enabled = configMutationReady) {
                        if (modelsCurrent) editCloudModels()
                        else { requestCloudModelsRead(); render() }
                    }
                    if (cloudModelsExpected != null) settingsRow("取消模型保存", "停止当前配置事务；不会重放未完成写入", enabled = true) {
                        if (!directSession.cancelConfigTransaction()) directMessage = "当前模型配置已结束"
                        else directMessage = "正在取消模型配置"
                        render()
                    }
                    settingsRow("导入唤醒词模型", wakeMessage ?: "从本地导入已训练的唤醒词模型",
                        enabled = configMutationReady) {
                        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                            type = "application/octet-stream"; addCategory(Intent.CATEGORY_OPENABLE)
                        }, WAKE_MODEL_REQUEST)
                    }
                    val bundled = listOf("nihao_openvela", "nihao_bingbing", "nihao_shaniu")
                    val wakeNames = listOf("你好，open-vela", "你好冰冰", "你好傻妞")
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
                    settingsRow("恢复上一唤醒词模型", currentWake?.previous?.let { "恢复为 ${wakeModelSummary(it)}" }
                        ?: "恢复前先读取设备保存的上一模型", enabled = configMutationReady) { restoreWakeModel() }
                    settingsRow("设备配置", "配网与添加结果核对", !busy) { startProvisioning() }
                }
                sectionTitle("隐私与管理")
                settingsRow("隐私与权限", "了解语音、凭据与记忆的使用") { selectTab(TAB_PRIVACY); render() }
                settingsRow("固件更新", "查看设备当前版本与升级状态") { selectTab(TAB_UPDATE); render() }
                if (directConnection != null)
                    settingsRow("断开手机连接", "设备的独立对话不受此操作影响") { closeDirect(); render() }
                if (bound) settingsRow("移除本机连接资料", "保留设备上的网络与服务配置",
                    enabled = !busy && !directPending) { confirmClearProvisioning() }
                if ((applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
                    content.addView(TextView(this).apply {
                        text = "开发版本 · 历史服务联调"
                        textSize = 12f; setTextColor(MUTED); gravity = Gravity.CENTER
                        minHeight = dp(48); isClickable = true; isFocusable = true
                        setOnClickListener {
                            startActivity(Intent(this@MainActivity, MainActivity::class.java)
                                .putExtra("legacy_console", true))
                        }
                    })
                }
            }
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
        val current = epoch == firmwareInspectionEpoch && !destroyed && foreground && currentTab == TAB_UPDATE
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
        if ((applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0) {
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
        // The provisioning transaction uses the same GATT service. Release
        // daily control before handing ownership to that Activity.
        directSession.disconnect(user = false)
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
                            selected: Boolean = false, action: () -> Unit) {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(18), dp(16), dp(18), dp(16))
            minimumHeight = dp(72)
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (selected) Color.rgb(230, 239, 231) else Color.WHITE)
                cornerRadius = dp(18).toFloat()
            }
            isEnabled = enabled; isClickable = enabled; isFocusable = enabled
            contentDescription = "$title，$subtitle" + if (selected) "，当前已选择" else ""
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_YES
            if (enabled) setOnClickListener { action() }
        }
        val labels = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
            addView(TextView(context).apply {
                text = title; textSize = 16f; setTextColor(if (enabled) INK else MUTED)
                typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
            })
            addView(TextView(context).apply {
                text = subtitle; textSize = 12f; setTextColor(MUTED)
                setPadding(0, dp(5), 0, 0)
            })
        }
        row.addView(labels, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        row.addView(TextView(this).apply {
            text = if (selected) "✓" else if (enabled) "›" else ""
            textSize = 22f; setTextColor(MUTED); setPadding(dp(12), 0, 0, 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        })
        content.addView(row, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(6); bottomMargin = dp(2) })
    }

    private fun primaryButton(label: String, enabled: Boolean, action: () -> Unit) {
        content.addView(Button(this).apply {
            text = label; isAllCaps = false; textSize = 16f
            typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
            isEnabled = enabled
            setTextColor(Color.WHITE)
            stateListAnimator = null
            background = android.graphics.drawable.GradientDrawable().apply {
                setColor(if (enabled) INK else MUTED); cornerRadius = dp(20).toFloat()
            }
            setOnClickListener { action() }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(56)).apply {
            topMargin = dp(8)
        })
    }

    private fun navigationIcon(tab: Int): android.graphics.drawable.Drawable =
        object : android.graphics.drawable.Drawable() {
            private val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
                color = INK; style = android.graphics.Paint.Style.STROKE
                strokeWidth = 1.7f; strokeCap = android.graphics.Paint.Cap.ROUND
                strokeJoin = android.graphics.Paint.Join.ROUND
            }
            override fun getIntrinsicWidth() = dp(23)
            override fun getIntrinsicHeight() = dp(23)
            override fun setAlpha(alpha: Int) { paint.alpha = alpha; invalidateSelf() }
            override fun setColorFilter(filter: android.graphics.ColorFilter?) { paint.colorFilter = filter }
            @Deprecated("Drawable contract")
            override fun getOpacity() = android.graphics.PixelFormat.TRANSLUCENT
            override fun draw(canvas: android.graphics.Canvas) {
                canvas.save()
                canvas.translate(bounds.left.toFloat(), bounds.top.toFloat())
                canvas.scale(bounds.width() / 24f, bounds.height() / 24f)
                when (tab) {
                    TAB_OVERVIEW -> {
                        canvas.drawRoundRect(3f, 4f, 21f, 20f, 6f, 6f, paint)
                        canvas.drawLine(8f, 10f, 8f, 13f, paint)
                        canvas.drawLine(16f, 10f, 16f, 13f, paint)
                    }
                    TAB_PERSONALITY -> {
                        val path = android.graphics.Path().apply {
                            moveTo(12f, 20f)
                            cubicTo(-7f, 8f, 7f, -2f, 12f, 7f)
                            cubicTo(17f, -2f, 31f, 8f, 12f, 20f)
                            close()
                        }
                        canvas.drawPath(path, paint)
                    }
                    else -> {
                        canvas.drawCircle(12f, 12f, 7f, paint)
                        canvas.drawCircle(12f, 12f, 2.5f, paint)
                        for (i in 0 until 8) {
                            canvas.drawLine(12f, 2f, 12f, 5f, paint)
                            canvas.rotate(45f, 12f, 12f)
                        }
                    }
                }
                canvas.restore()
            }
        }

    private fun sectionTitle(title: String) {
        content.addView(
            TextView(this).apply {
                text = title
                textSize = 23f
                typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
                setTextColor(INK)
                setPadding(0, dp(18), 0, dp(8))
            },
        )
    }

    private fun addCard(title: String, body: String) {
        content.addView(
            LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                background = android.graphics.drawable.GradientDrawable().apply {
                    setColor(Color.WHITE); cornerRadius = dp(20).toFloat()
                }
                setPadding(dp(20), dp(18), dp(20), dp(18))
                addView(TextView(context).apply {
                    text = title
                    textSize = 17f
                    typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
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

    private fun addMuted(message: String) {
        content.addView(TextView(this).apply {
            text = message
            textSize = 14f
            setTextColor(MUTED)
            setPadding(0, dp(4), 0, dp(12))
        })
    }

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
                setColor(if (enabled) Color.rgb(222, 235, 229) else Color.rgb(235, 237, 233))
                cornerRadius = dp(18).toFloat()
            }
            minHeight = dp(54)
            setOnClickListener { action() }
        }

    private fun confirm(title: String, message: String, confirmed: () -> Unit) {
        AlertDialog.Builder(this)
            .setTitle(title)
            .setMessage(message)
            .setNegativeButton("取消", null)
            .setPositiveButton("确认") { _, _ -> confirmed() }
            .show()
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
        private const val TAB_SETTINGS = 5
        private val TABS = listOf(TAB_OVERVIEW to "陪伴", TAB_PERSONALITY to "心情", TAB_SETTINGS to "设置")

        private val BACKGROUND = Color.rgb(248, 248, 243)
        private val INK = Color.rgb(35, 57, 50)
        private val MUTED = Color.rgb(113, 126, 119)
    }
}
