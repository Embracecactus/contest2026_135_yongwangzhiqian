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
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import java.nio.ByteBuffer
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.security.SecureRandom
import com.shaniu.companion.provision.AndroidDeviceControlFactory
import com.shaniu.companion.provision.DeviceControlSession
import com.shaniu.companion.provision.ImportedFileOwner
import com.shaniu.companion.provision.DeviceControlProtocol
import com.shaniu.companion.provision.ResponseLengthPreference
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
import com.shaniu.companion.protocol.FirmwareReleaseCatalog
import com.shaniu.companion.protocol.GatewayConnection
import com.shaniu.companion.protocol.MutationContext
import com.shaniu.companion.protocol.MutationDecision
import com.shaniu.companion.protocol.MutationReceiptStatus
import com.shaniu.companion.protocol.MemoryDeleteScope
import com.shaniu.companion.protocol.PermissionLevel
import com.shaniu.companion.protocol.PermissionState
import com.shaniu.companion.protocol.PrivacyCapability
import com.shaniu.companion.provision.ProvisionActivity
import com.shaniu.companion.provision.ProvisionBindingStore
import com.shaniu.companion.provision.ProvisionBootstrap
import com.shaniu.companion.ota.BkpackInspector
import com.shaniu.companion.ota.OtaControlUpload
import com.shaniu.companion.ota.OtaPackageServer
import com.shaniu.companion.ota.OtaPackageServerException
import com.shaniu.companion.ota.OtaPackageServerStage
import com.shaniu.companion.ota.OtaSourceLease
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
    internal data class GatewayRuntime(
        val epoch: Long,
        val deviceId: String,
        val session: OkHttpConsoleGatewaySession,
        val client: ConsoleGatewayClient,
    )

    internal lateinit var preferences: SharedPreferences
    private lateinit var tokenStore: AndroidKeystoreTokenStore
    private lateinit var provisionBindingStore: ProvisionBindingStore
    internal lateinit var content: LinearLayout
    private lateinit var contentScroll: ScrollView
    private lateinit var statusBanner: TextView
    /** +1 drill-in, -1 back out, 0 sibling tab: chooses the page transition. */
    private var navDirection = 0
    internal lateinit var discoveryHost: android.widget.FrameLayout
    private lateinit var cloudEditorHost: LinearLayout
    private var cloudEditorOpening = false
    private var cloudEditorEmbedded = false
    internal lateinit var pageRoot: LinearLayout
    private var renderedTab: Int? = null
    private var renderTicket = 0L
    private var touchActive = false
    private var renderDeferred = false

    private val ioExecutor = Executors.newSingleThreadExecutor()
    internal val mainHandler = Handler(Looper.getMainLooper())
    // A worker owns imported files until the main-thread completion callback
    // explicitly transfers them to the selected-file fields.
    private val importedFileOwner = ImportedFileOwner()
    private val requestCounter = AtomicLong()
    internal var inspectedFirmware: BkpackInspector.Metadata? = null
    internal var selectedFirmwareFile: java.io.File? = null
    internal var firmwareInspectionPending = false
    internal var firmwareInspectionMessage: String? = null
    private var firmwareInspectionEpoch = 0L
    private var otaSource: OtaSourceLease<OtaPackageServer>? = null
    private val otaServer get() = otaSource?.current()
    internal var otaUpload: OtaControlUpload? = null
    internal var otaStatus: DeviceControlProtocol.OtaStatus? = null
    internal var otaStatusGeneration: Long? = null
    internal var otaMessage = ""
    internal var otaStatusReadError: String? = null
    private var otaVerificationPending = false
    private val otaStartGate = OtaStartGate()

    internal var runtime: GatewayRuntime? = null
    private var eventConnection: GatewayEventConnection? = null
    internal var store: CompanionStore? = null
    internal var releaseCatalog: FirmwareReleaseCatalog? = null
    private var connectionEpoch = 0L
    private var eventStreamEpoch = 0L
    internal var currentTab = TAB_OVERVIEW
    internal var expressionPreview = 0
    internal var updateResources = false
    // Detail pages remember where they were opened from, so Back and the
    // highlighted tab follow the user's path rather than a fixed parent.
    private val detailStack = ArrayList<Int>()
    private var renderedNavTab: Int? = null
    internal var companionSheet: android.app.Dialog? = null
    internal var refreshCompanionSheet: (() -> Unit)? = null
    internal var busy = false
    internal var eventConnected = false
    private var foreground = false
    internal var autoReconnectAllowed = true
    private var reconnectAttempt = 0
    private var reconnectTicket = 0L
    internal var destroyed = false
    internal var developerPanel = false
    private var legacyConsoleMode = false
    // Reflected by the UI acceptance probe as getDirectSession(); keep the JVM name.
    @get:JvmName("getDirectSession")
    internal val directSession by lazy {
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
    internal val directConnection: DeviceControlSession?
        get() = directSession.takeIf { it.current().authenticated }
    private val directSubscriptions = mutableListOf<DeviceControlSession.Cancel>()
    private var directObservedGeneration = 0L
    private var directServiceWasReady = false
    internal var directScanner: DeviceControlScanner? = null
    private var directDialog: AlertDialog? = null
    internal data class DirectCandidate(
        val device: android.bluetooth.BluetoothDevice,
        val name: String,
        val epoch: Long,
    )
    // Scan records are selection hints only. A candidate is never a claimed or
    // authenticated device until the existing control session verifies it.
    internal val directCandidates = mutableListOf<DirectCandidate>()
    internal var directScanFinished = false
    internal var directDiscoveryDismissed = false
    internal var directDiscoveryVisible = false
    internal val directSnapshot get() = directSession.current().snapshot
    internal val directFirmwareInfo get() = directSession.current().firmwareInfo
    private var directEpoch = 0L
    internal val directPending get() = directSession.current().writePending
    internal var memoryResultMessage: String? = null
    internal var memoryDesiredEnabled: Boolean? = null
    internal var memoryRequestAccepted = false
    internal var directConnecting = false
    internal var directMessage = "尚未连接设备"
    internal data class CloudModels(val asr: String, val chat: String, val tts: String)
    private var cloudModels: CloudModels? = null
    private var cloudModelsGeneration: Long? = null
    private var cloudModelsFailedGeneration: Long? = null
    private var cloudModelsReadError: String? = null
    private var cloudModelsWire: ByteArray? = null
    private var cloudModelsOffset = 0
    private var cloudModelsTotal = -1
    internal var cloudModelsExpected: CloudModels? = null
    private var cloudModelsReadDeadline = 0L
    private var cloudModelsReadTicket = 0L
    internal var cloudModelsCanceling = false
    internal var responseMode: Int? = null
    internal var responseModeGeneration: Long? = null
    private var responseModeFailedGeneration: Long? = null
    private var responseModeExpected: Int? = null
    internal var responseModeError: String? = null
    private var responseModeCanceling = false
    internal var responseLength: Int? = null
    internal var responseLengthApplied: Int? = null
    private var responseLengthRevision: Long? = null
    internal var responseLengthGeneration: Long? = null
    private var responseLengthExpected: Int? = null
    internal var responseLengthError: String? = null
    private var responseLengthCanceling = false
    private var responseLengthWire = ByteArray(0)
    private var responseLengthOffset = 0
    private var responseLengthVerifyHeader = false
    private var responseLengthTransaction: ByteArray? = null
    internal var wakeSensitivityPercent: Int? = null
    internal var wakeSensitivityGeneration: Long? = null
    private var wakeSensitivityFailedGeneration: Long? = null
    private var wakeSensitivityExpected: Int? = null
    internal var wakeSensitivityError: String? = null
    private var wakeSensitivityCanceling = false
    internal enum class ConfigFlow { SETTINGS, NONE, CAPABILITIES, CLOUD, WAKE, RESPONSE, RESPONSE_LENGTH, SENSITIVITY, EYES }
    private var trialSecondsDraft = ""
    private var trialExpressionDraft = 0
    private var trialPackDraft = false
    private var defaultEditor: com.shaniu.companion.provision.DefaultSelectionController? = null
    private var trialEditor: com.shaniu.companion.provision.ExpressionTrialController? = null
    private var nfcMinutesDraft = "25"
    private var nfcSlotDraft = 0
    private var nfcActionDraft = 0
    private var nfcDraftCapture: (() -> Unit)? = null
    private var pcEditor: com.shaniu.companion.provision.PcAuthorizationController? = null
    private var pcConfirmation: android.app.AlertDialog? = null
    private var pcRequestSelected: ((android.net.Uri) -> Unit)? = null
    private var pcResponseSelected: ((android.net.Uri) -> Unit)? = null
    private var pcReceiptDevice = ""
    private var pcReceiptTransaction: String? = null
    private var pcReceiptTarget: com.shaniu.companion.provision.PcAuthorizationController.Target? = null
    private var nfcEditor: com.shaniu.companion.provision.NfcBindingController? = null
    private var focusMinutesDraft = "25"
    private var focusEditor: com.shaniu.companion.provision.FocusTimerController? = null
    private var agentEditor: com.shaniu.companion.provision.AgentDataController? = null
    private var approvalEditor: com.shaniu.companion.provision.PcApprovalController? = null
    private var agentImportSelected: ((android.net.Uri) -> Unit)? = null
    private var factDialog: AlertDialog? = null
    internal var settingsEditor: com.shaniu.companion.provision.DeviceSettingsEditor? = null
    internal var factoryReset: com.shaniu.companion.provision.FactoryResetController? = null
    internal var configFlow = ConfigFlow.NONE
    private var configAppendMax = 32
    private var configCapabilitiesGeneration: Long? = null
    private var wakePackage: WakeModelPackage? = null
    internal var wakePayload: ByteArray? = null
    private var wakeExpectedSha: ByteArray? = null
    internal var wakeStatus: WakeModelPackage.Companion.Status? = null
    internal var wakeStatusGeneration: Long? = null
    private var wakeRestoreRequested = false
    internal var wakeApplied = false
    internal var wakeCanceling = false
    private var wakeReadTicket = 0L
    private var wakeOffset = 0
    private var wakeRead = ByteArray(0)
    private var wakeReadTotal = -1
    private var wakeDeadline = 0L
    internal var wakeMessage: String? = null
    internal var pendingWakeImport: WakeModelPackage? = null
    private var wakeImportDeviceId = ""
    private var wakeImportDeadline = 0L
    private var wakeImportTicket = 0L
    // Bounded local draft only. Never retain an install transaction/server.
    private var eyeRestoreDraft: Pair<ByteArray, ByteArray>? = null
    internal var selectedEyePack: EyePack? = null
    private var selectedEyeFile: java.io.File? = null
    private var selectedEyeAssetSha256: ByteArray? = null
    internal var eyeMessage: String? = null
    internal var eyeImportPending = false
    private var eyeSource: OtaSourceLease<OtaPackageServer>? = null
    private var eyeRecord: ByteArray? = null
    private var eyeOffset = 0
    private var eyeRead = ByteArray(0)
    private var eyeReadingOnly = false
    private var eyeCanceling = false
    private var eyeApplySent = false
    private var eyeReadDeferred = false
    private val navigation = mutableMapOf<Int, TextView>()
    internal var lastAction = "请配置 HTTPS Gateway、设备 ID 和访问令牌"

    internal var draftOrigin = ""
    internal var draftDeviceId = ""
    internal var draftPins = ""
    internal var provisionedDeviceId = ""
    internal var controlCredentialRequired = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = BACKGROUND
        window.navigationBarColor = BACKGROUND
        window.isStatusBarContrastEnforced = false
        window.decorView.systemUiVisibility = if (design.dark) 0 else
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        currentTab = savedInstanceState?.getInt("navigation", TAB_OVERVIEW) ?: TAB_OVERVIEW
        trialSecondsDraft = savedInstanceState?.getString("trial_seconds_draft") ?: ""
        trialExpressionDraft = (savedInstanceState?.getInt("trial_expression_draft", 0) ?: 0).coerceIn(0, 8)
        trialPackDraft = savedInstanceState?.getBoolean("trial_pack_draft", false) ?: false
        focusMinutesDraft = savedInstanceState?.getString("focus_minutes_draft") ?: "25"
        pcReceiptDevice = savedInstanceState?.getString("pc_receipt_device").orEmpty()
        pcReceiptTransaction = savedInstanceState?.getString("pc_receipt_transaction")
        pcReceiptTarget = savedInstanceState?.let { saved ->
            val client = saved.getString("pc_receipt_client")
            val revision = saved.getString("pc_receipt_revision")?.toULongOrNull()
            if (client != null && revision != null)
                com.shaniu.companion.provision.PcAuthorizationController.Target(client, saved.getInt("pc_receipt_caps", -1), revision)
            else null
        }
        nfcMinutesDraft = savedInstanceState?.getString("nfc_minutes_draft") ?: "25"
        nfcSlotDraft = (savedInstanceState?.getInt("nfc_slot_draft", 0) ?: 0).coerceIn(0, 7)
        nfcActionDraft = (savedInstanceState?.getInt("nfc_action_draft", 0) ?: 0).coerceIn(0, 6)
        expressionPreview = savedInstanceState?.getInt("expression_preview", 0) ?: 0
        updateResources = savedInstanceState?.getBoolean("update_resources", false) ?: false
        savedInstanceState?.getIntegerArrayList("detail_stack")?.let { saved ->
            detailStack.clear(); detailStack.addAll(saved.filter { it in TOP_LEVEL_TABS || it in DETAIL_TABS })
        }
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
                responseLengthGeneration = null; responseLengthExpected = null; responseLengthCanceling = false
                responseLengthWire = ByteArray(0); responseLengthOffset = 0
                responseLengthError = "连接恢复后读取设备实际回答长度；未完成的设置不会重发"
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
                val accepted = otaVerificationPending && otaSource?.accepted == true
                otaUpload?.close(); otaUpload = null
                if (otaSource != null && (!accepted || !foreground || destroyed)) {
                    otaStatus = null; otaStatusGeneration = null; otaVerificationPending = false
                    otaStartGate.release()
                    setOtaKeepAwake(false)
                    closeOtaServer("控制会话已结束；返回后读取设备实际升级状态，未完成的请求不会重发。")
                }
            }
            if (state.authenticated) directConnecting = false
            if (foreground && !destroyed) {
                otaStartGate.resume(state.readPending)
                render()
            }
        }
        directSubscriptions += directSession.observeResults(::onDirectResult)
        restoreEyeDraft(savedInstanceState)
        render()
    }

    fun navigateBack() {
        when {
            directDiscoveryVisible -> dismissDirectDiscovery()
            developerPanel -> developerPanel = false
            currentTab in DETAIL_TABS -> {
                val previous = detailStack.removeLastOrNull() ?: defaultParent(currentTab)
                // Popping must not push the page being left back onto the stack.
                selectTab(previous, push = false)
            }
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
        if (configFlow == ConfigFlow.EYES && eyeReadDeferred) {
            // Resume only an unsent read, preserving completed chunks. An
            // APPLY/read already in flight keeps its own completion callback.
            requestEyeRead(eyeOffset)
        }
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
        leaveEyeInstallForBackground()
        if (otaSource?.accepted == false && otaUpload == null) {
            otaStartGate.release()
            setOtaKeepAwake(false)
            closeOtaServer("已离开前台；未向设备发送升级请求。")
        }
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
            PC_PAIR_REQUEST -> data?.data?.let { pcRequestSelected?.invoke(it) }
            PC_PAIR_RESPONSE -> data?.data?.let { pcResponseSelected?.invoke(it) }
            AGENT_IMPORT_REQUEST -> data?.data?.let { agentImportSelected?.invoke(it) }
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
        importedFileOwner.close()
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
        // Material navigation matched to width: bottom bar on compact phones,
        // a navigation rail on expanded widths (tablets, landscape foldables).
        val rail = resources.configuration.screenWidthDp >= RAIL_MIN_WIDTH_DP
        val root = LinearLayout(this).apply {
            orientation = if (rail) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
            setBackgroundColor(BACKGROUND)
        }
        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(24), dp(8), dp(24), dp(32))
        }
        // Readable measure on wide windows: centre a phone-width column.
        val column = object : android.widget.FrameLayout(this) {
            override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
                val available = View.MeasureSpec.getSize(widthMeasureSpec)
                val limit = dp(MAX_CONTENT_WIDTH_DP)
                val horizontal = ((available - limit) / 2).coerceAtLeast(0)
                content.setPadding(dp(24) + horizontal, content.paddingTop, dp(24) + horizontal, content.paddingBottom)
                super.onMeasure(widthMeasureSpec, heightMeasureSpec)
            }
        }.apply { addView(content, android.widget.FrameLayout.LayoutParams(-1, -2)) }
        contentScroll = ScrollView(this).apply {
            addView(column); isVerticalScrollBarEnabled = false
            overScrollMode = View.OVER_SCROLL_IF_CONTENT_SCROLLS
        }
        val pages = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        // Outcome and failure messages sit above the scrolling page so they are
        // seen where the user is, never left below the fold.
        statusBanner = TextView(this).apply {
            design.style(this, CompanionDesign.Type.CALLOUT, INK)
            setPadding(dp(16), dp(12), dp(16), dp(12))
            compoundDrawablePadding = dp(12)
            accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            visibility = View.GONE
        }
        pages.addView(statusBanner, LinearLayout.LayoutParams(-1, -2).apply {
            leftMargin = dp(16); rightMargin = dp(16); topMargin = dp(8)
        })
        pages.addView(contentScroll, LinearLayout.LayoutParams(-1, 0, 1f))
        cloudEditorHost = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; visibility = View.GONE
        }
        pages.addView(cloudEditorHost, LinearLayout.LayoutParams(-1, 0, 1f))
        // The bar shares the page ground and is separated by a hairline, so the
        // system gesture area beneath it never shows a mismatched band.
        val tabs = LinearLayout(this).apply {
            orientation = if (rail) LinearLayout.VERTICAL else LinearLayout.HORIZONTAL
            gravity = if (rail) Gravity.CENTER_HORIZONTAL or Gravity.TOP else Gravity.CENTER
            setBackgroundColor(BACKGROUND)
            if (rail) setPadding(dp(8), dp(24), dp(8), dp(24)) else setPadding(dp(8), dp(6), dp(8), dp(6))
        }
        renderedNavTab = null
        TABS.forEach { (tab, title) ->
            tabs.addView(
                TextView(this).apply {
                    text = title
                    design.style(this, CompanionDesign.Type.CAPTION, MUTED)
                    minimumHeight = dp(64)
                    setPadding(dp(4), dp(4), dp(4), dp(4))
                    compoundDrawablePadding = dp(4)
                    setCompoundDrawablesWithIntrinsicBounds(null, navigationIcon(tab), null, null)
                    gravity = Gravity.CENTER
                    isClickable = true
                    isFocusable = true
                    background = android.graphics.drawable.RippleDrawable(
                        android.content.res.ColorStateList.valueOf(design.selected), null,
                        design.shape(Color.WHITE, dp(20).toFloat()))
                    navigation[tab] = this
                    setOnClickListener {
                        selectTab(tab)
                        developerPanel = false
                        render()
                    }
                },
                if (rail) LinearLayout.LayoutParams(dp(80), ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(12) }
                else LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f),
            )
        }
        val hairline = View(this).apply { setBackgroundColor(design.divider) }
        if (rail) {
            root.addView(tabs, LinearLayout.LayoutParams(dp(96), ViewGroup.LayoutParams.MATCH_PARENT))
            root.addView(hairline, LinearLayout.LayoutParams(1, -1))
            root.addView(pages, LinearLayout.LayoutParams(0, -1, 1f))
        } else {
            root.addView(pages, LinearLayout.LayoutParams(-1, 0, 1f))
            root.addView(hairline, LinearLayout.LayoutParams(-1, 1))
            root.addView(tabs, LinearLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        pageRoot = root
        discoveryHost = android.widget.FrameLayout(this).apply { visibility = View.GONE }
        return android.widget.FrameLayout(this).apply {
            addView(root, android.widget.FrameLayout.LayoutParams(-1, -1))
            addView(discoveryHost, android.widget.FrameLayout.LayoutParams(-1, -1))
        }
    }

    /** Persistent outcome or failure of the last action, shown at the top. */
    private fun renderStatusBanner(message: String) {
        val failure = listOf("失败", "无法", "不能", "错误", "停止自动连接").any { it in message }
        statusBanner.visibility = if (message.isBlank()) View.GONE else View.VISIBLE
        if (message.isBlank()) return
        statusBanner.text = message
        statusBanner.setTextColor(if (failure) design.warning else INK)
        statusBanner.background = design.shape(if (failure) design.warningSurface else design.selected,
            dp(CompanionDesign.RADIUS_CONTROL.toInt()).toFloat())
        statusBanner.setCompoundDrawablesRelativeWithIntrinsicBounds(
            CompanionIcons.drawable(this, if (failure) "info" else "check", if (failure) design.warning else INK).apply {
                setBounds(0, 0, dp(18), dp(18))
            }, null, null, null)
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

    fun render() {
        if (destroyed) return
        refreshCompanionSheet?.invoke()
        // Polling must not remove the target between touch-down and click.
        if (touchActive) { renderDeferred = true; return }
        val scrollY = if (renderedTab == currentTab) contentScroll.scrollY else 0
        val pageChanged = renderedTab != null && renderedTab != currentTab
        renderedTab = currentTab
        val ticket = ++renderTicket
        contentScroll.post {
            if (!destroyed && ticket == renderTicket && !touchActive)
                contentScroll.scrollTo(0, scrollY)
        }
        val selectedTab = when (currentTab) {
            TAB_INTERACTION -> TAB_OVERVIEW
            in DETAIL_TABS -> detailStack.firstOrNull { it in TOP_LEVEL_TABS } ?: defaultParent(currentTab)
            else -> currentTab
        }
        if (renderedNavTab != selectedTab) {
            // Rebuild indicators only when the selection changes, so a status
            // poll never replays the selection animation.
            val animateFrom = renderedNavTab
            renderedNavTab = selectedTab
            navigation.forEach { (tab, label) ->
                val chosen = tab == selectedTab
                label.isSelected = chosen
                design.style(label, if (chosen) CompanionDesign.Type.LABEL else CompanionDesign.Type.CAPTION, if (chosen) INK else MUTED)
                label.contentDescription = label.text.toString() + if (chosen) "，当前页面" else ""
                label.setCompoundDrawablesWithIntrinsicBounds(null,
                    navigationIcon(tab, chosen, animate = chosen && animateFrom != null), null, null)
            }
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
        if (pageChanged) revealPage(navDirection)
        if (!legacyConsoleMode) {
            renderStatusBanner(lastAction)
            renderDirectCompanion()
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

    internal val directPersonas = listOf("温柔陪伴", "活泼俏皮", "安静倾听", "认真交流", "轻轻嘴硬")
    internal val directPersonaDescriptions = listOf("慢慢聊，温柔回应", "轻松一点，多一点趣味",
        "留些空间，听你说完", "一起理清思路", "带一点俏皮的小别扭")

    internal fun directStatus(): String {
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

    // Public: the OTA acceptance probe invokes this reflectively.
    fun closeDirect(preserveAcceptedOtaSource: Boolean = false): Boolean {
        cancelPendingWakeImport("连接已关闭，尚未向设备发送模型")
        closeEyeServer()
        if (configFlow == ConfigFlow.EYES) finishEyeInstall("连接已关闭；未完成的眼睛安装不会重发")
        val preserveOtaSource = preserveAcceptedOtaSource && foreground && !destroyed &&
            otaServer?.running == true && otaVerificationPending && otaSource?.accepted == true
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

    // Keep the one-argument JVM signature: the UI acceptance probe invokes it reflectively.
    fun selectTab(value: Int) = selectTab(value, push = true)

    fun selectTab(value: Int, push: Boolean) {
        dismissCompanionSheet()
        navDirection = when {
            !push -> -1
            value in DETAIL_TABS && value != currentTab -> 1
            else -> 0
        }
        if (value in TOP_LEVEL_TABS || value == TAB_INTERACTION) detailStack.clear()
        else if (push && value != currentTab) {
            detailStack.remove(value)
            detailStack.add(currentTab)
        }
        currentTab = value
        if (value != TAB_SERVICES && cloudEditorEmbedded) {
            cloudEditorEmbedded = false
            settingsEditor?.close()
            settingsEditor = null
        }
    }

    private fun closeOtaServer(message: String? = null) {
        val source = otaSource ?: return
        otaSource = null
        source.close()
        if (message != null) otaMessage = message
    }

    private fun closeEyeServer() {
        val source = eyeSource ?: return
        eyeSource = null
        source.close()
    }

    private fun ownImportedFile(file: java.io.File): Boolean = importedFileOwner.own(file)

    /** Transfers a worker-owned file to a main-thread field. */
    private fun claimImportedFile(file: java.io.File): Boolean = importedFileOwner.claim(file)

    private fun discardImportedFile(file: java.io.File) {
        importedFileOwner.discard(file)
    }

    private fun setOtaKeepAwake(keep: Boolean) {
        if (keep) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    internal fun directRequest(command: DeviceControlProtocol.Command, value: Int = 0): Boolean {
        if (!foreground) return false
        return directSession.request(command, value)
    }

    internal fun directOtaRequest(command: DeviceControlProtocol.Command,
                                 payload: ByteArray = ByteArray(0)): Boolean {
        if (!foreground) return false
        return directSession.requestOta(command, payload)
    }

    private fun directConfigRequest(command: DeviceControlProtocol.Command, payload: ByteArray): Boolean =
        foreground && directSession.requestPayload(command, payload)

    internal fun configAvailable(): Boolean = foreground && directSession.current().authenticated &&
        directSnapshot?.publicConfigSupported == true && configFlow == ConfigFlow.NONE && agentEditor == null && focusEditor == null && trialEditor == null && defaultEditor == null && nfcEditor == null && pcEditor == null && !directPending &&
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

    internal fun cancelPendingWakeImport(message: String) {
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
        val needsWakeStatus = pack.bytes[3] == '2'.code.toByte() &&
            wakeStatusGeneration != state.generation
        if (state.snapshotFresh && configAvailable()) {
            // The chooser can expire the old session. Import owns these reads
            // even on the resources page, where settings polling does not run.
            when {
                configCapabilitiesGeneration != state.generation -> requestConfigCapabilities()
                needsWakeStatus -> requestWakeStatus()
                else -> {
                    // Consume before CONFIG_BEGIN; never replay a sent write.
                    cancelPendingWakeImport("")
                    beginWakePackage(pack)
                    return
                }
            }
        }
        val message = when {
            !state.authenticated -> "正在验证设备，模型尚未发送"
            !state.snapshotFresh || configCapabilitiesGeneration != state.generation -> "正在读取设备能力，模型尚未发送"
            needsWakeStatus -> "正在读取设备模型状态，模型尚未发送"
            else -> "等待当前设备操作结束，模型尚未发送"
        }
        if (wakeMessage != message) {
            wakeMessage = message
            if (foreground) render()
        }
        mainHandler.postDelayed({ advanceWakeImport(ticket) }, 200)
    }

    internal fun selectBundledWakeModel(label: String) {
        val pack = try { assets.open("wake-models/$label.wkm").use(WakeModelPackage::read) } catch (_: Exception) { null }
        if (pack == null) { wakeMessage = "尚未随 APK 提供有效模型包"; render(); return }
        beginWakePackage(pack)
    }

    internal fun wakeModelSummary(descriptor: WakeModelPackage.Companion.Descriptor): String =
        "${descriptor.phrase} · 模型 ${descriptor.sha256.take(4).joinToString("") { "%02x".format(it.toInt() and 0xff) }}"

    internal fun restoreWakeModel() {
        if (!configAvailable()) return
        configFlow = ConfigFlow.WAKE; wakeRestoreRequested = true
        wakeDeadline = android.os.SystemClock.elapsedRealtime() + 10_000
        wakeMessage = "正在读取设备保存的上一模型"
        requestWakeStatus()
    }

    internal fun requestWakeStatus() {
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

    fun failWake(message: String) {
        wakeMessage = message; wakeReadTicket++
        wakeStatusGeneration = null
        if (wakeApplied) { finishWake(message); return }
        when (directSession.cancelConfigTransaction()) {
            DeviceControlSession.ConfigCancellation.REMOTE_PENDING -> wakeCanceling = true
            DeviceControlSession.ConfigCancellation.LOCAL_CANCELLED,
            DeviceControlSession.ConfigCancellation.NOT_ACTIVE -> finishWake(message)
        }
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

    internal fun requestResponseModeRead() {
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

    internal fun requestResponseLengthRead() {
        if (configFlow == ConfigFlow.NONE) { if (!configAvailable()) return; configFlow = ConfigFlow.RESPONSE_LENGTH }
        if (configFlow != ConfigFlow.RESPONSE_LENGTH || responseLengthCanceling) return
        responseLengthGeneration = null; responseLengthError = null; responseLengthWire = ByteArray(0); responseLengthOffset = 0; responseLengthVerifyHeader = false
        requestResponseLengthChunk()
    }

    private fun requestResponseLengthChunk() {
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt((21 shl 16) or responseLengthOffset).array())) failResponseLength("设备忙，尚未读取回答长度")
    }

    internal fun responseLengthLabel(mode: Int) = when (mode) {
        1 -> "简洁"; 2 -> "详细"; else -> "标准"
    }

    private fun failResponseLength(message: String) {
        responseLengthGeneration = null; responseLengthExpected = null; responseLengthError = message
        responseLengthTransaction?.fill(0); responseLengthTransaction = null
        if (!responseLengthCanceling && directSession.cancelConfigTransaction() == DeviceControlSession.ConfigCancellation.REMOTE_PENDING) { responseLengthCanceling = true; return }
        responseLengthCanceling = false; configFlow = ConfigFlow.NONE; directMessage = message
        directSession.finishConfigTransaction(message)
    }

    internal fun editResponseLength() {
        val old = responseLength ?: return; val generation = directSession.current().generation
        if (responseLengthGeneration != generation) return
        var desired = old
        directDialog = AlertDialog.Builder(this).setTitle("回答长度")
            .setSingleChoiceItems(arrayOf("标准", "简洁", "详细"), old) { _, which -> desired = which }
            .setNegativeButton("取消", null).setPositiveButton("保存") { _, _ ->
                if (generation != directSession.current().generation || responseLengthGeneration != generation ||
                    !directSession.current().snapshotFresh || !configAvailable() || directSnapshot?.busy == true) {
                    directMessage = "设备忙或连接状态已变化，请稍后读取再设置"; render(); return@setPositiveButton
                }
                configFlow = ConfigFlow.RESPONSE_LENGTH; responseLengthGeneration = null
                responseLengthExpected = desired; responseLengthError = null
                responseLengthTransaction?.fill(0)
                responseLengthTransaction = ByteArray(16).also { SecureRandom().nextBytes(it) }
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                        ByteBuffer.allocate(8).putInt(21).putInt(32).array())) failResponseLength("设备忙，尚未发送回答长度")
            }.show()
    }

    private fun handleResponseLengthResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) { responseLengthCanceling = true; failResponseLength(responseLengthError ?: "回答长度操作未确认，请重新读取"); return }
        if (responseLengthCanceling) return
        if (snapshot.error != 0) { failResponseLength(if (snapshot.error == -95 || snapshot.error == -138) "当前固件未提供回答长度设置" else "回答长度操作未确认（${snapshot.error}），请重新读取"); return }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN -> {
                val desired = responseLengthExpected ?: run { failResponseLength("回答长度设置已取消"); return }
                val revision = responseLengthRevision ?: run { failResponseLength("回答长度版本已过期"); return }
                val transaction = responseLengthTransaction ?: run { failResponseLength("回答长度事务已失效"); return }
                val record = try {
                    ResponseLengthPreference.record(desired, revision, transaction)
                } catch (_: IllegalArgumentException) {
                    failResponseLength("回答长度事务或版本无效，请重新读取"); return
                }
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, record)) failResponseLength("设备忙，无法发送回答长度")
            }
            DeviceControlProtocol.Command.CONFIG_APPEND -> if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))) failResponseLength("设备忙，无法保存回答长度")
            DeviceControlProtocol.Command.CONFIG_APPLY -> requestResponseLengthRead()
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk
                if (chunk == null || chunk.totalLength != 24 || responseLengthOffset > 16) { failResponseLength("回答长度响应无效"); return }
                if (responseLengthVerifyHeader) {
                    if (!chunk.bytes.copyOf(16).contentEquals(responseLengthWire.copyOf(16))) { failResponseLength("回答长度在读取期间已变化，请重新读取"); return }
                } else responseLengthWire += chunk.bytes.copyOf(minOf(16, 24 - responseLengthOffset))
                responseLengthOffset += 16
                if (responseLengthWire.size < 24) { requestResponseLengthChunk(); return }
                if (!responseLengthVerifyHeader) { responseLengthVerifyHeader = true; responseLengthOffset = 0; requestResponseLengthChunk(); return }
                val value = try {
                    ResponseLengthPreference.decode(responseLengthWire, chunk.bytes)
                } catch (_: IllegalArgumentException) {
                    failResponseLength("回答长度响应无效或读取期间已变化，请重新读取"); return
                }
                val expected = responseLengthExpected
                responseLength = value.mode
                responseLengthRevision = value.revision
                responseLengthApplied = value.applied
                responseLengthGeneration = directSession.current().generation
                responseLengthExpected = null; responseLengthError = null; configFlow = ConfigFlow.NONE
                directMessage = value.receipt(expected)
                responseLengthTransaction?.fill(0); responseLengthTransaction = null
                if (expected != null) directSession.finishConfigTransaction(directMessage)
            }
            else -> Unit
        }
    }

    internal fun requestWakeSensitivityRead() {
        if (configFlow == ConfigFlow.NONE) {
            if (!configAvailable()) return
            configFlow = ConfigFlow.SENSITIVITY
        }
        if (configFlow != ConfigFlow.SENSITIVITY) return
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt(6 shl 16).array()))
            failWakeSensitivity("设备忙，尚未读取唤醒灵敏度")
    }

    internal fun editWakeSensitivity() {
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
        if (!wakeSensitivityCanceling && directSession.cancelConfigTransaction() == DeviceControlSession.ConfigCancellation.REMOTE_PENDING) {
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

    internal fun editResponseMode() {
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
        if (!responseModeCanceling && directSession.cancelConfigTransaction() == DeviceControlSession.ConfigCancellation.REMOTE_PENDING) {
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
                if (otaSource?.shouldCloseAfterStatus(snapshot.otaStatus.state, otaUpload != null) == true) {
                    // A terminal source no longer needs Wi-Fi, even if INFO is
                    // still pending before the separate version confirmation.
                    closeOtaServer()
                    otaStartGate.release()
                    setOtaKeepAwake(false)
                }
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

        if (command == DeviceControlProtocol.Command.OTA_CANCEL) {
            android.util.Log.i("ShaniuOta", "cancel generation=$epoch result=${snapshot.error} sourceAccepted=${otaSource?.accepted}")
        }
        if (command == DeviceControlProtocol.Command.OTA_CANCEL && snapshot.error == -114) {
            otaUpload?.close(); otaUpload = null
            otaVerificationPending = true
            // Refusing cancellation leaves the device's accepted download
            // active. Keep its source, start lease and foreground wake lock.
            otaMessage = "设备已进入不可取消阶段，更新将继续；请保持 App 在前台并保持设备供电。"
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
                if (otaSource?.accepted == true) {
                    // START succeeded before a racing CANCEL failed to send or
                    // was rejected. Its source must outlive this upload object.
                    val error = upload.error ?: snapshot.error
                    upload.close(); otaUpload = null
                    otaVerificationPending = true
                    "取消请求未获成功确认（$error）；设备已接受更新，请保持 App 在前台及设备供电，等待状态确认。"
                } else {
                    val message = "设备拒绝${otaCommandLabel(command)}（${upload.error ?: snapshot.error}）；升级来源未完成。"
                    closeOtaServer()
                    /* The board retains a rejected source record until this GATT
                     * session closes. Do not retry BEGIN on the same channel. */
                    closeDirect()
                    "$message 已断开设备连接，请重新连接后重试。"
                }
            }
            upload?.state == OtaControlUpload.State.ACCEPTED -> {
                "设备已接受升级来源，正在等待设备报告升级状态。"
            }
            upload?.state == OtaControlUpload.State.CANCELED -> {
                otaUpload = null
                otaVerificationPending = false
                closeOtaServer()
                otaStartGate.release()
                setOtaKeepAwake(false)
                preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
                "设备已确认取消升级请求。"
            }
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

    private fun otaStartStateCurrent(): Boolean {
        val state = directSession.current()
        return OtaUpdatePolicy.mayAdmitStart(
            state.authenticated, state.snapshotFresh,
            state.snapshot?.otaSupported == true,
        )
    }

    fun startLocalOta() {
        val file = selectedFirmwareFile ?: return
        val pack = inspectedFirmware ?: return
        val connection = directConnection
        val info = directFirmwareInfo
        val sessionState = directSession.current()
        android.util.Log.i("ShaniuOta", "start generation=$directEpoch authenticated=${sessionState.authenticated} fresh=${sessionState.snapshotFresh} supported=${sessionState.snapshot?.otaSupported} readPending=${sessionState.readPending} writePending=$directPending upload=${otaUpload?.state}")
        if (!otaStartStateCurrent()) {
            otaMessage = "设备状态已过期，请刷新后再试；未发送升级请求。"
            render()
            return
        }
        if (connection == null || directPending || otaUpload != null ||
            preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false)) return
        if (info == null) { otaMessage = "正在读取设备版本，暂不能开始升级。"; render(); return }
        if (!OtaUpdatePolicy.mayStart(pack.board, DEVICE_BOARD, info.securityCounter, pack.securityCounter)) {
            otaMessage = if (pack.board != DEVICE_BOARD) "固件包不适用于当前设备。"
                else "设备安全计数不低于目标固件，不能降级或重复升级。"
            render(); return
        }
        val sourceLease = otaStartGate.acquireLease()
        if (sourceLease == null) {
            android.util.Log.i("ShaniuOta", "start generation=$directEpoch skipped=preparing")
            return
        }
        fun releaseSourceLease(): Boolean {
            val released = otaStartGate.release(sourceLease)
            if (released) setOtaKeepAwake(false)
            return released
        }
        val epoch = directEpoch
        otaMessage = "正在准备本机升级来源…"
        val source = OtaSourceLease<OtaPackageServer> { server -> ioExecutor.execute { server.close() } }
        otaSource = source
        setOtaKeepAwake(true)
        render()
        ioExecutor.execute {
            val started = android.os.SystemClock.elapsedRealtime()
            val opened = runCatching { OtaPackageServer.open(applicationContext, file) }
            val failure = opened.exceptionOrNull()
            android.util.Log.i("ShaniuOta", "source generation=$epoch elapsedMs=${android.os.SystemClock.elapsedRealtime() - started} stage=${(failure as? OtaPackageServerException)?.stage ?: "READY"} error=${failure?.javaClass?.simpleName ?: "none"}")
            val server = opened.getOrNull()
            if (server != null && !source.publish(server)) {
                server.close()
                return@execute
            }
            mainHandler.post {
                if (otaSource !== source) return@post
                if (epoch != directEpoch || !foreground || destroyed) {
                    source.close()
                    if (otaSource === source) otaSource = null
                    if (releaseSourceLease() && epoch == directEpoch && !destroyed) {
                        otaMessage = "已离开升级页面；未向设备发送升级请求。"
                    }
                    return@post
                }
                if (server == null) {
                    if (otaSource === source) closeOtaServer()
                    otaMessage = otaSourceOpenFailureMessage(opened.exceptionOrNull())
                    releaseSourceLease()
                    render()
                    return@post
                }
                otaMessage = "正在等待设备状态读取完成…"
                val prepared = otaStartGate.prepare(sourceLease) prepared@ {
                    if (otaSource !== source || epoch != directEpoch || !foreground || destroyed) {
                        source.close()
                        releaseSourceLease()
                        return@prepared
                    }
                    val admission = directSession.current()
                    android.util.Log.i("ShaniuOta", "begin generation=$epoch readPending=${admission.readPending} writePending=${admission.writePending} authenticated=${admission.authenticated} fresh=${admission.snapshotFresh} supported=${admission.snapshot?.otaSupported}")
                    if (!otaStartStateCurrent()) {
                        closeOtaServer()
                        releaseSourceLease()
                        otaMessage = "设备状态已过期，请刷新后再试；未发送升级请求。"
                        render()
                        return@prepared
                    }
                    if (directPending) {
                        closeOtaServer()
                        releaseSourceLease()
                        otaMessage = "设备正在处理其他操作；未发送升级请求。"
                        render()
                        return@prepared
                    }
                    val metadata = server.metadata
                    if (metadata == null || metadata.catalogSha256 != pack.catalogSha256 || !persistExpected(metadata)) {
                        closeOtaServer()
                        releaseSourceLease()
                        otaMessage = "无法保存本次升级核验目标，未向设备发送升级请求。"
                        render()
                        return@prepared
                    }
                    lateinit var upload: OtaControlUpload
                    upload = OtaControlUpload(server.requestRecord, onStartAccepted = {
                        source.accept()
                        otaStatus = null; otaStatusGeneration = null
                        otaVerificationPending = true
                    }) { command, payload ->
                        if (epoch != directEpoch || !foreground || directConnection == null) false
                        else if (command == DeviceControlProtocol.Command.OTA_START &&
                            !preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, true).commit()) false
                        else directOtaRequest(command, payload)
                    }
                    otaUpload = upload
                    if (!upload.start()) {
                        otaUpload = null; closeOtaServer(); releaseSourceLease()
                        otaMessage = "设备控制通道不可用，未开始升级。"
                    } else {
                        otaMessage = "升级来源已准备，正在请求设备接受升级。"
                    }
                    render()
                }
                if (!prepared) {
                    closeOtaServer()
                    releaseSourceLease()
                    otaMessage = "升级准备已失效；未发送升级请求。"
                } else otaStartGate.resume(directSession.current().readPending)
                render()
            }
        }
    }

    internal fun cancelLocalOta() {
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

    internal fun expectedOtaConfirmed(): Boolean {
        if (!OtaSourceLease.mayApplyVerification(otaSource)) return false
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
        if (!OtaSourceLease.mayApplyVerification(otaSource)) return
        val info = directFirmwareInfo ?: return
        val status = otaStatus ?: return
        val version = "${info.major}.${info.minor}.${info.revision}+${info.build}"
        val expectedVersion = preferences.getString(KEY_OTA_EXPECTED_VERSION, null)
        val expectedCounter = preferences.getLong(KEY_OTA_EXPECTED_COUNTER, -1L)
        val expectedDeviceId = preferences.getString(KEY_OTA_EXPECTED_DEVICE, null)
        if (preferences.getBoolean(KEY_OTA_EXPECTED_PENDING, false) && otaUpload == null &&
            expectedVersion != null && expectedDeviceId != null &&
            OtaUpdatePolicy.mayDiscardTerminalMismatch(otaSource?.accepted == true,
                expectedDeviceId, provisionedDeviceId, expectedVersion, expectedCounter,
                version, info.securityCounter, status.state, status.phase, status.result)) {
            preferences.edit().putBoolean(KEY_OTA_EXPECTED_PENDING, false).commit()
            otaVerificationPending = false
            otaStartGate.release(); setOtaKeepAwake(false)
            otaMessage = "设备已报告完成旧更新，但当前版本不是待核对目标；已结束等待，可重新发起更新。"
            return
        }
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

    internal fun editDirectVolume() {
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
    internal fun scanDirect() {
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

    internal fun connectDirect(device: android.bluetooth.BluetoothDevice, epoch: Long) {
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

    fun dismissDirectDiscovery() {
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
                ConfigFlow.RESPONSE_LENGTH -> handleResponseLengthResult(command, snapshot)
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

    internal fun failCloudModels(message: String) {
        cloudModelsWire?.fill(0); cloudModelsWire = null; cloudModelsTotal = -1; cloudModelsOffset = 0
        cloudModelsExpected = null; cloudModelsReadError = message
        cloudModelsGeneration = null
        // A failed optional read must not starve other configuration kinds.
        // A new connection or an explicit read can retry; no write is replayed.
        cloudModelsFailedGeneration = directSession.current().generation
        cloudModelsReadDeadline = 0; cloudModelsReadTicket++
        if (!cloudModelsCanceling && directSession.cancelConfigTransaction() == DeviceControlSession.ConfigCancellation.REMOTE_PENDING) {
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

    internal fun editDeviceSettings() {
        openDeviceSettings(false)
    }

    fun openDeviceSettings(cloud: Boolean, modelFocus: String? = null) {
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
            TAB_OVERVIEW, TAB_INTERACTION -> renderHome(bound)
            TAB_PERSONALITY -> renderAppearance()
            TAB_RESOURCES -> {
                CompanionPage(this, content).pageTitle("资源包与唤醒词", "先检查兼容性，再发送到设备。", ::navigateBack)
                renderCustomizationResources()
            }
            TAB_PERSONA -> renderPersona(bound)
            TAB_PRIVACY -> {
                CompanionPage(this, content).pageTitle("隐私与记忆", "有用的陪伴，也应有清楚的边界。", ::navigateBack)
                renderPrivacyControls(content)
            }
            TAB_UPDATE -> renderFirmwareUpdate()
            TAB_ADVANCED -> renderVoiceAdvanced()
            TAB_SETTINGS -> renderSettingsHome(bound)
            else -> renderServices(bound)
        }
    }

    // Public: the UI acceptance probe opens this sheet reflectively.
    fun showWakeSheet() {
        showCompanionSheet("一句你好，唤醒她", "当前唤醒词以设备回读为准。", done = false) { body, dialog ->
            val page = CompanionPage(this, body)
            val wave = LinearLayout(this).apply { gravity = Gravity.CENTER; importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }
            // A two-syllable "你好" envelope followed by the name: decorative only.
            listOf(6, 14, 26, 32, 22, 10, 6, 12, 24, 36, 28, 16, 30, 20, 8).forEach { height ->
                wave.addView(View(this).apply { background = design.shape(design.ink, dp(2).toFloat()) },
                    LinearLayout.LayoutParams(dp(4), dp(height)).apply { marginStart = dp(2); marginEnd = dp(2) })
            }
            body.addView(wave, LinearLayout.LayoutParams(-1, dp(56)).apply { bottomMargin = dp(12) })
            val current = wakeStatus.takeIf { wakeStatusGeneration == directSession.current().generation && directSession.current().authenticated }
            page.informationRow("当前唤醒模型", current?.active?.let(::wakeModelSummary) ?: "尚未读取设备当前模型", "mic")
            page.informationRow("候选模型单独验证", "先校验兼容性，不会自动替换当前模型", "shield")
            page.primaryButton("管理唤醒资源", true) { selectTab(TAB_RESOURCES); render() }
            page.secondaryButton("完成", true) { dialog.dismiss() }
        }
    }

    // Public: the UI acceptance probe opens this sheet reflectively.
    fun showPrivacySheet() {
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

    fun showDefaultSelection() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        showCompanionSheet("默认表情", "把喜欢的模样，留作日常陪伴。", done = false, onClosed = {
            defaultEditor?.close(); defaultEditor = null
        }) { body, dialog ->
            val page = CompanionPage(this, body)
            val status = TextView(this).apply {
                textSize = 16f; setTextColor(design.ink)
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            body.addView(status, LinearLayout.LayoutParams(-1, -2))
            val selected = selectedEyePack
            page.addCard("本地所选素材", selected?.let { "${it.packId} · 版本 ${it.revision}" }
                ?: "尚未选择素材。可先在资源更新中导入本地素材包。")
            page.notice("本页切换设备上同名已安装的素材，不上传本地文件。请先刷新设备默认；选择或预览不会更改默认。关闭页面不会撤销已受理的操作。")
            page.settingsRow("去选择素材", "打开资源更新", iconName = "download") {
                dialog.dismiss(); updateResources = true; selectTab(TAB_UPDATE); render()
            }
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            defaultEditor = com.shaniu.companion.provision.DefaultSelectionController(directSession, changed = { state ->
                val value = state.snapshot
                status.text = state.message + (value?.filename?.let {
                    "\n上次回读：${it.removeSuffix(".bkep")}"
                } ?: "") + if (value?.saved == true && value.rendered.not()) "\n设备报告已保存，显示尚未确认" else ""
                controls.removeAllViews()
                val actions = CompanionPage(this, controls)
                val editor = defaultEditor
                val ready = !state.busy && directSession.current().authenticated
                actions.settingsRow("读取操作结果", "只读最近任务，不重新提交", ready) { editor?.refresh() }
                actions.settingsRow("刷新设备默认", "读取设备上保存的默认素材", editor?.canAct(2) == true) { editor?.act(2) }
                actions.primaryButton("将所选素材设为默认", selected != null && editor?.canAct(1) == true) {
                    editor?.act(1, selected?.let { "${it.packId}.bkep" })
                }
                actions.settingsRow("取消未提交的操作", "提交开始后不能撤销保存", editor?.canAct(3) == true) { editor?.act(3) }
                if (value?.releaseError != null && value.releaseError != 0)
                    actions.settingsRow("恢复资源访问", "重试释放资源，原操作结果仍需核对", editor?.canAct(4) == true) { editor?.act(4) }
            })
            defaultEditor?.refresh()
        }
    }

    fun showExpressionTrial() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        showCompanionSheet("限时表情试用", "让她换个表情，陪你一小会儿。", onClosed = {
            trialEditor?.close(); trialEditor = null
        }) { body, dialog ->
            val page = CompanionPage(this, body)
            val status = TextView(this).apply {
                textSize = 16f; setTextColor(design.accent)
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            body.addView(status)
            val filename = selectedEyePack?.packId?.let { "$it.bkep" }
                ?.takeIf { it.length < 40 && Regex("[a-z][a-z0-9._-]*\\.bkep").matches(it) }
            var refreshActions: () -> Unit = {}
            val sources = android.widget.Spinner(this).apply {
                contentDescription = "试用素材"
                adapter = android.widget.ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                    listOf("设备当前默认素材", selectedEyePack?.let { "所选素材：${it.packId}" } ?: "所选素材（尚未选择）"))
                setSelection(if (trialPackDraft) 1 else 0)
                onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                    override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                        trialPackDraft = position == 1; refreshActions()
                    }
                    override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                }
            }
            body.addView(sources, LinearLayout.LayoutParams(-1, dp(56)))
            page.notice("所选素材按设备上同名已安装的包试用，不上传本地文件，也不核对本地版本。可先在资源更新中选择素材。")
            page.settingsRow("去选择素材", "打开资源更新", iconName = "download") {
                dialog.dismiss(); updateResources = true; selectTab(TAB_UPDATE); render()
            }
            if (selectedEyePack != null && filename == null)
                page.notice("所选素材的名称暂不支持试用，请选择其他素材。")
            val expressions = android.widget.Spinner(this).apply {
                contentDescription = "试用表情"
                adapter = android.widget.ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                    listOf("日常", "开心", "害羞", "难过", "惊讶", "思考", "倾听", "说话", "晚安"))
                setSelection(trialExpressionDraft)
                onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                    override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                        trialExpressionDraft = position
                    }
                    override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                }
            }
            body.addView(expressions, LinearLayout.LayoutParams(-1, dp(56)))
            val seconds = EditText(this).apply {
                hint = "试用秒数（例如 30）"; contentDescription = "试用秒数"
                setText(trialSecondsDraft); setTextColor(design.ink)
                inputType = android.text.InputType.TYPE_CLASS_NUMBER
                addTextChangedListener(object : android.text.TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                        trialSecondsDraft = s?.toString().orEmpty()
                    }
                    override fun afterTextChanged(s: android.text.Editable?) = Unit
                })
            }
            body.addView(seconds, LinearLayout.LayoutParams(-1, -2))
            page.notice("试用不设为默认。关闭此页或手机断开后，设备仍按时长结束；新的显示操作可能提前结束试用。")
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            refreshActions = {
                val state = trialEditor?.current()
                val value = state?.snapshot
                status.text = (state?.message ?: "尚未读取设备试用") + (value?.remainingMs?.takeIf { value.state in 1..3 }?.let {
                    "\n上次回读剩余 ${it / 1000} 秒"
                } ?: "")
                controls.removeAllViews()
                val actions = CompanionPage(this, controls)
                val ready = state?.busy == false && directSession.current().authenticated
                sources.isEnabled = state?.busy != true
                actions.primaryButton("读取试用状态", ready) { trialEditor?.refresh() }
                actions.primaryButton("开始试用", ready && value?.state in listOf(0, 6, 7, 8, 9) && (!trialPackDraft || filename != null)) {
                    val count = seconds.text.toString().toLongOrNull()
                    if (count == null || count !in 1L..4294967L) seconds.error = "请输入有效秒数"
                    else trialEditor?.act(1, count * 1000, expressions.selectedItemPosition + 1,
                        if (trialPackDraft) filename else null)
                }
                actions.primaryButton("取消试用", ready && value?.state in listOf(1, 3, 4)) { trialEditor?.act(2) }
            }
            trialEditor = com.shaniu.companion.provision.ExpressionTrialController(directSession, changed = { refreshActions() })
            refreshActions()
            trialEditor?.refresh()
        }
    }

    fun showFocusTimer() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        showCompanionSheet("专注计时", "把这一段时间，留给眼前的事。", onClosed = {
            focusEditor?.close(); focusEditor = null
        }) { body, _ ->
            val page = CompanionPage(this, body)
            val status = TextView(this).apply { textSize = 16f; setTextColor(design.accent) }
            body.addView(status)
            val minutes = EditText(this).apply {
                hint = "专注分钟数"; setText(focusMinutesDraft)
                addTextChangedListener(object : android.text.TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                        focusMinutesDraft = s?.toString().orEmpty()
                    }
                    override fun afterTextChanged(s: android.text.Editable?) = Unit
                })
                inputType = android.text.InputType.TYPE_CLASS_NUMBER
                contentDescription = "专注分钟数"; setTextColor(design.ink)
            }
            body.addView(minutes, LinearLayout.LayoutParams(-1, -2))
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            page.notice("设备执行计时。关闭此页不会取消已开始的计时；支持此功能的固件会显示进度和完成图标；暂不提供重启恢复或声音提醒。")
            focusEditor = com.shaniu.companion.provision.FocusTimerController(directSession, changed = { state ->
                val value = state.snapshot
                val label = when (value?.state) {
                    0 -> "尚未开始"; 1 -> "专注中"; 2 -> "已暂停"; 3 -> "计时已结束"; 4 -> "已取消"; else -> "尚未确认"
                }
                status.text = label + (value?.let { " · 上次回读剩余 ${it.remainingMs / 1000} 秒" } ?: "") + "\n" + state.message
                controls.removeAllViews()
                val actions = CompanionPage(this, controls)
                val ready = !state.busy && directSession.current().authenticated
                actions.primaryButton("读取设备计时", ready) { focusEditor?.refresh() }
                actions.primaryButton("开始专注", ready && value?.state in listOf(0, 3, 4)) {
                    val count = minutes.text.toString().toLongOrNull()
                    if (count == null || count <= 0 || count > Long.MAX_VALUE / 60000) minutes.error = "请输入有效分钟数"
                    else focusEditor?.act(1, count * 60000)
                }
                actions.primaryButton(if (value?.state == 2) "继续" else "暂停", ready && value?.state in 1..2) {
                    focusEditor?.act(if (value?.state == 2) 3 else 2)
                }
                actions.primaryButton("取消计时", ready && value?.state in 1..2) { focusEditor?.act(4) }
            })
            focusEditor?.refresh()
        }
    }

    internal fun showPcApproval() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        showCompanionSheet("电脑审批", "傻妞眼睛出现“!”时，在这里核对完整操作。", onClosed = {
            approvalEditor?.close(); approvalEditor = null
        }) { body, dialog ->
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            fun detail(label: String, value: String) {
                controls.addView(TextView(this).apply {
                    text = label; textSize = 12f; setTextColor(MUTED); setPadding(0, dp(12), 0, dp(4))
                })
                controls.addView(TextView(this).apply {
                    text = value.ifEmpty { "（未提供）" }; textSize = 15f; setTextColor(design.ink)
                    typeface = android.graphics.Typeface.MONOSPACE; setTextIsSelectable(true)
                    setPadding(dp(14), dp(12), dp(14), dp(12))
                    background = design.shape(design.selected, dp(14).toFloat())
                }, LinearLayout.LayoutParams(-1, -2))
            }
            var shown = ""
            approvalEditor = com.shaniu.companion.provision.PcApprovalController(directSession) { state ->
                // Polling re-reads every 2 s; redraw only when what the owner
                // is reading changes (remaining time in 10 s steps).
                val signature = listOf(state.busy, state.message, state.noRequest,
                    state.request?.requestId?.joinToString("") { "%02x".format(it.toInt() and 255) },
                    state.request?.state, state.request?.remainingMs?.div(10000)).toString()
                if (signature == shown) return@PcApprovalController
                shown = signature
                controls.removeAllViews()
                val page = CompanionPage(this, controls)
                page.notice(state.message)
                val value = state.request
                val ready = !state.busy && directSession.current().authenticated
                if (value != null) {
                    detail("请求方", value.agent)
                    detail("目标", value.target)
                    detail("完整操作（逐字显示）", value.operation)
                    val fingerprint = value.digest.take(4).joinToString("") { "%02x".format(it.toInt() and 255) }
                    detail("请求指纹（与电脑终端核对）", fingerprint +
                        if (value.state == com.shaniu.companion.provision.PcApprovalController.PENDING)
                            " · 剩余约 ${value.remainingMs / 1000} 秒" else "")
                    val pending = value.state == com.shaniu.companion.provision.PcApprovalController.PENDING
                    page.primaryButton("批准此操作", ready && pending) {
                        confirm("批准电脑操作", "${value.agent} 将执行：\n\n${value.operation}\n\n目标：${value.target.ifEmpty { "（未提供）" }}") {
                            approvalEditor?.decide(true)
                        }
                    }
                    page.primaryButton("拒绝", ready && pending) { approvalEditor?.decide(false) }
                }
                page.primaryButton("重新读取", ready) { approvalEditor?.refresh() }
                page.notice("只有本页显示的这份完整内容会被批准；电脑若修改内容或更换请求，设备会拒绝此决定。超时或关闭电脑端视为拒绝。")
            }
            // Poll while this sheet is open and idle so a new request appears
            // without extra taps; a decision is never sent automatically.
            val poll = object : Runnable {
                override fun run() {
                    if (!dialog.isShowing || approvalEditor == null) return
                    approvalEditor?.refresh()
                    mainHandler.postDelayed(this, 2000)
                }
            }
            mainHandler.post(poll)
        }
    }

    internal fun showAgentData(kind: Int) {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        val facts = kind == 26
        val native = kind == 28
        showCompanionSheet(if (facts) "事实记忆" else if (native) "原生功能" else "运行时技能",
            if (facts) "只保存你明确表达的信息，可随时纠正。" else if (native) "从文件或 HTTPS 商店安装经发布者签名验证的设备功能。" else "由你决定启用哪些场景与能力。",
            onClosed = {
                factDialog?.dismiss(); factDialog = null
                agentImportSelected = null; agentEditor?.close(); agentEditor = null
            }) { body, _ ->
            val controls = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
            body.addView(controls)
            agentEditor = com.shaniu.companion.provision.AgentDataController(directSession, kind) { state ->
                controls.removeAllViews()
                val page = CompanionPage(this, controls)
                page.notice(state.message)
                val ready = !state.busy && directSession.current().authenticated && directSnapshot?.busy == false
                page.primaryButton("重新读取", !state.busy && directSession.current().authenticated) { agentEditor?.refresh() }
                val document = state.document
                if (facts) {
                    val enabled = document?.get("enabled")?.asBoolean == true
                    val values = document?.getAsJsonArray("facts")?.associate {
                        it.asJsonObject["key"].asString to it.asJsonObject["value"].asString
                    }.orEmpty()
                    for ((key, label) in listOf("name" to "称呼", "preference" to "偏好", "topic" to "近期话题")) {
                        page.settingsRow(label, values[key] ?: "尚未保存", enabled = ready && enabled) {
                            editAgentFact(key, label, values[key])
                        }
                    }
                    page.notice("修改或删除事实会同时清空近期对话，避免旧内容继续影响回复。关闭记忆后不读取或新增保存；删除全部记忆请返回隐私页。")
                } else if (native && document != null) {
                    renderNativeFeatures(page, document, ready)
                } else if (document != null) {
                    for (element in document.getAsJsonArray("items")) {
                        val item = element.asJsonObject
                        val id = item["id"].asString
                        val enabled = item["enabled"].asBoolean
                        val title = item.get("name")?.asString ?: id
                        val trigger = item.get("trigger")?.asString.orEmpty()
                        val permissions = item.getAsJsonArray("permissions").joinToString("、") {
                            when (it.asString) {
                                "focus_timer", "focus.timer" -> "专注计时"
                                "camera_capture", "camera.capture" -> "拍照并发送到云端识别"
                                "device_status", "device.status" -> "读取设备状态"
                                "device_motion", "device.motion" -> "读取运动状态"
                                "reply_expression", "reply.expression" -> "对话表情"
                                "read_file", "skill.read" -> "读取本技能说明"
                                else -> it.asString
                            }
                        }
                        val origin = if (item.get("builtin")?.asBoolean == true) "固件内置" else "设备已验签"
                        page.settingsRow(title, "$origin · 触发词：$trigger\n权限：$permissions", ready, checked = enabled) {
                            confirm(if (enabled) "停用$title" else "启用$title",
                                if (enabled) "下次对话将拒绝此场景。" else "此场景可以使用：$permissions。") {
                                agentEditor?.apply(com.google.gson.JsonObject().apply {
                                    addProperty("id", id); addProperty("enabled", !enabled)
                                })
                            }
                        }
                    }
                    val fingerprint = document.get("trusted_key_sha256")?.asString.orEmpty()
                    page.notice(if (fingerprint.isEmpty()) "尚未信任外部技能发布者。" else "发布者公钥指纹\n$fingerprint")
                    page.primaryButton("导入发布者公钥", ready) { selectAgentImport(true) }
                    page.primaryButton("安装签名技能包", ready && fingerprint.isNotEmpty()) { selectAgentImport(false) }
                    page.notice("技能只在明确触发后生效。签名包不能增加固件未开放的权限；安装后需单独启用。更换发布者会清除原来的外部技能。")
                }
            }
            agentEditor?.refresh()
        }
    }

    private fun editAgentFact(key: String, label: String, value: String?) {
        val editor = agentEditor ?: return
        val input = EditText(this).apply {
            setText(value.orEmpty()); hint = label; contentDescription = label
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
            setPadding(dp(20), dp(12), dp(20), dp(12))
        }
        val dialog = AlertDialog.Builder(this).setTitle(label)
            .setMessage("保存或删除会清空近期对话。")
            .setView(input).setPositiveButton("保存", null).setNegativeButton("取消", null)
            .apply { if (value != null) setNeutralButton("删除", null) }.create()
        factDialog = dialog
        dialog.setOnShowListener {
            dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                val text = input.text.toString().trim()
                if (text.isEmpty() || text.toByteArray(Charsets.UTF_8).size > 96) {
                    input.error = "请输入简短事实（最多 32 个常用汉字或 96 字节）"
                } else if (agentEditor === editor && editor.apply(com.google.gson.JsonObject().apply {
                        addProperty("op", "set"); addProperty("key", key); addProperty("value", text)
                    })) dialog.dismiss()
                else input.error = "设备忙或数据已变化，请返回重新读取"
            }
            dialog.getButton(AlertDialog.BUTTON_NEUTRAL)?.setOnClickListener {
                if (agentEditor === editor && editor.apply(com.google.gson.JsonObject().apply {
                        addProperty("op", "delete"); addProperty("key", key)
                    })) dialog.dismiss()
                else input.error = "设备忙或数据已变化，请返回重新读取"
            }
        }
        dialog.show()
    }

    private fun renderNativeFeatures(page: CompanionPage, document: com.google.gson.JsonObject, ready: Boolean) {
        val running = document.get("running")?.asString.orEmpty()
        page.notice(if (running.isEmpty()) "当前没有运行中的原生功能。" else "正在运行：$running")
        val lastApp = document.get("last_app")?.asString.orEmpty()
        val lastValue = document.get("last_value")?.asInt ?: 0
        val lastLabels = document.getAsJsonArray("items").map { it.asJsonObject }
            .firstOrNull { it["id"].asString == lastApp }?.getAsJsonArray("results")
        if (running.isEmpty() && (document.get("last_result")?.asInt ?: 0) == 0 &&
            lastLabels != null && lastValue in 1..lastLabels.size()) {
            page.notice("上次结果（$lastApp）：${lastLabels[lastValue - 1].asString}")
        }
        when (document.get("last_result")?.asInt ?: 0) {
            -125 -> page.notice("上次运行已被设备的优先任务中止。")
            -16 -> page.notice("设备显示或其他资源忙，上次运行未能完成。")
            -1 -> page.notice("上次运行请求了未声明的权限，已被设备停止。")
            -110 -> page.notice("上次运行单步耗时超出预算，已被设备停止。")
            -34 -> page.notice("上次运行输出了无效的画面或反馈请求，已被设备停止。")
            0 -> Unit
            else -> page.notice("上次运行失败。请确认功能包与当前固件兼容，再重新启动。")
        }
        for (element in document.getAsJsonArray("items")) {
            val item = element.asJsonObject
            val id = item["id"].asString
            val name = item["name"].asString
            val version = item["version"].asLong
            val description = item["description"].asString
            val permissions = item.getAsJsonArray("permissions")?.map { it.asString } ?: listOf("display")
            val faults = item.get("faults")?.asInt ?: 0
            val blocked = item.get("blocked")?.asBoolean ?: false
            val health = if (blocked) "\n本次开机已连续异常 $faults 次，已暂停；重启设备或安装新版本后恢复。"
                else if (faults > 0) "\n本次开机异常 $faults 次" else ""
            page.settingsRow(name, "$description\n版本 $version · " +
                com.shaniu.companion.provision.NativeFeatureStore.permissionLabels(permissions) + health,
                ready && (running.isEmpty() || running == id) && (!blocked || running == id)) {
                if (running == id) {
                    confirm("停止$name", "停止正在设备上运行的功能？") { nativeOperation("stop", id) }
                } else {
                    confirm("启动$name", "设备一次运行一个原生功能。语音、升级和关机等优先任务会中止它。") { nativeOperation("start", id) }
                }
            }
            page.settingsRow("卸载$name", "从设备删除此功能", ready && running.isEmpty()) {
                confirm("卸载$name", "卸载后需要重新导入功能包才能恢复。") { nativeOperation("uninstall", id) }
            }
        }
        document.getAsJsonArray("schedule")?.takeIf { it.size() > 0 }?.let { schedule ->
            val clock = document.get("clock_valid")?.asBoolean ?: false
            page.notice("定时启动（可用语音设置或取消）" + (if (clock) "" else "\n设备时间未同步，定时暂不生效。") + "\n" +
                schedule.joinToString("\n") { element ->
                    val entry = element.asJsonObject
                    val minute = entry["minute"].asInt
                    val id = entry["id"].asString.ifEmpty { "（已卸载的功能）" }
                    val last = when (entry["last_result"].asInt) { 1 -> "尚未到时"; 0 -> "已启动"; else -> "上次已跳过" }
                    "%02d:%02d %s %s · %s".format(minute / 60, minute % 60,
                        com.shaniu.companion.provision.NativeFeatureStore.repeatLabel(entry["days"].asInt), id, last)
                })
        }
        val fingerprint = document.get("trusted_key_sha256")?.asString.orEmpty()
        val publisher = document.get("publisher_key_sha256")?.asString.orEmpty()
        if (publisher.isEmpty()) page.notice("当前固件尚未配置第一方功能发布者，暂不能安装原生功能。")
        page.notice(if (fingerprint.isEmpty()) "尚未信任原生功能发布者。" else "发布者公钥指纹\n$fingerprint")
        page.primaryButton("导入发布者公钥", ready && publisher.isNotEmpty() && document.getAsJsonArray("items").size() == 0) { selectAgentImport(true) }
        page.primaryButton("从文件安装功能包", ready && fingerprint.isNotEmpty() && running.isEmpty()) { selectAgentImport(false) }
        val url = preferences.getString("native_feature_store_url", "").orEmpty()
        page.notice(if (url.isEmpty()) "尚未配置 HTTPS 功能商店目录。" else "功能商店目录\n$url")
        page.primaryButton("配置商店地址", ready) { editNativeStoreUrl(url) }
        page.primaryButton("刷新商店", ready && url.isNotEmpty() && fingerprint.isNotEmpty() && running.isEmpty()) { fetchNativeStore(url) }
        page.notice("仅接受固件指定发布者签名的第一方功能。原生功能与设备固件共享权限；商店使用 HTTPS，设备安装时验签。")
    }

    private fun nativeOperation(op: String, id: String) {
        agentEditor?.apply(com.google.gson.JsonObject().apply { addProperty("op", op); addProperty("id", id) })
    }

    private fun editNativeStoreUrl(initial: String) {
        val input = EditText(this).apply { setText(initial); hint = "https://example.com/shaniu-store.json"; inputType = InputType.TYPE_TEXT_VARIATION_URI }
        AlertDialog.Builder(this).setTitle("功能商店目录").setMessage("仅保存 HTTPS 地址；不会自动下载或信任发布者。")
            .setView(input).setPositiveButton("保存", null).setNegativeButton("取消", null).create().also { dialog ->
                dialog.setOnShowListener { dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
                    val value = input.text.toString().trim()
                    if (!com.shaniu.companion.provision.NativeFeatureStore.isHttps(value)) input.error = "请输入 HTTPS 地址"
                    else { preferences.edit().putString("native_feature_store_url", value).apply(); dialog.dismiss(); agentEditor?.refresh() }
                }}
            }.show()
    }

    private fun fetchNativeStore(url: String) {
        val editor = agentEditor ?: return
        val generation = directSession.current().generation
        ioExecutor.execute {
            try {
                val entries = com.shaniu.companion.provision.NativeFeatureStore.fetchIndex(url)
                mainHandler.post {
                    if (agentEditor === editor && generation == directSession.current().generation) showNativeStoreEntries(entries)
                }
            } catch (_: Exception) { mainHandler.post { lastAction = "无法读取 HTTPS 功能商店目录"; render() } }
        }
    }

    private fun showNativeStoreEntries(entries: List<com.shaniu.companion.provision.NativeFeatureStore.Entry>) {
        if (entries.isEmpty()) { AlertDialog.Builder(this).setTitle("功能商店").setMessage("商店目录暂时没有可安装的功能。").setPositiveButton("知道了", null).show(); return }
        val labels = entries.map { "${it.name} · v${it.version}\n${it.description}" }.toTypedArray()
        AlertDialog.Builder(this).setTitle("功能商店").setItems(labels) { _, which -> downloadNativeFeature(entries[which]) }.setNegativeButton("取消", null).show()
    }

    private fun downloadNativeFeature(entry: com.shaniu.companion.provision.NativeFeatureStore.Entry) {
        val editor = agentEditor ?: return
        val generation = directSession.current().generation
        ioExecutor.execute {
            try {
                val request = com.shaniu.companion.provision.NativeFeatureStore.installRequest(
                    com.shaniu.companion.provision.NativeFeatureStore.fetchPackage(entry.packageUrl, entry.sha256), entry)
                mainHandler.post {
                    if (agentEditor === editor && generation == directSession.current().generation) confirm("安装${entry.name}", "将下载的已校验功能包发送给设备验签并安装。\n申请权限：" +
                        com.shaniu.companion.provision.NativeFeatureStore.permissionLabels(com.shaniu.companion.provision.NativeFeatureStore.requestPermissions(request))) { editor.apply(request) }
                }
            } catch (_: Exception) { mainHandler.post { lastAction = "功能包下载或格式校验失败"; render() } }
        }
    }

    private fun selectAgentImport(publicKey: Boolean) {
        val editor = agentEditor ?: return
        val generation = directSession.current().generation
        agentImportSelected = { uri ->
            agentImportSelected = null
            if (agentEditor === editor && generation == directSession.current().generation) {
                try {
                    val native = editor.kind() == 28
                    val bytes = contentResolver.openInputStream(uri)?.use { readPcBytes(it, if (publicKey) 256 else if (native) 24576 else 4095) }
                        ?: error("无法读取文件")
                    if (publicKey && native) com.shaniu.companion.provision.NativeFeatureStore.validatePublisherKey(bytes)
                    val request = if (publicKey) com.google.gson.JsonObject().apply {
                        addProperty("op", "trust")
                        addProperty("public_key", java.util.Base64.getEncoder().encodeToString(bytes))
                    } else if (native) com.shaniu.companion.provision.NativeFeatureStore.installRequest(bytes) else com.google.gson.JsonParser.parseString(String(bytes, Charsets.UTF_8)).asJsonObject.also {
                        require(it.keySet() == setOf("manifest", "signature"))
                        it.addProperty("op", "install")
                    }
                    val detail = if (publicKey) (if (native) "公钥必须匹配固件指定的第一方发布者，且设备上没有已安装的原生功能。\nSHA-256：\n" else "仅信任你确认的发布者。更换公钥将清除已安装的外部技能。\nSHA-256：\n") +
                        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it.toInt() and 255) }
                    else if (native) "设备将验证原生包签名、ELF 完整性与声明的权限。安装或更新后默认为停止状态。\n申请权限：" +
                        com.shaniu.companion.provision.NativeFeatureStore.permissionLabels(com.shaniu.companion.provision.NativeFeatureStore.requestPermissions(request)) else "设备将验证签名和权限范围。安装成功后默认停用，需单独启用。"
                    confirm(if (publicKey) if (native) "信任原生功能发布者" else "信任技能发布者" else if (native) "安装原生功能" else "安装技能", detail) {
                        if (agentEditor === editor && generation == directSession.current().generation && !editor.apply(request)) {
                            lastAction = "设备忙，请重新读取技能状态"; render()
                        }
                    }
                } catch (_: Exception) {
                    AlertDialog.Builder(this).setTitle("无法导入")
                        .setMessage(if (publicKey) "请选择 P-256 发布者公钥文件（DER 格式）。" else if (editor.kind() == 28) "请选择完整的原生功能包。" else "请选择完整的签名技能包。")
                        .setPositiveButton("知道了", null).show()
                }
            }
        }
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
        }, AGENT_IMPORT_REQUEST)
    }

    private fun pcDeliveryFile(device: String): android.util.AtomicFile {
        val name = java.security.MessageDigest.getInstance("SHA-256").digest(device.toByteArray())
            .joinToString("") { "%02x".format(it.toInt() and 255) }
        return android.util.AtomicFile(java.io.File(filesDir, "pc-pair-$name.spd"))
    }
    private fun readPcBytes(input: java.io.InputStream, limit: Int): ByteArray {
        val bytes = ByteArray(limit + 1); var count = 0
        while (count < bytes.size) {
            val n = input.read(bytes, count, bytes.size - count)
            if (n < 0) break
            require(n > 0); count += n
        }
        require(count in 1..limit)
        return bytes.copyOf(count)
    }

    fun showPcAuthorization() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        val device = provisionedDeviceId
        if (pcReceiptDevice != device) { pcReceiptDevice = device; pcReceiptTransaction = null; pcReceiptTarget = null }
        showCompanionSheet("电脑授权", "由你决定，谁可以和傻妞协作。", done = false, onClosed = {
            pcConfirmation?.dismiss(); pcConfirmation = null
            pcEditor?.close(); pcEditor = null
            pcRequestSelected = null; pcResponseSelected = null
        }) { body, dialog ->
            val page = CompanionPage(this, body)
            val status = TextView(this).apply {
                textSize = 16f; setTextColor(design.accent)
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            body.addView(status)
            val details = TextView(this).apply {
                textSize = 14f; setTextColor(design.ink); setPadding(0, dp(12), 0, dp(12))
            }
            body.addView(details)
            val deliveryFile = pcDeliveryFile(device)
            var delivery = runCatching {
                if (deliveryFile.baseFile.exists()) com.shaniu.companion.provision.PcPairingDelivery.decode(
                    deliveryFile.openRead().use { readPcBytes(it, 8716) }) else null
            }.getOrNull()
            var preparingPair = false
            if (delivery == null && deliveryFile.baseFile.exists()) page.notice("本机配对记录无法读取；不会重发授权。可读取设备状态后明确移除本机记录。")
            delivery?.let { pcReceiptTransaction = it.transaction; pcReceiptTarget = it.target }
            page.notice("从电脑导入配对请求，核对摘要和权限后确认。设备保存授权并回读确认后，才能导出加密响应。关闭本页不会撤回已提交的操作。")
            fun button(label: String, action: () -> Unit): com.google.android.material.button.MaterialButton {
                page.primaryButton(label, false, action)
                return (body.getChildAt(body.childCount - 1) as com.google.android.material.button.MaterialButton).apply {
                    setTextColor(design.ink); backgroundTintList = android.content.res.ColorStateList.valueOf(design.selected)
                }
            }
            val importRequest = button("导入电脑配对请求") {
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                    type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
                }, PC_PAIR_REQUEST)
            }
            val export = button("导出加密配对响应") {
                startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                    type = "application/octet-stream"; addCategory(Intent.CATEGORY_OPENABLE)
                    putExtra(Intent.EXTRA_TITLE, "shaniu-pc-response.spr")
                }, PC_PAIR_RESPONSE)
            }
            button("复制设备证书摘要") {
                val identity = directSession.current().peerIdentity
                if (directSession.current().authenticated && identity != null) {
                    (getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager)
                        .setPrimaryClip(android.content.ClipData.newPlainText("设备证书 SHA256", identity.sha256))
                    status.text = "已复制当前已认证设备的证书摘要，请在电脑导入时核对"
                }
            }.apply { isEnabled = true; alpha = 1f }
            button("移除本机配对记录") {
                android.app.AlertDialog.Builder(this).setTitle("移除本机记录？")
                    .setMessage("这不会撤销设备上的电脑授权。移除后不能用本机记录重新导出响应；授权结果未知时请先查询。")
                    .setNegativeButton("保留", null).setPositiveButton("移除记录") { _, _ ->
                        if (preparingPair || pcEditor?.current()?.busy != false) return@setPositiveButton
                        deliveryFile.delete(); delivery = null
                        pcReceiptTransaction = null; pcReceiptTarget = null
                        dialog.dismiss(); showPcAuthorization()
                    }.show()
            }.apply { isEnabled = true; alpha = 1f }
            pcRequestSelected = requestSelected@{ uri ->
                val controller = pcEditor ?: return@requestSelected
                if (preparingPair || deliveryFile.baseFile.exists()) { status.text = "已有配对记录，请先查询或明确移除"; return@requestSelected }
                val identity = directSession.current().peerIdentity ?: return@requestSelected
                val expected = controller.current().snapshot ?: return@requestSelected
                val generation = directSession.current().generation
                status.text = "正在读取配对请求"
                ioExecutor.execute {
                    val request = runCatching { contentResolver.openInputStream(uri)!!.use {
                        com.shaniu.companion.provision.PcPairingExchange.parse(readPcBytes(it, 1024), System.currentTimeMillis())
                    } }
                    mainHandler.post {
                        if (pcEditor !== controller || provisionedDeviceId != device || directSession.current().generation != generation) return@post
                        val value = request.getOrNull()
                        if (value == null) { status.text = "配对请求无效或已过期，设备未变更"; return@post }
                        val permissions = listOf(1 to "资源管理", 2 to "场景", 4 to "任务提醒", 8 to "有限诊断", 16 to "按需相机（可传回一帧画面）")
                            .filter { value.capabilities and it.first != 0 }.joinToString("、") { it.second }
                        pcConfirmation?.dismiss()
                        pcConfirmation = android.app.AlertDialog.Builder(this)
                            .setTitle("允许这台电脑连接？")
                            .setMessage("请与电脑显示的请求摘要逐项核对：\n${value.fingerprint}\n允许：$permissions\n当前已有授权将被替换。")
                            .setNegativeButton("取消", null).setPositiveButton("摘要一致，确认授权") { _, _ ->
                                if (pcEditor !== controller || !directSession.current().authenticated || directSession.current().generation != generation ||
                                    directSession.current().peerIdentity?.sha256 != identity.sha256 || controller.current().snapshot != expected) {
                                    status.text = "连接或授权状态已变化，请重新读取"; return@setPositiveButton
                                }
                                if (preparingPair) return@setPositiveButton
                                preparingPair = true
                                importRequest.isEnabled = false
                                ioExecutor.execute {
                                    val prepared = runCatching {
                                        com.shaniu.companion.provision.PcPairingDelivery.prepare(value, identity, expected, System.currentTimeMillis())
                                    }.getOrNull()
                                    val saved = prepared != null && runCatching {
                                        check(!deliveryFile.baseFile.exists())
                                        val bytes = prepared.delivery.encode(); val output = deliveryFile.startWrite()
                                        try { output.write(bytes); output.fd.sync(); deliveryFile.finishWrite(output) }
                                        catch (error: Exception) { deliveryFile.failWrite(output); throw error }
                                        check(deliveryFile.openRead().use { readPcBytes(it, 8716) }.contentEquals(bytes))
                                        true
                                    }.getOrDefault(false)
                                    mainHandler.post {
                                        preparingPair = false
                                        if (prepared == null) { if (pcEditor === controller) status.text = "无法准备配对响应，设备未变更"; return@post }
                                        try {
                                            if (!saved || pcEditor !== controller || provisionedDeviceId != device ||
                                                !directSession.current().authenticated || directSession.current().generation != generation ||
                                                directSession.current().peerIdentity?.sha256 != identity.sha256 || System.currentTimeMillis() >= value.expiresAtMs) {
                                                if (pcEditor === controller) status.text = "配对准备未确认；请读取已保存记录，不会自动重发"
                                                return@post
                                            }
                                            delivery = prepared.delivery
                                            if (!prepared.submit(controller, System.currentTimeMillis()) { it.encode().contentEquals(prepared.delivery.encode()) })
                                                status.text = "授权未发出，请查询或重新读取；已保存记录不会自动重发"
                                        } finally { prepared.close() }
                                    }
                                }
                            }.create().also { it.show() }
                    }
                }
            }
            pcResponseSelected = responseSelected@{ uri ->
                val controller = pcEditor ?: return@responseSelected
                val state = directSession.current()
                val bytes = delivery?.response(controller.current(), state.peerIdentity.takeIf { state.authenticated }, System.currentTimeMillis())
                if (bytes == null) { status.text = "授权未确认、连接已变化或请求过期，请先查询"; return@responseSelected }
                ioExecutor.execute {
                    val saved = runCatching { contentResolver.openOutputStream(uri, "wt")!!.use { it.write(bytes); it.flush() } }.isSuccess
                    mainHandler.post { if (pcEditor === controller) status.text = if (saved) "加密响应已导出；电脑仍需完成导入与设备鉴权" else "导出未确认，可在有效期内重试" }
                }
            }
            val reload = button("读取授权状态") { pcEditor?.refresh() }
            val query = button("查询上次操作") { pcEditor?.query() }
            val revoke = button("撤销电脑授权") {
                val controller = pcEditor ?: return@button
                val expected = controller.current().snapshot ?: return@button
                pcConfirmation?.dismiss()
                pcConfirmation = android.app.AlertDialog.Builder(this)
                    .setTitle("撤销这台电脑的授权？")
                    .setMessage("电脑标识：${expected.client}\n设备确认保存后，这台电脑将不能继续访问傻妞。")
                    .setNegativeButton("保留授权", null)
                    .setPositiveButton("确认撤销") { _, _ ->
                        if (pcEditor === controller && provisionedDeviceId == device && !controller.revoke(expected)) {
                            status.text = "状态已变化或设备正忙，请重新读取后确认"
                        }
                    }.create().also { it.show() }
            }
            button("返回") { dialog.dismiss() }.apply { isEnabled = true; alpha = 1f }
            pcEditor = com.shaniu.companion.provision.PcAuthorizationController(directSession,
                resumeTransaction = pcReceiptTransaction, resumeTarget = pcReceiptTarget, changed = { state ->
                    pcReceiptDevice = device; pcReceiptTransaction = state.transaction; pcReceiptTarget = state.target
                    status.text = state.message
                    val value = state.snapshot
                    details.text = when {
                        value == null -> "当前授权：尚未确认"
                        !value.active -> "当前没有有效电脑授权"
                        else -> "电脑标识：${value.client}\n允许：" + listOf(1 to "资源管理", 2 to "场景", 4 to "任务提醒", 8 to "有限诊断", 16 to "按需相机（可传回一帧画面）")
                            .filter { value.capabilities and it.first != 0 }.joinToString("、") { it.second }
                    }
                    val ready = !state.busy && directSession.current().authenticated
                    fun enable(view: View, enabled: Boolean) { view.isEnabled = enabled; view.alpha = if (enabled) 1f else 0.45f }
                    enable(importRequest, ready && !preparingPair && value != null && directSession.current().peerIdentity != null && !deliveryFile.baseFile.exists() &&
                        state.outcome !in listOf(com.shaniu.companion.provision.PcAuthorizationController.Outcome.PENDING, com.shaniu.companion.provision.PcAuthorizationController.Outcome.UNKNOWN))
                    enable(export, delivery?.response(state, directSession.current().peerIdentity, System.currentTimeMillis()) != null)
                    enable(reload, ready)
                    enable(query, ready && state.transaction != null)
                    enable(revoke, ready && value?.active == true && state.outcome !in listOf(
                        com.shaniu.companion.provision.PcAuthorizationController.Outcome.PENDING,
                        com.shaniu.companion.provision.PcAuthorizationController.Outcome.UNKNOWN))
                })
            if (pcReceiptTransaction != null) pcEditor?.query() else pcEditor?.refresh()
        }
    }

    fun showNfcBindings() {
        if (!configAvailable() || settingsEditor != null || factoryReset != null) return
        showCompanionSheet("场景卡片", "为兼容卡片选择一个明确操作。", done = false, onClosed = {
            nfcDraftCapture?.invoke(); nfcDraftCapture = null
            nfcEditor?.close(); nfcEditor = null
        }) { body, dialog ->
            val page = CompanionPage(this, body)
            val status = TextView(this).apply {
                textSize = 16f; setTextColor(design.accent)
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            body.addView(status)
            page.sectionTitle("保存位置")
            var selectionChanged: (() -> Unit)? = null
            val slots = android.widget.Spinner(this).apply {
                contentDescription = "卡片保存位置"; minimumHeight = dp(56)
                adapter = android.widget.ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item,
                    (1..8).map { "卡片 $it" })
                setSelection(nfcSlotDraft)
                onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                    override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                        nfcSlotDraft = position; selectionChanged?.invoke()
                    }
                    override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                }
            }
            body.addView(slots, LinearLayout.LayoutParams(-1, -2))
            page.sectionTitle("专注分钟数")
            val minutes = EditText(this).apply {
                contentDescription = "卡片专注分钟数"; hint = "例如 25"; setText(nfcMinutesDraft)
                textSize = 16f; minimumHeight = dp(56); setTextColor(design.ink)
                inputType = android.text.InputType.TYPE_CLASS_NUMBER
                addTextChangedListener(object : android.text.TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) { nfcMinutesDraft = s?.toString().orEmpty() }
                    override fun afterTextChanged(s: android.text.Editable?) = Unit
                })
            }
            body.addView(minutes, LinearLayout.LayoutParams(-1, -2))
            page.sectionTitle("原生功能")
            val nativeApp = EditText(this).apply {
                contentDescription = "卡片启动的原生功能 ID"; hint = "功能 ID，例如 shake"
                textSize = 16f; minimumHeight = dp(56); setTextColor(design.ink)
                inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            }
            val nativeSeconds = EditText(this).apply {
                contentDescription = "原生功能运行秒数"; hint = "运行秒数 1–3600，留空为功能默认"
                textSize = 16f; minimumHeight = dp(56); setTextColor(design.ink)
                inputType = android.text.InputType.TYPE_CLASS_NUMBER
            }
            val nativeTitle = body.getChildAt(body.childCount - 1)
            body.addView(nativeApp, LinearLayout.LayoutParams(-1, -2))
            body.addView(nativeSeconds, LinearLayout.LayoutParams(-1, -2))
            val sceneLabels = listOf("开始专注", "暂停专注", "继续专注", "取消专注", "播放四拍节律（4 秒）", "取消本地内容", "启动原生功能")
            val actions = android.widget.Spinner(this).apply {
                contentDescription = "卡片场景操作"
                adapter = android.widget.ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, sceneLabels)
                setSelection(nfcActionDraft)
                onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                    override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                        nfcActionDraft = position
                        minutes.visibility = if (position == 0) View.VISIBLE else View.GONE
                        val native = if (position == 6) View.VISIBLE else View.GONE
                        nativeTitle.visibility = native; nativeApp.visibility = native; nativeSeconds.visibility = native
                    }
                    override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                }
                minimumHeight = dp(48)
            }
            body.addView(actions, LinearLayout.LayoutParams(-1, -2))
            nfcDraftCapture = { nfcMinutesDraft = minutes.text.toString(); nfcSlotDraft = slots.selectedItemPosition.coerceIn(0, 7); nfcActionDraft = actions.selectedItemPosition.coerceIn(0, 6) }
            val saved = TextView(this).apply { textSize = 14f; setTextColor(design.ink); setPadding(0, dp(12), 0, dp(8)) }
            body.addView(saved)
            val sceneStatus = TextView(this).apply {
                textSize = 14f; setTextColor(design.ink)
                accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            body.addView(sceneStatus)
            page.notice("将兼容卡片贴近傻妞，再点登记；一次只放一张；驻留不重复触发，移开后才可再次触发。首次登记不自动执行。关闭页面不会取消设备已受理的作业。")
            fun button(label: String, primary: Boolean = false, action: () -> Unit): com.google.android.material.button.MaterialButton {
                page.primaryButton(label, false, action)
                return (body.getChildAt(body.childCount - 1) as com.google.android.material.button.MaterialButton).apply {
                    if (!primary) { setTextColor(design.ink); backgroundTintList = android.content.res.ColorStateList.valueOf(design.selected) }
                }
            }
            val reload = button("读取设备状态") { nfcEditor?.refresh() }
            val load = button("加载已保存卡片") { nfcEditor?.act(1) }
            val enroll = button("登记当前卡片", true) {
                val sceneAction = actions.selectedItemPosition + 1
                val count = minutes.text.toString().toLongOrNull()
                val appId = nativeApp.text.toString().trim()
                val seconds = nativeSeconds.text.toString().let { if (it.isBlank()) 0L else it.toLongOrNull() ?: -1L }
                if (sceneAction == 1 && (count == null || count <= 0 || count > Long.MAX_VALUE / 60000)) {
                    minutes.error = "请输入有效分钟数"; minutes.requestFocus()
                } else if (sceneAction == 7 && !appId.matches(Regex("[a-z0-9][a-z0-9-]{0,30}"))) {
                    nativeApp.error = "请输入设备上已安装功能的 ID"; nativeApp.requestFocus()
                } else if (sceneAction == 7 && seconds !in 0..3600) {
                    nativeSeconds.error = "请输入 1–3600 秒，或留空"; nativeSeconds.requestFocus()
                } else {
                    minutes.error = null; nativeApp.error = null; nativeSeconds.error = null
                    val argument = when (sceneAction) {
                        1 -> count!! * 60000
                        5 -> 1L
                        7 -> com.shaniu.companion.provision.NfcBindingController.nativeArgument(appId, seconds)
                        else -> 0L
                    }
                    nfcEditor?.act(2, slots.selectedItemPosition, argument, sceneAction)
                }
            }
            val remove = button("删除此卡绑定") {
                val slot = slots.selectedItemPosition
                android.app.AlertDialog.Builder(this)
                    .setTitle("删除卡片 ${slot + 1} 的绑定？")
                    .setMessage("只删除这个保存位置，其他卡片不受影响。")
                    .setNegativeButton("保留", null)
                    .setPositiveButton("删除绑定") { _, _ -> nfcEditor?.act(3, slot) }.show()
            }
            selectionChanged = {
                val state = nfcEditor?.current()
                val value = state?.snapshot
                remove.isEnabled = state?.busy == false && directSession.current().authenticated &&
                    value?.phase == 4 && value.actions[slots.selectedItemPosition] > 0
                remove.alpha = if (remove.isEnabled) 1f else 0.45f
            }
            val cancel = button("取消设备作业") { nfcEditor?.act(4) }
            button("返回") { dialog.dismiss() }.apply { isEnabled = true; alpha = 1f }
            nfcEditor = com.shaniu.companion.provision.NfcBindingController(directSession) { state ->
                val value = state.snapshot
                status.text = state.message
                sceneStatus.text = state.sceneMessage
                saved.text = if (value?.phase == 4) value.durations.mapIndexed { index, duration ->
                    "卡片 ${index + 1}：" + when (val action = value.actions[index]) {
                        0 -> "未登记"
                        1 -> "开始专注 ${duration / 1000} 秒"
                        7 -> "启动原生功能 #%08x".format(duration ushr 32) +
                            ((duration and 0xffff_ffffL).let { if (it == 0L) "（默认时长）" else "（$it 秒）" })
                        else -> sceneLabels[action - 1]
                    }
                }.joinToString("\n") else "已保存卡片：尚未确认，请加载并回读"
                val ready = !state.busy && directSession.current().authenticated
                val terminal = value != null && value.phase in listOf(0, 4, 5, 6)
                fun enable(view: View, enabled: Boolean) { view.isEnabled = enabled; view.alpha = if (enabled) 1f else 0.45f }
                enable(reload, ready)
                enable(load, ready && terminal)
                enable(actions, ready && value?.extended == true)
                if (value?.extended == false) actions.setSelection(0)
                enable(enroll, ready && terminal && value?.phase != 0)
                enable(remove, ready && value?.phase == 4 && value.actions[slots.selectedItemPosition] > 0)
                enable(cancel, ready && value?.phase in 1..2)
                enroll.backgroundTintList = android.content.res.ColorStateList.valueOf(design.accent)
            }
            nfcEditor?.refresh()
        }
    }

    private fun restoreEyeDraft(saved: Bundle?) {
        val bytes = saved?.getByteArray("eye_pack_draft") ?: return
        val sha = saved.getByteArray("eye_pack_draft_sha256")
        if (bytes.size !in 128..131072 || sha?.size != 32) {
            eyeMessage = "保存的本地素材无效，请重新选择；设备未变更"
            return
        }
        val draft = bytes.copyOf() to sha.copyOf()
        // A second recreation can happen before asynchronous validation finishes.
        eyeRestoreDraft = draft
        loadEyePack({ draft.first.inputStream() }, draft.second)
    }

    private fun selectEyePack(uri: android.net.Uri) =
        loadEyePack({ contentResolver.openInputStream(uri) })

    private fun loadEyePack(open: () -> java.io.InputStream?, expectedSha: ByteArray? = null) {
        if (eyeImportPending) return
        eyeImportPending = true; eyeMessage = "正在检查眼睛素材包…"; render()
        ioExecutor.execute {
            val result = runCatching {
                val file = java.io.File.createTempFile("eyes-selected-", ".bkep", cacheDir)
                try {
                    open().use { input ->
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
                    val sha = MessageDigest.getInstance("SHA-256").digest(bytes)
                    require(expectedSha == null || MessageDigest.isEqual(sha, expectedSha))
                    val pack = requireNotNull(EyePack.parse(bytes))
                    check(ownImportedFile(file)) { "Activity destroyed while importing eyes" }
                    Triple(pack, file, sha)
                } catch (error: Exception) { discardImportedFile(file); throw error }
            }
            mainHandler.post {
                eyeImportPending = false
                eyeRestoreDraft = null
                // Finishing a local file read in background does not start a
                // device write. A destroyed Activity must release its own file.
                if (destroyed) { result.getOrNull()?.second?.let(::discardImportedFile); return@post }
                val accepted = result.getOrNull()
                if (accepted == null) eyeMessage = if (expectedSha != null)
                    "保存的本地素材无效，请重新选择；设备未变更"
                    else "眼睛素材包格式、长度或校验无效"
                else {
                    if (!claimImportedFile(accepted.second)) return@post
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

    internal fun startEyeInstall() {
        val file = selectedEyeFile ?: return
        selectedEyePack ?: return
        val assetSha256 = selectedEyeAssetSha256 ?: return
        if (!configAvailable() || eyeImportPending) return
        val epoch = directEpoch
        configFlow = ConfigFlow.EYES; eyeCanceling = false; eyeApplySent = false
        eyeMessage = "正在准备 Wi-Fi 眼睛素材来源…"; render()
        val source = OtaSourceLease<OtaPackageServer> { server -> ioExecutor.execute { server.close() } }
        eyeSource = source
        ioExecutor.execute {
            val opened = runCatching { OtaPackageServer.openAsset(applicationContext, file, assetSha256) }
            val server = opened.getOrNull()
            if (server != null && !source.publish(server)) {
                server.close()
                return@execute
            }
            mainHandler.post {
                if (eyeSource !== source) return@post
                if (epoch != directEpoch || !foreground || destroyed || configFlow != ConfigFlow.EYES) {
                    source.close()
                    if (eyeSource === source) eyeSource = null
                    return@post
                }
                if (server == null) { finishEyeInstall(otaSourceOpenFailureMessage(opened.exceptionOrNull())); render(); return@post }
                if (server.requestRecord.size !in 44..3371) {
                    finishEyeInstall("本机眼睛素材来源请求长度无效"); render(); return@post
                }
                eyeRecord = server.requestRecord; eyeOffset = 0; eyeRead = ByteArray(0)
                if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_BEGIN,
                        ByteBuffer.allocate(8).putInt(5).putInt(server.requestRecord.size).array()))
                    finishEyeInstall("设备控制通道不可用，未开始眼睛安装")
                else eyeMessage = "正在发送 Wi-Fi 素材来源…"
                render()
            }
        }
    }

    private fun requestEyeRead(offset: Int = 0) {
        if (!foreground) {
            eyeReadDeferred = true
            eyeMessage = "眼睛安装已提交；返回后读取设备实际状态"
            return
        }
        eyeReadDeferred = false
        if (!directConfigRequest(DeviceControlProtocol.Command.CONFIG_READ,
                ByteBuffer.allocate(4).putInt((5 shl 16) or offset).array()))
            finishEyeInstall("无法读取设备当前眼睛状态")
    }

    internal fun readCurrentEyes() {
        if (!configAvailable()) return
        configFlow = ConfigFlow.EYES; eyeReadingOnly = true; eyeCanceling = false; eyeApplySent = false
        eyeOffset = 0; eyeRead = ByteArray(0)
        eyeMessage = "正在读取设备当前眼睛…"
        requestEyeRead(); render()
    }

    private fun finishEyeInstall(message: String) {
        eyeRecord = null; eyeOffset = 0; eyeRead = ByteArray(0); eyeReadingOnly = false
        eyeCanceling = false; eyeApplySent = false; eyeReadDeferred = false
        closeEyeServer()
        if (configFlow == ConfigFlow.EYES) {
            configFlow = ConfigFlow.NONE
            directSession.finishConfigTransaction(message)
        }
        eyeMessage = message
    }

    /** Stops a pre-apply install without releasing local ownership before the
     * board has acknowledged cancellation.  APPLY is a committed request: it
     * is left to complete or time out and is never presented as cancelled. */
    private fun leaveEyeInstallForBackground() {
        if (configFlow != ConfigFlow.EYES) return
        if (eyeReadingOnly) {
            finishEyeInstall("已离开前台；已停止读取眼睛状态")
            return
        }
        if (eyeApplySent) {
            eyeMessage = "眼睛安装已提交；返回后读取设备实际状态"
            return
        }
        when (directSession.cancelConfigTransaction()) {
            DeviceControlSession.ConfigCancellation.REMOTE_PENDING -> {
                eyeCanceling = true
                eyeMessage = "正在取消未完成的眼睛安装…"
            }
            DeviceControlSession.ConfigCancellation.LOCAL_CANCELLED,
            DeviceControlSession.ConfigCancellation.NOT_ACTIVE ->
                finishEyeInstall("已离开前台；未开始的眼睛安装已取消")
        }
    }

    private fun handleEyeResult(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (command == DeviceControlProtocol.Command.CONFIG_CANCEL) {
            if (snapshot.error == 0) finishEyeInstall("未完成的眼睛安装已取消")
            else {
                // The board may still own staging. End this session so the
                // next connection reads actual state instead of reusing it.
                eyeMessage = "眼睛安装取消未确认（${snapshot.error}）；连接已结束，请重新读取设备状态"
                directSession.disconnect(user = false)
            }
            return
        }
        if (eyeCanceling) return
        if (snapshot.error != 0) { finishEyeInstall("眼睛安装未确认（${snapshot.error}）；请读取设备实际状态"); return }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_BEGIN, DeviceControlProtocol.Command.CONFIG_APPEND -> {
                val record = eyeRecord ?: run { finishEyeInstall("眼睛安装请求已取消"); return }
                if (eyeOffset < record.size) {
                    val end = minOf(record.size, eyeOffset + configAppendMax)
                    if (directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPEND, record.copyOfRange(eyeOffset, end))) eyeOffset = end
                    else finishEyeInstall("设备忙，眼睛安装请求未完成")
                } else if (directConfigRequest(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))) {
                    eyeApplySent = true
                } else finishEyeInstall("设备忙，未开始 Wi-Fi 安装")
            }
            DeviceControlProtocol.Command.CONFIG_APPLY -> {
                eyeOffset = 0; eyeRead = ByteArray(0)
                requestEyeRead()
            }
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
                    val metadata = BkpackInspector.inspect(file)
                    check(ownImportedFile(file)) { "Activity destroyed while importing firmware" }
                    metadata to file
                } catch (error: Exception) {
                    discardImportedFile(file)
                    throw error
                }
            }
            mainHandler.post {
                val accepted = inspected.getOrNull()
                if (accepted != null && !claimImportedFile(accepted.second)) return@post
                finishFirmwareInspection(epoch, accepted, inspected.isFailure)
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

    internal fun connect(
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

    internal fun refreshSnapshot() {
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

    internal fun fetchReleases() {
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

    internal fun submitMutation(
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

    // Public: the UI acceptance probe opens this sheet reflectively.
    fun confirmFactoryReset() {
        if (provisionedDeviceId.isBlank() || !directSession.current().authenticated || factoryReset != null) return
        if (!directSession.current().snapshotFresh) {
            directMessage = "设备状态已过期，请刷新后再试"
            render()
            return
        }
        val expectedDevice = provisionedDeviceId
        confirm(
            title = "准备交给新主人？",
            message = "这会撤销当前控制权限，并清除傻妞上的网络、云服务、记忆、偏好和显示选择。完成后必须重新扫描设备二维码认领。本机旧控制凭据会保留到设备回执明确确认完成。",
            consent = "我理解需要重新扫码认领和配网",
        ) {
            if (expectedDevice != provisionedDeviceId || !directSession.current().authenticated || factoryReset != null)
                return@confirm
            if (!directSession.current().snapshotFresh) {
                directMessage = "设备状态已过期，请刷新后再试"
                render()
                return@confirm
            }
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

    internal fun queryFactoryReset() {
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

    internal fun confirmClearProvisioning() {
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

    internal fun closeRuntime(
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

    internal fun canMutate(): Boolean =
        runtime != null && eventConnected && !busy && store?.state?.needsSnapshot == false

    internal fun requireReportedState(action: String = "查看和调整设置"): CompanionState? {
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

    internal fun startProvisioning(developerMode: Boolean = false) {
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

    internal fun startConsoleEnrollmentImport() {
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

    internal fun expectedControlDeviceId(): String =
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

    internal val design get() = CompanionDesign(this)
    private val BACKGROUND get() = design.background
    internal val INK get() = design.ink
    internal val MUTED get() = design.muted

    override fun onSaveInstanceState(outState: Bundle) {
        nfcDraftCapture?.invoke()
        val eyeDraft = selectedEyePack?.let { pack ->
            selectedEyeAssetSha256?.let { pack.bytes to it }
        } ?: eyeRestoreDraft
        eyeDraft?.let { (bytes, sha) ->
            // EyePack admission bounds this to 128 KiB; no credential or URI
            // grant is stored, and no install/HTTP state is restored.
            outState.putByteArray("eye_pack_draft", bytes.copyOf())
            outState.putByteArray("eye_pack_draft_sha256", sha.copyOf())
        }
        outState.putString("trial_seconds_draft", trialSecondsDraft)
        outState.putInt("trial_expression_draft", trialExpressionDraft)
        outState.putBoolean("trial_pack_draft", trialPackDraft)
        outState.putString("focus_minutes_draft", focusMinutesDraft)
        outState.putString("pc_receipt_device", pcReceiptDevice)
        outState.putString("pc_receipt_transaction", pcReceiptTransaction)
        pcReceiptTarget?.let {
            outState.putString("pc_receipt_client", it.client)
            outState.putString("pc_receipt_revision", it.revision.toString())
            outState.putInt("pc_receipt_caps", it.capabilities)
        }
        outState.putString("nfc_minutes_draft", nfcMinutesDraft)
        outState.putInt("nfc_slot_draft", nfcSlotDraft)
        outState.putInt("nfc_action_draft", nfcActionDraft)
        outState.putInt("navigation", currentTab)
        outState.putInt("expression_preview", expressionPreview)
        outState.putBoolean("update_resources", updateResources)
        outState.putIntegerArrayList("detail_stack", ArrayList(detailStack))
        super.onSaveInstanceState(outState)
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        super.onConfigurationChanged(newConfig)
        // Theme/window changes must not destroy the foreground package server or
        // reconnect the authenticated device. Existing sensitive drafts stay
        // in their editor until the user closes it.
        theme.applyStyle(R.style.Theme_Shaniu, true)
        window.statusBarColor = BACKGROUND
        window.navigationBarColor = BACKGROUND
        window.decorView.systemUiVisibility = if (design.dark) 0 else
            View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        val scroll = contentScroll.scrollY
        navigation.clear()
        setContentView(buildRoot().apply { applySystemInsets() })
        if (cloudEditorEmbedded) settingsEditor?.reattach(cloudEditorHost)
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
        internal const val KEY_OTA_EXPECTED_PENDING = "ota_expected_pending"
        private const val DEVICE_BOARD = "aidk_ai_toy"
        private const val PROVISION_REQUEST = 41
        private const val CONSOLE_ENROLLMENT_REQUEST = 42
        private const val MUTATION_TTL_MS = 30_000L
        private const val RECONNECT_BUSY_WAIT_MS = 500L
        private val RECONNECT_DELAYS_MS = longArrayOf(1_000L, 2_000L, 4_000L, 8_000L, 16_000L, 30_000L)

        private const val MAX_CONTENT_WIDTH_DP = 560
        private const val RAIL_MIN_WIDTH_DP = 600
        internal const val TAB_OVERVIEW = 0
        internal const val TAB_INTERACTION = 1
        internal const val TAB_PERSONALITY = 2
        internal const val TAB_PRIVACY = 3
        internal const val TAB_UPDATE = 4
        internal const val FIRMWARE_PACKAGE_REQUEST = 6043
        internal const val WAKE_MODEL_REQUEST = 6044
        internal const val EYE_PACK_REQUEST = 6045
        private const val PC_PAIR_REQUEST = 6046
        private const val PC_PAIR_RESPONSE = 6047
        private const val AGENT_IMPORT_REQUEST = 6048
        internal const val TAB_SETTINGS = 5
        internal const val TAB_SERVICES = 6
        internal const val TAB_RESOURCES = 7
        internal const val TAB_PERSONA = 8
        internal const val TAB_ADVANCED = 9
        private val TABS = listOf(TAB_OVERVIEW to "设备", TAB_PERSONALITY to "定制", TAB_UPDATE to "更新", TAB_SETTINGS to "设置")
        private val TOP_LEVEL_TABS = setOf(TAB_OVERVIEW, TAB_PERSONALITY, TAB_UPDATE, TAB_SETTINGS)
        private val DETAIL_TABS = setOf(TAB_SERVICES, TAB_ADVANCED, TAB_RESOURCES, TAB_PERSONA, TAB_PRIVACY)
        /** Parent used when a detail page is restored without its path. */
        private fun defaultParent(tab: Int) = when (tab) {
            TAB_RESOURCES, TAB_PERSONA, TAB_ADVANCED -> TAB_PERSONALITY
            else -> TAB_SETTINGS
        }
    }
}
