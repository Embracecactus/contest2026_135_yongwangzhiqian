// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Test

class DeviceSettingsTest {
    @Test fun decodesOptionalApplicationReadbackWithoutChangingPublicSettings() {
        val record = ByteBuffer.allocate(32).apply {
            putInt(0x53434131) // SCA1
            putInt(2) // READY
            putLong(9)
            putLong(9)
            putInt(0)
            putInt(0)
        }.array()

        assertEquals(
            DeviceSettings.Application(DeviceSettings.ApplicationState.READY, 9, 9, 0),
            DeviceSettings.decodeApplication(record),
        )
    }

    @Test fun applicationReadbackDoesNotCallCloudReachabilityReady() {
        val settings = DeviceSettings.Public(2, 9, "00", 0, true, true, true, true,
            443, 1, "wifi", "cloud.example", "/v1", "asr", "chat", "tts")
        assertEquals(DeviceSettings.ApplicationReadback.SAVED,
            DeviceSettings.applicationReadback(settings,
                DeviceSettings.Application(DeviceSettings.ApplicationState.UNKNOWN, 9, 0, 0)))
        assertEquals(DeviceSettings.ApplicationReadback.APPLYING,
            DeviceSettings.applicationReadback(settings,
                DeviceSettings.Application(DeviceSettings.ApplicationState.APPLYING, 9, 9, 0)))
        assertEquals(DeviceSettings.ApplicationReadback.READY,
            DeviceSettings.applicationReadback(settings,
                DeviceSettings.Application(DeviceSettings.ApplicationState.READY, 9, 9, 0)))
        assertEquals(DeviceSettings.ApplicationReadback.FAILED,
            DeviceSettings.applicationReadback(settings,
                DeviceSettings.Application(DeviceSettings.ApplicationState.FAILED, 9, 9, -110)))
        assertEquals(DeviceSettings.ApplicationReadback.UNAVAILABLE,
            DeviceSettings.applicationReadback(settings, null))
    }

    @Test fun unfinishedOrFailedPersistenceIsNeverReportedAsSaved() {
        fun settings(state: Int, result: Int) = DeviceSettings.Public(
            state, 9, "00", result, true, true, true, true,
            443, 1, "wifi", "cloud.example", "/v1", "asr", "chat", "tts",
        )
        val ready = DeviceSettings.Application(
            DeviceSettings.ApplicationState.READY, 9, 9, 0,
        )

        assertNotEquals(DeviceSettings.ApplicationReadback.SAVED,
            DeviceSettings.applicationReadback(settings(1, 0), ready))
        assertNotEquals(DeviceSettings.ApplicationReadback.SAVED,
            DeviceSettings.applicationReadback(settings(3, -5), ready))
        assertNotEquals(DeviceSettings.ApplicationReadback.SAVED,
            DeviceSettings.applicationReadback(settings(4, -115), ready))
        assertEquals(DeviceSettings.ApplicationReadback.SAVE_PENDING,
            DeviceSettings.applicationReadback(settings(1, 0), ready))
        assertEquals(DeviceSettings.ApplicationReadback.SAVE_FAILED,
            DeviceSettings.applicationReadback(settings(3, -5), ready))
        assertEquals(DeviceSettings.ApplicationReadback.SAVE_UNCERTAIN,
            DeviceSettings.applicationReadback(settings(4, -115), ready))
    }
}
