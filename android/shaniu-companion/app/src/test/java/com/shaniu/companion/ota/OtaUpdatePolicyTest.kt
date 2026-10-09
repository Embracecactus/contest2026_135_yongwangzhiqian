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
