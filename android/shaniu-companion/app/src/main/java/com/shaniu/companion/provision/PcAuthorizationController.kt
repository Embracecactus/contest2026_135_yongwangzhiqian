// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.SecureRandom

/** Phone-owned PC authorization view. The device owns persistence and receipts.
 * Public state contains only transaction and principal metadata, never a PC key.
 */
internal class PcAuthorizationController(
    private val session: DeviceControlSession,
    private val transactionId: () -> ByteArray = { ByteArray(16).also { SecureRandom().nextBytes(it) } },
    resumeTransaction: String? = null,
    resumeTarget: Target? = null,
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Snapshot(val active: Boolean, val configRevision: ULong, val grantRevision: ULong,
                        val capabilities: Int, val client: String, val transaction: String)
    enum class Outcome { NONE, PENDING, UNKNOWN, FAILED, CONFIRMED }
    data class State(val snapshot: Snapshot?, val busy: Boolean, val outcome: Outcome,
                     val transaction: String?, val message: String, val target: Target?)
    private enum class Phase { IDLE, VIEW, VERIFY, BEGIN, APPEND, APPLY, RECEIPT }
    private var phase = Phase.IDLE
    private var snapshot: Snapshot? = null
    private var transaction = resumeTransaction?.takeIf { it.matches(Regex("[0-9a-f]{32}")) && it.any { c -> c != '0' } }
    private var outcome = if (transaction == null) Outcome.NONE else Outcome.UNKNOWN
    private var message = if (transaction == null) "尚未读取电脑授权" else "上次操作结果未确认，可查询回执"
    private val data = ByteArray(64)
    private var offset = 0
    private var request: ByteArray? = null
    private var verifyOutcome = false
    data class Target(val client: String, val capabilities: Int, val revision: ULong)
    private var target = resumeTarget?.takeIf {
        transaction != null && it.revision > 0u && it.capabilities in 0..31 &&
            it.client.matches(Regex("[0-9a-f]{32}")) &&
            (it.capabilities != 0) == it.client.any { c -> c != '0' }
    }
    private var ownsTransaction = false
    private var submitted = transaction != null
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            snapshot = null; phase = Phase.IDLE; ownsTransaction = false
            clearBuffers()
            if (!submitted) transaction = null
            outcome = if (transaction == null) Outcome.NONE else Outcome.UNKNOWN
            message = "连接变化；请重新读取或查询回执，操作不会重发"
            publish()
        }
    }
    fun current() = State(snapshot, phase != Phase.IDLE, outcome, transaction.takeIf { submitted }, message, target.takeIf { submitted })
    private fun publish() { if (active) changed(current()) }
    private fun available() = active && phase == Phase.IDLE && session.current().authenticated &&
        generation == session.current().generation
    fun refresh(): Boolean {
        if (!available()) return false
        verifyOutcome = false
        return readView()
    }
    private fun readView(): Boolean {
        snapshot = null; data.fill(0); offset = 0; phase = Phase.VIEW
        message = "正在读取设备授权状态"; publish()
        return read(0)
    }
    /** The caller presents confirmation for this exact verified snapshot. */
    fun revoke(expected: Snapshot): Boolean {
        if (!expected.active) return false
        return submit(expected, ByteArray(16), ByteArray(32), 0)
    }
    /** Borrowed inputs belong to a separately authenticated PC exchange. The
     * caller must obtain explicit confirmation for the principal/capabilities;
     * this method does not create, transfer or persist desktop credentials.
     */
    fun grant(expected: Snapshot, client: ByteArray, key: ByteArray, capabilities: Int, pairingTransaction: ByteArray? = null): Boolean {
        if (client.size != 16 || client.all { it == 0.toByte() } ||
            key.size != 32 || key.all { it == 0.toByte() } ||
            capabilities !in 1..31) return false
        return submit(expected, client, key, capabilities, pairingTransaction)
    }
    private fun submit(expected: Snapshot, client: ByteArray, key: ByteArray, capabilities: Int, pairingTransaction: ByteArray? = null): Boolean {
        if (!available() || snapshot != expected || expected.grantRevision == ULong.MAX_VALUE ||
            outcome in listOf(Outcome.PENDING, Outcome.UNKNOWN)) return false
        val id = pairingTransaction?.copyOf() ?: transactionId()
        if (id.size != 16 || id.all { it == 0.toByte() }) { id.fill(0); return false }
        val record = ByteBuffer.allocate(88).put("PCW1".toByteArray(Charsets.US_ASCII))
            .putLong(expected.configRevision.toLong()).putLong(expected.grantRevision.toLong()).put(id)
            .put(client).put(key).putInt(capabilities).array()
        target = Target(hex(client), capabilities, expected.grantRevision + 1u)
        transaction = hex(id); id.fill(0)
        request = record; offset = 0; submitted = false; verifyOutcome = false
        outcome = Outcome.UNKNOWN; snapshot = null; phase = Phase.BEGIN; ownsTransaction = true
        message = "正在提交电脑授权变更；尚未确认生效"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(14).putInt(88).array())
    }
    fun query(): Boolean {
        if (!available() || transaction == null) return false
        snapshot = null; data.fill(0); offset = 0; phase = Phase.RECEIPT
        message = "正在查询上次操作回执"; publish()
        return read(0, receipt = true)
    }
    private fun read(position: Int, receipt: Boolean = false): Boolean {
        val bytes = ByteBuffer.allocate(if (receipt) 20 else 4).putInt((14 shl 16) or position)
        if (receipt) bytes.put(requireNotNull(transaction).chunked(2).map { it.toInt(16).toByte() }.toByteArray())
        return send(DeviceControlProtocol.Command.CONFIG_READ, bytes.array())
    }
    private fun send(command: DeviceControlProtocol.Command, bytes: ByteArray): Boolean {
        val accepted = try { session.requestPayload(command, bytes) } finally { bytes.fill(0) }
        if (!accepted) {
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) {
                ownsTransaction = false; transaction = null; outcome = Outcome.NONE
            }
            fail("请求未发出；请重新读取或查询回执")
        }
        return accepted
    }
    private fun clearBuffers() { request?.fill(0); request = null; data.fill(0) }
    private fun fail(reason: String) {
        phase = Phase.IDLE; snapshot = null; clearBuffers(); message = reason
        if (!submitted) { transaction = null; outcome = Outcome.NONE }
        if (transaction != null) outcome = Outcome.UNKNOWN
        if (ownsTransaction) {
            ownsTransaction = false
            if (submitted) session.finishConfigTransaction("操作结果未确认") else session.cancelConfigTransaction()
        }
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
        if (phase == Phase.APPLY) {
            if (reply.error == -61) { submitted = false; fail("设备未收到完整记录；本次提交未完成"); return }
            ownsTransaction = false; clearBuffers(); phase = Phase.IDLE
            session.finishConfigTransaction("设备已回复；结果仍需回执确认")
            query()
            return
        }
        if (reply.error != 0) {
            fail(if (reply.error == -95) "此固件尚不支持电脑授权管理" else "设备返回 ${reply.error}；未确认结果，请稍后读取")
            return
        }
        when (phase) {
            Phase.BEGIN, Phase.APPEND -> {
                val record = requireNotNull(request)
                if (offset < record.size) {
                    val end = minOf(offset + 32, record.size)
                    val part = record.copyOfRange(offset, end); offset = end; phase = Phase.APPEND
                    send(DeviceControlProtocol.Command.CONFIG_APPEND, part)
                } else {
                    record.fill(0); request = null; submitted = true; phase = Phase.APPLY
                    publish()
                    send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf())
                }
            }
            else -> receiveChunk(reply.configChunk)
        }
    }
    private fun receiveChunk(chunk: DeviceControlProtocol.ConfigChunk?) {
        val total = if (phase == Phase.RECEIPT) 32 else 64
        if (chunk == null || chunk.totalLength != total || chunk.bytes.size != 16) { fail("设备授权回包格式无效"); return }
        if (phase == Phase.VERIFY) {
            if (!chunk.bytes.contentEquals(data.copyOfRange(offset, offset + 16))) { fail("授权版本已变化，请重新读取"); return }
            if (offset == 0) { offset = 16; read(offset); return }
            parseView(); return
        }
        chunk.bytes.copyInto(data, offset); offset += 16
        if (offset < total) { read(offset, phase == Phase.RECEIPT); return }
        if (phase == Phase.RECEIPT) parseReceipt()
        else { offset = 0; phase = Phase.VERIFY; read(0) }
    }
    private fun parseView() {
        val b = ByteBuffer.wrap(data); val magic = b.int; val enabled = b.int
        val config = b.long.toULong(); val revision = b.long.toULong(); val caps = b.int; val reserved = b.int
        val client = data.copyOfRange(32, 48); val tx = data.copyOfRange(48, 64)
        if (magic != 0x50435331 || enabled !in 0..1 || caps and 31 != caps || reserved != 0 ||
            (enabled == 1) != (caps != 0) || (enabled == 1) != client.any { it != 0.toByte() }) {
            fail("设备授权状态无效"); return
        }
        snapshot = Snapshot(enabled == 1, config, revision, caps, hex(client), hex(tx))
        phase = Phase.IDLE; message = if (enabled == 1) "已回读：存在电脑授权" else "已回读：没有有效电脑授权"
        if (verifyOutcome) {
            val wanted = target
            outcome = if (wanted != null && hex(tx) == transaction &&
                revision == wanted.revision && caps == wanted.capabilities && hex(client) == wanted.client)
                Outcome.CONFIRMED else Outcome.UNKNOWN
            message = if (outcome == Outcome.CONFIRMED) "授权变更已持久保存，并已回读确认" else "回执与预期授权状态无法核对，结果未确认"
            verifyOutcome = false
        }
        clearBuffers(); publish()
    }
    private fun parseReceipt() {
        val b = ByteBuffer.wrap(data); val magic = b.int; val result = b.int; val receiptPhase = b.int; val reserved = b.int
        val valid = when (receiptPhase) { 0, 3 -> result < 0; 1 -> result == -11; 2 -> result == 0; 4 -> result == -115; else -> false }
        if (magic != 0x50435231 || reserved != 0 || !valid || hex(data.copyOfRange(16, 32)) != transaction) {
            fail("事务回执无效或不属于本次操作"); return
        }
        phase = Phase.IDLE; clearBuffers()
        when (receiptPhase) {
            2 -> { verifyOutcome = true; readView(); return }
            1 -> { outcome = Outcome.PENDING; message = "设备仍在处理，请稍后查询回执" }
            3 -> { outcome = Outcome.FAILED; message = "设备确认操作失败（$result），请重新读取当前授权" }
            else -> { outcome = Outcome.UNKNOWN; message = "操作结果未知（$result）；请查询，不会自动重发" }
        }
        publish()
    }
    override fun close() {
        // No APPLY was sent: cleanup of staging is not a remote job cancel.
        // A resumed receipt remains queryable even if this view never wrote.
        if (!submitted) { transaction = null; outcome = Outcome.NONE; publish() }
        active = false; results.cancel(); connection.cancel()
        if (ownsTransaction) {
            if (submitted) session.finishConfigTransaction("操作结果未确认，可重新查询") else session.cancelConfigTransaction()
        }
        ownsTransaction = false; phase = Phase.IDLE; snapshot = null; clearBuffers()
    }
    private fun hex(bytes: ByteArray) = bytes.joinToString("") { "%02x".format(it.toInt() and 255) }
}
