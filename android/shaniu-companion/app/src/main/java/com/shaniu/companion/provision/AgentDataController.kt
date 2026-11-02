// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction

/** Revision-checked facts/Skill editor on the existing foreground session. */
internal class AgentDataController(
    private val session: DeviceControlSession,
    private val kind: Int,
    private val changed: (State) -> Unit,
) : AutoCloseable {
    data class State(val document: JsonObject?, val busy: Boolean, val message: String)
    private enum class Phase { IDLE, HEADER, BODY, VERIFY, BEGIN, APPEND, APPLY }
    private var phase = Phase.IDLE
    private var document: JsonObject? = null
    private var message = "尚未读取设备数据"
    private var header = ByteArray(0)
    private var body = ByteArray(0)
    private var offset = 0
    private var request = ByteArray(0)
    private var requested: JsonObject? = null
    private var sent = 0
    private var ownsTransaction = false
    private var active = true
    private var generation = session.current().generation
    private var authenticated = session.current().authenticated
    private val results = session.observeResults(::received)
    private val connection = session.observe {
        val wasAuthenticated = authenticated
        authenticated = it.authenticated
        if (!it.authenticated || generation != it.generation) {
            generation = it.generation
            ownsTransaction = false
            fail("连接变化；请重新读取，未完成的写入不会重发")
        } else if (!wasAuthenticated) {
            // Re-enable explicit reads after authentication without replaying
            // the previous operation or presenting its stale document.
            publish()
        }
    }
    init { require(kind in 26..28) }
    fun kind() = kind
    fun current() = State(document, phase != Phase.IDLE, message)
    private fun publish() { if (active) changed(current()) }
    private fun clearBuffers() { header.fill(0); body.fill(0); request.fill(0); header = byteArrayOf(); body = byteArrayOf(); request = byteArrayOf() }
    fun refresh(): Boolean {
        if (!active || phase != Phase.IDLE || !session.current().authenticated) return false
        document = null; clearBuffers(); offset = 0
        phase = Phase.HEADER; message = "正在读取设备数据"; publish()
        return read(0)
    }
    fun apply(value: JsonObject): Boolean {
        val current = document ?: return false
        if (!active || phase != Phase.IDLE || !session.current().authenticated ||
            session.current().generation != generation || session.current().snapshot?.busy != false) return false
        val next = value.deepCopy()
        next.addProperty(if (kind == 26) "expected_revision" else "revision", current["revision"].asString)
        request = next.toString().toByteArray(Charsets.UTF_8)
        if (request.size !in 1..if (kind == 28) 32768 else 4095) { request.fill(0); return false }
        requested = value.deepCopy(); sent = 0
        phase = Phase.BEGIN; ownsTransaction = true
        document = null; message = "正在提交，尚未确认生效"; publish()
        return send(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(kind).putInt(request.size).array())
    }
    private fun read(at: Int) = send(DeviceControlProtocol.Command.CONFIG_READ,
        ByteBuffer.allocate(4).putInt((kind shl 16) or at).array())
    private fun send(command: DeviceControlProtocol.Command, bytes: ByteArray): Boolean {
        val accepted = session.requestPayload(command, bytes)
        bytes.fill(0)
        if (!accepted) {
            if (command == DeviceControlProtocol.Command.CONFIG_BEGIN) ownsTransaction = false
            fail("设备忙，结果未确认；请重新读取")
        }
        return accepted
    }
    private fun fail(reason: String) {
        phase = Phase.IDLE; document = null; requested = null; clearBuffers()
        message = reason
        if (ownsTransaction) { ownsTransaction = false; session.cancelConfigTransaction() }
        publish()
    }
    private fun append() {
        val count = minOf(32, request.size - sent)
        val chunk = request.copyOfRange(sent, sent + count)
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
                -95 -> "当前固件不支持此功能"
                -116, -11 -> "设备版本已变化，请重新读取后修改"
                -13 -> "设备拒绝操作，请检查记忆开关或权限"
                -129 -> "技能签名无效或发布者不受信任"
                -22, -74 -> "文件内容或数据格式不符合设备要求"
                -17 -> "设备已有外部技能；重新导入发布者公钥会清除原技能"
                -115 -> "设备保存结果尚未确认；请重启设备后重新读取"
                else -> "设备返回 ${reply.error}，结果未确认"
            }); return
        }
        when (phase) {
            Phase.BEGIN -> { phase = Phase.APPEND; append() }
            Phase.APPEND -> if (sent < request.size) append() else {
                phase = Phase.APPLY
                send(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf())
            }
            Phase.APPLY -> {
                ownsTransaction = false; phase = Phase.IDLE
                session.finishConfigTransaction("操作已受理，正在回读确认")
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
            val magic = input.int; val bytes = input.int
            if (magic != 0x414a5631 || bytes !in 1..4095 || chunk.totalLength != 16 + bytes) {
                fail("设备数据格式不受支持"); return
            }
            body = ByteArray(bytes); offset = 0; phase = Phase.BODY; read(16); return
        }
        if (chunk.totalLength != 16 + body.size) { fail("设备数据已变化，请重新读取"); return }
        if (phase == Phase.BODY) {
            val count = minOf(16, body.size - offset)
            chunk.bytes.copyInto(body, offset, 0, count); offset += count
            if (offset < body.size) read(16 + offset) else { phase = Phase.VERIFY; read(0) }
            return
        }
        if (!header.contentEquals(chunk.bytes)) { fail("设备数据已变化，请重新读取"); return }
        try {
            val decoder = Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            val result = JsonParser.parseString(decoder.decode(ByteBuffer.wrap(body)).toString()).asJsonObject
            val revision = java.lang.Long.toUnsignedString(ByteBuffer.wrap(header).getLong(8))
            require(result["revision"].asString == revision)
            require(result[if (kind == 26) "facts" else "items"].isJsonArray)
            requested?.let { verify(result, it) }
            document = result; requested = null; clearBuffers(); phase = Phase.IDLE
            message = "已从设备回读确认"; publish()
        } catch (_: RuntimeException) { fail("设备回读与操作不一致，请重新读取") }
          catch (_: java.nio.charset.CharacterCodingException) { fail("设备返回无效文字编码") }
    }
    private fun verify(result: JsonObject, wanted: JsonObject) {
        if (kind == 26) {
            val item = result["facts"].asJsonArray.firstOrNull {
                it.asJsonObject["key"].asString == wanted["key"].asString
            }?.asJsonObject
            if (wanted["op"].asString == "delete") require(item == null)
            else require(item?.get("value")?.asString == wanted["value"].asString)
        } else if (kind == 27) {
            when (wanted.get("op")?.asString) {
                "trust" -> {
                    val key = java.util.Base64.getDecoder().decode(wanted["public_key"].asString)
                    val hash = java.security.MessageDigest.getInstance("SHA-256").digest(key)
                        .joinToString("") { "%02x".format(it.toInt() and 255) }
                    require(result["trusted_key_sha256"].asString == hash)
                    return
                }
                "install" -> {
                    val bytes = java.util.Base64.getDecoder().decode(wanted["manifest"].asString)
                    val manifest = JsonParser.parseString(String(bytes, Charsets.UTF_8)).asJsonObject
                    val item = result["items"].asJsonArray.first {
                        it.asJsonObject["id"].asString == manifest["id"].asString
                    }.asJsonObject
                    require(item["signed"].asBoolean && !item["enabled"].asBoolean)
                    return
                }
            }
            val item = result["items"].asJsonArray.first { it.asJsonObject["id"].asString == wanted["id"].asString }.asJsonObject
            require(item["enabled"].asBoolean == wanted["enabled"].asBoolean)
        } else {
            when (wanted.get("op")?.asString) {
                "trust" -> {
                    val key = java.util.Base64.getDecoder().decode(wanted["public_key"].asString)
                    val hash = java.security.MessageDigest.getInstance("SHA-256").digest(key)
                        .joinToString("") { "%02x".format(it.toInt() and 255) }
                    require(result["trusted_key_sha256"].asString == hash)
                }
                "install" -> {
                    val pack = wanted["package"].asJsonObject
                    val manifest = JsonParser.parseString(String(java.util.Base64.getDecoder()
                        .decode(pack["manifest"].asString), Charsets.UTF_8)).asJsonObject
                    val item = result["items"].asJsonArray.first {
                        it.asJsonObject["id"].asString == manifest["id"].asString
                    }.asJsonObject
                    require(item["version"].asLong == manifest["version"].asLong)
                }
                "start" -> require(result["running"].asString == wanted["id"].asString)
                "stop" -> require(result["running"].asString.isEmpty())
                "uninstall" -> require(result["items"].asJsonArray.none {
                    it.asJsonObject["id"].asString == wanted["id"].asString
                })
            }
        }
    }
    override fun close() {
        active = false; results.cancel(); connection.cancel()
        if (ownsTransaction) session.cancelConfigTransaction()
        ownsTransaction = false; requested = null; document = null; clearBuffers(); phase = Phase.IDLE
    }
}
