// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Test

class CloudSettingsTest {
    private fun encode(url: String = "https://cloud.example/v1", key: String = "fixture-only") =
        CloudSettings.encode(url, key.toCharArray(), CloudSettings.Dialect.OPENAI_CHAT_AUDIO,
            "vendor/asr", "vendor/chat", "vendor/tts")

    @Test fun matchesFirmwareLayoutWithGenericModels() {
        val data = encode()
        val b = ByteBuffer.wrap(data)
        assertEquals(0x43434631, b.int)
        assertEquals(1, b.get().toInt()); assertEquals(0, b.get().toInt())
        assertEquals(443, b.short.toInt())
        val fields = listOf("cloud.example", "/v1", "fixture-only", "vendor/asr", "vendor/chat", "vendor/tts")
        fields.forEach { assertEquals(it.length, b.short.toInt()) }
        assertEquals(0, b.int)
        assertArrayEquals(fields.joinToString("").toByteArray(Charsets.US_ASCII), data.copyOfRange(24, data.size))
        data.fill(0)
    }

    @Test fun rejectsUnsafeEndpointAndCredential() {
        for (url in listOf("http://cloud.example/v1", "https://u:p@cloud.example/v1",
            "https://cloud.example/v1?q=x", "https://cloud.example/v1#x",
            "https://cloud.example/../v1", "https://cloud.example/%2e/v1",
            "https://cloud.example:0/v1", "https://-bad.example/v1")) {
            assertThrows(IllegalArgumentException::class.java) { encode(url) }
        }
        for (key in listOf("", "a\r\nx:y", "x y", "密钥", "x".repeat(4097))) {
            assertThrows(IllegalArgumentException::class.java) { encode(key = key) }
        }
    }

    @Test fun keepsCallerKeyOwnedAndAllowsExplicitTlsPort() {
        val key = CharArray(4096) { 'x' }
        val data = CloudSettings.encode("https://cloud.example:8443/v1", key,
            CloudSettings.Dialect.MIMO, "mimo-v2.5-asr", "mimo-v2.5", "mimo-v2.5-tts")
        assertEquals(8443, ByteBuffer.wrap(data).getShort(6).toInt())
        assertTrue(key.all { it == 'x' })
        key.fill('\u0000'); data.fill(0)
    }

    @Test fun reportsSpecificCloudInputErrors() {
        assertEquals("语音服务 Key 应为 1 至 4096 个可打印 ASCII 字符。",
            CloudSettings.inputError("https://cloud.example/v1", CharArray(0),
                "vendor/asr", "vendor/chat", "vendor/tts"))
        assertEquals("语音服务 HTTPS 地址无效。",
            CloudSettings.inputError("not a URI", "fixture-only".toCharArray(),
                "vendor/asr", "vendor/chat", "vendor/tts"))
    }

    @Test fun rejectsRetiredChatModelsOnOfficialMimoEndpoint() {
        for (chat in listOf("mimo-v2.5", "mimo-v2.5-pro")) {
            val error = CloudSettings.inputError("https://API.XIAOMIMIMO.COM/v1", CharArray(0),
                "mimo-v2.5-asr", chat, "mimo-v2.5-tts", allowMissingKey = true)
            assertTrue(error?.contains("已停用") == true)
            assertThrows(IllegalArgumentException::class.java) {
                CloudSettings.encode(CloudSettings.MIMO_STANDARD_URL, "fixture-only".toCharArray(),
                    CloudSettings.Dialect.MIMO, "mimo-v2.5-asr", chat, "mimo-v2.5-tts")
            }
        }
    }

    @Test fun acceptsCurrentMimoChatAndUnchangedSpeechModels() {
        for (chat in listOf("mimo-v2.6-flash", "mimo-v2.6-pro", "mimo-v2.6-pro-ultraspeed")) {
            assertNull(CloudSettings.inputError(CloudSettings.MIMO_STANDARD_URL, CharArray(0),
                "mimo-v2.5-asr", chat, "mimo-v2.5-tts", allowMissingKey = true))
        }
    }

    @Test fun preservesCustomEndpointModelAliases() {
        assertNull(CloudSettings.inputError("https://cloud.example/v1", "fixture-only".toCharArray(),
            "mimo-v2.5-asr", "mimo-v2.5", "mimo-v2.5-tts"))
    }
}
