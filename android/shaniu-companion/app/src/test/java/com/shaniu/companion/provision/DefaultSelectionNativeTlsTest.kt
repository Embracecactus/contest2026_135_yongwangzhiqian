// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test

/** Named consumer: test_pack_trial.py android-default-tls-{save,cancel,recovery}.
 * That driver requires exactly one fresh, unskipped JUnit case. Ordinary JVM
 * runs without the compiled native fixture cannot supply integration evidence.
 * Only ATT delivery, scheduling and hardware sinks are externally controlled.
 */
class DefaultSelectionNativeTlsTest {
    private class Fixture : NativeDisplayTlsFixture() {
        private var nonce = 10
        val controller = DefaultSelectionController(session,
            { ByteArray(16).also { it[0] = (++nonce).toByte() } }) {}
        fun read(): DefaultSelectionController.Snapshot {
            assertTrue(controller.refresh()); pump { !controller.current().busy }
            return requireNotNull(controller.current().snapshot) { controller.current().message }
        }
        fun act(action: Int, name: String? = null): DefaultSelectionController.Snapshot {
            assertTrue(controller.act(action, name)); pump { !controller.current().busy }
            return requireNotNull(controller.current().snapshot) { controller.current().message }
        }
        fun prepare() {
            assertEquals(0, read().state)
            assertFalse(controller.canAct(1))
            assertEquals(1, act(2).state)
            external("step")
            val state = read()
            assertEquals(6, state.state); assertEquals(1uL, state.revision)
            assertEquals("shaniu-default-v1.bkep", state.filename)
        }
        override fun close() { controller.close(); super.close() }
    }

    @Test fun authenticatedNativeSavePreservesAckAndRenderBoundary() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            val pending = f.act(1, "shaniu-upload-v1.bkep")
            assertEquals(1, pending.state); assertFalse(pending.saved); assertFalse(pending.rendered)
            assertEquals(before, f.stats()) // ACK and queries did not touch the store or FB.
            f.external("step")
            val done = f.read(); val after = f.stats()
            assertEquals(6, done.state); assertTrue(done.saved && done.rendered)
            assertEquals(2uL, done.revision); assertEquals("shaniu-upload-v1.bkep", done.filename)
            assertEquals("2", after[5]); assertEquals(done.filename, after[6])
            assertEquals(before[2].toInt() + 2, after[2].toInt()) // Every pixel checked by native sink.
            repeat(3) { assertEquals(6, f.read().state) }
            assertEquals(after, f.stats())
        }
    }
    @Test fun confirmedNativeCancelDoesNotWriteOrRender() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            assertEquals(1, f.act(1, "shaniu-upload-v1.bkep").state)
            val canceled = f.act(3)
            assertEquals(7, canceled.state); assertFalse(canceled.saved || canceled.rendered)
            f.external("step"); assertEquals(7, f.read().state)
            assertEquals(before, f.stats())
            assertFalse(f.controller.canAct(3))
        }
    }
    @Test fun nativeReleaseFailureRemainsUnknownAfterRecovery() {
        Fixture().use { f ->
            f.prepare(); val before = f.stats()
            f.act(1, "shaniu-upload-v1.bkep")
            f.external("fail-unmount"); f.external("step")
            val unknown = f.read()
            assertEquals(9, unknown.state); assertTrue(unknown.saved); assertFalse(unknown.rendered)
            assertEquals(-5, unknown.releaseError); assertFalse(f.controller.canAct(1))
            f.act(4); f.external("step")
            val recovered = f.read(); val after = f.stats()
            assertEquals(9, recovered.state); assertEquals(0, recovered.releaseError)
            assertFalse(recovered.rendered); assertEquals(before[2], after[2])
            assertEquals(after[3], after[4]); assertEquals("2", after[5])
            f.act(2); f.external("step")
            val refreshed = f.read()
            assertEquals(6, refreshed.state); assertTrue(refreshed.refresh)
            assertEquals(2uL, refreshed.revision); assertFalse(refreshed.rendered)
        }
    }
}
