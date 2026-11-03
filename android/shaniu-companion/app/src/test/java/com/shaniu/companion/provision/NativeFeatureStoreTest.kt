// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.JsonArray
import com.google.gson.JsonObject
import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class NativeFeatureStoreTest {
    private fun packageBytes(image: ByteArray = byteArrayOf(1, 2, 3), badDigest: Boolean = false,
                             abi: Int = 1, permissions: List<String> = listOf("display"),
                             results: List<String>? = null): ByteArray {
        val manifest = JsonObject().apply {
            addProperty("id", "breath-coach"); addProperty("name", "呼吸练习"); addProperty("description", "提示呼吸节奏")
            addProperty("version", 1); addProperty("abi", abi); addProperty("target", "bk7258-ap-armv8m")
            addProperty("sha256", if (badDigest) "0".repeat(64) else NativeFeatureStore.sha256(image)); addProperty("image_size", image.size)
            add("permissions", JsonArray().apply { permissions.forEach { add(it) } })
            if (results != null) add("results", JsonArray().apply { results.forEach { add(it) } })
        }.toString().toByteArray()
        return JsonObject().apply {
            addProperty("format", "shaniu.native/1"); addProperty("manifest", Base64.getEncoder().encodeToString(manifest))
            addProperty("signature", Base64.getEncoder().encodeToString(byteArrayOf(9)))
            addProperty("image", Base64.getEncoder().encodeToString(image))
        }.toString().toByteArray()
    }
    @Test fun packageKeepsExactSignedManifestAndRequiresImageDigest() {
        val request = NativeFeatureStore.installRequest(packageBytes())
        assertEquals("install", request["op"].asString)
        assertEquals("shaniu.native/1", request["package"].asJsonObject["format"].asString)
        assertThrows(IllegalArgumentException::class.java) { NativeFeatureStore.installRequest(packageBytes(badDigest = true)) }
    }
    @Test fun abi2DeclaresCanonicalPermissionsLikeTheDevice() {
        val request = NativeFeatureStore.installRequest(packageBytes(abi = 2, permissions = listOf("display", "motion", "haptic")))
        assertEquals(listOf("display", "motion", "haptic"), NativeFeatureStore.requestPermissions(request))
        assertEquals("显示表情、读取动作事件、请求短振动",
            NativeFeatureStore.permissionLabels(NativeFeatureStore.requestPermissions(request)))
        for ((abi, names) in listOf(1 to listOf("display", "motion"), 2 to listOf("motion"),
                2 to listOf("display", "haptic", "motion"), 2 to listOf("display", "motion", "motion"),
                2 to listOf("display", "camera"), 2 to listOf("display", "clock"),
                3 to listOf("display", "storage", "clock"), 4 to listOf("display"))) {
            assertThrows("$abi $names", IllegalArgumentException::class.java) {
                NativeFeatureStore.installRequest(packageBytes(abi = abi, permissions = names))
            }
        }
    }
    @Test fun abi3AddsClockStorageAndResultLabels() {
        val request = NativeFeatureStore.installRequest(packageBytes(abi = 3,
            permissions = listOf("display", "motion", "haptic", "clock", "storage"), results = listOf("完成", "错过")))
        assertEquals("显示表情、读取动作事件、请求短振动、读取本地时间、保存少量进度",
            NativeFeatureStore.permissionLabels(NativeFeatureStore.requestPermissions(request)))
        for (bad in listOf(emptyList(), listOf("a", "b", "c", "d", "e"), listOf("超过十六个字节的标签"), listOf(""))) {
            assertThrows("$bad", IllegalArgumentException::class.java) {
                NativeFeatureStore.installRequest(packageBytes(abi = 3, results = bad))
            }
        }
        assertThrows(IllegalArgumentException::class.java) {
            NativeFeatureStore.installRequest(packageBytes(abi = 2, results = listOf("完成")))
        }
        assertEquals("每天", NativeFeatureStore.repeatLabel(0x7f))
        assertEquals("工作日", NativeFeatureStore.repeatLabel(0x3e))
        assertEquals("周末", NativeFeatureStore.repeatLabel(0x41))
        assertEquals("每周一、三", NativeFeatureStore.repeatLabel(0x0a))
    }
    @Test fun appHashMatchesDeviceFnv1a() {
        // Pinned against bknative_app_hash() in the host firmware test.
        assertEquals(0xbd8bb2f5L, NativeFeatureStore.appHash("shake"))
        assertEquals(2166136261L, NativeFeatureStore.appHash(""))
    }
    @Test fun storeDirectoryAcceptsOnlyHttpsAndBoundedExactRecords() {
        val hash = "a".repeat(64)
        val document = """{"format":"shaniu.store/1","apps":[{"id":"breath-coach","name":"呼吸练习","description":"提示","version":1,"package_url":"https://store.example/app.json","sha256":"$hash"}]}"""
        assertEquals("breath-coach", NativeFeatureStore.parseIndex(document.toByteArray()).single().id)
        assertThrows(IllegalArgumentException::class.java) {
            NativeFeatureStore.parseIndex(document.replace("https://", "http://").toByteArray())
        }
    }
    @Test fun catalogCannotMislabelDownloadedPackage() {
        val expected = NativeFeatureStore.Entry("different-app", "呼吸练习", "提示呼吸节奏", 1,
            "https://store.example/app.json", "a".repeat(64))
        assertThrows(IllegalArgumentException::class.java) { NativeFeatureStore.installRequest(packageBytes(), expected) }
    }
    @Test fun manifestUsesFirmwareLimitsWithoutNumberTruncation() {
        val packageObject = com.google.gson.JsonParser.parseString(packageBytes().toString(Charsets.UTF_8)).asJsonObject
        val manifest = com.google.gson.JsonParser.parseString(String(Base64.getDecoder().decode(packageObject["manifest"].asString))).asJsonObject
        fun encoded(): ByteArray {
            packageObject.addProperty("manifest", Base64.getEncoder().encodeToString(manifest.toString().toByteArray()))
            return packageObject.toString().toByteArray()
        }
        manifest.addProperty("version", 1.5)
        assertThrows(ArithmeticException::class.java) { NativeFeatureStore.installRequest(encoded()) }
        manifest.addProperty("version", 1)
        manifest.addProperty("name", "字".repeat(22))
        assertThrows(IllegalArgumentException::class.java) { NativeFeatureStore.installRequest(encoded()) }
        assertFalse(NativeFeatureStore.isHttps("https://user:password@store.example/app.json"))
    }
}
