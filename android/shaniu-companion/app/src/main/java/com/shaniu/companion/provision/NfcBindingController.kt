package com.shaniu.companion.provision

import java.nio.ByteBuffer

/** 复用前台会话；设备拥有作业和绑定，页面关闭不冒充设备取消。 */
internal class NfcBindingController(
    private val session: DeviceControlSession,
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Snapshot(val phase: Int, val error: Int, val operation: Long,
        val revision: Long, val floor: Long, val durations: List<Long>)
    data class Scene(val capabilities: Int, val flags: Int, val error: Int)
    data class State(val snapshot: Snapshot?, val busy: Boolean, val message: String,
        val scene: Scene?, val sceneMessage: String)
    private enum class Phase { IDLE, READ, VERIFY, CAPABILITY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var snapshot: Snapshot? = null
    private var scene: Scene? = null
    private var sceneMessage = "自动专注能力未确认，请读取设备状态"
    private var message = "尚未读取设备卡片设置"
    private var bytes = ByteArray(112)
    private var offset = 0
    private var request: ByteArray? = null
    private var sentBytes = 0
    private var awaitingOperation: Long? = null
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            scene = null; sceneMessage = "连接变化，自动专注能力未确认"
            snapshot = null; phase = Phase.IDLE; ownsTransaction = false
            request = null; awaitingOperation = null; bytes.fill(0)
            message = "连接变化；请重新读取。已受理作业由设备继续，不自动重发"
            publish()
        }
    }
    fun current() = State(snapshot, phase != Phase.IDLE, message, scene, sceneMessage)
    private fun publish() { if (active) changed(current()) }
    fun refresh(): Boolean {
        if (!active || phase != Phase.IDLE || !session.current().authenticated) return false
        snapshot = null; scene = null; sceneMessage = "正在读取自动专注能力"; bytes.fill(0); offset = 0
        phase = Phase.READ; message = "正在读取设备卡片状态"; publish()
        return read()
    }
    fun act(action: Int, slot: Int = 0, durationMs: Long = 0): Boolean {
        val value = snapshot ?: return false
        if (!active || phase != Phase.IDLE || !session.current().authenticated ||
            generation != session.current().generation || action !in 1..4 || slot !in 0..7 ||
            (action == 2 && durationMs <= 0) || (action != 2 && durationMs != 0L) ||
            (action in listOf(1,4) && slot != 0)) return false
        if (action == 4) {
            if (value.phase !in 1..2 || value.operation == 0L) return false
        } else if (value.phase in 1..3 || value.phase == 7 ||
            (action != 1 && value.phase == 0) || value.floor == Long.MAX_VALUE) return false
        val id = if (action == 4) value.operation else value.floor + 1
        awaitingOperation = id
        request = ByteBuffer.allocate(40).put("NCF1".toByteArray(Charsets.US_ASCII))
            .putInt(action).putInt(slot).putInt(0)
            .putLong(if (action in listOf(1,4)) 0 else value.revision)
            .putLong(id).putLong(durationMs).array()
        scene = null; sceneMessage = "设备正在处理，自动专注当前状态未确认"
        snapshot = null; phase = Phase.BEGIN; ownsTransaction = true
        message = "正在提交；是否完成以设备回读为准"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(12).putInt(40).array())
    }
    private fun read() = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((12 shl 16) or offset).array())
    private fun send(command: DeviceControlProtocol.Command, payload: ByteArray): Boolean {
        val accepted = session.requestPayload(command, payload)
        if (!accepted && phase == Phase.CAPABILITY) {
            finishCapability(null, "自动专注能力未确认，请稍后重新读取")
            return false
        }
        if (!accepted) {
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) ownsTransaction = false
            fail("设备正忙，结果未确认；请重新读取")
        }
        return accepted
    }
    private fun fail(reason: String) {
        scene = null; sceneMessage = "自动专注能力未确认，请重新读取"
        phase = Phase.IDLE; snapshot = null; request = null; awaitingOperation = null; bytes.fill(0)
        message = reason
        if (ownsTransaction) { ownsTransaction = false; session.cancelConfigTransaction() }
        publish()
    }
    private fun received(command: DeviceControlProtocol.Command, reply: DeviceControlProtocol.Snapshot) {
        if (!active || generation != session.current().generation || phase == Phase.IDLE) return
        val expected = when (phase) {
            Phase.BEGIN -> DeviceControlProtocol.Command.CONFIG_BEGIN
            Phase.APPEND -> DeviceControlProtocol.Command.CONFIG_APPEND
            Phase.APPLY -> DeviceControlProtocol.Command.CONFIG_APPLY
            else -> DeviceControlProtocol.Command.CONFIG_READ
        }
        if (command != expected) return
        if (phase == Phase.CAPABILITY) { receiveCapability(reply); return }
        if (reply.error != 0) {
            fail(if (reply.error == -95) "此固件不支持卡片绑定" else "设备返回 ${reply.error}；请重新读取，不会自动重发")
            return
        }
        when (phase) {
            Phase.BEGIN -> { phase = Phase.APPEND; sentBytes = 0; append() }
            Phase.APPEND -> {
                if (sentBytes < request!!.size) append()
                else { phase = Phase.APPLY; send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf()) }
            }
            Phase.APPLY -> {
                request = null; ownsTransaction = false; phase = Phase.IDLE
                session.finishConfigTransaction("设备已受理，正在回读作业状态")
                refresh()
            }
            Phase.READ, Phase.VERIFY -> {
                val chunk = reply.configChunk
                if (chunk == null || chunk.totalLength != 112 || chunk.bytes.size != 16) {
                    fail("设备卡片状态格式无效"); return
                }
                if (phase == Phase.READ) {
                    chunk.bytes.copyInto(bytes, offset)
                    offset += 16
                    if (offset == 112) { offset = 0; phase = Phase.VERIFY }
                    read()
                } else {
                    if (!bytes.copyOfRange(offset, offset + 16).contentEquals(chunk.bytes)) {
                        fail("卡片状态已变化，请重新读取"); return
                    }
                    offset += 16
                    if (offset < 48) read() else finishRead()
                }
            }
            else -> Unit
        }
    }
    private fun append() {
        val record = request ?: return
        val end = minOf(sentBytes + 32, record.size)
        val chunk = record.copyOfRange(sentBytes, end)
        sentBytes = end
        send(DeviceControlProtocol.Command.CONFIG_APPEND, chunk)
    }
    private fun finishRead() {
        val buffer = ByteBuffer.wrap(bytes)
        if (String(bytes, 0, 4, Charsets.US_ASCII) != "NCS1") { fail("设备卡片状态格式无效"); return }
        buffer.position(4)
        val state = buffer.int; val error = buffer.int; val reserved = buffer.int
        val operation = buffer.long; val revision = buffer.long; val floor = buffer.long
        val reservedTail = buffer.long
        val durations = List(8) { buffer.long }
        if (state !in 0..7 || error > 0 || reserved != 0 || reservedTail != 0L ||
            operation < 0 || revision < 0 || floor < 0 || floor < operation ||
            durations.any { it < 0 } || (state in 0..4 && error != 0)) {
            fail("设备数据或计数超出支持范围；不能提交操作"); return
        }
        if (awaitingOperation != null && awaitingOperation != operation) {
            fail("设备当前作业已被替换，本次结果未确认；请重新读取"); return
        }
        awaitingOperation = null
        snapshot = Snapshot(state,error,operation,revision,floor,durations)
        phase = Phase.IDLE; bytes.fill(0)
        message = when (state) {
            0 -> "状态已读取；请加载已保存的卡片设置"
            1 -> "设备已受理，等待执行；可再次读取进度"
            2 -> "设备正在处理；可请求取消并再次读取"
            3 -> "设备正在提交，暂时不能取消"
            4 -> "设备确认作业完成；以下为已回读设置"
            5 -> "设备作业失败（$error）；设置以上次回读为准"
            6 -> "设备确认作业已取消"
            else -> "保存结果未知，不能继续写入；请重新读取或恢复设备后再试"
        }
        phase = Phase.CAPABILITY
        publish()
        send(DeviceControlProtocol.Command.CONFIG_READ,
            ByteBuffer.allocate(4).putInt(13 shl 16).array())
    }
    private fun finishCapability(value: Scene?, summary: String) {
        scene = value; sceneMessage = summary; phase = Phase.IDLE
        publish()
    }
    private fun receiveCapability(reply: DeviceControlProtocol.Snapshot) {
        val chunk = reply.configChunk
        if (reply.error != 0 || chunk?.totalLength != 16 || chunk.bytes.size != 16) {
            finishCapability(null, "自动专注能力未确认；此固件可能未提供该信息"); return
        }
        val data = ByteBuffer.wrap(chunk.bytes)
        if (data.int != 0x4e434131) { finishCapability(null, "自动专注能力格式未知"); return }
        val caps = data.int; val flags = data.int; val error = data.int
        if (caps !in 0..1 || flags and 31 != flags || error > 0 ||
            (caps == 0 && (flags != 0 || error != 0)) ||
            (flags and 2 != 0 && flags and 1 == 0) ||
            (flags and 16 != 0 && error == 0)) {
            finishCapability(null, "自动专注能力格式未知"); return
        }
        val summary = when {
            caps == 0 -> "此固件未提供刷卡自动专注"
            flags and 16 != 0 -> "读卡服务故障（$error），需恢复设备后再试"
            error !in listOf(0, -2, -16, -114) -> "读卡状态异常（$error），请重新读取"
            flags and 1 == 0 -> "支持刷卡自动专注；读卡服务尚未就绪"
            flags and 2 == 0 -> "支持刷卡自动专注；当前暂停，请在设备空闲后移开再刷"
            flags and 4 == 0 -> "支持刷卡自动专注；卡片设置尚未就绪"
            flags and 8 != 0 -> "支持刷卡自动专注；设备正在读卡或处理设置"
            error == -2 -> "支持刷卡自动专注；当前卡片尚未登记"
            error == -114 -> "支持刷卡自动专注；请先移开卡片，再放回"
            error == -16 -> "支持刷卡自动专注；设备暂忙，请稍后移开再刷"
            else -> "支持刷卡自动专注；登记后移开，再刷卡启动"
        }
        finishCapability(Scene(caps,flags,error), summary)
    }
    override fun close() {
        active = false; results.cancel(); connection.cancel()
        scene = null; sceneMessage = "自动专注能力未确认"
        if (ownsTransaction) session.cancelConfigTransaction()
        ownsTransaction = false; request = null; awaitingOperation = null; snapshot = null; bytes.fill(0); phase = Phase.IDLE
    }
}
