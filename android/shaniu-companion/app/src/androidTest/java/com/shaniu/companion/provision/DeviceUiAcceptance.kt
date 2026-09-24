// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.app.Instrumentation
import android.app.Instrumentation.ActivityMonitor
import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.view.WindowInsets
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.view.inputmethod.InputMethodManager
import android.widget.ScrollView
import android.widget.TextView
import com.shaniu.companion.MainActivity
import com.shaniu.companion.ota.BkpackInspector
import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.security.KeyStore
import java.util.UUID
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/** Synthetic in-memory snapshots only. No binding, credential, BLE or device
 * mutation. Reflection keeps fixture injection out of the production APK API.
 */
internal object DeviceUiAcceptance {
    /** Render real Views with isolated public fixtures; no production preview path. */
    fun runGallery(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(Intent(instrumentation.targetContext, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
        val session = MainActivity::class.java.getDeclaredMethod("getDirectSession").apply { isAccessible = true }
            .invoke(activity) as DeviceControlSession
        val stateField = DeviceControlSession::class.java.getDeclaredField("state").apply { isAccessible = true }
        val render = MainActivity::class.java.getDeclaredMethod("render").apply { isAccessible = true }
        val base = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0,
            memorySupported = true, memoryEnabled = false, wifiReady = false, otaSupported = true)
        fun scene(tab: Int, state: DeviceControlSession.State = session.current()) {
            onUi(instrumentation) {
                stateField.set(session, state)
                field("currentTab").set(activity, tab)
                render.invoke(activity)
            }
        }
        fun capture(name: String, root: () -> View = { activity.window.decorView }) {
            instrumentation.waitForIdleSync()
            Thread.sleep(250)
            onUi(instrumentation) {
                val view = root()
                val bitmap = Bitmap.createBitmap(view.width, view.height, Bitmap.Config.ARGB_8888)
                val canvas = Canvas(bitmap)
                view.draw(canvas)
                val paint = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
                    color = android.graphics.Color.rgb(120, 30, 30)
                    textSize = 14 * activity.resources.displayMetrics.density
                }
                canvas.drawText("模拟状态 · 无真实设备连接", 16f, bitmap.height - 24f, paint)
                File(activity.cacheDir, "gallery-$name.png").outputStream().use {
                    check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
                }
                bitmap.recycle()
            }
        }
        fun assertFooterFullyVisible(editor: DeviceSettingsEditor, dialog: android.app.Dialog) {
            val decor = checkNotNull(dialog.window).decorView
            val windowFrame = Rect().also { decor.getWindowVisibleDisplayFrame(it) }
            val inset = decor.rootWindowInsets
            val insetText = if (inset == null) "none" else {
                val bars = inset.getInsets(WindowInsets.Type.systemBars())
                val ime = inset.getInsets(WindowInsets.Type.ime())
                "bars=${bars.left},${bars.top},${bars.right},${bars.bottom};ime=${ime.left},${ime.top},${ime.right},${ime.bottom}"
            }
            val footer = DeviceSettingsEditor::class.java.getDeclaredField("footer").apply { isAccessible = true }
                .get(editor) as ViewGroup
            listOf("saveAction", "closeAction").map { name ->
                DeviceSettingsEditor::class.java.getDeclaredField(name).apply { isAccessible = true }
                    .get(editor) as com.google.android.material.button.MaterialButton
            }.forEach { button ->
                val visible = Rect()
                val position = IntArray(2).also { button.getLocationOnScreen(it) }
                val bar = button.parent as? View
                val children = (bar as? ViewGroup)?.let { group ->
                    (0 until group.childCount).joinToString(";") { index ->
                        val child = group.getChildAt(index)
                        "${child.javaClass.simpleName}[${child.left},${child.top},${child.right},${child.bottom};h=${child.height};min=${child.minimumHeight}]"
                    }
                } ?: "none"
                check(button.parent === footer && button.isClickable && button.isEnabled &&
                    button.height >= 48 * activity.resources.displayMetrics.density &&
                    button.getGlobalVisibleRect(visible) && visible.height() == button.height &&
                    visible.width() == button.width) {
                    "dialog footer action clipped: ${button.text}; button=${position[0]},${position[1]}," +
                        "${button.width}x${button.height};visible=$visible;bar=${bar?.javaClass?.simpleName}" +
                        "[h=${bar?.height};min=${bar?.minimumHeight};top=${bar?.top};bottom=${bar?.bottom};children=$children];" +
                        "decor=${decor.width}x${decor.height};window=$windowFrame;insets=$insetText"
                }
            }
        }
        try {
            onUi(instrumentation) { field("provisionedDeviceId").set(activity, "ui-synthetic-device") }
            scene(0, DeviceControlSession.State(connection = DeviceControlSession.Connection.CONNECTED,
                authenticated = true, snapshot = base, snapshotFresh = true))
            capture("connected-offline")
            onUi(instrumentation) {
                field("directDiscoveryVisible").set(activity, true)
                field("directScanFinished").set(activity, true)
            }
            scene(0, session.current().copy(connection = DeviceControlSession.Connection.CONNECTING,
                authenticated = false, snapshotFresh = false))
            capture("connecting")
            scene(0, session.current().copy(connection = DeviceControlSession.Connection.DISCONNECTED,
                error = "设备身份验证失败，请核对认领设备"))
            capture("authentication-failed")
            onUi(instrumentation) {
                field("directScanFinished").set(activity, true)
                field("directDiscoveryVisible").set(activity, true)
                field("directMessage").set(activity, "未发现设备，请确认傻妞已开机并在附近。")
            }
            scene(0)
            capture("discovery-empty")
            fun setDiscoveryCandidates(vararg fixture: Pair<String, String>) {
                onUi(instrumentation) {
                    val adapter = checkNotNull(android.bluetooth.BluetoothAdapter.getDefaultAdapter()) {
                        "synthetic discovery gallery requires the emulator Bluetooth facade"
                    }
                    val candidates = field("directCandidates").get(activity) as MutableList<Any>
                    val candidateType = Class.forName("com.shaniu.companion.MainActivity\$DirectCandidate")
                    val constructor = candidateType.getDeclaredConstructor(
                        android.bluetooth.BluetoothDevice::class.java,
                        String::class.java,
                        Long::class.javaPrimitiveType,
                    ).apply { isAccessible = true }
                    val epoch = field("directEpoch").getLong(activity)
                    candidates.clear()
                    fixture.forEach { (name, address) ->
                        // getRemoteDevice only creates an opaque local handle; this
                        // gallery never scans, connects, or sends a credential.
                        candidates += constructor.newInstance(adapter.getRemoteDevice(address), name, epoch)
                    }
                    field("directDiscoveryVisible").set(activity, true)
                    field("directScanFinished").set(activity, true)
                    field("directMessage").set(activity, "已完成查找，请选择要验证的设备。")
                }
            }
            setDiscoveryCandidates("傻妞客厅" to "02:00:00:00:00:11")
            scene(0, session.current().copy(connection = DeviceControlSession.Connection.DISCONNECTED, error = null))
            capture("discovery-one-candidate")
            // Every connection entry must expose the same candidate without
            // starting another scan or manufacturing an authenticated state.
            val discoveryEpoch = field("directEpoch").getLong(activity)
            val discoveryGeneration = session.current().generation
            for (tab in listOf(5, 2, 4, 0)) {
                scene(tab)
                onUi(instrumentation) {
                    check(findView(activity.window.decorView) {
                        it is TextView && it.text.toString().startsWith("傻妞客厅 · 00:11\n")
                    } != null) { "candidate missing from tab $tab" }
                    check(findView(activity.window.decorView) {
                        it.contentDescription?.toString() == "傻妞客厅，未验证设备，身份以安全认领验证为准" &&
                            it.isEnabled && it.isClickable
                    } != null) { "candidate not selectable from tab $tab" }
                    check(field("directEpoch").getLong(activity) == discoveryEpoch)
                    check(session.current().generation == discoveryGeneration && !session.current().authenticated)
                    check(field("directScanner").get(activity) == null)
                }
            }
            scene(5)
            capture("settings-discovery-one-candidate")
            setDiscoveryCandidates(
                "傻妞客厅" to "02:00:00:00:00:11",
                "傻妞书房" to "02:00:00:00:00:12",
                "傻妞卧室" to "02:00:00:00:00:13",
            )
            scene(0)
            capture("discovery-multiple-candidates")
            onUi(instrumentation) { field("provisionedDeviceId").set(activity, "") }
            scene(0)
            capture("discovery-unbound-candidates")
            check(!session.current().authenticated)
            onUi(instrumentation) { field("provisionedDeviceId").set(activity, "ui-synthetic-device") }
            scene(2, session.current().copy(connection = DeviceControlSession.Connection.CONNECTED,
                authenticated = true, snapshotFresh = true))
            capture("customization")
            scene(3)
            capture("privacy")
            onUi(instrumentation) {
                field("otaStatusGeneration").set(activity, session.current().generation)
                field("otaStatus").set(activity, DeviceControlProtocol.OtaStatus(2, 1, 35, 100, -115))
            }
            scene(4)
            capture("ota-progress")
            onUi(instrumentation) { field("otaStatus").set(activity, DeviceControlProtocol.OtaStatus(3, 8, 35, 100, -5)) }
            scene(4)
            capture("ota-failure")
            val generation = session.current().generation
            val observers = DeviceControlSession::class.java.getDeclaredField("observers").apply { isAccessible = true }
            val count = (observers.get(session) as Set<*>).size
            repeat(20) {
                for (title in listOf("设备", "定制", "更新", "设置")) {
                    onUi(instrumentation) {
                        val button = checkNotNull(findView(activity.window.decorView) {
                            it is TextView && it.text.toString() == title && it.isClickable
                        })
                        check(button.performClick())
                    }
                    instrumentation.waitForIdleSync()
                }
            }
            check(session.current().generation == generation)
            check((observers.get(session) as Set<*>).size == count)
            capture("settings")
            onUi(instrumentation) {
                activity.requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            Thread.sleep(1000)
            instrumentation.waitForIdleSync()
            check(!activity.isDestroyed) { "rotation destroyed foreground task owner" }
            check(session.current().generation == generation)
            check((observers.get(session) as Set<*>).size == count)
            check(activity.resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE)
            capture("landscape-settings")
            onUi(instrumentation) {
                activity.requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
            Thread.sleep(1000)
            instrumentation.waitForIdleSync()
            for (cloud in listOf(false, true)) {
                lateinit var editor: DeviceSettingsEditor
                lateinit var dialog: android.app.Dialog
                onUi(instrumentation) {
                    editor = DeviceSettingsEditor(activity, session, "ui-synthetic-device", 32, cloud) { }
                    val sample = DeviceSettings.Public(0, 7, "0".repeat(32), 0,
                        true, true, true, true, 443, 0, "演示网络", "api.example.invalid", "/v1",
                        "mimo-v2.5-asr", "mimo-v2.5", "mimo-v2.5-tts")
                    // populate() fills fields only; the production footer also
                    // requires an authoritative current record before enabling save.
                    DeviceSettingsEditor::class.java.getDeclaredField("current").apply { isAccessible = true }
                        .set(editor, sample)
                    DeviceSettingsEditor::class.java.getDeclaredMethod("populate", DeviceSettings.Public::class.java)
                        .apply { isAccessible = true }.invoke(editor, sample)
                    DeviceSettingsEditor::class.java.getDeclaredMethod("editable", Boolean::class.javaPrimitiveType)
                        .apply { isAccessible = true }.invoke(editor, true)
                    DeviceSettingsEditor::class.java.getDeclaredField("message").apply { isAccessible = true }
                        .let { (it.get(editor) as TextView).text = "模拟状态 · 配置 revision 7；没有提交到真实设备" }
                    dialog = DeviceSettingsEditor::class.java.getDeclaredField("dialog").apply { isAccessible = true }
                        .get(editor) as android.app.Dialog
                }
                try {
                    capture(if (cloud) "cloud-models" else "wifi-settings") { dialog.window!!.decorView }
                    onUi(instrumentation) { assertFooterFullyVisible(editor, dialog) }
                    val input = DeviceSettingsEditor::class.java.getDeclaredField(if (cloud) "url" else "network")
                        .apply { isAccessible = true }.get(editor) as EditText
                    onUi(instrumentation) {
                            input.requestFocus(); input.setSelection(input.length())
                            activity.getSystemService(InputMethodManager::class.java).showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
                    }
                    Thread.sleep(500)
                    instrumentation.uiAutomation.executeShellCommand("input text _draft").use { descriptor ->
                        android.os.ParcelFileDescriptor.AutoCloseInputStream(descriptor).use { it.readBytes() }
                    }
                    instrumentation.waitForIdleSync()
                    onUi(instrumentation) {
                        if (!cloud) {
                            check(input.text.toString().endsWith("_draft")) { "keyboard input not delivered" }
                            render.invoke(activity)
                            check(input.text.toString().endsWith("_draft")) { "status render overwrote draft" }
                        }
                        check(dialog.window!!.decorView.rootWindowInsets.isVisible(WindowInsets.Type.ime())) { "IME not visible" }
                        val formScroll = DeviceSettingsEditor::class.java.getDeclaredField("formScroll").apply { isAccessible = true }
                            .get(editor) as ScrollView
                        // Keep exercising the long-form scroll path, then return
                        // to the focused field: a footer-only dialog is not usable.
                        formScroll.scrollTo(0, formScroll.getChildAt(0).height)
                        val fieldRect = Rect()
                        input.getDrawingRect(fieldRect)
                        formScroll.offsetDescendantRectToMyCoords(input, fieldRect)
                        formScroll.scrollTo(0, (fieldRect.centerY() - formScroll.height / 2).coerceAtLeast(0))
                    }
                    // Wait for IME/focus layout and any posted scroll before
                    // checking the same settled frame that will be captured.
                    Thread.sleep(350)
                    instrumentation.waitForIdleSync()
                    onUi(instrumentation) {
                        val formScroll = DeviceSettingsEditor::class.java.getDeclaredField("formScroll").apply { isAccessible = true }
                            .get(editor) as ScrollView
                        val fieldVisible = Rect()
                        check(formScroll.height >= 48 * activity.resources.displayMetrics.density &&
                            input.getGlobalVisibleRect(fieldVisible) && fieldVisible.height() == input.height) {
                            "keyboard editor form unavailable: scroll=${formScroll.width}x${formScroll.height};field=$fieldVisible"
                        }
                    }
                    instrumentation.waitForIdleSync()
                    capture(if (cloud) "cloud-keyboard" else "wifi-keyboard") { dialog.window!!.decorView }
                    onUi(instrumentation) {
                        // v34 起保存动作属于固定页脚，不再检查已隐藏的旧表单按钮。
                        assertFooterFullyVisible(editor, dialog)
                        activity.getSystemService(InputMethodManager::class.java).hideSoftInputFromWindow(input.windowToken, 0)
                    }
                }
                finally { onUi(instrumentation) { editor.close() } }
            }
            val provision = instrumentation.startActivitySync(Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as ProvisionActivity
            try { capture("onboarding") { provision.window.decorView } }
            finally { onUi(instrumentation) { provision.finish() } }
        } finally {
            onUi(instrumentation) { activity.finish() }
            instrumentation.waitForIdleSync()
        }
    }

    /** Local ProvisionActivity validation only: no bootstrap, binding, DNS, BLE or key material. */
    fun runProvisionInputValidationProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        fun field(name: String) = ProvisionActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val showPage = ProvisionActivity::class.java.getDeclaredMethod(
            "showPage", Int::class.javaPrimitiveType!!,
        ).apply { isAccessible = true }
        try {
            instrumentation.runOnMainSync {
                showPage.invoke(activity, 1)
                (field("ssid").get(activity) as TextView).text = "test-only-wifi"
                (field("password").get(activity) as TextView).text = "test-only"
                (field("cloudUrl").get(activity) as TextView).text = "https://cloud.example/v1"
                (field("cloudKey").get(activity) as TextView).text = ""
                (field("asrModel").get(activity) as TextView).text = "test-asr"
                (field("chatModel").get(activity) as TextView).text = "test-chat"
                (field("ttsModel").get(activity) as TextView).text = "test-tts"
                fun views(root: View): List<View> = listOf(root) + if (root is ViewGroup)
                    (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
                val save = views(activity.window.decorView).single {
                    it is TextView && it.text.toString() == "保存并连接"
                }
                repeat(2) {
                    save.performClick()
                    check((field("status").get(activity) as TextView).text.toString() ==
                        "请填写语音服务 Key 后再保存；当前事务会同时保存 Wi-Fi 和语音服务。")
                    check((field("ssid").get(activity) as TextView).text.toString() == "test-only-wifi")
                    check((field("password").get(activity) as TextView).text.toString() == "test-only")
                    check((field("cloudKey").get(activity) as TextView).text.isEmpty())
                    check(field("connection").get(activity) == null)
                    check(field("cloudResolving").get(activity) == false)
                    check(field("cloudLookupGeneration").get(activity) == 0L)
                }
            }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
            instrumentation.waitForIdleSync()
        }
    }

    /**
     * Emulator-only fixture: network resolution and BLE are replaced at their
     * narrow owners, but every claim transition below is decoded by the real
     * ProvisionClaimProtocol and consumed by ProvisionActivity's callback.
     */
    fun runProvisionEmulatorFlowProbe(instrumentation: Instrumentation, only: String? = null): String {
        val failures = mutableListOf<String>()
        val results = mutableListOf<String>()
        var selected = 0
        fun scenario(name: String, block: () -> Unit) {
            if (only != null && only != name) return
            selected++
            try {
                block()
                results += "$name=PASS"
            } catch (error: Throwable) {
                val detail = "$name=FAIL ${error.javaClass.simpleName}: ${error.message}"
                failures += detail; results += detail
            }
        }
        scenario("local_utf8_and_nonhex_password_rejection") {
            val activity = startProvision(instrumentation)
            val binding = TestBinding(instrumentation.targetContext)
            try {
                val resolverCalls = intArrayOf(0)
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = {
                        resolverCalls[0]++; throw IOException("fixture DNS must not run")
                    }, store = binding.store)
                    enterNetworkPage(activity)
                    fillCloudFields(activity)
                    /* Every rejection uses the actual Save button.  Keep the
                     * invalid value visible so a correction/retry is possible. */
                    field(activity, "ssid", TextView::class.java).text = "测".repeat(17)
                    saveButton(activity).performClick()
                    check(status(activity).contains("Wi-Fi"))
                    check(resolverCalls[0] == 0)
                    check(field(activity, "ssid", TextView::class.java).text.toString() == "测".repeat(17))
                    field(activity, "ssid", TextView::class.java).text = "test-wifi"
                    field(activity, "password", TextView::class.java).text = "g".repeat(64)
                    saveButton(activity).performClick()
                    check(status(activity).contains("Wi-Fi"))
                    check(resolverCalls[0] == 0)
                    check(field(activity, "password", TextView::class.java).text.toString() == "g".repeat(64))
                    field(activity, "password", TextView::class.java).text = ""
                    field(activity, "cloudUrl", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("HTTPS")); check(resolverCalls[0] == 0)
                    field(activity, "cloudUrl", TextView::class.java).text = "https://cloud.example/v1"
                    field(activity, "cloudKey", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("Key")); check(resolverCalls[0] == 0)
                    field(activity, "cloudKey", TextView::class.java).text = "test-only-key"
                    field(activity, "asrModel", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("模型")); check(resolverCalls[0] == 0)
                    field(activity, "asrModel", TextView::class.java).text = "test-asr"
                    field(activity, "chatModel", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("模型")); check(resolverCalls[0] == 0)
                    field(activity, "chatModel", TextView::class.java).text = "test-chat"
                    field(activity, "ttsModel", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("模型")); check(resolverCalls[0] == 0)
                    /* An open network has an empty password. Its actual Save
                     * passes local validation and reaches the injected resolver. */
                    fillCloudFields(activity)
                    field(activity, "password", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                }
                awaitUi(instrumentation, activity) { rawField(activity, "cloudResolving").get(activity) == false }
                onUi(instrumentation) {
                    check(resolverCalls[0] == 1)
                    check(rawField(activity, "connection").get(activity) == null)
                    check(field(activity, "password", TextView::class.java).text.isEmpty())
                    saveButton(activity).performClick()
                }
                awaitUi(instrumentation, activity) { rawField(activity, "cloudResolving").get(activity) == false }
                onUi(instrumentation) { check(resolverCalls[0] == 2) }
            } finally { finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("dns_failure_does_not_create_session") {
            val activity = startProvision(instrumentation)
            val binding = TestBinding(instrumentation.targetContext)
            try {
                val resolved = CountDownLatch(1)
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = {
                        resolved.countDown(); throw IOException("fixture DNS failure")
                    }, store = binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                check(resolved.await(3, TimeUnit.SECONDS))
                awaitUi(instrumentation, activity) { rawField(activity, "cloudResolving").get(activity) == false }
                onUi(instrumentation) {
                    check(status(activity) == "无法验证语音服务，请检查 HTTPS 地址及网络。")
                    check(rawField(activity, "connection").get(activity) == null)
                }
            } finally { finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("verify_ack_then_negative12_is_failure") {
            val activity = startProvision(instrumentation)
            val fixture = ClaimTransportFixture()
            val retryFixture = ClaimTransportFixture()
            val binding = TestBinding(instrumentation.targetContext)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, fixture)
                fixture.tls(); fixture.respond(3); fixture.respond(4)
                while (fixture.lastType() == 3) fixture.respond(4)
                check(fixture.lastType() == 4)
                fixture.respond(5)
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity) == "认领进行中，请保持设备连接。")
                    check(rawField(activity, "connection").get(activity) != null)
                }
                fixture.respond(7, -12)
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity).contains("设备验证") && status(activity).contains("-12"))
                    check(rawField(activity, "connection").get(activity) == null)
                    check(notSuccessfulUi(activity))
                    check(field(activity, "password", TextView::class.java).text.isEmpty())
                    check(field(activity, "cloudKey", TextView::class.java).text.isEmpty())
                }
                captureSyntheticView(instrumentation, activity, "emulator-flow-verify-failure.png")
                onUi(instrumentation) {
                    /* The returned button is the real failure path.  Re-enter
                     * with fresh synthetic inputs and a new transport; no
                     * terminal state is injected into the activity. */
                    (rawField(activity, "resultButton").get(activity) as android.widget.Button).performClick()
                    check(rawField(activity, "page").get(activity) == 0)
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() },
                        retryFixture, binding.store)
                    (rawField(activity, "nextButton").get(activity) as android.widget.Button).performClick()
                    check(rawField(activity, "page").get(activity) == 1)
                    fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, retryFixture)
                retryFixture.tls(); retryFixture.respond(3); retryFixture.respond(4)
                while (retryFixture.lastType() == 3) retryFixture.respond(4)
                check(retryFixture.lastType() == 4)
                retryFixture.respond(5)
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity) == "认领进行中，请保持设备连接。")
                    check(rawField(activity, "connection").get(activity) != null)
                }
                retryFixture.respond(6)
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity) == "设备已确认保存连接设置。")
                    check((rawField(activity, "resultButton").get(activity) as TextView).text.toString() == "返回首页")
                    check(binding.store.pending("emulator-claim") == null)
                    check(binding.store.boundDeviceId() == "emulator-claim")
                    check(field(activity, "password", TextView::class.java).text.isEmpty())
                    check(field(activity, "cloudKey", TextView::class.java).text.isEmpty())
                }
                captureSyntheticView(instrumentation, activity, "emulator-flow-committed.png")
            } finally {
                fixture.clear(); retryFixture.clear(); finishProvision(instrumentation, activity); binding.close()
            }
        }
        scenario("authentication_failure") {
            val activity = startProvision(instrumentation)
            val fixture = ClaimTransportFixture()
            val binding = TestBinding(instrumentation.targetContext)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, fixture)
                fixture.tls(); fixture.respond(7, -9)
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity).contains("设备认证") && status(activity).contains("-9"))
                    check(notSuccessfulUi(activity))
                }
            } finally { fixture.clear(); finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("transport_timeout_close") {
            val activity = startProvision(instrumentation)
            val fixture = ClaimTransportFixture()
            val binding = TestBinding(instrumentation.targetContext)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, fixture)
                fixture.timeoutClose()
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(status(activity).contains("连接超时") && status(activity).contains("重试"))
                    check(notSuccessfulUi(activity))
                }
            } finally { fixture.clear(); finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("cancelled_late_callback") {
            val activity = startProvision(instrumentation)
            val fixture = ClaimTransportFixture()
            val binding = TestBinding(instrumentation.targetContext)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, fixture)
                fixture.tls(); fixture.respond(3); fixture.respond(4)
                while (fixture.lastType() == 3) fixture.respond(4)
                check(fixture.lastType() == 4)
                onUi(instrumentation) {
                    val cancel = rawField(activity, "resultButton").get(activity) as android.widget.Button
                    cancel.performClick()
                }
                instrumentation.waitForIdleSync()
                fixture.lateCommitted()
                instrumentation.waitForIdleSync()
                onUi(instrumentation) {
                    check(notSuccessfulUi(activity))
                    check(status(activity) != "设备已确认保存连接设置。")
                    check(rawField(activity, "connection").get(activity) == null)
                    check(binding.store.pending("emulator-claim") != null)
                }
                val restarted = ProvisionBindingStore(ProvisionBindingStore.SharedPreferencesBackend(binding.preferences),
                    ControlKeyCipher.android(binding.alias))
                check(restarted.pending("emulator-claim") != null)
            } finally {
                fixture.clear(); finishProvision(instrumentation, activity)
                binding.close()
            }
        }
        scenario("delayed_resolver_double_click_then_back_ignores_late_failure") {
            val activity = startProvision(instrumentation)
            val binding = TestBinding(instrumentation.targetContext)
            try {
                val entered = CountDownLatch(1)
                val release = CountDownLatch(1)
                val calls = intArrayOf(0)
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = {
                        calls[0]++; entered.countDown()
                        check(release.await(3, TimeUnit.SECONDS))
                        throw IOException("late fixture DNS failure")
                    }, store = binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity)
                    saveButton(activity).performClick(); saveButton(activity).performClick()
                }
                check(entered.await(3, TimeUnit.SECONDS))
                onUi(instrumentation) { activity.onBackPressed() }
                release.countDown()
                awaitUi(instrumentation, activity) { rawField(activity, "cloudResolving").get(activity) == false }
                onUi(instrumentation) {
                    check(calls[0] == 1)
                    check(rawField(activity, "page").get(activity) == 0)
                    check(status(activity) == "请选择设备继续。")
                    check(rawField(activity, "connection").get(activity) == null)
                }
            } finally { finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("ime_input_scroll_bounds_and_secure_view_draw") {
            val activity = startProvision(instrumentation)
            val binding = TestBinding(instrumentation.targetContext)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, store = binding.store)
                    enterNetworkPage(activity)
                    field(activity, "ssid", EditText::class.java).requestFocus()
                    instrumentation.targetContext.getSystemService(InputMethodManager::class.java)
                        .showSoftInput(field(activity, "ssid", EditText::class.java), InputMethodManager.SHOW_IMPLICIT)
                }
                awaitUi(instrumentation, activity) {
                    activity.window.decorView.rootWindowInsets?.isVisible(WindowInsets.Type.ime()) == true
                }
                instrumentation.sendStringSync("ime-wifi")
                onUi(instrumentation) {
                    val scroll = findView(activity.window.decorView) { it is ScrollView } as ScrollView
                    scroll.smoothScrollTo(0, scroll.getChildAt(0).height)
                }
                awaitUi(instrumentation, activity) {
                    val save = saveButton(activity)
                    val bounds = Rect(); val visible = Rect()
                    activity.window.decorView.getWindowVisibleDisplayFrame(visible)
                    val completelyVisible = save.getGlobalVisibleRect(bounds) && bounds.width() > 0 &&
                        bounds.height() > 0 && bounds.top >= visible.top && bounds.bottom <= visible.bottom
                    if (!completelyVisible) return@awaitUi false
                    check((activity.window.attributes.flags and android.view.WindowManager.LayoutParams.FLAG_SECURE) != 0)
                    save.performClick()
                    val root = activity.window.decorView
                    val image = Bitmap.createBitmap(root.width.coerceAtLeast(1), root.height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
                    /* View.draw deliberately records app content only; secure
                     * system IME pixels are not part of this synthetic capture. */
                    root.draw(Canvas(image))
                    val output = File(instrumentation.targetContext.cacheDir,
                        "emulator-flow-layout-${UUID.randomUUID()}.png")
                    output.outputStream().use { check(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                    image.recycle()
                    check(output.length() > 0)
                    check(field(activity, "ssid", TextView::class.java).text.toString().contains("ime-wifi"))
                    true
                }
                awaitUi(instrumentation, activity) {
                    val message = status(activity)
                    if (message != "请填写语音服务 Key 后再保存；当前事务会同时保存 Wi-Fi 和语音服务。") {
                        return@awaitUi false
                    }
                    val bounds = Rect(); val visible = Rect()
                    activity.window.decorView.getWindowVisibleDisplayFrame(visible)
                    check(field(activity, "status", TextView::class.java).getGlobalVisibleRect(bounds) &&
                        bounds.top >= visible.top && bounds.bottom <= visible.bottom) {
                        "input error is not fully visible after Save: $bounds in $visible"
                    }
                    val root = activity.window.decorView
                    val image = Bitmap.createBitmap(root.width.coerceAtLeast(1), root.height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
                    root.draw(Canvas(image))
                    val output = File(instrumentation.targetContext.cacheDir, "emulator-flow-error.png")
                    output.outputStream().use { check(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                    image.recycle()
                    check(output.length() > 0)
                    true
                }
            } finally { finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("recreate_keeps_only_isolated_pending_binding") {
            var activity = startProvision(instrumentation)
            val fixture = ClaimTransportFixture()
            val binding = TestBinding(instrumentation.targetContext)
            val monitor = ActivityMonitor(ProvisionActivity::class.java.name, null, false)
            instrumentation.addMonitor(monitor)
            try {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity)
                    check(field(activity, "password", TextView::class.java).text.isNotEmpty())
                    check(field(activity, "cloudKey", TextView::class.java).text.isNotEmpty())
                }
                val unsaved = activity
                onUi(instrumentation) { activity.recreate() }
                activity = checkNotNull(monitor.waitForActivityWithTimeout(3_000) as? ProvisionActivity)
                onUi(instrumentation) {
                    /* No Save happened before this recreation.  onDestroy must
                     * still clear the old in-memory secret fields. */
                    check(field(unsaved, "password", TextView::class.java).text.isEmpty())
                    check(field(unsaved, "cloudKey", TextView::class.java).text.isEmpty())
                    check(field(activity, "password", TextView::class.java).text.isEmpty())
                    check(field(activity, "cloudKey", TextView::class.java).text.isEmpty())
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, fixture, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, fixture)
                fixture.tls(); fixture.respond(3); fixture.respond(4)
                while (fixture.lastType() == 3) fixture.respond(4)
                check(fixture.lastType() == 4); fixture.respond(5)
                instrumentation.waitForIdleSync()
                check(binding.store.pending("emulator-claim") != null)
                onUi(instrumentation) { activity.recreate() }
                activity = checkNotNull(monitor.waitForActivityWithTimeout(3_000) as? ProvisionActivity)
                onUi(instrumentation) {
                    check(field(activity, "password", TextView::class.java).text.isEmpty())
                    check(field(activity, "cloudKey", TextView::class.java).text.isEmpty())
                    rawField(activity, "bindingStore").set(activity, binding.store)
                    check(binding.store.pending("emulator-claim") != null)
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() },
                        ClaimTransportFixture(), binding.store)
                    (rawField(activity, "nextButton").get(activity) as android.widget.Button).performClick()
                }
                check(binding.store.pending("emulator-claim") != null)
            } finally {
                instrumentation.removeMonitor(monitor); fixture.clear()
                finishProvision(instrumentation, activity); binding.close()
            }
        }
        scenario("backgrounded_resolver_clears_progress_before_late_callback") {
            val activity = startProvision(instrumentation)
            val binding = TestBinding(instrumentation.targetContext)
            try {
                val entered = CountDownLatch(1)
                val release = CountDownLatch(1)
                val resolverReturned = CountDownLatch(1)
                var resolverCalls = 0
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = {
                        resolverCalls++
                        if (resolverCalls == 1) {
                            entered.countDown(); check(release.await(3, TimeUnit.SECONDS))
                            resolverReturned.countDown()
                            throw IOException("background late DNS failure")
                        }
                        throw IOException("retry resolver failure")
                    }, store = binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                check(entered.await(3, TimeUnit.SECONDS)) { "resolver did not start" }
                onUi(instrumentation) { check(activity.moveTaskToBack(true)) }
                val stoppedDeadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
                var stopped = false
                while (System.nanoTime() < stoppedDeadline && !stopped) {
                    onUi(instrumentation) {
                        stopped = rawField(activity, "foreground").get(activity) == false
                    }
                    if (!stopped) Thread.sleep(10)
                }
                check(stopped) { "onStop did not set foreground=false after moveTaskToBack" }
                onUi(instrumentation) {
                    check(rawField(activity, "cloudResolving").get(activity) == false) {
                        "cloud resolver remained active after onStop"
                    }
                    check(status(activity) != "正在验证语音服务连接…") {
                        "onStop left resolver progress text: ${status(activity)}"
                    }
                }
                release.countDown()
                check(resolverReturned.await(3, TimeUnit.SECONDS)) { "late resolver did not return" }
                /* The worker has returned; wait until its handler.post is no
                 * longer queued before bringing the original task forward. */
                instrumentation.waitForIdleSync()
                val manager = instrumentation.targetContext.getSystemService(ActivityManager::class.java)
                // Instrumentation plays the system Recents action. The app
                // itself does not need permission to reorder other tasks.
                instrumentation.uiAutomation.adoptShellPermissionIdentity(android.Manifest.permission.REORDER_TASKS)
                try { manager.moveTaskToFront(activity.taskId, 0) }
                finally { instrumentation.uiAutomation.dropShellPermissionIdentity() }
                awaitUi(instrumentation, activity) { rawField(activity, "foreground").get(activity) == true }
                onUi(instrumentation) {
                    check(rawField(activity, "page").get(activity) == 1) { "same activity did not resume on Wi-Fi page" }
                    check(status(activity) != "无法验证语音服务，请检查 HTTPS 地址及网络。") {
                        "late failure overwrote resumed page"
                    }
                    field(activity, "ssid", TextView::class.java).text = ""
                    saveButton(activity).performClick()
                    check(status(activity).contains("Wi-Fi 名称")) { "resumed page did not accept corrected input" }
                    field(activity, "ssid", TextView::class.java).text = "retry-wifi"
                    saveButton(activity).performClick()
                }
                awaitUi(instrumentation, activity) { rawField(activity, "cloudResolving").get(activity) == false }
                onUi(instrumentation) {
                    check(resolverCalls == 2) { "corrected Save did not retry resolver" }
                    check(status(activity) == "无法验证语音服务，请检查 HTTPS 地址及网络。")
                    check(rawField(activity, "connection").get(activity) == null)
                }
            } finally { finishProvision(instrumentation, activity); binding.close() }
        }
        scenario("recovery_cancel_before_and_after_connection_creation") {
            val type = Class.forName("com.shaniu.companion.provision.ProvisionActivity\$RecoveryAttempt")
            val constructor = type.getDeclaredConstructor().apply { isAccessible = true }
            val attach = type.getDeclaredMethod("attach", AutoCloseable::class.java).apply { isAccessible = true }
            repeat(2) { early ->
                val attempt = constructor.newInstance() as AutoCloseable
                var closed = 0
                val connection = AutoCloseable { closed++ }
                if (early == 0) attempt.close()
                attach.invoke(attempt, connection)
                check(closed == if (early == 0) 1 else 0)
                attempt.close(); attempt.close()
                check(closed == 1) { "late or cancelled recovery link was leaked or closed twice" }
            }
        }
        if (only != null) check(selected == 1) { "Unknown emulator flow scenario: $only" }
        check(failures.isEmpty()) { results.joinToString("\n") }
        return results.joinToString("; ")
    }

    /** Two invocation fixture for a root-controlled force-stop between calls.
     * It records only random test preference/key aliases, never the app's
     * provision_receipts identity. */
    fun runProvisionProcessRestartFixture(instrumentation: Instrumentation, resume: Boolean): String {
        val binding = if (resume) checkNotNull(TestBinding.openPersistent(instrumentation.targetContext)) {
            "No isolated emulator process fixture is pending"
        } else TestBinding.createPersistent(instrumentation.targetContext)
        val activity = startProvision(instrumentation)
        val transport = ClaimTransportFixture()
        var keepFixture = false
        try {
            if (!resume) {
                onUi(instrumentation) {
                    installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, transport, binding.store)
                    enterNetworkPage(activity); fillCloudFields(activity); saveButton(activity).performClick()
                }
                awaitStarted(instrumentation, transport)
                transport.tls(); transport.respond(3); transport.respond(4)
                while (transport.lastType() == 3) transport.respond(4)
                check(transport.lastType() == 4); transport.respond(5)
                instrumentation.waitForIdleSync()
                check(binding.store.pending("emulator-claim") != null)
                onUi(instrumentation) { activity.onBackPressed() }
                check(binding.store.pending("emulator-claim") != null)
                keepFixture = true
                return "PROCESS_FIXTURE_READY isolated pending receipt"
            }
            check(binding.store.pending("emulator-claim") != null)
            onUi(instrumentation) {
                installProvisionFixture(instrumentation, activity, resolver = { verifiedEndpoint() }, transport, binding.store)
                (rawField(activity, "nextButton").get(activity) as android.widget.Button).performClick()
            }
            onUi(instrumentation) {
                /* This fixture uses provision-bootstrap-v1, not an SN1 QR.
                 * Its production recovery route is SPV1 AUTH_OWNER/VERIFY;
                 * only screenBootstrap uses SDC1 control reconciliation. */
                check(rawField(activity, "page").get(activity) == 2)
                check(rawField(activity, "connection").get(activity) != null)
                check(rawField(activity, "recoveryControl").get(activity) == null)
                check((rawField(activity, "resultButton").get(activity) as TextView).text.toString() == "取消连接")
                check(binding.store.pending("emulator-claim") != null)
            }
            awaitStarted(instrumentation, transport)
            transport.tls()
            check(transport.lastType() == 7) { "legacy recovery did not authenticate owner" }
            transport.respond(3)
            check(transport.lastType() == 5) { "recovery must query VERIFY, never resubmit configuration" }
            return "PROCESS_FIXTURE_RESUMED legacy receipt opened; AUTH_OWNER then VERIFY, no repeated configuration"
        } finally {
            transport.clear(); finishProvision(instrumentation, activity)
            if (!keepFixture) {
                binding.close(); TestBinding.clearPersistentReference(instrumentation.targetContext)
            }
        }
    }

    private fun startProvision(instrumentation: Instrumentation) = instrumentation.startActivitySync(
        Intent(instrumentation.targetContext, ProvisionActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
    ) as ProvisionActivity

    private fun rawField(activity: ProvisionActivity, name: String) =
        ProvisionActivity::class.java.getDeclaredField(name).apply { isAccessible = true }

    private fun <T> field(activity: ProvisionActivity, name: String, type: Class<T>): T =
        requireNotNull(type.cast(rawField(activity, name).get(activity)))

    private fun status(activity: ProvisionActivity) = field(activity, "status", TextView::class.java).text.toString()

    private fun captureSyntheticView(instrumentation: Instrumentation, activity: ProvisionActivity,
                                     filename: String) {
        awaitUi(instrumentation, activity) {
            val root = activity.window.decorView
            val scroll = findView(root) { it is ScrollView } as? ScrollView ?: return@awaitUi false
            val title = field(activity, "titleLabel", TextView::class.java)
            val currentStatus = field(activity, "status", TextView::class.java)
            val result = field(activity, "resultButton", TextView::class.java)
            val visible = Rect().also { root.getWindowVisibleDisplayFrame(it) }
            fun fullyVisible(view: View): Boolean {
                val bounds = Rect()
                return view.getGlobalVisibleRect(bounds) && bounds.top >= visible.top && bounds.bottom <= visible.bottom
            }
            // Hidden pages intentionally retain pending layout requests until
            // shown again; they are not part of the frame being captured.
            fun layoutRequested(view: View): Boolean = view.visibility == View.VISIBLE && (view.isLayoutRequested ||
                (view as? ViewGroup)?.let { group ->
                    (0 until group.childCount).any { layoutRequested(group.getChildAt(it)) }
                } == true)
            if (layoutRequested(root) || scroll.scrollY != 0 || !fullyVisible(title) ||
                !fullyVisible(currentStatus) || !fullyVisible(result)) return@awaitUi false
            true
        }
        val drawn = CountDownLatch(2)
        lateinit var root: View
        val listener = object : android.view.ViewTreeObserver.OnPreDrawListener {
            override fun onPreDraw(): Boolean {
                drawn.countDown()
                if (drawn.count > 0) root.postInvalidateOnAnimation()
                return true
            }
        }
        onUi(instrumentation) {
            root = activity.window.decorView
            root.viewTreeObserver.addOnPreDrawListener(listener)
            root.postInvalidateOnAnimation()
        }
        try {
            check(drawn.await(3, TimeUnit.SECONDS)) { "two stable pre-draw frames did not arrive" }
        } finally {
            onUi(instrumentation) {
                if (root.viewTreeObserver.isAlive) root.viewTreeObserver.removeOnPreDrawListener(listener)
            }
        }
        onUi(instrumentation) {
            val image = Bitmap.createBitmap(root.width.coerceAtLeast(1), root.height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
            root.draw(Canvas(image))
            val output = File(instrumentation.targetContext.cacheDir, filename)
            output.outputStream().use { check(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            image.recycle()
            check(output.length() > 0)
        }
    }

    private fun notSuccessfulUi(activity: ProvisionActivity): Boolean =
        status(activity) != "设备已确认保存连接设置。" &&
            (rawField(activity, "resultButton").get(activity) as TextView).text.toString() != "返回首页"

    private fun saveButton(activity: ProvisionActivity): View {
        fun views(root: View): List<View> = listOf(root) + if (root is ViewGroup)
            (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
        return views(activity.window.decorView).single { it is TextView && it.text.toString() == "保存并连接" }
    }

    private fun findView(root: View, predicate: (View) -> Boolean): View? {
        if (predicate(root)) return root
        if (root is ViewGroup) for (index in 0 until root.childCount) {
            findView(root.getChildAt(index), predicate)?.let { return it }
        }
        return null
    }

    private fun enterNetworkPage(activity: ProvisionActivity) {
        (rawField(activity, "nextButton").get(activity) as android.widget.Button).performClick()
        check(rawField(activity, "page").get(activity) == 1)
    }

    private fun fillCloudFields(activity: ProvisionActivity) {
        field(activity, "ssid", TextView::class.java).text = "test-wifi"
        field(activity, "password", TextView::class.java).text = "pass1234"
        field(activity, "cloudUrl", TextView::class.java).text = "https://cloud.example/v1"
        field(activity, "cloudKey", TextView::class.java).text = "test-only-key"
        field(activity, "asrModel", TextView::class.java).text = "test-asr"
        field(activity, "chatModel", TextView::class.java).text = "test-chat"
        field(activity, "ttsModel", TextView::class.java).text = "test-tts"
    }

    private fun verifiedEndpoint() = CloudEndpoint.Verified(byteArrayOf(1, 1, 1, 1), byteArrayOf(1))

    private fun installProvisionFixture(instrumentation: Instrumentation, activity: ProvisionActivity,
                                        resolver: (String) -> CloudEndpoint.Verified,
                                        transport: ClaimTransportFixture? = null,
                                        store: ProvisionBindingStore? = null) {
        val binding = requireNotNull(store) { "Test fixture requires isolated binding storage" }
        val bootstrap = ProvisionBootstrap.parse(
            """{"protocol":"provision-bootstrap-v1","device_id":"emulator-claim","certificate_sha256":"${"ab".repeat(32)}","possession_secret":"${java.util.Base64.getEncoder().encodeToString(ByteArray(32) { 42 })}"}""".toCharArray())
        val adapter = checkNotNull(instrumentation.targetContext
            .getSystemService(android.bluetooth.BluetoothManager::class.java)?.adapter)
        rawField(activity, "bindingStore").set(activity, binding)
        rawField(activity, "bootstrap").set(activity, bootstrap)
        rawField(activity, "selected").set(activity, adapter.getRemoteDevice("02:00:00:00:00:01"))
        /* BLE discovery itself is outside this fixture. Mirror only the
         * selection UI's post-scan enabled/visible state; nextPage remains the
         * actual click path and rechecks selected/bootstrap/pending state. */
        (rawField(activity, "nextButton").get(activity) as android.widget.Button).apply {
            isEnabled = true
            visibility = View.VISIBLE
        }
        rawField(activity, "cloudResolver").set(activity, resolver)
        if (transport != null) rawField(activity, "connectionFactory").set(activity,
            connectionFactory(instrumentation, activity, transport, binding))
    }

    private fun connectionFactory(instrumentation: Instrumentation, activity: ProvisionActivity,
                                  transport: ClaimTransportFixture,
                                  store: ProvisionBindingStore? = null):
        (android.bluetooth.BluetoothDevice, ProvisionBootstrap, ByteArray,
            (ProvisionClaimProtocol.State) -> Unit, Boolean) -> ProvisioningConnection {
        val binding = store ?: rawField(activity, "bindingStore").get(activity) as ProvisionBindingStore
        return { device, identity, candidate, changed, recover ->
            ProvisioningConnection(instrumentation.targetContext, device, identity, candidate, changed,
                recover, binding, { events -> transport.install(events) })
        }
    }

    private fun awaitStarted(instrumentation: Instrumentation, transport: ClaimTransportFixture) {
        check(transport.started.await(3, TimeUnit.SECONDS))
        instrumentation.waitForIdleSync()
    }

    /** Propagate UI-thread assertions to the instrumentation thread so a
     * scenario can fail without terminating the process before later cases. */
    private fun onUi(instrumentation: Instrumentation, action: () -> Unit) {
        val failure = AtomicReference<Throwable?>()
        instrumentation.runOnMainSync {
            try { action() } catch (error: Throwable) { failure.set(error) }
        }
        failure.get()?.let { throw it }
    }

    private fun awaitUi(instrumentation: Instrumentation, activity: ProvisionActivity,
                        condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3)
        while (System.nanoTime() < deadline) {
            instrumentation.waitForIdleSync()
            var matched = false
            onUi(instrumentation) { matched = condition() }
            if (matched) return
            Thread.sleep(10)
        }
        throw AssertionError("UI condition did not become true")
    }

    private fun finishProvision(instrumentation: Instrumentation, activity: ProvisionActivity) {
        onUi(instrumentation) { activity.finish() }
        instrumentation.waitForIdleSync()
    }

    /** Owns only a unique test preferences file and Android Keystore alias. */
    private class TestBinding(private val context: Context, private val name: String = "emulator-claim-${UUID.randomUUID()}",
                              val alias: String = "emulator-claim-${UUID.randomUUID()}") {
        val preferences = context.getSharedPreferences(name, Context.MODE_PRIVATE)
        val store = ProvisionBindingStore(ProvisionBindingStore.SharedPreferencesBackend(preferences),
            ControlKeyCipher.android(alias))
        fun close() {
            preferences.edit().clear().commit()
            context.deleteSharedPreferences(name)
            runCatching { KeyStore.getInstance("AndroidKeyStore").apply { load(null) }.deleteEntry(alias) }
        }
        companion object {
            private const val INDEX = "emulator-process-fixture-index"
            private const val NAME = "preferences_name"
            private const val ALIAS = "keystore_alias"
            fun createPersistent(context: Context): TestBinding = TestBinding(context).also { fixture ->
                check(context.getSharedPreferences(INDEX, Context.MODE_PRIVATE).edit()
                    .putString(NAME, fixture.name).putString(ALIAS, fixture.alias).commit())
            }
            fun openPersistent(context: Context): TestBinding? {
                val index = context.getSharedPreferences(INDEX, Context.MODE_PRIVATE)
                val name = index.getString(NAME, null) ?: return null
                val alias = index.getString(ALIAS, null) ?: return null
                return TestBinding(context, name, alias)
            }
            fun clearPersistentReference(context: Context) {
                context.getSharedPreferences(INDEX, Context.MODE_PRIVATE).edit().clear().commit()
                context.deleteSharedPreferences(INDEX)
            }
        }
    }

    private class ClaimTransportFixture : ProvisioningConnection.Transport {
        lateinit var events: ProvisioningConnection.Transport.Events
        val started = CountDownLatch(1)
        private val packets = mutableListOf<ByteArray>()
        private var closed = false
        fun install(value: ProvisioningConnection.Transport.Events): ProvisioningConnection.Transport {
            events = value
            return this
        }
        override fun start() { started.countDown() }
        override fun send(bytes: ByteArray) { packets += bytes.copyOf() }
        override fun close() {
            if (!closed) { closed = true; events.closed("cancelled") }
        }
        fun tls() = events.tlsEstablished()
        fun lastType() = packets.last()[4].toInt()
        fun respond(remote: Int, result: Int = 0) {
            val request = packets.last()
            events.plaintext(ByteBuffer.allocate(40).putInt(0x53505631).putInt(0x80000000.toInt())
                .put(request, 8, 20).putInt(8).putInt(remote).putInt(result).array())
        }
        fun lateCommitted() = runCatching { respond(6) }
        fun timeoutClose() = events.closed("handshake_timeout")
        fun clear() { packets.forEach { it.fill(0) }; packets.clear() }
    }

    fun run(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(Intent(instrumentation.targetContext, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        val session = MainActivity::class.java.getDeclaredMethod("getDirectSession").apply {
            isAccessible = true
        }.invoke(activity) as DeviceControlSession
        val sessionState = DeviceControlSession::class.java.getDeclaredField("state").apply { isAccessible = true }
        fun set(name: String, value: Any?) {
            // The production Activity now reads from the single session owner.
            // These snapshots remain instrumentation-only and open no transport.
            if (name == "directSnapshot") {
                sessionState.set(session, session.current().copy(connection = DeviceControlSession.Connection.CONNECTED,
                    authenticated = true, snapshotFresh = true, snapshot = value as DeviceControlProtocol.Snapshot))
                return
            }
            if (name == "directFirmwareInfo") {
                sessionState.set(session, session.current().copy(firmwareInfo = value as DeviceControlProtocol.FirmwareInfo))
                return
            }
            MainActivity::class.java.getDeclaredField(name).apply { isAccessible = true }.set(activity, value)
        }
        fun get(name: String): Any? = MainActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }.get(activity)
        val render = MainActivity::class.java.getDeclaredMethod("render").apply { isAccessible = true }
        val confirmExpectedOta = MainActivity::class.java.getDeclaredMethod("confirmExpectedOta").apply {
            isAccessible = true
        }
        fun views(root: View): List<View> = listOf(root) + if (root is ViewGroup)
            (0 until root.childCount).flatMap { views(root.getChildAt(it)) } else emptyList()
        fun row(title: String) = views(activity.window.decorView).single {
            it.contentDescription?.toString()?.startsWith("$title，") == true
        }
        fun text(value: String) = views(activity.window.decorView).any { it is TextView && it.text.toString() == value }
        fun containsText(value: String) = views(activity.window.decorView).any {
            it is TextView && it.text.toString().contains(value)
        }
        fun button(value: String) = views(activity.window.decorView).single {
            it is TextView && it.text.toString() == value
        }
        val base = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0,
            memorySupported = true, memoryEnabled = false, wifiReady = true)
        val preferenceName = "device-ui-ota-${UUID.randomUUID()}"
        val testPreferences = instrumentation.targetContext.getSharedPreferences(
            preferenceName, Context.MODE_PRIVATE)
        lateinit var originalPreferences: SharedPreferences
        instrumentation.runOnMainSync {
            originalPreferences = get("preferences") as SharedPreferences
        }
        try {
            instrumentation.runOnMainSync {
                set("provisionedDeviceId", "ui-synthetic-device")
                set("legacyConsoleMode", false)
                set("currentTab", 3)
                set("directSnapshot", base)
                render.invoke(activity)
                check(row("跨重启记忆").isEnabled && row("删除已保存的记忆").isEnabled)
                check(text("已关闭 · 不读取或新增保存"))
                set("directSnapshot", base.copy(busy = true, memoryEnabled = null, memoryPending = true))
                render.invoke(activity)
                check(!row("跨重启记忆").isEnabled && !row("删除已保存的记忆").isEnabled)
                check(!row("清空近期对话").isEnabled)
                set("directSnapshot", base.copy(memoryEnabled = null, memoryFailed = true))
                render.invoke(activity)
                check(!row("跨重启记忆").isEnabled && !row("删除已保存的记忆").isEnabled)

                /* Enter through the actual overview-page row, not the
                 * settings-page duplicate. No connection or OTA command is
                 * injected. */
                set("currentTab", 0)
                set("directSnapshot", base)
                render.invoke(activity)
                row("固件更新").performClick()
                check(text("固件更新"))
                check(text("版本不可用"))
                check(!button("从手机开始升级").isEnabled)

                /* An OTA-capability-free STATUS is old firmware. */
                set("directSnapshot", base.copy(otaSupported = false))
                render.invoke(activity)
                check(!button("从手机开始升级").isEnabled)

                /* Use only a unique, test-owned expected record. Calling the
                 * real confirmation method must reject a mismatched INFO. */
                set("preferences", testPreferences)
                check(testPreferences.edit()
                    .putString("ota_expected_version", "1.2.3+4")
                    .putLong("ota_expected_counter", 4)
                    .putString("ota_expected_catalog", "synthetic-catalog")
                    .putString("ota_expected_device", "ui-synthetic-device")
                    .commit())
                set("otaStatus", DeviceControlProtocol.OtaStatus(3, 6, 100, 100, 0))
                set("otaStatusGeneration", session.current().generation)
                set("otaVerificationPending", true)
                set("directFirmwareInfo", DeviceControlProtocol.FirmwareInfo(99, 0, 0, 1, 999))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(containsText("已结束，等待版本核对"))
                check(text("设备未确认本次升级完成，请根据设备状态重试或恢复。"))
                check(!views(activity.window.decorView).filterIsInstance<TextView>().any {
                    it.text.toString().startsWith("设备已确认完成升级：")
                })

                /* The same terminal report only becomes success after INFO
                 * matches both version and security counter. */
                set("directFirmwareInfo", DeviceControlProtocol.FirmwareInfo(1, 2, 3, 4, 4))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(text("设备已确认完成升级：1.2.3+4。"))
                check(containsText("已确认完成升级"))
                check(!containsText("已结束，等待版本核对"))

                /* A terminal device error must remain visible and can never
                 * reuse the confirmation success wording. */
                set("otaStatus", DeviceControlProtocol.OtaStatus(3, 8, 100, 100, -5))
                set("otaMessage", "")
                confirmExpectedOta.invoke(activity)
                render.invoke(activity)
                check(containsText("升级失败"))
                check(containsText("设备错误 -5"))
                check(!views(activity.window.decorView).filterIsInstance<TextView>().any {
                    it.text.toString().startsWith("设备已确认完成升级：")
                })

                set("currentTab", 0)
                set("directSnapshot", base.copy(wifiReady = false))
                render.invoke(activity)
                check(text("设备 Wi-Fi 未就绪，暂时无法发起云端对话"))
                set("directSnapshot", base.copy(busy = true, memoryEnabled = null, memoryPending = true))
                render.invoke(activity)
                check(text("正在处理记忆设置"))
                check(!text("停止这次对话"))
                runFirmwareInspectionLifecycleProbe(activity)
                set("currentTab", 3)
                set("directSnapshot", base)
                render.invoke(activity)
            }
            runProvisionRecoveryCancelProbe(instrumentation)
            runProvisionBootstrapNormalModeProbe(instrumentation)
            instrumentation.waitForIdleSync()
            val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            File(instrumentation.targetContext.cacheDir, "device-ui-acceptance.png").outputStream().use {
                check(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
            screenshot.recycle()
        } finally {
            instrumentation.runOnMainSync {
                set("preferences", originalPreferences)
                testPreferences.edit().clear().commit()
                instrumentation.targetContext.deleteSharedPreferences(preferenceName)
                activity.finish()
            }
            instrumentation.waitForIdleSync()
        }
    }

    private fun runFirmwareInspectionLifecycleProbe(activity: MainActivity) {
        fun field(name: String) = MainActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val finish = MainActivity::class.java.getDeclaredMethod(
            "finishFirmwareInspection", Long::class.javaPrimitiveType, Pair::class.java,
            Boolean::class.javaPrimitiveType,
        ).apply { isAccessible = true }
        val select = MainActivity::class.java.getDeclaredMethod(
            "selectTab", Int::class.javaPrimitiveType,
        ).apply { isAccessible = true }
        val image = BkpackInspector.Image(1, "0".repeat(64))
        val metadata = BkpackInspector.Metadata("aidk_ai_toy", "1.2.3+4", 4, "0".repeat(64),
            "1".repeat(64), image, image)
        val stale = File(activity.cacheDir, "ota-stale-${UUID.randomUUID()}").apply { writeText("stale") }
        val late = File(activity.cacheDir, "ota-late-${UUID.randomUUID()}").apply { writeText("late") }
        try {
            field("currentTab").set(activity, 4)
            field("firmwareInspectionEpoch").set(activity, 20L)
            field("firmwareInspectionPending").set(activity, true)
            finish.invoke(activity, 19L, metadata to stale, false)
            check(!stale.exists())
            check(field("firmwareInspectionPending").get(activity) == true)

            select.invoke(activity, 5)
            select.invoke(activity, 4)
            finish.invoke(activity, 20L, metadata to late, false)
            check(late.exists())
            check(field("selectedFirmwareFile").get(activity) == late)
            check(field("inspectedFirmware").get(activity) == metadata)
            check(field("firmwareInspectionPending").get(activity) == false)

            field("firmwareInspectionEpoch").set(activity, 21L)
            field("firmwareInspectionPending").set(activity, true)
            finish.invoke(activity, 21L, null, true)
            check(field("firmwareInspectionPending").get(activity) == false)
            check(field("firmwareInspectionMessage").get(activity) != null)
        } finally {
            stale.delete(); late.delete()
        }
    }

    private fun runProvisionRecoveryCancelProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        val preferencesName = "device-ui-recovery-${UUID.randomUUID()}"
        val preferences = instrumentation.targetContext.getSharedPreferences(
            preferencesName, Context.MODE_PRIVATE)
        val store = ProvisionBindingStore(
            ProvisionBindingStore.SharedPreferencesBackend(preferences),
        )
        val deviceId = "ui-recovery-device"
        val receipt = store.begin(deviceId, ByteArray(16) { (it + 1).toByte() })
        fun field(name: String): java.lang.reflect.Field =
            ProvisionActivity::class.java.getDeclaredField(name).apply { isAccessible = true }
        fun get(name: String): Any? = field(name).get(activity)
        fun set(name: String, value: Any?) = field(name).set(activity, value)
        val showPage = ProvisionActivity::class.java.getDeclaredMethod(
            "showPage", Int::class.javaPrimitiveType!!,
        ).apply { isAccessible = true }
        val resultButton = get("resultButton") as android.widget.Button
        class CloseProbe : AutoCloseable {
            var count = 0
            var detachedBeforeClose = false
            var epochBeforeClose = Long.MIN_VALUE
            override fun close() {
                count++
                epochBeforeClose = get("epoch") as Long
                detachedBeforeClose = get("recoveryControl") == null
            }
        }
        try {
            instrumentation.runOnMainSync {
                set("bindingStore", store)
                showPage.invoke(activity, 2)
                val first = CloseProbe()
                val before = get("epoch") as Long
                set("recoveryControl", first)
                resultButton.performClick()
                check(get("page") == 0)
                check((get("status") as TextView).text.toString() ==
                    "已取消本次连接，认领结果仍需核对。")
                check(first.count == 1 && first.detachedBeforeClose &&
                    first.epochBeforeClose > before)
                check(store.pending(deviceId) == receipt)

                showPage.invoke(activity, 2)
                val second = CloseProbe()
                val beforeBack = get("epoch") as Long
                set("recoveryControl", second)
                activity.onBackPressed()
                check(get("page") == 0)
                check(second.count == 1 && second.detachedBeforeClose &&
                    second.epochBeforeClose > beforeBack)
                check(store.pending(deviceId) == receipt)
                activity.onBackPressed()
                check(second.count == 1)
            }
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
            preferences.edit().clear().commit()
            instrumentation.targetContext.deleteSharedPreferences(preferencesName)
        }
    }

    private fun runProvisionBootstrapNormalModeProbe(instrumentation: Instrumentation) {
        val activity = instrumentation.startActivitySync(
            Intent(instrumentation.targetContext, ProvisionActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
        ) as ProvisionActivity
        fun field(name: String) = ProvisionActivity::class.java.getDeclaredField(name).apply {
            isAccessible = true
        }
        val nextPage = ProvisionActivity::class.java.getDeclaredMethod("nextPage").apply {
            isAccessible = true
        }
        val bootstrap = ProvisionBootstrap.parse(
            """{"protocol":"provision-bootstrap-v1","device_id":"ui-normal-device","certificate_sha256":"${"ab".repeat(32)}","possession_secret":"${java.util.Base64.getEncoder().encodeToString(ByteArray(32) { 42 })}"}""".toCharArray(),
        )
        try {
            instrumentation.runOnMainSync {
                val adapter = checkNotNull(instrumentation.targetContext
                    .getSystemService(android.bluetooth.BluetoothManager::class.java)?.adapter)
                field("selected").set(activity, adapter.getRemoteDevice("02:00:00:00:00:01"))
                field("bootstrap").set(activity, bootstrap)
                field("ca").set(activity, null)
                field("developerMode").set(activity, false)
                nextPage.invoke(activity)
                check(field("page").get(activity) == 1)
                check((field("status").get(activity) as TextView).text.toString().contains("设置网络"))
            }
        } finally {
            bootstrap.close()
            instrumentation.runOnMainSync { activity.finish() }
        }
    }
}
