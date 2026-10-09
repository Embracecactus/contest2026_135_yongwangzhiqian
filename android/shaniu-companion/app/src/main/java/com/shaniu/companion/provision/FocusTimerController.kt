package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.SecureRandom

/** Foreground view coordinator; the device owns time and all state transitions. */
internal class FocusTimerController(
    private val session: DeviceControlSession,
    private val operationId: () -> Long = { SecureRandom().nextLong().and(Long.MAX_VALUE).coerceAtLeast(1) },
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Snapshot(val state: Int, val revision: Long, val remainingMs: Long, val durationMs: Long)
    data class State(val snapshot: Snapshot?, val busy: Boolean, val message: String)
    private enum class Phase { IDLE, FIRST, LAST, VERIFY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var snapshot: Snapshot? = null
    private var message = "尚未读取设备计时"
    private var first: ByteArray? = null
    private var last: ByteArray? = null
    private var request: ByteArray? = null
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            snapshot = null; phase = Phase.IDLE; ownsTransaction = false
            request = null; first = null; last = null
            message = "连接变化；请重新读取，未完成的操作不会重发"
            publish()
        }
    }
    fun current() = State(snapshot, phase != Phase.IDLE, message)
    private fun publish() { if (active) changed(current()) }
    fun refresh(): Boolean {
        if (!active || phase != Phase.IDLE || !session.current().authenticated) return false
        snapshot = null; first = null; last = null
        phase = Phase.FIRST; message = "正在读取设备计时"; publish()
        return read(0)
    }
    fun act(action: Int, durationMs: Long = 0): Boolean {
        val value = snapshot ?: return false
        if (!active || phase != Phase.IDLE || !session.current().authenticated ||
            generation != session.current().generation || action !in 1..4 ||
            (action == 1 && durationMs <= 0) || (action != 1 && durationMs != 0L)) return false
        val allowed = when (action) { 1 -> value.state in listOf(0, 3, 4); 2 -> value.state == 1; 3 -> value.state == 2; else -> value.state in 1..2 }
        if (!allowed) return false
        val id = operationId()
        if (id <= 0) return false
        request = ByteBuffer.allocate(32).put("FOC1".toByteArray(Charsets.US_ASCII))
            .putInt(action).putLong(value.revision).putLong(id).putLong(durationMs).array()
        snapshot = null; phase = Phase.BEGIN; ownsTransaction = true
        message = "正在提交设备操作"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(10).putInt(32).array())
    }
    private fun read(offset: Int) = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((10 shl 16) or offset).array())
    private fun send(command: DeviceControlProtocol.Command, bytes: ByteArray): Boolean {
        val accepted = session.requestPayload(command, bytes)
        if (!accepted) {
            // A rejected BEGIN never acquired this session's staging slot.
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) ownsTransaction = false
            fail("设备正忙，结果未确认；请重新读取")
        }
        return accepted
    }
    private fun fail(reason: String) {
        phase = Phase.IDLE; snapshot = null; request = null; first = null; last = null
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
        if (reply.error != 0) {
            fail(if (reply.error == -95) "此固件尚不支持专注计时" else "设备返回 ${reply.error}，结果未确认；请重新读取")
            return
        }
        when (phase) {
            Phase.BEGIN -> { phase = Phase.APPEND; send(DeviceControlProtocol.Command.CONFIG_APPEND, request!!) }
            Phase.APPEND -> { phase = Phase.APPLY; send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf()) }
            Phase.APPLY -> {
                request = null; ownsTransaction = false; phase = Phase.IDLE
                session.finishConfigTransaction("设备已受理，正在读取计时状态")
                refresh()
            }
            else -> {
                val chunk = reply.configChunk
                if (chunk == null || chunk.totalLength != 32 || chunk.bytes.size != 16) { fail("设备计时格式无效"); return }
                when (phase) {
                    Phase.FIRST -> { first = chunk.bytes.copyOf(); phase = Phase.LAST; read(16) }
                    Phase.LAST -> { last = chunk.bytes.copyOf(); phase = Phase.VERIFY; read(0) }
                    Phase.VERIFY -> {
                        if (!first!!.contentEquals(chunk.bytes) || String(first!!, 0, 4, Charsets.US_ASCII) != "FOS1") {
                            fail("计时状态已变化，请重新读取"); return
                        }
                        val header = ByteBuffer.wrap(first!!); header.position(4)
                        val state = header.int; val revision = header.long
                        val tail = ByteBuffer.wrap(last!!); val remaining = tail.long; val duration = tail.long
                        if (state !in 0..4 || revision < 0 || remaining < 0 || duration < 0 || remaining > duration ||
                            (state !in 1..2 && remaining != 0L)) { fail("设备计时数据无效"); return }
                        snapshot = Snapshot(state, revision, remaining, duration)
                        phase = Phase.IDLE; message = "设备状态已回读；剩余时间以上次读取为准"
                        first = null; last = null; publish()
                    }
                    else -> Unit
                }
            }
        }
    }
    override fun close() {
        active = false; results.cancel(); connection.cancel()
        if (ownsTransaction) session.cancelConfigTransaction()
        ownsTransaction = false; request = null; snapshot = null; phase = Phase.IDLE
    }
}
