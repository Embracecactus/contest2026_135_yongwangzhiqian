// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import android.content.Context
import java.nio.ByteBuffer
import java.security.MessageDigest
import java.security.SecureRandom

/**
 * Foreground-only SRV1/SRR1 coordinator. It owns no BLE transport: all bytes
 * use the supplied authenticated [DeviceControlSession]. The persisted record
 * is a public transaction locator plus expected revision, never a control key
 * or provisioning proof. A missing APPLY response is deliberately uncertain
 * and is never silently retransmitted.
 */
internal class FactoryResetController(
    context: Context,
    private val session: DeviceControlSession,
    deviceId: String,
    private val changed: (State) -> Unit = {},
) : AutoCloseable {
    enum class Phase { IDLE, PREPARING, BEGIN, APPEND, APPLY, AWAITING_RECEIPT, QUERY_FIRST, QUERY_LAST, COMPLETED, FAILED }
    data class State(val phase: Phase, val transaction: ByteArray? = null,
                     val expectedRevision: Long? = null, val message: String)

    private data class Pending(val revision: Long, val transaction: ByteArray)
    private val preferences = context.applicationContext.getSharedPreferences("shaniu-reset-receipts", Context.MODE_PRIVATE)
    private val key = "reset-" + MessageDigest.getInstance("SHA-256").digest(deviceId.toByteArray())
        .joinToString("") { "%02x".format(it.toInt() and 0xff) }
    private var pending = load()
    private var phase = Phase.IDLE
    private var first: ByteArray? = null
    private var active = true
    private val resultSubscription = session.observeResults(::received)

    fun current(): State = State(phase, pending?.transaction?.copyOf(), pending?.revision, message())

    /** Call only after the user explicitly confirms the destructive impact. */
    fun begin(): Boolean {
        if (!active || pending != null || !session.current().authenticated) return false
        phase = Phase.PREPARING; publish()
        return session.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,
            ByteBuffer.allocate(4).putInt(7 shl 16).array()).also {
                if (!it) fail("无法读取设备当前配置版本")
            }
    }

    fun hasPending(): Boolean = pending != null

    /** Explicit user action only; this reuses the persisted exact SRT1. */
    fun retryDurableApply(): Boolean {
        val value = pending ?: return false
        if (!active || phase != Phase.AWAITING_RECEIPT || !session.current().authenticated) return false
        phase = Phase.BEGIN; publish()
        return sendBegin(value)
    }

    /** Safe after an APPLY timeout/disconnect. It does not write or retry. */
    fun queryReceipt(): Boolean {
        val value = pending ?: return false
        if (!active || !session.current().authenticated) return false
        first = null; phase = Phase.QUERY_FIRST; publish()
        return sendRead(value, 0)
    }

    /** Invoke only for the existing physical QR/TLS recovery receipt matching
     * this saved transaction. This never calls ProvisionBindingStore.commit. */
    fun physicalReceiptCompleted(transaction: ByteArray): Boolean {
        val value = pending ?: return false
        if (!transaction.contentEquals(value.transaction)) return false
        complete()
        return true
    }

    private fun sendBegin(value: Pending): Boolean = session.requestPayload(
        DeviceControlProtocol.Command.CONFIG_BEGIN,
        ByteBuffer.allocate(8).putInt(DeviceControlProtocol.RESET_TRANSFER_KIND).putInt(32).array(),
    ).also { if (!it) fail("设备忙，未发送恢复出厂请求") }

    private fun sendRead(value: Pending, offset: Int): Boolean = session.requestPayload(
        DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(20).putInt(DeviceControlProtocol.RESET_TRANSFER_KIND shl 16 or offset)
            .put(value.transaction).array(),
    ).also { if (!it) fail("无法发送只读回执查询") }

    private fun received(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) {
        if (!active) return
        if (phase == Phase.PREPARING) {
            if (command != DeviceControlProtocol.Command.CONFIG_READ) return
            val chunk = snapshot.configChunk
            if (snapshot.error != 0 || chunk == null || chunk.totalLength < 16 ||
                !chunk.bytes.copyOfRange(0, 4).contentEquals("SCS1".toByteArray())) {
                fail("无法确认设备配置版本（${snapshot.error})")
            } else {
                val revision = ByteBuffer.wrap(chunk.bytes, 8, 8).long
                val transaction = ByteArray(16).also { SecureRandom().nextBytes(it) }
                val next = Pending(revision, transaction)
                if (revision <= 0L || !save(next)) { transaction.fill(0); fail("无法保存恢复出厂事务定位符") }
                else { pending = next; phase = Phase.BEGIN; publish(); if (!sendBegin(next)) fail("设备忙，未发送恢复出厂请求") }
            }
            return
        }
        val value = pending ?: return
        when (phase) {
            Phase.BEGIN -> if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) {
                if (snapshot.error != 0) fail("请求未接受（${snapshot.error}）")
                else {
                    phase = Phase.APPEND; publish()
                    val record = ByteBuffer.allocate(32).put("SRT1".toByteArray()).putInt(0)
                        .putLong(value.revision).put(value.transaction).array()
                    val accepted = session.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, record)
                    record.fill(0)
                    if (!accepted) fail("恢复出厂请求未写入传输队列")
                }
            }
            Phase.APPEND -> if (command == DeviceControlProtocol.Command.CONFIG_APPEND) {
                if (snapshot.error != 0) { session.cancelConfigTransaction(); fail("请求上传失败（${snapshot.error}）") }
                else {
                    phase = Phase.APPLY; publish()
                    if (!session.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0)))
                        fail("请求未进入设备提交阶段")
                }
            }
            Phase.APPLY -> if (command == DeviceControlProtocol.Command.CONFIG_APPLY) {
                session.finishConfigTransaction()
                phase = Phase.AWAITING_RECEIPT
                publish() // ACK is acceptance only; SRS1/SRR1 proves completion.
            }
            Phase.QUERY_FIRST, Phase.QUERY_LAST -> if (command == DeviceControlProtocol.Command.CONFIG_READ) {
                val chunk = snapshot.configChunk
                if (snapshot.error != 0 || chunk == null || chunk.totalLength != 28) { fail("回执不可确认（${snapshot.error}）"); return }
                if (phase == Phase.QUERY_FIRST) {
                    first = chunk.bytes.copyOf()
                    phase = Phase.QUERY_LAST; publish()
                    if (!sendRead(value, 16)) fail("无法读取回执末段")
                } else {
                    val wire = (first ?: ByteArray(0)) + chunk.bytes
                    if (wire.size < 28 || !wire.copyOfRange(0, 4).contentEquals("SRS1".toByteArray()) ||
                        ByteBuffer.wrap(wire, 8, 4).int != 0 ||
                        !wire.copyOfRange(12, 28).contentEquals(value.transaction)) fail("回执格式或事务不匹配")
                    else when (ByteBuffer.wrap(wire, 4, 4).int) {
                        2 -> complete()
                        0 -> fail("设备确认未找到该恢复出厂事务")
                        1 -> { phase = Phase.AWAITING_RECEIPT; publish() }
                        else -> fail("设备返回未知回执状态")
                    }
                    wire.fill(0); first?.fill(0); first = null
                }
            }
            else -> Unit
        }
    }

    private fun complete() { phase = Phase.COMPLETED; publish() }
    /** Call only after the caller has durably removed the matching old binding. */
    fun confirmLocalRevocation(): Boolean {
        val value = pending ?: return false
        if (phase != Phase.COMPLETED || !preferences.edit().remove(key).commit()) return false
        value.transaction.fill(0); pending = null; publish()
        return true
    }
    private fun fail(reason: String) { phase = Phase.FAILED; changed(State(phase, pending?.transaction?.copyOf(), pending?.revision, reason)) }
    private fun publish() = changed(current())
    private fun message() = when (phase) {
        Phase.AWAITING_RECEIPT -> "恢复出厂结果待确认；请核对设备回执，不要重复提交"
        Phase.COMPLETED -> "恢复出厂已由设备回执确认；现在才可撤销本机旧控制凭据"
        Phase.FAILED -> "恢复出厂结果未确认；已保留旧凭据和事务定位符"
        else -> ""
    }
    private fun save(value: Pending): Boolean = preferences.edit().putString(key,
        "${value.revision}:${value.transaction.joinToString("") { "%02x".format(it.toInt() and 0xff) }}").commit()
    private fun load(): Pending? = runCatching {
        val parts = preferences.getString(key, null)?.split(":")
        if (parts == null || parts.size != 2) null else {
            val revision = parts[0].toLong()
            val tx = ByteArray(16) { parts[1].substring(it * 2, it * 2 + 2).toInt(16).toByte() }
            if (revision <= 0 || parts[1].length != 32 || tx.all { it == 0.toByte() }) null
            else Pending(revision, tx)
        }
    }.getOrNull()

    override fun close() { active = false; first?.fill(0); first = null; resultSubscription.cancel() }

    companion object {
        private fun key(deviceId: String) = "reset-" + MessageDigest.getInstance("SHA-256").digest(deviceId.toByteArray())
            .joinToString("") { "%02x".format(it.toInt() and 0xff) }
        /** Public locator for the existing QR/proof recovery transport only. */
        fun pendingPhysical(context: Context, deviceId: String): ByteArray? = runCatching {
            val value = context.applicationContext.getSharedPreferences("shaniu-reset-receipts", Context.MODE_PRIVATE)
                .getString(key(deviceId), null)?.split(":") ?: return@runCatching null
            if (value.size != 2 || value[1].length != 32) null else ByteArray(16) {
                value[1].substring(it * 2, it * 2 + 2).toInt(16).toByte()
            }
        }.getOrNull()
        fun hasPending(context: Context, deviceId: String) = pendingPhysical(context, deviceId) != null
        fun completePhysical(context: Context, deviceId: String, transaction: ByteArray): Boolean {
            val saved = pendingPhysical(context, deviceId) ?: return false
            return try { saved.contentEquals(transaction) && context.applicationContext
                .getSharedPreferences("shaniu-reset-receipts", Context.MODE_PRIVATE).edit().remove(key(deviceId)).commit() }
            finally { saved.fill(0) }
        }
    }
}
