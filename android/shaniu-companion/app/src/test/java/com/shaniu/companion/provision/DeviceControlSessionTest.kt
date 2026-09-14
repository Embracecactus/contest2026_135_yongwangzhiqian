// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test

class DeviceControlSessionTest {
    private val status = DeviceControlProtocol.Snapshot(0, true, false, 53, 0, 0, 0)
    private val failure = DeviceControlProtocol.Snapshot(-5, false, false, null, null, null, null)
    private class Clock {
        var now = 1L
        class Task(val whenMs: Long, val action: () -> Unit) : DeviceControlSession.Cancel {
            var cancelled = false
            override fun cancel() { cancelled = true }
        }
        val tasks = mutableListOf<Task>()
        fun schedule(delay: Long, action: () -> Unit): DeviceControlSession.Cancel = Task(now + delay, action).also { tasks += it }
        fun advance(delta: Long) {
            val until = now + delta
            while (true) {
                val next = tasks.filter { !it.cancelled && it.whenMs <= until }.minByOrNull { it.whenMs } ?: break
                tasks.remove(next); now = next.whenMs; next.action()
            }
            now = until
        }
    }
    private class Sent(val command: DeviceControlProtocol.Command, val value: Int, val accepted: (Boolean) -> Unit)
    private inner class Fixture {
        val clock = Clock()
        val peers = mutableListOf<Peer>()
        var poll = DeviceControlProtocol.Command.STATUS
        val session = DeviceControlSession({ clock.now }, { it() }, clock::schedule, { poll })
        inner class Peer(val events: DeviceControlSession.Events) : DeviceControlSession.Transport {
            val sent = mutableListOf<Sent>()
            var closed = false
            var automaticStatus = false
            override fun request(command: DeviceControlProtocol.Command, value: Int, accepted: (Boolean) -> Unit) {
                check(!closed)
                sent += Sent(command, value, accepted)
                accepted(true)
                if (automaticStatus && command == DeviceControlProtocol.Command.STATUS) reply(command, status)
            }
            override fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit) {
                request(command, 0, accepted)
            }
            override fun close() { closed = true }
            fun reply(command: DeviceControlProtocol.Command, snapshot: DeviceControlProtocol.Snapshot) = events.result(command, snapshot)
        }
        val factory = object : DeviceControlSession.Factory {
            override fun open(events: DeviceControlSession.Events): DeviceControlSession.Transport = Peer(events).also { peers += it }
        }
        val peer get() = peers.last()
        fun connect(initial: DeviceControlProtocol.Snapshot = status) {
            session.setForeground(true)
            session.connect(factory)
            peer.reply(DeviceControlProtocol.Command.STATUS, initial)
        }
    }
    @Test fun tabsSubscribeTwentyTimesWithoutOpeningAnotherConnection() {
        val f = Fixture(); f.connect()
        repeat(20) { repeat(3) { val page = f.session.observe { }; page.cancel() } }
        assertEquals(1, f.peers.size)
        assertTrue(f.session.current().authenticated)
    }
    @Test fun foregroundPollContinuesAcross120Seconds() {
        val f = Fixture(); f.connect(); f.peer.automaticStatus = true
        f.clock.advance(130_000)
        assertEquals(1, f.peers.size)
        assertTrue(f.peer.sent.count { it.command == DeviceControlProtocol.Command.STATUS } >= 60)
        assertTrue(f.session.current().snapshotFresh)
    }
    @Test fun writeWaitsBehindReadAndRequiresQuantizedReadback() {
        val f = Fixture(); f.connect(); f.clock.advance(2000)
        assertTrue(f.session.current().readPending)
        assertFalse(f.session.current().writePending)
        assertTrue(f.session.request(DeviceControlProtocol.Command.VOLUME, 65))
        assertFalse(f.session.request(DeviceControlProtocol.Command.PERSONA, 2))
        assertEquals(listOf(DeviceControlProtocol.Command.STATUS), f.peer.sent.map { it.command })
        assertTrue(f.session.current().writePending)
        f.peer.reply(DeviceControlProtocol.Command.STATUS, status)
        assertEquals(DeviceControlProtocol.Command.VOLUME, f.peer.sent.last().command)
        f.peer.reply(DeviceControlProtocol.Command.VOLUME, status.copy(volume = 67))
        assertEquals(53, f.session.current().snapshot?.volume)
        assertTrue(f.session.current().writePending)
        assertEquals(DeviceControlProtocol.Command.STATUS, f.peer.sent.last().command)
        f.peer.reply(DeviceControlProtocol.Command.STATUS, status.copy(volume = 67))
        assertEquals(67, f.session.current().snapshot?.volume)
        assertFalse(f.session.current().writePending)
        assertEquals("设备已回读确认", f.session.current().operationMessage)
    }
    @Test fun temporaryReadFailurePreservesConnectionAndUnconfirmedWrite() {
        val f = Fixture(); f.connect()
        f.session.request(DeviceControlProtocol.Command.VOLUME, 60)
        f.peer.reply(DeviceControlProtocol.Command.VOLUME, status.copy(volume = 60))
        f.peer.reply(DeviceControlProtocol.Command.STATUS, failure)
        assertTrue(f.session.current().authenticated)
        assertEquals(53, f.session.current().snapshot?.volume)
        assertFalse(f.session.current().snapshotFresh)
        assertTrue(f.session.current().writePending)
        f.clock.advance(2000)
        f.peer.reply(DeviceControlProtocol.Command.STATUS, status.copy(volume = 60))
        assertEquals(60, f.session.current().snapshot?.volume)
        assertFalse(f.session.current().writePending)
    }
    @Test fun ordinaryErrorIsNotDisconnectionAndDoesNotReplaceValue() {
        val f = Fixture(); f.connect()
        f.session.request(DeviceControlProtocol.Command.VOLUME, 60)
        f.peer.reply(DeviceControlProtocol.Command.VOLUME, failure.copy(error = -16))
        assertTrue(f.session.current().authenticated)
        assertTrue(f.session.current().snapshotFresh)
        assertEquals(53, f.session.current().snapshot?.volume)
        assertTrue(f.session.current().operationMessage!!.contains("正在收音"))
    }
    @Test fun infoAndOtaDoNotReplaceStatusAndFragmentsStayContiguous() {
        val f = Fixture(); f.connect(status.copy(infoSupported = true))
        assertEquals(DeviceControlProtocol.Command.INFO, f.peer.sent.single().command)
        val info = DeviceControlProtocol.FirmwareInfo(1, 2, 3, 4, 4)
        f.peer.reply(DeviceControlProtocol.Command.INFO, status.copy(volume = null, firmwareInfo = info))
        val updated = f.session.current().updatedAt
        assertEquals(53, f.session.current().snapshot?.volume)
        assertEquals(info, f.session.current().firmwareInfo)
        val subscription = f.session.observeResults { command, _ ->
            if (command == DeviceControlProtocol.Command.OTA_BEGIN)
                assertTrue(f.session.requestOta(DeviceControlProtocol.Command.OTA_APPEND, byteArrayOf(1)))
        }
        assertTrue(f.session.requestOta(DeviceControlProtocol.Command.OTA_BEGIN, byteArrayOf(0, 0, 0, 44)))
        f.peer.reply(DeviceControlProtocol.Command.OTA_BEGIN, status.copy(volume = null))
        assertEquals(DeviceControlProtocol.Command.OTA_APPEND, f.peer.sent.last().command)
        f.peer.reply(DeviceControlProtocol.Command.OTA_APPEND, status.copy(volume = null))
        assertEquals(updated, f.session.current().updatedAt)
        assertEquals(53, f.session.current().snapshot?.volume)
        subscription.cancel()
        f.clock.advance(2000); f.peer.reply(DeviceControlProtocol.Command.STATUS, status.copy(infoSupported = true))
        assertEquals(1, f.peer.sent.count { it.command == DeviceControlProtocol.Command.INFO })
    }
    @Test fun reconnectDropsWritesAndOldCallbacksCannotPolluteNewState() {
        val f = Fixture(); f.connect(); f.clock.advance(2000)
        val old = f.peer
        val lateAccepted = old.sent.last().accepted
        f.session.request(DeviceControlProtocol.Command.VOLUME, 60)
        old.events.closed("disconnected")
        old.events.closed("duplicate")
        f.clock.advance(1000)
        assertEquals(2, f.peers.size)
        old.reply(DeviceControlProtocol.Command.STATUS, status.copy(volume = 99))
        lateAccepted(false)
        f.peer.reply(DeviceControlProtocol.Command.STATUS, status.copy(volume = 40))
        assertEquals(40, f.session.current().snapshot?.volume)
        assertFalse(f.peer.sent.any { it.command == DeviceControlProtocol.Command.VOLUME })
        assertFalse(f.session.current().writePending)
        f.session.disconnect(); f.peer.events.closed("late")
        f.clock.advance(130_000)
        assertEquals(2, f.peers.size)
    }
    @Test fun failedReconnectHasOneBoundedBackoffSequence() {
        val f = Fixture(); f.connect(); f.peer.events.closed("disconnected")
        for (delay in listOf(1000L, 2000L, 4000L)) {
            f.clock.advance(delay)
            f.peer.events.closed("handshake_timeout")
        }
        f.clock.advance(130_000)
        assertEquals(4, f.peers.size)
        assertEquals(DeviceControlSession.Connection.DISCONNECTED, f.session.current().connection)
    }
    @Test fun otherActivityGraceAndForegroundReturnHaveDifferentPolicies() {
        val f = Fixture(); f.connect()
        f.session.setForeground(false); f.clock.advance(10_000); f.session.setForeground(true)
        assertEquals(1, f.peers.size)
        f.session.setForeground(false); f.clock.advance(30_000)
        assertTrue(f.peer.closed)
        f.session.setForeground(true)
        assertEquals(2, f.peers.size)
        assertFalse(f.session.current().authenticated)
        f.peer.reply(DeviceControlProtocol.Command.STATUS, status)
        assertTrue(f.session.current().authenticated)
        f.session.disconnect(); f.session.setForeground(false); f.clock.advance(30_000); f.session.setForeground(true)
        assertEquals(2, f.peers.size)
    }
    @Test fun graceDisconnectCannotReopenAnExplicitlyClosedSession() {
        val f = Fixture(); f.connect()
        f.session.disconnect()
        f.session.setForeground(false); f.clock.advance(30_000); f.session.setForeground(true)
        assertEquals(1, f.peers.size)
        assertEquals(DeviceControlSession.Connection.SUSPENDED, f.session.current().connection)
    }
}
