// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OtaUpdatePolicyTest {
    @Test fun staleOrUnauthenticatedSnapshotCannotAdmitNewOta() {
        assertTrue(OtaUpdatePolicy.mayAdmitStart(true, true, true))
        assertFalse(OtaUpdatePolicy.mayAdmitStart(false, true, true))
        assertFalse(OtaUpdatePolicy.mayAdmitStart(true, false, true))
        assertFalse(OtaUpdatePolicy.mayAdmitStart(true, true, false))
    }

    @Test fun confirmsOnlyFirmwareConfirmedPhase() {
        assertTrue(OtaUpdatePolicy.confirmed("device", "device", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, OtaUpdatePolicy.CONFIRMED_PHASE, 0))
        assertFalse(OtaUpdatePolicy.confirmed("device", "device", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, 7, 0))
        assertFalse(OtaUpdatePolicy.confirmed("device", "other", "1.2.3+4", 9,
            "1.2.3+4", 9, 3, OtaUpdatePolicy.CONFIRMED_PHASE, 0))
    }

    @Test fun terminalMismatchRequiresSameDeviceAndNoAcceptedSource() {
        fun mayDiscard(sourceAccepted: Boolean, device: String = "device", phase: Long? = 6L) =
            OtaUpdatePolicy.mayDiscardTerminalMismatch(sourceAccepted,
                "device", device, "0.7.26+56", 770,
                "0.7.25+55", 769, 3, phase, 0)
        assertTrue(mayDiscard(false))
        assertFalse(mayDiscard(true))
        assertFalse(mayDiscard(false, device = "other"))
        assertFalse(mayDiscard(false, phase = 5L))
        assertFalse(OtaUpdatePolicy.mayDiscardTerminalMismatch(false,
            "device", "device", "0.7.26+56", 770,
            "0.7.25+55", 769, 2, 3, 0))
        assertFalse(OtaUpdatePolicy.mayDiscardTerminalMismatch(false,
            "device", "device", "0.7.26+56", 770,
            "0.7.25+55", 769, null, null, 0))
        assertFalse(OtaUpdatePolicy.mayDiscardTerminalMismatch(false,
            "device", "device", "0.7.26+56", 770,
            "0.7.25+55", 769, 0xffff_ffffL, 0xffff_ffffL, -16))
    }

    @Test fun cancellationKeepsDeviceCommitAndSingleRequestGuards() {
        assertTrue(OtaUpdatePolicy.mayRequestCancellation(true, true, 3L, 6L))
        assertTrue(OtaUpdatePolicy.mayRequestCancellation(false, false, 2L, 1L))
        assertFalse(OtaUpdatePolicy.mayRequestCancellation(false, true, 2L, 1L))
        assertFalse(OtaUpdatePolicy.mayRequestCancellation(false, false, 2L, 3L))
        assertFalse(OtaUpdatePolicy.mayRequestCancellation(false, false, 3L, 6L))
    }

    @Test fun releasedPreparationCannotRunUnderNewLease() {
        val gate = OtaStartGate()
        val old = checkNotNull(gate.acquireLease())
        assertTrue(gate.prepare(old) { error("released start must not run") })
        gate.resume(true)
        gate.release()
        val current = checkNotNull(gate.acquireLease())
        assertFalse(gate.prepare(old) { error("stale lease must not be accepted") })
        assertFalse(gate.release(old))
        var starts = 0
        assertTrue(gate.prepare(current) { starts++; gate.resume(false) })
        gate.resume(false)
        gate.resume(false)
        org.junit.Assert.assertEquals(1, starts)
        assertTrue(gate.release(current))
        val canceled = checkNotNull(gate.acquireLease())
        assertTrue(gate.prepare(canceled) { error("canceled preparation must not run") })
        assertTrue(gate.release(canceled))
        gate.resume(false)
    }

    @Test fun gateRejectsDoubleStartAndFailedPersistence() {
        val gate = OtaStartGate()
        assertFalse(gate.begin { false })
        assertTrue(gate.begin { true })
        assertFalse(gate.begin { true })
        gate.release()
        assertTrue(gate.begin { true })

        val owned = OtaStartGate()
        val stale = checkNotNull(owned.acquireLease())
        owned.release()
        val replacement = checkNotNull(owned.acquireLease())
        assertFalse(owned.release(stale))
        assertFalse(owned.acquire())
        assertTrue(owned.release(replacement))
        assertTrue(owned.acquire())
    }
}
