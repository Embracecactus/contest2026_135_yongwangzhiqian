package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

class ExpressionTrialControllerTest {
    private class Fixture {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val controller: ExpressionTrialController
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
            controller = ExpressionTrialController(session, { 11L }) {}
        }
        fun reply(error: Int = 0, bytes: ByteArray? = null) {
            val cmd = sent.last().first
            events.result(cmd, ok.copy(error = error, configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(32, it) }))
        }
        fun head(state: Int, id: Long) = ByteBuffer.allocate(16).put("ETS1".toByteArray()).putInt(state).putInt(id.toInt()).putInt(0).array()
        fun read(state: Int, revision: Long, remain: Long, operation: Long = 0) {
            reply(bytes = head(state, revision))
            reply(bytes = ByteBuffer.allocate(16).putLong(remain).putLong(operation).array())
            reply(bytes = head(state, revision))
        }
    }
    @Test fun acceptedTrialUsesGoldenRecordAndDeviceReadback() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.read(0, 0, 0)
        assertTrue(f.controller.act(1, 15000, 2)); f.reply()
        val expected = byteArrayOf(69,84,67,49, 0,0,0,1, 0,0,0,0, 0,0,58,-104,
            0,0,0,0,0,0,0,11, 0,0,0,2, 0,0,0,0)
        assertArrayEquals(expected, f.sent.last().second)
        f.reply(); f.reply()
        assertTrue(f.controller.current().busy); assertNull(f.controller.current().snapshot)
        f.read(1, 1, 15000, 11)
        assertEquals(1, f.controller.current().snapshot?.state)
        assertTrue(f.controller.current().message.contains("尚未显示"))
    }
    @Test fun cancellationAcceptanceIsNotRestoredCompletion() {
        val f = Fixture(); f.controller.refresh(); f.read(3, 7, 9000, 5)
        assertTrue(f.controller.act(2)); f.reply()
        assertEquals(7, ByteBuffer.wrap(f.sent.last().second, 8, 4).int)
        f.reply(); f.reply(); f.read(4, 7, 0, 11)
        assertTrue(f.controller.current().message.contains("等待设备恢复"))
        assertTrue(f.controller.refresh()); f.read(7, 7, 0, 11)
        assertTrue(f.controller.current().message.contains("确认取消"))
    }
    @Test fun changedHeaderCannotProduceMixedSnapshot() {
        val f = Fixture(); f.controller.refresh()
        f.reply(bytes = f.head(3, 1))
        f.reply(bytes = ByteBuffer.allocate(16).putLong(15000).putLong(1).array())
        f.reply(bytes = f.head(3, 2))
        assertNull(f.controller.current().snapshot); assertFalse(f.controller.act(2))
    }
    @Test fun disconnectAndLateAckNeverReplayMutation() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        f.controller.act(1, 15000, 2); f.events.closed("lost")
        val count = f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_BEGIN, f.ok)
        assertEquals(count, f.sent.size); assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.act(1, 15000, 2))
    }
    @Test fun unsupportedDeviceDoesNotPretendToTrial() {
        for (error in listOf(-95, -138, -38)) {
            val f = Fixture(); f.controller.refresh(); f.reply(error)
            assertNull(f.controller.current().snapshot); assertFalse(f.controller.current().busy)
            assertFalse(f.controller.act(1, 15000, 2))
            assertTrue(f.controller.current().message.contains("不支持"))
        }
    }
    @Test fun rejectedBeginCannotCancelAnotherEditorsTransaction() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
            ByteBuffer.allocate(8).putInt(7).putInt(32).array()))
        assertFalse(f.controller.act(1, 15000, 2)); f.reply()
        assertTrue(f.session.requestPayload(DeviceControlProtocol.Command.CONFIG_APPEND, byteArrayOf(1)))
        assertFalse(f.sent.any { it.first == DeviceControlProtocol.Command.CONFIG_CANCEL })
    }
    @Test fun closingActiveTrialDoesNotSendRemoteCancel() {
        val f = Fixture(); f.controller.refresh(); f.read(3, 1, 9000, 5)
        val count = f.sent.size; f.controller.close()
        assertEquals(count, f.sent.size)
    }
    @Test fun invalidDurationAndMismatchedReceiptCannotClaimSuccess() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        assertFalse(f.controller.act(1, 0, 2)); assertFalse(f.controller.act(1, 4294967296L, 2))
        assertFalse(f.controller.act(1, 15000, 10))
        assertTrue(f.controller.act(1, 15000, 2)); f.reply(); f.reply(); f.reply()
        f.read(3, 2, -1, 99)
        assertNull(f.controller.current().snapshot?.remainingMs)
        assertTrue(f.controller.current().message.contains("未确认本次操作"))
    }
    @Test fun realProtocolAcceptsPackTrialLengthWithoutWeakeningOtherKinds() {
        val sent = mutableListOf<ByteArray>()
        val protocol = DeviceControlProtocol(ByteArray(32) { 42 }, { sent += it.copyOf() }, { _, _ -> })
        fun reply() {
            val frame = ByteBuffer.wrap(sent.last())
            protocol.receive(ByteBuffer.allocate(40).putInt(0x53444331)
                .putInt(frame.getInt(4) or Int.MIN_VALUE).putInt(frame.getInt(8)).putInt(24)
                .putInt(0).putInt(0).putInt(-1).putInt(-1).putInt(-1).putInt(0).array())
        }
        protocol.start(); reply()
        for (length in listOf(32, 72)) {
            assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(11).putInt(length).array()))
            reply()
        }
        for (length in listOf(0, 31, 33, 71, 73, 96)) {
            assertThrows(IllegalArgumentException::class.java) {
                protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                    ByteBuffer.allocate(8).putInt(11).putInt(length).array())
            }
        }
        assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,
                ByteBuffer.allocate(8).putInt(10).putInt(72).array())
        }
    }

    @Test fun installedPackUsesGoldenEtc2AndThreeBoundedChunks() {
        val f = Fixture(); f.controller.refresh(); f.read(0, 0, 0)
        assertTrue(f.controller.act(1, 15000, 2, "green.bkep"))
        assertArrayEquals(byteArrayOf(0,0,0,11,0,0,0,72), f.sent.last().second)
        f.reply()
        val chunks = mutableListOf<ByteArray>()
        repeat(3) { chunks += f.sent.last().second; f.reply() }
        assertEquals(listOf(32,32,8), chunks.map { it.size })
        val expected = byteArrayOf(69,84,67,50, 0,0,0,1, 0,0,0,0, 0,0,58,-104,
            0,0,0,0,0,0,0,11, 0,0,0,2, 0,0,0,0) + "green.bkep".toByteArray() + ByteArray(30)
        assertArrayEquals(expected, chunks.reduce { a,b -> a+b })
        assertEquals(DeviceControlProtocol.Command.CONFIG_APPLY, f.sent.last().first)
        f.reply(); f.read(1, 1, 15000, 11)
        assertTrue(f.controller.current().message.contains("尚未显示"))
    }
    @Test fun invalidInstalledNameNeverAcquiresStaging() {
        val f = Fixture(); f.controller.refresh(); f.read(0,0,0)
        val count = f.sent.size
        for (name in listOf("", "../green.bkep", "Green.bkep", "1green.bkep", "green.png", "a".repeat(35)+".bkep", "a\u0000.bkep")) {
            assertFalse(f.controller.act(1, 15000, 2, name))
        }
        assertEquals(count, f.sent.size)
        assertTrue(f.controller.act(1, 15000, 2, "a".repeat(34)+".bkep"))
    }
    @Test fun failedPackAppendCancelsOwnedStageWithoutApply() {
        val f = Fixture(); f.controller.refresh(); f.read(0,0,0)
        assertTrue(f.controller.act(1, 15000, 2, "green.bkep")); f.reply(); f.reply(-5)
        assertEquals(DeviceControlProtocol.Command.CONFIG_CANCEL, f.sent.last().first)
        assertFalse(f.sent.any { it.first == DeviceControlProtocol.Command.CONFIG_APPLY })
        assertNull(f.controller.current().snapshot)
    }
    @Test fun disconnectedPackAppendCannotReviveAndCancelRemainsEtc1() {
        val f = Fixture(); f.controller.refresh(); f.read(0,0,0)
        f.controller.act(1, 15000, 2, "green.bkep"); f.reply()
        f.events.closed("lost"); val count = f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_APPEND, f.ok)
        assertEquals(count, f.sent.size); assertNull(f.controller.current().snapshot)
        val cancel = Fixture(); cancel.controller.refresh(); cancel.read(3,7,9000,5)
        assertFalse(cancel.controller.act(2, packFilename = "green.bkep"))
        assertTrue(cancel.controller.act(2)); cancel.reply()
        assertEquals(32, cancel.sent.last().second.size)
        assertEquals("ETC1", String(cancel.sent.last().second,0,4))
    }

    @Test fun unsupportedPackFormatNeverFallsBackToBuiltinTrial() {
        val f = Fixture(); f.controller.refresh(); f.read(0,0,0)
        assertTrue(f.controller.act(1, 15000, 2, "green.bkep")); f.reply(-90)
        assertNull(f.controller.current().snapshot); assertFalse(f.controller.current().busy)
        val begins = f.sent.filter { it.first == DeviceControlProtocol.Command.CONFIG_BEGIN }
        assertEquals(1, begins.size); assertEquals(72, ByteBuffer.wrap(begins.single().second).getInt(4))
        assertFalse(f.sent.any { it.first in listOf(DeviceControlProtocol.Command.CONFIG_APPEND, DeviceControlProtocol.Command.CONFIG_APPLY) })
    }

}
