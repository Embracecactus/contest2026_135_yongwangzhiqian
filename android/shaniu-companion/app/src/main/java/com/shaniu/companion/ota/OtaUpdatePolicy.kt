// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

/** Pure UI policy: device acceptance and update completion are separate facts. */
internal object OtaUpdatePolicy {
    const val CONFIRMED_PHASE = 6L

    fun mayAdmitStart(authenticated: Boolean, snapshotFresh: Boolean,
                      otaSupported: Boolean): Boolean =
        authenticated && snapshotFresh && otaSupported

    fun confirmed(expectedDeviceId: String, connectedDeviceId: String,
                  expectedVersion: String, expectedCounter: Long,
                  actualVersion: String, actualCounter: Long,
                  state: Long?, phase: Long?, result: Int): Boolean =
        expectedDeviceId == connectedDeviceId && state == 3L &&
            phase == CONFIRMED_PHASE && result == 0 &&
            expectedVersion == actualVersion && expectedCounter == actualCounter

    fun mayStart(packageBoard: String, expectedBoard: String, currentCounter: Long,
                 targetCounter: Long): Boolean = packageBoard == expectedBoard &&
        currentCounter < targetCounter
}

/** Prevents duplicate asynchronous source preparation until its owner releases it. */
internal class OtaStartGate {
    internal class Lease
    private var owner: Lease? = null

    fun acquireLease(): Lease? {
        if (owner != null) return null
        return Lease().also { owner = it }
    }
    fun release(lease: Lease): Boolean {
        if (owner !== lease) return false
        owner = null
        return true
    }
    fun acquire(): Boolean = acquireLease() != null
    /** Invalidates the current owner during explicit connection teardown. */
    fun release() { owner = null }
    fun begin(persist: () -> Boolean): Boolean {
        val lease = acquireLease() ?: return false
        if (persist()) return true
        release(lease)
        return false
    }
}
