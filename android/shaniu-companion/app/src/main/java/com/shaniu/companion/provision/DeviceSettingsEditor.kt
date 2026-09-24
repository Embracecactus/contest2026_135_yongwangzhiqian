// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.app.Activity
import android.app.Dialog
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.InputType
import android.view.View
import android.widget.*
import java.net.URI
import java.nio.ByteBuffer

/** Uses the foreground's one authenticated session. It never opens BLE itself.
 * The only persisted draft is a PUBLIC operation ID/revision for reconciliation.
 */
internal class DeviceSettingsEditor(
    private val activity: Activity,
    private val session: DeviceControlSession,
    private val deviceId: String,
    private val appendMax: Int,
    private val cloudPage: Boolean = false,
    private val modelFocus: String? = null,
    private val embeddedHost: LinearLayout? = null,
    private val navigateBack: (() -> Unit)? = null,
    private val finished: (String) -> Unit,
) : AutoCloseable {
    private enum class Phase { IDLE, READING, SCANNING, RESOLVING, BEGIN, APPEND, APPLY, VERIFY, CLOSING }
    private val handler = Handler(Looper.getMainLooper())
    private val preferences = activity.getSharedPreferences("shaniu-settings-receipts", Activity.MODE_PRIVATE)
    private val previousSoftInputMode = activity.window.attributes.softInputMode
    private val secureFlagWasSet = activity.window.attributes.flags and android.view.WindowManager.LayoutParams.FLAG_SECURE != 0
    private val design = com.shaniu.companion.CompanionDesign(activity)
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
    private val box = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(16), dp(24), dp(16)) }
    private val editorContent = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
    // The form is the sole shrinkable region. The editor owns its actions so
    // AlertDialog/ButtonBarLayout cannot lose a stacked action at large text.
    private val formScroll = ScrollView(activity).apply {
        addView(box)
        layoutParams = LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            0,
            1f,
        )
    }
    private val fieldContainers = linkedMapOf<View, View>()
    private val message = TextView(activity).apply {
        text = "正在通过已认证蓝牙读取设备配置…"; textSize = 14f
        setTextColor(design.ink)
        background = design.shape(design.selected, dp(16).toFloat())
        setPadding(dp(16), dp(12), dp(16), dp(12))
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
        box.addView(this)
    }
    private val scanWifi = button("选择傻妞附近的 Wi-Fi") { scan() }
    private val connectionStatus = TextView(activity).apply {
        textSize = 14f; setTextColor(design.muted)
        setPadding(0, dp(12), 0, dp(8)); box.addView(this)
        accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
    }
    private val network = field("Wi-Fi 名称（隐藏网络可手动输入）")
    private val password = field("新 Wi-Fi 密码（留空不修改）", true)
    private val replacePassword = CheckBox(activity).apply { text = "开放网络（无密码）"; box.addView(this) }
    private val saveWifi = button("仅保存 Wi-Fi") { save(false) }
    private val url = field("HTTPS 服务地址")
    private val key = field("新 API Key（留空保留原密钥）", true)
    private val dialect = Spinner(activity).apply {
        adapter = ArrayAdapter(activity, android.R.layout.simple_spinner_dropdown_item, listOf("MiMo", "Chat Completions 音频"))
        box.addView(this)
    }
    private val asr = modelField("听懂你 · 语音识别模型 ID")
    private val chat = modelField("对话 · 回答模型 ID")
    private val tts = modelField("说给你听 · 语音合成模型 ID")
    private val saveCloud = button("仅保存云服务和模型") { save(true) }
    private val reload = button("重新读取设备配置") { read(false) }
    private val saveAction = actionButton(if (cloudPage) "保存云配置" else "保存 Wi-Fi") { save(cloudPage) }
    private val closeAction = actionButton("关闭") { close() }
    private val footer = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(24), dp(8), dp(24), dp(16))
        addView(saveAction, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        addView(closeAction, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            topMargin = dp(8)
        })
    }
    private var phase = Phase.IDLE
    private var active = true
    private var connectionLost = false
    private var scanUnsupported = false
    private var generation = session.current().generation
    private var ticket = 0L
    private var deadline = 0L
    private var offset = 0
    private var readBytes: ByteArray? = null
    private var payload: ByteArray? = null
    private var operation: ByteArray? = null
    private var current: DeviceSettings.Public? = null
    private var waiting: DeviceControlProtocol.Command? = null
    private var readVerifying = false
    private var applied = false
    private var resultSubscription: DeviceControlSession.Cancel? = null
    private var stateSubscription: DeviceControlSession.Cancel? = null
    private var dialog: Dialog? = null
    private var retryBytes: ByteArray? = null
    private var lastMessage = "设备配置未修改"
    private var contentDecor: View? = null
    private var contentLayoutListener: View.OnLayoutChangeListener? = null
    private var contentWindowSignature = Int.MIN_VALUE
    private val serviceDetails = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
    private var serviceDetailsToggle: com.google.android.material.button.MaterialButton? = null

    private val modelOptions = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
    private val modelTabs = mutableListOf<TextView>()
    private var selectedModel = modelFocus ?: "chat"
    private lateinit var modelSummary: TextView

    init {
        // Submission belongs to the editor's fixed footer, never the scrolling form.
        saveWifi.visibility = View.GONE
        saveCloud.visibility = View.GONE
        listOf<View>(scanWifi, network, password, replacePassword, saveWifi).forEach { (fieldContainers[it] ?: it).visibility = if (cloudPage) View.GONE else View.VISIBLE }
        listOf<View>(url, key, dialect, asr, chat, tts, saveCloud).forEach { (fieldContainers[it] ?: it).visibility = if (cloudPage) View.VISIBLE else View.GONE }
        saveWifi.visibility = View.GONE; saveCloud.visibility = View.GONE
        if (cloudPage) {
            buildCloudForm()
        } else {
            editorContent.addView(TextView(activity).apply {
                text = "连接网络"; textSize = 22f; setTextColor(design.ink)
                setPadding(dp(24), dp(20), dp(24), dp(12)); isAccessibilityHeading = true
            })
        }
        editorContent.addView(formScroll)
        editorContent.addView(footer)
        resultSubscription = session.observeResults(::received)
        stateSubscription = session.observe { state ->
            connectionStatus.text = when {
                !state.authenticated -> "蓝牙未认证 · 无法查询联网状态"
                !state.snapshotFresh -> "联网状态待刷新 · 不代表保存失败"
                state.snapshot?.wifiReady == true -> "设备报告 Wi-Fi 就绪 · 云服务状态需单独确认"
                state.snapshot?.wifiReady == false -> "设备尚未联网或连接已中断 · 可继续修改 Wi-Fi"
                else -> "此固件未报告联网状态"
            }
            if (active && !connectionLost && (!state.authenticated || state.generation != generation)) {
                // Publishing a terminal transaction state notifies this observer again.
                connectionLost = true
                fail(if (applied) "连接已变化，保存结果待回读；不会自动重发" else "连接已变化，请关闭后重新打开设置")
                editable(false)
                handler.post { if (active) close() }
            }
        }
        // 长表单采用单一显式布局，不让 AlertDialog 的 custom panel
        // 再次测量/裁切固定页脚；认证、事务与安全窗口语义保持不变。
        if (embeddedHost != null) {
            editorContent.setBackgroundColor(design.background)
            embeddedHost.addView(editorContent, LinearLayout.LayoutParams(-1, -1))
            activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
            activity.window.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
            formScroll.addOnLayoutChangeListener { _, _, top, _, bottom, _, oldTop, _, oldBottom ->
                if (bottom - top != oldBottom - oldTop) formScroll.post {
                    // IME resize happens after focus; expose the field again in
                    // the newly measured viewport without recreating its draft.
                    (box.findFocus() as? EditText)?.let { input ->
                        input.requestRectangleOnScreen(android.graphics.Rect(0, 0, input.width, input.height), true)
                    }
                }
            }
        } else dialog = Dialog(activity).also {
                it.requestWindowFeature(android.view.Window.FEATURE_NO_TITLE)
                it.setContentView(editorContent)
                it.window?.setBackgroundDrawable(design.shape(design.surface, dp(24).toFloat()))
                it.window?.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
                it.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
                it.setOnDismissListener { close() }
                it.show()
                it.window?.decorView?.let { decor ->
                    val listener = View.OnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
                        constrainContentHeight(it)
                    }
                    contentDecor = decor; contentLayoutListener = listener
                    decor.addOnLayoutChangeListener(listener)
                    editorContent.post { constrainContentHeight(it) }
                }
            }
        read(false)
    }

    private fun buildCloudForm() {
        // Reparent the original inputs: one draft and one transaction across tabs.
        box.removeAllViews()
        box.setPadding(dp(24), dp(8), dp(24), dp(16))
        val page = com.shaniu.companion.CompanionPage(activity, box)
        page.pageTitle("云服务与模型", "听、想、说，分别选择适合的服务。") {
            navigateBack?.invoke() ?: close()
        }
        val tabs = LinearLayout(activity).apply {
            setPadding(dp(4), dp(4), dp(4), dp(4))
            background = design.shape(design.divider, dp(14).toFloat())
        }
        listOf("chat" to "对话", "asr" to "听懂你", "tts" to "说给你听").forEach { (name, label) ->
            val tab = TextView(activity).apply {
                text = label; tag = name; textSize = 14f
                gravity = android.view.Gravity.CENTER; minHeight = dp(48)
                setPadding(dp(4), dp(8), dp(4), dp(8))
                isClickable = true; isFocusable = true
                setOnClickListener { selectedModel = name; updateModelTab() }
            }
            modelTabs.add(tab)
            tabs.addView(tab, LinearLayout.LayoutParams(0, -2, 1f))
        }
        box.addView(tabs, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(20) })
        modelSummary = TextView(activity).apply {
            textSize = 14f; setTextColor(design.accent)
            setPadding(dp(18), dp(16), dp(18), dp(16))
            compoundDrawablePadding = dp(14)
            background = design.shape(design.selected, dp(20).toFloat())
        }
        box.addView(modelSummary, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(16) })
        val card = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL; setPadding(dp(18), dp(18), dp(18), dp(18))
            background = design.shape(design.surface, dp(24).toFloat())
        }
        card.addView(TextView(activity).apply {
            text = "服务提供方"; textSize = 14f; setTextColor(design.ink)
            typeface = android.graphics.Typeface.DEFAULT_BOLD
        })
        dialect.adapter = object : ArrayAdapter<String>(activity, android.R.layout.simple_spinner_dropdown_item,
            listOf("MiMo", "Chat Completions 音频")) {
            override fun getView(position: Int, convertView: View?, parent: android.view.ViewGroup): View =
                (super.getView(position, convertView, parent) as TextView).apply {
                    textSize = 16f; setTextColor(design.ink)
                    typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
                }
        }
        dialect.apply {
            minimumHeight = dp(52); setPadding(dp(8), 0, dp(32), 0)
            background = design.shape(design.background, dp(14).toFloat()).apply { setStroke(dp(1), design.divider) }
        }
        val provider = FrameLayout(activity).apply {
            addView(dialect, FrameLayout.LayoutParams(-1, -2))
            addView(ImageView(activity).apply {
                setImageDrawable(com.shaniu.companion.CompanionIcons.drawable(activity, "chevron", design.ink))
                rotation = 90f; importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }, FrameLayout.LayoutParams(dp(20), dp(20), android.view.Gravity.END or android.view.Gravity.CENTER_VERTICAL)
                .apply { marginEnd = dp(12) })
        }
        card.addView(provider, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
        listOf(chat, asr, tts, key).forEach { input ->
            val group = fieldContainers.getValue(input) as LinearLayout
            (group.getChildAt(0) as TextView).apply {
                text = if (input === key) "API Key" else "模型 ID"
                setTextColor(design.ink); typeface = android.graphics.Typeface.DEFAULT_BOLD
            }
            input.typeface = android.graphics.Typeface.create("sans-serif-medium", android.graphics.Typeface.NORMAL)
            input.setHintTextColor(design.muted)
            input.minHeight = dp(52)
            val container = group.getChildAt(1) as com.google.android.material.textfield.TextInputLayout
            container.boxBackgroundColor = design.background
            container.boxStrokeWidth = dp(1)
            container.boxStrokeWidthFocused = dp(1)
            container.setBoxStrokeColorStateList(android.content.res.ColorStateList(
                arrayOf(intArrayOf(android.R.attr.state_focused), intArrayOf()),
                intArrayOf(design.accent, design.divider)))
            container.setBoxCornerRadii(dp(14).toFloat(), dp(14).toFloat(), dp(14).toFloat(), dp(14).toFloat())
            input.hint = if (input === key) "留空保留已有密钥" else "读取设备后显示"
            card.addView(group)
        }
        card.addView(TextView(activity).apply {
            text = "留空保留已有密钥。三项模型共用服务连接；保存会提交所有标签中的修改。更换主机需重新核对认证。"
            textSize = 12f; setTextColor(design.muted); setPadding(0, dp(4), 0, 0)
        })
        box.addView(card, LinearLayout.LayoutParams(-1, -2))
        box.addView(modelOptions, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
        serviceDetails.addView(fieldContainers.getValue(url))
        serviceDetails.visibility = View.GONE
        button("高级设置 · 服务连接与凭据") {
            val expanded = serviceDetails.visibility != View.VISIBLE
            serviceDetails.visibility = if (expanded) View.VISIBLE else View.GONE
            serviceDetailsToggle?.text = if (expanded) "收起服务连接与凭据" else "高级设置 · 服务连接与凭据"
        }.also {
            serviceDetailsToggle = it
            it.backgroundTintList = android.content.res.ColorStateList.valueOf(android.graphics.Color.TRANSPARENT)
            it.setTextColor(design.accent); it.insetTop = 0; it.insetBottom = 0
            it.elevation = 0f; it.stateListAnimator = null
        }
        box.addView(serviceDetails)
        box.addView(message, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
        box.addView(connectionStatus)
        box.addView(reload)
        saveAction.text = "保存本项配置"
        saveAction.contentDescription = "保存云服务与模型配置"
        saveAction.minHeight = dp(56)
        saveAction.cornerRadius = dp(20)
        saveAction.insetTop = 0; saveAction.insetBottom = 0
        saveAction.setTextColor(design.onAccent)
        saveAction.backgroundTintList = android.content.res.ColorStateList.valueOf(design.accent)
        if (embeddedHost != null) closeAction.visibility = View.GONE
        footer.setBackgroundColor(design.background)
        updateModelTab()
    }

    private fun updateModelTab() {
        modelTabs.forEach { tab ->
            val selected = tab.tag == selectedModel
            tab.isSelected = selected
            tab.setTextColor(if (selected) design.ink else design.muted)
            tab.background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(design.selected),
                design.shape(if (selected) design.surface else android.graphics.Color.TRANSPARENT, dp(10).toFloat()), null)
        }
        listOf("chat" to chat, "asr" to asr, "tts" to tts).forEach { (name, input) ->
            fieldContainers.getValue(input).visibility = if (name == selectedModel) View.VISIBLE else View.GONE
        }
        val (icon, summary) = when (selectedModel) {
            "asr" -> "mic" to "语音识别\n让傻妞听懂你说的话"
            "tts" -> "speaker" to "语音合成\n选择说给你听的声音"
            else -> "cloud" to "对话模型\n让傻妞怎样思考和回答"
        }
        modelOptions.removeAllViews()
        val options = com.shaniu.companion.CompanionPage(activity, modelOptions)
        when (selectedModel) {
            "asr" -> options.settingsRow("识别模式", "当前固件未提供整段 / 流式切换", enabled = false, iconName = "mic") { }
            "tts" -> {
                options.settingsRow("默认声音", "当前固件未提供独立音色选择", enabled = false, iconName = "speaker") { }
                options.settingsRow("唤醒应答", "当前固件未提供独立应答设置", enabled = false, iconName = "mic") { }
            }
            else -> options.settingsRow("回答模式", "快速对话或深度思考 · 在声音与回答中设置", iconName = "spark") {
                android.app.AlertDialog.Builder(activity).setTitle("回答模式")
                    .setMessage("先保存当前模型配置，再前往设置 → 帮助与诊断 → 声音与回答，读取并修改设备的回答模式。")
                    .setPositiveButton("知道了", null).show()
            }
        }
        modelSummary.text = summary
        modelSummary.setCompoundDrawablesWithIntrinsicBounds(
            com.shaniu.companion.CompanionIcons.drawable(activity, icon, design.accent), null, null, null)
    }

    /** Give the dialog, rather than its wrap-content custom panel, the visible
     * window height. The weighted ScrollView then receives every pixel left by
     * the title and fixed footer. This has no child/parent feedback path. */
    private fun constrainContentHeight(alert: Dialog) {
        if (!active || !alert.isShowing) return
        val decor = alert.window?.decorView ?: return
        val visible = android.graphics.Rect()
        decor.getWindowVisibleDisplayFrame(visible)
        // API 29 is supported too; visible-frame geometry needs no API 30
        // WindowInsets.Type call and changes when the IME resizes this window.
        val windowSignature = 31 * visible.height() + visible.width()
        if (contentWindowSignature == windowSignature) return
        contentWindowSignature = windowSignature
        // 可见显示区域已经扣除了 IME；decor 是本对话框自身尺寸，不能
        // 再扣一次键盘或用它限制新高度，否则窗口会自我收缩且无法恢复。
        val windowHeight = (visible.height() - dp(32)).coerceAtLeast(dp(240))
        alert.window?.setLayout((visible.width() - dp(32)).coerceAtLeast(dp(240)), windowHeight)
        editorContent.layoutParams?.let { params ->
            if (params.height != LinearLayout.LayoutParams.MATCH_PARENT) {
                params.height = LinearLayout.LayoutParams.MATCH_PARENT
                editorContent.layoutParams = params
            }
        }
    }

    private fun modelField(label: String) = field(label).apply {
        // 模型 ID 是公开标识，禁止输入法组合或纠正连字符。
        inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        imeOptions = android.view.inputmethod.EditorInfo.IME_FLAG_FORCE_ASCII or
            android.view.inputmethod.EditorInfo.IME_ACTION_DONE
    }

    private fun field(label: String, secret: Boolean = false): EditText {
        val group = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        // Floating hints are single-line and truncate the key-retention warning
        // at 200% font scale. Keep the full, wrapping label outside the field.
        val caption = TextView(activity).apply {
            text = label; textSize = 14f; setTextColor(design.muted)
            setPadding(0, 0, 0, dp(8))
        }
        group.addView(caption, LinearLayout.LayoutParams(-1, -2))
        val container = com.google.android.material.textfield.TextInputLayout(activity).apply {
            isHintEnabled = false
            boxBackgroundMode = com.google.android.material.textfield.TextInputLayout.BOX_BACKGROUND_OUTLINE
            setBoxCornerRadii(dp(16).toFloat(), dp(16).toFloat(), dp(16).toFloat(), dp(16).toFloat())
            isSaveEnabled = false
            if (secret) endIconMode = com.google.android.material.textfield.TextInputLayout.END_ICON_PASSWORD_TOGGLE
        }
        val input = com.google.android.material.textfield.TextInputEditText(container.context).apply {
            id = View.generateViewId()
            contentDescription = label; isSingleLine = true; textSize = 16f
            inputType = InputType.TYPE_CLASS_TEXT or if (secret) InputType.TYPE_TEXT_VARIATION_PASSWORD else InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            typeface = android.graphics.Typeface.DEFAULT
            importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO
            isSaveEnabled = false; minHeight = dp(56)
            setTextColor(design.ink)
        }
        caption.labelFor = input.id
        container.addView(input, LinearLayout.LayoutParams(-1, -2))
        group.addView(container, LinearLayout.LayoutParams(-1, -2))
        fieldContainers[input] = group
        box.addView(group, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(16) })
        return input
    }
    private fun button(label: String, action: () -> Unit) = com.google.android.material.button.MaterialButton(activity).apply {
        text = label; isAllCaps = false; minHeight = dp(48)
        setOnClickListener { action() }; box.addView(this)
    }
    private fun actionButton(label: String, action: () -> Unit) = com.google.android.material.button.MaterialButton(activity).apply {
        text = label; contentDescription = label; isAllCaps = false; minHeight = dp(48)
        setSingleLine(false); maxLines = 2
        setOnClickListener { action() }
    }
    private fun note(text: String) { message.text = text; lastMessage = text }
    private fun editable(enabled: Boolean) {
        listOf<View>(scanWifi, network, password, replacePassword, saveWifi, url, key, dialect, asr, chat, tts, saveCloud).forEach { it.isEnabled = enabled }
        saveWifi.isEnabled = enabled && current != null
        scanWifi.isEnabled = enabled && !scanUnsupported
        saveCloud.isEnabled = enabled && current != null
        saveAction.apply {
            isEnabled = enabled && current != null
            text = if (phase in setOf(Phase.BEGIN, Phase.APPEND, Phase.APPLY, Phase.VERIFY, Phase.RESOLVING)) "保存中…"
                else if (cloudPage) "保存本项配置" else "保存 Wi-Fi"
        }
        reload.isEnabled = enabled || phase == Phase.IDLE
    }
    private fun live() = active && session.current().authenticated && session.current().generation == generation
    private fun submit(command: DeviceControlProtocol.Command, bytes: ByteArray) {
        if (!live()) { bytes.fill(0); fail("设备连接已变化，未重发操作"); return }
        waiting = command
        val accepted = try { session.requestPayload(command, bytes) } catch (_: Exception) { false }
        if (!accepted) {
            waiting = null
            val ownTicket = ticket
            val copy = bytes.copyOf()
            retryBytes?.fill(0); retryBytes = copy
            handler.postDelayed({
                if (retryBytes === copy) retryBytes = null
                if (active && ownTicket == ticket && SystemClock.elapsedRealtime() < deadline) submit(command, copy)
                else { copy.fill(0); if (active && ownTicket == ticket) fail("设备忙或请求超时；请回读确认结果") }
            }, 100)
        }
        bytes.fill(0)
    }
    private fun read(verify: Boolean) {
        if (!live() || phase !in setOf(Phase.IDLE, Phase.VERIFY, Phase.READING)) return
        if (!verify && phase == Phase.IDLE) deadline = SystemClock.elapsedRealtime() + 15_000
        ticket++; offset = 0; readBytes?.fill(0); readBytes = null
        readVerifying = verify; phase = Phase.READING
        editable(false)
        submit(DeviceControlProtocol.Command.CONFIG_READ, ByteBuffer.allocate(4).putInt(DeviceSettings.KIND shl 16).array())
    }
    private fun scan() {
        if (!live() || phase != Phase.IDLE || scanUnsupported) return
        ticket++; offset = 0; readBytes?.fill(0); readBytes = null
        phase = Phase.SCANNING; deadline = SystemClock.elapsedRealtime() + 30_000
        editable(false); note("傻妞正在扫描附近网络，不会修改已保存的配置…")
        submitScan()
    }
    private fun submitScan() = submit(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((8 shl 16) or offset).array())

    private fun received(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (!live() || command != waiting) return
        waiting = null
        if (phase == Phase.SCANNING) {
            if (snapshot.error in listOf(-11, -16) && SystemClock.elapsedRealtime() < deadline) {
                val ownTicket = ticket
                handler.postDelayed({ if (live() && ticket == ownTicket) submitScan() }, 250)
                return
            }
            if (snapshot.error != 0) {
                // NuttX ENOTSUP 为 138；兼容 Linux peer 的 ENOTSUP/ENOSYS。
                if (snapshot.error in listOf(-138, -95, -38)) {
                    scanUnsupported = true
                    scanWifi.text = "当前固件不支持扫描，请手动输入 Wi-Fi 名称"
                    fail("当前设备固件不支持附近 Wi-Fi 扫描（${snapshot.error}）。请手动输入网络名称和密码并保存；升级匹配固件后可使用扫描。")
                } else {
                    fail("设备扫描未完成（${snapshot.error}），可重试或手动输入网络名称")
                }
                return
            }
            val chunk = snapshot.configChunk ?: run { fail("扫描结果缺失"); return }
            if (chunk.totalLength !in 12..876) { fail("扫描结果长度无效"); return }
            if (readBytes == null) readBytes = ByteArray(chunk.totalLength)
            val bytes = readBytes!!
            if (bytes.size != chunk.totalLength || offset !in bytes.indices) { fail("扫描结果已变化，请重试"); return }
            val count = minOf(16, bytes.size - offset)
            chunk.bytes.copyInto(bytes, offset, 0, count); offset += count
            if (offset < bytes.size) { submitScan(); return }
            val result = runCatching { WifiScanProtocol.decodeControl(bytes) }
            bytes.fill(0); readBytes = null; phase = Phase.IDLE; editable(true)
            result.fold(onSuccess = { found ->
                val choices = found.networks.sortedByDescending { it.rssi }.distinctBy { it.ssid }
                note(if (choices.isEmpty()) "没有找到可见网络，可重新扫描或手动输入隐藏网络"
                    else "请选择网络后输入密码；扫描结果来自傻妞，不是手机")
                if (choices.isNotEmpty()) com.google.android.material.dialog.MaterialAlertDialogBuilder(activity)
                    .setTitle("傻妞附近的 Wi-Fi")
                    .setItems(choices.map { "${it.ssid} · ${it.rssi} dBm" }.toTypedArray()) { _, index ->
                        if (live()) {
                            network.setText(choices[index].ssid); password.text.clear()
                            replacePassword.isChecked = false; password.requestFocus()
                        }
                    }.setNegativeButton("取消", null).show()
            }, onFailure = { note("扫描结果格式无效，可手动输入网络名称") })
            return
        }
        if (snapshot.error != 0) {
            if (command == DeviceControlProtocol.Command.CONFIG_READ && snapshot.error in listOf(-11, -16) &&
                SystemClock.elapsedRealtime() < deadline) {
                phase = Phase.VERIFY
                handler.postDelayed({ if (active) read(readVerifying) }, 200)
                return
            }
            fail(if (snapshot.error in listOf(-138, -95, -38)) "此固件不支持独立设备设置，请安装匹配固件"
                else "设备返回 ${snapshot.error}；原配置不会被占位密钥覆盖，请重新读取")
            return
        }
        when (command) {
            DeviceControlProtocol.Command.CONFIG_READ -> {
                val chunk = snapshot.configChunk ?: run { fail("缺少配置回读"); return }
                if (chunk.totalLength !in 56..DeviceSettings.MAX_PUBLIC) { fail("配置长度无效"); return }
                if (readBytes == null) readBytes = ByteArray(chunk.totalLength)
                val buffer = readBytes!!
                if (buffer.size != chunk.totalLength || offset !in buffer.indices) { fail("配置回读不一致"); return }
                val count = minOf(16, buffer.size - offset)
                chunk.bytes.copyInto(buffer, offset, 0, count); offset += count
                if (offset < buffer.size) {
                    submit(command, ByteBuffer.allocate(4).putInt((DeviceSettings.KIND shl 16) or offset).array())
                } else {
                    val state = try { DeviceSettings.decode(buffer) } catch (_: Exception) { fail("配置回读格式无效"); return }
                    buffer.fill(0); readBytes = null
                    current = state
                    if (readVerifying && state.operation == operationHex() && state.state in listOf(1, 4) &&
                        SystemClock.elapsedRealtime() < deadline) {
                        phase = Phase.VERIFY
                        note(if (state.state == 4) "设备提交结果暂不确定，继续查询同一操作…" else "设备正在持久化配置…")
                        handler.postDelayed({ if (active) read(true) }, 250)
                        return
                    }
                    reconcile(state)
                    populate(state)
                    phase = Phase.IDLE; applied = false
                    session.finishConfigTransaction(lastMessage)
                    editable(true)
                }
            }
            DeviceControlProtocol.Command.CONFIG_BEGIN, DeviceControlProtocol.Command.CONFIG_APPEND -> sendNext()
            DeviceControlProtocol.Command.CONFIG_APPLY -> {
                payload?.fill(0); payload = null
                phase = Phase.VERIFY; deadline = SystemClock.elapsedRealtime() + 30_000
                note("设备已接收，正在回读持久化结果；联网状态将单独更新")
                read(true)
            }
            DeviceControlProtocol.Command.CONFIG_CANCEL -> { phase = Phase.IDLE; editable(true) }
            else -> Unit
        }
    }
    private fun operationHex() = operation?.joinToString("") { "%02x".format(it.toInt() and 255) }
    private fun reconcile(state: DeviceSettings.Public) {
        val pending = preferences.getString("$deviceId.operation", null)
        val revision = preferences.getLong("$deviceId.revision", -1)
        if (pending != null && state.operation == pending && state.state == 2 && state.result == 0 && state.revision == revision + 1) {
            preferences.edit().remove("$deviceId.operation").remove("$deviceId.revision").commit()
            note("已保存并回读确认 · 配置版本 ${state.revision}。联网结果见下方状态，保存成功不等于云服务可用。")
            android.widget.Toast.makeText(activity, "配置已保存并回读确认", android.widget.Toast.LENGTH_LONG).show()
        } else if (pending != null && state.operation == pending && state.state in listOf(1, 4)) {
            note("该次保存结果仍待确认，请重新读取；不要重复提交")
        } else {
            if (pending != null && (state.operation != pending || state.state == 3))
                preferences.edit().remove("$deviceId.operation").remove("$deviceId.revision").commit()
            note(if (pending == null) "已读取设备配置，密钥和密码不会返回；可以独立修改 Wi-Fi 或模型"
                else "已读取真实配置；上次操作未得到匹配的成功回执，请核对后再编辑")
        }
    }
    private fun populate(state: DeviceSettings.Public) {
        network.setText(state.ssid); password.text.clear(); replacePassword.isChecked = false
        url.setText(state.baseUrl); key.text.clear(); dialect.setSelection(if (state.dialect == 1) 1 else 0)
        asr.setText(state.asr.ifEmpty { "mimo-v2.5-asr" }); chat.setText(state.chat.ifEmpty { "mimo-v2.5" }); tts.setText(state.tts.ifEmpty { "mimo-v2.5-tts" })
    }
    private fun save(cloud: Boolean) {
        val state = current ?: return
        if (!live() || phase != Phase.IDLE) return
        if (preferences.contains("$deviceId.operation")) { note("上次保存尚未确认，请先重新读取"); return }
        val op = DeviceSettings.operation()
        val pass = if (!cloud && (replacePassword.isChecked || password.length() != 0)) CharArray(password.length()) { password.text[it] } else null
        val api = CharArray(key.length()) { key.text[it] }
        val name = network.text.toString()
        val address = url.text.toString().trim()
        val models = listOf(asr.text.toString().trim(), chat.text.toString().trim(), tts.text.toString().trim())
        val selectedDialect = if (dialect.selectedItemPosition == 0) CloudSettings.Dialect.MIMO else CloudSettings.Dialect.OPENAI_CHAT_AUDIO
        val localError = if (cloud) CloudSettings.inputError(address, api, models[0], models[1], models[2], allowMissingKey = state.hasKey)
            else if (pass != null) ProvisionSettings.inputError(name, pass)
            else if (name != state.ssid) "请输入所选网络的密码；无密码网络请勾选开放网络" else null
        if (localError != null) { pass?.fill('\u0000'); api.fill('\u0000'); op.fill(0); note(localError); return }
        phase = Phase.RESOLVING; editable(false)
        note(if (cloud) "正在准备云服务配置；新主机将由手机验证 TLS" else "正在准备 Wi-Fi 修改")
        val ownTicket = ++ticket
        Thread {
            val result = runCatching {
                if (cloud) {
                    val uri = URI(address)
                    val port = if (uri.port == -1) 443 else uri.port
                    val changedEndpoint = !state.hasCloud || uri.host != state.host || port != state.port
                    require(!changedEndpoint || api.isNotEmpty()) { "更换服务主机必须输入新的 API Key" }
                    val verified = if (changedEndpoint) CloudEndpoint.resolve(address) else null
                    val record = CloudSettings.encode(address, api, selectedDialect, models[0], models[1], models[2], allowMissingKey = state.hasKey)
                    try { DeviceSettings.patch(state, op, cloud = record, replaceKey = api.isNotEmpty(), endpoint = verified) }
                    finally { record.fill(0) }
                } else DeviceSettings.patch(state, op, ssid = name, password = pass)
            }
            pass?.fill('\u0000'); api.fill('\u0000')
            handler.post {
                if (!live() || ticket != ownTicket) { result.getOrNull()?.fill(0); op.fill(0); return@post }
                result.fold(onSuccess = { bytes ->
                    payload?.fill(0); payload = bytes; operation?.fill(0); operation = op
                    password.text.clear(); key.text.clear(); offset = 0; applied = false
                    phase = Phase.BEGIN; deadline = SystemClock.elapsedRealtime() + 30_000
                    submit(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(DeviceSettings.KIND).putInt(bytes.size).array())
                }, onFailure = {
                    op.fill(0); phase = Phase.IDLE; editable(true)
                    note("配置准备失败；请检查网络字段、服务地址以及新主机的 TLS 连接。没有向设备提交。")
                })
            }
        }.start()
    }
    private fun sendNext() {
        val bytes = payload ?: run { fail("缺少待提交配置"); return }
        if (offset < bytes.size) {
            val count = minOf(appendMax.coerceIn(32, 512), bytes.size - offset)
            phase = Phase.APPEND
            val part = bytes.copyOfRange(offset, offset + count); offset += count
            submit(DeviceControlProtocol.Command.CONFIG_APPEND, part)
        } else {
            val op = operationHex() ?: run { fail("缺少操作标识"); return }
            val revision = current?.revision ?: return
            if (!preferences.edit().putString("$deviceId.operation", op).putLong("$deviceId.revision", revision).commit()) {
                fail("无法保存操作回执，未发送最终提交"); return
            }
            phase = Phase.APPLY; applied = true
            submit(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
        }
    }
    private fun fail(reason: String) {
        ticket++; waiting = null; readBytes?.fill(0); readBytes = null
        retryBytes?.fill(0); retryBytes = null
        payload?.fill(0); payload = null
        note(reason)
        phase = Phase.IDLE
        if (!applied) session.cancelConfigTransaction()
        else session.finishConfigTransaction(reason)
        editable(live())
    }
    override fun close() {
        if (!active) return
        active = false; ticket++; phase = Phase.CLOSING
        handler.removeCallbacksAndMessages(null)
        retryBytes?.fill(0); retryBytes = null
        resultSubscription?.cancel(); stateSubscription?.cancel()
        resultSubscription = null; stateSubscription = null
        contentLayoutListener?.let { listener -> contentDecor?.removeOnLayoutChangeListener(listener) }
        contentDecor = null; contentLayoutListener = null
        if (!applied) session.cancelConfigTransaction() else session.finishConfigTransaction()
        readBytes?.fill(0); payload?.fill(0); operation?.fill(0)
        password.text.clear(); key.text.clear()
        dialog?.let { if (it.isShowing) it.dismiss() }; dialog = null
        if (embeddedHost != null) activity.getSystemService(android.view.inputmethod.InputMethodManager::class.java)
            .hideSoftInputFromWindow(editorContent.windowToken, 0)
        embeddedHost?.removeView(editorContent)
        if (embeddedHost != null) activity.window.setSoftInputMode(previousSoftInputMode)
        if (embeddedHost != null && !secureFlagWasSet) activity.window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
        finished(if (applied) "保存结果待回读；未自动重发操作" else lastMessage)
    }
}
