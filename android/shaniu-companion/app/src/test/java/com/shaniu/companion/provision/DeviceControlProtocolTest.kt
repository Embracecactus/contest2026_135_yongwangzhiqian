// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

class DeviceControlProtocolTest {
    @Test fun authenticatedScanReadAcceptsBoundedRecordsButCannotWriteScanKind() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, s -> states += s })
        protocol.start(); protocol.receive(response(sent.last()))
        assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ, be32(8 shl 16)))
        protocol.receive(response(sent.last(), flags = 876))
        assertEquals(876, states.last().configChunk!!.totalLength)
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(8).putInt(12).array())
        }
        protocol.close()
    }

    private fun response(request: ByteArray, error: Int = 0, flags: Int = 0,
                         volume: Int = -1, persona: Int = -1, turn: Int = -1,
                         runtimeError: Int = 0) =
        ByteBuffer.allocate(40).putInt(0x53444331)
            .putInt(ByteBuffer.wrap(request).getInt(4) or Int.MIN_VALUE)
            .putInt(ByteBuffer.wrap(request).getInt(8)).putInt(24)
            .putInt(error).putInt(flags).putInt(volume).putInt(persona).putInt(turn)
            .putInt(runtimeError).array()

    private fun be32(value: Int) = ByteBuffer.allocate(4).putInt(value).array()

    @Test fun authenticatesThenAcceptsOnlyConfirmedFragmentedState() {
        for (fragment in 1..40) {
            val sent = mutableListOf<ByteArray>()
            val states = mutableListOf<DeviceControlProtocol.Snapshot>()
            val key = ByteArray(32) { 42 }
            var borrowed: ByteArray? = null
            val protocol = DeviceControlProtocol(key, { borrowed = it; sent += it.copyOf() }, { _, s -> states += s })
            key.fill(0)
            protocol.start()
            assertTrue(borrowed!!.all { it == 0.toByte() })
            assertArrayEquals(ByteArray(32) { 42 }, sent[0].copyOfRange(16, 48))
            protocol.receive(response(sent.last()))
            assertTrue(protocol.authenticated)
            assertTrue(protocol.request(DeviceControlProtocol.Command.STATUS))
            assertFalse(protocol.request(DeviceControlProtocol.Command.CANCEL))
            val reply = response(sent.last(), flags = 29, volume = 73, persona = 2, turn = 0)
            reply.toList().chunked(fragment).forEach { protocol.receive(it.toByteArray()) }
            assertEquals(73, states.last().volume)
            assertEquals(2, states.last().persona)
            assertTrue(states.last().ready)
            assertFalse(states.last().busy)
            assertTrue(protocol.request(DeviceControlProtocol.Command.VOLUME, 20))
            assertEquals(20, ByteBuffer.wrap(sent.last()).getInt(16))
            // A rejected mutation must not be displayed as its requested value.
            protocol.receive(response(sent.last(), error = -16))
            assertEquals(-16, states.last().error)
            assertNull(states.last().volume)
            assertTrue(protocol.request(DeviceControlProtocol.Command.CANCEL))
            protocol.receive(response(sent.last(), flags = 3))
            assertTrue(states.last().busy) // Accepted cancel is not completed drain.
            protocol.close()
            assertFalse(protocol.authenticated)
        }
    }

    @Test fun memoryMutationDistinguishesPendingConfirmedAndUncertain() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, s -> states += s })
        protocol.start(); protocol.receive(response(sent.last()))
        assertThrows(IllegalArgumentException::class.java) { protocol.request(DeviceControlProtocol.Command.MEMORY_SET, 2) }
        assertTrue(protocol.request(DeviceControlProtocol.Command.MEMORY_SET, 1))
        assertEquals(20, sent.last().size)
        assertEquals(1, ByteBuffer.wrap(sent.last()).getInt(16))
        protocol.receive(response(sent.last(), flags = 512 or 128 or 3))
        assertTrue(states.last().memoryPending)
        assertNull(states.last().memoryEnabled)
        protocol.request(DeviceControlProtocol.Command.STATUS)
        protocol.receive(response(sent.last(), flags = 512 or 32 or 64 or 1))
        assertEquals(true, states.last().memoryEnabled)
        assertFalse(states.last().memoryPending || states.last().memoryFailed)
        protocol.request(DeviceControlProtocol.Command.MEMORY_DELETE)
        assertEquals(16, sent.last().size)
        protocol.receive(response(sent.last(), flags = 512 or 128 or 3))
        protocol.request(DeviceControlProtocol.Command.STATUS)
        protocol.receive(response(sent.last(), flags = 512 or 256 or 1))
        assertTrue(states.last().memoryFailed)
        assertNull(states.last().memoryEnabled) // Never report deletion from an uncertain write.
    }

    @Test fun infoIsCapabilityGatedAndUsesItsOwnPayload() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, s -> states += s })
        protocol.start(); protocol.receive(response(sent.last()))
        assertTrue(protocol.request(DeviceControlProtocol.Command.STATUS))
        protocol.receive(response(sent.last(), flags = 1 or 8192))
        assertFalse(states.last().infoSupported)
        assertTrue(states.last().otaSupported)
        assertNull(states.last().otaStatus)
        assertTrue(protocol.request(DeviceControlProtocol.Command.INFO))
        val info = response(sent.last(), flags = 1, volume = 2, persona = 3, turn = 4).also { ByteBuffer.wrap(it).putInt(36, 5) }
        protocol.receive(info)
        assertEquals(DeviceControlProtocol.FirmwareInfo(1, 2, 3, 4, 5), states.last().firmwareInfo)
        assertNull(states.last().volume)
        assertFalse(states.last().otaSupported)
        assertNull(states.last().otaStatus)
    }

    @Test fun otaRequestsEnforceCommandAndPayloadBoundariesAndScrubBorrowedFrames() {
        val sent = mutableListOf<ByteArray>()
        var borrowed: ByteArray? = null
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, {
            borrowed = it
            sent += it.copyOf()
        }, { _, _ -> })
        assertThrows(IllegalStateException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.OTA_STATUS)
        }
        protocol.start(); protocol.receive(response(sent.last()))

        assertThrows(IllegalArgumentException::class.java) {
            protocol.request(DeviceControlProtocol.Command.OTA_STATUS)
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.STATUS)
        }
        for (invalid in listOf(43, 3372)) {
            assertThrows(IllegalArgumentException::class.java) {
                protocol.requestOta(DeviceControlProtocol.Command.OTA_BEGIN, be32(invalid))
            }
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.OTA_BEGIN, ByteArray(3))
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.OTA_APPEND)
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.OTA_APPEND, ByteArray(33))
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestOta(DeviceControlProtocol.Command.OTA_START, byteArrayOf(1))
        }

        for (total in listOf(44, 3371)) {
            val payload = be32(total)
            assertTrue(protocol.requestOta(DeviceControlProtocol.Command.OTA_BEGIN, payload))
            assertArrayEquals(be32(total), payload)
            assertTrue(borrowed!!.all { it == 0.toByte() })
            assertEquals(20, sent.last().size)
            assertEquals(DeviceControlProtocol.Command.OTA_BEGIN.wire,
                ByteBuffer.wrap(sent.last()).getInt(4))
            assertEquals(total, ByteBuffer.wrap(sent.last()).getInt(16))
            assertFalse(protocol.requestOta(DeviceControlProtocol.Command.OTA_STATUS))
            protocol.receive(response(sent.last(), flags = -1, volume = -1,
                persona = -1, turn = -1, runtimeError = -1))
        }

        for (size in listOf(1, 32)) {
            val payload = ByteArray(size) { (it + 1).toByte() }
            val original = payload.copyOf()
            assertTrue(protocol.requestOta(DeviceControlProtocol.Command.OTA_APPEND, payload))
            assertArrayEquals(original, payload)
            assertEquals(16 + size, sent.last().size)
            assertArrayEquals(original, sent.last().copyOfRange(16, 16 + size))
            assertTrue(borrowed!!.all { it == 0.toByte() })
            protocol.receive(response(sent.last(), flags = -1, volume = -1,
                persona = -1, turn = -1, runtimeError = -1))
        }
        for (command in listOf(DeviceControlProtocol.Command.OTA_START,
                               DeviceControlProtocol.Command.OTA_STATUS,
                               DeviceControlProtocol.Command.OTA_CANCEL)) {
            assertTrue(protocol.requestOta(command))
            assertEquals(16, sent.last().size)
            protocol.receive(response(sent.last(), flags = -1, volume = -1,
                persona = -1, turn = -1, runtimeError = -1))
        }
    }

    @Test fun otaRepliesUseOnlyOtaStatusAndSupportFragmentsAndUnsignedValues() {
        for (fragment in listOf(1, 17, 39, 40)) {
            val sent = mutableListOf<ByteArray>()
            val replies = mutableListOf<Pair<DeviceControlProtocol.Command, DeviceControlProtocol.Snapshot>>()
            val protocol = DeviceControlProtocol(ByteArray(32) { 42 },
                { sent += it.copyOf() }, { command, snapshot -> replies += command to snapshot })
            protocol.start(); protocol.receive(response(sent.last()))
            assertTrue(protocol.requestOta(DeviceControlProtocol.Command.OTA_STATUS))
            val reply = response(sent.last(), error = -16, flags = -2, volume = -1,
                persona = 100, turn = Int.MIN_VALUE, runtimeError = -5)
            reply.toList().chunked(fragment).forEach { protocol.receive(it.toByteArray()) }
            assertEquals(DeviceControlProtocol.Command.OTA_STATUS, replies.last().first)
            val snapshot = replies.last().second
            assertEquals(-16, snapshot.error)
            assertFalse(snapshot.ready || snapshot.busy || snapshot.otaSupported)
            assertNull(snapshot.volume)
            assertNull(snapshot.firmwareInfo)
            assertEquals(4294967294L, snapshot.otaStatus!!.state)
            assertNull(snapshot.otaStatus!!.phase)
            assertEquals(100L, snapshot.otaStatus!!.progress)
            assertEquals(2147483648L, snapshot.otaStatus!!.total)
            assertEquals(-5, snapshot.otaStatus!!.result)

            assertTrue(protocol.requestOta(DeviceControlProtocol.Command.OTA_BEGIN, be32(44)))
            protocol.receive(response(sent.last(), flags = -1, volume = -1,
                persona = -1, turn = -1, runtimeError = -1))
            // Unknown state prevents this signed -1 from claiming runtime failure.
            assertEquals(DeviceControlProtocol.OtaStatus(null, null, null, null, -1),
                replies.last().second.otaStatus)
            assertEquals(0, replies.last().second.error)
        }
    }

    @Test fun malformedOtaReplyClosesWithoutReplay() {
        for (bad in 0..2) {
            val sent = mutableListOf<ByteArray>()
            val protocol = DeviceControlProtocol(ByteArray(32) { 42 },
                { sent += it.copyOf() }, { _, _ -> })
            protocol.start(); protocol.receive(response(sent.last()))
            protocol.requestOta(DeviceControlProtocol.Command.OTA_STATUS)
            val reply = response(sent.last(), flags = -1, volume = -1,
                persona = -1, turn = -1, runtimeError = -1)
            when (bad) {
                0 -> ByteBuffer.wrap(reply).putInt(8, 99)
                1 -> ByteBuffer.wrap(reply).putInt(12, 20)
                2 -> ByteBuffer.wrap(reply).putInt(16, 1)
            }
            assertThrows(IllegalStateException::class.java) { protocol.receive(reply) }
            assertTrue(protocol.closed)
            assertEquals(2, sent.size)
        }
    }

    @Test fun wifiStatusPreservesUnknownOfflineAndReady() {
        val sent = mutableListOf<ByteArray>()
        val states = mutableListOf<DeviceControlProtocol.Snapshot>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, s -> states += s })
        protocol.start(); protocol.receive(response(sent.last()))
        for ((flags, expected) in listOf(1 to null, (1 or 1024) to false, (1 or 1024 or 2048) to true)) {
            protocol.request(DeviceControlProtocol.Command.STATUS)
            protocol.receive(response(sent.last(), flags = flags))
            assertEquals(expected, states.last().wifiReady)
        }
        protocol.request(DeviceControlProtocol.Command.STATUS)
        assertThrows(IllegalStateException::class.java) {
            protocol.receive(response(sent.last(), flags = 1 or 2048))
        }
        assertTrue(protocol.closed)
    }

    @Test fun wrongSequenceMalformedStatusAndReplayedAuthCloseConnection() {
        for (bad in 0..5) {
            val sent = mutableListOf<ByteArray>()
            val protocol = DeviceControlProtocol(ByteArray(32) { 1 }, { sent += it.copyOf() }, { _, _ -> })
            protocol.start()
            val auth = response(sent.last())
            protocol.receive(auth)
            protocol.request(DeviceControlProtocol.Command.STATUS)
            val frame = response(sent.last())
            when (bad) {
                0 -> ByteBuffer.wrap(frame).putInt(8, 7)
                1 -> ByteBuffer.wrap(frame).putInt(20, 32)
                2 -> ByteBuffer.wrap(frame).putInt(24, 50) // No known-volume flag.
                3 -> ByteBuffer.wrap(frame).putInt(12, 28)
                4 -> ByteBuffer.wrap(frame).putInt(16, 1)
                5 -> auth.copyInto(frame)
            }
            assertThrows(IllegalStateException::class.java) { protocol.receive(frame) }
            assertTrue(protocol.closed)
        }
    }

    @Test fun timeoutAndSendFailureNeverReplayMutation() {
        var now = 0L
        val sent = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 1 }, { sent += it.copyOf() }, { _, _ -> }, { now })
        protocol.start(); protocol.receive(response(sent.last()))
        protocol.request(DeviceControlProtocol.Command.PERSONA, 4)
        now = 10000
        assertThrows(DeviceControlProtocol.ControlTimeout::class.java) { protocol.tick() }
        assertTrue(protocol.closed)
        assertEquals(2, sent.size)
        var borrowed: ByteArray? = null
        val failed = DeviceControlProtocol(ByteArray(32) { 1 }, { borrowed = it; error("send failed") }, { _, _ -> })
        assertThrows(IllegalStateException::class.java) { failed.start() }
        assertTrue(failed.closed && borrowed!!.all { it == 0.toByte() })
    }

    @Test fun eyeApplyAllowsInstallationThenRestoresOrdinaryCommandDeadline() {
        var now = 0L
        val sent = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 1 },
            { sent += it.copyOf() }, { _, _ -> }, { now })
        protocol.start(); protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(5).putInt(44).array())
        protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(44))
        protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
        now = 10_000
        protocol.tick()
        assertFalse(protocol.closed)
        assertFalse(protocol.request(DeviceControlProtocol.Command.STATUS))
        now = 29_999
        protocol.receive(response(sent.last()))
        assertFalse(protocol.closed)
        assertTrue(protocol.request(DeviceControlProtocol.Command.STATUS))
        now += 10_000
        assertThrows(DeviceControlProtocol.ControlTimeout::class.java) { protocol.tick() }
        assertTrue(protocol.closed)
        assertEquals(5, sent.size) // No replay of the completed installation.
    }

    @Test fun eyeApplyStillHasAHardDeadlineWithoutReplay() {
        var now = 0L
        val sent = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 1 },
            { sent += it.copyOf() }, { _, _ -> }, { now })
        protocol.start(); protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(5).putInt(44).array())
        protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(44))
        protocol.receive(response(sent.last()))
        protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
        now = 30_000
        assertThrows(DeviceControlProtocol.ControlTimeout::class.java) { protocol.tick() }
        assertTrue(protocol.closed)
        assertEquals(4, sent.size)
    }

    @Test fun onlyAnAcceptedEyeTransactionExtendsApplyDeadline() {
        for (scenario in 0..4) {
            var now = 0L
            val sent = mutableListOf<ByteArray>()
            val protocol = DeviceControlProtocol(ByteArray(32) { 1 },
                { sent += it.copyOf() }, { _, _ -> }, { now })
            protocol.start(); protocol.receive(response(sent.last()))
            val kind = if (scenario == 0) 1 else 5
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(kind).putInt(44).array())
            protocol.receive(response(sent.last(), error = if (scenario == 1) -16 else 0))
            if (scenario >= 2) {
                if (scenario >= 3) {
                    protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(44))
                    protocol.receive(response(sent.last()))
                }
                val command = if (scenario == 2) DeviceControlProtocol.Command.CONFIG_CANCEL
                    else DeviceControlProtocol.Command.CONFIG_APPLY
                protocol.requestPayload(command, ByteArray(0))
                protocol.receive(response(sent.last(), error = if (scenario == 4) -110 else 0))
            }
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
            now = 10_000
            assertThrows(DeviceControlProtocol.ControlTimeout::class.java) { protocol.tick() }
            assertTrue(protocol.closed)
        }
    }

    @Test fun incompleteEyeApplyRetainsItsBudgetButAppendDoesNotExtendItsDeadline() {
        for (apply in listOf(false, true)) {
            var now = 0L
            val sent = mutableListOf<ByteArray>()
            val protocol = DeviceControlProtocol(ByteArray(32) { 1 },
                { sent += it.copyOf() }, { _, _ -> }, { now })
            protocol.start(); protocol.receive(response(sent.last()))
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(5).putInt(44).array())
            protocol.receive(response(sent.last()))
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
            protocol.receive(response(sent.last(), error = -61))
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, ByteArray(44))
            if (apply) {
                protocol.receive(response(sent.last()))
                protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_APPLY, ByteArray(0))
                now = 10_000
                protocol.tick()
                assertFalse(protocol.closed)
                protocol.receive(response(sent.last()))
                protocol.close()
            } else {
                now = 10_000
                assertThrows(DeviceControlProtocol.ControlTimeout::class.java) { protocol.tick() }
                assertTrue(protocol.closed)
            }
        }
    }
}
