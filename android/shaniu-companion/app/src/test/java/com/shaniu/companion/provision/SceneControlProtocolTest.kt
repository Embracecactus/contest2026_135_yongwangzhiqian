// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

/** Real SDC1 codec; the external peer supplies independently specified frames. */
class SceneControlProtocolTest {
    private fun accepted(action: () -> Boolean): Boolean = try { action() }
        catch (_: IllegalArgumentException) { fail("Production codec rejected a contract-valid request"); false }

    private class Peer {
        val sent = mutableListOf<ByteArray>()
        val results = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, s -> results += s })
        init { protocol.start(); reply() }
        fun reply(error: Int = 0, total: Int = 0) {
            val request = ByteBuffer.wrap(sent.last())
            val frame = ByteBuffer.allocate(40).putInt(0x53444331)
                .putInt(request.getInt(4) or Int.MIN_VALUE).putInt(request.getInt(8)).putInt(24)
                .putInt(error).putInt(total).putInt(-1).putInt(-1).putInt(-1).putInt(0).array()
            frame.toList().chunked(3).forEach { protocol.receive(it.toByteArray()) }
        }
        fun read(kind: Int, offset: Int = 0, receipt: Boolean = false): Boolean {
            val b = ByteBuffer.allocate(if (receipt) 20 else 4).putInt((kind shl 16) or offset)
            if (receipt) b.put(ByteArray(16) { 7 })
            return protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ, b.array())
        }
        fun begin(kind: Int, size: Int) = protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(kind).putInt(size).array())
    }
    @Test fun sceneKindsCanReadAndWriteThroughProductionCodec() {
        for ((kind, size, total) in listOf(Triple(10, 32, 32), Triple(11, 32, 32), Triple(12, 40, 112))) {
            val p = Peer()
            for (offset in 0 until total step 16) {
                assertTrue(accepted { p.read(kind, offset) }); p.reply(total = total)
                assertEquals(total, p.results.last().configChunk!!.totalLength)
            }
            assertTrue(accepted { p.begin(kind, size) }); p.reply()
            assertTrue(p.protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(size)))
            p.reply(); assertTrue(p.protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf()))
            p.reply(); p.protocol.close()
        }
    }
    @Test fun sceneCapabilityRemainsReadOnly() {
        val p = Peer(); assertTrue(accepted { p.read(13) }); p.reply(total = 16)
        assertThrows(IllegalArgumentException::class.java) { p.begin(13, 16) }
        assertThrows(IllegalArgumentException::class.java) { p.read(13, 16) }
        p.protocol.close()
    }
    @Test fun pcSnapshotReceiptAndWriteUseTheirOwnBoundedShapes() {
        val p = Peer()
        for (offset in 0..48 step 16) { assertTrue(accepted { p.read(14, offset) }); p.reply(total = 64) }
        for (offset in 0..16 step 16) { assertTrue(accepted { p.read(14, offset, true) }); p.reply(total = 32) }
        assertTrue(accepted { p.begin(14, 88) }); p.reply()
        assertTrue(p.protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(88)))
        p.reply(); assertTrue(p.protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, byteArrayOf()))
        p.reply(error = -11)
        assertFalse(p.protocol.busy)
        assertTrue(accepted { p.read(14, receipt = true) }); p.reply(total = 32)
        p.protocol.close()
    }
    @Test fun newKindsRejectWrongLengthsOffsetsAndUnknownKindsBeforeSending() {
        val p = Peer(); val count = p.sent.size
        for ((kind, total) in listOf(10 to 32, 11 to 32, 12 to 112, 13 to 16, 14 to 64)) {
            assertThrows(IllegalArgumentException::class.java) { p.read(kind, 1) }
            assertThrows(IllegalArgumentException::class.java) { p.read(kind, total) }
            if (kind != 14) assertThrows(IllegalArgumentException::class.java) { p.read(kind, receipt = true) }
        }
        for ((kind, size) in listOf(10 to 32, 11 to 32, 12 to 40, 14 to 88)) {
            for (bad in listOf(size - 1, size + 1)) assertThrows(IllegalArgumentException::class.java) { p.begin(kind, bad) }
        }
        assertThrows(IllegalArgumentException::class.java) { p.read(14, 32, true) }
        assertThrows(IllegalArgumentException::class.java) { p.read(15) }
        assertThrows(IllegalArgumentException::class.java) { p.begin(15, 88) }
        assertEquals(count, p.sent.size); p.protocol.close()
    }
    @Test fun pcReceiptAndSnapshotCannotBeInterchanged() {
        for (receipt in listOf(false, true)) {
            val p = Peer(); assertTrue(accepted { p.read(14, receipt = receipt) })
            assertThrows(IllegalStateException::class.java) { p.reply(total = if (receipt) 64 else 32) }
            assertTrue(p.protocol.closed)
        }
    }
    @Test fun newReadKindsRejectMalformedResponseSize() {
        for ((kind, size) in listOf(10 to 32, 11 to 32, 12 to 112, 13 to 16, 14 to 64)) {
            val p = Peer(); assertTrue(accepted { p.read(kind) })
            assertThrows(IllegalStateException::class.java) { p.reply(total = size + 1) }
            assertTrue(p.protocol.closed)
        }
    }
    @Test fun newKindsPreserveAuthenticationAndSequenceValidation() {
        val sent = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, _ -> })
        assertThrows(IllegalStateException::class.java) { protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,
            ByteBuffer.allocate(4).putInt(14 shl 16).array()) }
        assertTrue(sent.isEmpty()); protocol.close()
        val p = Peer(); assertTrue(accepted { p.read(14) })
        val wrongSequence = ByteBuffer.allocate(40).putInt(0x53444331).putInt(15 or Int.MIN_VALUE)
            .putInt(0).putInt(24).putInt(0).putInt(64).array()
        assertThrows(IllegalStateException::class.java) { p.protocol.receive(wrongSequence) }
        assertTrue(p.protocol.closed)
    }
}
