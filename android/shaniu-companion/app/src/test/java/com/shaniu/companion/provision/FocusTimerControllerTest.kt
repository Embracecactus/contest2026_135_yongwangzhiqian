package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

class FocusTimerControllerTest {
    private class Fixture {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val controller: FocusTimerController
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
            controller = FocusTimerController(session, { 11L }) {}
        }
        fun reply(error: Int = 0, bytes: ByteArray? = null) {
            val cmd = sent.last().first
            events.result(cmd, ok.copy(error = error, configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(32, it) }))
        }
        fun head(state: Int, revision: Long) = ByteBuffer.allocate(16).put("FOS1".toByteArray()).putInt(state).putLong(revision).array()
        fun read(state: Int, revision: Long, remain: Long) {
            reply(bytes = head(state, revision))
            reply(bytes = ByteBuffer.allocate(16).putLong(remain).putLong(60000).array())
            reply(bytes = head(state, revision))
        }
    }
    @Test fun acceptedStartWaitsForDeviceReadback() {
        val f = Fixture()
        assertTrue(f.controller.refresh()); f.read(0, 0, 0)
        assertTrue(f.controller.act(1, 60000))
        f.reply(); val record = f.sent.last().second
        assertEquals("FOC1", String(record.copyOfRange(0, 4)))
        assertEquals(60000L, ByteBuffer.wrap(record, 24, 8).long)
        f.reply(); f.reply()
        assertTrue(f.controller.current().busy)
        assertNull(f.controller.current().snapshot)
        f.read(1, 1, 60000)
        assertFalse(f.controller.current().busy)
        assertEquals(1, f.controller.current().snapshot?.state)
    }
    @Test fun changedRevisionCannotProduceMixedSnapshot() {
        val f = Fixture(); assertTrue(f.controller.refresh())
        f.reply(bytes = f.head(1, 1))
        f.reply(bytes = ByteBuffer.allocate(16).putLong(50000).putLong(60000).array())
        f.reply(bytes = f.head(2, 2))
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.act(2))
    }
    @Test fun disconnectAndLateAckNeverReplayMutation() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        f.controller.act(1, 60000)
        f.events.closed("lost")
        val count = f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_BEGIN, f.ok)
        assertEquals(count, f.sent.size)
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.act(1, 60000))
    }
    @Test fun unsupportedDeviceDoesNotStartLocalTimer() {
        val f = Fixture(); f.controller.refresh(); f.reply(-95)
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.current().busy)
        assertFalse(f.controller.act(1, 60000))
    }
    @Test fun rejectedBeginCannotCancelAnotherEditorsTransaction() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(7).putInt(32).array()))
        assertFalse(f.controller.act(1, 60000))
        f.reply()
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, byteArrayOf(1)))
        assertFalse(f.sent.any { it.first == DeviceControlProtocol.Command.CONFIG_CANCEL })
    }

}
