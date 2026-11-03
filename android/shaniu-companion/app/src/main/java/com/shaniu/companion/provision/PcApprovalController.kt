// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.security.MessageDigest

/**
 * Owner-side Agent Approval Totem: reads the exact APR1 request the PC sent
 * to the device (kind 30, APV1) and submits an APD1 decision bound to its
 * request ID and the SHA-256 of the exact bytes shown here.
 */
internal class PcApprovalController(
    private val session: DeviceControlSession,
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Request(
        val state: Int,
        val requestId: ByteArray,
        val digest: ByteArray,
        val remainingMs: Long,
        val ttlMs: Long,
        val agent: String,
        val target: String,
        val operation: String,
    )
    data class State(val request: Request?, val noRequest: Boolean, val busy: Boolean, val message: String)

    private enum class Phase { IDLE, HEADER, BODY, VERIFY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var request: Request? = null
    private var noRequest = false
    private var message = "尚未读取电脑审批请求"
    private var header = ByteArray(0)
    private var body = ByteArray(0)
    private var offset = 0
    private var decision = ByteArray(0)
    private var sent = 0
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            ownsTransaction = false
            fail("连接变化；请重新读取，未完成的决定不会重发")
        }
    }

    fun current() = State(request, noRequest, phase != Phase.IDLE, message)
    private fun publish() { if (active) changed(current()) }

    fun refresh(): Boolean {
        if (!active || phase != Phase.IDLE || !session.current().authenticated) return false
        clearBuffers(); offset = 0
        phase = Phase.HEADER
        return read(0)
    }

    /** approve=true sends decision 1, false sends 2; never without a fresh read. */
    fun decide(approve: Boolean): Boolean {
        val value = request ?: return false
        if (!active || phase != Phase.IDLE || value.state != PENDING ||
            !session.current().authenticated || session.current().generation != generation) return false
        decision = ByteBuffer.allocate(DECISION_SIZE).put("APD1".toByteArray(Charsets.US_ASCII))
            .putInt(if (approve) 1 else 2).put(value.requestId).put(value.digest).array()
        sent = 0; phase = Phase.BEGIN; ownsTransaction = true
        message = if (approve) "正在提交批准，尚未确认" else "正在提交拒绝，尚未确认"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(KIND).putInt(DECISION_SIZE).array())
    }

    private fun read(at: Int) = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((KIND shl 16) or at).array())

    private fun send(command: DeviceControlProtocol.Command, bytes: ByteArray): Boolean {
        val accepted = session.requestPayload(command, bytes)
        if (!accepted) {
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) ownsTransaction = false
            fail("设备忙，结果未确认；请重新读取")
        }
        return accepted
    }

    private fun clearBuffers() {
        header.fill(0); body.fill(0); decision.fill(0)
        header = byteArrayOf(); body = byteArrayOf(); decision = byteArrayOf()
    }

    private fun fail(reason: String) {
        phase = Phase.IDLE; request = null; noRequest = false; clearBuffers()
        message = reason
        if (ownsTransaction) { ownsTransaction = false; session.cancelConfigTransaction() }
        publish()
    }

    private fun append() {
        val count = minOf(32, decision.size - sent)
        val chunk = decision.copyOfRange(sent, sent + count)
        sent += count
        send(DeviceControlProtocol.Command.CONFIG_APPEND, chunk)
    }

    private fun received(command: DeviceControlProtocol.Command, reply: DeviceControlProtocol.Snapshot) {
        if (!active || phase == Phase.IDLE || generation != session.current().generation) return
        val expected = when (phase) {
            Phase.BEGIN -> DeviceControlProtocol.Command.CONFIG_BEGIN
            Phase.APPEND -> DeviceControlProtocol.Command.CONFIG_APPEND
            Phase.APPLY -> DeviceControlProtocol.Command.CONFIG_APPLY
            else -> DeviceControlProtocol.Command.CONFIG_READ
        }
        if (command != expected) return
        if (reply.error != 0) {
            fail(when (reply.error) {
                -95, -138 -> "当前固件不支持电脑审批"
                -110 -> "请求已超时，电脑端会按拒绝处理"
                -114 -> "该请求已有决定，不能更改"
                -116 -> "电脑已换了新的请求，请重新核对后再决定"
                -74 -> "请求内容与读取时不一致，已拒绝提交"
                -2 -> "当前没有待审批的请求"
                -13 -> "设备拒绝操作，请检查电脑授权"
                else -> "设备返回 ${reply.error}，结果未确认"
            }); return
        }
        when (phase) {
            Phase.BEGIN -> { phase = Phase.APPEND; append() }
            Phase.APPEND -> if (sent < decision.size) append() else {
                phase = Phase.APPLY
                send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf())
            }
            Phase.APPLY -> {
                ownsTransaction = false; phase = Phase.IDLE; clearBuffers()
                session.finishConfigTransaction("决定已提交，正在回读确认")
                refresh()
            }
            else -> receiveChunk(reply)
        }
    }

    private fun receiveChunk(reply: DeviceControlProtocol.Snapshot) {
        val chunk = reply.configChunk ?: run { fail("设备响应缺少数据"); return }
        if (chunk.bytes.size != 16) { fail("设备数据长度无效"); return }
        if (phase == Phase.HEADER) {
            header = chunk.bytes.copyOf()
            val input = ByteBuffer.wrap(header)
            val magic = input.int; val state = input.int; val size = input.int
            if (magic != 0x41505631 || state !in 0..5 || size !in 0..RECORD_MAX ||
                chunk.totalLength != 16 + size || (size == 0) != (state == 0)) {
                fail("设备审批数据格式不受支持"); return
            }
            if (size == 0) {
                request = null; noRequest = true; phase = Phase.IDLE
                message = "当前没有电脑审批请求"; clearBuffers(); publish(); return
            }
            body = ByteArray(size); offset = 0; phase = Phase.BODY; read(16); return
        }
        if (chunk.totalLength != 16 + body.size) { fail("请求已变化，请重新读取"); return }
        if (phase == Phase.BODY) {
            val count = minOf(16, body.size - offset)
            chunk.bytes.copyInto(body, offset, 0, count); offset += count
            if (offset < body.size) read(16 + offset) else { phase = Phase.VERIFY; read(0) }
            return
        }
        // Remaining time may tick; magic, state and size must be unchanged.
        if (!header.copyOfRange(0, 12).contentEquals(chunk.bytes.copyOfRange(0, 12))) {
            fail("请求已变化，请重新读取"); return
        }
        try {
            val head = ByteBuffer.wrap(chunk.bytes)
            head.position(4)
            val state = head.int; head.int
            val remaining = head.int.toUInt().toLong()
            request = parse(body, state, remaining)
            noRequest = false; phase = Phase.IDLE; clearBuffers()
            message = when (state) {
                PENDING -> "请核对下方完整内容后再决定"
                2 -> "已批准"; 3 -> "已拒绝"; 4 -> "已超时，电脑端按拒绝处理"
                else -> "电脑已取消该请求"
            }
            publish()
        } catch (_: IllegalArgumentException) { fail("请求内容无效，已拒绝显示") }
          catch (_: CharacterCodingException) { fail("请求包含无效文字编码，已拒绝显示") }
    }

    override fun close() {
        active = false; results.cancel(); connection.cancel()
        if (ownsTransaction) session.cancelConfigTransaction()
        ownsTransaction = false; request = null; clearBuffers(); phase = Phase.IDLE
    }

    companion object {
        const val KIND = DeviceControlProtocol.PC_APPROVAL_DECISION_KIND
        const val PENDING = 1
        const val DECISION_SIZE = 56
        const val RECORD_MAX = 32 + 32 + 256 + 768

        /** Strict APR1 parser; the digest covers exactly these bytes. */
        fun parse(record: ByteArray, state: Int, remainingMs: Long): Request {
            require(record.size in 34..RECORD_MAX)
            val input = ByteBuffer.wrap(record)
            val magic = ByteArray(4).also { input.get(it) }
            require(String(magic, Charsets.US_ASCII) == "APR1")
            val id = ByteArray(16).also { input.get(it) }
            require(id.any { it != 0.toByte() })
            val ttl = input.int.toUInt().toLong()
            val agent = input.short.toUShort().toInt()
            val target = input.short.toUShort().toInt()
            val operation = input.short.toUShort().toInt()
            require(input.short.toInt() == 0)
            require(ttl in 5000..300000 && agent in 1..32 && target in 0..256 && operation in 1..768)
            require(32 + agent + target + operation == record.size)
            fun text(start: Int, size: Int): String {
                val decoder = Charsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                val value = decoder.decode(ByteBuffer.wrap(record, start, size)).toString()
                require(value.none { c ->
                    val code = c.code
                    (code < 0x20 && c != '\t' && c != '\n') || code in 0x7f..0x9f ||
                        code == 0x061c || code == 0x200e || code == 0x200f ||
                        code in 0x202a..0x202e || code in 0x2066..0x2069
                })
                return value
            }
            return Request(
                state, id, MessageDigest.getInstance("SHA-256").digest(record), remainingMs, ttl,
                text(32, agent), text(32 + agent, target), text(32 + agent + target, operation),
            )
        }
    }
}
