// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.shaniu.companion.applySystemInsets
import com.shaniu.companion.installSystemBack
import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.text.InputType
import android.util.Log
import android.view.WindowManager
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** Explicit foreground onboarding. No background discovery, automatic claiming,
 * saved password or configuration success inferred from a disconnected link.
 */
@SuppressLint("MissingPermission")
class ProvisionActivity : Activity() {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var form: LinearLayout
    private lateinit var status: TextView
    private lateinit var devices: LinearLayout
    private lateinit var ssid: EditText
    private lateinit var password: EditText
    private lateinit var cloudUrl: EditText
    private lateinit var cloudKey: EditText
    private lateinit var asrModel: EditText
    private lateinit var chatModel: EditText
    private lateinit var ttsModel: EditText
    private lateinit var cloudDialect: android.widget.Spinner
    private var cloudResolving = false
    private var cloudLookupGeneration = 0L
    private lateinit var host: EditText
    private lateinit var address: EditText
    private lateinit var port: EditText
    private lateinit var discoveryPage: LinearLayout
    private lateinit var networkPage: LinearLayout
    private lateinit var resultPage: LinearLayout
    private lateinit var nextButton: Button
    private lateinit var activationButton: Button
    private lateinit var scanButton: Button
    private lateinit var wifiScanButton: Button
    private lateinit var wifiCancelButton: Button
    private lateinit var wifiNetworks: LinearLayout
    private lateinit var nfcButton: Button
    private lateinit var nfcStatus: TextView
    private var nfcRequested = false
    private var nfcResumed = false
    private var nfcSeen = false
    private var nfcLocator: ByteArray? = null
    private val nfcTimeout = Runnable {
        stopNfc()
        nfcStatus.text = "碰一碰已暂停，可以重试或使用蓝牙查找。"
    }
    private lateinit var stopButton: Button
    private lateinit var stepLabel: TextView
    private lateinit var titleLabel: TextView
    private lateinit var resultButton: Button
    private lateinit var bindingStore: ProvisionBindingStore
    private var page = 0
    private var developerMode = false
    private var bootstrap: ProvisionBootstrap? = null
    private var ca: ByteArray? = null
    private var selected: BluetoothDevice? = null
    private var connection: ProvisioningConnection? = null
    private var cloudResolver: (String) -> CloudEndpoint.Verified = CloudEndpoint::resolve
    private var connectionFactory: (BluetoothDevice, ProvisionBootstrap, ByteArray,
        (ProvisionClaimProtocol.State) -> Unit, Boolean) -> ProvisioningConnection =
        { device, identity, candidate, changed, recover ->
            ProvisioningConnection(this, device, identity, candidate, changed, recover = recover)
        }
    private var wifiScan: WifiScanConnection? = null
    private var wifiScanGeneration = 0L
    private var recoveryControl: AutoCloseable? = null
    private var scanning = false
    private var outcomeUnknown = false
    private var epoch = 0L
    private var alive = true
    private var foreground = false
    private var bindingStateAvailable = true
    private val found = mutableSetOf<String>()
    private val scanner get() = getSystemService(BluetoothManager::class.java)
        ?.adapter?.bluetoothLeScanner
    private val stopScan = Runnable {
        stopDiscovery()
        if (alive) status.text = if (found.isEmpty())
            "没有找到傻妞。请把未认领设备放在手机旁并保持通电，再试一次。App 只会继续验证可认领设备。"
        else "扫描结束，请选择设备或重新扫描。"
    }
    private val callback = object : ScanCallback() {
        override fun onScanResult(type: Int, result: ScanResult) {
            handler.post {
                if (!scanning || !alive) return@post
                val hint = nfcLocator
                if (hint != null && result.scanRecord?.getServiceData(ParcelUuid.fromString(
                    "81e70001-9b31-4c48-9c62-e6da4b392531"))?.contentEquals(hint) != true) return@post
                if (!found.add(result.device.address)) return@post
                devices.addView(Button(this@ProvisionActivity).apply {
                    text = "选择 ${result.scanRecord?.deviceName ?: "傻妞设备"} · ${result.device.address.takeLast(5)}"
                    setOnClickListener {
                        selected = result.device
                        stopDiscovery()
                        nextButton.isEnabled = bindingStateAvailable
                        nextButton.visibility = View.VISIBLE
                        status.text = if (bindingStateAvailable) "已选择设备，点击下一步继续。"
                            else "本机认领状态无法读取；为防止重复提交，已停止认领。"
                    }
                })
            }
        }
        override fun onScanFailed(code: Int) {
            handler.post { stopDiscovery(); status.text = "扫描失败，请检查蓝牙是否开启。" }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        bindingStore = ProvisionBindingStore(applicationContext)
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        window.statusBarColor = BACKGROUND
        window.navigationBarColor = BACKGROUND
        window.isStatusBarContrastEnforced = false
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        developerMode = intent.getBooleanExtra("developer_mode", false) &&
            (applicationInfo.flags and android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
        form = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BACKGROUND)
            setPadding(dp(24), dp(12), dp(24), dp(24))
        }
        setContentView(ScrollView(this).apply { addView(form); applySystemInsets() })
        installSystemBack { goBack() }
        text("‹ 返回", 16).apply {
            minHeight = dp(48); isClickable = true; isFocusable = true
            setOnClickListener { goBack() }
        }
        stepLabel = text("01 找到设备   ·   02 连接 Wi-Fi   ·   03 确认", 12)
        titleLabel = text("添加傻妞", 28)
        status = text("把未认领的傻妞放在手机旁并保持通电。App 会自动查找设备并验证所有权。", 15)
        discoveryPage = section()
        discoveryPage.addView(com.shaniu.companion.CompanionPortraitView(this),
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(190)))
        button("扫描傻妞屏幕上的认领码", discoveryPage) {
            if (connection == null) {
                stopDiscovery()
                com.google.zxing.integration.android.IntentIntegrator(this)
                    .setDesiredBarcodeFormats(com.google.zxing.integration.android.IntentIntegrator.QR_CODE)
                    .setPrompt("扫描设备屏幕认领码，不要分享二维码截图")
                    .setCaptureActivity(ClaimCaptureActivity::class.java)
                    .setBeepEnabled(false).setOrientationLocked(false)
                    .setBarcodeImageEnabled(false).initiateScan()
            }
        }.apply { primaryStyle() }
        scanButton = button("查找附近的傻妞", discoveryPage) { stopNfc(); discover() }.apply { primaryStyle() }
        nfcButton = button("碰一碰查找", discoveryPage) { beginNfc() }
        nfcStatus = text("", 13, discoveryPage).apply { visibility = View.GONE }
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_NFC_HOST_CARD_EMULATION)) {
            nfcButton.visibility = View.GONE
        }
        devices = section(discoveryPage)
        nextButton = button("下一步", discoveryPage) { nextPage() }.apply { isEnabled = false; visibility = View.GONE }
        stopButton = button("停止查找", discoveryPage) {
            stopNfc(); stopDiscovery(); status.text = "已停止查找，可以重新开始。"
        }.apply { visibility = View.GONE }
        networkPage = section()
        text("输入傻妞要使用的 Wi-Fi 名称和密码。", 15, networkPage)
        ssid = field("Wi-Fi 名称", target = networkPage)
        password = field("Wi-Fi 密码", secret = true, target = networkPage)
        wifiScanButton = button("扫描设备附近的 Wi-Fi", networkPage) { scanWifi() }
        wifiCancelButton = button("取消 Wi-Fi 扫描", networkPage) { cancelWifiScan() }.apply {
            visibility = View.GONE
        }
        wifiNetworks = section(networkPage)
        text("语音服务", 20, networkPage)
        val servicePreset = android.widget.Spinner(this).apply {
            adapter = android.widget.ArrayAdapter(this@ProvisionActivity,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("MiMo Token Plan（订阅）", "MiMo 标准接口", "其他兼容服务"))
            contentDescription = "语音服务接入方式"
            minimumHeight = dp(48)
            networkPage.addView(this)
        }
        cloudKey = field("API Key", secret = true, target = networkPage)
        text("凭据仅用于你选择的语音服务，不提供明文回读。", 12, networkPage)
        val advanced = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val advancedToggle = button("自定义语音服务", networkPage) {
            advanced.visibility = if (advanced.visibility == View.VISIBLE) View.GONE else View.VISIBLE
        }
        networkPage.addView(advanced)
        cloudUrl = field("HTTPS 服务地址", target = advanced).apply { setText(CloudSettings.MIMO_TOKEN_PLAN_URL) }
        cloudDialect = android.widget.Spinner(this).apply {
            adapter = android.widget.ArrayAdapter(this@ProvisionActivity,
                android.R.layout.simple_spinner_dropdown_item,
                listOf("MiMo", "Chat Completions 音频 + 标准 TTS"))
            contentDescription = "语音服务协议"
            minimumHeight = dp(48)
            advanced.addView(this)
        }
        text("兼容协议需要服务同时支持语音输入与 PCM 合成，当前合成音色为 alloy；仅支持文字聊天的服务不可用。", 12, advanced)
        asrModel = field("语音识别模型", target = advanced).apply { setText("mimo-v2.5-asr") }
        chatModel = field("对话模型", target = advanced).apply { setText("mimo-v2.5") }
        ttsModel = field("语音合成模型", target = advanced).apply { setText("mimo-v2.5-tts") }
        advanced.visibility = View.GONE
        servicePreset.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
            override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?, position: Int, id: Long) {
                if (position < 2) {
                    cloudUrl.setText(if (position == 0) CloudSettings.MIMO_TOKEN_PLAN_URL else CloudSettings.MIMO_STANDARD_URL)
                    cloudDialect.setSelection(0)
                    asrModel.setText("mimo-v2.5-asr")
                    chatModel.setText("mimo-v2.5")
                    ttsModel.setText("mimo-v2.5-tts")
                } else advanced.visibility = View.VISIBLE
            }
        }
        if (developerMode) listOf(cloudKey, advancedToggle, servicePreset)
            .forEach { it.visibility = View.GONE }
        button("保存并连接", networkPage) { connect() }.apply { primaryStyle() }
        resultPage = section()
        resultButton = button("取消连接", resultPage) { if (connection != null) connection?.close() else goBack() }

        // Diagnostic inputs are never added to the normal onboarding view.
        val developerPage = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        host = field("Gateway 证书中的主机名", target = developerPage)
        address = field("Gateway IPv4 地址", target = developerPage)
        port = field("Gateway 端口", target = developerPage).apply { inputType = InputType.TYPE_CLASS_NUMBER; setText("8443") }
        activationButton = button("导入设备激活资料", discoveryPage) { pick(ACTIVATION) }
        if (developerMode) {
            text("开发者联调", 16, discoveryPage)
            button("导入设备认领凭据", discoveryPage) { pick(BOOTSTRAP) }
            button("导入 Gateway CA 证书（DER）", discoveryPage) { pick(CERTIFICATE) }
            discoveryPage.addView(developerPage)
        }
        showPage(0)
    }

    private fun section(parent: LinearLayout = form) = LinearLayout(this).also {
        it.orientation = LinearLayout.VERTICAL; parent.addView(it)
    }
    private fun text(label: String, size: Int, target: LinearLayout = form) = TextView(this).also {
        it.text = label; it.textSize = size.toFloat(); it.setTextColor(if (size >= 20) INK else MUTED)
        if (size >= 20) it.typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
        it.setLineSpacing(dp(3).toFloat(), 1f)
        it.setPadding(0, dp(8), 0, dp(12)); target.addView(it)
    }
    private fun field(label: String, secret: Boolean = false, target: LinearLayout = form) = EditText(this).also {
        it.hint = label
        it.contentDescription = label
        it.setTextColor(INK); it.setHintTextColor(MUTED)
        it.setPadding(dp(16), dp(12), dp(16), dp(12))
        it.background = GradientDrawable().apply { setColor(Color.WHITE); cornerRadius = dp(16).toFloat() }
        it.isSaveEnabled = false
        it.importantForAutofill = android.view.View.IMPORTANT_FOR_AUTOFILL_NO
        it.inputType = InputType.TYPE_CLASS_TEXT or if (secret) InputType.TYPE_TEXT_VARIATION_PASSWORD else InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        target.addView(it, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(60)).apply { bottomMargin = dp(12) })
    }
    private fun button(label: String, target: LinearLayout = form, action: () -> Unit): Button {
        return Button(this).apply {
            text = label; isAllCaps = false; textSize = 16f; setTextColor(INK)
            stateListAnimator = null
            typeface = android.graphics.Typeface.create("sans-serif-medium", 0)
            background = GradientDrawable().apply { setColor(Color.rgb(222,235,229)); cornerRadius = dp(18).toFloat() }
            setOnClickListener { action() }
            target.addView(this, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(54)).apply { topMargin = dp(6); bottomMargin = dp(6) })
        }
    }
    private fun Button.primaryStyle() {
        setTextColor(Color.WHITE)
        background = GradientDrawable().apply { setColor(INK); cornerRadius = dp(20).toFloat() }
    }

    private fun showPage(value: Int) {
        if (value != 1) cancelWifiScan()
        page = value
        if (value != 0) stopNfc()
        discoveryPage.visibility = if (value == 0) View.VISIBLE else View.GONE
        networkPage.visibility = if (value == 1) View.VISIBLE else View.GONE
        resultPage.visibility = if (value == 2) View.VISIBLE else View.GONE
        titleLabel.text = listOf("添加傻妞", "连接 Wi-Fi", "正在连接傻妞")[value]
        stepLabel.text = "${value + 1} / 3  ·  " + listOf("找到设备", "连接 Wi-Fi", "确认结果")[value]
        revealStatus()
    }
    private fun revealStatus() {
        getSystemService(android.view.inputmethod.InputMethodManager::class.java)
            ?.hideSoftInputFromWindow(status.windowToken, 0)
        (form.parent as? ScrollView)?.let { scroll ->
            scroll.post { if (alive && foreground) scroll.smoothScrollTo(0, 0) }
        }
    }
    private fun reportStatus(message: String) {
        status.text = message
        revealStatus()
    }
    private fun nextPage() {
        if (selected == null) { reportStatus("请先选择附近的傻妞。"); return }
        // Owner activation is required before collecting a Wi-Fi password.
        if (bootstrap == null ||
            (developerMode && (ca == null || host.text.isNullOrBlank() || address.text.isNullOrBlank()))) {
            reportStatus("请先导入随设备提供的激活资料，再继续设置网络。")
            return
        }
        val pending = hasPending(bootstrap!!.deviceId) ?: return
        if (pending) {
            connect(recover = true, controlFirst = bootstrap?.screenBootstrap == true)
            return
        }
        if (bootstrap?.screenBootstrap == true) {
            connect()
            return
        }
        stopDiscovery(); showPage(1)
        status.text = "为傻妞设置网络，之后就不用一直连接手机了。"
    }
    private fun goBack() {
        if (connection != null) { connection?.close(); return }
        if (wifiScan != null) {
            cancelWifiScan()
            return
        }
        if (recoveryControl != null) {
            cancelRecoveryToDiscovery()
            return
        }
        if (page > 0) {
            cloudLookupGeneration++; cloudResolving = false
            showPage(0); status.text = "请选择设备继续。"
        }
        else finish()
    }
    @Deprecated("Platform back callback")
    override fun onBackPressed() = goBack()
    private fun pick(code: Int) {
        if (connection != null) { status.text = "请等待本次认领结束。"; return }
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE)
        }, code)
    }
    @Deprecated("Platform Activity callback")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val scanned = com.google.zxing.integration.android.IntentIntegrator.parseActivityResult(requestCode, resultCode, data)
        if (scanned != null) {
            val value = scanned.contents ?: return
            try {
                val next = ProvisionBootstrap.parseQr(value)
                bootstrap?.close(); bootstrap = next
                selected = null; devices.removeAllViews(); found.clear()
                nextButton.isEnabled = false
                activationButton.visibility = View.GONE
                showPage(0)
                reportStatus("已读取设备认领码，请查找并选择附近的傻妞。无需先连接 Wi-Fi。")
            } catch (_: Exception) { reportStatus("认领码无效；请扫描傻妞当前屏幕上的二维码。") }
            return
        }
        if (resultCode != RESULT_OK || requestCode !in listOf(BOOTSTRAP, CERTIFICATE, ACTIVATION)) return
        val uri = data?.data ?: return
        var bytes: ByteArray? = null
        try {
            val limit = when (requestCode) { BOOTSTRAP -> 1024; ACTIVATION -> ProvisionActivation.MAX_BYTES; else -> 4096 }
            bytes = contentResolver.openInputStream(uri)?.use { stream ->
                val buffer = ByteArray(limit + 1)
                try {
                    var count = 0
                    while (count < buffer.size) {
                        val n = stream.read(buffer, count, buffer.size - count)
                        if (n < 0) break
                        require(n > 0)
                        count += n
                    }
                    require(count in 1..limit)
                    buffer.copyOf(count)
                } finally { buffer.fill(0) }
            } ?: error("No input")
            if (requestCode == ACTIVATION) {
                val chars = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes))
                val input = CharArray(chars.remaining()).also { chars.get(it) }
                try {
                    ProvisionActivation.parse(input).use { next ->
                        val nextCa = next.caCopy()
                        val nextBootstrap = next.takeBootstrap()
                        bootstrap?.close(); ca?.fill(0)
                        bootstrap = nextBootstrap; ca = nextCa
                        host.setText(next.host ?: ""); address.setText(next.address ?: "")
                        port.setText(next.port?.toString() ?: "8443")
                        val pending = hasPending(nextBootstrap.deviceId)
                        if (pending == null) {
                            nextButton.isEnabled = false
                            nextButton.text = "认领状态不可用"
                            return@use
                        }
                        nextButton.text = if (pending) "核对添加结果" else "下一步"
                        activationButton.visibility = View.GONE
                        reportStatus(if (pending) "上次添加结果需要核对。请把设备放在手机旁并保持通电，App 会先验证已保存的控制凭据。"
                            else "资料已就绪。请把设备放在手机旁并保持通电，App 将验证所有权后继续。")
                    }
                } finally {
                    input.fill('\u0000')
                    if (chars.hasArray()) chars.array().fill('\u0000')
                }
            } else if (requestCode == BOOTSTRAP) {
                val chars = Charsets.UTF_8.newDecoder().decode(java.nio.ByteBuffer.wrap(bytes))
                val input = CharArray(chars.remaining()).also { chars.get(it) }
                try {
                    val next = ProvisionBootstrap.parse(input)
                    bootstrap?.close(); bootstrap = next
                    status.text = "已载入 ${next.deviceId} 的认领凭据。"
                } finally {
                    input.fill('\u0000')
                    if (chars.hasArray()) chars.array().fill('\u0000')
                }
            } else {
                // The board also validates this CA before changing networking.
                val cert = java.security.cert.CertificateFactory.getInstance("X.509")
                    .generateCertificate(bytes.inputStream()) as java.security.cert.X509Certificate
                require(cert.basicConstraints >= 0 && cert.encoded.contentEquals(bytes))
                ca?.fill(0); ca = bytes.copyOf()
                reportStatus("Gateway CA 已载入。")
            }
        } catch (_: Exception) { reportStatus("文件无效或无法读取；未开始认领。") }
        finally { bytes?.fill(0) }
    }

    private fun permissions(): Array<String> = if (Build.VERSION.SDK_INT >= 31)
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun discover() {
        if (!foreground || connection != null) return
        if (permissions().any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }) {
            requestPermissions(permissions(), PERMISSIONS); return
        }
        stopDiscovery(); found.clear(); devices.removeAllViews(); selected = null
        nextButton.isEnabled = false; nextButton.visibility = View.GONE
        try {
            val active = scanner ?: error("Bluetooth unavailable")
            scanning = true
            scanButton.isEnabled = false; scanButton.alpha = 0.5f
            stopButton.visibility = View.VISIBLE
            active.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid.fromString(
                "81e70001-9b31-4c48-9c62-e6da4b392531")).build()),
                ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback)
            handler.postDelayed(stopScan, 30000)
            status.text = "正在扫描，请在 30 秒内选择设备。"
        } catch (_: Exception) { stopDiscovery(); status.text = "无法扫描，请开启蓝牙并授予权限。" }
    }
    override fun onRequestPermissionsResult(code: Int, names: Array<out String>, grants: IntArray) {
        super.onRequestPermissionsResult(code, names, grants)
        if (code != PERMISSIONS && code != NFC_PERMISSIONS) return
        if (grants.isNotEmpty() && grants.all { it == PackageManager.PERMISSION_GRANTED }) {
            if (code == NFC_PERMISSIONS) beginNfc() else discover()
        }
        else status.text = "未取得扫描权限；没有开始认领。"
    }
    private fun stopDiscovery() {
        handler.removeCallbacks(stopScan)
        if (scanning) { scanning = false; try { scanner?.stopScan(callback) } catch (_: Exception) {} }
        if (::scanButton.isInitialized) { scanButton.isEnabled = true; scanButton.alpha = 1f }
        if (::stopButton.isInitialized) stopButton.visibility = View.GONE
    }

    private fun localConnectInputError(recover: Boolean): String? {
        if (recover) return null
        if (ssid.text.isBlank()) return "请填写 Wi-Fi 名称后再保存。"
        val wifiSecret = CharArray(password.length()) { password.text[it] }
        try {
            ProvisionSettings.inputError(ssid.text.toString(), wifiSecret)?.let { return it }
        } finally { wifiSecret.fill('\u0000') }
        if (!developerMode) {
            if (cloudUrl.text.toString().trim().isEmpty())
                return "请填写语音服务 HTTPS 地址后再保存。"
            if (cloudKey.length() == 0)
                return "请填写语音服务 Key 后再保存；当前事务会同时保存 Wi-Fi 和语音服务。"
            if (asrModel.text.isBlank() || chatModel.text.isBlank() || ttsModel.text.isBlank())
                return "请填写语音识别、对话和语音合成模型后再保存。"
            val apiKey = CharArray(cloudKey.length()) { cloudKey.text[it] }
            try {
                CloudSettings.inputError(cloudUrl.text.toString().trim(), apiKey,
                    asrModel.text.toString().trim(), chatModel.text.toString().trim(),
                    ttsModel.text.toString().trim())?.let { return it }
            } finally { apiKey.fill('\u0000') }
        } else {
            if (host.text.isBlank()) return "请填写 Gateway 主机名后再保存。"
            if (ca == null) return "请先导入 Gateway CA 后再保存。"
        }
        return null
    }

    private fun connect(recover: Boolean = false, endpoint: CloudEndpoint.Verified? = null,
                        controlFirst: Boolean = false) {
        if (!foreground || connection != null) return
        if (wifiScan != null) {
            status.text = "请先取消 Wi-Fi 扫描，再保存连接设置。"
            return
        }
        if (outcomeUnknown && !recover) {
            status.text = "上次提交结果未确认，需要先核对设备回执。"
            return
        }
        if (bootstrap?.screenBootstrap != true) localConnectInputError(recover)?.let {
            reportStatus(it)
            return
        }
        if (!developerMode && !recover && endpoint == null && bootstrap?.screenBootstrap != true) {
            if (cloudResolving) return
            val url = cloudUrl.text.toString().trim()
            val device = selected?.address
            val generation = ++cloudLookupGeneration
            cloudResolving = true
            reportStatus("正在验证语音服务连接…")
            Thread {
                val resolved = runCatching { cloudResolver(url) }
                handler.post {
                    if (generation != cloudLookupGeneration) return@post
                    cloudResolving = false
                    if (!alive || !foreground || page != 1 || device != selected?.address ||
                        url != cloudUrl.text.toString().trim()) return@post
                    resolved.fold(onSuccess = { connect(endpoint = it) },
                        onFailure = { reportStatus("无法验证语音服务，请检查 HTTPS 地址及网络。") })
                }
            }.start()
            return
        }
        var bundle: ByteArray? = null
        var handedOff = false
        val secret = CharArray(password.length()) { password.text[it] }
        val apiKey = CharArray(cloudKey.length()) { cloudKey.text[it] }
        try {
            val identity = bootstrap ?: error("Missing identity")
            val pending = hasPending(identity.deviceId) ?: return
            if (!recover && pending) {
                status.text = "此设备有待核对的提交回执；核对前不会重复认领。"
                return
            }
            val target = selected ?: error("Missing device")
            if (recover) {
                require(pending)
                if (controlFirst) {
                    startControlRecovery(target, identity)
                    handedOff = true
                    return
                }
                bundle = byteArrayOf(0) // No configuration is sent in recovery.
            } else {
            if (identity.screenBootstrap) {
                val controlKey = ByteArray(32).also { java.security.SecureRandom().nextBytes(it) }
                try { bundle = ProvisionSettings.encodeOwner(System.currentTimeMillis() / 1000, controlKey) }
                finally { controlKey.fill(0) }
            } else if (!developerMode) {
                val verified = requireNotNull(endpoint)
                val controlKey = ByteArray(32).also { java.security.SecureRandom().nextBytes(it) }
                try {
                bundle = ProvisionSettings.encodeCloud(ssid.text.toString(), secret,
                    cloudUrl.text.toString().trim(), apiKey,
                    if (cloudDialect.selectedItemPosition == 0) CloudSettings.Dialect.MIMO else CloudSettings.Dialect.OPENAI_CHAT_AUDIO,
                    asrModel.text.toString().trim(), chatModel.text.toString().trim(), ttsModel.text.toString().trim(),
                    verified.address, verified.caDer, System.currentTimeMillis() / 1000, controlKey)
                } finally { controlKey.fill(0) }
            } else {
            val cert = ca ?: error("Missing CA")
            val parts = address.text.toString().trim().split('.')
            require(parts.size == 4)
            val ipv4 = parts.map { require(it.matches(Regex("[0-9]{1,3}"))); it.toInt().also { n -> require(n in 0..255) }.toByte() }.toByteArray()
            bundle = ProvisionSettings.encode(ssid.text.toString(), secret, host.text.toString().trim(),
                ipv4, port.text.toString().toInt(), cert, System.currentTimeMillis() / 1000)
            }
            }
            stopDiscovery()
            showPage(2)
            resultButton.text = "取消连接"
            resultButton.setOnClickListener { connection?.close() }
            val current = ++epoch
            connection = connectionFactory(target, identity, bundle, { state ->
                handler.post {
                    if (!alive || current != epoch) return@post
                    if (state == ProvisionClaimProtocol.State.UNCONFIRMED) outcomeUnknown = true
                    if (state == ProvisionClaimProtocol.State.COMMITTED || state == ProvisionClaimProtocol.State.NOT_COMMITTED) outcomeUnknown = false
                    status.text = when (state) {
                        ProvisionClaimProtocol.State.LOCAL_CONFIRMATION -> "正在由 App 验证设备所有权，请保持设备靠近手机并通电。"
                        ProvisionClaimProtocol.State.COMMITTED -> if (identity.screenBootstrap)
                            "认领已保存。返回首页连接设备，在设备设置中配置 Wi-Fi 与云服务。"
                            else "设备已确认保存连接设置。"
                        ProvisionClaimProtocol.State.NOT_COMMITTED -> "设备确认没有已提交配置，可以重新认领。"
                        ProvisionClaimProtocol.State.UNCONFIRMED -> "提交结果未确认，请先核对设备状态，勿重复认领。"
                        ProvisionClaimProtocol.State.FAILED -> connection?.failureMessage()
                            ?: "认领失败，未获得提交成功回执。"
                        ProvisionClaimProtocol.State.CLOSED -> "连接已关闭。"
                        else -> "认领进行中，请保持设备连接。"
                    }
                    if (state in listOf(ProvisionClaimProtocol.State.COMMITTED, ProvisionClaimProtocol.State.NOT_COMMITTED, ProvisionClaimProtocol.State.FAILED,
                            ProvisionClaimProtocol.State.UNCONFIRMED, ProvisionClaimProtocol.State.CLOSED)) {
                        connection?.close(); connection = null
                        if (state == ProvisionClaimProtocol.State.COMMITTED) {
                            // This is only the public device locator. The activation route
                            // and its CA authenticate board provisioning, not the app console.
                            setResult(RESULT_OK, Intent().putExtra(EXTRA_PROVISIONED_DEVICE_ID, identity.deviceId))
                        }
                        titleLabel.text = if (state == ProvisionClaimProtocol.State.COMMITTED) "设置已保存" else "连接尚未完成"
                        resultButton.text = if (state == ProvisionClaimProtocol.State.COMMITTED) "返回首页" else "返回查找设备"
                        resultButton.setOnClickListener { if (state == ProvisionClaimProtocol.State.COMMITTED) finish() else goBack() }
                    }
                }
            }, recover)
            handedOff = true
        } catch (_: Exception) {
            if (page == 2) showPage(1)
            reportStatus(if (developerMode) "Gateway 或 Wi-Fi 配置无效；请检查后再保存。"
                else "Wi-Fi 或语音服务配置无效；请检查后再保存。")
        }
        finally {
            secret.fill('\u0000'); apiKey.fill('\u0000'); bundle?.fill(0)
            if (handedOff) { password.text.clear(); cloudKey.text.clear() }
        }
    }

    private fun startControlRecovery(target: BluetoothDevice, identity: ProvisionBootstrap) {
        val transaction = bindingStore.pending(identity.deviceId) ?: return
        stopDiscovery(); showPage(2)
        resultButton.text = "取消连接"
        val current = ++epoch
        var terminal = false
        lateinit var control: DeviceControlConnection
        control = DeviceControlConnection(this, target, identity.deviceId, { command, snapshot ->
            if (command == DeviceControlProtocol.Command.STATUS) handler.post {
                if (!alive || !foreground || current != epoch || recoveryControl !== control) return@post
                if (snapshot.error != 0) {
                    status.text = "设备未确认已保存配置，正在核对认领回执。"
                    control.close()
                } else if (runCatching {
                        bindingStore.commit(identity.deviceId, transaction)
                    }.getOrDefault(false)) {
                    terminal = true
                    recoveryControl = null
                    status.text = "设备已确认保存连接设置。"
                    titleLabel.text = "设置已保存"
                    resultButton.text = "返回首页"
                    resultButton.setOnClickListener { finish() }
                    setResult(RESULT_OK, Intent().putExtra(EXTRA_PROVISIONED_DEVICE_ID, identity.deviceId))
                    control.close()
                } else {
                    terminal = true
                    recoveryControl = null
                    status.text = "提交已验证但本机保存未确认；请勿重复认领。"
                    outcomeUnknown = true
                    titleLabel.text = "连接尚未完成"
                    resultButton.text = "返回查找设备"
                    resultButton.setOnClickListener { goBack() }
                    control.close()
                }
            }
        }, { _ -> handler.post {
            if (!alive || !foreground || current != epoch || recoveryControl !== control) return@post
            recoveryControl = null
            if (!terminal) connect(recover = true, controlFirst = false)
        } }, bindingStore, transaction)
        recoveryControl = control
        resultButton.setOnClickListener { goBack() }
    }

    private fun cancelRecoveryToDiscovery() {
        ++epoch
        val control = recoveryControl
        recoveryControl = null
        control?.close()
        showPage(0)
        status.text = "已取消本次连接，认领结果仍需核对。"
    }

    private fun hasPending(deviceId: String): Boolean? = try {
        bindingStore.pending(deviceId) != null
    } catch (_: Exception) {
        bindingStateAvailable = false
        status.text = "本机保存状态未确认，已暂停认领。请关闭并重新启动 App 后核对结果，保留现有认领资料。"
        null
    }

    private fun scanWifi() {
        if (!foreground || page != 1 || connection != null || wifiScan != null) return
        val target = selected
        val identity = bootstrap
        if (target == null || identity == null) {
            status.text = "请先导入激活资料并选择设备。"
            return
        }
        val generation = ++wifiScanGeneration
        wifiNetworks.removeAllViews()
        wifiScanButton.isEnabled = false
        wifiCancelButton.visibility = View.VISIBLE
        status.text = "正在让傻妞扫描附近 Wi-Fi，最多约 15 秒。"
        try {
            wifiScan = WifiScanConnection(this, target, identity, { result ->
                handler.post {
                    if (!alive || generation != wifiScanGeneration || page != 1) return@post
                    val active = wifiScan
                    wifiScan = null
                    active?.close()
                    wifiScanButton.isEnabled = true
                    wifiCancelButton.visibility = View.GONE
                    if (result.status != 0) {
                        Log.w(WIFI_SCAN_LOG_TAG, "wifi_scan_result status=${result.status}")
                        status.text = "Wi-Fi 扫描未完成（设备状态 ${result.status}）；可以手动输入网络名称。"
                        return@post
                    }
                    Log.i(WIFI_SCAN_LOG_TAG, "wifi_scan_result status=0 count=${result.networks.size} truncated=${result.truncated}")
                    result.networks.forEach { network ->
                        wifiNetworks.addView(Button(this@ProvisionActivity).apply {
                            text = "${network.ssid} · ${wifiSignal(network.rssi)} · ${wifiSecurity(network.security)}"
                            contentDescription = "选择 Wi-Fi ${network.ssid}"
                            setOnClickListener {
                                ssid.setText(network.ssid)
                                status.text = "已选择 ${network.ssid}，请输入密码后保存。"
                            }
                        })
                    }
                    status.text = when {
                        result.networks.isEmpty() -> "没有可选择的 Wi-Fi；可以手动输入网络名称。"
                        result.truncated -> "已显示部分 Wi-Fi；也可以手动输入隐藏网络名称。"
                        else -> "请选择 Wi-Fi，或手动输入隐藏网络名称。"
                    }
                }
            }, { reason -> handler.post {
                if (!alive || generation != wifiScanGeneration || page != 1) return@post
                if (wifiScan != null) {
                    wifiScan = null
                    wifiScanButton.isEnabled = true
                    wifiCancelButton.visibility = View.GONE
                    Log.w(WIFI_SCAN_LOG_TAG, "wifi_scan_transport_closed reason=$reason")
                    status.text = wifiScanClosedMessage(reason)
                }
            } })
        } catch (_: Exception) {
            wifiScan = null
            wifiScanButton.isEnabled = true
            wifiCancelButton.visibility = View.GONE
            status.text = "无法开始 Wi-Fi 扫描；可以手动输入网络名称。"
        }
    }

    private fun cancelWifiScan() {
        wifiScanGeneration++
        wifiScan?.close()
        wifiScan = null
        if (::wifiScanButton.isInitialized) wifiScanButton.isEnabled = true
        if (::wifiCancelButton.isInitialized) wifiCancelButton.visibility = View.GONE
    }

    private fun wifiScanClosedMessage(reason: String) = when (reason) {
        "bluetooth_permission_denied" -> "蓝牙权限不足，请允许附近设备权限后重新扫描。"
        "disconnected" -> "设备连接已断开，请确认设备通电并靠近手机后重新扫描。"
        "handshake_timeout" -> "设备安全连接超时，请保持设备靠近手机后重新扫描。"
        "session_timeout", "write_timeout" -> "Wi-Fi 扫描响应超时，请保持设备靠近手机后重新扫描。"
        "transport_error" -> "设备安全连接或身份认证失败，请确认激活资料与目标设备匹配后重新扫描。"
        "service_discovery_failed", "provision_service_missing" ->
            "未找到设备 Wi-Fi 扫描服务，请确认设备已完成启动后重新扫描。"
        "notification_registration_failed" -> "设备未能开启 Wi-Fi 扫描通知，请重新连接设备后再试。"
        "descriptor_write_start_failed" -> "设备未能开始订阅 Wi-Fi 扫描通知，请重新连接设备后再试。"
        "subscription_failed" -> "设备未确认 Wi-Fi 扫描通知订阅，请重新连接设备后再试。"
        else -> "Wi-Fi 扫描连接异常，请检查蓝牙和设备状态后重新扫描。"
    }

    private fun wifiSignal(rssi: Int) = when {
        rssi >= -55 -> "信号强"
        rssi >= -70 -> "信号中"
        else -> "信号弱"
    }

    private fun wifiSecurity(value: Int) = when (value) {
        0 -> "开放网络"
        1 -> "WEP"
        in 2..4 -> "WPA"
        in 5..7 -> "WPA2"
        8 -> "WPA3 SAE"
        9 -> "WPA2/WPA3"
        10 -> "EAP"
        11 -> "OWE"
        else -> "安全类型未知"
    }
    private fun beginNfc() {
        if (!foreground || page != 0 || connection != null) return
        stopNfc()
        stopDiscovery()
        found.clear(); devices.removeAllViews(); selected = null
        nextButton.isEnabled = false; nextButton.visibility = View.GONE
        if (permissions().any { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }) {
            requestPermissions(permissions(), NFC_PERMISSIONS)
            return
        }
        nfcRequested = true
        nfcSeen = false
        nfcLocator = null
        nfcStatus.visibility = View.VISIBLE
        val adapter = android.nfc.NfcAdapter.getDefaultAdapter(this)
        if (adapter == null) {
            stopNfc(); nfcStatus.text = "这台手机不支持碰一碰，请使用蓝牙查找。"
        } else if (!adapter.isEnabled) {
            nfcStatus.text = "请开启 NFC，返回后再点“碰一碰查找”。"
            runCatching { startActivity(Intent(android.provider.Settings.ACTION_NFC_SETTINGS)) }
                .onFailure { nfcStatus.text = "请在手机设置中开启 NFC，再返回重试。" }
        } else updateNfc()
    }

    private fun updateNfc() {
        if (!nfcResumed || !nfcRequested || page != 0) return
        val preferred = runCatching {
            val adapter = android.nfc.NfcAdapter.getDefaultAdapter(this)
            adapter != null && adapter.isEnabled &&
                android.nfc.cardemulation.CardEmulation.getInstance(adapter)
                    .setPreferredService(this, android.content.ComponentName(this, ShaniuHceService::class.java))
        }.getOrDefault(false)
        ShaniuHceService.foregroundEnabled = preferred
        if (!preferred) {
            ShaniuHceService.selectionListener = null
            nfcStatus.text = "碰一碰暂不可用，请开启 NFC 或使用蓝牙查找。"
            return
        }
        nfcButton.text = "重新碰一碰"
        nfcStatus.text = "把手机背面贴近傻妞，保持屏幕解锁。"
        ShaniuHceService.selectionListener = { locator ->
            if (nfcResumed && nfcRequested && page == 0 && !nfcSeen) {
                nfcSeen = true
                nfcLocator = locator.copyOf()
                nfcStatus.text = "已碰到设备，正在查找对应的蓝牙连接。"
                // NFC selection is not device identity or ownership proof.
                discover()
            }
        }
        handler.removeCallbacks(nfcTimeout)
        handler.postDelayed(nfcTimeout, 60000)
    }

    private fun pauseNfc() {
        handler.removeCallbacks(nfcTimeout)
        ShaniuHceService.foregroundEnabled = false
        ShaniuHceService.selectionListener = null
        runCatching {
            android.nfc.NfcAdapter.getDefaultAdapter(this)?.let {
                android.nfc.cardemulation.CardEmulation.getInstance(it).unsetPreferredService(this)
            }
        }
    }

    private fun stopNfc() {
        nfcRequested = false
        if (nfcLocator != null) stopDiscovery()
        nfcLocator = null
        pauseNfc()
        if (::nfcButton.isInitialized) nfcButton.text = "碰一碰查找"
    }

    override fun onResume() {
        super.onResume()
        nfcResumed = true
        updateNfc()
    }
    override fun onPause() {
        nfcResumed = false
        if (nfcRequested) nfcStatus.text = "碰一碰已暂停，返回后可重新开始。"
        stopNfc()
        super.onPause()
    }
    override fun onStart() {
        super.onStart()
        foreground = true
    }
    override fun onStop() {
        foreground = false
        if (cloudResolving) status.text = "连接验证已暂停，返回后可再次保存。"
        cloudLookupGeneration++; cloudResolving = false
        cancelWifiScan()
        val wasScanning = scanning
        stopDiscovery()
        if (wasScanning) status.text = "扫描已暂停，返回后可重新扫描。"
        // Closing drives the protocol's UNCONFIRMED state after APPLY; its
        // durable receipt locator survives. Never treat backgrounding as a
        // failed commit or silently continue an owner confirmation offscreen.
        if (recoveryControl != null) {
            cancelRecoveryToDiscovery()
        } else {
            connection?.close()
            recoveryControl = null
        }
        super.onStop()
    }
    override fun onDestroy() {
        alive = false; epoch++; stopNfc(); stopDiscovery(); connection?.close(); recoveryControl?.close()
        bootstrap?.close(); ca?.fill(0); password.text.clear(); cloudKey.text.clear()
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }
    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()
    companion object {
        const val EXTRA_PROVISIONED_DEVICE_ID = "com.shaniu.companion.provisioned_device_id"
        private const val WIFI_SCAN_LOG_TAG = "ShaniuWifiScan"
        private const val ACTIVATION = 104
        private const val NFC_PERMISSIONS = 105
        private const val BOOTSTRAP = 101; private const val CERTIFICATE = 102; private const val PERMISSIONS = 103
        private val BACKGROUND = Color.rgb(248,248,243)
        private val INK = Color.rgb(35,57,50)
        private val MUTED = Color.rgb(113,126,119)
    }
}
