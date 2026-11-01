// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.JsonObject
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

class AgentDataControllerTest {
    private class Fixture(kind: Int = 26) {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val published = mutableListOf<AgentDataController.State>()
        val controller: AgentDataController
        init {
            session.setForeground(true)
            session.connect(object : DeviceControlSession.Factory {
                override fun open(e: DeviceControlSession.Events): DeviceControlSession.Transport {
                    events = e
                    return object : DeviceControlSession.Transport {
                        override fun request(c: DeviceControlProtocol.Command, v: Int, a: (Boolean) -> Unit) { sent += c to byteArrayOf(); a(true) }
                        override fun requestOta(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { error("not OTA") }
                        override fun requestPayload(c: DeviceControlProtocol.Command, p: ByteArray, a: (Boolean) -> Unit) { sent += c to p.copyOf(); a(true) }
                        override fun close() {}
                    }
                }
            })
            events.result(DeviceControlProtocol.Command.STATUS, ok)
            controller = AgentDataController(session, kind) { published += it }
        }
        fun reply(error: Int = 0, total: Int = 0, bytes: ByteArray? = null) {
            events.result(sent.last().first, ok.copy(error = error,
                configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(total, it) }))
        }
        fun read(json: String, revision: Long, changed: Boolean = false) {
            val bytes = json.toByteArray()
            val head = ByteBuffer.allocate(16).put("AJV1".toByteArray()).putInt(bytes.size).putLong(revision).array()
            reply(total = 16 + bytes.size, bytes = head)
            bytes.asList().chunked(16).forEach { chunk ->
                reply(total = 16 + bytes.size, bytes = chunk.toByteArray().copyOf(16))
            }
            reply(total = 16 + bytes.size, bytes = head.copyOf().also { if (changed) it[15]++ })
        }
        fun acceptMutation(): String {
            reply()
            val all = mutableListOf<Byte>()
            while (sent.last().first == DeviceControlProtocol.Command.CONFIG_APPEND) {
                all += sent.last().second.toList(); reply()
            }
            assertEquals(DeviceControlProtocol.Command.CONFIG_APPLY, sent.last().first)
            reply()
            assertTrue(controller.current().busy)
            assertNull(controller.current().document)
            return String(all.toByteArray())
        }
    }
    private fun facts(revision: Int, value: String?) =
        """{"version":2,"revision":"$revision","enabled":true,"facts":[${value?.let { """{"key":"name","value":"$it"}""" }.orEmpty()}]}"""
    private fun set(value: String) = JsonObject().apply {
        addProperty("op", "set"); addProperty("key", "name"); addProperty("value", value)
    }
    @Test fun writeUsesCasAndOnlyPublishesAfterReadback() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.read(facts(1, null), 1)
        assertTrue(f.controller.apply(set("新称呼")))
        val command = f.acceptMutation()
        assertTrue(command.contains("\"expected_revision\":\"1\""))
        f.read(facts(2, "新称呼"), 2)
        assertEquals("新称呼", f.controller.current().document?.getAsJsonArray("facts")?.get(0)?.asJsonObject?.get("value")?.asString)
        assertFalse(f.controller.current().busy)
    }
    @Test fun wrongReadbackIsNotSuccess() {
        val f = Fixture(); f.controller.refresh(); f.read(facts(1, null), 1)
        f.controller.apply(set("新称呼")); f.acceptMutation(); f.read(facts(2, "旧称呼"), 2)
        assertNull(f.controller.current().document)
        assertTrue(f.controller.current().message.contains("不一致"))
    }
    @Test fun changedRevisionRejectsMixedChunks() {
        val f = Fixture(); f.controller.refresh(); f.read(facts(1, null), 1, changed = true)
        assertNull(f.controller.current().document)
        assertFalse(f.controller.apply(set("新称呼")))
    }
    @Test fun disconnectedWriteIsNotReplayed() {
        val f = Fixture(); f.controller.refresh(); f.read(facts(1, null), 1)
        f.controller.apply(set("新称呼")); f.events.closed("lost")
        val count = f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_BEGIN, f.ok)
        assertEquals(count, f.sent.size); assertNull(f.controller.current().document)
    }
    @Test fun reauthenticatedSessionRepublishesWithoutReplayingMutation() {
        val f = Fixture(); f.controller.refresh(); f.read(facts(1, null), 1)
        f.controller.apply(set("新称呼")); f.events.closed("lost")
        val sentBefore = f.sent.size
        f.session.disconnect(user = false)
        f.session.setForeground(true)
        val publishedBefore = f.published.size
        f.events.result(DeviceControlProtocol.Command.STATUS, f.ok)
        assertTrue(f.session.current().authenticated)
        assertTrue(f.published.size > publishedBefore)
        assertEquals(sentBefore, f.sent.size)
        assertNull(f.controller.current().document)
        assertFalse(f.controller.current().busy)
        assertTrue(f.controller.refresh())
        assertEquals(DeviceControlProtocol.Command.CONFIG_READ, f.sent.last().first)
    }
    @Test fun rejectedBeginDoesNotCancelAnotherOwner() {
        val f = Fixture(); f.controller.refresh(); f.read(facts(1, null), 1)
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(7).putInt(32).array()))
        assertFalse(f.controller.apply(set("新称呼")))
        assertFalse(f.sent.any { it.first == DeviceControlProtocol.Command.CONFIG_CANCEL })
    }
    @Test fun unsupportedDeviceRemainsUnconfirmed() {
        val f = Fixture(); f.controller.refresh(); f.reply(-95)
        assertNull(f.controller.current().document)
        assertFalse(f.controller.current().busy)
    }
    @Test fun skillEnableRequiresMatchingDeviceReadback() {
        val f = Fixture(27)
        fun skills(revision: Int, enabled: Boolean) = """{"revision":"$revision","items":[{"id":"focus-coach","enabled":$enabled}]}"""
        f.controller.refresh(); f.read(skills(1, false), 1)
        f.controller.apply(JsonObject().apply { addProperty("id", "focus-coach"); addProperty("enabled", true) })
        assertTrue(f.acceptMutation().contains("\"revision\":\"1\""))
        f.read(skills(2, true), 2)
        assertNotNull(f.controller.current().document)
    }
    @Test fun trustedPublisherReadbackMustMatchImportedFingerprint() {
        val f = Fixture(27)
        f.controller.refresh(); f.read("""{"revision":"1","items":[],"trusted_key_sha256":""}""", 1)
        val key = "fixture-public".toByteArray()
        val hash = java.security.MessageDigest.getInstance("SHA-256").digest(key)
            .joinToString("") { "%02x".format(it.toInt() and 255) }
        f.controller.apply(JsonObject().apply {
            addProperty("op", "trust")
            addProperty("public_key", java.util.Base64.getEncoder().encodeToString(key))
        })
        f.acceptMutation()
        f.read("""{"revision":"2","items":[],"trusted_key_sha256":"$hash"}""", 2)
        assertNotNull(f.controller.current().document)
    }
    @Test fun installationReadbackMustBeVerifiedAndDisabled() {
        val f = Fixture(27)
        f.controller.refresh(); f.read("""{"revision":"1","items":[]}""", 1)
        val manifest = java.util.Base64.getEncoder().encodeToString("""{"id":"fixture"}""".toByteArray())
        f.controller.apply(JsonObject().apply {
            addProperty("op", "install"); addProperty("manifest", manifest); addProperty("signature", "fixture")
        })
        f.acceptMutation()
        f.read("""{"revision":"2","items":[{"id":"fixture","signed":false,"enabled":false}]}""", 2)
        assertNull(f.controller.current().document)
    }
}
