package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.SecureRandom

/** Foreground trial coordinator, using the existing authenticated Session.
 * The device owns TTL/restoration. Closing only releases owned staging; it
 * never pretends that the remote trial was canceled, nor replays a write.
 */
internal class ExpressionTrialController(
    private val session: DeviceControlSession,
    private val operationId: () -> Long = { SecureRandom().nextLong().and(Long.MAX_VALUE).coerceAtLeast(1) },
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Snapshot(val state: Int, val id: Long, val error: Int, val remainingMs: Long?, val operation: Long)
    data class State(val snapshot: Snapshot?, val busy: Boolean, val message: String)
    private enum class Phase { IDLE, FIRST, LAST, VERIFY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var snapshot: Snapshot? = null
    private var message = "尚未读取设备试用"
    private var first: ByteArray? = null
    private var last: ByteArray? = null
    private var request: ByteArray? = null
    private var expectedOperation: Long? = null
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            snapshot = null; phase = Phase.IDLE; ownsTransaction = false
            request = null; first = null; last = null; expectedOperation = null
            message = "连接变化；请重新读取，未完成的操作不会重发"
            publish()
        }
    }
    fun current() = State(snapshot, phase != Phase.IDLE, message)
    private fun publish() { if (active) changed(current()) }
    fun refresh(): Boolean {
        if (!active || phase != Phase.IDLE || !session.current().authenticated) return false
        snapshot = null; first = null; last = null
        phase = Phase.FIRST; message = "正在读取设备试用"; publish()
        return read(0)
    }
    fun act(action: Int, durationMs: Long = 0, expression: Int = 0): Boolean {
        val value = snapshot ?: return false
        if (!active || phase != Phase.IDLE || !session.current().authenticated ||
            generation != session.current().generation || action !in 1..2 ||
            (action == 1 && (durationMs !in 1L..0xffffffffL || expression !in 1..9)) ||
            (action == 2 && (durationMs != 0L || expression != 0))) return false
        val allowed = if (action == 1) value.state in listOf(0, 6, 7, 8, 9) else value.state in listOf(1, 3, 4)
        if (!allowed) return false
        val id = operationId()
        if (id <= 0) return false
        expectedOperation = id
        request = ByteBuffer.allocate(32).put("ETC1".toByteArray(Charsets.US_ASCII))
            .putInt(action).putInt(value.id.toInt()).putInt(durationMs.toInt())
            .putLong(id).putInt(expression).putInt(0).array()
        snapshot = null; phase = Phase.BEGIN; ownsTransaction = true
        message = "正在提交设备操作"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(11).putInt(32).array())
    }
    private fun read(offset: Int) = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((11 shl 16) or offset).array())
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
            fail(if (reply.error in listOf(-95, -138, -38)) "此固件尚不支持限时表情试用" else "设备返回 ${reply.error}，结果未确认；请重新读取")
            return
        }
        when (phase) {
            Phase.BEGIN -> { phase = Phase.APPEND; send(DeviceControlProtocol.Command.CONFIG_APPEND, request!!) }
            Phase.APPEND -> { phase = Phase.APPLY; send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf()) }
            Phase.APPLY -> {
                request = null; ownsTransaction = false; phase = Phase.IDLE
                session.finishConfigTransaction("设备已受理，正在读取试用状态")
                refresh()
            }
            else -> {
                val chunk = reply.configChunk
                if (chunk == null || chunk.totalLength != 32 || chunk.bytes.size != 16) { fail("设备试用格式无效"); return }
                when (phase) {
                    Phase.FIRST -> { first = chunk.bytes.copyOf(); phase = Phase.LAST; read(16) }
                    Phase.LAST -> { last = chunk.bytes.copyOf(); phase = Phase.VERIFY; read(0) }
                    Phase.VERIFY -> {
                        if (!first!!.contentEquals(chunk.bytes) || String(first!!, 0, 4, Charsets.US_ASCII) != "ETS1") {
                            fail("试用状态已变化，请重新读取"); return
                        }
                        val header = ByteBuffer.wrap(first!!); header.position(4)
                        val state = header.int; val id = header.int.toLong() and 0xffffffffL; val error = header.int
                        val tail = ByteBuffer.wrap(last!!); val remaining = tail.long; val operation = tail.long
                        if (state !in 0..9 || error > 0 || (state == 0) != (id == 0L) ||
                            (remaining != -1L && remaining !in 0L..0xffffffffL) ||
                            (state !in 1..3 && remaining != 0L)) { fail("设备试用数据无效"); return }
                        snapshot = Snapshot(state, id, error, remaining.takeUnless { it == -1L }, operation)
                        phase = Phase.IDLE
                        message = if (expectedOperation != null && expectedOperation != operation)
                            "已读取设备当前状态；未确认本次操作，请核对后继续"
                        else {
                            expectedOperation = null
                            when (state) {
                                0 -> "设备当前没有试用"
                                1 -> "设备已受理试用，尚未显示"
                                2 -> "设备正在准备试用，尚未确认显示"
                                3 -> "设备报告试用已显示；剩余时间以上次读取为准"
                                4 -> "取消已受理，等待设备恢复"
                                5 -> "设备正在恢复表情"
                                6 -> if (error == 0) "试用已到期，设备报告已恢复" else "试用排队时已到期，未显示"
                                7 -> "设备已确认取消试用"
                                8 -> "试用已被新的显示操作替代"
                                else -> "试用或恢复失败（$error），请读取设备当前状态"
                            }
                        }
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
        ownsTransaction = false; request = null; snapshot = null; expectedOperation = null; phase = Phase.IDLE
    }
}
