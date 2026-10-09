// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceControlPresentationTest {
    private val snapshot = DeviceControlProtocol.Snapshot(0, true, false, 65, null, null, null)

    @Test fun volumeExplainsAuthenticationFreshnessAndBusyGates() {
        val disconnected = DeviceControlPresentation.volume(DeviceControlSession.State())
        assertFalse(disconnected.enabled)
        assertTrue(disconnected.reason.contains("连接"))
        val stale = DeviceControlPresentation.volume(DeviceControlSession.State(
            connection = DeviceControlSession.Connection.CONNECTED, authenticated = true,
            snapshot = snapshot, snapshotFresh = false, error = "读取状态失败"))
        assertFalse(stale.enabled)
        assertTrue(stale.reason.contains("读取状态失败"))
        val busy = DeviceControlPresentation.volume(DeviceControlSession.State(
            connection = DeviceControlSession.Connection.CONNECTED, authenticated = true,
            snapshot = snapshot.copy(busy = true), snapshotFresh = true))
        assertFalse(busy.enabled)
        assertTrue(busy.reason.contains("收音"))
    }

    @Test fun volumeKeepsFreshControlsEnabledDuringReadButLocksWrites() {
        val read = DeviceControlPresentation.volume(DeviceControlSession.State(
            connection = DeviceControlSession.Connection.CONNECTED, authenticated = true,
            snapshot = snapshot, snapshotFresh = true, readPending = true))
        assertTrue(read.enabled)
        assertTrue(read.reason.contains("可调节"))
        val writing = DeviceControlPresentation.volume(DeviceControlSession.State(
            connection = DeviceControlSession.Connection.CONNECTED, authenticated = true,
            snapshot = snapshot, snapshotFresh = true, writePending = true))
        assertFalse(writing.enabled)
        val ready = DeviceControlPresentation.volume(DeviceControlSession.State(
            connection = DeviceControlSession.Connection.CONNECTED, authenticated = true,
            snapshot = snapshot, snapshotFresh = true))
        assertTrue(ready.enabled)
        assertTrue(ready.reason.contains("65"))
        assertTrue(ready.reason.contains("可调节"))
        assertFalse(ready.reason.contains("不可"))
        val unknown = DeviceControlPresentation.volume(DeviceControlSession.State(
            authenticated = true, snapshotFresh = true, snapshot = snapshot.copy(volume = null)))
        assertFalse(unknown.enabled)
        assertTrue(unknown.reason.contains("暂不可用"))
        assertFalse(unknown.reason.contains("连接设备后"))
    }
}
