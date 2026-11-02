// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.JsonArray
import com.google.gson.JsonObject
import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class NativeFeatureStoreTest {
    private fun packageBytes(image: ByteArray = byteArrayOf(1, 2, 3), badDigest: Boolean = false): ByteArray {
        val manifest = JsonObject().apply {
            addProperty("id", "breath-coach"); addProperty("name", "呼吸练习"); addProperty("description", "提示呼吸节奏")
            addProperty("version", 1); addProperty("abi", 1); addProperty("target", "bk7258-ap-armv8m")
            addProperty("sha256", if (badDigest) "0".repeat(64) else NativeFeatureStore.sha256(image)); addProperty("image_size", image.size)
            add("permissions", JsonArray().apply { add("display") })
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
