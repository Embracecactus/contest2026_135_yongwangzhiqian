// SPDX-License-Identifier: Apache-2.0
package com.shaniu.companion.provision

import com.google.gson.JsonObject
import com.google.gson.JsonParser
import java.net.URL
import android.os.SystemClock
import java.io.InputStream
import javax.net.ssl.HttpsURLConnection
import java.security.MessageDigest
import java.security.KeyFactory
import java.security.AlgorithmParameters
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Strict, bounded parsing for packages fetched or selected by the user. */
internal object NativeFeatureStore {
    const val MAX_PACKAGE_BYTES = 24_576
    private const val MAX_INDEX_BYTES = 65_536
    private const val FETCH_TIMEOUT_MS = 30_000L
    private const val CONNECT_TIMEOUT_MS = 10_000
    private const val READ_TIMEOUT_MS = 15_000
    private val fetchDeadlineExecutor = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "ShaniuStoreDeadline").apply { isDaemon = true }
    }
    data class Entry(val id: String, val name: String, val description: String, val version: Long,
                     val packageUrl: String, val sha256: String)

    fun installRequest(bytes: ByteArray, expected: Entry? = null): JsonObject {
        require(bytes.size in 1..MAX_PACKAGE_BYTES)
        val pack = JsonParser.parseString(bytes.toString(Charsets.UTF_8)).asJsonObject
        require(pack.keySet() == setOf("format", "manifest", "signature", "image"))
        require(pack["format"].asString == "shaniu.native/1")
        val manifestBytes = Base64.getDecoder().decode(pack["manifest"].asString)
        val image = Base64.getDecoder().decode(pack["image"].asString)
        require(manifestBytes.size in 1..1024 && image.size in 1..16_384)
        require(Base64.getDecoder().decode(pack["signature"].asString).size in 1..80)
        val manifest = JsonParser.parseString(manifestBytes.toString(Charsets.UTF_8)).asJsonObject
        val required = setOf("id", "name", "description", "version", "abi", "target", "sha256", "image_size", "permissions")
        require(manifest.keySet() == required || manifest.keySet() == required + "results")
        require(manifest["id"].asString.matches(Regex("[a-z0-9][a-z0-9-]{0,30}")))
        text(manifest, "name", 64)
        text(manifest, "description", 160)
        integer(manifest, "version", 1, 0xffff_ffffL)
        integer(manifest, "abi", 1, 3)
        require(manifest["target"].asString == "bk7258-ap-armv8m")
        integer(manifest, "image_size", image.size.toLong(), image.size.toLong())
        require(manifest["sha256"].asString == sha256(image))
        require(permissionsValid(manifest["abi"].asInt, manifest["permissions"].asJsonArray.asList().map { it.asString }))
        if (manifest.has("results")) {
            require(manifest["abi"].asInt == 3)
            val results = manifest["results"].asJsonArray
            require(results.size() in 1..4)
            results.forEach { require(it.isJsonPrimitive && it.asJsonPrimitive.isString) }
            for (i in 0 until results.size()) text(results, i, 16)
        }
        if (expected != null) {
            require(manifest["id"].asString == expected.id && manifest["name"].asString == expected.name &&
                manifest["description"].asString == expected.description && manifest["version"].asLong == expected.version)
        }
        return JsonObject().apply { addProperty("op", "install"); add("package", pack) }
    }

    /** Device grant names in canonical order; "display" always first, ABI 1 grants nothing else. */
    val PERMISSIONS = listOf("display", "motion", "haptic", "sound", "clock", "storage")
    private val PERMISSION_LIMIT = mapOf(1 to 1, 2 to 4, 3 to 6)
    private val PERMISSION_LABELS = mapOf("display" to "显示表情", "motion" to "读取动作事件",
        "haptic" to "请求短振动", "sound" to "结束时播放节律", "clock" to "读取本地时间",
        "storage" to "保存少量进度")

    fun permissionsValid(abi: Int, names: List<String>): Boolean {
        val limit = PERMISSION_LIMIT[abi] ?: return false
        if (names.isEmpty() || names.first() != "display") return false
        val order = names.map { PERMISSIONS.indexOf(it) }
        return -1 !in order && order.all { it < limit } && order == order.distinct().sorted()
    }

    /** Daily-schedule days mask (bit 0 Sunday) as a short Chinese label. */
    fun repeatLabel(days: Int): String = when (days) {
        0x7f -> "每天"; 0x3e -> "工作日"; 0x41 -> "周末"
        else -> (0..6).filter { days and (1 shl it) != 0 }
            .joinToString("、", prefix = "每周") { "日一二三四五六"[it].toString() }
    }

    fun permissionLabels(names: List<String>): String =
        names.joinToString("、") { PERMISSION_LABELS[it] ?: it }

    /** Permissions declared by a validated install request's signed manifest. */
    fun requestPermissions(request: JsonObject): List<String> {
        val manifest = Base64.getDecoder().decode(request["package"].asJsonObject["manifest"].asString)
        return JsonParser.parseString(manifest.toString(Charsets.UTF_8)).asJsonObject["permissions"]
            .asJsonArray.map { it.asString }
    }

    /** FNV-1a over the app ID, matching the device's bknative_app_hash(). */
    fun appHash(id: String): Long {
        var hash = 2166136261L
        for (byte in id.toByteArray(Charsets.UTF_8)) {
            hash = ((hash xor (byte.toLong() and 255)) * 16777619L) and 0xffff_ffffL
        }
        return hash
    }

    fun parseIndex(bytes: ByteArray): List<Entry> {
        require(bytes.size in 1..MAX_INDEX_BYTES)
        val root = JsonParser.parseString(bytes.toString(Charsets.UTF_8)).asJsonObject
        require(root.keySet() == setOf("format", "apps") && root["format"].asString == "shaniu.store/1")
        val apps = root["apps"].asJsonArray
        require(apps.size() <= 32)
        val entries = apps.map { raw ->
            val app = raw.asJsonObject
            require(app.keySet() == setOf("id", "name", "description", "version", "package_url", "sha256"))
            val entry = Entry(app["id"].asString, app["name"].asString, app["description"].asString,
                app["version"].asLong, app["package_url"].asString, app["sha256"].asString)
            require(entry.id.matches(Regex("[a-z0-9][a-z0-9-]{0,30}")))
            text(app, "name", 64)
            text(app, "description", 160)
            integer(app, "version", 1, 0xffff_ffffL)
            require(isHttps(entry.packageUrl) && entry.sha256.matches(Regex("[0-9a-f]{64}")))
            entry
        }
        require(entries.map { it.id }.distinct().size == entries.size)
        return entries
    }

    fun fetchIndex(url: String): List<Entry> = parseIndex(fetch(url, MAX_INDEX_BYTES))
    fun validatePublisherKey(bytes: ByteArray) {
        require(bytes.size in 1..256)
        val key = KeyFactory.getInstance("EC").generatePublic(X509EncodedKeySpec(bytes)) as ECPublicKey
        val expected = AlgorithmParameters.getInstance("EC").apply {
            init(ECGenParameterSpec("secp256r1"))
        }.getParameterSpec(ECParameterSpec::class.java)
        require(key.params.curve == expected.curve && key.params.generator == expected.generator &&
            key.params.order == expected.order && key.params.cofactor == expected.cofactor)
    }
    fun fetchPackage(url: String, expectedSha256: String): ByteArray {
        val bytes = fetch(url, MAX_PACKAGE_BYTES)
        require(sha256(bytes) == expectedSha256)
        return bytes
    }
    fun isHttps(value: String): Boolean = try {
        URL(value).let { it.protocol.equals("https", true) && it.host.isNotEmpty() && it.userInfo == null }
    } catch (_: Exception) { false }
    fun sha256(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256").digest(bytes)
        .joinToString("") { "%02x".format(it.toInt() and 255) }

    private fun fetch(value: String, limit: Int): ByteArray {
        require(isHttps(value))
        val deadline = SystemClock.elapsedRealtime() + FETCH_TIMEOUT_MS
        val connection = (URL(value).openConnection() as HttpsURLConnection).apply {
            instanceFollowRedirects = false
            connectTimeout = remainingTimeout(deadline, CONNECT_TIMEOUT_MS)
            readTimeout = remainingTimeout(deadline, READ_TIMEOUT_MS)
        }
        // URLConnection's read timeout is an inactivity bound.  This watchdog
        // also interrupts a connected stream that keeps making slow progress.
        val cancellation = fetchDeadlineExecutor.schedule(
            { connection.disconnect() },
            remainingTimeout(deadline, FETCH_TIMEOUT_MS.toInt()).toLong(),
            TimeUnit.MILLISECONDS,
        )
        return try {
            require(connection.responseCode == HttpsURLConnection.HTTP_OK)
            require(connection.contentLengthLong == -1L || connection.contentLengthLong in 1..limit.toLong())
            connection.inputStream.use { input ->
                readBounded(input, limit, deadline, SystemClock::elapsedRealtime) {
                    connection.readTimeout = remainingTimeout(deadline, READ_TIMEOUT_MS)
                }
            }
        } finally {
            cancellation.cancel(false)
            connection.disconnect()
        }
    }

    /** Every blocking read is bounded by the remaining whole-download budget. */
    internal fun readBounded(
        input: InputStream,
        limit: Int,
        deadlineMs: Long,
        nowMs: () -> Long,
        beforeRead: () -> Unit = {},
    ): ByteArray {
        val result = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(4096)
        while (true) {
            require(nowMs() < deadlineMs)
            beforeRead()
            val count = input.read(buffer)
            // Disconnect may surface as EOF.  It is not a successful complete
            // download if the operation crossed its whole-download deadline.
            require(nowMs() < deadlineMs)
            if (count < 0) break
            require(result.size() + count <= limit)
            result.write(buffer, 0, count)
        }
        return result.toByteArray().also { require(it.isNotEmpty()) }
    }

    private fun remainingTimeout(deadlineMs: Long, capMs: Int): Int {
        val remaining = deadlineMs - SystemClock.elapsedRealtime()
        require(remaining > 0)
        return minOf(remaining, capMs.toLong()).toInt().coerceAtLeast(1)
    }

    private fun text(array: com.google.gson.JsonArray, index: Int, limit: Int) {
        val value = array[index].asString
        require(value.isNotEmpty() && value.toByteArray(Charsets.UTF_8).size <= limit && value.none { it.code < 32 })
    }

    private fun text(objectValue: JsonObject, name: String, limit: Int) {
        val value = objectValue[name].asJsonPrimitive
        require(value.isString && value.asString.isNotEmpty() &&
            value.asString.toByteArray(Charsets.UTF_8).size <= limit &&
            value.asString.none { it.code < 32 })
    }

    private fun integer(objectValue: JsonObject, name: String, minimum: Long, maximum: Long) {
        val value = objectValue[name].asJsonPrimitive
        require(value.isNumber)
        require(value.asBigDecimal.longValueExact() in minimum..maximum)
    }
}
