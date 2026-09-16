// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.ota

/** Pure UI policy: device acceptance and update completion are separate facts. */
internal object OtaUpdatePolicy {
    const val CONFIRMED_PHASE = 6L

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
    private var held = false
    fun acquire(): Boolean {
        if (held) return false
        held = true
        return true
    }
    fun release() { held = false }
    fun begin(persist: () -> Boolean): Boolean {
        if (!acquire()) return false
        if (persist()) return true
        release()
        return false
    }
}
