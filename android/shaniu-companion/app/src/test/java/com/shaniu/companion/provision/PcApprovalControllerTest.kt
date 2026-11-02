// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer
import java.security.MessageDigest

class PcApprovalControllerTest {
    private class Fixture {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val controller: PcApprovalController
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
            controller = PcApprovalController(session) {}
        }
        fun reply(total: Int, bytes: ByteArray? = null, error: Int = 0) {
            events.result(sent.last().first, ok.copy(error = error,
                configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(total, it) }))
        }
        /** Serve the APV1 view in 16-byte reads exactly as the device does. */
        fun serve(state: Int, record: ByteArray, remaining: Int = 30000, changedHeader: Boolean = false) {
            val total = 16 + record.size
            val head = ByteBuffer.allocate(16).put("APV1".toByteArray()).putInt(state)
                .putInt(record.size).putInt(remaining).array()
            reply(total, head)
            var at = 0
            while (at < record.size) {
                val chunk = ByteArray(16); record.copyInto(chunk, 0, at, minOf(record.size, at + 16))
                reply(total, chunk); at += 16
            }
            val verify = if (changedHeader) head.copyOf().also { it[7] = 5 } else
                head.copyOf().also { ByteBuffer.wrap(it).putInt(12, remaining - 500) }
            reply(total, verify)
        }
    }

    private fun record(id: Byte = 7, operation: String = "git push --force origin main\n# 中文"): ByteArray {
        val agent = "claude-code".toByteArray(); val target = "/home/u/repo".toByteArray()
        val op = operation.toByteArray()
        return ByteBuffer.allocate(32 + agent.size + target.size + op.size)
            .put("APR1".toByteArray()).put(ByteArray(16) { id }).putInt(60000)
            .putShort(agent.size.toShort()).putShort(target.size.toShort())
            .putShort(op.size.toShort()).putShort(0).put(agent).put(target).put(op).array()
    }

    @Test fun showsExactRequestAndBindsDecisionToItsDigest() {
        val f = Fixture(); val bytes = record()
        assertTrue(f.controller.refresh()); f.serve(1, bytes)
        val request = f.controller.current().request!!
        assertEquals("git push --force origin main\n# 中文", request.operation)
        assertEquals("/home/u/repo", request.target)
        assertEquals("claude-code", request.agent)
        assertTrue(f.controller.decide(false))
        val begin = ByteBuffer.wrap(f.sent.last().second)
        assertEquals(30, begin.int); assertEquals(56, begin.int)
        f.reply(16)
        val first = f.sent.last().second; f.reply(16)
        val second = f.sent.last().second
        val decision = first + second
        assertEquals("APD1", String(decision.copyOfRange(0, 4)))
        assertEquals(2, ByteBuffer.wrap(decision, 4, 4).int)
        assertArrayEquals(ByteArray(16) { 7 }, decision.copyOfRange(8, 24))
        assertArrayEquals(MessageDigest.getInstance("SHA-256").digest(bytes), decision.copyOfRange(24, 56))
        f.reply(16) // second APPEND accepted -> APPLY
        assertEquals(DeviceControlProtocol.Command.CONFIG_APPLY, f.sent.last().first)
        f.reply(16) // APPLY accepted -> controller re-reads for confirmation
        assertEquals(DeviceControlProtocol.Command.CONFIG_READ, f.sent.last().first)
        f.serve(3, bytes)
        assertEquals(3, f.controller.current().request?.state)
        assertFalse(f.controller.decide(true))
    }

    @Test fun changedOrUnsafeRequestIsNeverShownOrDecided() {
        val f = Fixture()
        assertTrue(f.controller.refresh()); f.serve(1, record(), changedHeader = true)
        assertNull(f.controller.current().request)
        assertFalse(f.controller.decide(true))
        assertTrue(f.controller.refresh()); f.serve(1, record(operation = "rm ‮fdp.exe"))
        assertNull(f.controller.current().request)
        assertTrue(f.controller.refresh()); f.serve(1, record(operation = "a\u001b[2K"))
        assertNull(f.controller.current().request)
    }

    @Test fun emptyDeviceAndRejectedDecisionAreReported() {
        val f = Fixture()
        assertTrue(f.controller.refresh())
        f.reply(16, ByteBuffer.allocate(16).put("APV1".toByteArray()).putInt(0).putInt(0).putInt(0).array())
        assertTrue(f.controller.current().noRequest)
        assertTrue(f.controller.refresh()); f.serve(1, record())
        assertTrue(f.controller.decide(true)); f.reply(16); f.reply(16); f.reply(16)
        f.reply(16, error = -116) // APPLY: request superseded on the device
        assertNull(f.controller.current().request)
        assertFalse(f.controller.current().busy)
    }

    @Test fun protocolAllowsOnlyBoundedApprovalFrames() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() },
            { _, snapshot -> states += snapshot })
        protocol.start(); protocol.receive(response(sent.last()))
        for ((offset, total) in listOf(0 to 16, 16 to 50, 1088 to 1104)) {
            assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,
                be32((30 shl 16) or offset)))
            protocol.receive(response(sent.last(), flags = total))
            assertEquals(total, states.last().configChunk!!.totalLength)
        }
        assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(30).putInt(56).array()))
        protocol.receive(response(sent.last()))
        for (size in listOf(20, 57)) assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(30).putInt(size).array())
        }
        // The PC request kind is not a phone operation.
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(29).putInt(40).array())
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ, be32((30 shl 16) or 1104))
        }
        protocol.close()
    }

    private fun response(request: ByteArray, error: Int = 0, flags: Int = 0) =
        ByteBuffer.allocate(40).putInt(0x53444331)
            .putInt(ByteBuffer.wrap(request).getInt(4) or Int.MIN_VALUE)
            .putInt(ByteBuffer.wrap(request).getInt(8)).putInt(24)
            .putInt(error).putInt(flags).putInt(-1).putInt(-1).putInt(-1).putInt(0).array()

    private fun be32(value: Int) = ByteBuffer.allocate(4).putInt(value).array()
}
