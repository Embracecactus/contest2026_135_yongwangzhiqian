// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.*
import org.junit.Test

/** Actual Android controller to native TLS/protocol/store/renderer. The host
 * clock and worker steps are external dependencies; no trial state is mocked.
 */
class ExpressionTrialNativeTlsTest {
    private class Fixture : NativeDisplayTlsFixture() {
        private var operation = 10L
        val trial = ExpressionTrialController(session, { ++operation }) {}
        fun read(): ExpressionTrialController.Snapshot {
            assertTrue(trial.refresh()); pump { !trial.current().busy }
            return requireNotNull(trial.current().snapshot) { trial.current().message }
        }
        fun act(action: Int, name: String? = null): ExpressionTrialController.Snapshot {
            assertTrue(trial.act(action, if (action == 1) 30000 else 0,
                if (action == 1) 2 else 0, name))
            pump { !trial.current().busy }
            return requireNotNull(trial.current().snapshot) { trial.current().message }
        }
        fun start(): List<String> {
            assertEquals(0, read().state)
            val before = stats()
            assertEquals(1, act(1, "shaniu-upload-v1.bkep").state)
            assertTrue(trial.current().message.contains("尚未显示"))
            assertEquals(before, stats())
            external("trial-step")
            assertEquals(3, read().state)
            val rendered = stats()
            assertEquals(before[1], rendered[1]) // No persistence writes.
            assertEquals(before[2].toInt()+2, rendered[2].toInt())
            assertEquals(before[5], rendered[5]); assertEquals(before[6], rendered[6])
            return before
        }
        override fun close() { trial.close(); super.close() }
    }
    @Test fun packExpiryRestoresDefaultWithoutPersistence() {
        Fixture().use { f ->
            val before = f.start()
            f.external("time 30100"); f.external("trial-step")
            val done = f.read(); val after = f.stats()
            assertEquals(6, done.state); assertEquals(0L, done.remainingMs)
            assertEquals(before[1], after[1]); assertEquals(before[5], after[5]); assertEquals(before[6], after[6])
            assertEquals(before[2].toInt()+4, after[2].toInt())
            f.external("trial-step"); assertEquals(6, f.read().state)
            assertEquals(after, f.stats())
        }
    }
    @Test fun packCancellationWaitsForActualRestore() {
        Fixture().use { f ->
            val before = f.start(); val active = f.stats()
            assertEquals(4, f.act(2).state)
            assertTrue(f.trial.current().message.contains("等待设备恢复"))
            assertEquals(active, f.stats())
            f.external("trial-step"); assertEquals(7, f.read().state)
            val canceled = f.stats()
            assertEquals(before[1], canceled[1]); assertEquals(before[5], canceled[5]); assertEquals(before[6], canceled[6])
            assertEquals(before[2].toInt()+4, canceled[2].toInt())
            f.external("time 30100"); f.external("trial-step")
            assertEquals(7, f.read().state); assertEquals(canceled, f.stats())
        }
    }
    @Test fun missingInstalledPackFailsWithoutFallback() {
        Fixture().use { f ->
            f.read(); val before = f.stats()
            assertEquals(1, f.act(1, "missing.bkep").state)
            f.external("trial-step")
            assertEquals(9, f.read().state)
            val failed = f.stats()
            assertEquals(before[1], failed[1]); assertEquals(before[2], failed[2])
            assertEquals(before[5], failed[5]); assertEquals(before[6], failed[6])
            assertEquals(failed[3], failed[4])
        }
    }
    @Test fun newDefaultSupersedesOldTrialAndLateExpiry() {
        Fixture().use { f ->
            f.start(); f.trial.close() // Closing the editor does not cancel the device trial.
            var nonce = 20
            DefaultSelectionController(f.session, { ByteArray(16).also { it[0] = (++nonce).toByte() } }) {}.use { selection ->
                fun read(): DefaultSelectionController.Snapshot {
                    assertTrue(selection.refresh()); f.pump { !selection.current().busy }
                    return requireNotNull(selection.current().snapshot)
                }
                fun act(action: Int, name: String? = null) {
                    assertTrue(selection.act(action, name)); f.pump { !selection.current().busy }
                }
                read(); act(2); f.external("step"); assertEquals(1uL, read().revision)
                act(1, "shaniu-upload-v1.bkep"); f.external("step")
                val saved = read(); val beforeExpiry = f.stats()
                assertTrue(saved.saved && saved.rendered); assertEquals(2uL, saved.revision)
                f.external("time 30100"); f.external("trial-step")
                assertEquals(beforeExpiry, f.stats())
                val state = ExpressionTrialController(f.session, { 99L }) {}
                state.use {
                    assertTrue(it.refresh()); f.pump { !it.current().busy }
                    assertEquals(8, it.current().snapshot?.state)
                }
                assertEquals("shaniu-upload-v1.bkep", f.stats()[6])
            }
        }
    }
}
