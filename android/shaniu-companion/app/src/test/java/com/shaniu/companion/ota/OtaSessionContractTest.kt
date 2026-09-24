// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import com.shaniu.companion.provision.DeviceControlProtocol
import com.shaniu.companion.provision.DeviceControlSession
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

/** OTA-01 L2: real upload, session and protocol; only authenticated transport I/O
 * and scheduling are external fixtures. This is not a TLS/BLE authentication test.
 */
class OtaSessionContractTest {
    private class Peer(val events: DeviceControlSession.Events) : DeviceControlSession.Transport {
        val wire = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 1 }, { wire += it.copyOf() },
            { command, snapshot ->
                if (command == DeviceControlProtocol.Command.AUTH) {
                    // The production transport reports its first authenticated status.
                } else events.result(command, snapshot)
            }, { 1L })
        init { protocol.start() }
        override fun request(command: DeviceControlProtocol.Command, value: Int, accepted: (Boolean) -> Unit) {
            accepted(protocol.request(command, value))
        }
        override fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit) {
            accepted(protocol.requestOta(command, payload))
        }
        override fun requestPayload(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit) {
            accepted(protocol.requestPayload(command, payload))
        }
        override fun close() { protocol.close() }
        fun ack(frame: ByteArray = wire.last()): ByteArray {
            // SDC1 public framing: response header + six BE32 result fields.
            val source = ByteBuffer.wrap(frame)
            assertEquals(0x53444331, source.int)
            val command = source.int; val sequence = source.int
            val result = ByteBuffer.allocate(40).putInt(0x53444331)
                .putInt(command or Int.MIN_VALUE).putInt(sequence).putInt(24)
                .putInt(0).putInt(0).putInt(-1).putInt(-1).putInt(-1).putInt(0).array()
            // Legal fragmentation must not alter dispatch.
            result.asList().chunked(3).forEach { protocol.receive(it.toByteArray()) }
            return result
        }
    }
    private class Fixture {
        lateinit var peer: Peer
        val session = DeviceControlSession({ 1L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel {
            override fun cancel() = Unit
        } })
        val upload: OtaControlUpload
        init {
            session.setForeground(true)
            session.connect(object : DeviceControlSession.Factory {
                override fun open(events: DeviceControlSession.Events): DeviceControlSession.Transport =
                    Peer(events).also { peer = it }
            })
            peer.ack()
            assertTrue(peer.protocol.request(DeviceControlProtocol.Command.STATUS))
            peer.ack()
            assertTrue(session.current().authenticated)
            upload = OtaControlUpload(ByteArray(44) { 42 }, session::requestOta)
            session.observeResults { command, result ->
                if (command in listOf(DeviceControlProtocol.Command.OTA_BEGIN,
                    DeviceControlProtocol.Command.OTA_APPEND, DeviceControlProtocol.Command.OTA_START,
                    DeviceControlProtocol.Command.OTA_CANCEL)) upload.response(command, result)
            }
        }
    }
    @Test fun OTA_01_cancelIsPendingUntilProtocolAcknowledges() {
        val f = Fixture()
        assertTrue(f.upload.start()); f.upload.cancel()
        assertEquals(OtaControlUpload.State.WAITING, f.upload.state)
        f.peer.ack()
        assertEquals(14, ByteBuffer.wrap(f.peer.wire.last()).getInt(4))
        assertEquals(OtaControlUpload.State.WAITING, f.upload.state)
        f.peer.ack()
        assertEquals(OtaControlUpload.State.CANCELED, f.upload.state)
        assertEquals(0, f.upload.totalBytes)
        f.session.disconnect()
    }
    @Test fun OTA_01_lateWireAckCannotRestartCanceledUpload() {
        val f = Fixture()
        assertTrue(f.upload.start()); f.upload.cancel()
        val oldAck = f.peer.ack(); f.peer.ack()
        val count = f.peer.wire.size
        assertThrows(IllegalStateException::class.java) { f.peer.protocol.receive(oldAck) }
        assertTrue(f.peer.protocol.closed)
        assertEquals(OtaControlUpload.State.CANCELED, f.upload.state)
        assertEquals(count, f.peer.wire.size)
        f.session.disconnect()
    }
    @Test fun OTA_01_lateTransportCallbackIsFilteredBySession() {
        val f = Fixture()
        assertTrue(f.upload.start()); f.upload.cancel(); f.peer.ack(); f.peer.ack()
        val count = f.peer.wire.size
        f.peer.events.result(DeviceControlProtocol.Command.OTA_BEGIN,
            DeviceControlProtocol.Snapshot(0, false, false, null, null, null, null))
        assertEquals(OtaControlUpload.State.CANCELED, f.upload.state)
        assertEquals(count, f.peer.wire.size)
        f.session.disconnect()
    }
}
