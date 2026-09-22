// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.app.Activity
import androidx.appcompat.app.AlertDialog
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
    private val finished: (String) -> Unit,
) : AutoCloseable {
    private enum class Phase { IDLE, READING, RESOLVING, BEGIN, APPEND, APPLY, VERIFY, CLOSING }
    private val handler = Handler(Looper.getMainLooper())
    private val preferences = activity.getSharedPreferences("shaniu-settings-receipts", Activity.MODE_PRIVATE)
    private val design = com.shaniu.companion.CompanionDesign(activity)
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
    private val box = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(24), dp(16), dp(24), dp(16)) }
    private val fieldContainers = linkedMapOf<View, View>()
    private val message = TextView(activity).apply {
        text = "正在通过已认证蓝牙读取设备配置…"; textSize = 14f
        setTextColor(design.muted); box.addView(this)
    }
    private val network = field("Wi-Fi 名称")
    private val password = field("新 Wi-Fi 密码（留空不修改）", true)
    private val replacePassword = CheckBox(activity).apply { text = "替换 Wi-Fi 密码（勾选且留空表示开放网络）"; box.addView(this) }
    private val saveWifi = button("仅保存 Wi-Fi") { save(false) }
    private val url = field("HTTPS 服务地址")
    private val key = field("新 API Key（留空保留原密钥）", true)
    private val dialect = Spinner(activity).apply {
        adapter = ArrayAdapter(activity, android.R.layout.simple_spinner_dropdown_item, listOf("MiMo", "Chat Completions 音频"))
        box.addView(this)
    }
    private val asr = field("语音识别模型")
    private val chat = field("对话模型")
    private val tts = field("语音合成模型")
    private val saveCloud = button("仅保存云服务和模型") { save(true) }
    private val reload = button("重新读取设备配置") { read(false) }
    private var phase = Phase.IDLE
    private var active = true
    private var connectionLost = false
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
    private var dialog: AlertDialog? = null
    private var retryBytes: ByteArray? = null
    private var lastMessage = "设备配置未修改"

    init {
        listOf<View>(network, password, replacePassword, saveWifi).forEach { (fieldContainers[it] ?: it).visibility = if (cloudPage) View.GONE else View.VISIBLE }
        listOf<View>(url, key, dialect, asr, chat, tts, saveCloud).forEach { (fieldContainers[it] ?: it).visibility = if (cloudPage) View.VISIBLE else View.GONE }
        resultSubscription = session.observeResults(::received)
        stateSubscription = session.observe { state ->
            if (active && !connectionLost && (!state.authenticated || state.generation != generation)) {
                // Publishing a terminal transaction state notifies this observer again.
                connectionLost = true
                fail(if (applied) "连接已变化，保存结果待回读；不会自动重发" else "连接已变化，请关闭后重新打开设置")
                editable(false)
                handler.post { if (active) close() }
            }
        }
        dialog = com.google.android.material.dialog.MaterialAlertDialogBuilder(activity).setTitle(if (cloudPage) "云服务与模型" else "Wi-Fi 网络")
            .setView(ScrollView(activity).apply { addView(box) })
            .setNegativeButton("关闭", null).create().also {
                it.setOnDismissListener { close() }
                it.show()
                it.window?.addFlags(android.view.WindowManager.LayoutParams.FLAG_SECURE)
                it.window?.setSoftInputMode(android.view.WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE)
            }
        read(false)
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
    private fun note(text: String) { message.text = text; lastMessage = text }
    private fun editable(enabled: Boolean) {
        listOf<View>(network, password, replacePassword, saveWifi, url, key, dialect, asr, chat, tts, saveCloud).forEach { it.isEnabled = enabled }
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
        ticket++; offset = 0; readBytes?.fill(0); readBytes = null
        readVerifying = verify; phase = Phase.READING
        if (!verify) deadline = SystemClock.elapsedRealtime() + 15_000
        editable(false)
        submit(DeviceControlProtocol.Command.CONFIG_READ, ByteBuffer.allocate(4).putInt(DeviceSettings.KIND shl 16).array())
    }
    private fun received(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (!live() || command != waiting) return
        waiting = null
        if (snapshot.error != 0) {
            if (command == DeviceControlProtocol.Command.CONFIG_READ && snapshot.error in listOf(-11, -16) &&
                SystemClock.elapsedRealtime() < deadline) {
                phase = Phase.VERIFY
                handler.postDelayed({ if (active) read(readVerifying) }, 200)
                return
            }
            fail(if (snapshot.error == -95) "此固件不支持独立设备设置，请安装匹配固件"
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
            note("配置已持久化并回读确认；Wi-Fi 连接和云端验证是后续独立状态")
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
        val pass = if (!cloud && replacePassword.isChecked) CharArray(password.length()) { password.text[it] } else null
        val api = CharArray(key.length()) { key.text[it] }
        val name = network.text.toString()
        val address = url.text.toString().trim()
        val models = listOf(asr.text.toString().trim(), chat.text.toString().trim(), tts.text.toString().trim())
        val selectedDialect = if (dialect.selectedItemPosition == 0) CloudSettings.Dialect.MIMO else CloudSettings.Dialect.OPENAI_CHAT_AUDIO
        val localError = if (cloud) CloudSettings.inputError(address, api, models[0], models[1], models[2], allowMissingKey = state.hasKey)
            else if (pass != null) ProvisionSettings.inputError(name, pass)
            else if (password.length() != 0) "请勾选替换 Wi-Fi 密码，避免输入被忽略"
            else if (name != state.ssid) "更换网络需要明确勾选替换密码（开放网络可留空）" else null
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
        if (!applied) session.cancelConfigTransaction() else session.finishConfigTransaction()
        readBytes?.fill(0); payload?.fill(0); operation?.fill(0)
        password.text.clear(); key.text.clear()
        dialog?.let { if (it.isShowing) it.dismiss() }; dialog = null
        finished(if (applied) "保存结果待回读；未自动重发操作" else lastMessage)
    }
}
