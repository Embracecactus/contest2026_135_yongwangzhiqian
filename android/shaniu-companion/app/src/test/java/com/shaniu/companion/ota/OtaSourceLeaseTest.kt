// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import com.shaniu.companion.provision.DeviceControlProtocol
import com.shaniu.companion.provision.DeviceControlSession
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.junit.Assert.*
import org.junit.Test

class OtaSourceLeaseTest {
    private class Source : AutoCloseable {
        val closes = AtomicInteger()
        override fun close() { closes.incrementAndGet() }
    }

    @Test fun acceptedSourceSurvivesDisconnectAndReconnectBeforeFirstOtaStatus() {
        val source = Source()
        val lease = OtaSourceLease<Source> { it.close() }
        assertTrue(lease.publish(source))
        val snapshot = DeviceControlProtocol.Snapshot(0, false, false, null, null, null, null)
        var sent = DeviceControlProtocol.Command.OTA_BEGIN
        var upload: OtaControlUpload? = OtaControlUpload(ByteArray(44)) { command, _ -> sent = command; true }
        assertTrue(upload!!.start())
        while (upload!!.state == OtaControlUpload.State.WAITING) upload!!.response(sent, snapshot)
        assertEquals(OtaControlUpload.State.ACCEPTED, upload!!.state)
        lease.accept()

        var events: DeviceControlSession.Events? = null
        val scheduled = mutableListOf<() -> Unit>()
        val session = DeviceControlSession({ 0L }, { it() }, { _, action ->
            var canceled = false
            scheduled += { if (!canceled) action() }
            object : DeviceControlSession.Cancel { override fun cancel() { canceled = true } }
        })
        session.setForeground(true)
        session.connect(object : DeviceControlSession.Factory {
            override fun open(value: DeviceControlSession.Events): DeviceControlSession.Transport {
                events = value
                return object : DeviceControlSession.Transport {
                    override fun request(command: DeviceControlProtocol.Command, value: Int, accepted: (Boolean) -> Unit) = Unit
                    override fun requestOta(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit) = Unit
                    override fun requestPayload(command: DeviceControlProtocol.Command, payload: ByteArray, accepted: (Boolean) -> Unit) = Unit
                    override fun close() = Unit
                }
            }
        })
        events!!.result(DeviceControlProtocol.Command.STATUS, snapshot)
        var generation = session.current().generation
        var changes = 0
        session.observe { state ->
            if (state.generation != generation) {
                generation = state.generation
                changes++
                upload?.close(); upload = null
                if (!lease.accepted) lease.close()
            }
        }
        events!!.closed("disconnected")
        assertEquals(1, changes)
        scheduled.toList().forEach { it() }
        assertEquals(2, changes)
        assertNull(upload)
        assertSame(source, lease.current())
        assertTrue(lease.accepted)
        assertEquals(0, source.closes.get())
        lease.close()
        assertFalse(lease.accepted)
        assertEquals(1, source.closes.get())
    }

    private fun assertPreparingSourceSurvivesStatus(state: Long) {
        val source = Source()
        val lease = OtaSourceLease<Source> { it.close() }
        // A poll can finish before open, or after open but before OTA_START.
        if (lease.shouldCloseAfterStatus(state, uploadPresent = false)) lease.close()
        assertFalse(lease.accepted)
        assertTrue(lease.publish(source))
        assertFalse(lease.shouldCloseAfterStatus(state, uploadPresent = false))
        assertFalse(lease.shouldCloseAfterStatus(state, uploadPresent = true))
        assertFalse(lease.accepted)
        assertSame(source, lease.current())
        assertEquals(0, source.closes.get())
        lease.close()
    }

    @Test fun preparingSourceSurvivesIdleFromAnInFlightPoll() {
        assertPreparingSourceSurvivesStatus(0)
    }

    @Test fun preparingSourceSurvivesAnOlderTasksTerminalStatus() {
        assertPreparingSourceSurvivesStatus(3)
    }

    @Test fun activeStatusCannotAcceptAPreparingSource() {
        assertPreparingSourceSurvivesStatus(1)
        assertPreparingSourceSurvivesStatus(2)
    }

    @Test fun acceptedSourceClosesOnTerminalEvenBeforeInfoIsAvailable() {
        val source = Source()
        val lease = OtaSourceLease<Source> { it.close() }
        assertTrue(lease.publish(source))
        lease.accept()
        assertTrue(lease.shouldCloseAfterStatus(3, uploadPresent = true))
        if (lease.shouldCloseAfterStatus(3, uploadPresent = true)) lease.close()
        assertNull(lease.current())
        assertFalse(lease.accepted)
        assertEquals(1, source.closes.get())
    }

