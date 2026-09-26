package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

class NfcBindingControllerTest {
    private class Fixture {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val controller: NfcBindingController
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
            controller = NfcBindingController(session) {}
        }
        fun reply(error: Int = 0, bytes: ByteArray? = null) {
            val cmd = sent.last().first
            events.result(cmd, ok.copy(error = error, configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(112, it) }))
        }
        fun wire(phase: Int = 4, operation: Long = 3, revision: Long = 1, floor: Long = 8): ByteArray =
            ByteBuffer.allocate(112).put("NCS1".toByteArray()).putInt(phase).putInt(0).putInt(0)
                .putLong(operation).putLong(revision).putLong(floor).putLong(0).putLong(60000).array()
        fun read(bytes: ByteArray = wire()) {
            for (offset in 0..96 step 16) reply(bytes = bytes.copyOfRange(offset, offset + 16))
            for (offset in 0..32 step 16) reply(bytes = bytes.copyOfRange(offset, offset + 16))
        }
    }
    @Test fun acceptedEnrollmentRemainsPendingUntilDeviceConfirms() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.read()
        assertTrue(f.controller.act(2, 2, 120000))
        f.reply(); val first = f.sent.last().second
        assertEquals(32, first.size)
        f.reply(); assertEquals(8, f.sent.last().second.size)
        val record = first + f.sent.last().second
        assertEquals("NCF1", String(record.copyOfRange(0, 4)))
        assertEquals(2, ByteBuffer.wrap(record, 8, 4).int)
        assertEquals(1L, ByteBuffer.wrap(record, 16, 8).long)
        assertEquals(9L, ByteBuffer.wrap(record, 24, 8).long)
        assertEquals(120000L, ByteBuffer.wrap(record, 32, 8).long)
        f.reply(); f.reply(); f.read(f.wire(1, 9, 1, 9))
        assertEquals(1, f.controller.current().snapshot?.phase)
        assertFalse(f.controller.act(2, 0, 60000))
        assertFalse(f.controller.current().message.contains("已保存"))
    }
    @Test fun verifiesOperationAndRevisionAcrossAllChunks() {
        val f = Fixture(); f.controller.refresh()
        val bytes=f.wire()
        for(offset in 0..96 step 16) f.reply(bytes=bytes.copyOfRange(offset,offset+16))
        f.reply(bytes=bytes.copyOfRange(0,16))
        f.reply(bytes=f.wire(operation=4).copyOfRange(16,32))
        f.reply(bytes=bytes.copyOfRange(32,48))
        assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.act(3,0))
    }
    @Test fun disconnectDropsMutationAndLateAck() {
        val f=Fixture(); f.controller.refresh(); f.read(); f.controller.act(2,0,60000)
        f.events.closed("lost");val count=f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_BEGIN,f.ok)
        assertEquals(count,f.sent.size);assertNull(f.controller.current().snapshot)
    }
    @Test fun rejectsUnrepresentableCounterAndUnknownOutcome() {
        val f=Fixture();f.controller.refresh();f.read(f.wire(floor=Long.MIN_VALUE))
        assertNull(f.controller.current().snapshot);assertFalse(f.controller.act(1))
        f.controller.refresh();val bytes=f.wire(7);ByteBuffer.wrap(bytes).putInt(8,-115);f.read(bytes)
        assertEquals(7,f.controller.current().snapshot?.phase);assertFalse(f.controller.act(2,0,60000))
    }
    @Test fun cancelNamesCurrentJobWithoutAllocatingNewId() {
        val f=Fixture();f.controller.refresh();f.read(f.wire(2,3,1,8))
        assertTrue(f.controller.act(4));f.reply();val bytes=f.sent.last().second
        assertEquals(4,ByteBuffer.wrap(bytes,4,4).int)
        assertEquals(3L,ByteBuffer.wrap(bytes,24,8).long)
        assertEquals(0L,ByteBuffer.wrap(bytes,16,8).long)
    }
    @Test fun unsupportedFirmwareStaysReadOnly() {
        val f=Fixture();f.controller.refresh();f.reply(-95)
        assertNull(f.controller.current().snapshot);assertFalse(f.controller.act(1))
        assertTrue(f.controller.current().message.contains("不支持"))
    }
    @Test fun rejectedBeginDoesNotCancelAnotherTransaction() {
        val f=Fixture();f.controller.refresh();f.read()
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(7).putInt(32).array()))
        assertFalse(f.controller.act(2,0,60000));f.reply()
        assertFalse(f.sent.any { it.first==DeviceControlProtocol.Command.CONFIG_CANCEL })
    }
    @Test fun closeNeverClaimsAcceptedDeviceJobCanceled() {
        val f=Fixture();f.controller.refresh();f.read();f.controller.act(2,0,60000)
        f.reply();f.reply();f.reply();f.reply()
        val count=f.sent.count { it.first==DeviceControlProtocol.Command.CONFIG_BEGIN }
        f.controller.close()
        assertEquals(count,f.sent.count { it.first==DeviceControlProtocol.Command.CONFIG_BEGIN })
        assertFalse(f.sent.any { it.first==DeviceControlProtocol.Command.CONFIG_CANCEL })
    }
    @Test fun supersedingJobCannotConfirmOwnOperation() {
        val f=Fixture();f.controller.refresh();f.read();assertTrue(f.controller.act(2,0,60000))
        f.reply();f.reply();f.reply();f.reply();f.read(f.wire(4,10,2,10))
        assertNull(f.controller.current().snapshot)
        assertTrue(f.controller.current().message.contains("替换"))
    }

}
