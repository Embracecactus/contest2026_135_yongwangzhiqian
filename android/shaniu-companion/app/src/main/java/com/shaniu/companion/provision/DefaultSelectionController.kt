// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import java.security.SecureRandom

/** One foreground editor on the existing authenticated session. Query is
 * metadata-only; an explicit refresh job reads storage. ACK is never a save.
 * Disconnect/close never replays a write or claims remote cancellation.
 */
internal class DefaultSelectionController(
    private val session: DeviceControlSession,
    private val token: () -> ByteArray = { ByteArray(16).also { SecureRandom().nextBytes(it) } },
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class Snapshot(val state: Int, val epoch: ByteArray, val id: Long,
        val error: Int, val releaseError: Int, val flags: Int, val revision: ULong?,
        val operation: ByteArray, val filename: String?, val sequence: Long) {
        val saved get() = flags and 2 != 0
        val rendered get() = flags and 4 != 0
        val refresh get() = flags and 8 != 0
        val recovering get() = flags and 16 != 0
    }
    data class State(val snapshot: Snapshot?, val busy: Boolean, val message: String)
    private enum class Phase { IDLE, READ, VERIFY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var snapshot: Snapshot? = null
    private var message = "尚未读取设备默认状态"
    private var query = ByteArray(0)
    private var buffer = ByteArray(128)
    private var offset = 0
    private var request: ByteArray? = null
    private var expected: Pair<ByteArray, ByteArray>? = null
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        if (!it.authenticated || it.generation != generation) {
            generation = it.generation
            snapshot = null; phase = Phase.IDLE; ownsTransaction = false; request = null
            buffer.fill(0); query = ByteArray(0)
            message = "连接变化；请重新读取，未完成的操作不会重发"
            publish()
        }
    }
    fun current() = State(snapshot, phase != Phase.IDLE, message)
    private fun publish() { if (active) changed(current()) }
    private fun ready() = active && phase == Phase.IDLE && session.current().authenticated &&
        generation == session.current().generation
    fun refresh(): Boolean {
        if (!ready()) return false
        val nonce = token()
        if (!validToken(nonce)) return false
        query = nonce.copyOf(); buffer.fill(0); offset = 0; snapshot = null
        phase = Phase.READ; message = "正在读取设备最近任务"; publish()
        return read(0)
    }
    fun canAct(action: Int): Boolean {
        val value = snapshot ?: return false
        if (!ready()) return false
        return when (action) {
            1 -> value.state in listOf(0,6,7,8,9) && value.revision != null && value.releaseError == 0
            2 -> value.state in listOf(0,6,7,8,9) && value.releaseError == 0
            3 -> value.state in listOf(1,2,5)
            4 -> value.state == 9 && value.releaseError != 0 && !value.recovering
            else -> false
        }
    }
    fun act(action: Int, filename: String? = null): Boolean {
        if (!canAct(action)) return false
        val value = snapshot ?: return false
        val nonce = token()
        val bytes = try { encode(action, value.epoch, nonce, value.id,
            if (action == 1) value.revision else null, filename) }
        catch (_: IllegalArgumentException) { return false }
        expected = value.epoch.copyOf() to nonce.copyOf()
        request = bytes; offset = 0; snapshot = null; ownsTransaction = true
        phase = Phase.BEGIN; message = "正在提交设备操作，尚未确认结果"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(17).putInt(96).array())
    }
    private fun read(at: Int) = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(20).putInt((17 shl 16) or at).put(query).array())
    private fun send(command: DeviceControlProtocol.Command, bytes: ByteArray): Boolean {
        val accepted = session.requestPayload(command, bytes)
        if (!accepted) {
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) ownsTransaction = false
            fail("设备正忙，结果未确认；请重新读取")
        }
        return accepted
    }
    private fun fail(reason: String) {
        phase = Phase.IDLE; snapshot = null; request = null; buffer.fill(0)
        message = reason
        if (ownsTransaction) { ownsTransaction = false; session.cancelConfigTransaction() }
        publish()
    }
    private fun received(command: DeviceControlProtocol.Command, reply: DeviceControlProtocol.Snapshot) {
        if (!active || phase == Phase.IDLE || !session.current().authenticated ||
            generation != session.current().generation) return
        val awaited = when (phase) {
            Phase.BEGIN -> DeviceControlProtocol.Command.CONFIG_BEGIN
            Phase.APPEND -> DeviceControlProtocol.Command.CONFIG_APPEND
            Phase.APPLY -> DeviceControlProtocol.Command.CONFIG_APPLY
            else -> DeviceControlProtocol.Command.CONFIG_READ
        }
        if (command != awaited) return
        if (reply.error != 0) {
            fail(if (reply.error in listOf(-95,-138,-38)) "此固件尚不支持默认表情管理"
                else "设备返回 ${reply.error}，结果未确认；请重新读取")
            return
        }
        when (phase) {
            Phase.BEGIN, Phase.APPEND -> {
                if (offset < 96) {
                    val bytes = request!!.copyOfRange(offset, offset + 32); offset += 32
                    phase = Phase.APPEND; send(DeviceControlProtocol.Command.CONFIG_APPEND, bytes)
                } else {
                    phase = Phase.APPLY; send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf())
                }
            }
            Phase.APPLY -> {
                request = null; ownsTransaction = false; phase = Phase.IDLE
                session.finishConfigTransaction("设备已受理，正在读取结果")
                refresh()
            }
            else -> {
                val chunk = reply.configChunk
                if (chunk == null || chunk.totalLength != 128 || chunk.bytes.size != 16) {
                    fail("默认状态格式无效"); return
                }
                if (phase == Phase.READ) {
                    chunk.bytes.copyInto(buffer, offset); offset += 16
                    if (offset < 128) read(offset)
                    else { phase = Phase.VERIFY; read(112) }
                } else {
                    if (!buffer.copyOfRange(112,128).contentEquals(chunk.bytes)) {
                        fail("设备状态已变化，请重新读取"); return
                    }
                    val value = try { decode(buffer) } catch (_: IllegalArgumentException) {
                        fail("设备默认状态无效"); return
                    }
                    snapshot = value; phase = Phase.IDLE
                    message = if (expected?.let { !it.first.contentEquals(value.epoch) || !it.second.contentEquals(value.operation) } == true)
                        "已读取设备当前状态；未确认本次操作，请核对后继续"
                    else when (value.state) {
                        0 -> "尚无选择任务；请刷新设备默认后再设置"
                        1,2 -> if (value.refresh) "设备已受理，刷新待完成" else "设备已受理，尚未保存"
                        3 -> "设备正在完成提交，暂不可取消"
                        4 -> "设备报告已保存，尚未确认显示"
                        5 -> "取消待确认"
                        6 -> if (value.refresh) "设备默认已刷新" else "设备报告已保存并显示"
                        7 -> "设备已确认取消"
                        8 -> "操作失败（${value.error}），请刷新设备默认"
                        else -> "结果未知（${value.error}）；保存与显示以设备回读为准"
                    }
                    publish()
                }
            }
        }
    }
    override fun close() {
        active = false; results.cancel(); connection.cancel()
        if (ownsTransaction) session.cancelConfigTransaction()
        ownsTransaction = false; request = null; snapshot = null; expected = null; phase = Phase.IDLE
    }
    companion object {
        private fun validToken(bytes: ByteArray) = bytes.size == 16 && bytes.any { it != 0.toByte() }
        private fun validName(name: String) = name.length < 40 && Regex("[a-z][a-z0-9._-]*\\.bkep").matches(name)
        fun encode(action: Int, epoch: ByteArray, nonce: ByteArray, id: Long,
                   revision: ULong? = null, filename: String? = null): ByteArray {
            require(action in 1..4 && validToken(epoch) && validToken(nonce) && id in 0..0xffffffffL)
            require(action !in 3..4 || id != 0L)
            if (action == 1) require(revision != null && filename != null && validName(filename))
            else require(revision == null && filename == null)
            return ByteBuffer.allocate(96).apply {
                put("ESC1".toByteArray(Charsets.US_ASCII)); putInt(action); put(epoch); put(nonce)
                putInt(id.toInt()); putInt(0); putLong(revision?.toLong() ?: 0)
                if (filename != null) put(filename.toByteArray(Charsets.US_ASCII))
            }.array()
        }
        fun decode(bytes: ByteArray): Snapshot {
            require(bytes.size == 128 && String(bytes,0,4,Charsets.US_ASCII) == "ESS1")
            val b = ByteBuffer.wrap(bytes); b.position(4)
            val state=b.int; val epoch=ByteArray(16).also { b.get(it) }; val id=b.int.toUInt().toLong()
            val error=b.int; val release=b.int; val flags=b.int; val revision=b.long.toULong(); val expected=b.long
            val operation=ByteArray(16).also { b.get(it) }; val name=ByteArray(40).also { b.get(it) }
            val sequence=b.long; val reserved=b.long; val end=name.indexOf(0)
            require(state in 0..9 && validToken(epoch) && error <= 0 && release <= 0 && flags and 31 == flags)
            require(sequence != 0L && reserved == 0L && end >= 0 && name.drop(end).all { it == 0.toByte() })
            val filename=if (end==0) null else String(name,0,end,Charsets.US_ASCII)
            require(filename == null || validName(filename))
            val known=flags and 1 != 0; val saved=flags and 2 != 0; val rendered=flags and 4 != 0
            val refresh=flags and 8 != 0; val recovering=flags and 16 != 0
            require((state==0)==(id==0L) && (!saved || (known && !refresh)) && (!rendered || saved))
            require(!saved || state !in listOf(1,2,5))
            require(state != 4 || saved)
            require((!known || filename!=null) && (known || revision==0uL))
            require((release==0 || state==9) && (!recovering || (release!=0 && state==9)))
            require(state!=6 || (error==0 && release==0 && known && (refresh || rendered)))
            require(state!=7 || (error!=0 && release==0 && !saved && !rendered))
            require(state!=0 || (flags==0 && error==0 && release==0 && revision==0uL && expected==0L && filename==null && operation.all { it==0.toByte() }))
            return Snapshot(state,epoch,id,error,release,flags,revision.takeIf { known },operation,filename,sequence)
        }
    }
}
