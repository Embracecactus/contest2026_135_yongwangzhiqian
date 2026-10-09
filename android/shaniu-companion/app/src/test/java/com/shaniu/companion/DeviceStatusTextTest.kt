// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion

import com.shaniu.companion.provision.DeviceControlProtocol
import org.junit.Assert.*
import org.junit.Test

class DeviceStatusTextTest {
    private fun wakePackage(frontend: Int?): ByteArray {
        val header = if (frontend == null) 136 else 140
        val model = byteArrayOf(1, 2, 3, 4)
        val bytes = ByteArray(header + model.size)
        (if (frontend == null) "WKM1" else "WKM2").toByteArray().copyInto(bytes)
        java.nio.ByteBuffer.wrap(bytes, 4, 4).putInt(model.size)
        java.security.MessageDigest.getInstance("SHA-256").digest(model).copyInto(bytes, 8)
        "nihao_openvela".toByteArray().copyInto(bytes, 40)
        "你好openvela".toByteArray().copyInto(bytes, 72)
        if (frontend != null) java.nio.ByteBuffer.wrap(bytes, 136, 4).putInt(frontend)
        model.copyInto(bytes, header)
        return bytes
    }

    @Test fun wakeFrontendIsExplicitAndBounded() {
        assertEquals(1, WakeModelPackage.read(wakePackage(null).inputStream())?.frontendVersion)
        assertEquals(2, WakeModelPackage.read(wakePackage(2).inputStream())?.frontendVersion)
        assertNull(WakeModelPackage.read(wakePackage(3).inputStream()))
        assertNull(WakeModelPackage.read(wakePackage(0).inputStream()))
        assertNull(WakeModelPackage.read(wakePackage(2).copyOf(139).inputStream()))
        val corrupt = wakePackage(2); corrupt[140] = 9
        assertNull(WakeModelPackage.read(corrupt.inputStream()))
    }

    @Test fun modelStatusPreservesLegacyAndRejectsMixedFrontendLayout() {
        for (modern in listOf(false, true)) {
            val header = if (modern) 140 else 136
            val status = ByteArray(12 + header * 2)
            (if (modern) "WKS2" else "WKS1").toByteArray().copyInto(status)
            wakePackage(if (modern) 2 else null).copyOf(header).copyInto(status, 12)
            val parsed = WakeModelPackage.status(status)
            assertNotNull(parsed)
            assertEquals(modern, parsed!!.supportsFrontendV2)
            assertEquals(if (modern) 2 else 1, parsed.active.frontendVersion)
            assertNull(parsed.previous)
            if (modern) {
                status[15] = '1'.code.toByte()
                assertNull(WakeModelPackage.status(status))
            }
        }
    }
    private val configured = DeviceControlProtocol.Snapshot(0, true, false, null, null, 0, 0)
    @Test fun configuredSessionDoesNotClaimWifiOrCloudReachability() {
        assertEquals("手机已连接，设备网络状态待确认", configured.statusText())
        assertEquals("设备 Wi-Fi 未就绪，暂时无法发起云端对话", configured.copy(wifiReady = false).statusText())
        assertEquals("设备已连接 Wi-Fi", configured.copy(wifiReady = true).statusText())
        assertEquals("上次对话未完成，请检查网络和语音服务配置", configured.copy(wifiReady = true, runtimeError = -110).statusText())
        assertEquals("请求被拒绝，请核对语音服务凭据或访问权限", configured.copy(wifiReady = true, runtimeError = -13).statusText())
    }
    @Test fun storageWorkIsNotPresentedAsConversation() {
        assertEquals("正在处理记忆设置", configured.copy(busy = true, memoryPending = true).statusText())
        assertEquals("正在处理这次对话", configured.copy(busy = true).statusText())
        assertEquals("手机已连接，语音服务尚未就绪", configured.copy(ready = false).statusText())
    }
}