    @Test fun acceptedSourceSurvivesActiveStatusAndEndsOnIdleAfterReconnect() {
        val source = Source()
        val lease = OtaSourceLease<Source> { it.close() }
        assertTrue(lease.publish(source))
        lease.accept()
        assertFalse(lease.shouldCloseAfterStatus(1, uploadPresent = false))
        assertFalse(lease.shouldCloseAfterStatus(2, uploadPresent = false))
        assertFalse(lease.shouldCloseAfterStatus(0, uploadPresent = true))
        assertSame(source, lease.current())
        assertEquals(0, source.closes.get())
        assertTrue(lease.shouldCloseAfterStatus(0, uploadPresent = false))
        if (lease.shouldCloseAfterStatus(0, uploadPresent = false)) lease.close()
        assertEquals(1, source.closes.get())
    }

    @Test fun previousConfirmedUpdateCannotReleaseANewPreparationOrItsStartGate() {
        val gate = OtaStartGate()
        assertNotNull(gate.acquireLease())
        val source = Source()
        val lease = OtaSourceLease<Source> { it.close() }
        var keepAwake = true
        val oldConfirmed = OtaUpdatePolicy.confirmed("device", "device", "1.2.3+4", 4,
            "1.2.3+4", 4, 3, OtaUpdatePolicy.CONFIRMED_PHASE, 0)
        assertTrue(oldConfirmed)
        fun applyOldConfirmation() {
            if (!OtaSourceLease.mayApplyVerification(lease)) return
            if (oldConfirmed) {
                lease.close()
                gate.release()
                keepAwake = false
            }
        }
        applyOldConfirmation()
        assertFalse(gate.acquire())
        assertTrue(keepAwake)
        assertTrue(lease.publish(source))
        applyOldConfirmation()
        assertFalse(gate.acquire())
        assertTrue(keepAwake)
        assertSame(source, lease.current())
        assertEquals(0, source.closes.get())
        // Once this source is accepted, verification is allowed to release
        // its resources and gate, using the newly persisted expected target.
        lease.accept()
        assertTrue(OtaSourceLease.mayApplyVerification(lease))
        applyOldConfirmation()
        assertFalse(keepAwake)
        assertNull(lease.current())
        assertEquals(1, source.closes.get())
        assertTrue(gate.acquire())
    }

    @Test fun persistedVerificationStillWorksWhenThereIsNoLocalSource() {
        assertTrue(OtaSourceLease.mayApplyVerification(null))
    }

    @Test fun destroyWhileOpenIsRunningLeavesLateCleanupOnTheOpeningWorker() {
        val worker = Executors.newSingleThreadExecutor()
        val opening = CountDownLatch(1)
        val finishOpen = CountDownLatch(1)
        val source = Source()
        val lease = OtaSourceLease<Source> { worker.execute { it.close() } }
        try {
            val future = worker.submit {
                opening.countDown()
                check(finishOpen.await(5, TimeUnit.SECONDS))
                if (!lease.publish(source)) source.close()
            }
            assertTrue(opening.await(5, TimeUnit.SECONDS))
            lease.close()
            worker.shutdown()
            finishOpen.countDown()
            future.get(5, TimeUnit.SECONDS)
            assertTrue(worker.awaitTermination(5, TimeUnit.SECONDS))
            assertNull(lease.current())
            assertEquals(1, source.closes.get())
        } finally {
            finishOpen.countDown()
            worker.shutdownNow()
        }
    }

    @Test fun destroyAfterPublishClosesEvenWhenUiCallbackIsRemoved() {
        val worker = Executors.newSingleThreadExecutor()
        val source = Source()
        val lease = OtaSourceLease<Source> { worker.execute { it.close() } }
        try {
            worker.submit { assertTrue(lease.publish(source)) }.get(5, TimeUnit.SECONDS)
            // onDestroy detaches before shutdown, independently of delivery of
            // the queued UI callback. A late callback sees no source to adopt.
            lease.close()
            worker.shutdown()
            assertTrue(worker.awaitTermination(5, TimeUnit.SECONDS))
            assertNull(lease.current())
            lease.close()
            assertEquals(1, source.closes.get())
        } finally { worker.shutdownNow() }
    }

    @Test fun canceledPreparationCannotReplaceANewerSource() {
        val old = OtaSourceLease<Source> { it.close() }
        old.close()
        val replacement = OtaSourceLease<Source> { it.close() }
        val current = Source()
        assertTrue(replacement.publish(current))
        val late = Source()
        assertFalse(old.publish(late))
        late.close()
        old.close()
        assertSame(current, replacement.current())
        assertFalse(replacement.accepted)
        assertEquals(0, current.closes.get())
        replacement.close()
        assertEquals(1, late.closes.get())
        assertEquals(1, current.closes.get())
    }
}
