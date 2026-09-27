// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

class DefaultSelectionControllerTest {
    private fun snapshot(state: Int = 0, id: Int = 0, flags: Int = 0,
                         nonce: Int = 0, revision: Long = 0, error: Int = 0,
                         release: Int = 0): ByteArray = ByteBuffer.allocate(128).apply {
        put("ESS1".toByteArray()); putInt(state); put(ByteArray(16) { 7 }); putInt(id)
        putInt(error); putInt(release); putInt(flags); putLong(revision); putLong(0)
        put(ByteArray(16) { nonce.toByte() })
        if (flags and 1 != 0) put("green.bkep".toByteArray())
        position(112); putLong(1); putLong(0)
    }.array()
    private class Fixture {
        lateinit var events: DeviceControlSession.Events
        val sent = mutableListOf<Pair<DeviceControlProtocol.Command, ByteArray>>()
        val session = DeviceControlSession({ 1000L }, { it() }, { _, _ -> object : DeviceControlSession.Cancel { override fun cancel() {} } })
        val ok = DeviceControlProtocol.Snapshot(0, true, false, 50, 0, 0, 0)
        val controller: DefaultSelectionController
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
            controller = DefaultSelectionController(session, { ByteArray(16) { 11 } }) {}
        }
        fun reply(error: Int = 0, bytes: ByteArray? = null) {
            events.result(sent.last().first, ok.copy(error = error,
                configChunk = bytes?.let { DeviceControlProtocol.ConfigChunk(128, it) }))
        }
        fun read(bytes: ByteArray) {
            for (offset in 0..112 step 16) {
                val query = sent.last().second
                assertEquals(20, query.size)
                assertEquals((17 shl 16) or offset, ByteBuffer.wrap(query).int)
                reply(bytes = bytes.copyOfRange(offset, offset + 16))
            }
            assertEquals((17 shl 16) or 112, ByteBuffer.wrap(sent.last().second).int)
            reply(bytes = bytes.copyOfRange(112, 128))
        }
        fun submit() { repeat(5) { reply() } } // BEGIN, three APPENDs, APPLY
    }
    @Test fun realProtocolRequiresAuthenticatedNonceReadAndExactDefaultLength() {
        val sent=mutableListOf<ByteArray>()
        val protocol=DeviceControlProtocol(ByteArray(32){42}, { sent+=it.copyOf() }, { _,_-> })
        fun response(total: Int=0): ByteArray = ByteBuffer.allocate(40).apply {
            putInt(0x53444331);putInt(ByteBuffer.wrap(sent.last()).getInt(4) or Int.MIN_VALUE)
            putInt(ByteBuffer.wrap(sent.last()).getInt(8));putInt(24);putInt(0);putInt(total)
            repeat(3){putInt(-1)};putInt(0)
        }.array()
        protocol.start();protocol.receive(response())
        val read=ByteBuffer.allocate(20).putInt(17 shl 16).put(ByteArray(16){7}).array()
        assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,read))
        protocol.receive(response(128))
        for(offset in listOf(1,128,65535)) {
            val bad=read.copyOf();ByteBuffer.wrap(bad).putInt((17 shl 16) or offset)
            assertThrows(IllegalArgumentException::class.java) { protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,bad) }
        }
        assertThrows(IllegalArgumentException::class.java) { protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,read.copyOf(4)) }
        assertThrows(IllegalArgumentException::class.java) { protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,read.copyOf().apply { fill(0,4,20) }) }
        for(size in listOf(32,95,97)) assertThrows(IllegalArgumentException::class.java) {
            protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,ByteBuffer.allocate(8).putInt(17).putInt(size).array())
        }
        assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN,ByteBuffer.allocate(8).putInt(17).putInt(96).array()))
        protocol.receive(response())
        assertTrue(protocol.requestPayload(DeviceControlProtocol.Command.CONFIG_READ,read))
        assertThrows(IllegalStateException::class.java) { protocol.receive(response(127)) }
        assertTrue(protocol.closed)
    }
    @Test fun goldenRecordPreservesUnsignedRevisionAndCanonicalName() {
        val data = DefaultSelectionController.encode(1, ByteArray(16) { 7 }, ByteArray(16) { 11 }, 0xffffffffL, ULong.MAX_VALUE, "green.bkep")
        assertEquals(96, data.size)
        assertArrayEquals(byteArrayOf(69,83,67,49,0,0,0,1), data.copyOfRange(0,8))
        assertTrue(data.copyOfRange(40,44).all { it == (-1).toByte() })
        assertTrue(data.copyOfRange(44,48).all { it == 0.toByte() })
        assertTrue(data.copyOfRange(48,56).all { it == (-1).toByte() })
        assertEquals("green.bkep", String(data.copyOfRange(56,66)))
        assertTrue(data.copyOfRange(66,96).all { it == 0.toByte() })
        for (name in listOf("../green.bkep", "Green.bkep", "green.png", "a".repeat(40))) {
            assertThrows(IllegalArgumentException::class.java) { DefaultSelectionController.encode(1, ByteArray(16){7}, ByteArray(16){11}, 0, 0uL, name) }
        }
    }
    @Test fun decoderRejectsInconsistentSuccessAndReservedFields() {
        assertEquals(ULong.MAX_VALUE, DefaultSelectionController.decode(snapshot(6,1,7,11,-1)).revision)
        for (bad in listOf(snapshot(6,1,0), snapshot(1,1,4), snapshot(1,1,3,11,1), snapshot(4,1,1,0,2), snapshot(0,0,1), snapshot(7,1,7,11,1,-125))) {
            assertThrows(IllegalArgumentException::class.java) { DefaultSelectionController.decode(bad) }
        }
        val bad = snapshot(); bad[127] = 1
        assertThrows(IllegalArgumentException::class.java) { DefaultSelectionController.decode(bad) }
    }
    @Test fun explicitRefreshThenSaveUsesThreeChunksAndDoesNotClaimAckAsSaved() {
        val f = Fixture(); assertTrue(f.controller.refresh()); f.read(snapshot())
        assertFalse(f.controller.act(1, "green.bkep")) // No known durable revision yet.
        assertTrue(f.controller.act(2)); f.submit(); f.read(snapshot(1,1,8,11))
        assertTrue(f.controller.current().message.contains("刷新待完成"))
        assertFalse(f.controller.current().message.contains("保存"))
        f.controller.refresh(); f.read(snapshot(6,1,9,11,1))
        assertTrue(f.controller.act(1, "green.bkep")); f.reply()
        val chunks = mutableListOf<ByteArray>()
        repeat(3) { chunks += f.sent.last().second; assertEquals(32, chunks.last().size); f.reply() }
        assertEquals(96, chunks.sumOf { it.size }); f.reply()
        assertTrue(f.controller.current().busy); assertNull(f.controller.current().snapshot)
        f.read(snapshot(1,2,0,11)); assertFalse(f.controller.current().snapshot!!.saved)
        assertTrue(f.controller.current().message.contains("尚未保存"))
    }
    @Test fun cancellationRecoveryAndUnknownRemainDistinct() {
        val f=Fixture(); f.controller.refresh(); f.read(snapshot(1,1))
        assertTrue(f.controller.act(3)); f.submit(); f.read(snapshot(5,1,0,11))
        assertTrue(f.controller.current().message.contains("取消待确认"))
        f.controller.refresh(); f.read(snapshot(9,1,3,11,2,-5,-5))
        assertTrue(f.controller.current().message.contains("结果未知"))
        assertTrue(f.controller.act(4)); f.submit(); f.read(snapshot(9,1,3,11,2,-5,0))
        assertTrue(f.controller.current().message.contains("结果未知"))
        assertFalse(f.controller.act(4))
    }
    @Test fun changedSequenceAndWrongOperationCannotPublishSuccess() {
        val f=Fixture(); f.controller.refresh(); val data=snapshot()
        for(offset in 0..112 step 16) f.reply(bytes=data.copyOfRange(offset,offset+16))
        val tail=data.copyOfRange(112,128);tail[7]=2;f.reply(bytes=tail)
        assertNull(f.controller.current().snapshot)
        f.controller.refresh();f.read(snapshot());f.controller.act(2);f.submit()
        f.read(snapshot(6,1,9,99,1))
        assertTrue(f.controller.current().message.contains("未确认本次操作"))
    }
    @Test fun disconnectAndLateAckNeverReplayAndCloseCannotCancelAnotherEditor() {
        val f=Fixture();f.controller.refresh();f.read(snapshot());f.controller.act(2)
        f.events.closed("lost");val count=f.sent.size
        f.events.result(DeviceControlProtocol.Command.CONFIG_BEGIN,f.ok)
        assertEquals(count,f.sent.size);assertNull(f.controller.current().snapshot)
        assertFalse(f.controller.act(2))
        val other=Fixture();other.controller.refresh();other.read(snapshot())
        assertTrue(other.session.requestPayload(DeviceControlProtocol.Command.CONFIG_BEGIN, ByteBuffer.allocate(8).putInt(7).putInt(32).array()))
        assertFalse(other.controller.act(2));other.controller.close()
        assertFalse(other.sent.any { it.first==DeviceControlProtocol.Command.CONFIG_CANCEL })
    }
    @Test fun unsupportedAndMalformedSnapshotNeverEnableWrites() {
        val f=Fixture();f.controller.refresh();f.reply(-95)
        assertTrue(f.controller.current().message.contains("不支持"));assertFalse(f.controller.act(2))
        f.controller.refresh();f.read(snapshot().apply { this[8]=0;fill(0,9,24) })
        assertNull(f.controller.current().snapshot);assertFalse(f.controller.act(2))
    }
}
