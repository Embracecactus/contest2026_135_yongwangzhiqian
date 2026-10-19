// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

/** Pure UI policy: device acceptance and update completion are separate facts. */
internal object OtaUpdatePolicy {
    const val CONFIRMED_PHASE = 6L

    fun mayAdmitStart(authenticated: Boolean, snapshotFresh: Boolean,
                      otaSupported: Boolean): Boolean =
        authenticated && snapshotFresh && otaSupported

    /** A waiting source upload records cancellation locally until its current
     * ACK. It must not inherit the preceding update's terminal phase or be
     * disabled by its own in-flight fragment. Later cancellation is a request
     * that the device may refuse after publishing its target.
     */
    fun mayRequestCancellation(uploadWaiting: Boolean, writePending: Boolean,
                               state: Long?, phase: Long?): Boolean =
        uploadWaiting || !writePending && (phase == null || phase in 1L..2L || state == 1L)

    fun confirmed(expectedDeviceId: String, connectedDeviceId: String,
                  expectedVersion: String, expectedCounter: Long,
                  actualVersion: String, actualCounter: Long,
                  state: Long?, phase: Long?, result: Int): Boolean =
        expectedDeviceId == connectedDeviceId && state == 3L &&
            phase == CONFIRMED_PHASE && result == 0 &&
            expectedVersion == actualVersion && expectedCounter == actualCounter

    /** A fresh terminal report for this device has no active OTA request. When
     * its confirmed firmware differs from the pending target, it is an older
     * terminal result rather than evidence that the target remains in flight.
     */
    fun mayDiscardTerminalMismatch(sourceAccepted: Boolean,
                                   expectedDeviceId: String, connectedDeviceId: String,
                                   expectedVersion: String, expectedCounter: Long,
                                   actualVersion: String, actualCounter: Long,
                                   state: Long?, phase: Long?, result: Int): Boolean =
        !sourceAccepted && expectedDeviceId == connectedDeviceId &&
            state == 3L && phase == CONFIRMED_PHASE && result == 0 &&
            (expectedVersion != actualVersion || expectedCounter != actualCounter)

    fun mayStart(packageBoard: String, expectedBoard: String, currentCounter: Long,
                 targetCounter: Long): Boolean = packageBoard == expectedBoard &&
        currentCounter < targetCounter
}

/** Prevents duplicate asynchronous source preparation until its owner releases it. */
internal class OtaStartGate {
    internal class Lease
    private var owner: Lease? = null
    private var preparedStart: (() -> Unit)? = null

    fun acquireLease(): Lease? {
        if (owner != null) return null
        return Lease().also { owner = it }
    }
    fun release(lease: Lease): Boolean {
        if (owner !== lease) return false
        owner = null
        preparedStart = null
        return true
    }
    fun acquire(): Boolean = acquireLease() != null
    /** Invalidates the current owner during explicit connection teardown. */
    fun release() { owner = null; preparedStart = null }

    /** Holds the unsent first request while an existing read finishes. The
     * owner must revalidate its source, generation and capability when called.
     */
    fun prepare(lease: Lease, start: () -> Unit): Boolean {
        if (owner !== lease || preparedStart != null) return false
        preparedStart = start
        return true
    }
    fun resume(readPending: Boolean) {
        if (readPending) return
        val start = preparedStart ?: return
        preparedStart = null // Sending publishes session state reentrantly.
        start()
    }
    fun begin(persist: () -> Boolean): Boolean {
        val lease = acquireLease() ?: return false
        if (persist()) return true
        release(lease)
        return false
    }
}
